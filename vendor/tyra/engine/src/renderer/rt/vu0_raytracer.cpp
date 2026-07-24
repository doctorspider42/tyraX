/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: VU0 micromode ray tracer (raytraced mirror reflections).
*/

#include <math.h>
#include "renderer/rt/vu0_raytracer.hpp"
#include "debug/debug.hpp"

// The microprogram, assembled by vclpp/vcl/dvp-as like the VU1 programs
// (see Makefile.base) - but uploaded to VU0 micro memory by the EE below,
// not DMA'd through VIF1.
extern u32 Vu0RtKernel_CodeStart __attribute__((section(".vudata")));
extern u32 Vu0RtKernel_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

namespace {

// VU0 micro/data memory as seen from the EE (identity-mapped, uncached).
// EE access is only legal while VU0 is idle (VPU STAT VBS0 == 0) - every
// touch below is bracketed by vu0WaitIdle().
volatile u32* const kVu0Micro = reinterpret_cast<volatile u32*>(0x11000000);
volatile u32* const kVu0Data = reinterpret_cast<volatile u32*>(0x11004000);
volatile u64* const kVu0Data64 = reinterpret_cast<volatile u64*>(0x11004000);

// Data-memory layout (must match the kernel header comment).
constexpr int kOutBase = 40;
constexpr int kTriBase = 104;

/** Spin until the VU0 microprogram ends (VPU STAT bit 0 = VBS0). */
inline void vu0WaitIdle() {
  u32 stat = 0;
  int spins = 0;
  do {
    asm volatile("cfc2 %0, $29" : "=r"(stat));
    // A 64-texel row is tens of microseconds of VU0 time; millions of
    // spins mean the kernel ran off the rails (bad upload, E-bit lost).
    TYRA_ASSERT(++spins < 8000000, "VU0 rt kernel timeout (VPU STAT stuck)");
  } while (stat & 1);
}

inline void storeQ(int qwAddr, const float* v) {
  const u32* s = reinterpret_cast<const u32*>(v);
  volatile u32* d = kVu0Data + qwAddr * 4;
  d[0] = s[0];
  d[1] = s[1];
  d[2] = s[2];
  d[3] = s[3];
}

inline void storeQi(int qwAddr, u32 x, u32 y, u32 z, u32 w) {
  volatile u32* d = kVu0Data + qwAddr * 4;
  d[0] = x;
  d[1] = y;
  d[2] = z;
  d[3] = w;
}

/**
 * Nearest-neighbor sample of a loaded texture's EE-RAM pixels. Returns
 * packed RGBA32 (alpha forced to 0x80). Understands the PNG loader's
 * storage: 32/24bpp linear, 8bpp indices + CSM1-rotated 256-entry CLUT
 * (the rotation is an involution - applying it again recovers the entry),
 * 4bpp nibble-swapped indices + linear 16-entry CLUT.
 */
u32 sampleTextureNearest(const Texture* t, float u, float v) {
  const TextureData* c = t->core;
  if (c == nullptr || c->data == nullptr) return 0x80808080;
  u -= floorf(u);
  v -= floorf(v);
  int x = static_cast<int>(u * c->width);
  int y = static_cast<int>(v * c->height);
  if (x >= c->width) x = c->width - 1;
  if (y >= c->height) y = c->height - 1;
  const unsigned char* d = c->data;
  const int i = y * c->width + x;
  switch (c->bpp) {
    case bpp32:
      return d[i * 4] | (d[i * 4 + 1] << 8) | (d[i * 4 + 2] << 16) |
             0x80000000U;
    case bpp24:
      return d[i * 3] | (d[i * 3 + 1] << 8) | (d[i * 3 + 2] << 16) |
             0x80000000U;
    case bpp8: {
      if (t->clut == nullptr || t->clut->data == nullptr) break;
      int p = d[i];
      if ((p & 0x18) == 0x08)
        p += 8;
      else if ((p & 0x18) == 0x10)
        p -= 8;
      const unsigned char* e = t->clut->data + p * 4;
      return e[0] | (e[1] << 8) | (e[2] << 16) | 0x80000000U;
    }
    case bpp4: {
      if (t->clut == nullptr || t->clut->data == nullptr) break;
      const unsigned char b = d[i >> 1];
      const int p = (x & 1) ? (b >> 4) & 0xF : b & 0xF;
      const unsigned char* e = t->clut->data + p * 4;
      return e[0] | (e[1] << 8) | (e[2] << 16) | 0x80000000U;
    }
    default:
      break;
  }
  return 0x80808080;
}

}  // namespace

void Vu0Raytracer::init() {
  if (uploaded) return;
  vu0WaitIdle();

  const u32 words = static_cast<u32>(&Vu0RtKernel_CodeEnd -
                                     &Vu0RtKernel_CodeStart);
  TYRA_ASSERT(words > 0 && words * 4 <= 4096,
              "VU0 rt kernel does not fit in 4KB of VU0 micro memory!");

  const u32* src = &Vu0RtKernel_CodeStart;
  for (u32 i = 0; i < words; i++) kVu0Micro[i] = src[i];
  asm volatile("sync.l");

  uploaded = true;
  TYRA_LOG("VU0 ray tracing kernel uploaded (", static_cast<int>(words * 4),
           " bytes)");
}

void Vu0Raytracer::setEye(const Vec4& eyeMirrored) {
  eye[0] = eyeMirrored.x;
  eye[1] = eyeMirrored.y;
  eye[2] = eyeMirrored.z;
}

void Vu0Raytracer::setLight(const Vec4& dirTowardLight) {
  light[0] = dirTowardLight.x;
  light[1] = dirTowardLight.y;
  light[2] = dirTowardLight.z;
}

void Vu0Raytracer::setSky(const Color& top, const Color& bottom) {
  skyTop[0] = top.r;
  skyTop[1] = top.g;
  skyTop[2] = top.b;
  skyBot[0] = bottom.r;
  skyBot[1] = bottom.g;
  skyBot[2] = bottom.b;
}

void Vu0Raytracer::setSpheres(const Vu0RtSphere* spheres, int count) {
  sphereCount = count > MaxSpheres ? MaxSpheres : (count < 0 ? 0 : count);
  for (int i = 0; i < sphereCount; i++) {
    sph[i][0] = spheres[i].center.x;
    sph[i][1] = spheres[i].center.y;
    sph[i][2] = spheres[i].center.z;
    sph[i][3] = spheres[i].radius * spheres[i].radius;
    sphCol[i][0] = spheres[i].color.r;
    sphCol[i][1] = spheres[i].color.g;
    sphCol[i][2] = spheres[i].color.b;
    sphCol[i][3] = 0.0F;
  }
}

void Vu0Raytracer::setBoxes(const Vu0RtBox* boxes, int count) {
  boxCount = count > MaxBoxes ? MaxBoxes : (count < 0 ? 0 : count);
  for (int i = 0; i < boxCount; i++) {
    boxMin[i][0] = boxes[i].min.x;
    boxMin[i][1] = boxes[i].min.y;
    boxMin[i][2] = boxes[i].min.z;
    boxMin[i][3] = 0.0F;
    boxMax[i][0] = boxes[i].max.x;
    boxMax[i][1] = boxes[i].max.y;
    boxMax[i][2] = boxes[i].max.z;
    boxMax[i][3] = 0.0F;
    boxCol[i][0] = boxes[i].color.r;
    boxCol[i][1] = boxes[i].color.g;
    boxCol[i][2] = boxes[i].color.b;
    boxCol[i][3] = 0.0F;
  }
}

// Records pack group 0 first, then group 1 - set group 0 BEFORE group 1
// each frame (the generated game does; a group-0 resize silently shifts
// group 1's records otherwise).
void Vu0Raytracer::setTriangles(int group, const Vu0RtTriangle* tris,
                                int count, const Texture* texture,
                                const Color& fallback) {
  if (group < 0 || group >= MaxGroups) return;
  const int avail = MaxTriangles - (group == 1 ? grpCount[0] : 0);
  if (count > avail) count = avail;
  if (count < 0) count = 0;
  grpCount[group] = count;
  grpTex[group] = texture;
  grpFallback[group] = static_cast<u32>(fallback.r) |
                       (static_cast<u32>(fallback.g) << 8) |
                       (static_cast<u32>(fallback.b) << 16) | 0x80000000U;

  const int base = group == 1 ? grpCount[0] : 0;
  float cx = 0.0F, cy = 0.0F, cz = 0.0F;
  int kept = 0;
  for (int i = 0; i < count; i++) {
    const Vu0RtTriangle& s = tris[i];
    const float e1[3] = {s.b.x - s.a.x, s.b.y - s.a.y, s.b.z - s.a.z};
    const float e2[3] = {s.c.x - s.a.x, s.c.y - s.a.y, s.c.z - s.a.z};
    // N = e1 x e2 (t + the shade normal); A/B = the dual basis so the
    // kernel gets barycentrics from two dot products: u = (H - v0).A
    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                        e1[2] * e2[0] - e1[0] * e2[2],
                        e1[0] * e2[1] - e1[1] * e2[0]};
    const float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    if (nn < 1e-12F) continue;  // degenerate triangle
    const float inv = 1.0F / nn;
    const float a[3] = {(e2[1] * n[2] - e2[2] * n[1]) * inv,
                        (e2[2] * n[0] - e2[0] * n[2]) * inv,
                        (e2[0] * n[1] - e2[1] * n[0]) * inv};
    const float b[3] = {(n[1] * e1[2] - n[2] * e1[1]) * inv,
                        (n[2] * e1[0] - n[0] * e1[2]) * inv,
                        (n[0] * e1[1] - n[1] * e1[0]) * inv};
    float* r0 = tri[(base + kept) * 4];
    r0[0] = s.a.x, r0[1] = s.a.y, r0[2] = s.a.z, r0[3] = 0.0F;
    float* r1 = tri[(base + kept) * 4 + 1];
    r1[0] = n[0], r1[1] = n[1], r1[2] = n[2], r1[3] = 0.0F;
    float* r2 = tri[(base + kept) * 4 + 2];
    r2[0] = a[0], r2[1] = a[1], r2[2] = a[2], r2[3] = 0.0F;
    float* r3 = tri[(base + kept) * 4 + 3];
    r3[0] = b[0], r3[1] = b[1], r3[2] = b[2], r3[3] = 0.0F;
    float* uv = triUV[base + kept];
    uv[0] = s.ua, uv[1] = s.ub - s.ua, uv[2] = s.uc - s.ua;
    uv[3] = s.va, uv[4] = s.vb - s.va, uv[5] = s.vc - s.va;
    cx += s.a.x + s.b.x + s.c.x;
    cy += s.a.y + s.b.y + s.c.y;
    cz += s.a.z + s.b.z + s.c.z;
    ++kept;
  }
  const int srcCount = count;
  grpCount[group] = kept;
  count = kept;
  // Bounding sphere: vertex-average center, max-distance radius over the
  // ORIGINAL input (a superset of the kept set - degenerates were skipped
  // above but their vertices are real model positions). Cheap and never
  // too small - the kernel only uses it as an early-out.
  if (kept > 0) {
    const float inv = 1.0F / (kept * 3);
    cx *= inv, cy *= inv, cz *= inv;
    float r2 = 0.0F;
    for (int i = 0; i < srcCount; i++) {
      const Vec4* vs[3] = {&tris[i].a, &tris[i].b, &tris[i].c};
      for (int k = 0; k < 3; k++) {
        const float dx = vs[k]->x - cx, dy = vs[k]->y - cy,
                    dz = vs[k]->z - cz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2) r2 = d2;
      }
    }
    grpBound[group][0] = cx;
    grpBound[group][1] = cy;
    grpBound[group][2] = cz;
    grpBound[group][3] = r2 * 1.02F + 0.01F;
  } else {
    grpBound[group][0] = grpBound[group][1] = grpBound[group][2] = 0.0F;
    grpBound[group][3] = 0.0F;  // r^2 = 0: the bound test always misses
  }
}

void Vu0Raytracer::kickRowAndWait() {
  // Drain the EE write buffer so the params land before the kernel reads
  // them, then start the microprogram at instruction 0.
  asm volatile("sync.l");
  asm volatile("vcallms 0" ::: "memory");
  vu0WaitIdle();
}

/** Turn a kernel triangle-hit payload into a shaded, textured pixel. The
 * kernel returns BARYCENTRIC u/v - texture coordinates interpolate here,
 * from the EE-side UV rows (they never enter VU0 data memory). */
u32 Vu0Raytracer::resolveTexel(s32 addr, s32 u4096, s32 v4096, s32 shade) {
  const int slot = (addr - kTriBase) / 4;
  const int group = slot < grpCount[0] ? 0 : 1;
  u32 base;
  if (grpTex[group] != nullptr) {
    const float bu = u4096 * (1.0F / 4096.0F);
    const float bv = v4096 * (1.0F / 4096.0F);
    const float* uv = triUV[slot];
    base = sampleTextureNearest(grpTex[group], uv[0] + bu * uv[1] + bv * uv[2],
                                uv[3] + bu * uv[4] + bv * uv[5]);
  } else
    base = grpFallback[group];
  if (shade < 0) shade = 0;
  if (shade > 255) shade = 255;
  const u32 r = ((base & 0xFF) * shade) >> 8;
  const u32 g = (((base >> 8) & 0xFF) * shade) >> 8;
  const u32 b = (((base >> 16) & 0xFF) * shade) >> 8;
  return r | (g << 8) | (b << 16) | 0x80000000U;
}

void Vu0Raytracer::trace(const Vec4& origin, const Vec4& du, const Vec4& dv,
                         u32* out, int size) {
  TYRA_ASSERT(size > 0 && size <= MaxSize,
              "Vu0Raytracer::trace size must be 1..512");
  init();
  vu0WaitIdle();

  // Frame-static parameters (see the kernel's data-memory contract).
  storeQ(0, eye);
  storeQ(3, light);
  storeQ(4, skyTop);
  storeQ(5, skyBot);
  const float consts2[4] = {0.30F, 0.70F, 0.5F, 1e38F};
  const float consts[4] = {0.01F, 1e-8F, 255.0F, 4096.0F};
  storeQ(7, consts2);
  storeQ(8, consts);
  const int groupsSent =
      grpCount[1] > 0 ? 2 : (grpCount[0] > 0 ? 1 : 0);
  storeQ(9, grpBound[0]);
  storeQ(10, grpBound[1]);
  storeQi(11, kTriBase, static_cast<u32>(grpCount[0]),
          kTriBase + static_cast<u32>(grpCount[0]) * 4,
          static_cast<u32>(grpCount[1]));
  for (int i = 0; i < sphereCount; i++) {
    storeQ(12 + i, sph[i]);
    storeQ(20 + i, sphCol[i]);
  }
  for (int i = 0; i < boxCount; i++) {
    storeQ(28 + i, boxMin[i]);
    storeQ(32 + i, boxMax[i]);
    storeQ(36 + i, boxCol[i]);
  }
  const int triQwords = (grpCount[0] + grpCount[1]) * 4;
  for (int i = 0; i < triQwords; i++) storeQ(kTriBase + i, tri[i]);

  const float duQ[4] = {du.x, du.y, du.z, 0.0F};
  storeQ(2, duQ);

  float rowBase[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  for (int row = 0; row < size; row++) {
    // Rows wider than one batch (size > 64) trace in 64-texel chunks -
    // same kernel, the chunk just starts further along the row. The kernel
    // restarts at instruction 0 every vcallms and re-reads all of data
    // memory, so only the start point and texel count change between kicks.
    for (int cx = 0; cx < size; cx += MaxRowTexels) {
      const int texels = size - cx > MaxRowTexels ? MaxRowTexels : size - cx;
      rowBase[0] = origin.x + dv.x * row + du.x * cx;
      rowBase[1] = origin.y + dv.y * row + du.y * cx;
      rowBase[2] = origin.z + dv.z * row + du.z * cx;
      storeQ(1, rowBase);
      storeQi(6, static_cast<u32>(sphereCount), static_cast<u32>(texels),
              static_cast<u32>(boxCount), static_cast<u32>(groupsSent));

      kickRowAndWait();

      // Pack the chunk. w >= 0 marks a triangle hit: (record address,
      // u*4096, v*4096, shade) - sample the group's texture in RAM and
      // modulate. w < 0 is a direct color: x=r y=g z=b, already clamped.
      u32* dst = out + row * size + cx;
      for (int i = 0; i < texels; i++) {
        const u64 xy = kVu0Data64[(kOutBase + i) * 2];
        const u64 zw = kVu0Data64[(kOutBase + i) * 2 + 1];
        const s32 w = static_cast<s32>(zw >> 32);
        if (w >= 0) {
          dst[i] = resolveTexel(static_cast<s32>(xy & 0xFFFFFFFF),
                                static_cast<s32>(xy >> 32),
                                static_cast<s32>(zw & 0xFFFFFFFF), w);
        } else {
          dst[i] = static_cast<u32>(xy & 0xFF) |
                   (static_cast<u32>(xy >> 32 & 0xFF) << 8) |
                   (static_cast<u32>(zw & 0xFF) << 16) | 0x80000000U;
        }
      }
    }
  }
}

}  // namespace Tyra

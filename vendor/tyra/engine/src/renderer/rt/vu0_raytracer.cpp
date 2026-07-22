/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: VU0 micromode ray tracer (raytraced mirror reflections).
*/

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

constexpr int kOutBase = 32;  // first output qword (kernel contract)

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

void Vu0Raytracer::setFloor(bool enabled, float planeY, float cellSize,
                            float fadeDistance, const Color& a,
                            const Color& b) {
  floorQ[0] = planeY;
  floorQ[1] = cellSize > 0.0001F ? 1.0F / cellSize : 1.0F;
  floorQ[2] = fadeDistance > 0.0001F ? 1.0F / fadeDistance : 0.001F;
  floorQ[3] = enabled ? 1.0F : 0.0F;
  floorA[0] = a.r;
  floorA[1] = a.g;
  floorA[2] = a.b;
  floorB[0] = b.r;
  floorB[1] = b.g;
  floorB[2] = b.b;
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

void Vu0Raytracer::kickRowAndWait() {
  // Drain the EE write buffer so the params land before the kernel reads
  // them, then start the microprogram at instruction 0.
  asm volatile("sync.l");
  asm volatile("vcallms 0" ::: "memory");
  vu0WaitIdle();
}

void Vu0Raytracer::trace(const Vec4& origin, const Vec4& du, const Vec4& dv,
                         u32* out, int size) {
  TYRA_ASSERT(size > 0 && size <= MaxSize,
              "Vu0Raytracer::trace size must be 1..128");
  init();
  vu0WaitIdle();

  // Frame-static parameters (see the kernel's data-memory contract).
  storeQ(0, eye);
  storeQ(3, light);
  storeQ(4, skyTop);
  storeQ(5, skyBot);
  storeQ(6, floorQ);
  storeQ(7, floorA);
  storeQ(8, floorB);
  const float consts2[4] = {0.30F, 0.70F, 0.5F, 1e38F};
  const float consts[4] = {0.01F, 0.0F /* spare */, 255.0F, 1024.0F};
  storeQ(10, consts2);
  storeQ(11, consts);
  for (int i = 0; i < sphereCount; i++) {
    storeQ(12 + i, sph[i]);
    storeQ(20 + i, sphCol[i]);
  }

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
      storeQi(9, static_cast<u32>(sphereCount), static_cast<u32>(texels), 0,
              0);

      kickRowAndWait();

      // Pack the chunk: kernel wrote ftoi0 ints (already clamped 0..255)
      // as x=r y=g z=b per output qword.
      u32* dst = out + row * size + cx;
      for (int i = 0; i < texels; i++) {
        const u64 rg = kVu0Data64[(kOutBase + i) * 2];
        const u64 bw = kVu0Data64[(kOutBase + i) * 2 + 1];
        dst[i] = static_cast<u32>(rg & 0xFF) |
                 (static_cast<u32>(rg >> 32 & 0xFF) << 8) |
                 (static_cast<u32>(bw & 0xFF) << 16) | 0x80000000U;
      }
    }
  }
}

}  // namespace Tyra

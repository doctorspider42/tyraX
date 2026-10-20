// Prototype kernels vs the engine's scalar code, in ONE ELF (dev-vu0-simd).
// 1. MMI 64-byte equality vs memcmp (the StaPip transform-cache check).
// 2. VU0 frustumCheckAABB over structure-of-arrays planes vs a verbatim copy
//    of CoreBBox::frustumCheckAABB: an oracle (classification + crossing
//    mask) and cycles per call.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "debug/debug.hpp"
#include "math/m4x4.hpp"
#include "math/plane.hpp"
#include "math/vec4.hpp"

using namespace Tyra;

namespace {

inline unsigned int ticks() {
  unsigned int t;
  asm volatile("mfc0 %0, $9" : "=r"(t));
  return t;
}

unsigned int rng = 0xC0FFEEu;
unsigned int nextU() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
float frand(float lo, float hi) {
  return lo + (hi - lo) * ((nextU() >> 8) * (1.0F / 16777216.0F));
}

void log(const char* fmt, unsigned a = 0, unsigned b = 0, unsigned c = 0,
         unsigned d = 0) {
  char line[200];
  snprintf(line, sizeof line, fmt, a, b, c, d);
  TYRA_LOG(line);
}

// ---------------------------------------------------------------- 1. equal64
typedef float F16[16];
inline bool equal64(const float* a, const float* b) {
  u64 acc;
  asm volatile(
      "lq     $8,  0x00(%1)   \n\t"
      "lq     $9,  0x00(%2)   \n\t"
      "lq     $10, 0x10(%1)   \n\t"
      "lq     $11, 0x10(%2)   \n\t"
      "pxor   $8,  $8,  $9    \n\t"
      "lq     $12, 0x20(%1)   \n\t"
      "lq     $13, 0x20(%2)   \n\t"
      "pxor   $10, $10, $11   \n\t"
      "lq     $14, 0x30(%1)   \n\t"
      "lq     $15, 0x30(%2)   \n\t"
      "pxor   $12, $12, $13   \n\t"
      "pxor   $14, $14, $15   \n\t"
      "por    $8,  $8,  $10   \n\t"
      "por    $12, $12, $14   \n\t"
      "por    $8,  $8,  $12   \n\t"
      "pcpyud $9,  $8,  $0    \n\t"
      "or     %0,  $8,  $9    \n\t"
      : "=r"(acc)
      : "r"(a), "r"(b), "m"(*reinterpret_cast<const F16*>(a)),
        "m"(*reinterpret_cast<const F16*>(b))
      : "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15");
  return acc == 0;
}

M4x4 MA[16], MB[16];

void benchEqual64() {
  // correctness: equal, then one differing byte at every position
  unsigned wrong = 0, cases = 0;
  for (int k = 0; k < 16; k++) MA[0].data[k] = frand(-5, 5);
  MB[0] = MA[0];
  cases++;
  if (equal64(MA[0].data, MB[0].data) != (memcmp(MA[0].data, MB[0].data, 64) == 0))
    wrong++;
  for (int byte = 0; byte < 64; byte++)
    for (int bit = 0; bit < 8; bit++) {
      MB[0] = MA[0];
      reinterpret_cast<unsigned char*>(MB[0].data)[byte] ^= (1u << bit);
      cases++;
      if (equal64(MA[0].data, MB[0].data) !=
          (memcmp(MA[0].data, MB[0].data, 64) == 0))
        wrong++;
    }
  log("KBENCH equal64 correctness: %u cases, %u wrong", cases, wrong);

  // timing: half equal, half differing in the LAST word (memcmp's worst case)
  for (int i = 0; i < 16; i++) {
    for (int k = 0; k < 16; k++) MA[i].data[k] = frand(-5, 5);
    MB[i] = MA[i];
    if (i & 1) MB[i].data[15] += 1.0F;
  }
  unsigned bestM = ~0u, bestE = ~0u;
  volatile int sink = 0;
  for (int run = 0; run < 5; run++) {
    unsigned t0 = ticks();
    for (int r = 0; r < 256; r++)
      for (int i = 0; i < 16; i++)
        sink += memcmp(MA[i].data, MB[i].data, 64) == 0;
    unsigned t1 = ticks();
    for (int r = 0; r < 256; r++)
      for (int i = 0; i < 16; i++) sink += equal64(MA[i].data, MB[i].data);
    unsigned t2 = ticks();
    if (t1 - t0 < bestM) bestM = t1 - t0;
    if (t2 - t1 < bestE) bestE = t2 - t1;
  }
  log("KBENCH equal64 cyc/call x100: memcmp=%u mmi=%u", bestM * 100 / 4096,
      bestE * 100 / 4096);
}

// ------------------------------------------------------- 2. frustum AABB
enum Cls { IN = 0, PART = 1, OUT = 2 };

// verbatim copy of CoreBBox::frustumCheckAABB (core_bbox.cpp)
Cls scalarAABB(const Plane* objectSpacePlanes, const Vec4& min,
               const Vec4& max, u8* crossingMask) {
  const float cx = (min.x + max.x) * 0.5F, ex = (max.x - min.x) * 0.5F;
  const float cy = (min.y + max.y) * 0.5F, ey = (max.y - min.y) * 0.5F;
  const float cz = (min.z + max.z) * 0.5F, ez = (max.z - min.z) * 0.5F;
  Cls result = IN;
  if (crossingMask) *crossingMask = 0;
  for (u8 i = 0; i < 6; i++) {
    const Vec4& n = objectSpacePlanes[i].normal;
    const float centre =
        objectSpacePlanes[i].distance + n.x * cx + n.y * cy + n.z * cz;
    const float radius = fabsf(n.x) * ex + fabsf(n.y) * ey + fabsf(n.z) * ez;
    if (centre + radius <= 0.0F) {
      if (crossingMask) *crossingMask = 0;
      return OUT;
    }
    if (centre - radius <= 0.0F) {
      result = PART;
      if (crossingMask) *crossingMask |= static_cast<u8>(1U << i);
    }
  }
  return result;
}

// Planes as structure-of-arrays: two sets of four lanes (planes 0-3, 4-7),
// per set NX NY NZ D |NX| |NY| |NZ|. Lanes 6 and 7 are "never" planes
// (n = 0, d = 1): centre 1, radius 0, never outside and never crossing.
struct alignas(16) PlanesSoA {
  float q[2][7][4];
};

void toSoA(PlanesSoA* out, const Plane* p) {
  for (int i = 0; i < 8; i++) {
    const int s = i >> 2, l = i & 3;
    const bool real = i < 6;
    const float nx = real ? p[i].normal.x : 0.0F;
    const float ny = real ? p[i].normal.y : 0.0F;
    const float nz = real ? p[i].normal.z : 0.0F;
    out->q[s][0][l] = nx;
    out->q[s][1][l] = ny;
    out->q[s][2][l] = nz;
    out->q[s][3][l] = real ? p[i].distance : 1.0F;
    out->q[s][4][l] = fabsf(nx);
    out->q[s][5][l] = fabsf(ny);
    out->q[s][6][l] = fabsf(nz);
  }
}

typedef float F56[56];
struct alignas(16) Out4 {
  s32 v[4][4];  // P(0-3) N(0-3) P(4-7) N(4-7) as raw bits
};

Cls vu0AABB(const PlanesSoA* planes, const Vec4& min, const Vec4& max,
            u8* crossingMask) {
  Out4 r;
  asm volatile(
      "lqc2      $vf1, %1               \n\t"
      "lqc2      $vf2, %2               \n\t"
      "qmtc2     %4,   $vf5             \n\t"  // $vf5.x = 0.5
      "vadd.xyz  $vf3, $vf1, $vf2       \n\t"
      "vsub.xyz  $vf4, $vf2, $vf1       \n\t"
      "lqc2      $vf6,  0x00(%3)        \n\t"  // set 0: NX NY NZ D |N|
      "lqc2      $vf7,  0x10(%3)        \n\t"
      "lqc2      $vf8,  0x20(%3)        \n\t"
      "lqc2      $vf9,  0x30(%3)        \n\t"
      "vmulx.xyz $vf3, $vf3, $vf5x      \n\t"  // centre of the box
      "vmulx.xyz $vf4, $vf4, $vf5x      \n\t"  // extent
      "lqc2      $vf10, 0x40(%3)        \n\t"
      "lqc2      $vf11, 0x50(%3)        \n\t"
      "lqc2      $vf12, 0x60(%3)        \n\t"
      "vmulaw    $ACC,  $vf9,  $vf0w    \n\t"  // ((d + nx cx) + ny cy) + nz cz
      "vmaddax   $ACC,  $vf6,  $vf3x    \n\t"
      "vmadday   $ACC,  $vf7,  $vf3y    \n\t"
      "vmaddz    $vf13, $vf8,  $vf3z    \n\t"
      "vmulax    $ACC,  $vf10, $vf4x    \n\t"  // (|nx| ex + |ny| ey) + |nz| ez
      "vmadday   $ACC,  $vf11, $vf4y    \n\t"
      "vmaddz    $vf14, $vf12, $vf4z    \n\t"
      "lqc2      $vf6,  0x70(%3)        \n\t"  // set 1
      "lqc2      $vf7,  0x80(%3)        \n\t"
      "lqc2      $vf8,  0x90(%3)        \n\t"
      "lqc2      $vf9,  0xa0(%3)        \n\t"
      "lqc2      $vf10, 0xb0(%3)        \n\t"
      "lqc2      $vf11, 0xc0(%3)        \n\t"
      "lqc2      $vf12, 0xd0(%3)        \n\t"
      "vadd.xyzw $vf15, $vf13, $vf14    \n\t"  // p = centre + radius
      "vsub.xyzw $vf16, $vf13, $vf14    \n\t"  // n = centre - radius
      "vmulaw    $ACC,  $vf9,  $vf0w    \n\t"
      "vmaddax   $ACC,  $vf6,  $vf3x    \n\t"
      "vmadday   $ACC,  $vf7,  $vf3y    \n\t"
      "vmaddz    $vf13, $vf8,  $vf3z    \n\t"
      "vmulax    $ACC,  $vf10, $vf4x    \n\t"
      "vmadday   $ACC,  $vf11, $vf4y    \n\t"
      "vmaddz    $vf14, $vf12, $vf4z    \n\t"
      "sqc2      $vf15, 0x00+%0         \n\t"
      "sqc2      $vf16, 0x10+%0         \n\t"
      "vadd.xyzw $vf15, $vf13, $vf14    \n\t"
      "vsub.xyzw $vf16, $vf13, $vf14    \n\t"
      "sqc2      $vf15, 0x20+%0         \n\t"
      "sqc2      $vf16, 0x30+%0         \n\t"
      : "=m"(r)
      : "m"(min.xyzw), "m"(max.xyzw), "r"(planes), "r"(0.5F),
        "m"(*reinterpret_cast<const F56*>(planes)));
  // x <= 0.0f  <=>  (s32)bits < 1 for every non-NaN float, -0 included
  u32 out = 0, cross = 0;
  for (int i = 0; i < 4; i++) {
    out |= static_cast<u32>(r.v[0][i] < 1) << i;
    cross |= static_cast<u32>(r.v[1][i] < 1) << i;
    out |= static_cast<u32>(r.v[2][i] < 1) << (i + 4);
    cross |= static_cast<u32>(r.v[3][i] < 1) << (i + 4);
  }
  if (out) {
    if (crossingMask) *crossingMask = 0;
    return OUT;
  }
  if (crossingMask) *crossingMask = static_cast<u8>(cross);
  return cross ? PART : IN;
}

constexpr int NSETS = 64;
constexpr int NBOX = 64;
Plane PL[NSETS][6];
PlanesSoA SOA[NSETS];
Vec4 BMIN[NBOX], BMAX[NBOX];

void makePlanes(Plane* p) {
  for (int i = 0; i < 6; i++) {
    Vec4 n(frand(-1, 1), frand(-1, 1), frand(-1, 1), 0.0F);
    if ((nextU() & 15) == 0) n.y = 0.0F;  // axis-aligned components happen
    n.normalize();
    // object space: the model's scale lands in the normal
    const float s = frand(0.05F, 20.0F);
    p[i].normal = Vec4(n.x * s, n.y * s, n.z * s, 1.0F);
    p[i].distance = frand(-60, 60);
  }
}

void makeBox(Vec4* mn, Vec4* mx, int kind) {
  const float cx = frand(-50, 50), cy = frand(-50, 50), cz = frand(-50, 50);
  float ex = frand(0, 10), ey = frand(0, 10), ez = frand(0, 10);
  if (kind == 0) ex = ey = ez = 0.0F;  // degenerate (a point)
  if (kind == 1) ey = 0.0F;            // flat (a quad's box)
  *mn = Vec4(cx - ex, cy - ey, cz - ez, 1.0F);
  *mx = Vec4(cx + ex, cy + ey, cz + ez, 1.0F);
}

// a box whose far or near corner sits exactly on plane 0: the boundary case
void makeTouchingBox(const Plane* p, Vec4* mn, Vec4* mx) {
  makeBox(mn, mx, 2);
  const Vec4& n = p[0].normal;
  const float cx = (mn->x + mx->x) * 0.5F, ex = (mx->x - mn->x) * 0.5F;
  const float cy = (mn->y + mx->y) * 0.5F, ey = (mx->y - mn->y) * 0.5F;
  const float cz = (mn->z + mx->z) * 0.5F, ez = (mx->z - mn->z) * 0.5F;
  const float r = fabsf(n.x) * ex + fabsf(n.y) * ey + fabsf(n.z) * ez;
  const float c = p[0].distance + n.x * cx + n.y * cy + n.z * cz;
  // shift along x so that centre +- radius lands at (about) zero
  const float target = (nextU() & 1) ? -r : r;
  if (n.x != 0.0F) {
    const float dx = (target - c) / n.x;
    mn->x += dx;
    mx->x += dx;
  }
}

void benchFrustum() {
  unsigned trials = 0, clsWrong = 0, maskWrong = 0;
  unsigned hist[3] = {0, 0, 0};
  for (int round = 0; round < 40; round++) {
    for (int s = 0; s < NSETS; s++) {
      makePlanes(PL[s]);
      toSoA(&SOA[s], PL[s]);
    }
    for (int s = 0; s < NSETS; s++)
      for (int b = 0; b < NBOX; b++) {
        Vec4 mn, mx;
        const int kind = b & 7;
        if (kind == 7)
          makeTouchingBox(PL[s], &mn, &mx);
        else
          makeBox(&mn, &mx, kind);
        u8 m0, m1;
        const Cls c0 = scalarAABB(PL[s], mn, mx, &m0);
        const Cls c1 = vu0AABB(&SOA[s], mn, mx, &m1);
        trials++;
        hist[c0]++;
        if (c0 != c1) clsWrong++;
        if (m0 != m1) maskWrong++;
        if ((c0 != c1 || m0 != m1) && clsWrong + maskWrong <= 12) {
          // how close to a plane was it? smallest |centre +- radius| (x1e6)
          const float cx = (mn.x + mx.x) * 0.5F, ex = (mx.x - mn.x) * 0.5F;
          const float cy = (mn.y + mx.y) * 0.5F, ey = (mx.y - mn.y) * 0.5F;
          const float cz = (mn.z + mx.z) * 0.5F, ez = (mx.z - mn.z) * 0.5F;
          float closest = 1e30F;
          for (int i = 0; i < 6; i++) {
            const Vec4& n = PL[s][i].normal;
            const float c = PL[s][i].distance + n.x * cx + n.y * cy + n.z * cz;
            const float r = fabsf(n.x) * ex + fabsf(n.y) * ey + fabsf(n.z) * ez;
            if (fabsf(c + r) < closest) closest = fabsf(c + r);
            if (fabsf(c - r) < closest) closest = fabsf(c - r);
          }
          log("KBENCH mismatch kind=%u scalar=%u/%02x vu0=%u", kind, c0, m0, c1);
          log("KBENCH   vu0mask=%02x closest|p,n|*1e6=%u", m1,
              static_cast<unsigned>(closest * 1e6F));
        }
      }
  }
  log("KBENCH frustum oracle: %u trials, %u class mismatches, %u mask mismatches",
      trials, clsWrong, maskWrong);
  log("KBENCH frustum mix: in=%u partial=%u out=%u", hist[0], hist[1], hist[2]);

  // timing: one plane set per "bag", 64 boxes per set (package classification)
  for (int b = 0; b < NBOX; b++) makeBox(&BMIN[b], &BMAX[b], 2 + (b % 5));
  unsigned bestS = ~0u, bestV = ~0u, bestT = ~0u;
  volatile unsigned sink = 0;
  for (int run = 0; run < 5; run++) {
    unsigned t0 = ticks();
    for (int s = 0; s < NSETS; s++)
      for (int b = 0; b < NBOX; b++) {
        u8 m;
        sink += scalarAABB(PL[s], BMIN[b], BMAX[b], &m) + m;
      }
    unsigned t1 = ticks();
    for (int s = 0; s < NSETS; s++)
      for (int b = 0; b < NBOX; b++) {
        u8 m;
        sink += vu0AABB(&SOA[s], BMIN[b], BMAX[b], &m) + m;
      }
    unsigned t2 = ticks();
    for (int s = 0; s < NSETS; s++) toSoA(&SOA[s], PL[s]);
    unsigned t3 = ticks();
    if (t1 - t0 < bestS) bestS = t1 - t0;
    if (t2 - t1 < bestV) bestV = t2 - t1;
    if (t3 - t2 < bestT) bestT = t3 - t2;
  }
  const unsigned calls = NSETS * NBOX;
  log("KBENCH frustum cyc/call x100: scalar=%u vu0=%u  soa-build/set=%u",
      bestS * 100 / calls, bestV * 100 / calls, bestT / NSETS);
}

}  // namespace

void vu0KernelsBenchRun() {
  benchEqual64();
  benchFrustum();
  TYRA_LOG("KBENCH done");
}

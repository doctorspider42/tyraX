// Added by TyraX: compact, prepared sequential impulses; scalar/VU0 oracles.
#pragma once
#include "convex_collision.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>
#ifndef TYRA_PHYSICS_SOLVER_VU0
#define TYRA_PHYSICS_SOLVER_VU0 0
#endif
namespace Tyra {
// EE malloc need not supply the quadword alignment required by lqc2/sqc2.
// Keep the original allocation immediately before the aligned payload.
template <class T> struct SolverAllocator {
  using value_type = T;
  SolverAllocator() = default;
  template <class U> SolverAllocator(const SolverAllocator<U> &) {}
  T *allocate(size_t n) {
    if (n > (SIZE_MAX - 15 - sizeof(void *)) / sizeof(T))
      throw std::bad_alloc();
    void *raw = ::operator new(n * sizeof(T) + 15 + sizeof(void *));
    uintptr_t addr = (reinterpret_cast<uintptr_t>(raw) + sizeof(void *) + 15) &
                     ~uintptr_t(15);
    reinterpret_cast<void **>(addr)[-1] = raw;
    return reinterpret_cast<T *>(addr);
  }
  void deallocate(T *p, size_t) {
    ::operator delete(reinterpret_cast<void **>(p)[-1]);
  }
};
template <class T, class U>
inline bool operator==(const SolverAllocator<T> &, const SolverAllocator<U> &) {
  return true;
}
template <class T, class U>
inline bool operator!=(const SolverAllocator<T> &, const SolverAllocator<U> &) {
  return false;
}
template <class T> using SolverBuffer = std::vector<T, SolverAllocator<T>>;
struct alignas(16) SolverVector {
  float x, y, z, w;
};
inline SolverVector sv(CollisionV3 v, float w = 0) {
  return {v.x, v.y, v.z, w};
}
inline CollisionV3 xyz(const SolverVector &v) { return {v.x, v.y, v.z}; }
struct alignas(16) SolverBody {
  SolverVector v, w, vp, wp;
  SolverVector invI[3]; // columns, already in VU0 load format
  SolverVector mass;    // x = inverse mass
};
struct alignas(16) SolverContact {
  SolverVector normal;   // w = bounce target
  SolverVector ra;       // w = predicted normal velocity
  SolverVector rb;       // w = penetration depth
  SolverVector angularA; // xyz = I^-1 (ra x n), w = normal denominator
  SolverVector angularB; // xyz = I^-1 (rb x n), w = friction coefficient
  SolverVector
      k[3]; // contact response columns; k[0/1].w = normal/split impulse
  SolverVector friction;
  int a, b;
  int reserved[2];
};
static_assert(sizeof(SolverBody) == 128, "VU0 body offsets");
static_assert(sizeof(SolverContact) == 160, "VU0 contact offsets");
static_assert(offsetof(SolverBody, invI) == 64 &&
                  offsetof(SolverBody, mass) == 112,
              "VU0 body loads");
static_assert(offsetof(SolverContact, ra) == 16 &&
                  offsetof(SolverContact, rb) == 32 &&
                  offsetof(SolverContact, angularA) == 48 &&
                  offsetof(SolverContact, angularB) == 64,
              "VU0 contact loads");
inline CollisionV3 solverMatrix(const SolverVector *m, CollisionV3 v) {
  return {m[0].x * v.x + m[1].x * v.y + m[2].x * v.z,
          m[0].y * v.x + m[1].y * v.y + m[2].y * v.z,
          m[0].z * v.x + m[1].z * v.y + m[2].z * v.z};
}
inline CollisionV3 solverRelative(const SolverBody &a, const SolverBody &b,
                                  const SolverContact &c,
                                  bool position = false) {
  return cvSub(cvAdd(xyz(position ? a.vp : a.v),
                     cvCross(xyz(position ? a.wp : a.w), xyz(c.ra))),
               cvAdd(xyz(position ? b.vp : b.v),
                     cvCross(xyz(position ? b.wp : b.w), xyz(c.rb))));
}
inline CollisionV3 solverResponse(const SolverBody &b, CollisionV3 r,
                                  CollisionV3 p) {
  return cvAdd(cvMul(p, b.mass.x),
               cvCross(solverMatrix(b.invI, cvCross(r, p)), r));
}
inline void prepareSolverContact(SolverContact &c, const SolverBody &a,
                                 const SolverBody &b, float bounce,
                                 float friction, float bounceThreshold) {
  const CollisionV3 n = xyz(c.normal), ra = xyz(c.ra), rb = xyz(c.rb);
  c.angularA = sv(solverMatrix(a.invI, cvCross(ra, n)));
  c.angularB = sv(solverMatrix(b.invI, cvCross(rb, n)));
  const float kn = a.mass.x + cvDot(cvCross(xyz(c.angularA), ra), n) +
                   b.mass.x + cvDot(cvCross(xyz(c.angularB), rb), n);
  c.angularA.w = std::max(kn, 1e-9f);
  c.angularB.w = .05f + .95f * friction;
  c.ra.w = cvDot(solverRelative(a, b, c), n);
  c.normal.w = -c.ra.w > bounceThreshold ? -bounce * c.ra.w : 0;
  for (int i = 0; i < 3; ++i) {
    CollisionV3 e = {float(i == 0), float(i == 1), float(i == 2)};
    c.k[i] = sv(cvAdd(solverResponse(a, ra, e), solverResponse(b, rb, e)));
  }
  c.friction = {0, 0, 0, 0};
}
template <bool VU0>
inline void solverNormal(SolverBody &a, SolverBody &b, const SolverContact &c,
                         float delta, bool position) {
#if defined(_EE)
  if constexpr (VU0) {
    uint32_t bits;
    std::memcpy(&bits, &delta, 4);
    SolverVector *av = position ? &a.vp : &a.v;
    SolverVector *bv = position ? &b.vp : &b.v;
    asm volatile(
        "qmtc2 %[bits], $vf10\n"
        "lqc2 $vf4, 0(%[av])\n lqc2 $vf5, 16(%[av])\n"
        "lqc2 $vf6, 112(%[a])\n lqc2 $vf7, 0(%[c])\n"
        "lqc2 $vf8, 48(%[c])\n lqc2 $vf9, 64(%[c])\n"
        "vmulx.xyz $vf11, $vf7, $vf10\n"
        "vmulx.xyz $vf12, $vf11, $vf6\n vadd.xyz $vf4, $vf4, $vf12\n"
        "vmulx.xyz $vf12, $vf8, $vf10\n vadd.xyz $vf5, $vf5, $vf12\n"
        "sqc2 $vf4, 0(%[av])\n sqc2 $vf5, 16(%[av])\n"
        "lqc2 $vf4, 0(%[bv])\n lqc2 $vf5, 16(%[bv])\n lqc2 $vf6, 112(%[b])\n"
        "vmulx.xyz $vf12, $vf11, $vf6\n vsub.xyz $vf4, $vf4, $vf12\n"
        "vmulx.xyz $vf12, $vf9, $vf10\n vsub.xyz $vf5, $vf5, $vf12\n"
        "sqc2 $vf4, 0(%[bv])\n sqc2 $vf5, 16(%[bv])\n"
        : "+m"(a), "+m"(b)
        : [av] "r"(av), [bv] "r"(bv), [a] "r"(&a), [b] "r"(&b), [c] "r"(&c),
          [bits] "r"(bits)
        : "memory");
    return;
  }
#endif
  SolverVector &av = position ? a.vp : a.v;
  SolverVector &aw = position ? a.wp : a.w;
  SolverVector &bv = position ? b.vp : b.v;
  SolverVector &bw = position ? b.wp : b.w;
  const CollisionV3 p = cvMul(xyz(c.normal), delta);
  av = sv(cvAdd(xyz(av), cvMul(p, a.mass.x)));
  aw = sv(cvAdd(xyz(aw), cvMul(xyz(c.angularA), delta)));
  bv = sv(cvSub(xyz(bv), cvMul(p, b.mass.x)));
  bw = sv(cvSub(xyz(bw), cvMul(xyz(c.angularB), delta)));
}
template <bool VU0>
inline void solverImpulse(SolverBody &a, SolverBody &b, const SolverContact &c,
                          const SolverVector &p) {
#if defined(_EE)
  if constexpr (VU0) {
    asm volatile(
        "lqc2 $vf4, 0(%[p])\n lqc2 $vf5, 16(%[c])\n"
        "vopmula.xyz $ACC, $vf5, $vf4\n vopmsub.xyz $vf6, $vf4, $vf5\n"
        "lqc2 $vf7, 64(%[a])\n lqc2 $vf8, 80(%[a])\n lqc2 $vf9, 96(%[a])\n"
        "vmulax.xyz $ACC, $vf7, $vf6\n vmadday.xyz $ACC, $vf8, $vf6\n "
        "vmaddz.xyz $vf10, $vf9, $vf6\n"
        "lqc2 $vf11, 0(%[a])\n lqc2 $vf12, 16(%[a])\n lqc2 $vf13, 112(%[a])\n"
        "vmulx.xyz $vf14, $vf4, $vf13\n vadd.xyz $vf11, $vf11, $vf14\n"
        "vadd.xyz $vf12, $vf12, $vf10\n sqc2 $vf11, 0(%[a])\n sqc2 $vf12, "
        "16(%[a])\n"
        "lqc2 $vf5, 32(%[c])\n"
        "vopmula.xyz $ACC, $vf5, $vf4\n vopmsub.xyz $vf6, $vf4, $vf5\n"
        "lqc2 $vf7, 64(%[b])\n lqc2 $vf8, 80(%[b])\n lqc2 $vf9, 96(%[b])\n"
        "vmulax.xyz $ACC, $vf7, $vf6\n vmadday.xyz $ACC, $vf8, $vf6\n "
        "vmaddz.xyz $vf10, $vf9, $vf6\n"
        "lqc2 $vf11, 0(%[b])\n lqc2 $vf12, 16(%[b])\n lqc2 $vf13, 112(%[b])\n"
        "vmulx.xyz $vf14, $vf4, $vf13\n vsub.xyz $vf11, $vf11, $vf14\n"
        "vsub.xyz $vf12, $vf12, $vf10\n sqc2 $vf11, 0(%[b])\n sqc2 $vf12, "
        "16(%[b])\n"
        : "+m"(a), "+m"(b)
        : [a] "r"(&a), [b] "r"(&b), [c] "r"(&c), [p] "r"(&p)
        : "memory");
    return;
  }
#endif
  a.v = sv(cvAdd(xyz(a.v), cvMul(xyz(p), a.mass.x)));
  a.w = sv(cvAdd(xyz(a.w), solverMatrix(a.invI, cvCross(xyz(c.ra), xyz(p)))));
  b.v = sv(cvSub(xyz(b.v), cvMul(xyz(p), b.mass.x)));
  b.w = sv(cvSub(xyz(b.w), solverMatrix(b.invI, cvCross(xyz(c.rb), xyz(p)))));
}
template <bool VU0>
inline void solvePrepared(SolverBody *bodies, SolverContact *contacts,
                          int count, int velocityIterations,
                          int positionIterations, float slop, float baumgarte) {
  for (int it = 0; it < velocityIterations; ++it)
    for (int k = 0; k < count; ++k) {
      SolverContact &c = contacts[k];
      SolverBody &a = bodies[c.a];
      SolverBody &b = bodies[c.b];
      float j =
          std::max(0.f, c.k[0].w + (c.normal.w - cvDot(solverRelative(a, b, c),
                                                       xyz(c.normal))) /
                                       c.angularA.w);
      float dj = j - c.k[0].w;
      c.k[0].w = j;
      if (dj != 0)
        solverNormal<VU0>(a, b, c, dj, false);
      CollisionV3 v = solverRelative(a, b, c);
      CollisionV3 vt = cvSub(v, cvMul(xyz(c.normal), cvDot(v, xyz(c.normal))));
      float l2 = cvDot(vt, vt);
      if (l2 < 1e-14f)
        continue;
      float len = std::sqrt(l2);
      CollisionV3 t = cvMul(vt, 1.f / len);
      float kt = cvDot(t, solverMatrix(c.k, t));
      if (kt < 1e-9f)
        continue;
      CollisionV3 nf = cvAdd(xyz(c.friction), cvMul(t, -len / kt));
      float limit = c.angularB.w * c.k[0].w, n2 = cvDot(nf, nf);
      if (n2 > limit * limit)
        nf = cvMul(nf, limit / std::sqrt(n2));
      SolverVector p = sv(cvSub(nf, xyz(c.friction)));
      solverImpulse<VU0>(a, b, c, p);
      c.friction = sv(nf);
    }
  for (int it = 0; it < positionIterations; ++it)
    for (int k = 0; k < count; ++k) {
      SolverContact &c = contacts[k];
      SolverBody &a = bodies[c.a];
      SolverBody &b = bodies[c.b];
      float remaining =
          c.rb.w - (cvDot(solverRelative(a, b, c), xyz(c.normal)) - c.ra.w);
      float target = remaining > slop ? baumgarte * (remaining - slop) : 0;
      float j = std::max(0.f, c.k[1].w +
                                  (target - cvDot(solverRelative(a, b, c, true),
                                                  xyz(c.normal))) /
                                      c.angularA.w);
      float dj = j - c.k[1].w;
      c.k[1].w = j;
      if (dj != 0)
        solverNormal<VU0>(a, b, c, dj, true);
    }
}
inline void solvePhysics(SolverBody *b, SolverContact *c, int n, int vi, int pi,
                         float slop, float baumgarte, bool vu0) {
#if defined(_EE)
  if (vu0) {
    unsigned status;
    do {
      asm volatile("cfc2 %0, $29" : "=r"(status)::"memory");
    } while (status & 1u);
  }
#endif
  if (vu0)
    solvePrepared<true>(b, c, n, vi, pi, slop, baumgarte);
  else
    solvePrepared<false>(b, c, n, vi, pi, slop, baumgarte);
}
} // namespace Tyra

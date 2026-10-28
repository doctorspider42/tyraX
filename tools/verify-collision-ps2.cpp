// Physical-PS2 acceptance probe. Copy into a generated project's scripts and
// call collisionPs2Check() once from Script::init; no engine patches required.
#include <cmath>
#include <cstdio>

#include "physics/broad_phase.hpp"
#ifndef TYRA_COLLISION_VU0
#define TYRA_COLLISION_VU0 1
#endif
#include "physics/collision_math.hpp"
#include "physics/collision_sweep.hpp"
#include "physics/convex_collision.hpp"
#include "physics/triangle_bvh.hpp"

static unsigned collisionTicks() {
#ifdef _EE
  unsigned value;
  asm volatile("mfc0 %0, $9" : "=r"(value));
  return value;
#else
  return 0;
#endif
}
static __attribute__((noinline)) void collisionScalar(const float* m,
                                                      Tyra::CollisionV3 pos,
                                                      const float* local,
                                                      int count,
                                                      Tyra::CollisionV3* out) {
  for (int i = 0; i < count; ++i) {
    const float* v = local + i * 3;
    out[i] = {m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + pos.x,
              m[3] * v[0] + m[4] * v[1] + m[5] * v[2] + pos.y,
              m[6] * v[0] + m[7] * v[1] + m[8] * v[2] + pos.z};
  }
}
int collisionPs2Check() {
  using namespace Tyra;
  int failures = 0;
  float maxError = 0;
  unsigned seed = 931487;
  auto value = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return (float(int((seed >> 8) % 20001) - 10000)) * .001f;
  };
  float local[24 * 3], matrix[9];
  CollisionV3 a[24], b[24];
  for (int round = 0; round < 1000; ++round) {
    for (float& x : local) x = value();
    for (float& x : matrix) x = value() * .1f;
    CollisionV3 pos = {value(), value(), value()};
    int count = round % 2 ? 24 : 8;
    collisionTransformPoints(matrix, pos, local, count, a);
    collisionScalar(matrix, pos, local, count, b);
    for (int i = 0; i < count; ++i) {
      float error = std::fabs(a[i].x - b[i].x) + std::fabs(a[i].y - b[i].y) +
                    std::fabs(a[i].z - b[i].z);
      maxError = std::max(maxError, error);
      if (!std::isfinite(error) || error > 1e-4f) ++failures;
    }
  }
  Tyra::BroadPhase bp;
  bp.begin(128);
  for (int i = 0; i < 128; ++i) {
    BroadBox box;
    for (int k = 0; k < 3; ++k) {
      box.mn[k] = i * 10.f;
      box.mx[k] = i * 10.f + 1;
    }
    bp.set(i, box, true);
  }
  bp.commit();
  BroadBox query;
  for (int k = 0; k < 3; ++k) {
    query.mn[k] = 0;
    query.mx[k] = 1;
  }
  std::vector<int> near;
  bp.queryBox(query, near);
  if (near.size() != 1 || near[0] != 0) ++failures;
  float triangle[] = {-10, -10, 0, 10, -10, 0, 0, 10, 0, 0, 0, 1};
  float fraction = 1;
  CollisionV3 normal;
  if (!sweepSphereTriangle({0, 0, -10}, {0, 0, 10}, .5f, triangle, fraction,
                           normal) ||
      std::fabs(fraction - .475f) > 1e-5f)
    ++failures;
  TriangleBvh tree;
  std::vector<float> tris(triangle, triangle + 12);
  tree.build(tris);
  auto bytes = tree.encode(tris);
  TriangleBvh loaded;
  std::vector<float> decoded;
  if (!loaded.decode(bytes, decoded) || decoded != tris) ++failures;
  printf("COLLISIONTEST failures=%d maxError=%.8f candidates=%u\n", failures,
         maxError, (unsigned)near.size());
  fflush(stdout);
  volatile float checksum = 0;
  for (int count : {8, 24}) {
    unsigned vu[4], scalar[4];
    for (int arm = 0; arm < 4; ++arm) {
      CollisionV3 pos = {1, 2, 3};
      unsigned start = collisionTicks();
      for (int i = 0; i < 1000; ++i) {
        pos.x += .00001f;
        collisionTransformPoints(matrix, pos, local, count, a);
        checksum = checksum + a[i % count].x;
      }
      vu[arm] = collisionTicks() - start;
      pos = {1, 2, 3};
      start = collisionTicks();
      for (int i = 0; i < 1000; ++i) {
        pos.x += .00001f;
        collisionScalar(matrix, pos, local, count, b);
        checksum = checksum + b[i % count].x;
      }
      scalar[arm] = collisionTicks() - start;
    }
    printf(
        "COLLISIONBENCH points=%d vu=%u,%u,%u,%u scalar=%u,%u,%u,%u "
        "checksum=%.3f\n",
        count, vu[0], vu[1], vu[2], vu[3], scalar[0], scalar[1], scalar[2],
        scalar[3], (float)checksum);
    fflush(stdout);
  }
  return failures;
}

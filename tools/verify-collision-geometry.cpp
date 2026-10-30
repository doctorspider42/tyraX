#include <cassert>
#include <cstdio>

#include "physics/collision_math.hpp"
#include "physics/collision_sweep.hpp"
#include "physics/convex_collision.hpp"
using namespace Tyra;
void box(CollisionV3* v, float x, float y, float z, float hx, float hy,
         float hz) {
  for (int i = 0; i < 8; ++i)
    v[i] = {x + ((i & 1) ? hx : -hx), y + ((i & 2) ? hy : -hy),
            z + ((i & 4) ? hz : -hz)};
}
int main() {
  CollisionV3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, a[8], b[8];
  ConvexContact c;
  box(a, 0, 0, 0, 4, .5f, .5f);
  box(b, 0, 0, 0, .5f, .5f, 4);
  assert(convexContact({a, 8, axes, 3, axes, 3}, {b, 8, axes, 3, axes, 3}, c));
  assert(c.depth > 0);
  box(b, 0, 2, 0, .5f, .5f, 4);
  assert(!convexContact({a, 8, axes, 3, axes, 3}, {b, 8, axes, 3, axes, 3}, c));
  float p[] = {-5, 0}, end[] = {5, 0}, mn[] = {-.1f, -1}, mx[] = {.1f, 1}, t,
        sign;
  int axis;
  assert(collisionSweepBox(p, end, mn, mx, 2, t, axis, sign));
  assert(std::fabs(t - .49f) < 1e-6f && axis == 0 && sign == -1);
  float tri[] = {-10, -10, 0, 10, -10, 0, 0, 10, 0, 0, 0, 1};
  CollisionV3 n;
  float best = 1;
  assert(sweepSphereTriangle({0, 0, -10}, {0, 0, 10}, .5f, tri, best, n));
  assert(std::fabs(best - .475f) < 1e-5f && n.z < -.99f);
  best = 1;
  assert(sweepSphereTriangle({0, 0, 10}, {0, 0, -10}, .5f, tri, best, n));
  assert(n.z > .99f);
  best = 1;
  assert(!sweepSphereTriangle({50, 0, -10}, {50, 0, 10}, .5f, tri, best, n));
  // Vertex-only hit and a grazing edge hit, both missed by plane-only sweeps.
  float tr2[] = {0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1};
  best = 1;
  assert(sweepSphereTriangle({-.2f, -.2f, -5}, {-.2f, -.2f, 5}, .5f, tr2, best,
                             n));
  best = 1;
  assert(sweepSphereTriangle({1, -.2f, -5}, {1, -.2f, 5}, .5f, tr2, best, n));
  box(a, 5, 0, 0, .1f, 1, 1);
  box(b, 0, 0, 0, .1f, 1, 1);
  assert(convexSweepContact({a, 8, axes, 3, axes, 3}, {b, 8, axes, 3, axes, 3},
                            {10, 0, 0}, {0, 0, 0}, c));
  assert(c.normal.x < -.99f);
  assert(sphereSweepContact({-5, 0, 0}, {5, 0, 0}, .2f, {0, 0, 0}, {0, 0, 0},
                            .2f, c));
  assert(c.normal.x < -.99f);
  float hv[24];
  box(a, 0, 0, 0, 1, 1, 1);
  for (int i = 0; i < 8; ++i) {
    hv[i * 3] = a[i].x;
    hv[i * 3 + 1] = a[i].y;
    hv[i * 3 + 2] = a[i].z;
  }
  float planes[] = {1, 0,  0, 1, -1, 0, 0, 1, 0, 1, 0,  1,
                    0, -1, 0, 1, 0,  0, 1, 1, 0, 0, -1, 1};
  float edges[66 * 3];
  assert(convexEdges(hv, 8, planes, 6, edges) == 3);
  assert(!sphereConvexContact({1.4f, 1.4f, 0}, .5f, hv, 8, planes, 6, c));
  assert(sphereConvexContact({1.3f, 1.3f, 0}, .5f, hv, 8, planes, 6, c));
  assert(std::fabs(c.depth - (.5f - std::sqrt(.18f))) < 1e-5f);
  float matrix[] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
  CollisionV3 transformed[8];
  collisionTransformPoints(matrix, {1, 2, 3}, hv, 8, transformed);
  for (int i = 0; i < 8; ++i) {
    assert(transformed[i].x == a[i].z + 1);
    assert(transformed[i].y == a[i].y + 2);
    assert(transformed[i].z == -a[i].x + 3);
  }
  puts(
      "convex/SAT and continuous sweeps: crossed hulls, separation, thin wall, "
      "face/edge/vertex hits passed");
}

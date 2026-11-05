// Added by TyraX: continuous sphere versus triangle (face, edges, vertices).
#pragma once
#include "convex_collision.hpp"
namespace Tyra {
inline bool sweepSphereTriangle(CollisionV3 p, CollisionV3 end, float radius,
                                const float* tri, float& best,
                                CollisionV3& normal) {
  CollisionV3 a = {tri[0], tri[1], tri[2]}, b = {tri[3], tri[4], tri[5]},
              c = {tri[6], tri[7], tri[8]}, n = {tri[9], tri[10], tri[11]};
  CollisionV3 v = cvSub(end, p);
  bool hit = false;
  auto accept = [&](float t, CollisionV3 closest) {
    if (t < 0 || t > best || t > 1) return;
    CollisionV3 d = cvSub(cvAdd(p, cvMul(v, t)), closest);
    float l2 = cvDot(d, d);
    if (l2 < 1e-14f) return;
    CollisionV3 nn = cvMul(d, 1 / std::sqrt(l2));
    if (cvDot(v, nn) >= -1e-8f) return;
    best = t;
    normal = nn;
    hit = true;
  };
  auto quadratic = [&](CollisionV3 q, CollisionV3 velocity, auto emit) {
    float aa = cvDot(velocity, velocity), bb = cvDot(q, velocity),
          cc = cvDot(q, q) - radius * radius;
    if (aa < 1e-14f) return;
    float disc = bb * bb - aa * cc;
    if (disc < 0) return;
    float t = (-bb - std::sqrt(disc)) / aa;
    if (t >= 0 && t <= best && t <= 1) emit(t);
  };
  const float dist = cvDot(cvSub(p, a), n), speed = cvDot(v, n);
  if (std::fabs(speed) > 1e-9f)
    for (int side = -1; side <= 1; side += 2) {
      float t = (side * radius - dist) / speed;
      if (t < 0 || t > best || t > 1) continue;
      CollisionV3 q = cvSub(cvAdd(p, cvMul(v, t)), cvMul(n, side * radius));
      if (cvDot(cvCross(cvSub(b, a), cvSub(q, a)), n) >= -1e-5f &&
          cvDot(cvCross(cvSub(c, b), cvSub(q, b)), n) >= -1e-5f &&
          cvDot(cvCross(cvSub(a, c), cvSub(q, c)), n) >= -1e-5f)
        accept(t, q);
    }
  const CollisionV3 verts[3] = {a, b, c};
  for (int i = 0; i < 3; ++i) {
    CollisionV3 x = verts[i], y = verts[(i + 1) % 3];
    quadratic(cvSub(p, x), v, [&](float t) { accept(t, x); });
    CollisionV3 e = cvSub(y, x), q = cvSub(p, x);
    float l2 = cvDot(e, e);
    if (l2 < 1e-14f) continue;
    CollisionV3 qp = cvSub(q, cvMul(e, cvDot(q, e) / l2)),
                vp = cvSub(v, cvMul(e, cvDot(v, e) / l2));
    quadratic(qp, vp, [&](float t) {
      float u = cvDot(cvAdd(q, cvMul(v, t)), e) / l2;
      if (u >= 0 && u <= 1) accept(t, cvAdd(x, cvMul(e, u)));
    });
  }
  return hit;
}
}  // namespace Tyra

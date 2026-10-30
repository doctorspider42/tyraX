// Private experiment: clip an OBB face patch after canonical discrete SAT.
// Collision admission, CCD and edge contacts remain the caller's authority.
#pragma once
#include "physics/convex_collision.hpp"
namespace BoxFaceManifold {
using V = Tyra::CollisionV3;
using Tyra::cvAdd;
using Tyra::cvSub;
using Tyra::cvMul;
using Tyra::cvDot;
struct Point { V n, p; float depth; };

// Return zero for edge axes, invalid dimensions or an empty clip. The caller
// must retain its canonical contact in those cases, never discard the pair.
// Only call for a DISCRETE overlap, not a linear-sweep-only contact.
inline int clip(const Tyra::CollisionObb& a, const Tyra::CollisionObb& b,
                const Tyra::ConvexContact& sat, Point (&out)[16]) {
  V unit[2][3];
  float half[2][3];
  const Tyra::CollisionObb* box[2] = {&a, &b};
  int reference = -1, face = -1;
  float best = 0;
  for (int body = 0; body < 2; ++body) {
    for (int axis = 0; axis < 3; ++axis) {
      const V h = box[body]->halfAxes[axis];
      const float length2 = cvDot(h, h);
      if (!(length2 > 1e-12f) || !std::isfinite(length2)) return 0;
      half[body][axis] = std::sqrt(length2);
      unit[body][axis] = cvMul(h, 1.f / half[body][axis]);
      const float alignment = std::fabs(cvDot(unit[body][axis], sat.normal));
      if (alignment > best) { best = alignment; reference = body; face = axis; }
    }
  }
  if (best < 1.f - 1e-6f || reference < 0) return 0;
  // n pushes A away from B: A's outward reference points -n, B's +n.
  const V outward = reference == 0 ? cvMul(sat.normal, -1.f) : sat.normal;
  const int incident = 1 - reference;
  const float sign = cvDot(unit[reference][face], outward) >= 0 ? 1.f : -1.f;
  const V ref = cvAdd(box[reference]->centre,
                     cvMul(box[reference]->halfAxes[face], sign));
  int incidentFace = 0;
  float incidentBest = -1;
  for (int axis = 0; axis < 3; ++axis) {
    const float alignment = std::fabs(cvDot(unit[incident][axis], outward));
    if (alignment > incidentBest) { incidentBest = alignment; incidentFace = axis; }
  }
  const float incidentSign = cvDot(unit[incident][incidentFace], outward) >= 0 ? -1.f : 1.f;
  const V centre = cvAdd(box[incident]->centre,
                        cvMul(box[incident]->halfAxes[incidentFace], incidentSign));
  const V u = box[incident]->halfAxes[(incidentFace + 1) % 3];
  const V v = box[incident]->halfAxes[(incidentFace + 2) % 3];
  // A clipped quadrilateral against a rectangle has at most eight vertices.
  // Spare storage handles roundoff; overflow returns to the canonical contact.
  V polygon[2][16];
  polygon[0][0] = cvSub(cvSub(centre, u), v);
  polygon[0][1] = cvSub(cvAdd(centre, u), v);
  polygon[0][2] = cvAdd(cvAdd(centre, u), v);
  polygon[0][3] = cvAdd(cvSub(centre, u), v);
  int count = 4, source = 0;
  for (int tangent = 1; tangent <= 2; ++tangent) {
    const int axis = (face + tangent) % 3;
    for (int side = -1; side <= 1; side += 2) {
      const V plane = cvMul(unit[reference][axis], float(side));
      const float limit = half[reference][axis];
      int nextCount = 0;
      auto append = [&](V p) {
        if (nextCount >= 16) return false;
        polygon[1-source][nextCount++] = p;
        return true;
      };
      for (int k = 0; k < count; ++k) {
        const V p = polygon[source][k], q = polygon[source][(k+1)%count];
        const float dp = limit - cvDot(plane, cvSub(p, ref));
        const float dq = limit - cvDot(plane, cvSub(q, ref));
        if (!std::isfinite(dp) || !std::isfinite(dq)) return 0;
        if (dp >= 0 && !append(p)) return 0;
        if ((dp >= 0) != (dq >= 0)) {
          // Opposite signs guarantee a nonzero denominator. Do not replace
          // this branch with a mask multiplied by an unsafe reciprocal.
          const float t = dp / (dp - dq);
          if (!append(cvAdd(p, cvMul(cvSub(q, p), t)))) return 0;
        }
      }
      count = nextCount; source = 1-source;
      if (!count) return 0;
    }
  }
  int result = 0;
  for (int k = 0; k < count; ++k) {
    const V p = polygon[source][k];
    const float depth = cvDot(outward, cvSub(ref, p));
    if (!std::isfinite(depth) || depth < 0) continue;
    // Midpoint between the incident point and its reference-plane projection:
    // one shared world point preserves equal/opposite impulse application.
    out[result++] = {sat.normal, cvAdd(p, cvMul(outward, .5f * depth)), depth};
  }
  return result;
}
} // namespace BoxFaceManifold

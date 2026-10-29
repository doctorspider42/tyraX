// Added by TyraX: portable convex SAT and segment sweeps, shared with host
// oracles.
#pragma once
#include <algorithm>
#include <cmath>
namespace Tyra {
struct CollisionV3 {
  float x, y, z;
};
inline CollisionV3 cvAdd(CollisionV3 a, CollisionV3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline CollisionV3 cvSub(CollisionV3 a, CollisionV3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline CollisionV3 cvMul(CollisionV3 a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}
inline float cvDot(CollisionV3 a, CollisionV3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline CollisionV3 cvCross(CollisionV3 a, CollisionV3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
// A convex polyhedron with <=24 vertices has <=66 edges. Store unique edge
// directions once when building a shape, not inside every pair's SAT test.
inline int convexEdges(const float* verts, int nv, const float* planes, int np,
                       float* out) {
  int count = 0;
  float extent = 1;
  for (int i = 0; i < nv * 3; ++i)
    extent = std::max(extent, std::fabs(verts[i]));
  const float eps = extent * 1e-4f;
  for (int a = 0; a < nv; ++a)
    for (int b = a + 1; b < nv; ++b) {
      CollisionV3 va = {verts[a * 3], verts[a * 3 + 1], verts[a * 3 + 2]},
                  vb = {verts[b * 3], verts[b * 3 + 1], verts[b * 3 + 2]};
      CollisionV3 first = {0, 0, 0};
      bool face = false, edge = false;
      for (int f = 0; f < np; ++f) {
        const float* p = planes + f * 4;
        CollisionV3 n = {p[0], p[1], p[2]};
        if (std::fabs(cvDot(n, va) - p[3]) > eps ||
            std::fabs(cvDot(n, vb) - p[3]) > eps)
          continue;
        if (!face) {
          first = n;
          face = true;
        } else if (cvDot(cvCross(first, n), cvCross(first, n)) > 1e-8f) {
          edge = true;
          break;
        }
      }
      if (!edge) continue;
      CollisionV3 d = cvSub(vb, va);
      float len = std::sqrt(cvDot(d, d));
      if (len < eps) continue;
      d = cvMul(d, 1 / len);
      bool duplicate = false;
      for (int e = 0; e < count; ++e) {
        CollisionV3 v = {out[e * 3], out[e * 3 + 1], out[e * 3 + 2]};
        if (std::fabs(cvDot(v, d)) > .99999f) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate && count < 66) {
        out[count * 3] = d.x;
        out[count * 3 + 1] = d.y;
        out[count * 3 + 2] = d.z;
        ++count;
      }
    }
  return count;
}
struct ConvexView {
  const CollisionV3* verts;
  int nv;
  const CollisionV3* normals;
  int nn;
  const CollisionV3* edges;
  int ne;
};
struct ConvexContact {
  CollisionV3 normal, point;
  float depth;
};
// Conservative for the fixed-orientation linear sweep used below. Include
// end vertices translated back to their sweep start, not just the previous
// orientation's vertices: rotation can otherwise hide a linear CCD candidate.
struct CollisionBounds {
  CollisionV3 mn, mx;
};
inline CollisionBounds convexMotionBounds(const CollisionV3* v, int nv,
                                         CollisionV3 motion) {
  CollisionBounds b = {{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
  for (int i = 0; i < nv; ++i) {
    b.mn.x = std::min(b.mn.x, v[i].x);
    b.mn.y = std::min(b.mn.y, v[i].y);
    b.mn.z = std::min(b.mn.z, v[i].z);
    b.mx.x = std::max(b.mx.x, v[i].x);
    b.mx.y = std::max(b.mx.y, v[i].y);
    b.mx.z = std::max(b.mx.z, v[i].z);
  }
  b.mn.x -= std::max(0.f, motion.x);
  b.mn.y -= std::max(0.f, motion.y);
  b.mn.z -= std::max(0.f, motion.z);
  b.mx.x -= std::min(0.f, motion.x);
  b.mx.y -= std::min(0.f, motion.y);
  b.mx.z -= std::min(0.f, motion.z);
  return b;
}
inline bool collisionBoundsOverlap(const CollisionBounds& a,
                                   const CollisionBounds& b) {
  // Allow rounding at touching faces; the exact narrow phase decides.
  constexpr float e = 1e-4f;
  return a.mn.x <= b.mx.x + e && b.mn.x <= a.mx.x + e &&
         a.mn.y <= b.mx.y + e && b.mn.y <= a.mx.y + e &&
         a.mn.z <= b.mx.z + e && b.mn.z <= a.mx.z + e;
}
inline bool convexContact(const ConvexView& a, const ConvexView& b,
                          ConvexContact& out) {
  if (!a.nv || !b.nv) return false;
  out.depth = 1e30f;
  out.normal = {0, 1, 0};
  auto axis = [&](CollisionV3 n) {
    float l2 = cvDot(n, n);
    if (l2 < 1e-10f) return true;
    n = cvMul(n, 1 / std::sqrt(l2));
    float alo = 1e30f, ahi = -1e30f, blo = 1e30f, bhi = -1e30f;
    for (int i = 0; i < a.nv; ++i) {
      float d = cvDot(n, a.verts[i]);
      alo = std::min(alo, d);
      ahi = std::max(ahi, d);
    }
    for (int i = 0; i < b.nv; ++i) {
      float d = cvDot(n, b.verts[i]);
      blo = std::min(blo, d);
      bhi = std::max(bhi, d);
    }
    if (alo >= bhi || blo >= ahi) return false;
    float plus = bhi - alo, minus = ahi - blo;
    float depth = std::min(plus, minus);
    if (depth < out.depth) {
      out.depth = depth;
      out.normal = plus < minus ? n : cvMul(n, -1);
    }
    return true;
  };
  for (int i = 0; i < a.nn; ++i)
    if (!axis(a.normals[i])) return false;
  for (int i = 0; i < b.nn; ++i)
    if (!axis(b.normals[i])) return false;
  for (int i = 0; i < a.ne; ++i)
    for (int j = 0; j < b.ne; ++j)
      if (!axis(cvCross(a.edges[i], b.edges[j]))) return false;
  if (out.depth == 1e30f) return false;
  // Average the support features; avoids arbitrary end-point torque on edges.
  auto support = [&](const ConvexView& h, CollisionV3 n) {
    float best = -1e30f;
    for (int i = 0; i < h.nv; ++i) best = std::max(best, cvDot(h.verts[i], n));
    CollisionV3 sum = {0, 0, 0};
    int count = 0;
    for (int i = 0; i < h.nv; ++i)
      if (best - cvDot(h.verts[i], n) <= 1e-4f) {
        sum = cvAdd(sum, h.verts[i]);
        ++count;
      }
    return cvMul(sum, 1.f / count);
  };
  out.point = cvMul(
      cvAdd(support(a, cvMul(out.normal, -1)), support(b, out.normal)), .5f);
  return true;
}
// Exact closest feature on a convex hull: face projections and all vertex
// segments (including every edge). Interior diagonals cannot beat the closest
// boundary point for an exterior query, and need no adjacency allocation.
inline bool sphereConvexContact(CollisionV3 p, float radius, const float* verts,
                                int nv, const float* planes, int np,
                                ConvexContact& out) {
  float separation = -1e30f;
  int face = -1;
  for (int f = 0; f < np; ++f) {
    const float* pl = planes + f * 4;
    float d = pl[0] * p.x + pl[1] * p.y + pl[2] * p.z - pl[3];
    if (d > separation) {
      separation = d;
      face = f;
    }
  }
  if (face < 0 || separation >= radius) return false;
  if (separation <= 0) {
    const float* pl = planes + face * 4;
    out.normal = {pl[0], pl[1], pl[2]};
    out.depth = radius - separation;
    out.point = cvSub(p, cvMul(out.normal, separation));
    return true;
  }
  float best = 1e30f;
  CollisionV3 closest = {};
  auto consider = [&](CollisionV3 q) {
    float d = cvDot(cvSub(p, q), cvSub(p, q));
    if (d < best) {
      best = d;
      closest = q;
    }
  };
  for (int f = 0; f < np; ++f) {
    const float* pl = planes + f * 4;
    CollisionV3 n = {pl[0], pl[1], pl[2]};
    CollisionV3 q = cvSub(p, cvMul(n, cvDot(p, n) - pl[3]));
    bool inside = true;
    for (int j = 0; j < np; ++j) {
      const float* x = planes + j * 4;
      if (x[0] * q.x + x[1] * q.y + x[2] * q.z > x[3] + 1e-5f) {
        inside = false;
        break;
      }
    }
    if (inside) consider(q);
  }
  for (int a = 0; a < nv; ++a) {
    CollisionV3 x = {verts[a * 3], verts[a * 3 + 1], verts[a * 3 + 2]};
    consider(x);
    for (int b = a + 1; b < nv; ++b) {
      CollisionV3 y = {verts[b * 3], verts[b * 3 + 1], verts[b * 3 + 2]},
                  e = cvSub(y, x);
      float l2 = cvDot(e, e);
      if (l2 < 1e-14f) continue;
      float t = std::max(0.f, std::min(1.f, cvDot(cvSub(p, x), e) / l2));
      consider(cvAdd(x, cvMul(e, t)));
    }
  }
  if (best >= radius * radius || best < 1e-14f) return false;
  float distance = std::sqrt(best);
  out.normal = cvMul(cvSub(p, closest), 1 / distance);
  out.point = closest;
  out.depth = radius - distance;
  return true;
}
// Linear CCD at the predicted orientation. Angular motion is handled by the
// ordinary contact pass; this is not a claim of exact rotational CCD.
inline bool convexSweepContact(const ConvexView& a, const ConvexView& b,
                               CollisionV3 motionA, CollisionV3 motionB,
                               ConvexContact& out) {
  float enter = 0, leave = 1;
  CollisionV3 normal = {0, 0, 0};
  bool found = false;
  auto axis = [&](CollisionV3 n) {
    float l2 = cvDot(n, n);
    if (l2 < 1e-10f) return true;
    n = cvMul(n, 1 / std::sqrt(l2));
    float alo = 1e30f, ahi = -1e30f, blo = 1e30f, bhi = -1e30f;
    for (int i = 0; i < a.nv; ++i) {
      float v = cvDot(n, a.verts[i]);
      alo = std::min(alo, v);
      ahi = std::max(ahi, v);
    }
    for (int i = 0; i < b.nv; ++i) {
      float v = cvDot(n, b.verts[i]);
      blo = std::min(blo, v);
      bhi = std::max(bhi, v);
    }
    const float da = cvDot(n, motionA), db = cvDot(n, motionB), d = da - db;
    alo -= da;
    ahi -= da;
    blo -= db;
    bhi -= db;
    if (std::fabs(d) < 1e-9f) return !(ahi < blo || bhi < alo);
    float t0 = (blo - ahi) / d, t1 = (bhi - alo) / d;
    CollisionV3 nn = cvMul(n, -1);
    if (t0 > t1) {
      std::swap(t0, t1);
      nn = n;
    }
    if (t0 > enter) {
      enter = t0;
      normal = nn;
      found = true;
    }
    leave = std::min(leave, t1);
    return enter <= leave;
  };
  for (int i = 0; i < a.nn; ++i)
    if (!axis(a.normals[i])) return false;
  for (int i = 0; i < b.nn; ++i)
    if (!axis(b.normals[i])) return false;
  for (int i = 0; i < a.ne; ++i)
    for (int j = 0; j < b.ne; ++j)
      if (!axis(cvCross(a.edges[i], b.edges[j]))) return false;
  if (!found || enter > 1 || leave < 0) return false;
  float amin = 1e30f, bmax = -1e30f;
  CollisionV3 av = {}, bv = {};
  for (int i = 0; i < a.nv; ++i) {
    float v = cvDot(a.verts[i], normal);
    if (v < amin) {
      amin = v;
      av = a.verts[i];
    }
  }
  for (int i = 0; i < b.nv; ++i) {
    float v = cvDot(b.verts[i], normal);
    if (v > bmax) {
      bmax = v;
      bv = b.verts[i];
    }
  }
  out.normal = normal;
  out.depth = std::max(0.f, bmax - amin);
  out.point = cvMul(cvAdd(cvSub(av, cvMul(motionA, 1 - enter)),
                          cvSub(bv, cvMul(motionB, 1 - enter))),
                    .5f);
  return true;
}
inline bool sphereSweepContact(CollisionV3 a0, CollisionV3 a1, float ra,
                               CollisionV3 b0, CollisionV3 b1, float rb,
                               ConvexContact& out) {
  CollisionV3 p = cvSub(a0, b0), v = cvSub(cvSub(a1, a0), cvSub(b1, b0));
  float r = ra + rb, aa = cvDot(v, v), bb = cvDot(p, v),
        cc = cvDot(p, p) - r * r;
  if (cc <= 0 || aa < 1e-12f) return false;
  float disc = bb * bb - aa * cc;
  if (disc < 0) return false;
  float t = (-bb - std::sqrt(disc)) / aa;
  if (t < 0 || t > 1) return false;
  out.normal = cvMul(cvAdd(p, cvMul(v, t)), 1 / r);
  out.depth = std::max(0.f, r - cvDot(cvSub(a1, b1), out.normal));
  out.point = cvSub(cvAdd(a0, cvMul(cvSub(a1, a0), t)), cvMul(out.normal, ra));
  return true;
}
// Segment versus an axis-aligned box. The normal points out toward the start.
inline bool collisionSweepBox(const float* p, const float* end, const float* mn,
                              const float* mx, int dimensions, float& toi,
                              int& axisOut, float& signOut) {
  float enter = 0, leave = 1;
  int axis = -1;
  float sign = 0;
  bool inside = true;
  for (int k = 0; k < dimensions; ++k) {
    inside &= p[k] > mn[k] && p[k] < mx[k];
    float d = end[k] - p[k];
    if (std::fabs(d) < 1e-9f) {
      if (p[k] < mn[k] || p[k] > mx[k]) return false;
      continue;
    }
    float a = (mn[k] - p[k]) / d, b = (mx[k] - p[k]) / d, s = -1;
    if (a > b) {
      std::swap(a, b);
      s = 1;
    }
    if (a > enter || (a == enter && axis < 0)) {
      enter = a;
      axis = k;
      sign = s;
    }
    leave = std::min(leave, b);
    if (enter > leave) return false;
  }
  if (inside || axis < 0 || enter < 0 || enter > 1) return false;
  toi = enter;
  axisOut = axis;
  signOut = sign;
  return true;
}
}  // namespace Tyra

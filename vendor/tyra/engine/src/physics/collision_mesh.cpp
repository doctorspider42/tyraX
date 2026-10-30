/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: triangle-soup collision with a baked 3D BVH.
*/

#include "physics/collision_mesh.hpp"

#include <math.h>
#include <stdio.h>

#include "file/file_utils.hpp"
#include "physics/collision_sweep.hpp"

namespace Tyra {

void CollisionMesh::build(const float* verts, u32 vertexCount,
                          u32 strideFloats) {
  tris = collisionTriangles(verts, vertexCount, strideFloats);
  bvh.build(tris);
  nearby.clear();
  if (!bvh.nodes.empty())
    for (int k = 0; k < 3; ++k) {
      min[k] = bvh.nodes[0].mn[k];
      max[k] = bvh.nodes[0].mx[k];
    }
}

bool CollisionMesh::loadBaked(const std::string& path) {
  FILE* f = fopen(FileUtils::fromCwd(path).c_str(), "rb");
  if (!f) return false;
  std::string data;
  char chunk[8192];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    data.append(chunk, got);
    if (data.size() > 128u * 1024u * 1024u) {
      fclose(f);
      return false;
    }
  }
  const bool readOk = !ferror(f);
  fclose(f);
  if (!readOk || !bvh.decode(data, tris)) return false;
  if (!bvh.nodes.empty())
    for (int k = 0; k < 3; ++k) {
      min[k] = bvh.nodes[0].mn[k];
      max[k] = bvh.nodes[0].mx[k];
    }
  return true;
}

/** Moller-Trumbore, both faces. */
bool CollisionMesh::intersectTri(const float* t, const Vec4& origin,
                                 const Vec4& dir, float maxDist,
                                 float* outDist) const {
  const float e1x = t[3] - t[0], e1y = t[4] - t[1], e1z = t[5] - t[2];
  const float e2x = t[6] - t[0], e2y = t[7] - t[1], e2z = t[8] - t[2];
  const float px = dir.y * e2z - dir.z * e2y;
  const float py = dir.z * e2x - dir.x * e2z;
  const float pz = dir.x * e2y - dir.y * e2x;
  const float det = e1x * px + e1y * py + e1z * pz;
  if (det > -1e-8F && det < 1e-8F) return false;
  const float invDet = 1.0F / det;
  const float tx = origin.x - t[0];
  const float ty = origin.y - t[1];
  const float tz = origin.z - t[2];
  const float u = (tx * px + ty * py + tz * pz) * invDet;
  if (u < 0.0F || u > 1.0F) return false;
  const float qx = ty * e1z - tz * e1y;
  const float qy = tz * e1x - tx * e1z;
  const float qz = tx * e1y - ty * e1x;
  const float v = (dir.x * qx + dir.y * qy + dir.z * qz) * invDet;
  if (v < 0.0F || u + v > 1.0F) return false;
  const float dist = (e2x * qx + e2y * qy + e2z * qz) * invDet;
  if (dist < 0.0F || dist > maxDist) return false;
  *outDist = dist;
  return true;
}

bool CollisionMesh::raycast(const Vec4& origin, const Vec4& dir, float maxDist,
                            float* outDist) const {
  return raycast(origin, dir, maxDist, outDist, nullptr);
}

bool CollisionMesh::raycast(const Vec4& origin, const Vec4& dir, float maxDist,
                            float* outDist, float* outNormal) const {
  if (tris.empty()) return false;

  // The segment against the mesh's own box first (slab test). The grid
  // CLAMPS a query outside it to the edge cells, so a ray nowhere near the
  // mesh used to pay for every triangle along the border - the walker casts
  // two a frame for every mesh-mode object in the scene, near or not.
  {
    float t0 = 0.0F, t1 = maxDist;
    const float o3[3] = {origin.x, origin.y, origin.z};
    const float d3[3] = {dir.x, dir.y, dir.z};
    for (int a = 0; a < 3; ++a) {
      if (d3[a] > -1e-12F && d3[a] < 1e-12F) {
        if (o3[a] < min[a] || o3[a] > max[a]) return false;
        continue;
      }
      const float inv = 1.0F / d3[a];
      float ta = (min[a] - o3[a]) * inv, tb = (max[a] - o3[a]) * inv;
      if (ta > tb) {
        const float t = ta;
        ta = tb;
        tb = t;
      }
      if (ta > t0) t0 = ta;
      if (tb < t1) t1 = tb;
      if (t0 > t1) return false;
    }
  }

  bool hit = false;
  float best = maxDist;
  u32 bestTri = 0;
  const float p[3] = {origin.x, origin.y, origin.z};
  const float d[3] = {dir.x, dir.y, dir.z};
  bvh.ray(p, d, best, [&](u32 t) {
    float dist;
    if (intersectTri(&tris[t * kTriFloats], origin, dir, best, &dist) &&
        (!hit || dist < best || (dist == best && t < bestTri))) {
      best = dist;
      hit = true;
      bestTri = t;
    }
  });
  if (hit) {
    *outDist = best;
    if (outNormal) {
      const float* n = &tris[bestTri * kTriFloats + 9];
      outNormal[0] = n[0], outNormal[1] = n[1], outNormal[2] = n[2];
    }
  }
  return hit;
}

bool CollisionMesh::sweepSphere(const Vec4& from, const Vec4& to, float radius,
                                float* outFraction, float* outNormal,
                                float maxNormalY, const Vec4* upLocal) const {
  const CollisionV3 p = {from.x, from.y, from.z}, end = {to.x, to.y, to.z};
  const float o[3] = {from.x, from.y, from.z};
  const float d[3] = {to.x - from.x, to.y - from.y, to.z - from.z};
  float best = 1.0F;
  bool hit = false;
  CollisionV3 normal = {0, 1, 0};
  bvh.ray(
      o, d, best,
      [&](u32 t) {
        const float* tri = &tris[t * kTriFloats];
        const float up = upLocal ? tri[9] * upLocal->x + tri[10] * upLocal->y +
                                       tri[11] * upLocal->z
                                 : tri[10];
        if (fabsf(up) >= maxNormalY) return;
        if (sweepSphereTriangle(p, end, radius, tri, best, normal)) hit = true;
      },
      radius);
  if (!hit) return false;
  *outFraction = best;
  if (outNormal) {
    outNormal[0] = normal.x;
    outNormal[1] = normal.y;
    outNormal[2] = normal.z;
  }
  return true;
}

namespace {

/** Closest point on triangle (a, b, c) to p - Ericson, Real-Time Collision
 * Detection 5.1.5, scalar form. */
void closestPointOnTriangle(const float* tri, const Vec4& p, float* out) {
  const float ax = tri[0], ay = tri[1], az = tri[2];
  const float bx = tri[3], by = tri[4], bz = tri[5];
  const float cx = tri[6], cy = tri[7], cz = tri[8];

  const float abx = bx - ax, aby = by - ay, abz = bz - az;
  const float acx = cx - ax, acy = cy - ay, acz = cz - az;
  const float apx = p.x - ax, apy = p.y - ay, apz = p.z - az;

  const float d1 = abx * apx + aby * apy + abz * apz;
  const float d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0.0F && d2 <= 0.0F) {
    out[0] = ax, out[1] = ay, out[2] = az;
    return;
  }

  const float bpx = p.x - bx, bpy = p.y - by, bpz = p.z - bz;
  const float d3 = abx * bpx + aby * bpy + abz * bpz;
  const float d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0.0F && d4 <= d3) {
    out[0] = bx, out[1] = by, out[2] = bz;
    return;
  }

  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
    const float v = d1 / (d1 - d3);
    out[0] = ax + v * abx, out[1] = ay + v * aby, out[2] = az + v * abz;
    return;
  }

  const float cpx = p.x - cx, cpy = p.y - cy, cpz = p.z - cz;
  const float d5 = abx * cpx + aby * cpy + abz * cpz;
  const float d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0.0F && d5 <= d6) {
    out[0] = cx, out[1] = cy, out[2] = cz;
    return;
  }

  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
    const float w = d2 / (d2 - d6);
    out[0] = ax + w * acx, out[1] = ay + w * acy, out[2] = az + w * acz;
    return;
  }

  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    out[0] = bx + w * (cx - bx);
    out[1] = by + w * (cy - by);
    out[2] = bz + w * (cz - bz);
    return;
  }

  const float denom = 1.0F / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  out[0] = ax + abx * v + acx * w;
  out[1] = ay + aby * v + acy * w;
  out[2] = az + abz * v + acz * w;
}

}  // namespace

bool CollisionMesh::resolveSphere(Vec4* center, float radius,
                                  float maxNormalY) const {
  return resolveSphere(center, radius, maxNormalY,
                       Vec4(0.0F, 1.0F, 0.0F, 0.0F));
}

bool CollisionMesh::resolveSphere(Vec4* center, float radius, float maxNormalY,
                                  const Vec4& upLocal, const Vec4* prev) const {
  if (tris.empty()) return false;
  // Nothing can act on a sphere further than radius + the crossing band
  // (0.6, below) outside the mesh's box - and the grid clamps such a query
  // to the edge cells, which then cost a closest-point test per triangle.
  {
    const float reach = radius + 0.6F;
    if (center->x < min[0] - reach || center->x > max[0] + reach ||
        center->y < min[1] - reach || center->y > max[1] + reach ||
        center->z < min[2] - reach || center->z > max[2] + reach)
      return false;
  }

  bool moved = false;
  // two passes: the first push can slide the sphere into a neighbour face
  for (int pass = 0; pass < 2; ++pass) {
    // Preserve authored triangle order: the sequential push solver depends
    // on it. Re-query each pass after movement, now pruning in all three axes.
    const float mn[3] = {center->x - radius - 0.6F, center->y - radius - 0.6F,
                         center->z - radius - 0.6F};
    const float mx[3] = {center->x + radius + 0.6F, center->y + radius + 0.6F,
                         center->z + radius + 0.6F};
    nearby.clear();
    bvh.box(mn, mx, [&](u32 t) { nearby.push_back(t); });
    std::sort(nearby.begin(), nearby.end());
    bool movedThisPass = false;
    for (u32 t : nearby) {
      const float* tri = &tris[t * kTriFloats];
      // "steep" judged against the caller's up (local n dot local up =
      // the world-space slope when upLocal is world-up in mesh space)
      const float normalUp =
          tri[9] * upLocal.x + tri[10] * upLocal.y + tri[11] * upLocal.z;
      if (normalUp >= maxNormalY || normalUp <= -maxNormalY)
        continue;  // walkable floor/ceiling - the ground raycast owns it
      float closest[3];
      closestPointOnTriangle(tri, *center, closest);
      const float dx = center->x - closest[0];
      const float dy = center->y - closest[1];
      const float dz = center->z - closest[2];
      const float d2 = dx * dx + dy * dy + dz * dz;
      if (prev) {
        // Side-aware: eject the sphere to prev's side of this face. A
        // step that crossed the face plane this move is caught within
        // an extra capture band past the radius (a plain radius test
        // misses a step longer than the radius).
        const float sPrev = (prev->x - tri[0]) * tri[9] +
                            (prev->y - tri[1]) * tri[10] +
                            (prev->z - tri[2]) * tri[11];
        const float sCur = (center->x - tri[0]) * tri[9] +
                           (center->y - tri[1]) * tri[10] +
                           (center->z - tri[2]) * tri[11];
        bool crossed = sPrev * sCur < 0.0F;
        const float capture = crossed ? radius + 0.6F : radius;
        if (d2 >= capture * capture || d2 <= 1e-12F) continue;
        // Only treat the crossing as "through this face" when the
        // sphere sits against the triangle's interior (plane distance
        // makes up most of the gap). Crossing a wall's PLANE while
        // walking on the mesh's top face passes near the shared edge -
        // there the lateral offset dominates and ejecting along the
        // wall normal would yank the walker off the top.
        if (crossed && sCur * sCur < 0.5F * d2) {
          crossed = false;
          if (d2 >= radius * radius) continue;
        }
        // On prev's side the plain push is fine (and handles sphere-
        // vs-edge/corner directions exactly); a crossed face ejects
        // along its normal back to prev's side.
        if (!crossed && sCur * sPrev > 0.0F) {
          const float d = sqrtf(d2);
          const float push = (radius - d) / d;
          center->x += dx * push;
          center->y += dy * push;
          center->z += dz * push;
        } else {
          const float side = sPrev >= 0.0F ? 1.0F : -1.0F;
          center->x = closest[0] + tri[9] * side * radius;
          center->y = closest[1] + tri[10] * side * radius;
          center->z = closest[2] + tri[11] * side * radius;
        }
        movedThisPass = true;
        continue;
      }
      if (d2 >= radius * radius || d2 <= 1e-12F) continue;
      const float d = sqrtf(d2);
      const float push = (radius - d) / d;
      center->x += dx * push;
      center->y += dy * push;
      center->z += dz * push;
      movedThisPass = true;
    }
    moved |= movedThisPass;
    if (!movedThisPass) break;
  }
  return moved;
}

}  // namespace Tyra

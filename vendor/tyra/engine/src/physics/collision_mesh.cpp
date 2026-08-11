/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: triangle-soup collision with an XZ uniform grid.
*/

#include "physics/collision_mesh.hpp"

#include <math.h>

namespace Tyra {

void CollisionMesh::build(const float* verts, u32 vertexCount,
                          u32 strideFloats) {
  tris.clear();
  cellStart.clear();
  cellTris.clear();
  stamp.clear();
  stampCounter = 0;
  nx = nz = 0;
  if (!verts || vertexCount < 3 || strideFloats < 3) return;

  const u32 triCount = vertexCount / 3;
  tris.reserve(triCount * kTriFloats);
  for (u32 t = 0; t < triCount; ++t) {
    const float* a = verts + (t * 3 + 0) * strideFloats;
    const float* b = verts + (t * 3 + 1) * strideFloats;
    const float* c = verts + (t * 3 + 2) * strideFloats;
    // flat normal (not taken from the vertex data - strides of 3 have none)
    const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nxv = uy * vz - uz * vy;
    float nyv = uz * vx - ux * vz;
    float nzv = ux * vy - uy * vx;
    const float len = sqrtf(nxv * nxv + nyv * nyv + nzv * nzv);
    if (len <= 1e-8F) continue;  // degenerate
    nxv /= len;
    nyv /= len;
    nzv /= len;
    const float* pts[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
      tris.push_back(pts[i][0]);
      tris.push_back(pts[i][1]);
      tris.push_back(pts[i][2]);
    }
    tris.push_back(nxv);
    tris.push_back(nyv);
    tris.push_back(nzv);
  }
  const u32 kept = tris.size() / kTriFloats;
  if (kept == 0) return;

  // AABB over all triangle vertices
  min[0] = min[1] = min[2] = 1e30F;
  max[0] = max[1] = max[2] = -1e30F;
  for (u32 t = 0; t < kept; ++t) {
    const float* p = &tris[t * kTriFloats];
    for (int v = 0; v < 3; ++v)
      for (int i = 0; i < 3; ++i) {
        const float value = p[v * 3 + i];
        if (value < min[i]) min[i] = value;
        if (value > max[i]) max[i] = value;
      }
  }

  // grid resolution: aim for a handful of triangles per cell
  int cells = (int)sqrtf((float)kept);
  if (cells < 1) cells = 1;
  if (cells > kMaxGrid) cells = kMaxGrid;
  nx = nz = cells;
  const float sizeX = max[0] - min[0];
  const float sizeZ = max[2] - min[2];
  invStepX = sizeX > 1e-6F ? nx / sizeX : 0.0F;
  invStepZ = sizeZ > 1e-6F ? nz / sizeZ : 0.0F;

  // two-pass CRS fill: count per cell, prefix-sum, then insert
  std::vector<u32> counts(nx * nz, 0);
  auto forEachCell = [&](u32 t, auto&& fn) {
    const float* p = &tris[t * kTriFloats];
    float lox = p[0], hix = p[0], loz = p[2], hiz = p[2];
    for (int v = 1; v < 3; ++v) {
      const float x = p[v * 3], z = p[v * 3 + 2];
      if (x < lox) lox = x;
      if (x > hix) hix = x;
      if (z < loz) loz = z;
      if (z > hiz) hiz = z;
    }
    int cx0, cx1, cz0, cz1;
    cellRange(lox, hix, min[0], invStepX, nx, &cx0, &cx1);
    cellRange(loz, hiz, min[2], invStepZ, nz, &cz0, &cz1);
    for (int cz = cz0; cz <= cz1; ++cz)
      for (int cx = cx0; cx <= cx1; ++cx) fn(cz * nx + cx);
  };
  for (u32 t = 0; t < kept; ++t)
    forEachCell(t, [&](int cell) { counts[cell]++; });
  cellStart.assign(nx * nz + 1, 0);
  for (int i = 0; i < nx * nz; ++i) cellStart[i + 1] = cellStart[i] + counts[i];
  cellTris.assign(cellStart[nx * nz], 0);
  std::vector<u32> cursor(cellStart.begin(), cellStart.end() - 1);
  for (u32 t = 0; t < kept; ++t)
    forEachCell(t, [&](int cell) { cellTris[cursor[cell]++] = t; });

  stamp.assign(kept, 0);
}

int CollisionMesh::cellOf(float v, float origin, float invStep,
                          int cells) const {
  int c = (int)((v - origin) * invStep);
  if (c < 0) c = 0;
  if (c > cells - 1) c = cells - 1;
  return c;
}

void CollisionMesh::cellRange(float lo, float hi, float origin, float invStep,
                              int cells, int* first, int* last) const {
  *first = cellOf(lo, origin, invStep, cells);
  *last = cellOf(hi, origin, invStep, cells);
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
                            float* outDist, Vec4* outNormal) const {
  if (tris.empty()) return false;

  // cells overlapped by the XZ projection of the segment
  const float endX = origin.x + dir.x * maxDist;
  const float endZ = origin.z + dir.z * maxDist;
  int cx0, cx1, cz0, cz1;
  cellRange(origin.x < endX ? origin.x : endX,
            origin.x < endX ? endX : origin.x, min[0], invStepX, nx, &cx0,
            &cx1);
  cellRange(origin.z < endZ ? origin.z : endZ,
            origin.z < endZ ? endZ : origin.z, min[2], invStepZ, nz, &cz0,
            &cz1);

  ++stampCounter;
  bool hit = false;
  float best = maxDist;
  const float* bestTri = nullptr;
  for (int cz = cz0; cz <= cz1; ++cz)
    for (int cx = cx0; cx <= cx1; ++cx) {
      const int cell = cz * nx + cx;
      for (u32 i = cellStart[cell]; i < cellStart[cell + 1]; ++i) {
        const u32 t = cellTris[i];
        if (stamp[t] == stampCounter) continue;
        stamp[t] = stampCounter;
        float dist;
        const float* tri = &tris[t * kTriFloats];
        if (intersectTri(tri, origin, dir, best, &dist)) {
          best = dist;
          bestTri = tri;
          hit = true;
        }
      }
    }
  if (hit) {
    *outDist = best;
    if (outNormal && bestTri)
      outNormal->set(bestTri[9], bestTri[10], bestTri[11], 0.0F);
  }
  return hit;
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
                                  const Vec4& upLocal,
                                  const Vec4* prev) const {
  if (tris.empty()) return false;

  bool moved = false;
  // two passes: the first push can slide the sphere into a neighbour face
  for (int pass = 0; pass < 2; ++pass) {
    int cx0, cx1, cz0, cz1;
    cellRange(center->x - radius, center->x + radius, min[0], invStepX, nx,
              &cx0, &cx1);
    cellRange(center->z - radius, center->z + radius, min[2], invStepZ, nz,
              &cz0, &cz1);
    ++stampCounter;
    bool movedThisPass = false;
    for (int cz = cz0; cz <= cz1; ++cz)
      for (int cx = cx0; cx <= cx1; ++cx) {
        const int cell = cz * nx + cx;
        for (u32 i = cellStart[cell]; i < cellStart[cell + 1]; ++i) {
          const u32 t = cellTris[i];
          if (stamp[t] == stampCounter) continue;
          stamp[t] = stampCounter;
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
      }
    moved |= movedThisPass;
    if (!movedThisPass) break;
  }
  return moved;
}

}  // namespace Tyra

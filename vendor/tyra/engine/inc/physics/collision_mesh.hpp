/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: triangle-soup collision with an XZ uniform grid.
*/

#pragma once

#include <vector>
#include <tamtypes.h>
#include "../math/vec4.hpp"

namespace Tyra {

/**
 * Static triangle-mesh collider.
 *
 * Built once from an interleaved vertex array (consecutive triples form
 * triangles); queries run against an XZ uniform grid, so cost scales with
 * the triangles near the query point, not the whole mesh. All coordinates
 * are in the mesh's local space - transform the query into local space and
 * the results back (that keeps the grid axis-aligned under object rotation).
 *
 * Designed for player-versus-scenery collision: raycast() finds the ground
 * under the player, resolveSphere() pushes the player out of steep faces.
 */
class CollisionMesh {
 public:
  /**
   * @param verts interleaved vertex data, position at offset 0 of each vertex
   * @param vertexCount number of vertices (a multiple of 3)
   * @param strideFloats floats per vertex (>= 3), e.g. 8 for LeanObjMesh data
   */
  void build(const float* verts, u32 vertexCount, u32 strideFloats);

  bool empty() const { return tris.empty(); }
  u32 triangleCount() const { return (u32)(tris.size() / kTriFloats); }
  const float* aabbMin() const { return min; }
  const float* aabbMax() const { return max; }

  /**
   * Nearest triangle hit along a ray (both faces). Returns false on a miss.
   * @param dir must be normalized
   */
  bool raycast(const Vec4& origin, const Vec4& dir, float maxDist,
               float* outDist, Vec4* outNormal = nullptr) const;

  /**
   * Pushes a sphere out of every triangle steeper than maxNormalY
   * (fabs(normal.y) >= maxNormalY = walkable floor/ceiling, ignored - the
   * ground is raycast()'s job). Returns true when the center moved.
   */
  bool resolveSphere(Vec4* center, float radius, float maxNormalY) const;

  /**
   * Same, but "steep" is judged against an explicit up direction given in
   * MESH-LOCAL space (unit length). Callers whose mesh is rotated in the
   * world pass world-up transformed into local space - otherwise a mesh
   * lying on its side has world-walls whose LOCAL normal reads as a
   * walkable floor and the sphere walks straight through them.
   *
   * With `prev` (the query's pre-move position, mesh-local) the push is
   * SIDE-AWARE: the sphere is ejected to prev's side of each face. The
   * plain push moves along (center - closest), which for a center that
   * stepped PAST a face plane points INTO the volume - a fast walker
   * (step > radius) crossing a wall got sucked inside instead of blocked.
   * Steps that crossed a face this move are caught within an extra 0.6
   * capture band beyond the radius.
   */
  bool resolveSphere(Vec4* center, float radius, float maxNormalY,
                     const Vec4& upLocal, const Vec4* prev = nullptr) const;

 private:
  static constexpr u32 kTriFloats = 12;  // a[3], b[3], c[3], normal[3]
  static constexpr int kMaxGrid = 32;

  bool intersectTri(const float* t, const Vec4& origin, const Vec4& dir,
                    float maxDist, float* outDist) const;

  int cellOf(float v, float origin, float invStep, int cells) const;
  void cellRange(float lo, float hi, float origin, float invStep, int cells,
                 int* first, int* last) const;

  std::vector<float> tris;  // packed kTriFloats per triangle
  float min[3] = {0, 0, 0};
  float max[3] = {0, 0, 0};

  // XZ grid in CRS form: triangle indices of cell (cx, cz) are
  // cellTris[cellStart[cz * nx + cx] .. cellStart[cz * nx + cx + 1])
  std::vector<u32> cellStart;
  std::vector<u32> cellTris;
  int nx = 0, nz = 0;
  float invStepX = 0, invStepZ = 0;

  // visit stamps de-duplicate triangles shared by several cells per query
  mutable std::vector<u32> stamp;
  mutable u32 stampCounter = 0;
};

}  // namespace Tyra

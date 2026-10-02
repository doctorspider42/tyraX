/*
# Modified by TyraX - object-space frustum classification (planes are
# transformed into the box's local space once per bag, then every axis-aligned
# box is classified with the p-vertex/n-vertex test - 2 dot products per plane
# instead of transforming 8 corners and testing each against every plane) and
# a VU0 min/max vertex scan in the array constructor.
# Based on the original by Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#pragma once

#include <vector>
#include <string>
#include "./core_bbox_frustum.hpp"
#include "math/m4x4.hpp"
#include "math/plane.hpp"
#include <array>

namespace Tyra {

/** Bounding box */
class CoreBBox {
 public:
  CoreBBox();
  CoreBBox(const CoreBBox& t_bbox);
  explicit CoreBBox(const Vec4* t_vertices, const u32* faces,
                    const u32& t_count);
  explicit CoreBBox(const Vec4* t_vertices, const u32& t_count);
  explicit CoreBBox(const Vec4* t_vertices);
  explicit CoreBBox(CoreBBox** t_bboxes, const u32& count);
  explicit CoreBBox(const std::vector<CoreBBox>& t_bboxes,
                    const u32& startIndex, const u32& stopIndex);
  explicit CoreBBox(const CoreBBox& t_bbox, const M4x4& t_matrix);

  static CoreBBox create(const Vec4& center, const float& size);

  /**
   * 0 - lowX, lowY, lowZ
   * 1 - lowX, lowY, hiZ
   * 2 - lowX, hiY, lowZ
   * 3 - lowX, hiY, hiZ
   * 4 - hiX, lowY, lowZ
   * 5 - hiX, lowY, hiZ
   * 6 - hiX, hiY, lowZ
   * 7 - hiX, hiY, hiZ
   */
  Vec4 vertices[8];

  void operator=(const CoreBBox& v);

  const Vec4& operator[](const u8& i) const { return vertices[i]; }

  const u8 getVertexCount() const { return 8; }

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

  /** Get new transformed BBox by model matrix */
  CoreBBox getTransformed(const M4x4& t_matrix) const;

  /**
   * @brief Check if bbox is in/partially/outside view frustum
   *
   * @param frustumPlanes Available in
   * engine.renderer.core.renderer3D.frustumPlanes
   * @param model Model matrix if you want to fix bbox by model matrix
   * @param margins Optional margins
   */
  CoreBBoxFrustum frustumCheck(const Plane* frustumPlanes, const M4x4& model,
                               const float* margins = nullptr) const;
  CoreBBoxFrustum frustumCheck(const Plane* frustumPlanes,
                               const float* margins = nullptr) const;

  /**
   * @brief Check if bbox is in view frustum
   *
   * @param frustumPlanes Available in
   * engine.renderer.core.renderer3D.frustumPlanes
   * @param model Model matrix if you want to fix bbox by model matrix
   */
  bool isInFrustum(const Plane* frustumPlanes, const M4x4& model) const;
  bool isInFrustum(const Plane* frustumPlanes) const;

  /**
   * Modified by TyraX. Transform world-space frustum planes into the
   * object space of a model matrix (out[i] = model^T * worldPlanes[i], the
   * translation folded into the distance). Classifying an axis-aligned box
   * against these planes is exactly equivalent to transforming its corners
   * by `model` and testing them against the world planes - but the matrix
   * work happens once per object instead of once per box corner.
   */
  static void computeObjectSpacePlanes(Plane* out, const Plane* worldPlanes,
                                       const M4x4& model);

  /**
   * Modified by TyraX. Frustum classification of an axis-aligned box
   * given by its min/max corners, against object-space planes from
   * computeObjectSpacePlanes(). Uses the p-vertex/n-vertex trick: per plane
   * only the two extreme corners are tested, which matches testing all 8
   * corners because the distance function is linear. When `crossingMask` is
   * non-null, bit i is set if the box straddles plane i; requesting the mask
   * evaluates the n-vertex for all six planes instead of stopping after the
   * first crossing.
   */
  static CoreBBoxFrustum frustumCheckAABB(const Plane* objectSpacePlanes,
                                          const Vec4& min, const Vec4& max,
                                          u8* crossingMask = nullptr);

  /**
   * Modified by TyraX. AABB classification of THIS box (valid only for
   * boxes built from min/max corners - every constructor except the
   * matrix-transforming one, whose corners are not axis-aligned).
   */
  CoreBBoxFrustum frustumCheckAABB(const Plane* objectSpacePlanes,
                                   u8* crossingMask = nullptr) const {
    return frustumCheckAABB(objectSpacePlanes, vertices[0], vertices[7],
                            crossingMask);
  }

  /**
   * Bit i is set when any part of an AABB is outside plane i. Unlike the
   * classification helper, a box fully outside a plane still sets that bit:
   * the clipper must run that plane to discard the polygon.
   *
   * `count` may exceed the six clip planes (max 8, one bit per plane): the
   * guard-band routing appends the two exact near/far half-spaces the CULL
   * program's own clipw judgement uses. Those extra bits are a routing
   * question, never part of the mask VU1 receives.
   */
  static u8 activePlaneMaskAABB(const Plane* objectSpacePlanes,
                                const Vec4& min, const Vec4& max,
                                u8 count = 6);

 private:
  static std::array<Vec4, 8> frustumCheckVertices;
};

}  // namespace Tyra

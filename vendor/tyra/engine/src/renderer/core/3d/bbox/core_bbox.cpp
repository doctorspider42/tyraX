/*
# Modified by TyraX - object-space AABB frustum classification
# (computeObjectSpacePlanes + frustumCheckAABB) and a VU0 min/max scan in
# the vertex-array constructor (vmini/vmax fold 4 lanes per op; the scalar
# version cost six compare-and-branch pairs per vertex and runs per frame
# for skinned meshes and dynamic-pipeline bags).
# Based on the original by Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#include <string>
#include <sstream>
#include <iomanip>
#include "renderer/core/3d/bbox/core_bbox.hpp"

namespace Tyra {

std::array<Vec4, 8> CoreBBox::frustumCheckVertices;

namespace {

/**
 * Min/max of a contiguous Vec4 array on VU0 in macro mode.
 * count must be >= 1. Results match the scalar compare loop bit-for-bit
 * (vmini/vmax are exact comparisons).
 */
inline void vec4ArrayMinMax(const Vec4* vertices, u32 count, Vec4* outMin,
                            Vec4* outMax) {
  // count == 0 keeps the original semantics: min = max = vertices[0]
  // (the scalar loop initialized from [0] before iterating).
  u32 remaining = count > 0 ? count : 1;
  const float* ptr = vertices->xyzw;
  asm volatile(
      "lqc2       $vf4, 0x0(%1)       \n\t"  // min = first vertex
      "vmove.xyzw $vf5, $vf4          \n\t"  // max = first vertex
      "1:                             \n\t"
      "lqc2       $vf6, 0x0(%1)       \n\t"
      "addiu      %1, %1, 16          \n\t"
      "addiu      %0, %0, -1          \n\t"
      "vmini.xyz  $vf4, $vf4, $vf6    \n\t"
      "vmax.xyz   $vf5, $vf5, $vf6    \n\t"
      "bne        %0, $0, 1b          \n\t"
      "sqc2       $vf4, 0x0(%2)       \n\t"
      "sqc2       $vf5, 0x0(%3)       \n\t"
      : "+r"(remaining), "+r"(ptr)
      : "r"(outMin->xyzw), "r"(outMax->xyzw)
      : "memory");
}

}  // namespace

CoreBBox::CoreBBox() {
  for (u32 i = 0; i < 8; i++) {
    vertices[i] = Vec4(0.0F, 0.0F, 0.0F, 1.0F);
  }
}

CoreBBox::CoreBBox(CoreBBox** t_bboxes, const u32& count) {
  float lowX = t_bboxes[0]->vertices[0].x;
  float lowY = t_bboxes[0]->vertices[0].y;
  float lowZ = t_bboxes[0]->vertices[0].z;

  float hiX = t_bboxes[0]->vertices[7].x;
  float hiY = t_bboxes[0]->vertices[7].y;
  float hiZ = t_bboxes[0]->vertices[7].z;

  for (u32 i = 0; i < count; i++) {
    if (lowX > t_bboxes[i]->vertices[0].x) lowX = t_bboxes[i]->vertices[0].x;
    if (hiX < t_bboxes[i]->vertices[7].x) hiX = t_bboxes[i]->vertices[7].x;

    if (lowY > t_bboxes[i]->vertices[0].y) lowY = t_bboxes[i]->vertices[0].y;
    if (hiY < t_bboxes[i]->vertices[7].y) hiY = t_bboxes[i]->vertices[7].y;

    if (lowZ > t_bboxes[i]->vertices[0].z) lowZ = t_bboxes[i]->vertices[0].z;
    if (hiZ < t_bboxes[i]->vertices[7].z) hiZ = t_bboxes[i]->vertices[7].z;
  }

  vertices[0].set(lowX, lowY, lowZ);
  vertices[1].set(lowX, lowY, hiZ);
  vertices[2].set(lowX, hiY, lowZ);
  vertices[3].set(lowX, hiY, hiZ);

  vertices[4].set(hiX, lowY, lowZ);
  vertices[5].set(hiX, lowY, hiZ);
  vertices[6].set(hiX, hiY, lowZ);
  vertices[7].set(hiX, hiY, hiZ);
}

CoreBBox::CoreBBox(const std::vector<CoreBBox>& t_bboxes, const u32& startIndex,
                   const u32& stopIndex) {
  float lowX = t_bboxes[startIndex].vertices[0].x;
  float lowY = t_bboxes[startIndex].vertices[0].y;
  float lowZ = t_bboxes[startIndex].vertices[0].z;

  float hiX = t_bboxes[startIndex].vertices[7].x;
  float hiY = t_bboxes[startIndex].vertices[7].y;
  float hiZ = t_bboxes[startIndex].vertices[7].z;

  for (u32 i = startIndex; i < stopIndex; i++) {
    if (lowX > t_bboxes[i].vertices[0].x) lowX = t_bboxes[i].vertices[0].x;
    if (hiX < t_bboxes[i].vertices[7].x) hiX = t_bboxes[i].vertices[7].x;

    if (lowY > t_bboxes[i].vertices[0].y) lowY = t_bboxes[i].vertices[0].y;
    if (hiY < t_bboxes[i].vertices[7].y) hiY = t_bboxes[i].vertices[7].y;

    if (lowZ > t_bboxes[i].vertices[0].z) lowZ = t_bboxes[i].vertices[0].z;
    if (hiZ < t_bboxes[i].vertices[7].z) hiZ = t_bboxes[i].vertices[7].z;
  }

  vertices[0].set(lowX, lowY, lowZ);
  vertices[1].set(lowX, lowY, hiZ);
  vertices[2].set(lowX, hiY, lowZ);
  vertices[3].set(lowX, hiY, hiZ);

  vertices[4].set(hiX, lowY, lowZ);
  vertices[5].set(hiX, lowY, hiZ);
  vertices[6].set(hiX, hiY, lowZ);
  vertices[7].set(hiX, hiY, hiZ);
}

CoreBBox::CoreBBox(const Vec4* t_vertices, const u32* faces, const u32& count) {
  float lowX, lowY, lowZ, hiX, hiY, hiZ;
  lowX = hiX = t_vertices[faces[0]].x;
  lowY = hiY = t_vertices[faces[0]].y;
  lowZ = hiZ = t_vertices[faces[0]].z;
  for (u32 i = 0; i < count; i++) {
    if (lowX > t_vertices[faces[i]].x) lowX = t_vertices[faces[i]].x;
    if (hiX < t_vertices[faces[i]].x) hiX = t_vertices[faces[i]].x;

    if (lowY > t_vertices[faces[i]].y) lowY = t_vertices[faces[i]].y;
    if (hiY < t_vertices[faces[i]].y) hiY = t_vertices[faces[i]].y;

    if (lowZ > t_vertices[faces[i]].z) lowZ = t_vertices[faces[i]].z;
    if (hiZ < t_vertices[faces[i]].z) hiZ = t_vertices[faces[i]].z;
  }

  vertices[0].set(lowX, lowY, lowZ);
  vertices[1].set(lowX, lowY, hiZ);
  vertices[2].set(lowX, hiY, lowZ);
  vertices[3].set(lowX, hiY, hiZ);

  vertices[4].set(hiX, lowY, lowZ);
  vertices[5].set(hiX, lowY, hiZ);
  vertices[6].set(hiX, hiY, lowZ);
  vertices[7].set(hiX, hiY, hiZ);
}

CoreBBox::CoreBBox(const Vec4* t_vertices, const u32& count) {
  // Modified by TyraX: VU0 min/max scan - this constructor runs per frame
  // for skinned meshes (bbox recalculate) and per dynamic-pipeline bag.
  Vec4 minV, maxV;
  vec4ArrayMinMax(t_vertices, count, &minV, &maxV);
  const float lowX = minV.x, lowY = minV.y, lowZ = minV.z;
  const float hiX = maxV.x, hiY = maxV.y, hiZ = maxV.z;

  vertices[0].set(lowX, lowY, lowZ);
  vertices[1].set(lowX, lowY, hiZ);
  vertices[2].set(lowX, hiY, lowZ);
  vertices[3].set(lowX, hiY, hiZ);

  vertices[4].set(hiX, lowY, lowZ);
  vertices[5].set(hiX, lowY, hiZ);
  vertices[6].set(hiX, hiY, lowZ);
  vertices[7].set(hiX, hiY, hiZ);
}

CoreBBox::CoreBBox(const CoreBBox& t_bbox, const M4x4& t_matrix) {
  for (u32 i = 0; i < 8; i++) {
    vertices[i] = t_matrix * t_bbox.vertices[i];
  }
}

CoreBBox::CoreBBox(const CoreBBox& t_bbox) {
  for (auto i = 0; i < 8; i++)
    Vec4::copy(&vertices[i], t_bbox.vertices[i].xyzw);
}

CoreBBox CoreBBox::getTransformed(const M4x4& t_matrix) const {
  return CoreBBox(*this, t_matrix);
}

void CoreBBox::operator=(const CoreBBox& v) {
  for (auto i = 0; i < 8; i++) Vec4::copy(&vertices[i], v.vertices[i].xyzw);
}

CoreBBox::CoreBBox(const Vec4* t_vertices) {
  for (auto i = 0; i < 8; i++) Vec4::copy(&vertices[i], t_vertices[i].xyzw);
}

void CoreBBox::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void CoreBBox::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string CoreBBox::getPrint(const char* name) const {
  std::stringstream res;
  if (name) {
    res << name << "(";
  } else {
    res << "CoreBBox(";
  }
  res << std::fixed << std::setprecision(4);
  res << std::endl;
  for (auto i = 0; i < 8; i++) {
    res << i << ": " << vertices[i].x << ", " << vertices[i].y << ", "
        << vertices[i].z << ", " << vertices[i].w;

    if (i != 7)
      res << std::endl;
    else
      res << ")";
  }
  return res.str();
}

CoreBBox CoreBBox::create(const Vec4& center, const float& size) {
  CoreBBox bbox;

  float lowX = center.x - size;
  float lowY = center.y - size;
  float lowZ = center.z - size;

  float hiX = center.x + size;
  float hiY = center.y + size;
  float hiZ = center.z + size;

  bbox.vertices[0].set(lowX, lowY, lowZ);
  bbox.vertices[1].set(lowX, lowY, hiZ);
  bbox.vertices[2].set(lowX, hiY, lowZ);
  bbox.vertices[3].set(lowX, hiY, hiZ);

  bbox.vertices[4].set(hiX, lowY, lowZ);
  bbox.vertices[5].set(hiX, lowY, hiZ);
  bbox.vertices[6].set(hiX, hiY, lowZ);
  bbox.vertices[7].set(hiX, hiY, hiZ);

  return bbox;
}

CoreBBoxFrustum CoreBBox::frustumCheck(const Plane* frustumPlanes,
                                       const M4x4& model,
                                       const float* margins) const {
  CoreBBoxFrustum result = IN_FRUSTUM;
  u8 boxIn = 0, boxOut = 0;
  s8 calculatedBboxVertexIndex = -1;

  for (u8 i = 0; i < 6; i++) {
    const auto margin = margins == nullptr ? 0.0F : margins[i];
    boxOut = 0;
    boxIn = 0;

    // for each corner of the box do ...
    // get out of the cycle as soon as a box as corners
    // both inside and out of the frustum
    for (s8 y = 0; y < 8 && (boxIn == 0 || boxOut == 0); y++) {
      if (y > calculatedBboxVertexIndex) {
        frustumCheckVertices[y] = model * vertices[y];
        calculatedBboxVertexIndex = y;
      }

      auto isOut =
          frustumPlanes[i].distanceTo(frustumCheckVertices[y]) <= margin;

      if (isOut)
        boxOut++;
      else
        boxIn++;
    }

    // if all corners are out
    if (!boxIn)
      return OUTSIDE_FRUSTUM;
    else if (boxOut)
      result = PARTIALLY_IN_FRUSTUM;
  }
  return result;
}

CoreBBoxFrustum CoreBBox::frustumCheck(const Plane* frustumPlanes,
                                       const float* margins) const {
  CoreBBoxFrustum result = IN_FRUSTUM;
  u8 boxIn = 0, boxOut = 0;

  for (u8 i = 0; i < 6; i++) {
    const auto margin = margins == nullptr ? 0.0F : margins[i];
    boxOut = 0;
    boxIn = 0;

    for (s8 y = 0; y < 8 && (boxIn == 0 || boxOut == 0); y++) {
      auto isOut = frustumPlanes[i].distanceTo(vertices[y]) <= margin;

      if (isOut)
        boxOut++;
      else
        boxIn++;
    }

    // if all corners are out
    if (!boxIn)
      return OUTSIDE_FRUSTUM;
    else if (boxOut)
      result = PARTIALLY_IN_FRUSTUM;
  }
  return result;
}

bool CoreBBox::isInFrustum(const Plane* frustumPlanes,
                           const M4x4& model) const {
  s8 calculatedBboxVertexIndex = -1;

  for (u8 i = 0; i < 6; i++) {
    for (s8 y = 0; y < 8; y++) {
      if (y > calculatedBboxVertexIndex) {
        frustumCheckVertices[y] = model * vertices[y];
        calculatedBboxVertexIndex = y;
      }

      auto isIn = frustumPlanes[i].distanceTo(frustumCheckVertices[y]) > 0.0F;
      if (isIn) return true;
    }
  }
  return false;
}

bool CoreBBox::isInFrustum(const Plane* frustumPlanes) const {
  for (u8 i = 0; i < 6; i++) {
    for (s8 y = 0; y < 8; y++) {
      auto isIn = frustumPlanes[i].distanceTo(vertices[y]) > 0.0F;
      if (isIn) return true;
    }
  }

  return false;
}

// Modified by TyraX. The world-space distance the corner-based check
// computes is d + n . (M*v) with v.w == 1; regrouping by the components of v
// gives an object-space plane whose xyz is n * the rotation/scale part of M
// and whose distance folds in the translation. The d*column.w terms are
// intentionally dropped - the original test ignores the transformed vertex's
// w too, so this stays exactly the same math, just factored per object.
void CoreBBox::computeObjectSpacePlanes(Plane* out, const Plane* worldPlanes,
                                        const M4x4& model) {
  const float* m = model.data;
  for (u8 i = 0; i < 6; i++) {
    const Vec4& n = worldPlanes[i].normal;
    out[i].normal.x = n.x * m[0] + n.y * m[1] + n.z * m[2];
    out[i].normal.y = n.x * m[4] + n.y * m[5] + n.z * m[6];
    out[i].normal.z = n.x * m[8] + n.y * m[9] + n.z * m[10];
    out[i].normal.w = 1.0F;
    out[i].distance =
        worldPlanes[i].distance + n.x * m[12] + n.y * m[13] + n.z * m[14];
  }
}

// Modified by TyraX. Same in/partial/out contract as frustumCheck(): a box
// with every corner at distance <= 0 from some plane is outside, a box with
// corners on both sides of any plane is partial. Because the distance is
// linear in the corner coordinates, the corner that maximizes it is the
// per-axis sign pick (p-vertex) and the minimizing one is its mirror
// (n-vertex) - so two dot products replace the eight-corner sweep.
CoreBBoxFrustum CoreBBox::frustumCheckAABB(const Plane* objectSpacePlanes,
                                           const Vec4& min, const Vec4& max,
                                           u8* crossingMask) {
  CoreBBoxFrustum result = IN_FRUSTUM;
  if (crossingMask) *crossingMask = 0;

  for (u8 i = 0; i < 6; i++) {
    const Vec4& n = objectSpacePlanes[i].normal;
    const float& d = objectSpacePlanes[i].distance;

    const float pX = n.x >= 0.0F ? max.x : min.x;
    const float pY = n.y >= 0.0F ? max.y : min.y;
    const float pZ = n.z >= 0.0F ? max.z : min.z;
    const float maxDistance = d + n.x * pX + n.y * pY + n.z * pZ;

    if (maxDistance <= 0.0F) {
      if (crossingMask) *crossingMask = 0;
      return OUTSIDE_FRUSTUM;
    }

    if (result == IN_FRUSTUM || crossingMask) {
      const float nX = n.x >= 0.0F ? min.x : max.x;
      const float nY = n.y >= 0.0F ? min.y : max.y;
      const float nZ = n.z >= 0.0F ? min.z : max.z;
      const float minDistance = d + n.x * nX + n.y * nY + n.z * nZ;

      if (minDistance <= 0.0F) {
        result = PARTIALLY_IN_FRUSTUM;
        if (crossingMask) *crossingMask |= static_cast<u8>(1U << i);
      }
    }
  }

  return result;
}

}  // namespace Tyra

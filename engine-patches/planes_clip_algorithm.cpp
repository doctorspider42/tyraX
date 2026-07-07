/*
# Patched by tyra-editor (engine patch v2) - fast clipping via outcodes.
# Based on the original by Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#include "renderer/core/3d/clipper/planes_clip_algorithm.hpp"

namespace Tyra {

PlanesClipAlgorithm::PlanesClipAlgorithm() {
  tempVertices = new PlanesClipVertex[9];
}

PlanesClipAlgorithm::~PlanesClipAlgorithm() { delete[] tempVertices; }

float PlanesClipAlgorithm::clipMargin = -10.0F;

void PlanesClipAlgorithm::init(const RendererSettings& settings) {
  halfWidth = 0.5F;
  halfHeight = 0.5F;
  near = settings.getNear() - (-clipMargin);
  far = -settings.getFar();
}

u8 PlanesClipAlgorithm::clip(PlanesClipVertex* o_vertices,
                             PlanesClipVertexPtrs* i_vertices,
                             const EEClipAlgorithmSettings& settings) {
  // Cohen-Sutherland outcodes: one byte per vertex, six plane bits.
  u8 codes[3];
  for (int i = 0; i < 3; i++) {
    const Vec4& p = *i_vertices[i].position;
    if (p.w <= 0.0F) {
      codes[i] = 16;  // behind the eye plane - never trivially accepted
      continue;
    }
    u8 c = 0;
    if (p.x > halfWidth * p.w) c |= 1;
    if (p.x < -halfWidth * p.w) c |= 2;
    if (p.y > halfHeight * p.w) c |= 4;
    if (p.y < -halfHeight * p.w) c |= 8;
    if (p.z > near) c |= 16;
    if (p.z < far) c |= 32;
    codes[i] = c;
  }

  // Trivial reject: all three vertices outside the same plane.
  if (codes[0] & codes[1] & codes[2]) return 0;

  for (int i = 0; i < 3; i++) {
    o_vertices[i].position = *i_vertices[i].position;
    if (settings.lerpColors) o_vertices[i].color = *i_vertices[i].color;
    if (settings.lerpNormals) o_vertices[i].normal = *i_vertices[i].normal;
    if (settings.lerpTexCoords) o_vertices[i].st = *i_vertices[i].st;
  }

  // Trivial accept: all three vertices fully inside - no clipping needed.
  if ((codes[0] | codes[1] | codes[2]) == 0) return 3;

  // Real work only for triangles that actually cross a plane.
  u8 tempVerticesSize = 0;
  u8 outputSize = 0;

  tempVerticesSize =
      clipAgainstPlane(o_vertices, 3, tempVertices, 1, halfWidth, settings);

  outputSize = clipAgainstPlane(tempVertices, tempVerticesSize, o_vertices, 1,
                                -halfWidth, settings);

  tempVerticesSize = clipAgainstPlane(o_vertices, outputSize, tempVertices, 2,
                                      halfHeight, settings);

  outputSize = clipAgainstPlane(tempVertices, tempVerticesSize, o_vertices, 2,
                                -halfHeight, settings);

  tempVerticesSize =
      clipAgainstPlane(o_vertices, outputSize, tempVertices, 3, near, settings);

  outputSize = clipAgainstPlane(tempVertices, tempVerticesSize, o_vertices, 4,
                                far, settings);

  return outputSize;
}

float PlanesClipAlgorithm::getValueByPlane(const PlanesClipVertex& v,
                                           const int& plane) {
  switch (plane) {
    case 1:
      return v.position.x;  // x plane
    case 2:
      return v.position.y;  // y plane
    case 3:                 // z near
    case 4:
      return v.position.z;  // z far
    default:
      return 0;
  }
}

bool PlanesClipAlgorithm::isInside(const int& plane, const float& v,
                                   const float& w,
                                   const float& planeLimitValue) {
  switch (plane) {
    case 3:
      return v <= planeLimitValue;  // near z plane
    case 4:
      return v >= planeLimitValue;  // far z plane
    default:
      return (planeLimitValue < 0) ? (v >= planeLimitValue * w)
                                   : (v <= planeLimitValue * w);
  }
}

u8 PlanesClipAlgorithm::clipAgainstPlane(
    PlanesClipVertex* original, const u8& originalSize,
    PlanesClipVertex* clipped, const int& plane, const float& planeLimitValue,
    const EEClipAlgorithmSettings& settings) {
  int clippedSize = 0;

  for (u32 i = 0; i < originalSize; i++) {
    // const references - the original copied two 64-byte structs per edge
    const auto& a = original[i];
    const auto& b = original[(i + 1) % originalSize];
    const float apx = getValueByPlane(a, plane);
    const float bpx = getValueByPlane(b, plane);
    const bool aIsInside = isInside(plane, apx, a.position.w, planeLimitValue);
    const bool bIsInside = isInside(plane, bpx, b.position.w, planeLimitValue);

    if (aIsInside) {
      clipped[clippedSize++] = a;
    }

    if (aIsInside != bIsInside) {
      const float p =
          (plane >= 3)
              ? (planeLimitValue - a.position.z) / (b.position.z - a.position.z)
              : (-a.position.w * planeLimitValue + apx) /
                    ((b.position.w - a.position.w) * planeLimitValue -
                     (bpx - apx));

      const auto index = clippedSize++;

      clipped[index].position = Vec4::getByLerp(a.position, b.position, p);

      if (settings.lerpNormals)
        clipped[index].normal = Vec4::getByLerp(a.normal, b.normal, p);

      if (settings.lerpTexCoords)
        clipped[index].st = Vec4::getByLerp(a.st, b.st, p);

      if (settings.lerpColors)
        clipped[index].color = Vec4::getByLerp(a.color, b.color, p);
    }
  }

  return clippedSize;
}

}  // namespace Tyra

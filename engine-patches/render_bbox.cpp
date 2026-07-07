/*
# Patched by tyra-editor (engine patch v3) - guard-band aware frustum check.
# Based on the original by Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#include <string>
#include <sstream>
#include "renderer/core/3d/bbox/render_bbox.hpp"

namespace Tyra {

RenderBBox::RenderBBox(CoreBBox** t_bboxes, const u32& count)
    : CoreBBox(t_bboxes, count) {}

RenderBBox::RenderBBox(const std::vector<CoreBBox>& t_bboxes,
                       const u32& startIndex, const u32& stopIndex)
    : CoreBBox(t_bboxes, startIndex, stopIndex) {}

RenderBBox::RenderBBox(const Vec4* t_vertices, const u32* faces,
                       const u32& count)
    : CoreBBox(t_vertices, faces, count) {}

RenderBBox::RenderBBox(const Vec4* t_vertices, const u32& count)
    : CoreBBox(t_vertices, count) {}

RenderBBox::RenderBBox(const Vec4* t_vertices) : CoreBBox(t_vertices) {}

RenderBBox::RenderBBox(const RenderBBox& t_bbox, const M4x4& t_matrix)
    : CoreBBox(t_bbox, t_matrix) {}

RenderBBox RenderBBox::getTransformed(const M4x4& t_matrix) const {
  return RenderBBox(*this, t_matrix);
}

/**
 * Frustum check for the renderer.
 *
 * The VU1 cull programs accept a 3x wider XY window than the screen
 * (tyra-editor guard band patch) and the GS scissor trims the pixels, so
 * packages crossing the LEFT/RIGHT/TOP/BOTTOM planes render correctly on the
 * fast cull path with no geometric clipping.
 *
 * Only geometry close to the NEAR plane really needs the clipper: vertices
 * behind the eye cannot be scissored, they must be cut before the
 * perspective division.
 */
CoreBBoxFrustum RenderBBox::clipFrustumCheck(const Plane* frustumPlanes,
                                             const M4x4& model) const {
  auto result = frustumCheck(frustumPlanes, model);

  if (result != PARTIALLY_IN_FRUSTUM) {
    return result;
  }

  float guardBand[6];

  guardBand[0] = -100000.0F;  // Top    - handled by the GS scissor
  guardBand[1] = -100000.0F;  // Bottom - handled by the GS scissor
  guardBand[2] = -100000.0F;  // Left   - handled by the GS scissor
  guardBand[3] = -100000.0F;  // Right  - handled by the GS scissor
  guardBand[4] = 1.5F;        // Near   - the clipper must handle this band
  guardBand[5] = 0.0F;        // Far

  const auto withMargins = frustumCheck(frustumPlanes, model, guardBand);

  // Never skip on the second chance - worst case is a bit of extra clipping.
  return withMargins == OUTSIDE_FRUSTUM ? PARTIALLY_IN_FRUSTUM : withMargins;
}

}  // namespace Tyra

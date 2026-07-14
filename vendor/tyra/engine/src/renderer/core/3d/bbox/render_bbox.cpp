/*
# Modified by TyraX - clipFrustumCheck no longer re-runs the whole
# eight-corner frustum check a second time for PARTIALLY_IN_FRUSTUM boxes.
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
 * @brief Frustum checker for renderer.
 * Background: We want to really put as low as possible polys to clipper.
 * So we are doing magic trick. If BBox is partially inside frustum (clipper),
 * we are adding some margins, and checking again if it really needs clipping,
 * because "Cull" renderer can handle easy clip cases and its faster.
 */
CoreBBoxFrustum RenderBBox::clipFrustumCheck(const Plane* frustumPlanes,
                                             const M4x4& model) const {
  // Modified by TyraX: upstream re-ran the whole check with an all-zero
  // guard band for PARTIALLY_IN_FRUSTUM results - a second, identical
  // eight-corner sweep on exactly the boxes that are the most expensive to
  // process. A zero margin cannot change the answer, so the re-check is gone
  // until someone actually calibrates a non-zero band.
  return frustumCheck(frustumPlanes, model);
}

}  // namespace Tyra

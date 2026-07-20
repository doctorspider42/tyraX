/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: particle billboard expansion on VU1.
*/

#pragma once

#include "math/vec4.hpp"

namespace Tyra {

/**
 * Optional bag that switches a StaPipBag to the VU1 billboard program
 * family: `vertices` then carries PARTICLE CENTERS (xyz, w = 1) and each
 * center is expanded into a camera-facing quad (2 triangles, 6 GS
 * vertices) entirely on VU1.
 *
 * Channel reuse:
 *  - the texture bag is MANDATORY and its `coordinates` array carries one
 *    qword of 2x2 basis weights (m00, m01, m10, m11) per particle:
 *    halfAxisR = right*m00 + up*m01, halfAxisU = right*m10 + up*m11.
 *    `texture` may be nullptr - that selects the untextured program.
 *  - the color bag must be multi-color (one RGBA per particle).
 *  - lighting is unsupported; frustum culling must be Simple with
 *    fullClipChecks off (VU1 culls per quad - see the .vclpp).
 *
 * The basis below is the CAMERA right/up in world space, uploaded per
 * mesh (VU1_BILLBOARD_BASIS_ADDR). A second pass through another view
 * (e.g. a portal's virtual camera) can re-render the same bag after
 * swapping this basis - the centers never change.
 */
class StaPipBillboardBag {
 public:
  StaPipBillboardBag() : right(1.0F, 0.0F, 0.0F, 0.0F),
                         up(0.0F, 1.0F, 0.0F, 0.0F) {}
  ~StaPipBillboardBag() {}

  Vec4 right;
  Vec4 up;
};

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include "math/vec4.hpp"
#include "renderer/models/color.hpp"

namespace Tyra {

class PipelineLightingOptions {
 public:
  PipelineLightingOptions() {}
  ~PipelineLightingOptions() {}

  /**
   * Mandatory.
   * Default 128.0F, 128.0F, 128.0F, 128.0F
   */
  Color* ambientColor;

  /**
   * Mandatory.
   * Min/max length - 3.
   * Example color value: 64.0F, 0.0F, 0.0F, 1.0F
   */
  Color* directionalColors;

  /**
   * Mandatory.
   * Min/max length - 3.
   *
   * Modified by TyraX: documenting the wire layout, which is NOT three
   * direction vectors and has caught at least one caller out.
   *
   * The VU1 lighting macro (CalculateTyraDirectionalLights) consumes this as
   * a MATRIX, in the same column-major form as the light matrix beside it:
   *
   *     out = D[0] * n.x + D[1] * n.y + D[2] * n.z
   *
   * so out.i - the term that multiplies directionalColors[i] - works out to
   * the dot of ROW i with the normal. Light i's direction is therefore the
   * i-th row, i.e. the array holds the TRANSPOSE of the three directions:
   *
   *     D[0] = (L0.x, L1.x, L2.x)
   *     D[1] = (L0.y, L1.y, L2.y)
   *     D[2] = (L0.z, L1.z, L2.z)
   *
   * Filling it row-wise instead does not fail loudly: with a single light it
   * silently degrades to out.x = L0.x * n.x, so the mesh still shades - just
   * off the wrong component and scaled by one coordinate of the light.
   */
  Vec4* directionalDirections;
};

}  // namespace Tyra

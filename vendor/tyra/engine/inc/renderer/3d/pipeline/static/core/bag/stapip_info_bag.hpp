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

#include "renderer/3d/pipeline/shared/bag/pipeline_info_bag.hpp"

namespace Tyra {

class StaPipInfoBag : public PipelineInfoBag {
 public:
  StaPipInfoBag() {
    fullClipChecks = false;
    tightClipBand = false;
  }
  ~StaPipInfoBag() {}

  /**
   * @brief Experimental! False -> disables "clip against each plane" algorithm.
   * Default: True.
   *
   * Full clip checks are slow, but they are
   * preventing visual artifacts, which can happen
   * for big 3D objects (or objects near camera eyes)
   * Force enabled in dynamic pipe, because of efficiency.
   */
  bool fullClipChecks;

  /**
   * Modified by TyraX: cut this bag's crossing packages to the TIGHT X/Y band
   * (VU1_CLIP_XY_BAND_TIGHT) instead of the wide guard band. For large flat
   * textured geometry seen from close up - roads - whose triangles would
   * otherwise reach far off screen and make the console's GS texture swim.
   * Costs clip work on this bag only. Default false.
   */
  bool tightClipBand;
};

}  // namespace Tyra

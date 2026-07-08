/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Modified by tyra-editor: PipelineZTest_TestOnly (depth-tested, no z write).

#pragma once

namespace Tyra {

enum PipelineZTest {
  PipelineZTest_Standard = 0,
  PipelineZTest_AllPass = 1,
  /** Depth-tested but never writes Z (GS alpha-test all-fail + AFAIL=FB_ONLY
   * trick). For silhouette/outline passes drawn under other geometry. */
  PipelineZTest_TestOnly = 2,
};

}  // namespace Tyra

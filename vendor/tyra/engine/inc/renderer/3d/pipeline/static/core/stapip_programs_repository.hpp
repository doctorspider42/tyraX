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

#include "debug/debug.hpp"
#include "renderer/core/paths/path1/vu1_program.hpp"

#include "./programs/as_is/stapip_as_is_c_vu1_program.hpp"
#include "./programs/as_is/stapip_as_is_d_vu1_program.hpp"
#include "./programs/as_is/stapip_as_is_td_vu1_program.hpp"
#include "./programs/as_is/stapip_as_is_tc_vu1_program.hpp"

#include "./programs/cull/stapip_cull_c_vu1_program.hpp"
#include "./programs/cull/stapip_cull_d_vu1_program.hpp"
#include "./programs/cull/stapip_cull_td_vu1_program.hpp"
#include "./programs/cull/stapip_cull_tc_vu1_program.hpp"

// Modified by tyra-editor: VU1 clipping programs.
#include "./programs/clip/stapip_clip_c_vu1_program.hpp"
#include "./programs/clip/stapip_clip_d_vu1_program.hpp"
#include "./programs/clip/stapip_clip_td_vu1_program.hpp"
#include "./programs/clip/stapip_clip_tc_vu1_program.hpp"

namespace Tyra {

class StaPipProgramsRepository {
 public:
  StaPipProgramsRepository();
  ~StaPipProgramsRepository();

  StaPipVU1Program* getProgram(const StaPipProgramName& name);

 private:
  StaPipAsIsCVU1Program asIsColor;
  StaPipCullCVU1Program cullColor;
  StaPipAsIsDVU1Program asIsDirLights;
  StaPipCullDVU1Program cullDirLights;
  StaPipAsIsTDVU1Program asIsTextureDirLights;
  StaPipCullTDVU1Program cullTextureDirLights;
  StaPipAsIsTCVU1Program asIsTextureColor;
  StaPipCullTCVU1Program cullTextureColor;
  // Modified by tyra-editor: VU1 clipping programs.
  StaPipClipCVU1Program clipColor;
  StaPipClipDVU1Program clipDirLights;
  StaPipClipTDVU1Program clipTextureDirLights;
  StaPipClipTCVU1Program clipTextureColor;
};

}  // namespace Tyra

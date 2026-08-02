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

// Modified by TyraX: VU1 clipping programs.
#include "./programs/clip/stapip_clip_c_vu1_program.hpp"
#include "./programs/clip/stapip_clip_d_vu1_program.hpp"
#include "./programs/clip/stapip_clip_td_vu1_program.hpp"
#include "./programs/clip/stapip_clip_tc_vu1_program.hpp"

// Modified by TyraX: env (matcap) programs - reflective materials.
#include "./programs/cull/stapip_cull_tce_vu1_program.hpp"
#include "./programs/as_is/stapip_as_is_tce_vu1_program.hpp"
#include "./programs/clip/stapip_clip_tce_vu1_program.hpp"

// Modified by TyraX: particle billboard programs (centers -> quads on VU1).
#include "./programs/billboard/stapip_billboard_c_vu1_program.hpp"
#include "./programs/billboard/stapip_billboard_t_vu1_program.hpp"

namespace Tyra {

class StaPipProgramsRepository {
 public:
  StaPipProgramsRepository();
  ~StaPipProgramsRepository();

  StaPipVU1Program* getProgram(const StaPipProgramName& name);

  /** TyraX addition: let a GAME supply its own microprogram in place of a
   * built-in one (docs/vu-framework.md). The editor can generate a VU1 program
   * from a description the user authored without writing assembly; this is how
   * that program reaches the pipeline. Pass nullptr to go back to the built-in.
   *
   * A program only becomes resident once the cache is rebuilt and re-uploaded -
   * StaPipQBufferRenderer::setProgramOverride does both. Overriding a program
   * with a BIGGER one can push the ten-program set past VU1 micro memory; the
   * assert in Path1::createProgramsCache catches it in debug, and the editor's
   * VU panel reports the budget before you build. */
  void setOverride(const StaPipProgramName& name, StaPipVU1Program* program);

 private:
  StaPipAsIsCVU1Program asIsColor;
  StaPipCullCVU1Program cullColor;
  StaPipAsIsDVU1Program asIsDirLights;
  StaPipCullDVU1Program cullDirLights;
  StaPipAsIsTDVU1Program asIsTextureDirLights;
  StaPipCullTDVU1Program cullTextureDirLights;
  StaPipAsIsTCVU1Program asIsTextureColor;
  StaPipCullTCVU1Program cullTextureColor;
  // Modified by TyraX: VU1 clipping programs.
  StaPipClipCVU1Program clipColor;
  StaPipClipDVU1Program clipDirLights;
  StaPipClipTDVU1Program clipTextureDirLights;
  StaPipClipTCVU1Program clipTextureColor;
  // Modified by TyraX: env (matcap) programs - reflective materials.
  StaPipCullTCEVU1Program cullTextureEnv;
  StaPipAsIsTCEVU1Program asIsTextureEnv;
  StaPipClipTCEVU1Program clipTextureEnv;
  // Modified by TyraX: particle billboard programs.
  StaPipBillboardCVU1Program billboardColor;
  StaPipBillboardTVU1Program billboardTexture;

  /** Game-supplied replacements, indexed by StaPipProgramName. Null = use the
   * built-in member above. */
  static const int kOverrideSlots = 32;
  StaPipVU1Program* overrides[kOverrideSlots];
};

}  // namespace Tyra

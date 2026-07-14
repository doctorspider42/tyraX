/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/3d/pipeline/static/core/stapip_programs_repository.hpp"

namespace Tyra {

StaPipProgramsRepository::StaPipProgramsRepository() {}

StaPipProgramsRepository::~StaPipProgramsRepository() {}

StaPipVU1Program* StaPipProgramsRepository::getProgram(
    const StaPipProgramName& name) {
  switch (name) {
    case StaPipProgramName::StaPipAsIsColor:
      return &asIsColor;
    case StaPipProgramName::StaPipCullColor:
      return &cullColor;

    case StaPipProgramName::StaPipAsIsDirLights:
      return &asIsDirLights;
    case StaPipProgramName::StaPipCullDirLights:
      return &cullDirLights;

    case StaPipProgramName::StaPipAsIsTextureDirLights:
      return &asIsTextureDirLights;
    case StaPipProgramName::StaPipCullTextureDirLights:
      return &cullTextureDirLights;

    case StaPipProgramName::StaPipAsIsTextureColor:
      return &asIsTextureColor;
    case StaPipProgramName::StaPipCullTextureColor:
      return &cullTextureColor;

    // Modified by TyraX: VU1 clipping programs.
    case StaPipProgramName::StaPipClipColor:
      return &clipColor;
    case StaPipProgramName::StaPipClipDirLights:
      return &clipDirLights;
    case StaPipProgramName::StaPipClipTextureDirLights:
      return &clipTextureDirLights;
    case StaPipProgramName::StaPipClipTextureColor:
      return &clipTextureColor;

    // Modified by TyraX: env (matcap) programs - reflective materials.
    case StaPipProgramName::StaPipCullTextureEnv:
      return &cullTextureEnv;
    case StaPipProgramName::StaPipAsIsTextureEnv:
      return &asIsTextureEnv;

    default:
      TYRA_TRAP("Unknown VU1 program name");
      return &cullTextureDirLights;
  }
}

}  // namespace Tyra

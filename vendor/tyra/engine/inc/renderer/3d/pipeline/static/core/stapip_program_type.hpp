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

namespace Tyra {

enum StaPipProgramType {
  StaPipVU1Color,
  StaPipVU1DirLights,
  StaPipVU1TextureDirLights,
  StaPipVU1TextureColor,
  // Modified by TyraX: env (matcap) - texture + color, ST computed on VU1
  // from normals in the ST slot (reflective materials).
  StaPipVU1TextureEnvColor,
};

}  // namespace Tyra

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

enum StaPipProgramName {
  StaPipUndefinedProgram,

  StaPipCullColor,
  StaPipAsIsColor,

  StaPipCullDirLights,
  StaPipAsIsDirLights,

  StaPipCullTextureDirLights,
  StaPipAsIsTextureDirLights,

  StaPipCullTextureColor,
  StaPipAsIsTextureColor,

  // Modified by TyraX: VU1 clipping programs (replace the as_is family
  // + EE clipper when StaPipQBufferRenderer::setVU1Clipping(true) is active).
  StaPipClipColor,
  StaPipClipDirLights,
  StaPipClipTextureDirLights,
  StaPipClipTextureColor,

  // Modified by TyraX: env (matcap) variants - texture + color with the ST
  // computed on VU1 from normals (reflective materials).
  StaPipCullTextureEnv,
  StaPipAsIsTextureEnv,
  StaPipClipTextureEnv,
};

}  // namespace Tyra

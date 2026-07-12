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

  // Modified by tyra-editor: VU1 clipping programs (replace the as_is family
  // + EE clipper when StaPipQBufferRenderer::setVU1Clipping(true) is active).
  StaPipClipColor,
  StaPipClipDirLights,
  StaPipClipTextureDirLights,
  StaPipClipTextureColor,
};

}  // namespace Tyra

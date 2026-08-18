/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/3d/pipeline/static/core/bag/stapip_texture_bag.hpp"

namespace Tyra {

StaPipTextureBag::StaPipTextureBag() {
  coordinates = nullptr;
  texture = nullptr;
  coordinatesAreNormals = false;  // Modified by TyraX: env (matcap) mode
  textureFunction = 0;            // Added by TyraX: TEXTURE_FUNCTION_MODULATE
}

StaPipTextureBag::~StaPipTextureBag() {}

}  // namespace Tyra

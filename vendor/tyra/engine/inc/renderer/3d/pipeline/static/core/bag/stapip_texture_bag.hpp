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
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

class StaPipTextureBag {
 public:
  StaPipTextureBag();
  ~StaPipTextureBag();

  /** Mandatory. Texture coordinates per vertex. */
  Vec4* coordinates;

  /** Mandatory. Texture image. */
  Texture* texture;

  /**
   * Modified by TyraX: env (matcap) mode - the sphere-mapped pass of
   * reflective materials. When true, `coordinates` carries OBJECT-SPACE
   * NORMALS (one per vertex) and the texture ST is computed on VU1 from
   * this camera basis: s = 0.5 + 0.5*dot(n, right), t = 0.5 - 0.5*dot(n, up).
   * Requires no lighting bag and the EE-clipper pipeline (asserted in
   * StaPipCore::render - the VU1-clipping program set has no env variant).
   */
  bool coordinatesAreNormals;
  Vec4 envRight;
  Vec4 envUp;

  /**
   * Added by TyraX: the GS texture function (TFX) this bag draws with.
   * TEXTURE_FUNCTION_MODULATE (0) is the default and what every bag drew
   * before the field existed. HIGHLIGHT2 (3) is the NFS paint pass: the GS
   * computes RGB = Tex*VertexRGB>>7 + VertexALPHA, so a per-vertex fresnel
   * can ride the colour while a per-vertex WHITE specular rides the alpha -
   * two effects in ONE submit, with the additive FIX blend unchanged. TEX0
   * is (re)emitted per bag by the qbuffer renderer, which is what makes a
   * per-bag TFX safe on a shared texture.
   */
  u8 textureFunction;
};

}  // namespace Tyra

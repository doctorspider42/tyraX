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
#include "renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp"
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

class StaPipTextureBag {
 public:
  StaPipTextureBag();
  ~StaPipTextureBag();

  /** Mandatory. Texture coordinates per vertex. */
  Vec4* coordinates;

  /** Optional (TyraX addition). Stamp word for `coordinates`' CONTENTS - see
   * `StaPipBag::contentVersion`. nullptr = no tracking. In env (matcap) mode
   * this array carries object-space NORMALS, so the same stamp covers those. */
  const u32* contentVersion;

  /** Mandatory. Texture image. */
  Texture* texture;

  /**
   * Modified by TyraX: env (matcap) mode - the sphere-mapped pass of
   * reflective materials. When true, `coordinates` carries OBJECT-SPACE
   * NORMALS (one per vertex) and the texture ST is computed on VU1 from
   * this camera basis: s = 0.5 + 0.5*dot(n, right), t = 0.5 - 0.5*dot(n, up).
   * Requires no lighting bag. Both EE and VU1 clipping have env programs;
   * the optional paint prepass below requires VU1 clipping specifically.
   */
  bool coordinatesAreNormals;
  Vec4 envRight;
  Vec4 envUp;

#if TYRA_VU1_EXP_PAINT
  /** Opt-in HIGHLIGHT2 paint. Requires supportsPaintVu() at submission time,
   * many colours and unit object-space normals. The basis is the caller's
   * LAST ACCEPTED paint basis, independent of continuously updated envRight/
   * envUp. VU overwrites only its local colour stream before clipping; the
   * EE array may be stale until the caller explicitly materializes fallback. */
  bool paintVu = false;
  Vec4 paintForward;
  Vec4 paintHalf;
#endif

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

  /**
   * Added by TyraX: texture filtering for this bag, overriding the info bag's
   * textureMappingType. 0 = follow the info bag (the default, and what every
   * bag drew before the field existed), 1 = nearest (point sampled),
   * 2 = nearest when magnified + bilinear when minified ("pixel close-up").
   * TEX1 is emitted per bag in the object data, so like textureFunction it is
   * safe on a texture other bags sample smooth - an atlas page included.
   */
  u8 filter;
};

}  // namespace Tyra

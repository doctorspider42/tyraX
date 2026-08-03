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
#include "./stapip_info_bag.hpp"
#include "./stapip_color_bag.hpp"
#include "./stapip_lighting_bag.hpp"
#include "./stapip_texture_bag.hpp"
#include "./stapip_billboard_bag.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include "./packaging/stapip_bag_packages_bbox.hpp"

namespace Tyra {

/**
 * @brief 3D Render data bag.
 * Supports frustum culling, full plane clipping, lighting,
 * texture and single color / many colors.
 */
class StaPipBag {
 public:
  StaPipBag();
  ~StaPipBag();

  /** Mandatory. Object info. */
  StaPipInfoBag* info;

  /** Mandatory. Object color(s). */
  StaPipColorBag* color;

  /** Mandatory. Vertex count. */
  u32 count;

  /** Mandatory. Vertices. */
  Vec4* vertices;

  /** Optional. Texture coordinates and image. */
  StaPipTextureBag* texture;

  /** Optional. Object lighting. */
  StaPipLightingBag* lighting;

  /** Optional (TyraX addition). When set, `vertices` carries particle
   * CENTERS expanded to camera-facing quads on VU1 - see
   * StaPipBillboardBag for the channel layout and constraints. */
  StaPipBillboardBag* billboard;

  /** Bump whenever the content of `vertices` changes (TyraX addition).
   * The frustum-culling bbox cache is keyed by the vertex pointer plus this
   * version, so reused buffers with new data do not hit stale boxes. */
  u32 bboxVersion;

  /**
   * Optional (TyraX addition). Pins the VU1 package size for this bag instead
   * of deriving it from the bag's program class; 0 = derive as usual.
   *
   * Several bags may draw the SAME vertex array in coplanar passes (a
   * reflective object's additive env pass, a baked lightmap pass). The derived
   * size depends on the program class - an untextured base bag fits 108 verts
   * per package where its textured companion fits 72 - so the same array
   * splits at different boundaries, and one pass can classify a triangle
   * IN_FRUSTUM (perspective divide on VU1) while the other classifies it
   * PARTIALLY_IN_FRUSTUM (clipped on the EE, drawn `as_is`). The two routes
   * differ in the last bits of z and, at the frustum edge, in coverage - which
   * a coplanar GEQUAL test cannot survive. Pin every pass of an object to one
   * size and they classify identically.
   *
   * StaPipCore clamps this to the bag's own derived size (a class pinned above
   * its capacity would overflow the VU1 buffer) and to a multiple of 9, so the
   * value to pass is the MINIMUM over the passes that share the array.
   */
  u32 packageSize;

  /**
   * @param maxVertCount This parameter is available in renderer API.
   */
  StaPipBagPackagesBBox calculateBbox(const u32& maxVertCount);

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;
};

}  // namespace Tyra

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
   * version, so reused buffers with new data do not hit stale boxes.
   *
   * THIS IS A STATEMENT ABOUT THE BOUNDING BOX, i.e. about POSITIONS, and it
   * must stay that way - `StapipBagBBoxesCacher` is its consumer. For "the
   * bytes this bag's streams contain have changed", which is a different and
   * wider claim, see `contentVersion` below. */
  u32 bboxVersion;

  /**
   * Optional (TyraX addition). Points at a stamp word the OWNER of `vertices`
   * bumps whenever it rewrites that array's contents. nullptr = no tracking,
   * which is what every caller that does not opt in gets, and what the legacy
   * `StaticPipeline` path keeps.
   *
   * WHY THIS IS A POINTER AND NOT A VALUE, which is the whole design. A value
   * would have to be refreshed at every submit - one more line before every
   * `render()` call, i.e. another rule to remember, which is the thing this
   * exists to delete. A pointer is wired ONCE, where the stream pointer itself
   * is wired, and after that a write to the array updates what the engine
   * reads with no further cooperation from the caller. The obligation becomes
   * "bind the array", which cannot be forgotten, because a bag with no array
   * bound draws nothing.
   *
   * WHAT READS IT. The BAKED VIF STREAM cache only
   * (docs/baked-vif-stream.md). That cache INLINES the vertex payload into a
   * pre-built VIF block, so a re-write of a bag's array leaves the block stale
   * while every other field of its key is unchanged. The retained-command
   * cache deliberately does NOT read it: it stores the chain, whose `REF`s
   * still name the bag's own arrays, so re-written contents are followed at
   * DMA time and are always fresh - folding this in would rebuild its blocks
   * for nothing.
   *
   * The three companion bags carry their own, for the same reason and read by
   * the same cache: `StaPipColorBag::contentVersion` for `many`,
   * `StaPipTextureBag::contentVersion` for `coordinates`, and
   * `StaPipLightingBag::contentVersion` for `normals`. A bag that re-shades
   * per-vertex colours in place moves only the colour one.
   */
  const u32* contentVersion;

  /**
   * Optional (TyraX addition). Pins the VU1 package size for this bag instead
   * of deriving it from the bag's program class; 0 = derive as usual.
   *
   * Several bags may draw the SAME vertex array in coplanar passes (a
   * reflective object's additive env pass, a baked lightmap pass). The derived
   * size depends on the program class - an untextured base bag fits 111 verts
   * per package where its textured companion fits 75 - so the same array
   * splits at different boundaries, and one pass can classify a triangle
   * IN_FRUSTUM (perspective divide on VU1) while the other classifies it
   * PARTIALLY_IN_FRUSTUM (clipped on the EE, drawn `as_is`). The two routes
   * differ in the last bits of z and, at the frustum edge, in coverage - which
   * a coplanar GEQUAL test cannot survive. Pin every pass of an object to one
   * size and they classify identically.
   *
   * StaPipCore clamps this to the bag's own derived size (a class pinned above
   * its capacity would overflow the VU1 buffer) and to a multiple of 3, so the
   * value to pass is the MINIMUM over the passes that share the array.
   */
  u32 packageSize;

  /**
   * Optional (TyraX addition). `vertices` is a TRIANGLE STRIP, not a triangle
   * list: the GS takes one vertex per triangle after the first two, so a
   * shared vertex is packaged, transferred and transformed ONCE instead of
   * once per triangle that uses it. Everything the EE does per bag - bounds,
   * package creation, classification, packet construction, the send bracket -
   * scales with the VU1 package count, which scales with the vertex count, so
   * this is an EE saving before it is a VU1 or GIF one.
   *
   * The contract the submitter owes, all of it settled at BUILD time:
   *
   * - The array is chopped into independent RUNS of exactly `packageSize`
   *   vertices (the last one may be shorter), each a self-contained strip.
   *   `packageSize` must therefore be pinned and must not exceed the size the
   *   bag's program class derives, or StaPipCore's clamp would move the
   *   package boundaries off the run boundaries and fuse two strips.
   * - Every run length is a multiple of 3: the VU1 vertex loops step by three
   *   and a count that is not runs off the end of VU1 memory. Pad with a
   *   repeat of the last vertex - it makes a degenerate triangle, which the GS
   *   rasterises to nothing.
   * - Separate strips inside one run are joined by repeating a vertex on
   *   either side of the seam (degenerate triangles). Winding parity does NOT
   *   have to be preserved, because nothing in this engine backface-culls.
   *
   * Everything else is unchanged. The per-vertex ADC judgement the cull
   * programs write is `fcand 0x3FFFF` over the last three `clipw` results,
   * which for a strip is exactly the triangle that vertex kicks - so the
   * microprograms need no change at all and none was made. What DOES change is
   * the GIF tag's PRIM field (see StaPipQBuffer::stripped) and the clip route:
   * `clip_*` and the EE clipper are per-triangle, so StaPipCore expands a
   * package that genuinely crosses a clip plane back into a triangle list on
   * the EE, into the qbuffer copy pool, and clips that.
   */
  bool stripped;

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

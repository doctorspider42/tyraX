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

// Modified by tyra-editor: create() returns pointers into grow-only pools
// owned by the packager instead of new[] arrays - the per-submit heap
// round-trip was measurable on partially-visible geometry (hundreds of
// allocations per frame). Callers must NOT delete[] the result; it stays
// valid until the next create() call of the same overload.

#include <vector>

#include "renderer/core/3d/bbox/core_bbox.hpp"
#include "renderer/core/3d/renderer_3d_frustum_planes.hpp"
#include "renderer/core/3d/clipper/planes_clip_algorithm.hpp"
#include "./stapip_bag_packages_bbox.hpp"
#include "./stapip_bag_package.hpp"
#include "../stapip_bag.hpp"

namespace Tyra {

class StaPipBagPackager {
 public:
  StaPipBagPackager();
  ~StaPipBagPackager();

  void init(Renderer3DFrustumPlanes* frustumPlanes);
  void setRenderBBox(StaPipBagPackagesBBox* bbox) { renderBBox = bbox; }
  void setMaxVertCount(const u32& count);

  /**
   * @brief Create render packages from provided render data
   *
   * @param size Max maxVertCount verts (VU1 buffer size)
   */
  StaPipBagPackage* create(u16* o_size, StaPipBag* data, u16 size);
  /**
   * @brief Split render package to smaller packages
   *
   * @param size Max maxVertCount verts (VU1 buffer size)
   */
  StaPipBagPackage* create(u16* o_size, const StaPipBagPackage& pkg, u16 size);

  CoreBBoxFrustum checkFrustum(const StaPipBagPackage& pkg);

 private:
  u32 maxVertCount;
  Renderer3DFrustumPlanes* frustumPlanes;
  StaPipBagPackagesBBox* renderBBox;
  // Two pools because a bag-level package array is still in use while one of
  // its partial packages is split into subpackages (StaPipCore::renderPkgs).
  std::vector<StaPipBagPackage> bagPackagesPool;
  std::vector<StaPipBagPackage> splitPackagesPool;
};

}  // namespace Tyra

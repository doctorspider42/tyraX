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

#include "../stapip_bag.hpp"
#include "renderer/core/3d/bbox/core_bbox_frustum.hpp"

namespace Tyra {

class StaPipBagPackage {
 public:
  StaPipBagPackage();
  ~StaPipBagPackage();

  StaPipBag* bag;

  const Vec4* vertices;
  const Vec4* sts;
  const Vec4* normals;
  const Vec4* colors;
  /** maxVertCount is max value, because we decided to put max maxVertCount
   * verts to single quad buffer in VU1 */
  u16 size;
  CoreBBoxFrustum isInFrustum;

  /**
   * Modified by TyraX: conservative mask of frustum planes crossed by this
   * package's AABB. Populated only while StaPip telemetry is enabled for now;
   * false-positive bits cost work, but a missing bit must never reach VU1.
   */
  u8 clipPlaneMask;

  /**
   * We are creating StaPipBagPackagesBBox which checks CoreBBox for every
   * maxVertCount / 3. So this variable is index of starting
   * StaPipBagPackagesBBox's CoreBBox. If package have <= maxVertCount / 3
   * verts, we will need only single (starting) bbox. if package have
   * maxVertCount verts, we will calculate CoreBBox from 3
   * StaPipBagPackagesBBox's bboxes.
   */
  u32 indexOf1By3BBox;

  /**
   * Modified by TyraX: index of the LAST 1/3 CoreBBox the package
   * overlaps. VU1 clipping uses subpackages smaller than maxVertCount / 3,
   * which do not start on 1/3 boundaries - classifying them with only the
   * starting bbox misclassified visible geometry as outside (notches).
   */
  u32 endIndexOf1By3BBox;

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;
};

}  // namespace Tyra

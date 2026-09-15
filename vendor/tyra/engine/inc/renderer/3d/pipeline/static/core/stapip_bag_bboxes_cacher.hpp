/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Modified by TyraX: version-aware entries and a bounded hash index. A bag
// whose vertex buffer
// is rewritten in place (skinned meshes, particles) bumps its bboxVersion;
// the cacher recomputes that entry instead of piling up a new one per frame
// (250-frame retention made per-frame versions leak entries and allocations).

#pragma once

#include <tamtypes.h>
#include "./bag/packaging/stapip_bag_packages_bbox.hpp"
#include "renderer/3d/mesh/mesh.hpp"
#include <memory>
#include <vector>

namespace Tyra {

struct StapipBagBBoxesCacheItem {
  u32 vu1MaxVertCount;
  u32 id;
  u32 version;  // bag's bboxVersion at computation time
  std::unique_ptr<StaPipBagPackagesBBox> bboxes;
  int framesLeftToDestroy;
  int nextInBucket;
};

class StapipBagBBoxesCacher {
 public:
  StapipBagBBoxesCacher();
  ~StapipBagBBoxesCacher();

  const int cacheFramesCount = 50;
  const int cacheSecondsCount = 5;

  void onFrameEnd();

  StaPipBagPackagesBBox* getBBoxes(const Vec4* vertices, const u32& count,
                                   const u32& id, const u32& version,
                                   const u32& maxVertCount);

 private:
  static const u32 indexBucketCount = 256;

  StapipBagBBoxesCacheItem* getCache(const u32& maxVertCount, const u32& id);
  u32 getBucket(const u32& maxVertCount, const u32& id) const;
  void rebuildIndex();

  std::vector<StapipBagBBoxesCacheItem> storage;
  int indexBuckets[indexBucketCount];
};

}  // namespace Tyra

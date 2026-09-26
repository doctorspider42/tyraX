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
#include "./stapip_attrib.hpp"
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

#if TYRA_STAPIP_ATTRIB
  /**
   * Added by TyraX: attribution counters
   * (docs/render-submission-attribution.md). Compiled out by default with
   * everything else behind TYRA_STAPIP_ATTRIB - at 0 this class gains no
   * field and no increment.
   *
   * StaPipCore folds them into StaPipTelemetry::attrib in takeTelemetry()
   * and clears them there, so they reset on read like every other counter.
   * They are gathered unconditionally rather than under `telemetryEnabled`,
   * because the cacher has no view of that flag; the whole block only exists
   * in an instrumented build anyway.
   */
  struct Stats {
    u32 hits = 0;
    u32 recalcs = 0;
    u32 fresh = 0;
    u32 probes = 0;
    u32 entries = 0;
    u32 frameEndTicks = 0;
    u32 recalcTicks = 0;
  };
  Stats stats;
#endif

 private:
  static const u32 indexBucketCount = 256;

  StapipBagBBoxesCacheItem* getCache(const u32& maxVertCount, const u32& id);
  u32 getBucket(const u32& maxVertCount, const u32& id) const;
  void rebuildIndex();

  std::vector<StapipBagBBoxesCacheItem> storage;
  int indexBuckets[indexBucketCount];
};

}  // namespace Tyra

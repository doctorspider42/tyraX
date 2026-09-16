/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/3d/pipeline/static/core/stapip_bag_bboxes_cacher.hpp"
#include <algorithm>

namespace Tyra {

#if TYRA_STAPIP_ATTRIB
// Added by TyraX: the attribution pass' own clock, identical to the one
// StaPipCore uses. Compiled out with the counters it feeds.
static inline u32 readCacherAttribTicks() {
  u32 ticks;
  asm volatile("mfc0 %0, $9" : "=r"(ticks));
  return ticks;
}
#define TYRA_CACHER_INC(field) (++stats.field)
#else
#define TYRA_CACHER_INC(field) ((void)0)
#endif

StapipBagBBoxesCacher::StapipBagBBoxesCacher() {
  std::fill(indexBuckets, indexBuckets + indexBucketCount, -1);
}

StapipBagBBoxesCacher::~StapipBagBBoxesCacher() {}

void StapipBagBBoxesCacher::onFrameEnd() {
#if TYRA_STAPIP_ATTRIB
  const u32 attribStart = readCacherAttribTicks();
#endif
  for (auto& item : storage) {
    if (item.framesLeftToDestroy > 0) {
      item.framesLeftToDestroy--;
    }
  }

  const auto newEnd =
      std::remove_if(storage.begin(), storage.end(),
                     [](const StapipBagBBoxesCacheItem& item) {
                       return item.framesLeftToDestroy <= 0;
                     });
  if (newEnd != storage.end()) {
    storage.erase(newEnd, storage.end());
    rebuildIndex();
  }
#if TYRA_STAPIP_ATTRIB
  // This scan is per FRAME and lives outside StaPipCore::render, so it is
  // outside `boundsTicks` and every other shipped bracket - which is exactly
  // why nothing had ever measured it.
  stats.entries = static_cast<u32>(storage.size());
  stats.frameEndTicks += readCacherAttribTicks() - attribStart;
#endif
}

StaPipBagPackagesBBox* StapipBagBBoxesCacher::getBBoxes(
    const Vec4* vertices, const u32& count, const u32& id, const u32& version,
    const u32& maxVertCount) {
  auto* cache = getCache(maxVertCount, id);

  if (cache) {
    cache->framesLeftToDestroy = cacheFramesCount * cacheSecondsCount;
    // Modified by TyraX: same buffer, new content - recompute in
    // place; a changed vertex count needs a fresh part split. The version
    // check alone is not enough: games that free and reallocate vertex
    // buffers (layer streaming) can present a recycled heap address whose
    // version happens to equal the dead buffer's cached one - reusing those
    // boxes misclassifies packages and indexes past the cached part count.
    // A count mismatch exposes that case here; equal-count aliasing is
    // prevented by the process-unique version stamps generated games use.
    if (cache->version != version ||
        cache->bboxes->getVertexCount() != count) {
#if TYRA_STAPIP_ATTRIB
      const u32 recalcStart = readCacherAttribTicks();
#endif
      if (cache->bboxes->getVertexCount() == count) {
        TYRA_CACHER_INC(recalcs);
        cache->bboxes->recalculate(vertices, maxVertCount);
      } else {
        TYRA_CACHER_INC(fresh);
        cache->bboxes = std::make_unique<StaPipBagPackagesBBox>(vertices, count,
                                                                maxVertCount);
      }
#if TYRA_STAPIP_ATTRIB
      // Only the RECOMPUTE, so `bdCacheTicks` minus this is the pure lookup.
      stats.recalcTicks += readCacherAttribTicks() - recalcStart;
#endif
      cache->version = version;
    } else {
      TYRA_CACHER_INC(hits);
    }
    return cache->bboxes.get();
  }
  TYRA_CACHER_INC(fresh);

  auto bboxes =
      std::make_unique<StaPipBagPackagesBBox>(vertices, count, maxVertCount);

  const u32 bucket = getBucket(maxVertCount, id);
  storage.push_back(
      StapipBagBBoxesCacheItem{maxVertCount, id, version, std::move(bboxes),
                               cacheFramesCount * cacheSecondsCount,
                               indexBuckets[bucket]});

  indexBuckets[bucket] = static_cast<int>(storage.size() - 1);

  return storage.back().bboxes.get();
}

StapipBagBBoxesCacheItem* StapipBagBBoxesCacher::getCache(
    const u32& maxVertCount, const u32& id) {
  int itemIndex = indexBuckets[getBucket(maxVertCount, id)];
  while (itemIndex >= 0) {
    auto& item = storage[itemIndex];
    TYRA_CACHER_INC(probes);
    if (item.vu1MaxVertCount == maxVertCount && item.id == id) {
      return &item;
    }
    itemIndex = item.nextInBucket;
  }

  return nullptr;
}

u32 StapipBagBBoxesCacher::getBucket(const u32& maxVertCount,
                                     const u32& id) const {
  // Mix both parts of the cache key. indexBucketCount is a power of two, so
  // the mask is cheaper than division on the EE.
  u32 hash = id * 0x9E3779B1U;
  hash ^= maxVertCount + 0x85EBCA6BU + (hash << 6) + (hash >> 2);
  hash ^= hash >> 16;
  return hash & (indexBucketCount - 1);
}

void StapipBagBBoxesCacher::rebuildIndex() {
  std::fill(indexBuckets, indexBuckets + indexBucketCount, -1);
  for (u32 i = 0; i < storage.size(); ++i) {
    auto& item = storage[i];
    const u32 bucket = getBucket(item.vu1MaxVertCount, item.id);
    item.nextInBucket = indexBuckets[bucket];
    indexBuckets[bucket] = i;
  }
}

}  // namespace Tyra

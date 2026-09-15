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

StapipBagBBoxesCacher::StapipBagBBoxesCacher() {
  std::fill(indexBuckets, indexBuckets + indexBucketCount, -1);
}

StapipBagBBoxesCacher::~StapipBagBBoxesCacher() {}

void StapipBagBBoxesCacher::onFrameEnd() {
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
      if (cache->bboxes->getVertexCount() == count) {
        cache->bboxes->recalculate(vertices, maxVertCount);
      } else {
        cache->bboxes = std::make_unique<StaPipBagPackagesBBox>(vertices, count,
                                                                maxVertCount);
      }
      cache->version = version;
    }
    return cache->bboxes.get();
  }

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

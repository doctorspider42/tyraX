/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "math/math.hpp"

// Modified by TyraX: PipelineZTest_TestOnly branch in sendObjectData;
// per-mesh object-space spot light (flashlight) upload for the color VU1
// programs + EE clipper; alpha-test AFAIL fixed to ATEST_KEEP_ALL (see
// sendObjectData) so cutout textures stop stamping the z buffer.

#include <math.h>
#include <string.h>
#include <algorithm>
#include "debug/hardware_trace.hpp"
#include "renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp"
#include "renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h"
#include "renderer/core/gs/renderer_core_depth.hpp"
#include "packet2/packet2_tyra_utils.hpp"
#include "renderer/3d/pipeline/static/core/stapip_vu_tap.hpp"

// #define TYRA_QBUFF_RENDERER_VERBOSE_LOG 1

#ifdef TYRA_QBUFF_RENDERER_VERBOSE_LOG
#define Verbose(...) TyraDebug::writeLines("VRB: ", ##__VA_ARGS__, "\n")
#else
#define Verbose(...) ((void)0)
#endif

namespace Tyra {

static inline u32 readTelemetryTicks() {
  u32 result;
  asm volatile("mfc0 %0, $9" : "=r"(result));
  return result;
}

/**
 * VU1 = 1000 vert
 *
 * Quadbuffering:
 * 2 main buffers = 1000 / 2 = 500 vert
 * 2 kick buffers =  500 / 2 = 250 vert
 *
 * Vert data:
 * Pos + Normal + ST + Color = 4
 * = 4 * 48 = 192
 *
 * Other data:
 * mvp matrix, light matrix, tags = 14
 * 20 light vectors, light intesities = 25
 * = 14 + 25 = 39
 *
 * All data:
 * = 192 + 39 = 231
 *
 */

const u16 StaPipQBufferRenderer::buffersCount = 32;
namespace {
// Modified by TyraX: 57 packet qwords before the transient REF payloads were
// made inline, plus 4 MVP, 3 light-matrix, 3 direction and 1 colour qwords.
constexpr u16 kObjectDataPacketSize = 68;
constexpr u16 kSubmissionBatchSize = 4;
constexpr u16 kQBufferCommandCapacity = 32;  // keep with buffersCount
constexpr u16 kWorstBagPacketSize =
    4 * kQBufferCommandCapacity + kObjectDataPacketSize;

// Modified by TyraX: append quadwords that were already built as part of a DMA
// chain.
//
// Every tag this pipeline writes is POSITION-INDEPENDENT: a CNT tag counts the
// quadwords that follow it, a REF tag names an absolute address OUTSIDE the
// packet, and with TTE the VIF codes ride in the tag's own upper half. So a
// finished, quadword-aligned run of them means exactly the same thing at any
// quadword-aligned position in any packet, and copying one is indistinguishable
// from having built it there. Nothing here may be used for a tag that refers to
// its own position (NEXT, CALL) - the pipeline writes none, and
// docs/vu1-and-dma-cache-cost.md records why it must not start.
inline void appendChainQwords(packet2_t* packet, const qword_t* src,
                              u32 qwCount) {
  memcpy(packet->next, src, qwCount * 16);
  packet2_advance_next(packet, qwCount * 16);
}
}  // namespace

#if TYRA_STAPIP_RETAINED_COMMANDS

StaPipRetainedCommands::StaPipRetainedCommands() {
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
}

u32 StaPipRetainedCommands::getBucket(const void* vertices,
                                      u32 maxVertCount) const {
  u32 hash = static_cast<u32>(reinterpret_cast<u32>(vertices)) * 0x9E3779B1U;
  hash ^= maxVertCount + 0x85EBCA6BU + (hash << 6) + (hash >> 2);
  hash ^= hash >> 16;
  return hash & (kBucketCount - 1);
}

bool StaPipRetainedCommands::keyMatches(const StaPipRetainedEntry& a,
                                        const StaPipRetainedEntry& b) {
  return a.vertices == b.vertices && a.sts == b.sts && a.colors == b.colors &&
         a.normals == b.normals && a.program == b.program &&
         a.count == b.count && a.maxVertCount == b.maxVertCount &&
         a.bboxVersion == b.bboxVersion && a.primKey == b.primKey &&
         a.depthScaleBits == b.depthScaleBits &&
         a.singleColor == b.singleColor && a.stripped == b.stripped;
}

void StaPipRetainedCommands::rebuildIndex() {
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
  for (u32 i = 0; i < storage.size(); ++i) {
    auto& item = storage[i];
    const u32 bucket = getBucket(item.vertices, item.maxVertCount);
    item.nextInBucket = indexBuckets[bucket];
    indexBuckets[bucket] = static_cast<int>(i);
  }
}

void StaPipRetainedCommands::onFrameEnd() {
  evictionsThisFrame = 0;
  bool expired = false;
  for (auto& item : storage) {
    if (item.framesLeftToDestroy > 0) {
      item.framesLeftToDestroy--;
      if (item.framesLeftToDestroy <= 0) expired = true;
    }
  }
  if (!expired) return;

  u32 freed = 0;
  const auto newEnd = std::remove_if(
      storage.begin(), storage.end(), [&](const StaPipRetainedEntry& item) {
        if (item.framesLeftToDestroy > 0) return false;
        freed += static_cast<u32>(item.packages) * kMaxBlockQw;
        return true;
      });
  storage.erase(newEnd, storage.end());
  usedQwords -= freed < usedQwords ? freed : usedQwords;
  rebuildIndex();
}

void StaPipRetainedCommands::clear() {
  storage.clear();
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
  usedQwords = 0;
  evictionsThisFrame = 0;
}

// Modified by TyraX: the cap is a hard EE-RAM budget, so it has to be spent on
// the bags that are actually being drawn. Without this, the first bags to ask
// filled it and every later one was refused for the whole 250-frame lifetime -
// measured on the Motor District garage as ~25-40% of packages rebuilding every
// frame while the cache sat pinned at the cap holding geometry from a pose the
// camera had left. An entry this frame already touched is never a victim (its
// countdown is still at kLifetimeFrames), and the per-frame bound is what stops
// a scene whose working set genuinely does not fit from evicting and
// re-capturing the same bags forever - that would be strictly worse than not
// retaining them at all.
bool StaPipRetainedCommands::evictFor(u32 wanted) {
  while (usedQwords + wanted > kMaxQwords) {
    if (evictionsThisFrame >= kMaxEvictionsPerFrame) return false;
    int victim = -1;
    int oldest = kLifetimeFrames;  // strictly older than "touched this frame"
    for (u32 i = 0; i < storage.size(); ++i) {
      if (storage[i].framesLeftToDestroy < oldest) {
        oldest = storage[i].framesLeftToDestroy;
        victim = static_cast<int>(i);
      }
    }
    if (victim < 0) return false;
    const u32 freed =
        static_cast<u32>(storage[victim].packages) * kMaxBlockQw;
    usedQwords -= freed < usedQwords ? freed : usedQwords;
    storage.erase(storage.begin() + victim);
    rebuildIndex();
    ++evictionsThisFrame;
  }
  return true;
}

StaPipRetainedEntry* StaPipRetainedCommands::acquire(
    const StaPipRetainedEntry& key) {
  const u32 bucket = getBucket(key.vertices, key.maxVertCount);
  int index = indexBuckets[bucket];
  while (index >= 0) {
    auto& item = storage[index];
    if (item.vertices == key.vertices &&
        item.maxVertCount == key.maxVertCount) {
      item.framesLeftToDestroy = kLifetimeFrames;
      if (keyMatches(item, key)) return &item;
      // Something the block encodes moved - an LOD tier, a material, a
      // reallocated stream, a pipeline switch. Only the captured blocks are
      // thrown away; the storage is reused, or re-sized when the package count
      // moved with it.
      if (item.packages != key.packages) {
        const u32 had = static_cast<u32>(item.packages) * kMaxBlockQw;
        const u32 wants = static_cast<u32>(key.packages) * kMaxBlockQw;
        // A resize that does not fit is simply refused: the entry keeps its
        // old storage with every block invalidated, so this bag rebuilds until
        // room appears. Evicting here would have to consider evicting the very
        // entry being resized, and this case - a bag whose PACKAGE COUNT moved
        // while the cache is already full - is rare enough not to be worth it.
        if (key.packages == 0 || usedQwords - had + wants > kMaxQwords)
          return nullptr;
        item.data = std::unique_ptr<qword_t[]>(new qword_t[wants]);
        item.ready = std::unique_ptr<u8[]>(new u8[key.packages]);
        item.packages = key.packages;
        usedQwords = usedQwords - had + wants;
      }
      const StaPipRetainedEntry& k = key;
      item.sts = k.sts;
      item.colors = k.colors;
      item.normals = k.normals;
      item.program = k.program;
      item.count = k.count;
      item.bboxVersion = k.bboxVersion;
      item.primKey = k.primKey;
      item.depthScaleBits = k.depthScaleBits;
      item.singleColor = k.singleColor;
      item.stripped = k.stripped;
      item.blockQw = 0;
      memset(item.ready.get(), 0, item.packages);
      return &item;
    }
    index = item.nextInBucket;
  }

  const u32 wanted = static_cast<u32>(key.packages) * kMaxBlockQw;
  if (key.packages == 0 || wanted > kMaxQwords) return nullptr;
  if (usedQwords + wanted > kMaxQwords && !evictFor(wanted)) return nullptr;

  StaPipRetainedEntry entry;
  entry.vertices = key.vertices;
  entry.sts = key.sts;
  entry.colors = key.colors;
  entry.normals = key.normals;
  entry.program = key.program;
  entry.count = key.count;
  entry.maxVertCount = key.maxVertCount;
  entry.bboxVersion = key.bboxVersion;
  entry.primKey = key.primKey;
  entry.depthScaleBits = key.depthScaleBits;
  entry.singleColor = key.singleColor;
  entry.stripped = key.stripped;
  entry.packages = key.packages;
  entry.blockQw = 0;
  entry.data = std::unique_ptr<qword_t[]>(new qword_t[wanted]);
  entry.ready = std::unique_ptr<u8[]>(new u8[key.packages]);
  memset(entry.ready.get(), 0, key.packages);
  entry.framesLeftToDestroy = kLifetimeFrames;
  entry.nextInBucket = indexBuckets[bucket];

  storage.push_back(std::move(entry));
  indexBuckets[bucket] = static_cast<int>(storage.size() - 1);
  usedQwords += wanted;
  return &storage.back();
}

#endif  // TYRA_STAPIP_RETAINED_COMMANDS

// Modified by TyraX: the BAKED VIF STREAM spike - docs/baked-vif-stream.md.
#if TYRA_STAPIP_BAKED_STREAM

StaPipBakedStreams::StaPipBakedStreams() {
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
}

u32 StaPipBakedStreams::getBucket(const void* vertices,
                                  u32 maxVertCount) const {
  u32 hash = static_cast<u32>(reinterpret_cast<u32>(vertices)) * 0x9E3779B1U;
  hash ^= maxVertCount + 0x85EBCA6BU + (hash << 6) + (hash >> 2);
  hash ^= hash >> 16;
  return hash & (kBucketCount - 1);
}

bool StaPipBakedStreams::keyMatches(const StaPipBakedEntry& a,
                                    const StaPipBakedEntry& b) {
  return a.vertices == b.vertices && a.sts == b.sts && a.colors == b.colors &&
         a.normals == b.normals && a.program == b.program &&
         a.count == b.count && a.maxVertCount == b.maxVertCount &&
         a.bboxVersion == b.bboxVersion && a.primKey == b.primKey &&
         a.depthScaleBits == b.depthScaleBits &&
         a.programAddr == b.programAddr && a.singleColor == b.singleColor &&
         a.stripped == b.stripped;
}

void StaPipBakedStreams::rebuildIndex() {
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
  for (u32 i = 0; i < storage.size(); ++i) {
    auto& item = *storage[i];
    const u32 bucket = getBucket(item.vertices, item.maxVertCount);
    item.nextInBucket = indexBuckets[bucket];
    indexBuckets[bucket] = static_cast<int>(i);
  }
}

// Modified by TyraX: a baked block is READ BY DMA, which the retained blocks
// never are, so freeing one is not a local decision: the packet the pipeline
// submitted moments ago may still name it. Every free therefore goes through a
// two-frame graveyard. Two frames is not a guess about DMA speed - the static
// pipeline waits for VIF1 before every one of its ~120 submissions a frame, so
// one frame boundary already proves the transfer finished; the second is there
// because that count is a property of the scene and not of this class.
void StaPipBakedStreams::retire(StaPipBakedEntry& item) {
  const u32 qw = item.arenaQw;
  usedQwords -= qw < usedQwords ? qw : usedQwords;
  if (item.raw) graveyard[graveyardWrite].push_back(std::move(item.raw));
  item.arena = nullptr;
  item.arenaQw = 0;
}

void StaPipBakedStreams::onFrameEnd() {
  evictionsThisFrame = 0;
  graveyardWrite ^= 1;
  graveyard[graveyardWrite].clear();  // evicted two frames ago - now freed

  bool expired = false;
  for (auto& item : storage) {
    if (item->framesLeftToDestroy > 0) {
      item->framesLeftToDestroy--;
      if (item->framesLeftToDestroy <= 0) expired = true;
    }
  }
  if (!expired) return;

  for (auto& item : storage)
    if (item->framesLeftToDestroy <= 0) retire(*item);
  const auto newEnd =
      std::remove_if(storage.begin(), storage.end(),
                     [](const std::unique_ptr<StaPipBakedEntry>& item) {
                       return item->framesLeftToDestroy <= 0;
                     });
  storage.erase(newEnd, storage.end());
  rebuildIndex();
}

void StaPipBakedStreams::clear() {
  for (auto& item : storage) retire(*item);
  storage.clear();
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
  usedQwords = 0;
  evictionsThisFrame = 0;
}

bool StaPipBakedStreams::evictFor(u32 wanted) {
  while (usedQwords + wanted > kMaxQwords) {
    if (evictionsThisFrame >= kMaxEvictionsPerFrame) return false;
    int victim = -1;
    int oldest = kLifetimeFrames;  // never an entry this frame has touched
    for (u32 i = 0; i < storage.size(); ++i) {
      if (storage[i]->framesLeftToDestroy < oldest) {
        oldest = storage[i]->framesLeftToDestroy;
        victim = static_cast<int>(i);
      }
    }
    if (victim < 0) return false;
    retire(*storage[victim]);
    storage.erase(storage.begin() + victim);
    rebuildIndex();
    ++evictionsThisFrame;
  }
  return true;
}

bool StaPipBakedStreams::reserve(u32 qwords) {
  if (qwords > kMaxQwords) return false;
  if (usedQwords + qwords > kMaxQwords && !evictFor(qwords)) return false;
  usedQwords += qwords;
  return true;
}

StaPipBakedEntry* StaPipBakedStreams::acquire(const StaPipBakedEntry& key) {
  const u32 bucket = getBucket(key.vertices, key.maxVertCount);
  // Modified by TyraX: a SECOND PASS over one vertex array gets its own entry,
  // up to kVariantsPerArray of them.
  //
  // ATTRIBUTED, then routed around. The spike measured this cache rebuilding 64
  // package blocks a frame at a completely frozen pose, and STAPIPMISS named
  // the reason: `prim=2` per frame, in three large garage bags. The mechanism
  // is right here and is not a caller's fault - the walk below used to accept
  // the first entry matching (vertices, maxVertCount) ALONE and then overwrite
  // it whenever anything else in the key had moved. A bag drawn twice over one
  // array with two prim states therefore evicted its own other pass, every
  // frame, for ever. Two passes, one slot.
  //
  // So the walk now requires the prim state to match as well, and falls through
  // to a NEW entry when it does not. Both passes converge and `prim` goes to
  // zero.
  //
  // WHAT THIS DOES NOT CATCH, which matters more than what it does:
  //
  // - **It does nothing for the `bbox` half.** That is a caller bumping
  //   `bboxVersion` - a claim that the buffer's CONTENTS changed - on a scene
  //   that is not moving, and no key can route around a caller lying about its
  //   own data. `bboxVersion` must stay in the key: it is the ONLY signal that
  //   a rewritten-in-place array (skinned meshes, particles) changed, and
  //   dropping it would serve stale geometry. docs/wheel-rebake-skip.md records
  //   one caller already fixed this way; this one is still unnamed.
  // - **It costs memory, one whole arena per pass.** Two passes over a
  //   3 768-vertex array is two arenas, and a baked vertex costs about as much
  //   again as it already cost. That is why kVariantsPerArray is small.
  // - **It converts churn into occupancy, and would hide it.** A bag whose prim
  //   state genuinely varies every frame used to show up as `prim` and would
  //   now quietly allocate instead. The cap below is what stops that: past
  //   kVariantsPerArray the old replace-in-place behaviour returns, `prim`
  //   starts counting again, and the readout tells the truth either way.
  const u32 kVariantsPerArray = 3;
  u32 variants = 0;
  int index = indexBuckets[bucket];
  while (index >= 0) {
    auto& item = *storage[index];
    if (item.vertices == key.vertices &&
        item.maxVertCount == key.maxVertCount) {
      ++variants;
      const bool samePass = item.primKey == key.primKey &&
                            item.singleColor == key.singleColor &&
                            item.stripped == key.stripped;
      if (!samePass && variants < kVariantsPerArray) {
        index = item.nextInBucket;
        continue;  // a different pass over the same array - keep looking
      }
      item.framesLeftToDestroy = kLifetimeFrames;
      if (keyMatches(item, key)) return &item;
      // Something the stream encodes moved. Throw the arena away and
      // rebuild; the entry (and therefore any pointer the caller is holding)
      // survives, because storage owns pointers rather than values.
      //
      // Modified by TyraX: tally WHICH field moved before overwriting it. A
      // bag that invalidates at a frozen pose is either a caller lying about
      // its contents or a second pass over one array, and those want opposite
      // answers - so the readout has to distinguish them by name.
      MissReason reason = MissCountOrSize;
      if (item.bboxVersion != key.bboxVersion)
        reason = MissBBoxVersion;
      else if (item.primKey != key.primKey || item.singleColor != key.singleColor ||
               item.stripped != key.stripped)
        reason = MissPrimState;
      else if (item.sts != key.sts || item.colors != key.colors ||
               item.normals != key.normals)
        reason = MissStreams;
      else if (item.program != key.program ||
               item.programAddr != key.programAddr)
        reason = MissProgram;
      countMiss(reason);
      noteLoud(reason, key.count, key.packages);
      retire(item);
      if (item.packages != key.packages) {
        item.offsets =
            std::unique_ptr<u32[]>(new u32[key.packages ? key.packages : 1]);
        item.sizes =
            std::unique_ptr<u16[]>(new u16[key.packages ? key.packages : 1]);
        item.packages = key.packages;
      }
      item.sts = key.sts;
      item.colors = key.colors;
      item.normals = key.normals;
      item.program = key.program;
      item.count = key.count;
      item.bboxVersion = key.bboxVersion;
      item.primKey = key.primKey;
      item.depthScaleBits = key.depthScaleBits;
      item.programAddr = key.programAddr;
      item.singleColor = key.singleColor;
      item.stripped = key.stripped;
      item.built = 0;
      item.complete = 0;
      return key.packages == 0 ? nullptr : &item;
    }
    index = item.nextInBucket;
  }

  if (key.packages == 0) return nullptr;

  // Modified by TyraX: no entry for this (array, package size) at all. At a
  // frozen pose this should happen once per bag and never again; repeatedly is
  // a caller whose vertex array itself moves, which no key can cache.
  countMiss(MissNewEntry);
  noteLoud(MissNewEntry, key.count, key.packages);

  std::unique_ptr<StaPipBakedEntry> entry(new StaPipBakedEntry());
  entry->vertices = key.vertices;
  entry->sts = key.sts;
  entry->colors = key.colors;
  entry->normals = key.normals;
  entry->program = key.program;
  entry->count = key.count;
  entry->maxVertCount = key.maxVertCount;
  entry->bboxVersion = key.bboxVersion;
  entry->primKey = key.primKey;
  entry->depthScaleBits = key.depthScaleBits;
  entry->programAddr = key.programAddr;
  entry->singleColor = key.singleColor;
  entry->stripped = key.stripped;
  entry->packages = key.packages;
  entry->built = 0;
  entry->complete = 0;
  entry->arena = nullptr;
  entry->arenaQw = 0;
  entry->offsets = std::unique_ptr<u32[]>(new u32[key.packages]);
  entry->sizes = std::unique_ptr<u16[]>(new u16[key.packages]);
  entry->framesLeftToDestroy = kLifetimeFrames;
  entry->nextInBucket = indexBuckets[bucket];

  storage.push_back(std::move(entry));
  indexBuckets[bucket] = static_cast<int>(storage.size() - 1);
  return storage.back().get();
}

#endif  // TYRA_STAPIP_BAKED_STREAM

StaPipQBufferRenderer::StaPipQBufferRenderer() {
  currentBufferIndex = 0;
  nextBufferIndex = 0;
  context = 0;
  lastProgramName = StaPipUndefinedProgram;

  // Modified by TyraX: four bounded small direct bags can share one native
  // packet. Each retains its own worst-case uniforms and up to 15 command
  // groups; the extra headroom keeps packet construction fail-closed.
  packetSize = kSubmissionBatchSize * kWorstBagPacketSize;
  programsPacket = nullptr;
  billboardProgramsPacket = nullptr;
}

void StaPipQBufferRenderer::allocateOnUse() {
  rendererCore->texture.setMutationBarrier(textureMutationBarrier, this);
  staticDataPacket = packet2_create(3, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  // Modified by TyraX: +6 qwords for the spot light unpack, +16 for the
  // VU1 clipping constants + plane table.
  // Modified by TyraX: 42 -> 48 - the per-mesh ALPHA qword and the env
  // (matcap) camera basis added two unpack blocks to sendObjectData.
  // Modified by TyraX: 48 -> 52 - the billboard basis unpack (2 qwords
  // + headers).
  // Four inline lighting qwords replace the former REF payload.
  packets = new packet2_t*[2];
#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
  // Probe B (stapip_probes.hpp). 1 = P2_TYPE_UNCACHED, 2 = UNCACHED_ACCL.
  // packet2_create asserts qwords % 4 == 0 for either uncached type, so say so
  // here rather than discovering it as a trap on the console.
  static_assert((4 * (4 * 32 + 68)) % 4 == 0,
                "Probe B: an uncached packet2 needs a qword count that is a "
                "multiple of 4 (ps2sdk packet2_create). Keep packetSize so.");
  const enum Packet2Type probeType = TYRA_STAPIP_PROBE_UNCACHED_CHAIN == 2
                                         ? P2_TYPE_UNCACHED_ACCL
                                         : P2_TYPE_UNCACHED;
  for (u16 i = 0; i < 2; i++)
    packets[i] = packet2_create(packetSize, probeType, P2_MODE_CHAIN, true);
  probePacketUsesPool = false;
  // VERIFY THE ALLOCATION TOOK EFFECT BEFORE TRUSTING A SINGLE NUMBER FROM
  // THIS ARM. A packet that quietly came back in the normal segment would
  // measure "no flush needed" while the DMA read stale cache lines - the
  // failure mode is intermittent wrong geometry, not a crash. The segment is
  // the top nibble of the base pointer: 0x3 accelerated, 0x2 uncached.
  for (u16 i = 0; i < 2; i++) {
    TYRA_ASSERT(packets[i] != nullptr, "Probe B: packet2_create refused an "
                "uncached packet. Check packetSize % 4.");
    const u32 seg = reinterpret_cast<u32>(packets[i]->base) >> 28;
    TYRA_ASSERT(seg == (TYRA_STAPIP_PROBE_UNCACHED_CHAIN == 2 ? 0x3U : 0x2U),
                "Probe B: packet ", i, " is NOT in the uncached segment. "
                "base nibble: ", seg);
    TYRA_LOG("PROBEB: packet ", i, " base segment 0x", seg,
             " (1=uncached, 2=uncached-accelerated arm)");
  }
#else
  for (u16 i = 0; i < 2; i++)
    packets[i] =
        packet2_create(packetSize, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
#endif

  buffers = new StaPipQBuffer*[buffersCount];
  for (u16 i = 0; i < buffersCount; i++) {
    buffers[i] = new StaPipQBuffer();
  }
  // Modified by TyraX: these buffers hold no package size yet, so the cached
  // value that setMaxVertCount early-outs on must not claim they do. 0 is not
  // a legal size, so the next bag always propagates. See setMaxVertCount.
  maxVertCount = 0;

  dBufferPrograms = new StaPipVU1Program*[buffersCount];

  sendStaticData();
}

void StaPipQBufferRenderer::deallocateOnUse() {
  beforeTextureMutation();
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  rendererCore->texture.clearMutationBarrier(this);
  submissionBatchScope = false;
  submissionBatchCandidate = false;
#if TYRA_STAPIP_RETAINED_COMMANDS
  // Modified by TyraX: a scene teardown frees every stream the retained REF
  // tags name. Nothing may survive it - the pointer compare would only catch a
  // reallocation that came back at a DIFFERENT address.
  retainedCurrent = nullptr;
  retained.clear();
  clipBlockQw = 0;
#endif
#if TYRA_STAPIP_BAKED_STREAM
  // Same reasoning, and one more: a baked block is named by DMA REF tags, so
  // nothing may outlive the pipeline that submitted them.
  bakedCurrent = nullptr;
  bakeScratch.clear();
  baked.clear();
#endif
  packet2_free(staticDataPacket);
  for (u16 i = 0; i < 2; i++) packet2_free(packets[i]);
  delete[] packets;

  for (u16 i = 0; i < buffersCount; i++) delete buffers[i];
  delete[] buffers;

  delete[] dBufferPrograms;
}

StaPipQBufferRenderer::~StaPipQBufferRenderer() {
  if (programsPacket) packet2_free(programsPacket);
  if (billboardProgramsPacket) packet2_free(billboardProgramsPacket);
}

void StaPipQBufferRenderer::init(RendererCore* t_core, prim_t* t_prim,
                                 lod_t* t_lod) {
  path1 = t_core->getPath1();
  clipper.init(t_core->getSettings());
  rendererCore = t_core;
  prim = t_prim;
  lod = t_lod;

  // Modified by TyraX: the clip-space planes the VU1 clip programs cut
  // against - the same ones the EE clipper uses (see PlanesClipAlgorithm::init;
  // clipMargin must be set before setRenderer, like the EE path requires).
  clipNearZ =
      t_core->getSettings().getNear() - (-PlanesClipAlgorithm::clipMargin);
  clipFarZ = -t_core->getSettings().getFar();
#if TYRA_STAPIP_RETAINED_COMMANDS
  clipBlockQw = 0;  // Modified by TyraX: its inputs were just written.
#endif

  dma_channel_initialize(DMA_CHANNEL_VIF1, nullptr, 0);

  setProgramsCache();

  reinitVU1();

  TYRA_LOG("StaPipQBufferRenderer initialized");
}

void StaPipQBufferRenderer::reinitVU1() {
  flushPendingPacket();
  uploadPrograms();
  setDoubleBuffer();
}

namespace {
// Inverse of an affine model matrix (rotation * scale + translation).
// M4x4 is column-major: data[0..3] = X basis, [4..7] = Y, [8..11] = Z,
// [12..14] = translation. inv3x3 comes out row-major.
void invertAffine(const float* m, float* inv3x3, float* invT) {
  const float a = m[0], b = m[4], c = m[8];
  const float d = m[1], e = m[5], f = m[9];
  const float g = m[2], h = m[6], i = m[10];
  float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (det > -1e-12F && det < 1e-12F) det = 1e-12F;
  const float id = 1.0F / det;
  inv3x3[0] = (e * i - f * h) * id;
  inv3x3[1] = (c * h - b * i) * id;
  inv3x3[2] = (b * f - c * e) * id;
  inv3x3[3] = (f * g - d * i) * id;
  inv3x3[4] = (a * i - c * g) * id;
  inv3x3[5] = (c * d - a * f) * id;
  inv3x3[6] = (d * h - e * g) * id;
  inv3x3[7] = (b * g - a * h) * id;
  inv3x3[8] = (a * e - b * d) * id;
  invT[0] = -(inv3x3[0] * m[12] + inv3x3[1] * m[13] + inv3x3[2] * m[14]);
  invT[1] = -(inv3x3[3] * m[12] + inv3x3[4] * m[13] + inv3x3[5] * m[14]);
  invT[2] = -(inv3x3[6] * m[12] + inv3x3[7] * m[13] + inv3x3[8] * m[14]);
}

// Builds the object-space spot light for this mesh: transforms the world
// light through the inverse model matrix, expresses the range in object
// units via the (assumed near-uniform) mesh scale, and precomputes the
// constants the cull VU1 programs and the EE clipper both consume.
StaPipClipperSpot buildSpotForBag(const RendererCoreSpotLight& spot,
                                  const M4x4* model) {
  StaPipClipperSpot out;
  out.enabled = spot.enabled;
  if (!spot.enabled) return out;

  float inv[9], invT[3];
  invertAffine(model->data, inv, invT);

  out.position.x = inv[0] * spot.position.x + inv[1] * spot.position.y +
                   inv[2] * spot.position.z + invT[0];
  out.position.y = inv[3] * spot.position.x + inv[4] * spot.position.y +
                   inv[5] * spot.position.z + invT[1];
  out.position.z = inv[6] * spot.position.x + inv[7] * spot.position.y +
                   inv[8] * spot.position.z + invT[2];
  out.position.w = 1.0F;

  Vec4 dir;
  dir.x = inv[0] * spot.direction.x + inv[1] * spot.direction.y +
          inv[2] * spot.direction.z;
  dir.y = inv[3] * spot.direction.x + inv[4] * spot.direction.y +
          inv[5] * spot.direction.z;
  dir.z = inv[6] * spot.direction.x + inv[7] * spot.direction.y +
          inv[8] * spot.direction.z;
  float dirLen2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
  if (dirLen2 < 1e-10F) dirLen2 = 1.0F;

  // A world direction through the inverse scales by 1/s (uniform scale s),
  // so |dir|^2 = 1/s^2 - reuse it to express the range in object units.
  const float objRange2 = spot.range * spot.range * dirLen2;

  const float invDirLen = 1.0F / Math::sqrtNonNegative(dirLen2);
  out.direction.x = dir.x * invDirLen;
  out.direction.y = dir.y * invDirLen;
  out.direction.z = dir.z * invDirLen;
  out.direction.w = 0.0F;

  out.color[0] = spot.color.r;
  out.color[1] = spot.color.g;
  out.color[2] = spot.color.b;
  out.invRange2 = 1.0F / objRange2;

  // Point (omni) light through the SAME spot constants: zero direction makes
  // the axial term t = max(0, d.dir) collapse to 0, so the cone factor
  // becomes (0 - cosCut2*dist2)*invSoft = dist2 * invSoft with cosCut2 = -1.
  // invSoft is sized to saturate that to 1 within ~1% of the range - the
  // radial falloff alone shapes the light. No VU1 change, no micro memory.
  if (spot.point) {
    out.direction.x = 0.0F;
    out.direction.y = 0.0F;
    out.direction.z = 0.0F;
    out.cosCut2 = -1.0F;
    out.invSoft = 1.0e4F / objRange2;
    return out;
  }

  out.cosCut2 = spot.cosCutoff * spot.cosCutoff;
  // The VU1/EE cone term is clamp01((t^2 - cosCut2*dist2) * invSoft). Its
  // SIGN is the exact angular cutoff and is distance-independent, but its
  // MAGNITUDE scales with dist2 - so sizing invSoft off the full range
  // (objRange2 * (1 - cosCut2)) made the beam ramp up across the whole
  // range: dim on everything close to the lamp, fully bright only near its
  // far end. On the camera flashlight that reads as "it doesn't light what
  // I'm looking at". Saturate at a fraction of the range instead; the
  // cutoff ANGLE is unchanged, the edge just gets crisper.
  constexpr float kFullBrightAt = 0.18F;  // of the range, on the axis
  const float coneBase =
      objRange2 * kFullBrightAt * kFullBrightAt * (1.0F - out.cosCut2);
  out.invSoft = coneBase > 1e-10F ? spot.softness / coneBase : 0.0F;
  return out;
}
}  // namespace

// Modified by TyraX: the VU1 clipping uniform chain - one quadword of
// constants for the per-triangle crossing test plus the six clip planes as
// (A,B,C,D)+(E,0,0,0) pairs. Lifted out of sendObjectData unchanged so the
// retained-command path can capture exactly these bytes and replay them (see
// the call site); nothing in it depends on the mesh.
void StaPipQBufferRenderer::addClipChain(packet2_t* objectDataPacket) const {
  packet2_utils_vu_open_unpack(objectDataPacket, VU1_CLIP_CONSTS_ADDR, false);
  {
    packet2_add_float(objectDataPacket, clipNearZ - VU1_CLIP_GUARD);
    packet2_add_float(objectDataPacket, -clipFarZ - VU1_CLIP_GUARD);
    packet2_add_float(objectDataPacket, 0.0F);
    packet2_add_float(objectDataPacket, VU1_CLIP_GUARD);
  }
  packet2_utils_vu_close_unpack(objectDataPacket);

  const float planes[6][8] = {
      // near: z <= clipNearZ (exact, matches the EE clipper)
      {0.0F, 0.0F, -1.0F, 0.0F, clipNearZ, 0.0F, 0.0F, 0.0F},
      // far: z >= clipFarZ (exact, matches the EE clipper)
      {0.0F, 0.0F, 1.0F, 0.0F, -clipFarZ, 0.0F, 0.0F, 0.0F},
      // guard band X/Y at +/-VU1_CLIP_XY_BAND * w - strictly inside the
      // GS raster window; the scissor trims the rest of the way
      {-1.0F, 0.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
      {1.0F, 0.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, -1.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
  };
  packet2_utils_vu_open_unpack(objectDataPacket, VU1_CLIP_PLANES_ADDR, false);
  {
    for (u32 i = 0; i < 6; i++)
      for (u32 j = 0; j < 8; j++)
        packet2_add_float(objectDataPacket, planes[i][j]);
  }
  packet2_utils_vu_close_unpack(objectDataPacket);
}

void StaPipQBufferRenderer::sendObjectData(
    StaPipBag* bag, M4x4* mvp, RendererCoreTextureBuffers* texBuffers) {
  // Modified by TyraX: build uniforms at the head of the same packet that the
  // first geometry flush will append to. This removes one VIF1 DMA launch and
  // its intervening EE wait per visible bag without relying on a NEXT/CALL
  // jump between separately allocated chains (which is not stable on hardware).
  // The other packet was completed before sendPacket flipped back to this
  // context, so it is safe to reuse without adding a preparation-side wait.
  auto* objectDataPacket = packets[context];
  if (!objectDataPending) packet2_reset(objectDataPacket, false);
  // Modified by TyraX: everything in this chain lands at an ABSOLUTE VU1
  // address (MVP 0..3, OPTIONS 8, CLIP_CONSTS 20, ALPHA 21, the clip planes at
  // 944..955) and the microprograms read those inside their loops. The DMA
  // wait before this packet only proves the previous CHAIN was consumed - its
  // final MSCAL merely started the last batch, and that program may still be
  // running (parked on an XGKICK for as long as the GS takes to fill a big
  // translucent triangle). The VIF performs absolute unpacks immediately and
  // stalls only at the next MSCAL, so without a barrier the next mesh's matrix
  // and plane table land mid-draw: one vertex through the wrong MVP is a wedge
  // to the screen corner, a swapped plane table cuts or drops the polygon.
  // PCSX2 never overlaps a VIF unpack with a running microprogram, so it
  // cannot show this; a real PS2 does (docs/vu1-clipping.md). FLUSHE = wait
  // for the end of the microprogram; the halves the GIF may still be reading
  // are not touched by this chain, so no PATH1 drain is needed here.
  packet2_chain_open_cnt(objectDataPacket, 0, 0, 0);
  packet2_vif_flushe(objectDataPacket, 0);
  packet2_vif_nop(objectDataPacket, 0);
  packet2_chain_close_tag(objectDataPacket);
  if (submissionBatchCandidate) {
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_MVP_MATRIX_ADDR, false);
    const float* mvpData = reinterpret_cast<const float*>(mvp->data);
    for (u32 i = 0; i < 16; ++i)
      packet2_add_float(objectDataPacket, mvpData[i]);
    packet2_utils_vu_close_unpack(objectDataPacket);
  } else {
    packet2_utils_vu_add_unpack_data(objectDataPacket, VU1_MVP_MATRIX_ADDR,
                                     mvp->data, 4, false);
  }

  if (bag->lighting) {
    if (submissionBatchCandidate) {
      packet2_utils_vu_open_unpack(objectDataPacket, VU1_LIGHTS_MATRIX_ADDR,
                                   false);
      const float* lightMatrix =
          reinterpret_cast<const float*>(bag->lighting->lightMatrix->data);
      for (u32 i = 0; i < 12; ++i)
        packet2_add_float(objectDataPacket, lightMatrix[i]);
      packet2_utils_vu_close_unpack(objectDataPacket);

      packet2_utils_vu_open_unpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR,
                                   false);
      const Vec4* directions = bag->lighting->dirLights->getLightDirections();
      for (u32 i = 0; i < 3; ++i) {
        packet2_add_float(objectDataPacket, directions[i].x);
        packet2_add_float(objectDataPacket, directions[i].y);
        packet2_add_float(objectDataPacket, directions[i].z);
        packet2_add_float(objectDataPacket, directions[i].w);
      }
      packet2_utils_vu_close_unpack(objectDataPacket);
    } else {
      packet2_utils_vu_add_unpack_data(objectDataPacket,
                                       VU1_LIGHTS_MATRIX_ADDR,
                                       bag->lighting->lightMatrix, 3, false);
      packet2_utils_vu_add_unpack_data(
          objectDataPacket, VU1_LIGHTS_DIRS_ADDR,
          bag->lighting->dirLights->getLightDirections(), 3, false);
    }
    // add_unpack_data emits a DMA REF, not a copy. The mode-adjusted
    // colors must live in the packet, never in a temporary stack array.
    const Vec4* colors = bag->lighting->dirLights->getLightColors();
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_LIGHTS_COLORS_ADDR, false);
    for (int i = 0; i < 4; ++i) {
      packet2_add_float(objectDataPacket, colors[i].x);
      packet2_add_float(objectDataPacket, colors[i].y);
      packet2_add_float(objectDataPacket, colors[i].z);
      packet2_add_float(objectDataPacket, i == 3
          ? (bag->lighting->dirLights->signedSH ? -1.0F : 0.0F) : colors[i].w);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  // Modified by TyraX: dynamic light for the color programs - the per-bag
  // pick from StaPipCore (flashlight or the strongest scene point light),
  // falling back to the global flashlight state when no pick was made.
  // The dir-lights addresses are free when the bag has no lighting - the
  // C/TC programs read the three spot quads from there. Always uploaded and
  // the same numbers go to the EE clipper for the as_is path.
  //
  // Modified by TyraX: the quads are uploaded unconditionally, but the VU1
  // ARITHMETIC is not - `spotActive` below rides in VU1_OPTIONS_ADDR.y and
  // the cull/clip colour programs branch over CalculateTyraSpotLight when it
  // is clear. `enabled` is exactly the predicate the EE clipper's
  // addSpotToColor already used, so the two halves of the formula are gated
  // by one fact and a skipped mesh renders bit-identically (an inert light
  // uploads a black colour, and colour * anything is 0 on VU1).
  bool spotActive = false;
  if (!bag->lighting) {
    const auto& light = bagLight ? *bagLight : rendererCore->spot;
    const auto meshSpot = buildSpotForBag(light, bag->info->model);
    clipper.setSpot(meshSpot);
    spotActive = meshSpot.enabled;

    packet2_utils_vu_open_unpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, false);
    {
      packet2_add_float(objectDataPacket, meshSpot.position.x);
      packet2_add_float(objectDataPacket, meshSpot.position.y);
      packet2_add_float(objectDataPacket, meshSpot.position.z);
      packet2_add_float(objectDataPacket, meshSpot.invRange2);
      packet2_add_float(objectDataPacket, meshSpot.direction.x);
      packet2_add_float(objectDataPacket, meshSpot.direction.y);
      packet2_add_float(objectDataPacket, meshSpot.direction.z);
      packet2_add_float(objectDataPacket, meshSpot.cosCut2);
      packet2_add_float(objectDataPacket, meshSpot.enabled ? meshSpot.color[0]
                                                           : 0.0F);
      packet2_add_float(objectDataPacket, meshSpot.enabled ? meshSpot.color[1]
                                                           : 0.0F);
      packet2_add_float(objectDataPacket, meshSpot.enabled ? meshSpot.color[2]
                                                           : 0.0F);
      packet2_add_float(objectDataPacket, meshSpot.invSoft);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);

    // Modified by TyraX: the two quadwords a project's own microprogram reads
    // (docs/vu-authoring.md). Inside the `if (!bag->lighting)` on purpose -
    // they occupy the directional-lights COLOUR block, which a lit bag needs.
    if (vuCustomEnabled) {
      packet2_utils_vu_open_unpack(objectDataPacket, VU1_CUSTOM_PARAMS_ADDR,
                                   false);
      {
        for (u32 i = 0; i < 4; i++)
          packet2_add_float(objectDataPacket, vuParams[i]);
        for (u32 i = 0; i < 4; i++)
          packet2_add_float(objectDataPacket, vuTime[i]);
      }
      packet2_utils_vu_close_unpack(objectDataPacket);
    }
  }

  // Modified by TyraX: VU1 clipping data. One quad of constants for the
  // per-triangle crossing test (see stapip_vu1_shared_defines.h) and the six
  // clip planes as (A,B,C,D)+(E,0,0,0) pairs; inside = dot4(v,ABCD) + E >= 0.
  // Uploaded per mesh: other pipelines may reuse this VU1 memory in between.
  if (vu1Clipping) {
#if TYRA_STAPIP_RETAINED_COMMANDS
    // Modified by TyraX: retained command data. These fifteen quadwords are
    // the same for every mesh in the run - their only inputs are the
    // renderer's near/far pair and the guard-band constant - so the chain is
    // captured once out of the packet addClipChain just wrote it into and
    // replayed with a memcpy afterwards. 52 float stores per bag became one
    // copy. init() and setVU1Clipping() are the only places those inputs can
    // move and both drop the capture.
    if (clipBlockQw != 0) {
      appendChainQwords(objectDataPacket, clipBlock, clipBlockQw);
    } else {
      const u32 clipStart = packet2_get_qw_count(objectDataPacket);
      addClipChain(objectDataPacket);
      const u32 qw = packet2_get_qw_count(objectDataPacket) - clipStart;
      if (qw > 0 && qw <= (sizeof(clipBlock) / sizeof(clipBlock[0]))) {
        memcpy(clipBlock,
               reinterpret_cast<const u8*>(objectDataPacket->base) +
                   clipStart * 16,
               qw * 16);
        clipBlockQw = static_cast<u16>(qw);
      }
    }
#else
    addClipChain(objectDataPacket);
#endif
  }

  u8 singleColorEnabled = bag->color->single != nullptr;

  if (singleColorEnabled) {  // Color is placed in 4th slot of
                            // VU1_LIGHTS_MATRIX_ADDR
    if (submissionBatchCandidate) {
      packet2_utils_vu_open_unpack(objectDataPacket, VU1_SINGLE_COLOR_ADDR,
                                   false);
      for (u32 i = 0; i < 4; ++i)
        packet2_add_float(objectDataPacket, bag->color->single->rgba[i]);
      packet2_utils_vu_close_unpack(objectDataPacket);
    } else {
      Packet2TyraUtils::addUnpackData(objectDataPacket, VU1_SINGLE_COLOR_ADDR,
                                      bag->color->single->rgba, 1, false);
    }
  }

  packet2_utils_vu_open_unpack(objectDataPacket, VU1_OPTIONS_ADDR, false);
  {
    const u32 sharedClipVariant =
        bag->lighting != nullptr ||
                (bag->texture != nullptr && bag->texture->coordinatesAreNormals)
            ? 1
            : 0;
    packet2_add_u32(objectDataPacket,
                    singleColorEnabled);   // Single color enabled.
    // Static-pipeline-only use of the old dynpip lerp lane: C/D and TC/TCE
    // share one clip image per ABI-compatible pair. Other programs ignore it.
    //
    // Modified by TyraX: the lane is THREE-STATE now. Every reader that
    // predates this tested only `> 0` versus `<= 0` (clip_c's three `iblez`,
    // clip_tc's `ibgtz`), so the negative half was free to carry a second
    // fact: the colour programs run CalculateTyraSpotLight only when it is
    // negative. The two cannot collide - the peer variant is selected by a
    // lighting bag or matcap coordinates, and neither of those classes has
    // the spot macro at all - so the variant wins when both are true.
    const s32 variantLane = sharedClipVariant ? 1 : (spotActive ? -1 : 0);
    packet2_add_u32(objectDataPacket, static_cast<u32>(variantLane));
    // Modified by TyraX: GS hardware fog params (see RendererCoreFog)
    packet2_add_float(objectDataPacket, rendererCore->fog.scale);
    packet2_add_float(objectDataPacket, rendererCore->fog.offset);

    packet2_utils_gs_add_lod(objectDataPacket, lod);

    // Modified by TyraX: the destination-alpha gate (PipelineInfoBag::
    // dateLit) rides the same in-band TEST qword every mesh already emits -
    // DATE = 1 draws this bag's pixels only where the framebuffer alpha's
    // MSB is 0, which is how the flashlight's shadow volumes mask its light.
    const int date = bag->info->dateLit ? 1 : 0;
    if (bag->info->zTestType == PipelineZTest_AllPass) {
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(0, 0, 0, 0, date, 0, 0, ZTEST_METHOD_ALLPASS),
          GS_REG_TEST);
    } else if (bag->info->zTestType == PipelineZTest_TestOnly) {
      // Depth-tested, no z write: alpha test fails every pixel and AFAIL
      // keeps the z buffer (GS FB_ONLY - color still written). The ZBUF
      // register (and thus the VU1 options layout) stays untouched.
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_ALLFAIL, 0x00,
                      ATEST_KEEP_ZBUFFER, date, DRAW_DISABLE,
                      DRAW_ENABLE, rendererCore->gs.zBuffer.method),
          GS_REG_TEST);
    } else {
      // Cutout alpha: texels with alpha 0 fail the test and must write
      // NOTHING. Upstream passed ATEST_KEEP_FRAMEBUFFER, whose ps2sdk name
      // reads backwards - it is AFAIL=ZB_ONLY (2), "keep the framebuffer,
      // update z". So every transparent texel stamped the z buffer while
      // drawing no colour, and the invisible part of an alpha-cutout card
      // (foliage, decals, grates) occluded whatever was drawn behind it
      // later. ATEST_KEEP_ALL (0) leaves both buffers alone, which is what a
      // cutout means. Opaque geometry carries alpha 0x80 and never fails the
      // test, so nothing else changes.
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_NOTEQUAL, 0x00, ATEST_KEEP_ALL,
                      date, DRAW_DISABLE, DRAW_ENABLE,
                      rendererCore->gs.zBuffer.method),
          GS_REG_TEST);
    }

    if (texBuffers != nullptr) {
      rendererCore->texture.updateClutBuffer(texBuffers->clut);

      // Added by TyraX: per-bag GS texture function (StaPipTextureBag - the
      // NFS paint pass draws HIGHLIGHT2). TEX0 is emitted per bag right
      // here, so writing the shared texbuffer_t immediately before the emit
      // gives every bag its own TFX - the next bag on the same texture
      // overwrites it again, which is exactly why the mutation is safe.
      texBuffers->core->info.function = bag->texture->textureFunction;
      packet2_utils_gs_add_texbuff_clut(objectDataPacket, texBuffers->core,
                                        &rendererCore->texture.clut);
    }
  }
  packet2_utils_vu_close_unpack(objectDataPacket);

  // Modified by TyraX: particle billboard camera basis (right, up - world
  // space). Reuses the lights-matrix area like the env basis; billboard
  // bags never carry lighting (asserted in StaPipCore::render). Swapping
  // this basis and re-rendering the same bag draws the same centers for
  // another view (e.g. a portal's virtual camera).
  if (bag->billboard != nullptr) {
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_BILLBOARD_BASIS_ADDR,
                                 false);
    {
      packet2_add_float(objectDataPacket, bag->billboard->right.x);
      packet2_add_float(objectDataPacket, bag->billboard->right.y);
      packet2_add_float(objectDataPacket, bag->billboard->right.z);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, bag->billboard->up.x);
      packet2_add_float(objectDataPacket, bag->billboard->up.y);
      packet2_add_float(objectDataPacket, bag->billboard->up.z);
      packet2_add_float(objectDataPacket, 0.0F);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  // Modified by TyraX: env (matcap) camera basis for the TCE programs.
  // Transpose it and fold in the ST scale here so CalculateTyraEnvStq can
  // evaluate both scaled dot products in one VU1 accumulator chain. Reuses
  // the lights-matrix area; env bags never carry lighting.
  if (bag->texture != nullptr && bag->texture->coordinatesAreNormals) {
    const Vec4& r = bag->texture->envRight;
    const Vec4& u = bag->texture->envUp;
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_ENV_BASIS_ADDR, false);
    {
      packet2_add_float(objectDataPacket, r.x * 0.5F);
      packet2_add_float(objectDataPacket, u.x * -0.5F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, r.y * 0.5F);
      packet2_add_float(objectDataPacket, u.y * -0.5F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, r.z * 0.5F);
      packet2_add_float(objectDataPacket, u.z * -0.5F);
      packet2_add_float(objectDataPacket, 1.0F);
      packet2_add_float(objectDataPacket, 0.5F);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  // Modified by TyraX: per-mesh GS blend equation, emitted in-band with the
  // other tags by every program (StoreTyraGifTags*Alpha) - no FINISH barrier
  // to switch it. Default alpha-over; the reflective-material env pass sets
  // additiveBlendFix for Cv = Cs*FIX/128 + Cd.
  {
    const u8 fix = bag->info->additiveBlendFix;
    const u8 sub = bag->info->subtractiveBlendFix;
    // Subtractive wins over additive when both are set: (0 - Cs)*FIX + Cd,
    // clamped at 0 - the shadow volumes' count-down pass.
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_ALPHA_ADDR, false);
    packet2_add_2x_s64(objectDataPacket,
                       sub != 0   ? GS_SET_ALPHA(2, 0, 2, 1, sub)
                       : fix != 0 ? GS_SET_ALPHA(0, 2, 2, 1, fix)
                                  : GS_SET_ALPHA(0, 1, 0, 1, 0),
                       GS_REG_ALPHA);
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  // Do not terminate the DMA chain here: addBuffersDataToPacket appends the
  // first geometry commands and adds the single END tag for the whole packet.
  // A wholly culled mesh simply replaces this unsent packet on the next call.
  objectDataPending = true;
}

void StaPipQBufferRenderer::setInfo(PipelineInfoBag* bag) {
  prim->antialiasing = bag->antiAliasingEnabled;
  prim->blending = bag->blendingEnabled;
  prim->shading = bag->shadingType;

  if (bag->textureMappingType == TyraLinear) {
    lod->mag_filter = LOD_MAG_LINEAR;
    lod->min_filter = LOD_MIN_LINEAR;
  } else {
    lod->mag_filter = LOD_MAG_NEAREST;
    lod->min_filter = LOD_MIN_NEAREST;
  }
}

void StaPipQBufferRenderer::sendStaticData() const {
  packet2_reset(staticDataPacket, false);
  packet2_utils_vu_open_unpack(staticDataPacket, VU1_SET_GIFTAG_ADDR, false);
  { packet2_utils_gif_add_set(staticDataPacket, 1); }
  packet2_utils_vu_close_unpack(staticDataPacket);

  packet2_utils_vu_add_end_tag(staticDataPacket);
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
  dma_channel_send_packet2(staticDataPacket, DMA_CHANNEL_VIF1, true);
}

void StaPipQBufferRenderer::setProgramsCache() {
  // Modified by TyraX: in VU1 clipping mode the clip programs replace
  // the as_is family (both plus cull would overflow VU1 micro memory, and
  // as_is is only ever fed by the retired EE clipper path).
  // The env (matcap) variants ride along in both sets: cull_tce +
  // as_is_tce with the EE clipper, cull_tce + clip_tce in VU1-clipping
  // mode. The clip family uses three resident images: C/D share the C image,
  // TC/TCE share TC, and TD stays specialised. Their input/scratch/output ABIs
  // match within each pair; VU1_OPTIONS_ADDR.y selects only the per-corner
  // shading path. Path1 aliases identical source ranges to one destination.
  // Check the exact micro-memory budget with nm after touching any program -
  // the createProgramsCache overflow assert is compiled out in release.
  // Modified by TyraX: the set is DATA-DRIVEN now (setResidentClasses). Each
  // material class contributes a pair - the cull program plus its clipped or
  // as_is twin - and a class the project never draws can be dropped to buy
  // micro memory for a program the user wrote. Every class is kept unless a
  // game says otherwise, so this is the old hardcoded ten by default.
  VU1Program* programs[10];
  u32 count = 0;
  const struct {
    u32 bit;
    StaPipProgramName cull, clipped, asIs;
  } classes[] = {
      {StaPipClassColor, StaPipCullColor, StaPipClipColor, StaPipAsIsColor},
      {StaPipClassDirLights, StaPipCullDirLights, StaPipClipDirLights,
       StaPipAsIsDirLights},
      {StaPipClassTextureDirLights, StaPipCullTextureDirLights,
       StaPipClipTextureDirLights, StaPipAsIsTextureDirLights},
      {StaPipClassTextureColor, StaPipCullTextureColor, StaPipClipTextureColor,
       StaPipAsIsTextureColor},
      {StaPipClassTextureEnv, StaPipCullTextureEnv, StaPipClipTextureEnv,
       StaPipAsIsTextureEnv},
  };
  for (const auto& c : classes) {
    if ((residentClasses & c.bit) == 0) continue;
    programs[count++] = repository.getProgram(c.cull);
    programs[count++] =
        repository.getProgram(vu1Clipping ? c.clipped : c.asIs);
  }
  programsPacket = path1->createProgramsCache(programs, count, 0);

  // Modified by TyraX: the billboard family lives in its own small packet,
  // swapped in on demand (ensureProgramSet). Built once; independent of the
  // clipping mode, even though shared clip images now leave useful headroom.
  if (billboardProgramsPacket == nullptr) {
    VU1Program* billboardPrograms[2];
    billboardPrograms[0] = repository.getProgram(StaPipBillboardColor);
    billboardPrograms[1] = repository.getProgram(StaPipBillboardTexture);
    billboardProgramsPacket = path1->createProgramsCache(billboardPrograms, 2, 0);
  }
}

// TyraX addition: see the header. Safe to call at run time - a level that stops
// needing a material class can hand its micro memory to something else - but it
// is a full pipeline drain plus an upload, so it belongs at a zone or level
// boundary, never per bag.
void StaPipQBufferRenderer::setResidentClasses(const u32& mask) {
  const u32 wanted = mask | StaPipClassColor;  // colour is the fallback floor
  if (wanted == residentClasses) return;
  flushPendingPacket();
  residentClasses = wanted;
  if (programsPacket == nullptr) return;  // init() will build the right set

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

// TyraX addition: which material class a program name belongs to, so residency
// can be tested before anything is substituted.
static u32 classOfProgram(const StaPipProgramName& name) {
  switch (name) {
    case StaPipCullColor:
    case StaPipClipColor:
    case StaPipAsIsColor:
      return StaPipQBufferRenderer::StaPipClassColor;
    case StaPipCullDirLights:
    case StaPipClipDirLights:
    case StaPipAsIsDirLights:
      return StaPipQBufferRenderer::StaPipClassDirLights;
    case StaPipCullTextureDirLights:
    case StaPipClipTextureDirLights:
    case StaPipAsIsTextureDirLights:
      return StaPipQBufferRenderer::StaPipClassTextureDirLights;
    case StaPipCullTextureColor:
    case StaPipClipTextureColor:
    case StaPipAsIsTextureColor:
      return StaPipQBufferRenderer::StaPipClassTextureColor;
    case StaPipCullTextureEnv:
    case StaPipClipTextureEnv:
    case StaPipAsIsTextureEnv:
      return StaPipQBufferRenderer::StaPipClassTextureEnv;
    default:
      return 0;  // billboards and anything else: not class-managed
  }
}

// TyraX addition: see the header.
StaPipProgramName StaPipQBufferRenderer::residentFallback(
    const StaPipProgramName& name) const {
  const u32 cls = classOfProgram(name);
  // Not class-managed, or its class is resident: nothing to substitute.
  if (cls == 0 || (residentClasses & cls) != 0) return name;

  switch (name) {
    case StaPipCullTextureDirLights:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipCullTextureColor
                 : StaPipCullColor;
    case StaPipClipTextureDirLights:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipClipTextureColor
                 : StaPipClipColor;
    case StaPipAsIsTextureDirLights:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipAsIsTextureColor
                 : StaPipAsIsColor;
    case StaPipCullTextureEnv:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipCullTextureColor
                 : StaPipCullColor;
    case StaPipClipTextureEnv:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipClipTextureColor
                 : StaPipClipColor;
    case StaPipAsIsTextureEnv:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipAsIsTextureColor
                 : StaPipAsIsColor;
    case StaPipCullDirLights:
    case StaPipCullTextureColor:
      return StaPipCullColor;
    case StaPipClipDirLights:
    case StaPipClipTextureColor:
      return StaPipClipColor;
    case StaPipAsIsDirLights:
    case StaPipAsIsTextureColor:
      return StaPipAsIsColor;
    default:
      return name;
  }
}

// TyraX addition: see the header. sinf/cosf on the EE once per call is nothing
// next to what the same series costs three times per vertex on VU1.
void StaPipQBufferRenderer::setVuTime(const float& seconds) {
  vuTime[0] = seconds;
  vuTime[1] = sinf(seconds);
  vuTime[2] = cosf(seconds);
  vuTime[3] = 1.0F;
}

void StaPipQBufferRenderer::setProgramOverride(const StaPipProgramName& name,
                                               StaPipVU1Program* program) {
  flushPendingPacket();
  repository.setOverride(name, program);
  if (programsPacket == nullptr) return;  // init() will pick it up

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

// TyraX addition: see the header. Same work as setProgramOverride, once.
void StaPipQBufferRenderer::setProgramOverrides(
    const StaPipProgramName* names, StaPipVU1Program* const* programs,
    u32 count) {
  flushPendingPacket();
  for (u32 i = 0; i < count; i++) repository.setOverride(names[i], programs[i]);
  if (programsPacket == nullptr) return;  // init() will pick them up

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

void StaPipQBufferRenderer::setVU1Clipping(const bool& enabled) {
  if (vu1Clipping == enabled) return;
  flushPendingPacket();
  vu1Clipping = enabled;
#if TYRA_STAPIP_RETAINED_COMMANDS
  // Modified by TyraX: the clip chain is either present or absent per mode,
  // and every bag's cull program changes with it.
  clipBlockQw = 0;
  retained.clear();
#endif
#if TYRA_STAPIP_BAKED_STREAM
  // The clip chain is present or absent per mode and every bag's cull program
  // changes with it - and the baked stream carries the program's MSCAL.
  bakedCurrent = nullptr;
  bakeScratch.clear();
  baked.clear();
#endif

  if (programsPacket == nullptr) return;  // init() will build the right set

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

void StaPipQBufferRenderer::uploadPrograms() {
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
  dma_channel_send_packet2(programsPacket, DMA_CHANNEL_VIF1, true);
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
  billboardSetActive = false;  // Modified by TyraX: main set is resident now
}

// Modified by TyraX: swap between the resident program set and the
// billboard set (both packets are prebuilt - this is one VIF1 MPG upload,
// the VIF stalls it until VU1 halts, so it is safe mid-frame).
void StaPipQBufferRenderer::ensureProgramSet(const bool& billboard) {
  if (billboardSetActive == billboard) return;
  flushPendingPacket();
  billboardSetActive = billboard;

  const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;

  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
  dma_channel_send_packet2(
      billboard ? billboardProgramsPacket : programsPacket, DMA_CHANNEL_VIF1,
      true);
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
  if (telemetry != nullptr) {
    ++telemetry->programSetSwaps;
    telemetry->programSetWaitTicks += readTelemetryTicks() - waitStart;
  }
  clearLastProgramName();
}

void StaPipQBufferRenderer::setDoubleBuffer() {
  u16 startingAddr = VU1_STAPIP_LAST_ITEM_ADDR + 1;
  // Modified by TyraX: the double buffer stops below the VU1 clipping
  // scratch area (plane table + Sutherland-Hodgman polygons) at the top of
  // VU1 data memory - see stapip_vu1_shared_defines.h.
  const u16 bufferMaxSize = VU1_STAPIP_DBUFFER_END;
  bufferSize = (bufferMaxSize - startingAddr) / 2;

  path1->setDoubleBuffer(startingAddr, bufferSize);

  bufferSize -= 1;  // Because we don't want to upload anything from first
                    // buffer, to first addr of second buffer
}

StaPipQBuffer* StaPipQBufferRenderer::getBuffer() {
  currentBufferIndex = nextBufferIndex++;
  Verbose("Proposing buffer: ", currentBufferIndex);
  auto* result = buffers[currentBufferIndex];
  // Modified by TyraX: one gate every buffer passes through before any fill,
  // so a slot recycled out of a retained bag cannot carry a stale package
  // index into a copied or clipped one. StaPipCore sets it back after the fill
  // for the routes that may be retained.
  result->retainIndex = -1;
  result->bakeIndex = -1;  // Modified by TyraX: same gate, same reason
#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
  // Probe B: same gate, same reason - a recycled slot must not carry a stale
  // "my streams are in the copy pool" claim into a fillByPointer buffer.
  result->probeCopyFilled = false;
#endif
  if (nextBufferIndex >= buffersCount) {
    Verbose("Rollup - clearing buffer indices. currentBufferIndex: ",
            currentBufferIndex, " (before)nextBufferIndex: ", nextBufferIndex);
    nextBufferIndex = 0;
  }
  return result;
}

u16 StaPipQBufferRenderer::getQBufferIndex(StaPipQBuffer* buffer) {
  // Modified by TyraX: render submits the buffer just acquired by getBuffer.
  if (buffers[currentBufferIndex] == buffer) return currentBufferIndex;
  for (u16 i = 0; i < buffersCount; i++) {
    if (buffers[i] == buffer) return i;
  }
  TYRA_TRAP("Buffer not found!");
  return 0;
}

bool StaPipQBufferRenderer::is1stDBufferFlushTime() {
  return nextBufferIndex == buffersCount / 2;
}

bool StaPipQBufferRenderer::is2ndDBufferFlushTime() {
  return nextBufferIndex == 0;
}

void StaPipQBufferRenderer::flushBuffers() {
  auto is1stDBuffer = is1stDBufferFlushTime();
  auto is2ndDBuffer = is2ndDBufferFlushTime();

  if (!is1stDBuffer && !is2ndDBuffer) {
    auto offset = currentBufferIndex >= buffersCount / 2 ? buffersCount / 2 : 0;
    auto size = (currentBufferIndex + 1) - offset;
    Verbose("-- End flush. from: ", offset, " to: ", offset + size);
    addBuffersDataToPacket(offset, offset + size,
                           !submissionBatchCandidate);
    if (!submissionBatchCandidate) {
      sendPacket();
    } else if (++submissionBatchBags >= kSubmissionBatchSize) {
      flushPendingPacket();
    }
  }

  currentBufferIndex = 0;
  nextBufferIndex = 0;

  // Modified by TyraX: resetting the indices hands the slots to the next bag
  // while the packet just sent is still being read by the DMA, and the slots'
  // own arrays are what its REF tags point at whenever a package was COPIED
  // (fillByCopyMax / fillByCopy1By2 merge small in-frustum packages in both
  // clipping modes; fillByCopy1By3 / writeChunk are the EE clipper's). That is
  // safe only because those arrays live in the pool side the last send moved
  // away from - StaPipQBuffer::flipPoolSide in sendPacket. Do not add a DMA
  // wait here instead: measured at -4 FPS on the console (docs/vu1-clipping.md).

  Verbose("End flush - zeroing buffer indices.");
}

void StaPipQBufferRenderer::cull(StaPipQBuffer* buffer) {
  if (buffer->size == 0) {
    return;
  }

  dBufferPrograms[getQBufferIndex(buffer)] = getCullProgramByBag(buffer->bag);

  Verbose("Add cull[", getQBufferIndex(buffer), "]: ", buffer->size);

  auto is1stDBuffer = is1stDBufferFlushTime();
  auto is2ndDBuffer = is2ndDBufferFlushTime();

  if (is1stDBuffer || is2ndDBuffer) {
    auto from = is1stDBuffer ? 0 : buffersCount / 2;
    auto to = from + buffersCount / 2;

    Verbose("-- Half flush at ", getQBufferIndex(buffer), ". from: ", from,
            " to: ", to);

    addBuffersDataToPacket(from, to);
    sendPacket();
  }
}

void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {
  if (buffer->size == 0) {
    return;
  }

  // Modified by TyraX: VU1 clipping - clip-classified packages go to
  // the clip VU1 programs with raw object-space vertices (exactly like the
  // cull path); the EE clipper is bypassed entirely. The package occupancy
  // cap (StaPipCore::getClipPackageDivisor) bounds the on-VU1 fan-out.
  if (vu1Clipping) {
    dBufferPrograms[getQBufferIndex(buffer)] = getClipProgramByBag(buffer->bag);

    Verbose("Add vu1-clip[", getQBufferIndex(buffer), "]: ", buffer->size);

    auto is1stDBuffer = is1stDBufferFlushTime();
    auto is2ndDBuffer = is2ndDBufferFlushTime();

    if (is1stDBuffer || is2ndDBuffer) {
      auto from = is1stDBuffer ? 0 : buffersCount / 2;
      auto to = from + buffersCount / 2;

      addBuffersDataToPacket(from, to);
      sendPacket();
    }
    return;
  }

  // Modified by TyraX: a subpackage clipped against the frustum can fan
  // out into more verts than one VU1 buffer holds (maxVertCount). Drain the
  // clip result across as many buffer slots / VU1 draws as needed instead of
  // overflowing a single buffer (that tripped the "Max buffer size in VU1"
  // assert in stapip_qbuffer.cpp while walking around a scene).
  StaPipBag* bag = buffer->bag;
  const u32 total = clipper.clipToPool(buffer);

  // chunk is a multiple of 3 so a triangle is never split across two draws.
  const u32 chunk = (maxVertCount / 3) * 3;

  u32 emitted = 0;
  StaPipQBuffer* target = buffer;  // reuse the slot the caller already acquired
  do {
    const u32 remaining = total - emitted;
    const u32 n = remaining < chunk ? remaining : chunk;

    target->bag = bag;
    clipper.writeChunk(target, emitted, n);  // n == 0 -> empty slot, skipped
    dBufferPrograms[getQBufferIndex(target)] = getAsIsProgramByBag(bag);

    Verbose("Add clip[", getQBufferIndex(target), "]: ", target->size);

    auto is1stDBuffer = is1stDBufferFlushTime();
    auto is2ndDBuffer = is2ndDBufferFlushTime();

    if (is1stDBuffer || is2ndDBuffer) {
      auto from = is1stDBuffer ? 0 : buffersCount / 2;
      auto to = from + buffersCount / 2;

      Verbose("-- Half flush at ", getQBufferIndex(target), ". from: ", from,
              " to: ", to);

      addBuffersDataToPacket(from, to);
      sendPacket();
    }

    emitted += n;
    if (emitted >= total) break;

    target = getBuffer();  // next slot for the next chunk
  } while (true);
}

void StaPipQBufferRenderer::clearLastProgramName() {
  lastProgramName = StaPipUndefinedProgram;
}

// Modified by TyraX: retained command data - see StaPipRetainedCommands.
#if TYRA_STAPIP_RETAINED_COMMANDS

bool StaPipQBufferRenderer::beginRetainedBag(StaPipBag* bag,
                                             const u32& packageSize) {
  retainedCurrent = nullptr;
  if (bag == nullptr || packageSize == 0) return false;
  // A billboard bag expands centres on VU1 from a basis that changes every
  // frame, and it swaps the whole program set; a game-supplied writer has no
  // packet-size ABI and may not be position-independent. Both are excluded for
  // the same reason the submission-batch scope excludes them.
  if (bag->billboard != nullptr || repository.hasAnyOverride()) return false;

  StaPipRetainedEntry key;
  key.vertices = bag->vertices;
  key.sts = bag->texture != nullptr ? bag->texture->coordinates : nullptr;
  key.colors = bag->color->many;
  key.normals = bag->lighting != nullptr ? bag->lighting->normals : nullptr;
  key.program = getCullProgramByBag(bag);
  key.count = bag->count;
  key.maxVertCount = packageSize;
  key.bboxVersion = bag->bboxVersion;
  // The prim state reaches the block only through the GIFtag, so pack exactly
  // the fields that GIFtag reads. `mapping` is derived the way
  // addStandardBufferDataToPacket derives it, because prim->mapping still
  // holds the PREVIOUS bag's answer at this point.
  const u32 mapping =
      (bag->texture != nullptr && bag->texture->texture != nullptr) ? 1u : 0u;
  key.primKey = static_cast<u32>(prim->type) |
                (static_cast<u32>(prim->shading) << 4) | (mapping << 6) |
                (static_cast<u32>(prim->fogging) << 7) |
                (static_cast<u32>(prim->blending) << 8) |
                (static_cast<u32>(prim->antialiasing) << 9) |
                (static_cast<u32>(prim->mapping_type) << 10) |
                (static_cast<u32>(prim->colorfix) << 11);
  // The Z scale lands in the block's first data quadword and follows the
  // framebuffer's colour depth, which a display-mode switch moves.
  const float depthScale = RendererCoreDepth::scale;
  memcpy(&key.depthScaleBits, &depthScale, sizeof(u32));
  key.singleColor = bag->color->single != nullptr ? 1 : 0;
  key.stripped = bag->stripped ? 1 : 0;
  key.packages =
      static_cast<u16>((bag->count + packageSize - 1) / packageSize);

  retainedCurrent = retained.acquire(key);
  return retainedCurrent != nullptr;
}

void StaPipQBufferRenderer::endRetainedBag() { retainedCurrent = nullptr; }

u32 StaPipQBufferRenderer::takeRetainedHits() { return retained.takeHits(); }
u32 StaPipQBufferRenderer::takeRetainedBuilds() {
  return retained.takeBuilds();
}
u32 StaPipQBufferRenderer::getRetainedBytes() const {
  return retained.getBytes();
}

#else

bool StaPipQBufferRenderer::beginRetainedBag(StaPipBag*, const u32&) {
  return false;
}
void StaPipQBufferRenderer::endRetainedBag() {}
u32 StaPipQBufferRenderer::takeRetainedHits() { return 0; }
u32 StaPipQBufferRenderer::takeRetainedBuilds() { return 0; }
u32 StaPipQBufferRenderer::getRetainedBytes() const { return 0; }

#endif  // TYRA_STAPIP_RETAINED_COMMANDS

// Modified by TyraX: the BAKED VIF STREAM spike - docs/baked-vif-stream.md.
#if TYRA_STAPIP_BAKED_STREAM

bool StaPipQBufferRenderer::beginBakedBag(StaPipBag* bag,
                                          const u32& packageSize) {
  bakedCurrent = nullptr;
  bakeScratch.clear();
  if (bag == nullptr || packageSize == 0) return false;
  // Same two exclusions the retained cache and the submission-batch scope
  // make, for the same two reasons.
  if (bag->billboard != nullptr || repository.hasAnyOverride()) return false;

  StaPipVU1Program* program = getCullProgramByBag(bag);
  if (program == nullptr) return false;

  StaPipBakedEntry key;
  key.vertices = bag->vertices;
  key.sts = bag->texture != nullptr ? bag->texture->coordinates : nullptr;
  key.colors = bag->color->many;
  key.normals = bag->lighting != nullptr ? bag->lighting->normals : nullptr;
  key.program = program;
  key.count = bag->count;
  key.maxVertCount = packageSize;
  key.bboxVersion = bag->bboxVersion;
  const u32 mapping =
      (bag->texture != nullptr && bag->texture->texture != nullptr) ? 1u : 0u;
  key.primKey = static_cast<u32>(prim->type) |
                (static_cast<u32>(prim->shading) << 4) | (mapping << 6) |
                (static_cast<u32>(prim->fogging) << 7) |
                (static_cast<u32>(prim->blending) << 8) |
                (static_cast<u32>(prim->antialiasing) << 9) |
                (static_cast<u32>(prim->mapping_type) << 10) |
                (static_cast<u32>(prim->colorfix) << 11);
  const float depthScale = RendererCoreDepth::scale;
  memcpy(&key.depthScaleBits, &depthScale, sizeof(u32));
  // The baked stream carries the MSCAL, so the program's micro-memory
  // destination is part of what it encodes - the retained key does not need
  // this because the MSCAL stays outside its block.
  key.programAddr = program->getDestinationAddress();
  key.singleColor = bag->color->single != nullptr ? 1 : 0;
  key.stripped = bag->stripped ? 1 : 0;
  key.packages = static_cast<u16>((bag->count + packageSize - 1) / packageSize);

  bakedCurrent = baked.acquire(key);
  return bakedCurrent != nullptr;
}

/**
 * Transcode one package's finished DMA chain fragment into a pure VIFcode
 * stream.
 *
 * This is the whole format question in fifteen lines. A DMA REF tag's payload
 * is NOT interpreted by the DMAC - every word of it reaches VIF1 as a VIFcode -
 * so a block that a single REF replays may contain no tags. Each tag in the
 * fragment therefore becomes one header quadword holding that tag's OWN two
 * VIFcodes, copied verbatim and preceded by two VIF NOPs so the data that
 * follows the UNPACK still starts on a quadword boundary, and then the
 * quadwords the tag transferred: the ones inline behind a CNT, the ones at the
 * named address behind a REF. Nothing is re-derived from a description of the
 * packet format, which is what keeps this from drifting away from the writers.
 */
bool StaPipQBufferRenderer::bakeBlock(packet2_t* packet, u32 fromQw, u32 toQw,
                                      StaPipBakedEntry* entry, u32 index) {
  const u8* base = reinterpret_cast<const u8*>(packet->base);
  const qword_t* src = reinterpret_cast<const qword_t*>(base + fromQw * 16);
  const u32 srcQw = toQw - fromQw;
  if (srcQw == 0) return false;

  const u32 startOut = static_cast<u32>(bakeScratch.size());
  u32 i = 0;
  while (i < srcQw) {
    const dma_tag_t* tag = reinterpret_cast<const dma_tag_t*>(&src[i]);
    const u32 qwc = static_cast<u32>(tag->QWC);
    const u32 id = static_cast<u32>(tag->ID);
    const u32* vif = reinterpret_cast<const u32*>(&src[i]) + 2;
    const qword_t* payload;
    if (id == P2_DMA_TAG_CNT) {
      payload = &src[i + 1];
      i += 1 + qwc;
      if (i > srcQw) {
        bakeScratch.resize(startOut);
        return false;
      }
    } else if (id == P2_DMA_TAG_REF) {
      payload = reinterpret_cast<const qword_t*>(static_cast<u32>(tag->ADDR));
      i += 1;
    } else {
      // NEXT, CALL, RET, END, REFS, REFE. None is written here today, and a
      // position-relative one must never be: refuse rather than guess.
      bakeScratch.resize(startOut);
      return false;
    }
    qword_t header;
    header.sw[0] = 0;  // VIF NOP
    header.sw[1] = 0;  // VIF NOP
    header.sw[2] = vif[0];
    header.sw[3] = vif[1];
    bakeScratch.push_back(header);
    for (u32 q = 0; q < qwc; ++q) bakeScratch.push_back(payload[q]);
  }

  const u32 emitted = static_cast<u32>(bakeScratch.size()) - startOut;
  if (emitted == 0 || emitted > StaPipBakedStreams::kMaxBlockQw) {
    bakeScratch.resize(startOut);
    return false;
  }
  entry->offsets[index] = startOut;
  entry->sizes[index] = static_cast<u16>(emitted);
  entry->built = static_cast<u16>(index + 1);
  baked.countBuild();
  return true;
}

bool StaPipQBufferRenderer::replayWholeBakedBag() {
  StaPipBakedEntry* entry = bakedCurrent;
  if (entry == nullptr || !entry->complete || entry->arenaQw == 0) return false;
  // A DMA tag's QWC is 16 bits. A bag whose arena is larger than that cannot be
  // one tag; it keeps the ordinary route rather than being split here, because
  // splitting it is exactly the per-package bookkeeping this exists to delete.
  if (entry->arenaQw > 0xFFFFu) return false;
  // The arena must be quadword aligned or the DMAC transfers from somewhere
  // else - a tag's address field drops its low four bits. endBakedBag aligns
  // it; this is the assertion that says so out loud.
  TYRA_ASSERT((reinterpret_cast<u32>(entry->arena) & 0xFu) == 0,
              "Baked arena is not quadword aligned");

  auto* currentPacket = packets[context];
  // DO NOT FLUSH HERE, however tempting a capacity check looks. This bag's
  // uniforms - MVP, light matrices, options, ALPHA - are ALREADY in this packet
  // (StaPipCore::render calls sendObjectData before dispatch), so a flush now
  // would send them in one packet and put this REF in the next, replaying the
  // whole bag against whatever matrix the following bag uploads.
  //
  // There is nothing to check anyway: setSubmissionBatchCandidate reserved a
  // complete kWorstBagPacketSize for this bag BEFORE any of its bytes were
  // appended, precisely so a bag can never straddle. A replayed bag needs three
  // quadwords of that reservation. The assert states the invariant rather than
  // trusting it silently.
  TYRA_ASSERT(packet2_get_qw_count(currentPacket) + 4 <= packetSize,
              "No room for a baked bag's REF: the per-bag reservation in "
              "setSubmissionBatchCandidate is no longer conservative");

  packet2_chain_ref(currentPacket, entry->arena, entry->arenaQw, 0, 0, 0);
  packet2_vif_nop(currentPacket, 0);
  packet2_vif_nop(currentPacket, 0);

  // The arena's own tail already kicked the microprogram for every package in
  // it, so the name bookkeeping has to agree or the next buffer would emit a
  // second kick. Same reasoning as the per-package replay path above.
  auto* program =
      static_cast<StaPipVU1Program*>(const_cast<void*>(entry->program));
  lastProgramName = program->getName();

  baked.countHit();
  objectDataPending = true;
  // MANDATORY, not bookkeeping: flushPendingPacket() RESETS the packet when
  // submissionBatchBags is 0, which would throw this REF away. A replayed bag
  // consumes no qbuffer slot, so flushBuffers() - where the ordinary route
  // counts the bag - takes its is2ndDBufferFlushTime() early out and never sees
  // it.
  ++submissionBatchBags;
  if (!submissionBatchCandidate ||
      submissionBatchBags >= kSubmissionBatchSize) {
    flushPendingPacket();
  }
  return true;
}

void StaPipQBufferRenderer::endBakedBag() {
  StaPipBakedEntry* entry = bakedCurrent;
  bakedCurrent = nullptr;
  if (entry == nullptr) {
    bakeScratch.clear();
    return;
  }
  if (!entry->complete && entry->built == entry->packages &&
      !bakeScratch.empty()) {
    const u32 qw = static_cast<u32>(bakeScratch.size());
    if (baked.reserve(qw)) {
      entry->raw = std::unique_ptr<u8[]>(new u8[qw * 16 + 15]);
      const u32 aligned =
          (reinterpret_cast<u32>(entry->raw.get()) + 15u) & ~15u;
      entry->arena = reinterpret_cast<qword_t*>(aligned);
      memcpy(entry->arena, bakeScratch.data(), qw * 16);
      entry->arenaQw = qw;
      entry->complete = 1;
    } else {
      entry->built = 0;  // no room today; try again when some frees up
    }
  } else if (!entry->complete) {
    // Modified by TyraX: a package went missing - start the bag over, and say
    // so, because this one is invisible in the key.
    baked.countMiss(StaPipBakedStreams::MissIncomplete);
    baked.noteLoud(StaPipBakedStreams::MissIncomplete, entry->count,
                   entry->packages);
    entry->built = 0;
  }
  bakeScratch.clear();
}

u32 StaPipQBufferRenderer::takeBakedHits() { return baked.takeHits(); }
u32 StaPipQBufferRenderer::takeBakedBuilds() { return baked.takeBuilds(); }
u32 StaPipQBufferRenderer::getBakedBytes() const { return baked.getBytes(); }
const u32* StaPipQBufferRenderer::getBakedMisses() const {
  return baked.getMisses();
}
u32 StaPipQBufferRenderer::getBakedLoudCount() const {
  return baked.getLoudCount();
}
u32 StaPipQBufferRenderer::getBakedLoudPackages() const {
  return baked.getLoudPackages();
}
u32 StaPipQBufferRenderer::getBakedLoudReason() const {
  return baked.getLoudReason();
}
void StaPipQBufferRenderer::clearBakedMisses() { baked.clearMisses(); }

#else

bool StaPipQBufferRenderer::beginBakedBag(StaPipBag*, const u32&) {
  return false;
}
bool StaPipQBufferRenderer::replayWholeBakedBag() { return false; }
void StaPipQBufferRenderer::endBakedBag() {}
u32 StaPipQBufferRenderer::takeBakedHits() { return 0; }
u32 StaPipQBufferRenderer::takeBakedBuilds() { return 0; }
u32 StaPipQBufferRenderer::getBakedBytes() const { return 0; }
const u32* StaPipQBufferRenderer::getBakedMisses() const { return nullptr; }
u32 StaPipQBufferRenderer::getBakedLoudCount() const { return 0; }
u32 StaPipQBufferRenderer::getBakedLoudPackages() const { return 0; }
u32 StaPipQBufferRenderer::getBakedLoudReason() const { return 0; }
void StaPipQBufferRenderer::clearBakedMisses() {}

#endif  // TYRA_STAPIP_BAKED_STREAM

void StaPipQBufferRenderer::addBuffersDataToPacket(const u32& from,
                                                   const u32& to,
                                                   const bool& finalize) {
  HardwareTrace::Scope trace("Packet_build");
  const u32 buildStart = telemetry ? readTelemetryTicks() : 0;
  auto* currentPacket = packets[context];
  if (!objectDataPending) packet2_reset(currentPacket, false);

  for (u32 i = from; i < to; i++) {
    if (!buffers[i]->any()) continue;

    auto* program = dBufferPrograms[i];

    // Modified by TyraX: every vertex that actually reaches a VU1 buffer,
    // counted where it is packetised rather than where it was classified -
    // the number the EE's per-package bill scales with. See StaPipTelemetry.
    if (telemetry) telemetry->verticesSubmitted += buffers[i]->size;

#if TYRA_STAPIP_BAKED_STREAM
    // Modified by TyraX: the baked VIF stream. A run of consecutive packages
    // of one complete bag is ONE DMA REF tag - the arena holds them in package
    // order, so a run is contiguous - and that REF's payload carries the
    // unpacks, the inline vertex data AND the MSCAL/MSCNT of every package in
    // the run. The run stops at the flush boundary `to`, which is why a mesh
    // that straddles one costs two tags instead of one.
    {
      StaPipBakedEntry* bakedEntry = bakedCurrent;
      const int bakeIdx = buffers[i]->bakeIndex;
      if (bakedEntry != nullptr && bakedEntry->complete && bakeIdx >= 0 &&
          bakeIdx < bakedEntry->packages) {
        u32 runQw = bakedEntry->sizes[bakeIdx];
        u32 next = i + 1;
        u32 nextIdx = static_cast<u32>(bakeIdx) + 1;
        while (next < to && nextIdx < bakedEntry->packages &&
               buffers[next]->any() &&
               buffers[next]->bakeIndex == static_cast<int>(nextIdx)) {
          if (telemetry) telemetry->verticesSubmitted += buffers[next]->size;
          runQw += bakedEntry->sizes[nextIdx];
          ++next;
          ++nextIdx;
        }
        packet2_chain_ref(currentPacket,
                          bakedEntry->arena + bakedEntry->offsets[bakeIdx],
                          runQw, 0, 0, 0);
        packet2_vif_nop(currentPacket, 0);
        packet2_vif_nop(currentPacket, 0);
        // The block's own tail already kicked the microprogram, so the name
        // bookkeeping has to agree or the next buffer would emit a second one.
        lastProgramName = program->getName();
        baked.countHit();
        i = next - 1;  // the for() increments
        continue;
      }
    }
    const u32 bakeFrom = packet2_get_qw_count(currentPacket);
#endif

#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
    // Probe B: one buffer whose streams live in the copy pool is enough to
    // make this whole chain need the write-back. See stapip_probes.hpp.
    if (buffers[i]->probeCopyFilled) probePacketUsesPool = true;
#endif

#if TYRA_STAPIP_RETAINED_COMMANDS
    // Modified by TyraX: retained command data. The block is the bag's
    // geometry commands for THIS package - identical every frame while the key
    // holds - so a hit is a memcpy where the miss below runs the scale
    // quadword, the prim GIFtag and one DMA REF per stream. It is captured out
    // of the packet the miss just wrote, which is what makes the two paths
    // byte-identical by construction rather than by inspection.
    StaPipRetainedEntry* entry = retainedCurrent;
    const int retainIdx = buffers[i]->retainIndex;
    const bool retainable =
        entry != nullptr && retainIdx >= 0 && retainIdx < entry->packages;
    if (retainable && entry->ready[retainIdx]) {
      appendChainQwords(currentPacket,
                        &entry->data[static_cast<u32>(retainIdx) *
                                     StaPipRetainedCommands::kMaxBlockQw],
                        entry->blockQw);
      retained.countHit();
    } else {
      const u32 blockStart = packet2_get_qw_count(currentPacket);
      program->addBufferDataToPacket(currentPacket, buffers[i], prim);
      retained.countBuild();
      if (retainable) {
        const u32 blockQw = packet2_get_qw_count(currentPacket) - blockStart;
        // A block that does not fit, or that disagrees with the length this
        // bag's other packages produced, is simply never retained - the entry
        // keeps rebuilding, which is slower and always right.
        if (blockQw > 0 && blockQw <= StaPipRetainedCommands::kMaxBlockQw &&
            (entry->blockQw == 0 || entry->blockQw == blockQw)) {
          entry->blockQw = static_cast<u16>(blockQw);
          memcpy(&entry->data[static_cast<u32>(retainIdx) *
                              StaPipRetainedCommands::kMaxBlockQw],
                 reinterpret_cast<const u8*>(currentPacket->base) +
                     blockStart * 16,
                 blockQw * 16);
          entry->ready[retainIdx] = 1;
        }
      }
    }
#else
    program->addBufferDataToPacket(currentPacket, buffers[i], prim);
#endif

    Verbose("Send ", program->getStringName(), "[", i, "]: ", buffers[i]->size);

    if (lastProgramName != program->getName()) {
      packet2_utils_vu_add_start_program(currentPacket,
                                         program->getDestinationAddress());
      lastProgramName = program->getName();
    } else {
      packet2_utils_vu_add_continue_program(currentPacket);
    }

#if TYRA_STAPIP_BAKED_STREAM
    // Modified by TyraX: bake what the ordinary writers just wrote, program
    // kick included. render() clears lastProgramName per bag, so package 0 of
    // a bag always produced MSCAL and every later one MSCNT - which is what
    // makes the kick a bake-time fact rather than a property of the frame.
    {
      StaPipBakedEntry* bakedEntry = bakedCurrent;
      const int bakeIdx = buffers[i]->bakeIndex;
      if (bakedEntry != nullptr && !bakedEntry->complete && bakeIdx >= 0 &&
          bakeIdx < bakedEntry->packages &&
          bakeIdx == static_cast<int>(bakedEntry->built)) {
        if (!bakeBlock(currentPacket, bakeFrom,
                       packet2_get_qw_count(currentPacket), bakedEntry,
                       static_cast<u32>(bakeIdx))) {
          bakedEntry->built = 0;   // so the next frame may start over
          bakedCurrent = nullptr;  // this bag cannot be baked today
        }
      }
    }
#endif
  }

  if (finalize) packet2_utils_vu_add_end_tag(currentPacket);
  if (telemetry) telemetry->packetBuildTicks += readTelemetryTicks()-buildStart;
}

void StaPipQBufferRenderer::beginSubmissionBatch() {
  TYRA_ASSERT(!submissionBatchScope, "Submission batch is already open");
  submissionBatchScope = true;
}

void StaPipQBufferRenderer::endSubmissionBatch() {
  TYRA_ASSERT(submissionBatchScope, "Submission batch is not open");
  flushPendingPacket();
  submissionBatchScope = false;
  submissionBatchCandidate = false;
}

void StaPipQBufferRenderer::onFrameEnd() {
  // Modified by TyraX: fail safe for a mismatched caller scope. Never carry
  // DMA references into the next frame, where retained source arrays may be
  // updated before VIF1 consumes them.
  flushPendingPacket();
  TYRA_ASSERT(!submissionBatchScope,
              "Submission batch must end before the frame ends");
  submissionBatchScope = false;
  submissionBatchCandidate = false;
#if TYRA_STAPIP_RETAINED_COMMANDS
  retainedCurrent = nullptr;
  retained.onFrameEnd();
#endif
#if TYRA_STAPIP_BAKED_STREAM
  bakedCurrent = nullptr;
  bakeScratch.clear();
  baked.onFrameEnd();
#endif
}

void StaPipQBufferRenderer::setSubmissionBatchCandidate(const bool& candidate,
                                                        const bool& textured) {
  const bool enabled = submissionBatchScope && candidate;
  if (!enabled && objectDataPending) flushPendingPacket();
  // Modified by TyraX: reserve a complete old-style 32-group packet before
  // appending any bytes. The candidate currently caps at 15 groups, but this
  // conservative bound remains valid if that policy changes. Checking after
  // construction is too late: packet2 would already have written past base.
  if (enabled && objectDataPending &&
      packet2_get_qw_count(packets[context]) + kWorstBagPacketSize >
          packetSize)
    flushPendingPacket();
  submissionBatchCandidate = enabled;
  if (enabled && textured) submissionPacketHasTexture = true;
  if (enabled && telemetry) ++telemetry->submissionBatchEligibleBags;
}

void StaPipQBufferRenderer::flushPendingPacket() {
  if (!objectDataPending) return;
  if (submissionBatchBags == 0) {
    packet2_reset(packets[context], false);
    objectDataPending = false;
    submissionPacketHasTexture = false;
    return;
  }
  if (HardwareTrace::active && submissionBatchBags > 0) {
    const u32 now = HardwareTrace::ticks();
    HardwareTrace::record("Submission_batch", now, now, submissionBatchBags);
  }
  if (telemetry && submissionBatchBags > 0) {
    telemetry->submissionBatchedBags += submissionBatchBags;
    ++telemetry->submissionBatchPackets;
  }
  packet2_utils_vu_add_end_tag(packets[context]);
  sendPacket();
  submissionBatchBags = 0;
}

void StaPipQBufferRenderer::textureMutationBarrier(void* context) {
  static_cast<StaPipQBufferRenderer*>(context)->beforeTextureMutation();
}

void StaPipQBufferRenderer::beforeTextureMutation() {
#if TYRA_STAPIP_VIFHASH
  // Modified by TyraX: the gate's second input. This barrier is the ONLY thing
  // ordering a texture upload (GIF channel) against the draws that read it, and
  // it works by flushing the pending packet - so a redesign that moves the
  // flush cadence can move an upload between a different pair of draws. Folding
  // a marker here in sequence makes the hash model this pipeline's whole
  // contribution to GS state as an ordered sequence of draws and texture
  // changes. Folded BEFORE the flush, so it lands between the draws it
  // separates. docs/baked-stream-acceptance-gate.md.
  vifHash.foldTextureMutation(
      submissionTextureReadersOutstanding ? 1u : 0u);
#endif
  flushPendingPacket();
  if (!submissionTextureReadersOutstanding) return;
  rendererCore->sync.align3D();
  submissionTextureReadersOutstanding = false;
}

void StaPipQBufferRenderer::sendPacket() {
  auto* currentPacket = packets[context];

  TYRA_ASSERT(packet2_get_qw_count(currentPacket) <= packetSize,
              "Packet is too big. Internal error");

  const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
  if (telemetry != nullptr) {
    ++telemetry->packetFlushes;
    telemetry->vu1WaitTicks += readTelemetryTicks() - waitStart;
  }
  // Added by TyraX: this wait was inside no bracket at all, so it was
  // indistinguishable from `dispatch`'s package creation and classification.
  // Opt-in only - see stapip_attrib.hpp.
#if TYRA_STAPIP_ATTRIB
  const u32 gifWaitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
#endif
  { HardwareTrace::Scope trace("GIF_DMA_wait");
    dma_channel_wait(DMA_CHANNEL_GIF, 0); }  // Wait for texture. Issue #182.
#if TYRA_STAPIP_ATTRIB
  if (telemetry != nullptr)
    telemetry->attrib.gifWaitTicks += readTelemetryTicks() - gifWaitStart;
#endif

  // TyraX: the VU1 packet tap (docs/devkit.md). Null in any build whose devkit
  // layer does not exist, so this is one load + branch per bag flush and the
  // capture code is not linked at all.
  if (g_vuPacketHook)
    g_vuPacketHook(currentPacket->base, packet2_get_qw_count(currentPacket),
                   "");  // the program is in the packet's MSCAL address

#if TYRA_STAPIP_VIFHASH
  // Modified by TyraX: the acceptance gate, leg 1. Same seam as the devkit tap
  // and for the same reason - the chain is finished and the bytes here are the
  // bytes the DMAC is about to fetch - but it decodes and folds ON THE CONSOLE
  // instead of shipping a capture to the host, because a capture is one flush
  // of the ~120 a frame and its buffers truncate silently.
  // docs/baked-stream-acceptance-gate.md.
  vifHash.foldChain(currentPacket->base, packet2_get_qw_count(currentPacket));
#endif

  // dma_wait_fast(); // This have no impact on performance

  const u32 submitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
  { HardwareTrace::Scope trace("VIF1_submit");
    if (HardwareTrace::active) { const u32 t=HardwareTrace::ticks(); HardwareTrace::record("Packet_qwords", t, t, packet2_get_qw_count(currentPacket)); }
    // Modified by TyraX: the DMA chain quadwords this pipeline hands VIF1 -
    // the number the baked VIF stream exists to move. Compiled into both arms.
    if (telemetry) chainQwords += packet2_get_qw_count(currentPacket);
#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
    // Probe B (stapip_probes.hpp): the packet itself is uncached, so nothing
    // of IT needs writing back - but a chain whose REF tags name the qbuffer
    // copy pool still does, and that half is bounded rather than assumed.
    const bool probeFlush = probePacketUsesPool || TYRA_STAPIP_PROBE_FORCE_FLUSH;
    if (telemetry != nullptr) {
      if (probeFlush) ++telemetry->probeFlushedSends;
      else ++telemetry->probeUnflushedSends;
    }
#if TYRA_STAPIP_PROBE_LOG_SENDS
    {
      // Scene property, read in PCSX2. Never enable this for a console timing
      // arm - see TYRA_STAPIP_PROBE_LOG_SENDS in stapip_probes.hpp.
      static u32 probeKept = 0, probeDropped = 0;
      if (probeFlush) ++probeKept; else ++probeDropped;
      if ((probeKept + probeDropped) % 3000 == 0)
        TYRA_LOG("PROBEB: sends kept(flush)=", probeKept,
                 " dropped(no flush)=", probeDropped);
    }
#endif
    dma_channel_send_packet2(currentPacket, DMA_CHANNEL_VIF1, probeFlush);
    probePacketUsesPool = false;
#else
    dma_channel_send_packet2(currentPacket, DMA_CHANNEL_VIF1, true);
#endif
  }
  if (submissionPacketHasTexture)
    submissionTextureReadersOutstanding = true;
  submissionPacketHasTexture = false;
  if (telemetry) telemetry->dmaSubmitTicks += readTelemetryTicks()-submitStart;
  objectDataPending = false;

  // TyraX: with a VU1 memory hook installed (a devkit capture is in flight),
  // wait for the transfer AND for VU1 to finish its microprogram, then hand the
  // whole of VU1 data memory over. This stalls the pipeline on purpose - it runs
  // for the one frame a capture was armed for, never otherwise.
  if (g_vuMemHook) {
    { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    HardwareTrace::state("VIF1_WAIT_END"); }
    // VIF1_STAT: VPS (bits 0-1) = VIF status, VEW (bit 2) = waiting for VU1.
    volatile u32* const vif1Stat = (volatile u32*)0x10003c00;
    for (int spin = 0; spin < 2000000 && (*vif1Stat & 0x7) != 0; ++spin) {
    }
    g_vuMemHook((const void*)0x1100c000, 1024 * 16);
  }

  // Switch packet, so we can proceed during DMA transfer - and switch the
  // slots' copy pools with it, they are referenced by the packet just sent.
  context = !context;
  StaPipQBuffer::flipPoolSide();
}

// Modified by TyraX: propagate only when the value actually MOVES.
//
// StaPipCore calls this once per bag, inside the `bounds` bracket, and it fans
// the same u32 out to all 32 qbuffers plus the clipper - 33 out-of-line stores
// per bag, 7 474 per garage-day frame, to write the number that was already
// there. Measured by the attribution pass: the pin-and-store half of `bdSize`
// is 0.448 ms of a 2.016 ms `bounds` (docs/render-submission-attribution.md),
// while resolving the program is 0.072 and the integer divisions in
// getMaxVertCount are 0.057 (measured when there were three of them; the
// rounding step is one division now, so this is an upper bound). The package
// size is a property of the PROGRAM
// CLASS, so consecutive bags of one class - which is most of a frame - ask for
// the size that is already set.
//
// Every leaf setter here is a pure store (StaPipQBuffer, StaPipClipper), so
// skipping a no-op propagation is exact rather than approximate.
//
// THE INVARIANT THIS DEPENDS ON: `maxVertCount` must equal what the buffers
// hold. A fresh `allocateOnUse()` builds new StaPipQBuffers whose own
// maxVertCount is not yet meaningful, so that function resets this field to 0
// - never a legal package size, every derived one being a multiple of 3 and at
// least 3 - which forces the next bag to propagate. Without that reset a scene
// reload whose first bag happened to want the previous scene's size would skip
// the propagation and leave 32 buffers holding a stale number.
void StaPipQBufferRenderer::setMaxVertCount(const u32& count) {
  if (maxVertCount == count) return;
  maxVertCount = count;
  for (u32 i = 0; i < buffersCount; i++) {
    buffers[i]->setMaxVertCount(count);
  }
  clipper.setMaxVertCount(count);
}

StaPipVU1Program* StaPipQBufferRenderer::getAsIsProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);

  // Modified by TyraX: billboard bags always use the billboard programs
  // (per-quad culling happens on VU1; there is no as_is/clip variant).
  if (programType == StaPipVU1BillboardTexture)
    return getProgramByName(StaPipBillboardTexture);
  else if (programType == StaPipVU1BillboardColor)
    return getProgramByName(StaPipBillboardColor);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipAsIsTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipAsIsDirLights);
  else if (programType == StaPipVU1TextureEnvColor)  // TyraX: env (matcap)
    return getProgramByName(StaPipAsIsTextureEnv);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipAsIsTextureColor);
  else
    return getProgramByName(StaPipAsIsColor);
}

// Modified by TyraX: VU1 clipping.
StaPipVU1Program* StaPipQBufferRenderer::getClipProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);

  // Modified by TyraX: billboard bags always use the billboard programs.
  if (programType == StaPipVU1BillboardTexture)
    return getProgramByName(StaPipBillboardTexture);
  else if (programType == StaPipVU1BillboardColor)
    return getProgramByName(StaPipBillboardColor);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipClipTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipClipDirLights);
  else if (programType == StaPipVU1TextureEnvColor)  // TyraX: env (matcap)
    return getProgramByName(StaPipClipTextureEnv);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipClipTextureColor);
  else
    return getProgramByName(StaPipClipColor);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);
  return getCullProgramByType(programType);
}

StaPipVU1Program* StaPipQBufferRenderer::getProgramByName(
    const StaPipProgramName& name) {
  // Modified by TyraX: a class the project dropped has no program on VU1, and
  // MSCAL-ing to an address nothing was uploaded to draws garbage. Walk down to
  // a resident relative instead - the mesh loses a feature, not the frame.
  StaPipProgramName resolved = name;
  for (int guard = 0; guard < 4; ++guard) {
    const StaPipProgramName next = residentFallback(resolved);
    if (next == resolved) break;
    resolved = next;
  }
  return repository.getProgram(resolved);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByParams(
    const bool& isLightingEnabled, const bool& isTextureEnabled) {
  auto type = getDrawProgramTypeByParams(isLightingEnabled, isTextureEnabled);
  return getCullProgramByType(type);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByType(
    const StaPipProgramType& programType) {
  // Modified by TyraX: billboard bags always use the billboard programs.
  if (programType == StaPipVU1BillboardTexture)
    return getProgramByName(StaPipBillboardTexture);
  else if (programType == StaPipVU1BillboardColor)
    return getProgramByName(StaPipBillboardColor);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipCullTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipCullDirLights);
  else if (programType == StaPipVU1TextureEnvColor)  // TyraX: env (matcap)
    return getProgramByName(StaPipCullTextureEnv);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipCullTextureColor);
  else
    return getProgramByName(StaPipCullColor);
}

StaPipProgramType StaPipQBufferRenderer::getDrawProgramTypeByBag(
    const StaPipBag* bag) const {
  // Modified by TyraX: particle billboards - the vertex slot carries
  // centers, the ST slot the per-particle basis weights. The texture bag is
  // always present (params channel); a real image selects the T variant.
  if (bag->billboard != nullptr)
    return bag->texture->texture != nullptr ? StaPipVU1BillboardTexture
                                            : StaPipVU1BillboardColor;
  // Modified by TyraX: env (matcap) - the ST slot carries normals, the
  // texture ST is computed on VU1. Lighting is unsupported with env.
  if (bag->texture != nullptr && bag->texture->coordinatesAreNormals)
    return StaPipVU1TextureEnvColor;
  auto isLightingEnabled = bag->lighting != nullptr;
  auto isTextureEnabled = bag->texture != nullptr;
  return getDrawProgramTypeByParams(isLightingEnabled, isTextureEnabled);
}

StaPipProgramType StaPipQBufferRenderer::getDrawProgramTypeByParams(
    const bool& isLightingEnabled, const bool& isTextureEnabled) const {
  if (isLightingEnabled && isTextureEnabled)
    return StaPipVU1TextureDirLights;
  else if (isLightingEnabled)
    return StaPipVU1DirLights;
  else if (isTextureEnabled)
    return StaPipVU1TextureColor;
  else
    return StaPipVU1Color;
}

}  // namespace Tyra

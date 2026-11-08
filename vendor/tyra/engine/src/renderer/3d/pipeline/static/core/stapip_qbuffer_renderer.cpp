#include "renderer/core/paths/path1/frame_submission.hpp"
#include "renderer/3d/pipeline/static/core/stapip_spot_bounds.hpp"
/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: the stamp record and the stamp pass (TYRA_STAPIP_STAMP_PASS,
# docs/dod-submission.md phase 3) and their input epochs.
# Modified by TyraX: the crossing pass (TYRA_STAPIP_CROSSING_PASS): crossing
# records, crossing-only stamp slots, crossing draws emitted by the stamp pass.
*/

#include <malloc.h>  // Modified by TyraX: memalign (stamp records)
#include "renderer/3d/pipeline/static/core/stapip_stamp_verify.hpp"  // Modified by TyraX
#include "renderer/3d/pipeline/static/core/stapip_crossing.hpp"  // Modified by TyraX

#include "math/math.hpp"
#include "math/mmi.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <kernel.h>  // Modified by TyraX: FlushCache for the VIF1 queue

// Modified by TyraX: PipelineZTest_TestOnly branch in sendObjectData;
// per-mesh object-space spot light (flashlight) upload for the color VU1
// programs + EE clipper; alpha-test AFAIL fixed to ATEST_KEEP_ALL (see
// sendObjectData) so cutout textures stop stamping the z buffer.

#include <math.h>
#include <string.h>
#include <dma_tags.h>
#include <algorithm>
#include <new>
#include "info/info.hpp"
#include "debug/hardware_trace.hpp"
#include "renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp"
#include "renderer/3d/pipeline/static/core/stapip_baked_body.hpp"
#include "renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h"
#include "renderer/core/gs/renderer_core_depth.hpp"
#include "packet2/packet2_tyra_utils.hpp"
#include "renderer/3d/pipeline/static/core/stapip_vu_tap.hpp"
#include "renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp"

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

#if TYRA_STAPIP_PACKET_PROFILE
inline u8 telemetryProducer(const StaPipTelemetry* telemetry) {
  return telemetry != nullptr && telemetry->producer < StaPipProducerCount
             ? telemetry->producer
             : static_cast<u8>(StaPipProducerMixed);
}

bool isTexturedProgram(const StaPipProgramName name) {
  return name == StaPipCullTextureDirLights ||
         name == StaPipAsIsTextureDirLights ||
         name == StaPipCullTextureColor || name == StaPipAsIsTextureColor ||
         name == StaPipClipTextureDirLights ||
         name == StaPipClipTextureColor || name == StaPipCullTextureEnv ||
         name == StaPipAsIsTextureEnv || name == StaPipClipTextureEnv ||
         name == StaPipBillboardTexture;
}

void recordGsPacket(StaPipTelemetry* telemetry, StaPipVU1Program* program,
                    const u32 inputVertices, const bool emitState) {
  if (telemetry == nullptr || program == nullptr) return;
  StaPipPacketCounters& out = telemetry->packet[telemetryProducer(telemetry)];
  const bool textured = isTexturedProgram(program->getName());
  const bool stateReuse =
      (program->getName() == StaPipCullTextureColor ||
       program->getName() == StaPipCullTextureDirLights ||
       program->getName() == StaPipAsIsTextureColor ||
       program->getName() == StaPipAsIsTextureDirLights) &&
      !emitState;
  const u32 gsVertices = program->getGsVertexCount(inputVertices);
  const u32 registers = program->getReglistCount();
  // StoreTyraGifTags*Alpha emits three A+D state pairs for an untextured
  // package and four for a textured one, followed by the primitive giftag.
  out.gifTags += stateReuse ? 1 : (textured ? 5 : 4);
  out.adWrites += stateReuse ? 0 : (textured ? 4 : 3);
  out.gsPayloadQwords += (stateReuse ? 1 : (textured ? 9 : 7)) +
                         (gsVertices * registers + 1) / 2;
  if (stateReuse) ++out.gsStateReuses;
  ++out.xgkicks;
}

struct VifCounterState {
  u32 pending = 0;
  bool malformed = false;
};

void countVifWord(StaPipPacketCounters& out, VifCounterState& state,
                  const u32 word) {
  ++out.vifWords;
  if (state.pending != 0) {
    --state.pending;
    return;
  }
  const u32 cmd = (word >> 24) & 0xFFu;
  const u32 num = (word >> 16) & 0xFFu;
  if (cmd == 0x00) {
    ++out.vifNops;
    return;
  }
  if ((cmd & 0x60u) == 0x60u) {
    const u32 vn = (cmd >> 2) & 3u;
    const u32 vl = cmd & 3u;
    const u32 count = num == 0 ? 256u : num;
    state.pending = (count * (vn + 1u) * (32u >> vl) + 31u) / 32u;
    ++out.vifUnpack;
    return;
  }
  switch (cmd) {
    case 0x01: ++out.vifStcycl; return;
    case 0x05:  // STMOD
    case 0x07:  // MARK
      return;
    case 0x10: ++out.vifFlushe; return;
    case 0x11: ++out.vifFlush; return;
    case 0x13: ++out.vifFlusha; return;
    case 0x14: ++out.vifMscal; return;
    case 0x15: ++out.vifMscalf; return;
    case 0x17: ++out.vifMscnt; return;
    case 0x20: state.pending = 1; return;  // STMASK
    case 0x30: state.pending = 4; ++out.vifStrow; return;
    case 0x31: state.pending = 4; ++out.vifStcol; return;
    default: state.malformed = true; return;
  }
}

/** Count the exact source chain handed to VIF1. The decoder deliberately
 * understands the same closed VIF command set as StaPipVifHash: an unknown
 * command latches malformed instead of letting every later payload word pose
 * as a command. */
void recordVifChain(StaPipTelemetry* telemetry, const u8 producer,
                    const void* base, const u32 qwc) {
  if (telemetry == nullptr || base == nullptr || qwc == 0) return;
  StaPipPacketCounters& out = telemetry->packet[
      producer < StaPipProducerCount ? producer : StaPipProducerMixed];
  out.chainQwords += qwc;
  const qword_t* src = reinterpret_cast<const qword_t*>(base);
  VifCounterState state;
  u32 i = 0;
  while (i < qwc && !state.malformed) {
    const dma_tag_t* tag = reinterpret_cast<const dma_tag_t*>(&src[i]);
    const u32 tagQwc = static_cast<u32>(tag->QWC);
    const u32 id = static_cast<u32>(tag->ID);
    if (id < 8) ++out.dmaTags[id];
    const u32* tte = reinterpret_cast<const u32*>(&src[i]) + 2;
    countVifWord(out, state, tte[0]);
    countVifWord(out, state, tte[1]);

    const qword_t* payload = nullptr;
    bool stop = false;
    if (id == 1 || id == 7) {  // CNT / END
      if (i + 1 + tagQwc > qwc) {
        state.malformed = true;
        break;
      }
      payload = &src[i + 1];
      out.inlinePayloadQwords += tagQwc;
      i += 1 + tagQwc;
      stop = id == 7;
    } else if (id == 0 || id == 3 || id == 4) {  // REFE / REF / REFS
      const u32 addr = static_cast<u32>(tag->ADDR);
      payload = reinterpret_cast<const qword_t*>(addr);
      out.refPayloadQwords += tagQwc;
      if ((addr & 127u) == 0) ++out.refsAligned128;
      else ++out.refsUnaligned128;
      ++i;
      stop = id == 0;
    } else {
      state.malformed = true;  // NEXT/CALL/RET are not emitted here.
      break;
    }
    for (u32 q = 0; q < tagQwc && !state.malformed; ++q) {
      const u32* words = reinterpret_cast<const u32*>(&payload[q]);
      for (u32 w = 0; w < 4; ++w) countVifWord(out, state, words[w]);
    }
    if (stop) break;
  }
  if (state.malformed || state.pending != 0) ++out.malformedChains;
}
#endif

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

// Modified by TyraX: the object-data fast path (sendObjectData).
//
// Measured on a physical PS2 (Motor District garage day, 75 bags a frame):
// sendObjectData cost ~14 us a bag, and most of it was not the data. A
// section holding ONE qword - the FLUSHE tag, the ALPHA unpack - took ~1.4 us,
// because every packet2 open/close runs the DMA-tag bitfield writes, a
// back-patched VIFcode and a handful of asserts, and every packet2_add_float
// reloads packet->next through memory. Cache misses on the packet lines were
// ~20% (16 qwords: 1.0 us into cold lines, 0.17 us into hot ones).
//
// So each uniform block is now its header qword plus whole-qword copies. The
// header - CNT tag, STCYCL, UNPACK V4_32 - is produced ONCE per (address,
// length) by packet2 itself into a scratch packet and remembered, which makes
// the fast path's bytes identical to the old path's by construction; the
// VIF-hash gate (docs/baked-stream-acceptance-gate.md) compared the two.
struct UnpackHeader {
  u32 key;  // VU address << 8 | qwords; 0 = unused
  qword_t header;
};
constexpr int kUnpackHeaders = 32;
UnpackHeader g_unpackHeaders[kUnpackHeaders];
qword_t g_flusheHeader;
bool g_flusheHeaderReady = false;

packet2_t* headerScratch() {
  static packet2_t* scratch =
      packet2_create(32, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  packet2_reset(scratch, false);
  return scratch;
}

const qword_t& unpackHeader(u32 addr, u32 qwords) {
  const u32 key = (addr << 8) | qwords;
  for (int i = 0; i < kUnpackHeaders; ++i) {
    if (g_unpackHeaders[i].key == key) return g_unpackHeaders[i].header;
    if (g_unpackHeaders[i].key == 0) {
      packet2_t* s = headerScratch();
      packet2_utils_vu_open_unpack(s, addr, false);
      for (u32 q = 0; q < qwords; ++q) packet2_add_2x_s64(s, 0, 0);
      packet2_utils_vu_close_unpack(s);
      g_unpackHeaders[i].header = s->base[0];
      g_unpackHeaders[i].key = key;
      return g_unpackHeaders[i].header;
    }
  }
  TYRA_ASSERT(false, "Object-data unpack header cache is full: raise "
                     "kUnpackHeaders");
  return g_unpackHeaders[0].header;
}

// The object data's leading CNT tag: FLUSHE + NOP, no payload.
const qword_t& flusheHeader() {
  if (!g_flusheHeaderReady) {
    packet2_t* s = headerScratch();
    packet2_chain_open_cnt(s, 0, 0, 0);
    packet2_vif_flushe(s, 0);
    packet2_vif_nop(s, 0);
    packet2_chain_close_tag(s);
    g_flusheHeader = s->base[0];
    g_flusheHeaderReady = true;
  }
  return g_flusheHeader;
}

inline void emitQword(packet2_t* packet, const qword_t& q) {
  *packet->next = q;
  packet->next += 1;
}

// Modified by TyraX: the REPEAT entry block (TYRA_STAPIP_REPEAT_ENTRY): the
// wrap block sendObjectData writes for a bag that wants REPEAT, built by the
// same packet2 calls once. kRepeatEntryQw quadwords: the CNT tag carrying
// FLUSH + DIRECT, the A+D GIFtag, CLAMP_1.
constexpr u32 kRepeatEntryQw = 3;
qword_t g_repeatEntry[kRepeatEntryQw];
bool g_repeatEntryReady = false;
const qword_t* repeatEntryBlock() {
  if (!g_repeatEntryReady) {
    packet2_t* s = headerScratch();
    const texwrap_t& w = RendererCoreGS::repeatWrap();
    packet2_chain_open_cnt(s, 0, 0, 0);
    packet2_vif_flush(s, 0);
    packet2_vif_open_direct(s, 0);
    packet2_add_2x_s64(s, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    packet2_add_2x_s64(s, GS_SET_CLAMP(w.horizontal, w.vertical, w.minu, w.maxu,
                                       w.minv, w.maxv),
                       GS_REG_CLAMP_1);
    packet2_vif_close_direct_auto(s);
    packet2_chain_close_tag(s);
    for (u32 i = 0; i < kRepeatEntryQw; ++i) g_repeatEntry[i] = s->base[i];
    g_repeatEntry[0].dw[0] &= ~0x03FF0000ull;  // DMA tag PAD: never written
    g_repeatEntryReady = true;
  }
  return g_repeatEntry;
}
// Word-for-word, with the DMA tag's PAD bits (0x03FF0000 of word 0) masked:
// packet2 never writes them and the DMAC ignores them.
inline bool sameAsRepeatEntry(const qword_t* q) {
  const qword_t* b = repeatEntryBlock();
  return (q[0].dw[0] & ~0x03FF0000ull) == b[0].dw[0] && q[0].dw[1] == b[0].dw[1] &&
         q[1].dw[0] == b[1].dw[0] && q[1].dw[1] == b[1].dw[1] &&
         q[2].dw[0] == b[2].dw[0] && q[2].dw[1] == b[2].dw[1];
}

// A DMA REF to `qwords` of VIF stream at `data` (16-byte aligned, stable
// until every chain naming it has run). Its VIFcodes are two NOPs, so the
// stream VIF1 sees is exactly the referenced data.
inline void emitRef(packet2_t* packet, const void* data, u32 qwords) {
  qword_t* q = packet->next;
  q->dw[0] = static_cast<u64>(qwords & 0xFFFFU) | (static_cast<u64>(3) << 28) |
             (static_cast<u64>(reinterpret_cast<u32>(data) & 0x0FFFFFFFU)
              << 32);
  q->dw[1] = 0;
  packet->next = q + 1;
}

// A chain fragment of CNT tags + inline data, rewritten as the VIF stream it
// delivers: each tag's DMA half becomes two VIF NOPs, its VIFcodes and data
// stay. Returns the quadwords written (the same count), 0 if it is not a
// plain CNT run.
u32 chainToVifStream(const qword_t* src, u32 qw, qword_t* dst) {
  u32 i = 0;
  while (i < qw) {
    const u64 tag = src[i].dw[0];
    const u32 id = static_cast<u32>((tag >> 28) & 7U);
    const u32 n = static_cast<u32>(tag & 0xFFFFU);
    if (id != 1 || i + 1 + n > qw) return 0;  // only CNT, fully inside
    dst[i].dw[0] = 0;
    dst[i].dw[1] = src[i].dw[1];
    for (u32 k = 1; k <= n; ++k) dst[i + k] = src[i + k];
    i += 1 + n;
  }
  return qw;
}

// One uniform unpack to the absolute VU1 region: header + `qwords` copied
// whole from `src` (16-byte aligned).
inline void emitUnpack(packet2_t* packet, u32 addr, const void* src,
                       u32 qwords) {
  qword_t* q = packet->next;
  q[0] = unpackHeader(addr, qwords);
  const qword_t* s = static_cast<const qword_t*>(src);
  for (u32 i = 0; i < qwords; ++i) q[1 + i] = s[i];
  packet->next = q + 1 + qwords;
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
         a.screenScaleBits[0] == b.screenScaleBits[0] &&
         a.screenScaleBits[1] == b.screenScaleBits[1] &&
         a.programAddress == b.programAddress && a.slotQw == b.slotQw &&
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
        freed += item.storageQw();
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
    const u32 freed = storage[victim].storageQw();
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
      if (item.packages != key.packages || item.slotQw != key.slotQw) {
        const u32 had = item.storageQw();
        const u32 wants = key.storageQw();
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
        item.slotQw = key.slotQw;
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
      item.screenScaleBits[0] = k.screenScaleBits[0];
      item.screenScaleBits[1] = k.screenScaleBits[1];
      item.programAddress = k.programAddress;
      item.singleColor = k.singleColor;
      item.stripped = k.stripped;
      item.blockQw = 0;
      memset(item.ready.get(), 0, item.packages);
      return &item;
    }
    index = item.nextInBucket;
  }

  const u32 wanted = key.storageQw();
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
  entry.screenScaleBits[0] = key.screenScaleBits[0];
  entry.screenScaleBits[1] = key.screenScaleBits[1];
  entry.programAddress = key.programAddress;
  entry.slotQw = key.slotQw;
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

constexpr size_t kBakedPressureMargin = 512u * 1024u;

StaPipBakedStreams::StaPipBakedStreams() {
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
}

StaPipBakedStreams::~StaPipBakedStreams() {
  // Teardown is a real compatibility fence; no unsubmitted reader may survive.
  Vif1Queue::drain();
  clear();
  for (auto& slot : graveyard)
    for (auto& item : slot) Vif1Queue::collectRetiredImmutableSpan(item.arena);
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
         a.bboxVersion == b.bboxVersion &&
         // Modified by TyraX: the second version field. See
         // StaPipBakedEntry::contentVersion - this is what sees a caller
         // re-shading per-vertex colours in place, which bboxVersion cannot
         // and must not, because its meaning is the bounding box.
         a.contentVersion == b.contentVersion && a.primKey == b.primKey &&
         a.depthScaleBits == b.depthScaleBits &&
         a.programAddr == b.programAddr && a.singleColor == b.singleColor &&
         a.stripped == b.stripped;
}

#if TYRA_STAPIP_BAKED_SAMPLE_VERIFY
void StaPipBakedStreams::pickVerifyTarget() {
  if (storage.empty()) {
    verifyTarget = nullptr;
    return;
  }
  verifyTarget = storage[verifyCursor % storage.size()].get();
  ++verifyCursor;
}
#endif

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
  // Modified by TyraX: frame snapshots may borrow this immutable VIF stream.
  // Keep their bank leases until completion; ordinary eviction need not split
  // the recorded frame. Poison diagnostics still require an immediate fence.
  Vif1Queue::retireImmutableSpan(item.arena);
  const u32 qw = item.arenaQw;
  usedQwords -= qw < usedQwords ? qw : usedQwords;
#if TYRA_STAPIP_BAKED_POISON
  Vif1Queue::unregisterImmutableSpan(item.arena);
  // The adversarial mode: overwrite an evicted arena NOW instead of letting the
  // two-frame graveyard hide it. If any in-flight packet still names this
  // block, the next capture tears visibly instead of the defect waiting for
  // content that happens to expose it. See TYRA_STAPIP_BAKED_POISON.
  //
  // The pattern is chosen to be loud rather than plausible: as a VIFcode 0xDE
  // is not one this pipeline emits, so the gate's decoder latches `broken` and
  // names it, and as geometry it is a wild float - either way the arm reports
  // rather than limps.
  if (item.arena != nullptr && qw > 0) memset(item.arena, 0xDE, qw * 16);
#endif
  if (item.raw) {
    graveyard[graveyardWrite].push_back({std::move(item.raw), item.arena, qw});
    retiredQwords += qw;
  }
  item.arena = nullptr;
  item.arenaQw = 0;
}

void StaPipBakedStreams::onFrameEnd() {
  evictionsThisFrame = 0;
  ++frameNow;
  graveyardWrite ^= 1;
  auto& retired = graveyard[graveyardWrite];
  retired.erase(std::remove_if(retired.begin(), retired.end(),
       [this](const RetiredArena& item) {
         if (!Vif1Queue::collectRetiredImmutableSpan(item.arena)) return false;
         retiredQwords -= item.qwords;
         return true;
       }), retired.end()); // age alone cannot release an actual bank reader

  // Sample outside the draw loop. Under pressure, give gameplay first use of
  // the heap, while retiring streams normally so recorded DMA can finish.
  if ((frameNow & 7u) == 0 && (lowHeadroom || !storage.empty())) {
    const size_t margin = lowHeadroom ? 2u * kBakedPressureMargin
                                     : kBakedPressureMargin;
    const bool low = !Info::hasRuntimeMemory(margin);
    if (low && !lowHeadroom) clear();
    lowHeadroom = low;
  }

  bool expired = false;
  for (auto& item : storage) {
#if TYRA_STAPIP_PREPARED_STATIC
    if (item->scenePinned) continue;
#endif
    if (item->framesLeftToDestroy > 0) {
      item->framesLeftToDestroy--;
      if (item->framesLeftToDestroy <= 0) expired = true;
    }
  }
  if (expired) {
    for (auto& item : storage)
      if (item->framesLeftToDestroy <= 0) {
        retire(*item);
        noteDestroyed(item.get());  // Modified by TyraX: stamps
      }
    const auto newEnd =
        std::remove_if(storage.begin(), storage.end(),
                       [](const std::unique_ptr<StaPipBakedEntry>& item) {
                         return item->framesLeftToDestroy <= 0;
                       });
    storage.erase(newEnd, storage.end());
    rebuildIndex();
  }

#if TYRA_STAPIP_BAKED_SAMPLE_VERIFY
  // Modified by TyraX: choose the NEXT frame's verification victim, and do it
  // AFTER the expiry sweep above - picking before it could leave `verifyTarget`
  // naming an entry this very call erased. Once a frame, outside the
  // submission order, so the cursor cannot race the bags within a frame.
  pickVerifyTarget();
#endif
}

void StaPipBakedStreams::clear() {
  ++eraseGen;  // Modified by TyraX: stamps
  for (auto& item : storage) retire(*item);
  storage.clear();
  std::fill(indexBuckets, indexBuckets + kBucketCount, -1);
  usedQwords = 0;
  evictionsThisFrame = 0;
#if TYRA_STAPIP_BAKED_SAMPLE_VERIFY
  verifyTarget = nullptr;  // everything it could have named is gone
#endif
}

bool StaPipBakedStreams::evictFor(u32 wanted) {
  while (usedQwords + wanted > kMaxQwords) {
    if (evictionsThisFrame >= kMaxEvictionsPerFrame) return false;
    int victim = -1;
    int oldest = kLifetimeFrames;  // never an entry this frame has touched
    for (u32 i = 0; i < storage.size(); ++i) {
#if TYRA_STAPIP_PREPARED_STATIC
      if (storage[i]->scenePinned) continue;
#endif
      if (storage[i]->framesLeftToDestroy < oldest) {
        oldest = storage[i]->framesLeftToDestroy;
        victim = static_cast<int>(i);
      }
    }
    if (victim < 0) return false;
    noteDestroyed(storage[victim].get());  // Modified by TyraX: stamps
    retire(*storage[victim]);
#if TYRA_STAPIP_BAKED_SAMPLE_VERIFY
    // Modified by TyraX: eviction runs mid-frame, so drop the sampled arm's
    // target if this is it. `verifying()` only ever COMPARES the pointer, but
    // a later entry allocated at the same address would otherwise be picked by
    // accident - a harmless wrong answer is still a wrong answer.
    if (verifyTarget == storage[victim].get()) verifyTarget = nullptr;
#endif
    storage.erase(storage.begin() + victim);
    rebuildIndex();
    ++evictionsThisFrame;
  }
  return true;
}

std::unique_ptr<u8[]> StaPipBakedStreams::allocateArena(u32 qwords) {
  if (lowHeadroom || qwords > kMaxQwords ||
      !Info::hasRuntimeMemory(qwords * 16u + 15u + kBakedPressureMargin))
    return nullptr;
  if (usedQwords + qwords > kMaxQwords && !evictFor(qwords)) return nullptr;
  // Eviction drops the logical charge immediately. A DMA bank can still own
  // the bytes, so a replacement waits until onFrameEnd actually frees them.
  if (usedQwords + retiredQwords + qwords > kMaxQwords) return nullptr;
  std::unique_ptr<u8[]> raw(new (std::nothrow) u8[qwords * 16u + 15u]);
  if (raw) usedQwords += qwords;
  return raw;
}

// Modified by TyraX: the fold beginBakedBag uses for key.contentVersion,
// over explicit stamp values (StaPipBagPatch).
static inline u32 foldContentStamps(const u32 s[4]) {
  return s[0] * 0x9E3779B1U + s[1] * 0x85EBCA6BU + s[2] * 0xC2B2AE35U +
         s[3] * 0x27D4EB2FU;
}

void StaPipBakedStreams::retireArena(std::unique_ptr<u8[]> raw, qword_t* arena,
                                     u32 qw) {
  Vif1Queue::retireImmutableSpan(arena);
  usedQwords -= qw < usedQwords ? qw : usedQwords;
#if TYRA_STAPIP_BAKED_POISON
  Vif1Queue::unregisterImmutableSpan(arena);
  if (arena != nullptr && qw > 0) memset(arena, 0xDE, qw * 16);
#endif
  if (raw) {
    graveyard[graveyardWrite].push_back({std::move(raw), arena, qw});
    retiredQwords += qw;
  }
}

StaPipBakedEntry* StaPipBakedStreams::acquire(const StaPipBakedEntry& key,
                                              const StaPipBagPatch* patch) {
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
      // Modified by TyraX (R5): the program too - a partial bag keeps its
      // cull blocks AND a whole-clip variant over the same array.
      const bool samePass = item.primKey == key.primKey &&
                            item.singleColor == key.singleColor &&
                            item.stripped == key.stripped &&
                            item.program == key.program;
      if (!samePass && variants < kVariantsPerArray) {
        index = item.nextInBucket;
        continue;  // a different pass over the same array - keep looking
      }
      item.framesLeftToDestroy = kLifetimeFrames;
      if (keyMatches(item, key)) return &item;
      // Modified by TyraX: payload refresh (StaPipBakedEntry::runs). The
      // payload moved - a dent, a re-shaded paint pass, anything stamped or
      // bbox-bumped - and nothing structural did: rebuild the arena in one
      // pass, payloads fresh from the bag's arrays. A car's shine re-shade
      // was a whole bake every time (~2.3 ms of EE for a 2 025-vertex body on
      // a physical PS2), a dent ~7-11 ms. It replaces exactly the rebuilds
      // that would have happened: a payload that moves again within
      // kChurnGapFrames still goes to the churn damping below, as before.
      if (TYRA_STAPIP_PAYLOAD_REFRESH && item.complete && item.arena != nullptr &&
          !item.churning &&
          (frameNow - item.lastRebuildFrame > kChurnGapFrames || item.moveStreak < kRefreshStreak) &&
          item.runsValid && item.count == key.count &&
          item.packages == key.packages && item.sts == key.sts &&
          item.colors == key.colors && item.normals == key.normals &&
          item.program == key.program && item.programAddr == key.programAddr &&
          item.primKey == key.primKey && item.singleColor == key.singleColor &&
          item.stripped == key.stripped &&
          item.depthScaleBits == key.depthScaleBits &&
          (item.bboxVersion != key.bboxVersion ||
           item.contentVersion != key.contentVersion)
#if TYRA_STAPIP_BAKED_ANY_VERIFY
          && !verifying(&item)
#endif
      ) {
        const u32 qw = item.arenaQw;
        if (auto raw = allocateArena(qw)) {
          qword_t* arena = reinterpret_cast<qword_t*>(
              (reinterpret_cast<u32>(raw.get()) + 15u) & ~15u);
          const qword_t* streams[4] = {
              static_cast<const qword_t*>(key.vertices),
              static_cast<const qword_t*>(key.colors),
              static_cast<const qword_t*>(key.sts),
              static_cast<const qword_t*>(key.normals)};
          size_t r = 0;
          const size_t nRuns = item.runs.size();
          for (u32 pk = 0; pk < item.packages; ++pk) {
            const u32 base = item.offsets[pk], size = item.sizes[pk];
            u32 cursor = 0;
            while (r < nRuns && item.runs[r].package == pk) {
              const auto& run = item.runs[r++];
              if (run.at > cursor)
                memcpy(arena + base + cursor, item.arena + base + cursor,
                       (run.at - cursor) * 16);
              memcpy(arena + base + run.at, streams[run.stream] + run.vertex,
                     run.qwc * 16);
              cursor = run.at + run.qwc;
            }
            if (size > cursor)
              memcpy(arena + base + cursor, item.arena + base + cursor,
                     (size - cursor) * 16);
          }
          retireArena(std::move(item.raw), item.arena, qw);
          item.raw = std::move(raw);
          item.arena = arena;
          Vif1Queue::registerImmutableSpan(arena, qw);
          item.bboxVersion = key.bboxVersion;
          item.contentVersion = key.contentVersion;
          for (int k = 0; k < 4; ++k) item.streamStamps[k] = key.streamStamps[k];
          item.serial = ++serialNext;  // the arena moved
          item.moveStreak = frameNow - item.lastRebuildFrame > kChurnGapFrames
                                ? 1
                                : (u8)(item.moveStreak + 1);
          item.lastRebuildFrame = frameNow;
          item.refreshed = 1;
          ++colourRefreshes;
          return &item;
        }
      }
      // Churn damping (see kChurnHoldFrames). Only a PAYLOAD move counts -
      // the two stamps - so a second pass, a re-pinned package size or a
      // program swap keep the behaviour they had.
      {
        const bool payloadMoved = item.bboxVersion != key.bboxVersion ||
                                  item.contentVersion != key.contentVersion;
        if (item.churning) {
          if (key.bboxVersion == item.seenBBoxVersion &&
              key.contentVersion == item.seenContentVersion) {
            if (++item.settled >= kSettleFrames) item.churning = 0;
          } else {
            item.seenBBoxVersion = key.bboxVersion;
            item.seenContentVersion = key.contentVersion;
            item.settled = 0;
          }
        } else if (payloadMoved && item.complete &&
                   frameNow - item.lastRebuildFrame <= kChurnGapFrames) {
          item.churning = 1;
          item.seenBBoxVersion = key.bboxVersion;
          item.seenContentVersion = key.contentVersion;
          item.settled = 0;
        }
        if (item.churning) {
          ++churnSkips;
          return nullptr;  // not baked this frame; the retained route draws it
        }
        if (payloadMoved) item.lastRebuildFrame = frameNow;
        // Modified by TyraX: patch rebake. The owner promised which vertices
        // changed between the state this entry holds and the one asked for;
        // take it only when this entry holds EXACTLY that FROM state, the
        // request is EXACTLY the TO state, and nothing else in the key moved.
        if (payloadMoved && patch != nullptr && patch->dirty != nullptr &&
            item.complete && item.arena != nullptr &&
            patch->count == key.count && item.count == key.count &&
            item.packages == key.packages && item.sts == key.sts &&
            item.colors == key.colors && item.normals == key.normals &&
            item.program == key.program && item.programAddr == key.programAddr &&
            item.primKey == key.primKey && item.singleColor == key.singleColor &&
            item.stripped == key.stripped &&
            item.depthScaleBits == key.depthScaleBits &&
            item.bboxVersion == patch->fromBBox &&
            item.contentVersion == foldContentStamps(patch->fromStamps) &&
            key.bboxVersion == patch->toBBox &&
            key.contentVersion == foldContentStamps(patch->toStamps)
#if TYRA_STAPIP_BAKED_ANY_VERIFY
            && !verifying(&item)
#endif
        ) {
          item.patchOffsets.assign(item.offsets.get(),
                                   item.offsets.get() + item.packages);
          item.patchSizes.assign(item.sizes.get(), item.sizes.get() + item.packages);
          item.patchRaw = std::move(item.raw);
          item.patchArena = item.arena;
          item.patchQw = item.arenaQw;
          item.arena = nullptr;
          item.arenaQw = 0;
          item.serial = ++serialNext;  // the arena moves, as in a rebuild
          item.bboxVersion = key.bboxVersion;
          item.contentVersion = key.contentVersion;
          for (int k = 0; k < 4; ++k) item.streamStamps[k] = key.streamStamps[k];
          item.built = 0;
          item.complete = 0;
          item.patching = 1;
          ++patches;
          return &item;
        }
      }
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
      else if (item.contentVersion != key.contentVersion)
        reason = MissContentVersion;
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
      item.serial = ++serialNext;  // Modified by TyraX: stamps - arena moves
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
      item.contentVersion = key.contentVersion;
      item.primKey = key.primKey;
      item.depthScaleBits = key.depthScaleBits;
      item.programAddr = key.programAddr;
      item.singleColor = key.singleColor;
      item.stripped = key.stripped;
      item.built = 0;
      item.complete = 0;
      for (int k = 0; k < 4; ++k) item.streamStamps[k] = key.streamStamps[k];
      item.runs.clear();
      for (u32& q : item.runQw) q = 0;
      item.unknownRefs = 0;
      item.runsValid = 0;
      return key.packages == 0 ? nullptr : &item;
    }
    index = item.nextInBucket;
  }

  if (key.packages == 0 || lowHeadroom) return nullptr;

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
  entry->contentVersion = key.contentVersion;
  for (int k = 0; k < 4; ++k) entry->streamStamps[k] = key.streamStamps[k];
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
  entry->serial = ++serialNext;  // Modified by TyraX: stamps

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
  packets = new packet2_t*[kPacketCount];
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
  for (u16 i = 0; i < kPacketCount; i++)
    packets[i] = packet2_create(packetSize, probeType, P2_MODE_CHAIN, true);
  probePacketUsesPool = false;
  // VERIFY THE ALLOCATION TOOK EFFECT BEFORE TRUSTING A SINGLE NUMBER FROM
  // THIS ARM. A packet that quietly came back in the normal segment would
  // measure "no flush needed" while the DMA read stale cache lines - the
  // failure mode is intermittent wrong geometry, not a crash. The segment is
  // the top nibble of the base pointer: 0x3 accelerated, 0x2 uncached.
  for (u16 i = 0; i < kPacketCount; i++) {
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
  for (u16 i = 0; i < kPacketCount; i++)
    packets[i] =
        packet2_create(packetSize, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
#endif
  for (u16 i = 0; i < kPacketCount; i++) packetOrigin[i] = packets[i]->base;

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
  frameWaitVif();
  rendererCore->texture.clearMutationBarrier(this);
  submissionBatchScope = false;
  submissionBatchCandidate = false;
#if TYRA_STAPIP_RETAINED_COMMANDS
  // Modified by TyraX: a scene teardown frees every stream the retained REF
  // tags name. Nothing may survive it - the pointer compare would only catch a
  // reallocation that came back at a DIFFERENT address.
  retainedCurrent = nullptr;
  retained.clear();
  clipBlockQw[0] = clipBlockQw[1] = 0;
  clipBlockVifQw[0] = clipBlockVifQw[1] = 0;
#endif
#if TYRA_STAPIP_BAKED_STREAM
  // Same reasoning, and one more: a baked block is named by DMA REF tags, so
  // nothing may outlive the pipeline that submitted them.
  bakedCurrent = nullptr;
  bakeScratch.clear();
  baked.clear();
  if (partialBakePacket) packet2_free(partialBakePacket);
  partialBakePacket = nullptr;
#endif
  packet2_free(staticDataPacket);
  for (u16 i = 0; i < kPacketCount; i++) {
    packets[i]->base = packets[i]->next = packetOrigin[i];  // the own buffer
    packet2_free(packets[i]);
  }
  delete[] packets;

  for (u16 i = 0; i < buffersCount; i++) delete buffers[i];
  delete[] buffers;
  buffers = nullptr;  // Modified by TyraX: isAllocated()

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
  clipBlockQw[0] = clipBlockQw[1] = 0;
  // Modified by TyraX: their inputs were just written.
  clipBlockVifQw[0] = clipBlockVifQw[1] = 0;
#endif

  dma_channel_initialize(DMA_CHANNEL_VIF1, nullptr, 0);
#if TYRA_VIF1_QUEUE
  Vif1Queue::init();  // Modified by TyraX: see vif1_queue.hpp
#endif

  setProgramsCache();

  reinitVU1();

  TYRA_LOG("StaPipQBufferRenderer initialized");
}

void StaPipQBufferRenderer::reinitVU1() {
  DrawEpochs::programsChanged();  // Modified by TyraX: stamp records
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
StaPipClipperSpot buildSpotForBagInverse(const RendererCoreSpotLight& spot,
                                         const float* inv, const float* invT);

StaPipClipperSpot buildSpotForBag(const RendererCoreSpotLight& spot,
                                  const M4x4* model) {
  if (!spot.enabled) {
    StaPipClipperSpot out;
    out.enabled = false;
    return out;
  }
  float inv[9], invT[3];
  invertAffine(model->data, inv, invT);
  return buildSpotForBagInverse(spot, inv, invT);
}

// The same, with the model's inverse already known (a stamp keeps it).
StaPipClipperSpot buildSpotForBagInverse(const RendererCoreSpotLight& spot,
                                         const float* inv, const float* invT) {
  StaPipClipperSpot out;
  out.enabled = spot.enabled;
  if (!spot.enabled) return out;

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
  // Modified by TyraX: the X/Y band follows the bag (setClipBandTight).
  const float band = clipTight ? VU1_CLIP_XY_BAND_TIGHT : VU1_CLIP_XY_BAND;
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
      // guard band X/Y at +/-band * w - strictly inside the GS raster
      // window; the scissor trims the rest of the way
      {-1.0F, 0.0F, 0.0F, band, 0.0F, 0.0F, 0.0F, 0.0F},
      {1.0F, 0.0F, 0.0F, band, 0.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, -1.0F, 0.0F, band, 0.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F, band, 0.0F, 0.0F, 0.0F, 0.0F},
  };
  packet2_utils_vu_open_unpack(objectDataPacket, VU1_CLIP_PLANES_ADDR, false);
  {
    for (u32 i = 0; i < 6; i++)
      for (u32 j = 0; j < 8; j++)
        packet2_add_float(objectDataPacket, planes[i][j]);
  }
  packet2_utils_vu_close_unpack(objectDataPacket);
}

// Modified by TyraX: with the VIF1 queue a chain runs well after it was
// built, so a REF to the renderer's MVP / light / single-colour storage would
// read whatever the NEXT bag wrote there. Copy them into the packet instead -
// the same inline form bounded submission batching already uses.
constexpr bool kInlineUniforms = TYRA_VIF1_QUEUE != 0;

void StaPipQBufferRenderer::sendObjectData(
    StaPipBag* bag, M4x4* mvp, RendererCoreTextureBuffers* texBuffers) {
#if TYRA_VU1_EXP_PAINT
  const bool paintVu = bag->texture && bag->texture->paintVu;
  TYRA_ASSERT(!paintVu || supportsPaintVu(bag),
              "VU paint uniforms require a supported built-in paint program!");
#endif
  // Modified by TyraX: build uniforms at the head of the same packet that the
  // first geometry flush will append to. This removes one VIF1 DMA launch and
  // its intervening EE wait per visible bag without relying on a NEXT/CALL
  // jump between separately allocated chains (which is not stable on hardware).
  // The other packet was completed before sendPacket flipped back to this
  // context, so it is safe to reuse without adding a preparation-side wait.
  auto* objectDataPacket = packets[context];
  if (!objectDataPending) {
    startPacket(objectDataPacket);
    packetWrapSet = false;  // a fresh packet carries no wrap write yet
#if TYRA_STAPIP_PACKET_PROFILE
    packetTelemetryProducer = telemetryProducer(telemetry);
  } else if (packetTelemetryProducer != telemetryProducer(telemetry)) {
    // Submission batching may deliberately keep one VIF chain open across
    // producer scopes. Do not lie by charging that whole chain to whichever
    // scope happened to flush it.
    packetTelemetryProducer = StaPipProducerMixed;
#endif
  }
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
  emitQword(objectDataPacket, flusheHeader());  // Modified by TyraX: fast path
#if TYRA_STAPIP_STAMP
  // Modified by TyraX: stamp capture starts at this bag's FLUSHE; every
  // block below that a stamp cannot re-create clears stampOk.
  stampHead = objectDataPacket->next - 1;
  stampOk = stampArmed && kInlineUniforms;
  stampHasTex0 = 0;
  stampFogEligible = 0;
  stampWrapBlock = 0;
  stampSingleAt = 0;
  stampEnvAt = 0;
  stampEntryAt = 0;  // Modified by TyraX: REPEAT entry
  stampEntryQw = 0;
#endif

  // Modified by TyraX: the bag's texture wrap, in the chain (setBagWrap).
  // FLUSH rather than the FLUSHE above: the previous bag's last XGKICK may
  // still be on PATH1, and CLAMP_1 must reach the GS after those primitives,
  // not between them. DIRECT then hands the A+D pair to the GIF over PATH2.
  if (bagWrap != nullptr) {
    const texwrap_t& inForce =
        packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
    if (!RendererCoreGS::wrapEquals(*bagWrap, inForce)) {
#if TYRA_STAPIP_STAMP
      // State-dependent: a stamp keeps it only in its deterministic form,
      // the first non-REPEAT bag after REPEAT was in force.
      if (RendererCoreGS::wrapEquals(inForce, RendererCoreGS::repeatWrap())) {
        stampWrapBlock = 1;
        stampWrap = *bagWrap;
#if TYRA_STAPIP_REPEAT_ENTRY
      } else if (RendererCoreGS::wrapEquals(*bagWrap, RendererCoreGS::repeatWrap())) {
        // Modified by TyraX: the REPEAT entry - captured without this block.
        stampEntryAt = static_cast<u16>(objectDataPacket->next - stampHead);
#endif
      } else {
        stampOk = false;
      }
#endif
      packet2_chain_open_cnt(objectDataPacket, 0, 0, 0);
      packet2_vif_flush(objectDataPacket, 0);
      packet2_vif_open_direct(objectDataPacket, 0);
      packet2_add_2x_s64(objectDataPacket,
                         GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1),
                         GIF_REG_AD);
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_CLAMP(bagWrap->horizontal, bagWrap->vertical, bagWrap->minu,
                       bagWrap->maxu, bagWrap->minv, bagWrap->maxv),
          GS_REG_CLAMP_1);
      packet2_vif_close_direct_auto(objectDataPacket);
      packet2_chain_close_tag(objectDataPacket);
#if TYRA_STAPIP_STAMP && TYRA_STAPIP_REPEAT_ENTRY
      if (stampEntryAt != 0)
        stampEntryQw =
            static_cast<u16>(objectDataPacket->next - stampHead - stampEntryAt);
#endif
      packetWrap = *bagWrap;
      packetWrapSet = true;
    }
    bagWrap = nullptr;
  }
  if (submissionBatchCandidate || kInlineUniforms) {
#if TYRA_STAPIP_STAMP
    stampMvpAt = static_cast<u16>(objectDataPacket->next - stampHead);
#endif
    emitUnpack(objectDataPacket, VU1_MVP_MATRIX_ADDR, mvp->data, 4);
  } else {
    packet2_utils_vu_add_unpack_data(objectDataPacket, VU1_MVP_MATRIX_ADDR,
                                     mvp->data, 4, false);
  }

  // Modified by TyraX: experiment (b), TYRA_VU1_EXP_EE_LIGHT_FOLD - the
  // *_fold images read the light directions pre-multiplied by the light
  // matrix and never read the matrix, so the EE folds D * M here (nine
  // multiplies a bag, against three VU1 instructions a vertex) and skips the
  // matrix upload. A project override of a lit program keeps the unfolded
  // pair: its generator still does the multiply on VU1.
  bool foldLights = false;
#if TYRA_STAPIP_STAMP
  if (bag->lighting) stampOk = false;  // light uniforms are live state
#endif
#if TYRA_VU1_EXP_EE_LIGHT_FOLD
  foldLights = bag->lighting != nullptr && !litProgramOverridden();
#endif
  if (bag->lighting) {
  if (foldLights) {
    // VU1: n_w = M0*n.x + M1*n.y + M2*n.z (Mi = the matrix's qword i), then
    // c = D0*n_w.x + D1*n_w.y + D2*n_w.z; so Fi = D0*Mi.x + D1*Mi.y + D2*Mi.z.
    const float* m = bag->lighting->lightMatrix->data;
    const Vec4* d = bag->lighting->dirLights->getLightDirections();
    alignas(16) Vec4 f[3];
    for (int i = 0; i < 3; i++) {
      const float* mi = m + i * 4;
      f[i].x = d[0].x * mi[0] + d[1].x * mi[1] + d[2].x * mi[2];
      f[i].y = d[0].y * mi[0] + d[1].y * mi[1] + d[2].y * mi[2];
      f[i].z = d[0].z * mi[0] + d[1].z * mi[1] + d[2].z * mi[2];
      f[i].w = 0.0F;
    }
    emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, f, 3);
    // One line per boot, so an A/B arm can prove the fold path RAN - a scene
    // with no lit mesh never takes it and measures nothing.
    static bool foldLogged = false;
    if (!foldLogged) {
      foldLogged = true;
      TYRA_LOG("VU1 experiment (b): first folded light upload");
    }
  } else if (submissionBatchCandidate || kInlineUniforms) {
      emitUnpack(objectDataPacket, VU1_LIGHTS_MATRIX_ADDR,
                 bag->lighting->lightMatrix->data, 3);
      emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR,
                 bag->lighting->dirLights->getLightDirections(), 3);
    } else {
      packet2_utils_vu_add_unpack_data(objectDataPacket,
                                       VU1_LIGHTS_MATRIX_ADDR,
                                       bag->lighting->lightMatrix, 3, false);
      packet2_utils_vu_add_unpack_data(
          objectDataPacket, VU1_LIGHTS_DIRS_ADDR,
          bag->lighting->dirLights->getLightDirections(), 3, false);
  }
    // The colours and ambient go up in BOTH cases - a first cut of the
    // fold skipped them with the matrix and every lit mesh drew black
    // (caught by the PCSX2 pixel A/B; --vu-check stages memory itself and
    // cannot see an EE upload that is missing).
    // add_unpack_data emits a DMA REF, not a copy. The mode-adjusted
    // colors must live in the packet, never in a temporary stack array.
    const Vec4* colors = bag->lighting->dirLights->getLightColors();
    {
      alignas(16) Vec4 c[4] = {colors[0], colors[1], colors[2], colors[3]};
      c[3].w = bag->lighting->dirLights->signedSH ? -1.0F : 0.0F;
      emitUnpack(objectDataPacket, VU1_LIGHTS_COLORS_ADDR, c, 4);
    }
  }

  // Modified by TyraX: dynamic light for the color programs - the per-bag
  // pick from StaPipCore (flashlight or the strongest scene point light),
  // falling back to the global flashlight state when no pick was made.
  // The dir-lights addresses are free when the bag has no lighting - the
  // C/TC programs read the three spot quads from there. The same numbers go
  // to the EE clipper for the as_is path.
  //
  // Modified by TyraX: the VU1 ARITHMETIC is gated - `spotActive` below rides
  // in VU1_OPTIONS_ADDR.y and
  // the cull/clip colour programs branch over CalculateTyraSpotLight when it
  // is clear. `enabled` is exactly the predicate the EE clipper's
  // addSpotToColor already used, so the two halves of the formula are gated
  // by one fact and a skipped mesh renders bit-identically. An inactive
  // spot's uniform quads are also omitted unless a custom program may read them.
  bool spotActive = false;
  if (!bag->lighting) {
    const auto& light = bagLight ? *bagLight : rendererCore->spot;
    auto meshSpot = buildSpotForBag(light, bag->info->model);
    // Modified by TyraX: selection is unchanged. Skip the stock lighting
    // loop only when the selected light contributes zero to every vertex.
    // Custom VU stages retain their light/uniform contract even outside it.
    if (meshSpot.enabled && bagLightBounds && !repository.hasAnyOverride() &&
        stapipSpotHasNoInfluence(meshSpot, *bagLightBounds))
      meshSpot.enabled = false;
    clipper.setSpot(meshSpot);
    spotActive = meshSpot.enabled;

    // Modified by TyraX: the VU1 colour programs never read an inactive
    // spot's three quads. An override may, so preserve the upload for one.
#if TYRA_STAPIP_STAMP
    // The spot quads are re-derived per replay; record where they sit and
    // whether the block exists at all (its presence is part of the layout).
    stampSpotActive = spotActive ? 1 : 0;
    stampSpotAt = 0;
    if (repository.hasAnyOverride()) stampOk = false;
#endif
    if (meshSpot.enabled || repository.hasAnyOverride()) {
#if TYRA_STAPIP_STAMP
      stampSpotAt = static_cast<u16>(objectDataPacket->next - stampHead);
#endif
      alignas(16) const float s[12] = {
          meshSpot.position.x,  meshSpot.position.y,  meshSpot.position.z,
          meshSpot.invRange2,   meshSpot.direction.x, meshSpot.direction.y,
          meshSpot.direction.z, meshSpot.cosCut2,
          meshSpot.enabled ? meshSpot.color[0] : 0.0F,
          meshSpot.enabled ? meshSpot.color[1] : 0.0F,
          meshSpot.enabled ? meshSpot.color[2] : 0.0F,
          meshSpot.invSoft};
      emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, s, 3);
    }

    // Modified by TyraX: the two quadwords a project's own microprogram reads
    // (docs/vu-authoring.md). Inside the `if (!bag->lighting)` on purpose -
    // they occupy the directional-lights COLOUR block, which a lit bag needs.
    if (vuCustomEnabled) {
#if TYRA_STAPIP_STAMP
      stampOk = false;
#endif
      {
        alignas(16) const float c[8] = {vuParams[0], vuParams[1], vuParams[2],
                                        vuParams[3], vuTime[0],   vuTime[1],
                                        vuTime[2],   vuTime[3]};
        emitUnpack(objectDataPacket, VU1_CUSTOM_PARAMS_ADDR, c, 2);
      }
    }
  }

  // Modified by TyraX: VU1 clipping data. One quad of constants for the
  // per-triangle crossing test (see stapip_vu1_shared_defines.h) and the six
  // clip planes as (A,B,C,D)+(E,0,0,0) pairs; inside = dot4(v,ABCD) + E >= 0.
  // Uploaded per mesh: other pipelines may reuse this VU1 memory in between.
  const bool mayClip = bagMayClip;
  bagMayClip = true;
  if (vu1Clipping && mayClip) {
#if TYRA_STAPIP_STAMP
    stampOk = false;  // only partial bags clip; stamps are whole bags
#endif
#if TYRA_STAPIP_RETAINED_COMMANDS
    // Modified by TyraX: retained command data. These fifteen quadwords are
    // the same for every mesh in the run - their only inputs are the
    // renderer's near/far pair and the guard-band constant - so the chain is
    // captured once out of the packet addClipChain just wrote it into and
    // replayed with a memcpy afterwards. 52 float stores per bag became one
    // copy. init() and setVU1Clipping() are the only places those inputs can
    // move and both drop the capture.
    const int cb = clipTight ? 1 : 0;  // which band's capture
    if (clipBlockVifQw[cb] != 0) {
      // Modified by TyraX: one REF to the shared VIF-stream copy instead of
      // fifteen quadwords per bag (193 us of a 75-bag frame on a PS2).
      emitRef(objectDataPacket, clipBlockVif[cb], clipBlockVifQw[cb]);
    } else if (clipBlockQw[cb] != 0) {
      appendChainQwords(objectDataPacket, clipBlock[cb], clipBlockQw[cb]);
    } else {
      const u32 clipStart = packet2_get_qw_count(objectDataPacket);
      addClipChain(objectDataPacket);
      const u32 qw = packet2_get_qw_count(objectDataPacket) - clipStart;
      if (qw > 0 && qw <= (sizeof(clipBlock[cb]) / sizeof(clipBlock[cb][0]))) {
        memcpy(clipBlock[cb],
               reinterpret_cast<const u8*>(objectDataPacket->base) +
                   clipStart * 16,
               qw * 16);
        clipBlockQw[cb] = static_cast<u16>(qw);
        // The REF copy: rewritten only here, after every chain that could
        // still read the old one has run.
        frameWaitVif();
        clipBlockVifQw[cb] = static_cast<u16>(chainToVifStream(
            clipBlock[cb], clipBlockQw[cb], clipBlockVif[cb]));
      }
    }
#else
    addClipChain(objectDataPacket);
#endif
  }

  u8 singleColorEnabled = bag->color->single != nullptr;

  if (singleColorEnabled) {  // Color is placed in 4th slot of
                            // VU1_LIGHTS_MATRIX_ADDR
    if (submissionBatchCandidate || kInlineUniforms) {
#if TYRA_STAPIP_STAMP
      stampSingleAt = static_cast<u16>(objectDataPacket->next - stampHead);
#endif
      emitUnpack(objectDataPacket, VU1_SINGLE_COLOR_ADDR,
                 bag->color->single->rgba, 1);
    } else {
#if TYRA_STAPIP_STAMP
      stampOk = false;
#endif
      Packet2TyraUtils::addUnpackData(objectDataPacket, VU1_SINGLE_COLOR_ADDR,
                                      bag->color->single->rgba, 1, false);
    }
  }

  // Modified by TyraX: the options block through the fast path too (see
  // emitUnpack) - built as whole qwords, the same GS_SET_* words packet2's
  // helpers would have written, then one cached header + copy.
  {
    alignas(16) qword_t opt[4];
    u32 optQw = 0;
    const u32 sharedClipVariant =
        bag->lighting != nullptr ||
                (bag->texture != nullptr && bag->texture->coordinatesAreNormals)
            ? 1
            : 0;
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
    const s32 variantLane =
#if TYRA_VU1_EXP_PAINT
        paintVu ? 2 :
#endif
        (sharedClipVariant ? 1 : (spotActive ? -1 : 0));
    {
      qword_t& q = opt[optQw++];
      // Single color enabled - or, Modified by TyraX, -1 for a TD bag: the
      // TC clip image carries TD's lighting path and picks it on .x < 0
      // (every reader of the lane tests `> 0` only; a TD bag is never drawn
      // single-colour). Gated on the class being resident so a TD bag walked
      // down to the TC program (residentFallback) is still drawn as TC.
      const bool textureDirLightsBag =
          bag->lighting != nullptr && bag->texture != nullptr &&
          !bag->texture->coordinatesAreNormals && bag->billboard == nullptr &&
          (residentClasses & StaPipClassTextureDirLights) != 0;
      q.sw[0] = textureDirLightsBag ? static_cast<u32>(-1)
                                    : static_cast<u32>(singleColorEnabled);
      q.sw[1] = static_cast<u32>(variantLane);
      // Modified by TyraX: GS hardware fog params (see RendererCoreFog)
      float fogScale = rendererCore->fog.scale;
      float fogOffset = rendererCore->fog.offset;
      // Modified by TyraX: fog scale 0 is the "F is the constant 255" signal
      // cull_tc branches on (setFogConstant), read as the LOW 16 BITS of the
      // float. A real, non-zero scale whose low half happens to be zero gets
      // its lowest mantissa bit set: one ulp, invisible, never mistaken.
      // Decided only where it can pay: a textured, unlit, non-env bag with no
      // spot light reaching it is the one cull_tc runs through its fog-free
      // loop. The box test is the linear maximum of clip w over the box: w
      // at the centre plus |d w / d axis| times the half-extent, three
      // multiplies instead of eight matrix transforms (the transforms cost
      // the EE more than VU1 saved in a spot-lit night scene).
      bool fogConstant = fogOffForBag;
#if TYRA_STAPIP_STAMP
      // The fog words are re-derived per stamp replay from the live MVP and
      // fog parameters; record only the bag-static half of the predicate.
      stampFogEligible = !fogOffForBag && fogBox != nullptr && !spotActive &&
                         bag->lighting == nullptr && bag->texture != nullptr &&
                         !bag->texture->coordinatesAreNormals &&
                         bag->billboard == nullptr;
      stampFogOff = fogOffForBag ? 1 : 0;
#endif
      if (!fogConstant && fogBox != nullptr && fogScale < 0.0F && !spotActive &&
          bag->lighting == nullptr && bag->texture != nullptr &&
          !bag->texture->coordinatesAreNormals && bag->billboard == nullptr) {
        const Vec4& lo = (*fogBox)[0];
        const Vec4& hi = (*fogBox)[7];
        const float* m = mvp->data;
        const float cx = 0.5F * (lo.x + hi.x), hx = 0.5F * (hi.x - lo.x);
        const float cy = 0.5F * (lo.y + hi.y), hy = 0.5F * (hi.y - lo.y);
        const float cz = 0.5F * (lo.z + hi.z), hz = 0.5F * (hi.z - lo.z);
        const float maxW = m[3] * cx + m[7] * cy + m[11] * cz + m[15] +
                           fabsf(m[3]) * hx + fabsf(m[7]) * hy +
                           fabsf(m[11]) * hz;
        fogConstant = maxW * fogScale + fogOffset >= 255.0F;
      }
      if (fogConstant) {
        fogScale = 0.0F;
        fogOffset = 255.0F;
      } else {
        u32 bits;
        memcpy(&bits, &fogScale, 4);
        if ((bits & 0xFFFFU) == 0U && bits != 0U) {
          bits |= 1U;
          memcpy(&fogScale, &bits, 4);
        }
      }
      memcpy(&q.sw[2], &fogScale, 4);
      memcpy(&q.sw[3], &fogOffset, 4);
    }
    {
      qword_t& q = opt[optQw++];
      // Modified by TyraX: a texture bag's own filter wins over the info
      // bag's (StaPipTextureBag::filter) - per bag, so one info bag can
      // carry smooth and point-sampled textures side by side.
      int magFilter = lod->mag_filter, minFilter = lod->min_filter;
      if (bag->texture && bag->texture->filter != 0) {
        magFilter = LOD_MAG_NEAREST;
        minFilter = bag->texture->filter == 2 ? LOD_MIN_LINEAR : LOD_MIN_NEAREST;
      }
      q.dw[0] = GS_SET_TEX1(lod->calculation, lod->max_level, magFilter,
                            minFilter, lod->mipmap_select, lod->l,
                            (int)(lod->k * 16.0F));
      q.dw[1] = GS_REG_TEX1;
    }

    // Modified by TyraX: the destination-alpha gate (PipelineInfoBag::
    // dateLit) rides the same in-band TEST qword every mesh already emits -
    // DATE = 1 draws this bag's pixels only where the framebuffer alpha's
    // MSB is 0, which is how the flashlight's shadow volumes mask its light.
    const int date = bag->info->dateLit ? 1 : 0;
    {
      qword_t& q = opt[optQw++];
      if (bag->info->zTestType == PipelineZTest_AllPass) {
        q.dw[0] = GS_SET_TEST(0, 0, 0, 0, date, 0, 0, ZTEST_METHOD_ALLPASS);
      } else if (bag->info->zTestType == PipelineZTest_TestOnly) {
        // Depth-tested, no z write: alpha test fails every pixel and AFAIL
        // keeps the z buffer (GS FB_ONLY - color still written). The ZBUF
        // register (and thus the VU1 options layout) stays untouched.
        q.dw[0] = GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_ALLFAIL, 0x00,
                              ATEST_KEEP_ZBUFFER, date, DRAW_DISABLE,
                              DRAW_ENABLE, rendererCore->gs.zBuffer.method);
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
        q.dw[0] = GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_NOTEQUAL, 0x00,
                              ATEST_KEEP_ALL, date, DRAW_DISABLE, DRAW_ENABLE,
                              rendererCore->gs.zBuffer.method);
      }
      q.dw[1] = GS_REG_TEST;
    }

    if (texBuffers != nullptr) {
      rendererCore->texture.updateClutBuffer(texBuffers->clut);

      // Added by TyraX: per-bag GS texture function (StaPipTextureBag - the
      // NFS paint pass draws HIGHLIGHT2). TEX0 is emitted per bag right
      // here, so writing the shared texbuffer_t immediately before the emit
      // gives every bag its own TFX - the next bag on the same texture
      // overwrites it again, which is exactly why the mutation is safe.
      texBuffers->core->info.function = bag->texture->textureFunction;
      const texbuffer_t* tb = texBuffers->core;
      const clutbuffer_t* cl = &rendererCore->texture.clut;
      qword_t& q = opt[optQw++];
      q.dw[0] = GS_SET_TEX0(tb->address >> 6, tb->width >> 6, tb->psm,
                            tb->info.width, tb->info.height,
                            tb->info.components, tb->info.function,
                            cl->address >> 6, cl->psm, cl->storage_mode,
                            cl->start, cl->load_method);
      q.dw[1] = GS_REG_TEX0;
    }
#if TYRA_STAPIP_STAMP
    stampOptAt = static_cast<u16>(objectDataPacket->next - stampHead);
    stampHasTex0 = texBuffers != nullptr ? 1 : 0;
    stampTex0At = static_cast<u16>(stampOptAt + optQw);  // TEX0 is last
#endif
    emitUnpack(objectDataPacket, VU1_OPTIONS_ADDR, opt, optQw);
  }

  // Modified by TyraX: particle billboard camera basis (right, up - world
  // space). Reuses the lights-matrix area like the env basis; billboard
  // bags never carry lighting (asserted in StaPipCore::render). Swapping
  // this basis and re-rendering the same bag draws the same centers for
  // another view (e.g. a portal's virtual camera).
  if (bag->billboard != nullptr) {
#if TYRA_STAPIP_STAMP
    stampOk = false;
#endif
    {
      alignas(16) const float bb[8] = {
          bag->billboard->right.x, bag->billboard->right.y,
          bag->billboard->right.z, 0.0F,
          bag->billboard->up.x,    bag->billboard->up.y,
          bag->billboard->up.z,    0.0F};
      emitUnpack(objectDataPacket, VU1_BILLBOARD_BASIS_ADDR, bb, 2);
    }
  }

  // Modified by TyraX: env (matcap) camera basis for the TCE programs.
  // Transpose it and fold in the ST scale here so CalculateTyraEnvStq can
  // evaluate both scaled dot products in one VU1 accumulator chain. Reuses
  // the lights-matrix area; env bags never carry lighting.
  if (bag->texture != nullptr && bag->texture->coordinatesAreNormals) {
#if TYRA_STAPIP_STAMP
    // Camera-dependent, so a stamp re-writes it on replay (envAt) - it used
    // to keep every env pass off the stamp route, ~2.9 ms of EE a frame on
    // Showcase (docs/stapip-stamps.md, "Env bags"). The VU1 paint arm uploads
    // live paint vectors too and stays excluded.
#if TYRA_VU1_EXP_PAINT
    if (paintVu) stampOk = false;
#endif
    stampEnvAt = static_cast<u16>(objectDataPacket->next - stampHead);
#endif
    const Vec4& r = bag->texture->envRight;
    const Vec4& u = bag->texture->envUp;
    {
      alignas(16) const float e[12] = {r.x * 0.5F, u.x * -0.5F, 0.0F, 0.0F,
                                       r.y * 0.5F, u.y * -0.5F, 0.0F, 0.0F,
                                       r.z * 0.5F, u.z * -0.5F, 1.0F, 0.5F};
      emitUnpack(objectDataPacket, VU1_ENV_BASIS_ADDR, e, 3);
#if TYRA_VU1_EXP_PAINT
      if (paintVu) {
        const Vec4& f = bag->texture->paintForward;
        const Vec4& h = bag->texture->paintHalf;
        alignas(16) const float p[8] = {f.x, f.y, f.z, 0.0F,
                                      h.x, h.y, h.z, 0.0F};
        emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, p, 2);
      }
#endif
    }
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
    qword_t a;
    a.dw[0] = sub != 0   ? GS_SET_ALPHA(2, 0, 2, 1, sub)
              : fix != 0 ? GS_SET_ALPHA(0, 2, 2, 1, fix)
                         : GS_SET_ALPHA(0, 1, 0, 1, 0);
    a.dw[1] = GS_REG_ALPHA;
    emitUnpack(objectDataPacket, VU1_ALPHA_ADDR, &a, 1);
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
    frameWaitVif();
    HardwareTrace::state("VIF1_WAIT_END"); }
  frameSendPacket(staticDataPacket, DMA_CHANNEL_VIF1, true);
}

// Modified by TyraX: micro-memory words a program cache of these programs
// occupies - Path1::createProgramsCache's own arithmetic (back to back, each
// rounded to an even count, an image shared by two wrappers counted once).
static u32 programSetWords(VU1Program* const* programs, const u32& count) {
  u32 words = 0;
  for (u32 i = 0; i < count; i++) {
    bool shared = false;
    for (u32 j = 0; j < i && !shared; j++)
      shared = programs[i]->getStart() == programs[j]->getStart() &&
               programs[i]->getEnd() == programs[j]->getEnd();
    if (!shared) words += programs[i]->getProgramSize();
  }
  return words;
}

void StaPipQBufferRenderer::setProgramsCache() {
  DrawEpochs::programsChanged();  // Modified by TyraX: stamp records
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
  VU1Program* programs[13];  // ten class programs, two billboards, one decoder
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
  // Modified by TyraX: the billboard family joins the resident set whenever it
  // fits under the draw-finish helper - with clip TD sharing the TC image the
  // all-class VU1-clipping set is ~1740 of 2042 words and the pair is 206, so
  // that is every built-in configuration. Resident, ensureProgramSet never
  // swaps: a swap was two VIF1 drains plus a ~15.5 KB MPG upload per
  // billboard/non-billboard transition, several per frame in a scene with
  // particles. Only a set that a project's own programs have grown past the
  // ceiling falls back to the old separate packet, swapped in on demand.
  // Decided on every rebuild, because overrides and setResidentClasses change
  // the set's size at run time.
  VU1Program* billboardPrograms[2] = {
      repository.getProgram(StaPipBillboardColor),
      repository.getProgram(StaPipBillboardTexture)};
  billboardsResident = programSetWords(programs, count) +
                           programSetWords(billboardPrograms, 2) <=
                       path1->getDrawFinishAddr();
  if (billboardsResident) {
    programs[count++] = billboardPrograms[0];
    programs[count++] = billboardPrograms[1];
  }
  auto& decoder = StaPipVU1Program::quantizedDecoder();
  quantizedDecoderResident = vu1Clipping &&
      programSetWords(programs, count) + decoder.getProgramSize() <= path1->getDrawFinishAddr();
  if (quantizedDecoderResident) programs[count++] = &decoder;
  programsPacket = path1->createProgramsCache(programs, count, 0);

  // createProgramsCache assigns each program's micro-memory address, so the
  // fallback packet is rebuilt AFTER the main one: its programs must point at
  // address 0 (where the swap puts them), not at wherever an earlier resident
  // build had parked them.
  if (billboardProgramsPacket != nullptr) {
    packet2_free(billboardProgramsPacket);
    billboardProgramsPacket = nullptr;
  }
  if (!billboardsResident)
    billboardProgramsPacket =
        path1->createProgramsCache(billboardPrograms, 2, 0);
  TYRA_LOG("StaPip VU1 program set: ", count, " programs, billboards ",
           billboardsResident ? "resident" : "swapped on demand");
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
  DrawEpochs::globalsChanged();  // Modified by TyraX: stamp records
  if (programsPacket == nullptr) return;  // init() will build the right set

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

// TyraX addition: experiment (b) folds the light matrix on the EE only for the
// engine's own lit programs - see sendObjectData.
bool StaPipQBufferRenderer::litProgramOverridden() const {
  return repository.hasOverride(StaPipCullDirLights) ||
         repository.hasOverride(StaPipCullTextureDirLights) ||
         repository.hasOverride(StaPipClipDirLights) ||
         repository.hasOverride(StaPipClipTextureDirLights) ||
         repository.hasOverride(StaPipAsIsDirLights) ||
         repository.hasOverride(StaPipAsIsTextureDirLights);
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
  DrawEpochs::globalsChanged();  // Modified by TyraX: stamp records
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
  DrawEpochs::globalsChanged();  // Modified by TyraX: stamp records
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
  DrawEpochs::globalsChanged();  // Modified by TyraX: stamp records
#if TYRA_STAPIP_RETAINED_COMMANDS
  // Modified by TyraX: the clip chain is either present or absent per mode,
  // and every bag's cull program changes with it.
  clipBlockQw[0] = clipBlockQw[1] = 0;
  clipBlockVifQw[0] = clipBlockVifQw[1] = 0;
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
  DrawEpochs::programsChanged();  // Modified by TyraX: stamp records
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    frameWaitVif();
    HardwareTrace::state("VIF1_WAIT_END"); }
  frameSendPacket(programsPacket, DMA_CHANNEL_VIF1, true);
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    frameWaitVif();
    HardwareTrace::state("VIF1_WAIT_END"); }
  billboardSetActive = false;  // Modified by TyraX: main set is resident now
}

// Modified by TyraX: swap between the resident program set and the
// billboard set (both packets are prebuilt - this is one VIF1 MPG upload,
// the VIF stalls it until VU1 halts, so it is safe mid-frame).
void StaPipQBufferRenderer::ensureProgramSet(const bool& billboard) {
  // Modified by TyraX: resident billboards (see setProgramsCache) never swap.
  if (billboardsResident || billboardSetActive == billboard) return;
  flushPendingPacket();
  billboardSetActive = billboard;

  const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;

  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    frameWaitVif();
    HardwareTrace::state("VIF1_WAIT_END"); }
  frameSendPacket(
      billboard ? billboardProgramsPacket : programsPacket, DMA_CHANNEL_VIF1,
      true);
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    frameWaitVif();
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
  result->nativePacked = false;
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
    // Modified by TyraX: a batched PARTIAL bag (StaPipCore::setPartialBatch)
    // may not stay pending in two cases. (1) It copied into the slot pools
    // (strip expansion, merged subpackages): the next bag reuses those slots
    // on the same pool side, which only a send flips. (2) A half flush sent
    // the packet mid-bag: the uniforms are gone with it, so objectDataPending
    // is false and nothing would ever send what is appended now. Either way
    // the packet goes out at the end of this bag, as an unbatched one does.
    const bool keepPending =
        submissionBatchCandidate && objectDataPending &&
        !StaPipQBuffer::poolTouched();
    addBuffersDataToPacket(offset, offset + size, !keepPending);
    if (!keepPending) {
      sendPacket();
      submissionBatchBags = 0;
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
                                             const u32& packageSize,
                                             bool nativePacked) {
  retainedCurrent = nullptr;
  if (bag == nullptr || packageSize == 0) return false;
  // A billboard bag expands centres on VU1 from a basis that changes every
  // frame, and it swaps the whole program set; a game-supplied writer has no
  // packet-size ABI and may not be position-independent. Both are excluded for
  // the same reason the submission-batch scope excludes them.
  if (bag->billboard != nullptr || repository.hasAnyOverride()) return false;

  StaPipRetainedEntry key;
  key.vertices = nativePacked ? static_cast<const void*>(bag->packedMesh)
                              : bag->vertices;
  key.sts = !nativePacked && bag->texture ? bag->texture->coordinates : nullptr;
  key.colors = nativePacked ? nullptr : bag->color->many;
  key.normals = bag->lighting != nullptr ? bag->lighting->normals : nullptr;
  key.program = getCullProgramByBag(bag);
  key.count = bag->count;
  key.maxVertCount = packageSize;
  // A regenerated mesh can reuse the old object's address and DMA spans.
  key.bboxVersion = nativePacked ? bag->packedMesh->getId() : bag->bboxVersion;
  key.programAddress = nativePacked ? getCullProgramByBag(bag)->getDestinationAddress() : 0;
  key.slotQw = nativePacked ? StaPipRetainedCommands::kPackedBlockQw
                            : StaPipRetainedCommands::kMaxBlockQw;
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
  memcpy(&key.screenScaleBits[0], &RendererCoreDepth::screenScaleX, sizeof(u32));
  memcpy(&key.screenScaleBits[1], &RendererCoreDepth::screenScaleY, sizeof(u32));
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

bool StaPipQBufferRenderer::beginRetainedBag(StaPipBag*, const u32&, bool) {
  return false;
}
void StaPipQBufferRenderer::endRetainedBag() {}
u32 StaPipQBufferRenderer::takeRetainedHits() { return 0; }
u32 StaPipQBufferRenderer::takeRetainedBuilds() { return 0; }
u32 StaPipQBufferRenderer::getRetainedBytes() const { return 0; }

#endif  // TYRA_STAPIP_RETAINED_COMMANDS

// Modified by TyraX: the BAKED VIF STREAM spike - docs/baked-vif-stream.md.
#if TYRA_STAPIP_BAKED_STREAM

/** Read an optional content stamp. A null pointer means the owner does not
 * track its array's contents, and contributes nothing to the key - which is
 * how every caller that has not opted in keeps its previous behaviour. */
static inline u32 stampOf(const u32* p) { return p != nullptr ? *p : 0u; }

bool StaPipQBufferRenderer::fillCachedStrip(StaPipQBuffer* buffer,
    const StaPipBagPackage& pkg, u32 first, u32 triangles) {
  auto* bag = pkg.bag;
  if (!vu1Clipping || !triangles || first + triangles + 2 > pkg.size ||
      bag->billboard || repository.hasAnyOverride()) return false;
  const u32 count = triangles * 3;
  const u32 expandedCount = (pkg.size - 2) * 3;
  const u32 streams = 1 + unsigned(bag->texture != nullptr) +
      unsigned(bag->color->many != nullptr) + unsigned(bag->lighting != nullptr);
  const u32 qw = expandedCount * streams;
  if (qw > 4096) return false;
  StaPipBakedEntry key{};
  key.vertices = pkg.vertices;
  key.sts = pkg.sts;
  key.colors = pkg.colors;
  key.normals = pkg.normals;
  key.program = getClipProgramByBag(bag);
  key.count = expandedCount;
  // Disjoint from ordinary VU package sizes: this entry stores arrays, not VIF.
  key.maxVertCount = 0x80000000u | pkg.size;
  key.bboxVersion = bag->bboxVersion;
  key.contentVersion = stampOf(bag->contentVersion) * 0x9E3779B1U +
      stampOf(bag->color->contentVersion) * 0x85EBCA6BU +
      stampOf(bag->texture ? bag->texture->contentVersion : nullptr) * 0xC2B2AE35U +
      stampOf(bag->lighting ? bag->lighting->contentVersion : nullptr) * 0x27D4EB2FU;
  key.singleColor = bag->color->many == nullptr;
  key.packages = 1;
  auto* entry = baked.acquire(key);
  if (!entry) return false;
  if (!entry->complete) {
    entry->raw = baked.allocateArena(qw);
    if (!entry->raw) return false;
    const u32 aligned = (reinterpret_cast<u32>(entry->raw.get()) + 15u) & ~15u;
    entry->arena = reinterpret_cast<qword_t*>(aligned);
    // One registry slot per original strip, not per tiny clip chunk. Never
    // publish a copy-only entry after registry saturation: it would stay so.
    if (!Vif1Queue::registerImmutableSpan(entry->arena, qw)) {
      baked.discardUnpublishedArena(qw);
      entry->raw.reset();
      entry->arena = nullptr;
      return false;
    }
    for (u32 t = 0; t < pkg.size - 2u; t += triangles) {
      const u32 n = std::min(triangles, pkg.size - 2u - t);
      buffer->fillByStripExpand(pkg, t, n);
      Vec4* out = reinterpret_cast<Vec4*>(entry->arena) + t * 3;
      const Vec4* source[] = {buffer->vertices,
          bag->texture ? buffer->sts : nullptr,
          bag->color->many ? buffer->colors : nullptr,
          bag->lighting ? buffer->normals : nullptr};
      for (const auto* data : source) if (data) {
        memcpy(out, data, n * 3 * sizeof(Vec4));
        out += expandedCount;
      }
    }
    entry->arenaQw = qw;
    entry->offsets[0] = 0;
    entry->sizes[0] = qw;
    entry->built = entry->complete = 1;
  }
  StaPipBagPackage cached = pkg;
  const Vec4* in = reinterpret_cast<const Vec4*>(entry->arena) + first * 3;
  cached.size = count;
  cached.vertices = in; in += expandedCount;
  cached.sts = bag->texture ? in : nullptr;
  if (bag->texture) in += expandedCount;
  cached.colors = bag->color->many ? in : nullptr;
  if (bag->color->many) in += expandedCount;
  cached.normals = bag->lighting ? in : nullptr;
#if TYRA_STAPIP_EXPANSION_VERIFY
  // Compare every stream to the original expansion, including odd winding.
  buffer->fillByStripExpand(pkg, first, triangles);
  bool equal = memcmp(buffer->vertices, cached.vertices, count * sizeof(Vec4)) == 0;
  if (cached.sts) equal &= memcmp(buffer->sts, cached.sts, count * sizeof(Vec4)) == 0;
  if (cached.colors) equal &= memcmp(buffer->colors, cached.colors, count * sizeof(Vec4)) == 0;
  if (cached.normals) equal &= memcmp(buffer->normals, cached.normals, count * sizeof(Vec4)) == 0;
  static u32 checked = 0, failed = 0;
  ++checked; if (!equal) ++failed;
  if (!equal || checked == 1 || checked % 4096 == 0) {
    printf("EXPANDCHECK checked=%u failed=%u\n", unsigned(checked), unsigned(failed));
    fflush(stdout);
  }
  if (!equal) return true;  // original expanded data is already in the buffer
#endif
  buffer->fillByPointer(cached);
  buffer->stripped = false;
#if TYRA_STAPIP_PREPARED_STATIC
  if (preparingStatic) entry->scenePinned = true;
#endif
  return true;
}

bool StaPipQBufferRenderer::fillCachedPackage(StaPipQBuffer* buffer,
                                              const StaPipBagPackage& pkg) {
  auto* bag = pkg.bag;
  // A bag whose streams are already registered is borrowed in place.
  if (!vu1Clipping || pkg.size < 3 || bag->billboard ||
      repository.hasAnyOverride() || bag->immutableRegistered) return false;
  const Vec4* source[] = {pkg.vertices, bag->texture ? pkg.sts : nullptr,
                          bag->color->many ? pkg.colors : nullptr,
                          bag->lighting ? pkg.normals : nullptr};
  u32 streams = 0;
  for (const auto* data : source) if (data) ++streams;
  const u32 qw = pkg.size * streams;
  if (qw > 4096) return false;
  StaPipBakedEntry key{};
  key.vertices = pkg.vertices;
  key.sts = source[1];
  key.colors = source[2];
  key.normals = source[3];
  key.program = getClipProgramByBag(bag);
  key.count = pkg.size;
  // Disjoint from VU package sizes and from the strip expansions.
  key.maxVertCount = 0xC0000000u | pkg.size;
  key.bboxVersion = bag->bboxVersion;
  key.contentVersion = stampOf(bag->contentVersion) * 0x9E3779B1U +
      stampOf(bag->color->contentVersion) * 0x85EBCA6BU +
      stampOf(bag->texture ? bag->texture->contentVersion : nullptr) * 0xC2B2AE35U +
      stampOf(bag->lighting ? bag->lighting->contentVersion : nullptr) * 0x27D4EB2FU;
  key.singleColor = bag->color->many == nullptr;
  key.stripped = bag->stripped ? 1 : 0;
  key.packages = 1;
  auto* entry = baked.acquire(key);
  if (!entry) return false;
  if (!entry->complete) {
    entry->raw = baked.allocateArena(qw);
    if (!entry->raw) return false;
    const u32 aligned = (reinterpret_cast<u32>(entry->raw.get()) + 15u) & ~15u;
    entry->arena = reinterpret_cast<qword_t*>(aligned);
    if (!Vif1Queue::registerImmutableSpan(entry->arena, qw)) {
      baked.discardUnpublishedArena(qw);
      entry->raw.reset();
      entry->arena = nullptr;
      return false;
    }
    Vec4* out = reinterpret_cast<Vec4*>(entry->arena);
    for (const auto* data : source) if (data) {
      memcpy(out, data, pkg.size * sizeof(Vec4));
      out += pkg.size;
    }
    entry->arenaQw = qw;
    entry->offsets[0] = 0;
    entry->sizes[0] = qw;
    entry->built = entry->complete = 1;
  }
  StaPipBagPackage cached = pkg;
  const Vec4* in = reinterpret_cast<const Vec4*>(entry->arena);
  cached.vertices = in;
  in += pkg.size;
  if (source[1]) { cached.sts = in; in += pkg.size; }
  if (source[2]) { cached.colors = in; in += pkg.size; }
  if (source[3]) cached.normals = in;
  buffer->fillByPointer(cached);
  return true;
}

#if TYRA_STAPIP_PREPARED_STATIC
bool StaPipQBufferRenderer::prepareStaticGeometry(StaPipBag* bag,
    u32 packageSize, u32 clipSize) {
  if (!programsPacket || repository.hasAnyOverride() || !clipSize ||
      Vif1Queue::recordingFrame()) return false;
  const bool ready = beginBakedBag(bag, packageSize, true);
  if (ready) bakedCurrent->scenePinned = true;
  endBakedBag();
  if (!ready || !bag->stripped || !vu1Clipping) return ready;
  StaPipQBuffer scratch;
  scratch.setMaxVertCount(packageSize);
  preparingStatic = true;
  for (u32 offset = 0; offset < bag->count; offset += packageSize) {
    StaPipBagPackage pkg;
    pkg.bag = bag;
    pkg.size = std::min(packageSize, bag->count - offset);
    if (pkg.size < 3) continue;
    pkg.vertices = bag->vertices + offset;
    pkg.sts = bag->texture ? bag->texture->coordinates + offset : nullptr;
    pkg.colors = reinterpret_cast<const Vec4*>(bag->color->many) + offset;
    pkg.normals = nullptr;
    pkg.clipPlaneMask = 0x3f;
    // This creates no draw. Runtime classification supplies its fresh mask.
    fillCachedStrip(&scratch, pkg, 0, std::min(clipSize / 3, u32(pkg.size - 2)));
  }
  preparingStatic = false;
  return true;
}
#endif

// Modified by TyraX: see StaPipCore::prebakeBag. beginBakedBag's partial
// mode builds every block with the real writers when the entry is not
// complete (preparePartialBakedBag), and submits nothing.
bool StaPipQBufferRenderer::prebakeBag(StaPipBag* bag, u32 packageSize) {
  if (!programsPacket || repository.hasAnyOverride() ||
      Vif1Queue::recordingFrame())
    return false;
  const bool ready = beginBakedBag(bag, packageSize, true);
  endBakedBag();
  return ready;
}

bool StaPipQBufferRenderer::beginBakedBag(StaPipBag* bag,
                                          const u32& packageSize, bool partial) {
  bakedCurrent = nullptr;
  bakedPartial = partial;
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
  // Modified by TyraX: fold the four per-stream content stamps. A stream whose
  // owner does not opt in has a null pointer and contributes nothing, so an
  // un-migrated caller keeps exactly the behaviour it had. The mixing constants
  // are there so that two streams swapping equal stamps is not a no-op; a
  // plain sum or XOR would hide that.
  key.streamStamps[0] = stampOf(bag->contentVersion);
  key.streamStamps[1] = stampOf(bag->color->contentVersion);
  key.streamStamps[2] =
      stampOf(bag->texture != nullptr ? bag->texture->contentVersion : nullptr);
  key.streamStamps[3] =
      stampOf(bag->lighting != nullptr ? bag->lighting->contentVersion : nullptr);
  key.contentVersion = stampOf(bag->contentVersion) * 0x9E3779B1U +
                       stampOf(bag->color->contentVersion) * 0x85EBCA6BU +
                       stampOf(bag->texture != nullptr
                                   ? bag->texture->contentVersion
                                   : nullptr) *
                           0xC2B2AE35U +
                       stampOf(bag->lighting != nullptr
                                   ? bag->lighting->contentVersion
                                   : nullptr) *
                           0x27D4EB2FU;
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

  bakedCurrent = baked.acquire(key, bag->patch);
  // Modified by TyraX: a patch rebake acquire accepted is finished here, before
  // anything can replay or rebuild the entry (StaPipBagPatch).
  if (bakedCurrent != nullptr && bakedCurrent->patching)
    patchBakedBag(bag, packageSize);
  if (bakedCurrent != nullptr && bakedCurrent->refreshed) {
    bakedCurrent->refreshed = 0;
#if TYRA_STAPIP_PATCH_VERIFY
    verifyAgainstWriters(bag, packageSize, bakedCurrent, "colour", 0);
#endif
  }
#if TYRA_STAPIP_BAKED_ANY_VERIFY
  // Rewind a COMPLETE entry so the ordinary writers rebuild every block into
  // bakeScratch this frame; `complete` and the arena are left alone, and
  // endBakedBag compares rather than overwrites. The entry's offsets/sizes are
  // rewritten as a side effect, which costs nothing because a verified entry
  // is not replayed - a length difference still shows up in the comparison.
  //
  // Modified by TyraX: `verifying()` rather than the macro, so the sampled arm
  // rewinds only THIS FRAME'S victim and every other bag is replayed at full
  // speed. See StaPipBakedStreams::verifying.
  if (baked.verifying(bakedCurrent)) bakedCurrent->built = 0;
#endif
  if (partial && bakedCurrent && !preparePartialBakedBag(bag, packageSize)) {
    bakedCurrent = nullptr;
    return false;
  }
  return bakedCurrent != nullptr;
}

// Modified by TyraX: build immutable fixed slices with the real packet writers.
// No geometry is submitted here. Clipped/expanded slices still use the old path.
bool StaPipQBufferRenderer::preparePartialBakedBag(StaPipBag* bag, u32 size) {
  auto* entry = bakedCurrent;
  if (entry->complete && !baked.verifying(entry)) return true;
  if (static_cast<u64>(bag->count) * 4 + entry->packages * 16u >
      StaPipBakedStreams::kMaxQwords) return false;
  if (!partialBakePacket)
    partialBakePacket = packet2_create(64, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  if (!partialBakePacket) return false;
  StaPipQBuffer buffer;
  buffer.setMaxVertCount(size);
  auto* program = getCullProgramByBag(bag);
  const prim_t savedPrim = *prim;
  entry->built = 0;
  for (u32 i = 0, offset = 0; offset < bag->count; ++i, offset += size) {
    packet2_reset(partialBakePacket, false);
    buffer.fillByPointer(bag, offset, std::min(size, bag->count - offset));
    program->addBufferDataToPacket(partialBakePacket, &buffer, prim, i == 0);
    if (i == 0)
      packet2_utils_vu_add_start_program(partialBakePacket, program->getDestinationAddress());
    else packet2_utils_vu_add_continue_program(partialBakePacket);
    if (!bakeBlock(partialBakePacket, 0, packet2_get_qw_count(partialBakePacket), entry, i)) {
      entry->built = 0;
      bakeScratch.clear();
      *prim = savedPrim;
      return false;
    }
  }
  *prim = savedPrim;
  endBakedBag();
  bakedCurrent = entry;
  return entry->complete;
}

// Modified by TyraX: patch rebake (StaPipBagPatch). The same writers as
// preparePartialBakedBag, for the packages holding a dirty vertex only; every
// other block is copied out of the old arena, which is still allocated - it
// leaves for the graveyard (never free()) once the copy is done, because a
// packet of an earlier frame may still name it.
bool StaPipQBufferRenderer::patchBakedBag(StaPipBag* bag, u32 size) {
  StaPipBakedEntry* entry = bakedCurrent;
  const StaPipBagPatch* patch = bag->patch;
  entry->patching = 0;
  bool ok = patch != nullptr && patch->dirty != nullptr &&
            entry->patchArena != nullptr && size == entry->maxVertCount &&
            entry->patchOffsets.size() == entry->packages;
  if (ok && !partialBakePacket)
    partialBakePacket = packet2_create(64, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  StaPipVU1Program* program = ok ? getCullProgramByBag(bag) : nullptr;
  ok = ok && partialBakePacket != nullptr && program != nullptr;
  bakeScratch.clear();
  patchFromScratch.assign(entry->packages, 0);
  u32 rebuilt = 0;
  if (ok) {
    StaPipQBuffer buffer;
    buffer.setMaxVertCount(size);
    const prim_t savedPrim = *prim;
    for (u32 i = 0, offset = 0; ok && i < entry->packages; ++i, offset += size) {
      const u32 n = std::min(size, bag->count - offset);
      bool dirty = false;
      for (u32 k = 0; k < n && !dirty; ++k) dirty = patch->dirty[offset + k] != 0;
      if (!dirty) continue;
      packet2_reset(partialBakePacket, false);
      buffer.fillByPointer(bag, offset, n);
      program->addBufferDataToPacket(partialBakePacket, &buffer, prim, i == 0);
      if (i == 0)
        packet2_utils_vu_add_start_program(partialBakePacket,
                                           program->getDestinationAddress());
      else
        packet2_utils_vu_add_continue_program(partialBakePacket);
      ok = bakeBlock(partialBakePacket, 0, packet2_get_qw_count(partialBakePacket),
                     entry, i);
      patchFromScratch[i] = 1;
      ++rebuilt;
    }
    *prim = savedPrim;
  }
  u32 total = 0;
  for (u32 i = 0; ok && i < entry->packages; ++i)
    total += patchFromScratch[i] ? entry->sizes[i] : entry->patchSizes[i];
  if (ok && total > 0) entry->raw = baked.allocateArena(total);
  ok = entry->raw != nullptr;
  if (ok) {
    const u32 aligned = (reinterpret_cast<u32>(entry->raw.get()) + 15u) & ~15u;
    entry->arena = reinterpret_cast<qword_t*>(aligned);
    u32 at = 0;
    for (u32 i = 0; i < entry->packages; ++i) {
      const bool fresh = patchFromScratch[i] != 0;
      const qword_t* src = fresh ? bakeScratch.data() + entry->offsets[i]
                                 : entry->patchArena + entry->patchOffsets[i];
      const u32 qw = fresh ? entry->sizes[i] : entry->patchSizes[i];
      memcpy(entry->arena + at, src, qw * 16);
      entry->offsets[i] = at;
      entry->sizes[i] = static_cast<u16>(qw);
      at += qw;
    }
    entry->arenaQw = total;
    Vif1Queue::registerImmutableSpan(entry->arena, total);
    entry->built = entry->packages;
    entry->complete = 1;
    baked.patchBuilt += rebuilt;
    baked.patchCopied += entry->packages - rebuilt;
#if TYRA_STAPIP_PATCH_VERIFY
    verifyAgainstWriters(bag, size, entry, "patch", rebuilt);
#endif
  } else {
    // The ordinary rebuild takes over: an empty entry, as after a miss.
    entry->built = 0;
    entry->complete = 0;
  }
  baked.retireArena(std::move(entry->patchRaw), entry->patchArena, entry->patchQw);
  entry->patchArena = nullptr;
  entry->patchQw = 0;
  entry->patchOffsets.clear();
  entry->patchSizes.clear();
  bakeScratch.clear();
  return ok;
}

#if TYRA_STAPIP_PATCH_VERIFY
void StaPipQBufferRenderer::verifyAgainstWriters(StaPipBag* bag, u32 size,
                                                 StaPipBakedEntry* entry,
                                                 const char* what, u32 rebuilt) {
  static u32 checked = 0, failed = 0;
  if (!partialBakePacket)
    partialBakePacket = packet2_create(64, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  StaPipVU1Program* program = getCullProgramByBag(bag);
  if (!partialBakePacket || !program) return;
  std::vector<qword_t> saved;
  saved.swap(bakeScratch);
  StaPipQBuffer buffer;
  buffer.setMaxVertCount(size);
  const prim_t savedPrim = *prim;
  bool built = true;
  StaPipBakedEntry probe;
  probe.colors = nullptr;
  probe.runsValid = 1;
  probe.offsets = std::unique_ptr<u32[]>(new u32[entry->packages]);
  probe.sizes = std::unique_ptr<u16[]>(new u16[entry->packages]);
  for (u32 i = 0, offset = 0; built && i < entry->packages; ++i, offset += size) {
    packet2_reset(partialBakePacket, false);
    buffer.fillByPointer(bag, offset, std::min(size, bag->count - offset));
    program->addBufferDataToPacket(partialBakePacket, &buffer, prim, i == 0);
    if (i == 0)
      packet2_utils_vu_add_start_program(partialBakePacket,
                                         program->getDestinationAddress());
    else
      packet2_utils_vu_add_continue_program(partialBakePacket);
    built = bakeBlock(partialBakePacket, 0, packet2_get_qw_count(partialBakePacket),
                      &probe, i);
  }
  *prim = savedPrim;
  const u32 total = entry->arenaQw;
  ++checked;
  const bool same = built && bakeScratch.size() == total &&
                    memcmp(bakeScratch.data(), entry->arena, total * 16) == 0;
  if (!same) ++failed;
  u32 firstDiff = 0;
  if (!same && built && bakeScratch.size() == total) {
    const u32* a = reinterpret_cast<const u32*>(entry->arena);
    const u32* b = reinterpret_cast<const u32*>(bakeScratch.data());
    while (firstDiff < total * 4 && a[firstDiff] == b[firstDiff]) ++firstDiff;
  }
  TYRA_LOG("PATCHVERIFY ", what, " checked=", checked, " failed=", failed,
           same ? " ok" : " MISMATCH", " packages=", entry->packages,
           " rebuilt=", rebuilt, " qw=", total, " freshQw=",
           static_cast<u32>(bakeScratch.size()), " firstDiffQw=", firstDiff / 4);
  bakeScratch.swap(saved);
}
#endif

bool StaPipQBufferRenderer::replayPartialBakedBuffer(packet2_t* packet,
    StaPipQBuffer* buffer, StaPipVU1Program* program, bool emitState) {
  auto* entry = bakedCurrent;
  const int index = buffer->bakeIndex;
  if (!bakedPartial || !entry || !entry->complete || baked.verifying(entry) ||
      index < 0 || index >= entry->packages || entry->sizes[index] <= 4 ||
      program != entry->program) return false;
  const auto* block = entry->arena + entry->offsets[index];
  const u32 size = entry->sizes[index];
#if TYRA_STAPIP_PARTIAL_BAKED_VERIFY
  if (!partialBakePacket)
    partialBakePacket = packet2_create(64, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  packet2_reset(partialBakePacket, false);
  program->addBufferDataToPacket(partialBakePacket, buffer, prim, emitState);
  if (emitState) packet2_utils_vu_add_start_program(partialBakePacket, program->getDestinationAddress());
  else packet2_utils_vu_add_continue_program(partialBakePacket);
  const bool equal = BakedBody::matches(
      reinterpret_cast<const uint32_t*>(partialBakePacket->base),
      packet2_get_qw_count(partialBakePacket), reinterpret_cast<const uint32_t*>(block),
      size, VU1_STAPIP_EMIT_STATE_FLAG,
      [](uint32_t address, uint32_t) { return reinterpret_cast<const uint32_t*>(address); });
  static u32 checked = 0, failed = 0;
  ++checked;
  if (!equal) ++failed;
  if (!equal || checked == 1 || checked % 4096 == 0) {
    printf("PARTIALCHECK checked=%u failed=%u index=%d words=%u\n",
           unsigned(checked), unsigned(failed), index, unsigned(size));
    fflush(stdout);
  }
  if (!equal) { bakedCurrent = nullptr; return false; }
#endif
  // The standard writer emits a three-qword CNT block; the kick is one CNT.
  // Both are regenerated, since culling/clip interleaving changes entry state.
  const u32 before = packet2_get_qw_count(packet);
  program->addStandardBufferDataToPacket(packet, buffer, prim, emitState);
  if (packet2_get_qw_count(packet) - before != 3) {
    packet->next = packet->base + before;
    return false;
  }
  emitRef(packet, block + 3, size - 4);
  if (emitState) packet2_utils_vu_add_start_program(packet, program->getDestinationAddress());
  else packet2_utils_vu_add_continue_program(packet);
  lastProgramName = program->getName();
  baked.countHit();
  return true;
}

// Modified by TyraX (R5): see the header. The clip variant is built with the
// real writers, like preparePartialBakedBag, with the plane mask fixed at all
// six planes - the one input of a clip block that depends on the camera.
bool StaPipQBufferRenderer::beginPartialReplay(StaPipBag* bag, u32 size) {
  bakedClip = nullptr;
  StaPipBakedEntry* cull = bakedCurrent;
  if (!vu1Clipping || cull == nullptr || !cull->complete || !bakedPartial ||
      baked.verifying(cull) || bag->billboard != nullptr ||
      repository.hasAnyOverride() || size == 0)
    return false;
  // A strip package under three vertices draws nothing and the routed loop
  // skips it; the clip program must never be handed one.
  if (bag->stripped) {
    const u32 tail = bag->count % size;
    if (tail == 1 || tail == 2) return false;
  }
  if (static_cast<u64>(bag->count) * 4 + cull->packages * 16u >
      StaPipBakedStreams::kMaxQwords)
    return false;
  StaPipVU1Program* program = getClipProgramByBag(bag);
  if (program == nullptr) return false;

  StaPipBakedEntry key;
  key.vertices = cull->vertices;
  key.sts = cull->sts;
  key.colors = cull->colors;
  key.normals = cull->normals;
  key.program = program;
  key.count = cull->count;
  key.maxVertCount = cull->maxVertCount;
  key.bboxVersion = cull->bboxVersion;
  key.contentVersion = cull->contentVersion;
  key.primKey = cull->primKey;
  key.depthScaleBits = cull->depthScaleBits;
  key.programAddr = program->getDestinationAddress();
  key.singleColor = cull->singleColor;
  key.stripped = cull->stripped;
  key.packages = cull->packages;
  StaPipBakedEntry* clip = baked.acquire(key);
  if (clip == nullptr) return false;
  if (!clip->complete) {
    if (!partialBakePacket)
      partialBakePacket =
          packet2_create(64, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
    if (!partialBakePacket) return false;
    StaPipQBuffer buffer;
    buffer.setMaxVertCount(size);
    const prim_t savedPrim = *prim;
    bakeScratch.clear();
    clip->built = 0;
    bool ok = true;
    for (u32 i = 0, offset = 0; ok && offset < bag->count;
         ++i, offset += size) {
      packet2_reset(partialBakePacket, false);
      buffer.fillByPointer(bag, offset, std::min(size, bag->count - offset));
      buffer.clipPlaneMask = 0x3F;
      program->addBufferDataToPacket(partialBakePacket, &buffer, prim, i == 0);
      if (i == 0)
        packet2_utils_vu_add_start_program(partialBakePacket,
                                           program->getDestinationAddress());
      else
        packet2_utils_vu_add_continue_program(partialBakePacket);
      ok = bakeBlock(partialBakePacket, 0,
                     packet2_get_qw_count(partialBakePacket), clip, i);
    }
    *prim = savedPrim;
    if (!ok) {
      clip->built = 0;
      bakeScratch.clear();
      return false;
    }
    bakedCurrent = clip;
    endBakedBag();  // hands the scratch to the entry's arena
    bakedCurrent = cull;
    if (!clip->complete) return false;
  }
  bakedClip = clip;
  return true;
}

// The bag's uniforms are in the packet (sendObjectData), the qbuffer ring is
// empty; this is replayWholeBakedBag's tail for a bag that took no qbuffer.
void StaPipQBufferRenderer::finishPartialReplay() {
  objectDataPending = true;
  ++submissionBatchBags;
  if (!submissionBatchCandidate ||
      submissionBatchBags >= kSubmissionBatchSize)
    flushPendingPacket();
}

namespace {
// The clip header's plane mask is stored bit-reversed (stapip_vu1_program.cpp).
inline u32 reversedPlaneMask(u8 mask) {
  if (mask == 0) mask = 0x3F;
  u32 r = 0;
  for (u32 i = 0; i < 6; ++i)
    if (mask & (1u << i)) r |= 1u << (5 - i);
  return r;
}
}  // namespace

bool StaPipQBufferRenderer::emitPartialRuns(const u8* routes, u32 count,
                                            const u8* clipMasks) {
  StaPipBakedEntry* entries[3] = {nullptr, bakedCurrent, bakedClip};
  if (entries[1] == nullptr || entries[2] == nullptr || count == 0 ||
      count > entries[1]->packages || count > entries[2]->packages ||
      entries[1]->arenaQw > 0xFFFFu || entries[2]->arenaQw > 0xFFFFu)
    return false;
  // Price it first: a block whose header is regenerated costs 5 qwords
  // (header, body REF, kick), a run of baked blocks one REF.
  auto* packet = packets[context];
  u32 need = 0;
  {
    u8 last = 0;
    for (u32 i = 0; i < count;) {
      const u8 r = routes[i];
      if (r == 0) { ++i; continue; }
      u32 j = i + 1;
      while (j < count && routes[j] == r) ++j;
      if (r == 2 && clipMasks != nullptr) need += 5 * (j - i);
      else need += (r != last && i > 0) ? 6 : 1;
      last = r;
      i = j;
    }
  }
  if (packet2_get_qw_count(packet) + need + 4 > packetSize) return false;
  StaPipProgramName lastName = lastProgramName;
  // One block with a regenerated header: the GS-state flag and MSCAL when the
  // program changes here, the live plane mask for a clip block.
  auto emitBlock = [&](StaPipBakedEntry* e, StaPipVU1Program* program, u32 k,
                       bool switched, int mask) {
    const qword_t* block = e->arena + e->offsets[k];
    qword_t* q = packet->next;
    q[0].dw[0] = 2U | (static_cast<u64>(1) << 28);  // CNT, 2 qwords
    q[0].dw[1] = block[0].dw[1];  // the header's own VIFcodes
    q[1] = block[1];
    q[2] = block[2];
    u32& packed = reinterpret_cast<u32*>(q)[StaPipVU1Program::kPackedCountWord];
    packed &= ~static_cast<u32>(VU1_STAPIP_EMIT_STATE_FLAG);
    if (switched && program->emitsStateFlag(true))
      packed |= VU1_STAPIP_EMIT_STATE_FLAG;
    if (mask >= 0) {
      packed &= ~(0x3Fu << VU1_STAPIP_CLIP_MASK_SHIFT);
      packed |= reversedPlaneMask(static_cast<u8>(mask))
                << VU1_STAPIP_CLIP_MASK_SHIFT;
    }
    packet->next = q + 3;
    emitRef(packet, block + 3, e->sizes[k] - 4);
    if (switched)
      packet2_utils_vu_add_start_program(packet,
                                         program->getDestinationAddress());
    else
      packet2_utils_vu_add_continue_program(packet);
  };
  for (u32 i = 0; i < count;) {
    const u8 r = routes[i];
    if (r == 0) { ++i; continue; }
    u32 j = i + 1;
    while (j < count && routes[j] == r) ++j;
    StaPipBakedEntry* e = entries[r];
    auto* program =
        static_cast<StaPipVU1Program*>(const_cast<void*>(e->program));
    const bool switched = program->getName() != lastName;
    if (r == 2 && clipMasks != nullptr) {
      for (u32 k = i; k < j; ++k)
        emitBlock(e, program, k, switched && k == i, clipMasks[k]);
    } else {
      u32 from = i;  // first package REF'd as baked
      if (switched && i > 0) {
        // Baked as a continuation (no GS-state flag, MSCNT); this run starts
        // after another program, so its first block needs both.
        emitBlock(e, program, i, true, -1);
        from = i + 1;
      }
      if (from < j) {
        u32 qw = 0;
        for (u32 k = from; k < j; ++k) qw += e->sizes[k];
        emitRef(packet, e->arena + e->offsets[from], qw);
      }
    }
    lastName = program->getName();
    i = j;
  }
  lastProgramName = lastName;
  baked.countHit();
  ++partialRunHits;
#if TYRA_STAPIP_CROSSING_VERIFY
  crossVerifyCompare(packet);  // Modified by TyraX: crossing pass verify
#endif
  finishPartialReplay();
  return true;
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
  // Modified by TyraX: colour refresh - a whole build starts at package 0.
  const bool recordRuns = !entry->runsValid;
  if (recordRuns && index == 0) {
    entry->runs.clear();
    for (u32& q : entry->runQw) q = 0;
    entry->unknownRefs = 0;
  }
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
    if (recordRuns && id == P2_DMA_TAG_REF && qwc > 0) {
      const u8* pa = reinterpret_cast<const u8*>(payload);
      const size_t bytes = static_cast<size_t>(entry->count) * 16;
      const void* streams[4] = {entry->vertices, entry->colors, entry->sts,
                                entry->normals};
      bool known = false;
      for (u32 k = 0; k < 4 && !known; ++k) {
        const u8* base = static_cast<const u8*>(streams[k]);
        if (base == nullptr || pa < base || pa + qwc * 16 > base + bytes ||
            ((pa - base) & 15) != 0)
          continue;
        entry->runs.push_back(
            {index, static_cast<u32>(bakeScratch.size()) + 1 - startOut,
             static_cast<u32>((pa - base) / 16), qwc, k});
        entry->runQw[k] += qwc;
        known = true;
      }
      if (!known) ++entry->unknownRefs;
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
#if TYRA_STAPIP_BAKED_ANY_VERIFY
  // A VERIFIED bag never replays. Returning false sends it down the ordinary
  // loop, which runs the real writers and rebuilds every block into
  // bakeScratch; endBakedBag then COMPARES that against what the cache is
  // holding instead of overwriting it.
  //
  // Modified by TyraX: this is the one line that turns the exhaustive arm into
  // a shippable sampled one. It used to be an unconditional `return false`, so
  // NOTHING was replayed and the whole 1.287 ms went with it. Asking
  // `verifying()` instead refuses exactly this frame's victim, and the other
  // ~359 bags of a garage-day frame are replayed at full speed.
  if (baked.verifying(entry)) return false;
#endif
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
  wholeArena = entry->arena;  // Modified by TyraX: instancing
  wholeArenaQw = entry->arenaQw;
#if TYRA_STAPIP_STAMP
  // Modified by TyraX: the head + this REF are now contiguous in the packet,
  // before any flush can send them - the one moment a stamp can be copied.
  if (stampArmed && stampOk) captureStamp();
  stampArmed = false;
#endif

  // The arena's own tail already kicked the microprogram for every package in
  // it, so the name bookkeeping has to agree or the next buffer would emit a
  // second kick. Same reasoning as the per-package replay path above.
  auto* program =
      static_cast<StaPipVU1Program*>(const_cast<void*>(entry->program));
  lastProgramName = program->getName();
#if TYRA_STAPIP_PACKET_PROFILE
  // Whole-bag replay bypasses addBuffersDataToPacket(), so derive the same GS
  // structure here. This loop exists only in profiling builds; the retained
  // stream remains a single REF in production.
  bool emitState = true;
  for (u32 offset = 0; offset < entry->count;
       offset += entry->maxVertCount) {
    const u32 remaining = entry->count - offset;
    recordGsPacket(telemetry, program,
                   remaining < entry->maxVertCount ? remaining
                                                   : entry->maxVertCount,
                   emitState);
    emitState = false;
  }
#endif

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
#if TYRA_STAPIP_BAKED_ANY_VERIFY
  // A complete entry plus a freshly rebuilt scratch is exactly the comparison
  // this arm exists for: is what the cache WOULD have replayed still what the
  // ordinary writers produce? Any mismatch is a missing invalidation.
  if (baked.verifying(entry) && entry->complete &&
      entry->built == entry->packages && !bakeScratch.empty()) {
    const u32 qw = static_cast<u32>(bakeScratch.size());
    ++baked.verifyChecked;
    if (qw != entry->arenaQw ||
        memcmp(entry->arena, bakeScratch.data(), qw * 16) != 0) {
      ++baked.verifyFailed;
      // Name the program and the FIRST differing quadword. The offset is what
      // says which stream moved: a package's block is a header, the scale and
      // GIFtag, then one header + n quadwords per stream in the writer's order
      // (positions, ST, colour), so an offset in the second run means the ST
      // array was rewritten under an unchanged key.
      u32 firstDiff = 0;
      if (qw == entry->arenaQw) {
        const u32* a = reinterpret_cast<const u32*>(entry->arena);
        const u32* b = reinterpret_cast<const u32*>(bakeScratch.data());
        const u32 words = qw * 4;
        while (firstDiff < words && a[firstDiff] == b[firstDiff]) ++firstDiff;
      }
      auto* prog = static_cast<StaPipVU1Program*>(
          const_cast<void*>(entry->program));
      TYRA_LOG("STAPIPVERIFY MISMATCH ", prog->getStringName().c_str(),
               " count=", entry->count, " packages=", entry->packages,
               " cachedQw=", entry->arenaQw, " freshQw=", qw,
               " firstDiffQw=", firstDiff / 4, " ofBlock0Qw=",
               entry->sizes[0]);
      // Modified by TyraX: name the STREAM and show the bytes. "the first
      // differing quadword is at 156" is only actionable once you know which
      // array that offset lives in and what actually moved - the first round
      // of this arm cost a lot of guessing for want of these two lines.
      {
        const u32* a = reinterpret_cast<const u32*>(entry->arena);
        const u32* b = reinterpret_cast<const u32*>(bakeScratch.data());
        const u32 q = firstDiff / 4;
        TYRA_LOG("             streams v=", reinterpret_cast<u32>(entry->vertices),
                 " st=", reinterpret_cast<u32>(entry->sts),
                 " col=", reinterpret_cast<u32>(entry->colors),
                 " nrm=", reinterpret_cast<u32>(entry->normals),
                 " maxVert=", entry->maxVertCount,
                 " bboxV=", entry->bboxVersion,
                 " contentV=", entry->contentVersion);
        TYRA_LOG("             cached qw[", q, "]=", a[q * 4], ",", a[q * 4 + 1],
                 ",", a[q * 4 + 2], ",", a[q * 4 + 3]);
        TYRA_LOG("             fresh  qw[", q, "]=", b[q * 4], ",", b[q * 4 + 1],
                 ",", b[q * 4 + 2], ",", b[q * 4 + 3]);
      }
    }
    entry->built = entry->packages;  // the entry itself is untouched
    bakeScratch.clear();
    return;
  }
#endif
  if (!entry->complete && entry->built == entry->packages &&
      !bakeScratch.empty()) {
    const u32 qw = static_cast<u32>(bakeScratch.size());
    entry->raw = baked.allocateArena(qw);
    if (entry->raw) {
      const u32 aligned =
          (reinterpret_cast<u32>(entry->raw.get()) + 15u) & ~15u;
      entry->arena = reinterpret_cast<qword_t*>(aligned);
      memcpy(entry->arena, bakeScratch.data(), qw * 16);
      entry->arenaQw = qw;
      Vif1Queue::registerImmutableSpan(entry->arena, qw);
      entry->complete = 1;
      // Modified by TyraX: colour refresh is allowed only when the recorded
      // runs hold exactly one colour per vertex.
      if (!entry->runsValid) {
        const void* streams[4] = {entry->vertices, entry->colors, entry->sts,
                                  entry->normals};
        bool ok = entry->unknownRefs == 0 && entry->runQw[0] == entry->count;
        for (u32 k = 1; k < 4 && ok; ++k)
          ok = streams[k] == nullptr || entry->runQw[k] == entry->count;
        entry->runsValid = ok;
        if (!ok) entry->runs.clear();
      }
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

#if TYRA_STAPIP_STAMP
// Modified by TyraX: bag stamps (docs/stapip-stamps.md).
//
// The PS2 way to submit a static, wholly visible world bag is to append the
// chain it produced last frame, not to re-derive it: the ordinary path spends
// ~10 000 EE instructions and ~600 cache misses a bag rebuilding a head that
// differs from last frame's in at most the MVP, the fog words and TEX0. A
// stamp keeps those bytes, the facts they were derived from, and re-emits
// them; anything it cannot prove unchanged sends the bag down the ordinary
// path, which re-captures.

StaPipQBufferRenderer::StampEntry* StaPipQBufferRenderer::findStamp(
    const StaPipBag* bag, bool create) {
  if (stamps == nullptr) {
    if (!create) return nullptr;
    stamps = new StampEntry[kStampSlots];
    stampKeys = new const StaPipBag*[kStampSlots]();
    stampBakedOf = new const StaPipBakedEntry*[kStampSlots]();
#if TYRA_STAPIP_STAMP_PASS
    allocateStampRecords();  // Modified by TyraX: stamp pass
#endif
  }
  // Modified by TyraX: the slot the bag remembers - one compare against the
  // entry the hit reads anyway, no hash and no key line (item "stamp slot").
  if (stampSlotHint && bag->stampSlot >= 0 &&
      stamps[bag->stampSlot].bag == bag)
    return &stamps[bag->stampSlot];
  u32 h = reinterpret_cast<u32>(bag) * 0x9E3779B1U;
  h = (h ^ (h >> 15)) & (kStampSlots - 1);
  // Modified by TyraX: probe the packed keys, touch one entry.
  for (u32 probe = 0; probe < 8; ++probe) {
    const u32 at = (h + probe) & (kStampSlots - 1);
    if (stampKeys[at] == bag) return &stamps[at];
  }
  if (!create) return nullptr;
  StampEntry* oldest = nullptr;
  for (u32 probe = 0; probe < 8; ++probe) {
    const u32 at = (h + probe) & (kStampSlots - 1);
    StampEntry& s = stamps[at];
    if (stampKeys[at] == nullptr) return &s;
    if (oldest == nullptr || s.frame < oldest->frame) oldest = &s;
  }
  return oldest;  // create: recycle the stalest of the eight probes
}

int StaPipQBufferRenderer::stampSlot(const StaPipBag* bag) const {
  if (stampKeys == nullptr) return -1;
  if (stampSlotHint && bag->stampSlot >= 0 &&
      stamps[bag->stampSlot].bag == bag)
    return bag->stampSlot;
  u32 h = reinterpret_cast<u32>(bag) * 0x9E3779B1U;
  h = (h ^ (h >> 15)) & (kStampSlots - 1);
  for (u32 probe = 0; probe < 8; ++probe) {
    const u32 at = (h + probe) & (kStampSlots - 1);
    if (stampKeys[at] == bag) return static_cast<int>(at);
  }
  return -1;
}

bool StaPipQBufferRenderer::hasStamp(const StaPipBag* bag) const {
  return stampEnabled && stampSlot(bag) >= 0;
}

namespace {
inline void prefetchLine(const void* p) {
#ifdef _EE
  asm volatile("pref 0, 0(%0)" : : "r"(p));
#else
  (void)p;
#endif
}
}  // namespace

#if TYRA_STAPIP_STAMP_PASS
// Modified by TyraX: the stamp pass's per-slot state (docs/dod-submission.md,
// "Phase 3, first slice"; the code is further down, before tryStamp).
// The record of one stamp slot (64 bytes, one line).
struct alignas(64) StaPipQBufferRenderer::StampRecord {
  u32 cam, light, tex, glob, prog;  // epochs the answers were derived under
  const StaPipBag* bag;             // owner (slot reuse)
  const texwrap_t* wanted;          // the wrap in force after the bag
  texbuffer_t* tb;                  // the resident allocation's core buffer
  const RendererCoreSpotLight* lightPtr;  // the picked light
  u32 clutAddr;                     // CLUT state useTexture+updateClutBuffer set
  s16 allocIdx;                     // the residency entry (LRU touch)
  u8 clutPsm, clutLoad;
  u8 func, flags, pad0, pad1;
  u32 camSeen;                      // last camera epoch a C replay ran under
  u32 pad[3];
};
enum : u8 {
  kRecValid = 1, kRecSpotOn = 2, kRecCand = 4, kRecTextured = 8,
  kRecReceive = 16, kRecTexCache = 32, kRecIdent = 64
};
// Per stamp slot, what the revision path reads instead of the companion bags
// (info, colour, texture bag). Valid while clsRev == DrawEpochs::g.cls[cls].
struct alignas(64) StaPipQBufferRenderer::StampRevision {
  const StaPipColorBag* colorBag;
  const StaPipTextureBag* texBag;
  const u32* cv[3];        // content stamp words (bag, colour, texture)
  const Color* single;     // bag->color->single (the live colour words)
  u32 clsRev;              // class epoch the facts were verified under (0: none)
  u8 cls;                  // the class it was verified as
  u8 aa, blend, shading, linear;  // setInfo's inputs
  u8 fullClip, tight, receive;    // visibility band, light receive
  u8 func, textured, pad[2];
  u32 pad2[6];
};
// Per list position within the frame, what the pass found there last time
// (two per line, in list order): draw k+1's lines are prefetched from it.
struct alignas(32) StaPipQBufferRenderer::StampPlan {
  const StaPipBag* bag;
  const void* info;
  const void* model;
  const void* entry;
  int slot;
  const void* contentWord;  // the revision's cv[0] (prefetch), or nullptr
  u32 pad[2];
};

#endif

void StaPipQBufferRenderer::prefetchStamp(const StaPipBag* bag) const {
  const int at = stampSlot(bag);
  if (at < 0) return;
  const u8* e = reinterpret_cast<const u8*>(&stamps[at]);
  // Facts, boxes, matrix and the info copy: the first ~6 lines of the entry.
  for (u32 off = 0; off < 384; off += 64) prefetchLine(e + off);
#if TYRA_STAPIP_STAMP_PASS
  prefetchLine(&stampRecs[at]);  // Modified by TyraX: the stamp record
#endif
  prefetchLine(bag);
  prefetchLine(bag->info);
  prefetchLine(bag->info->model);
}

// Drop every stamp whose baked entry was destroyed since the last sweep, before
// anything can read through its raw entry pointer. Rare: entries die only on
// expiry (unused for kLifetimeFrames), eviction and clear().
void StaPipQBufferRenderer::sweepStamps() {
  const u32 gen = baked.eraseGeneration();
  if (gen != stampTableGen) {
    for (u32 i = 0; i < kStampSlots; ++i) {
      stamps[i].bag = nullptr;
      stampKeys[i] = nullptr;
    }
    stampTableGen = gen;
#if TYRA_STAPIP_CROSSING_ON
    crossSweepAll();  // Modified by TyraX: the crossing records die too
#endif
    baked.clearDestroyed();
    return;
  }
  const u32 n = baked.destroyedPending();
  if (n == 0) return;
  for (u32 i = 0; i < kStampSlots; ++i) {
    if (stampKeys[i] == nullptr) continue;
    for (u32 k = 0; k < n; ++k)
      if (stampBakedOf[i] == baked.destroyedAt(k)) {
        stamps[i].bag = nullptr;
        stampKeys[i] = nullptr;
        break;
      }
  }
#if TYRA_STAPIP_CROSSING_ON
  crossSweepDestroyed(n);  // Modified by TyraX: records on destroyed entries
#endif
  baked.clearDestroyed();
}

u32 StaPipQBufferRenderer::stampPrimKey(const StaPipBag* bag) const {
  const u32 mapping =
      (bag->texture != nullptr && bag->texture->texture != nullptr) ? 1u : 0u;
  return static_cast<u32>(prim->type) |
         (static_cast<u32>(prim->shading) << 4) | (mapping << 6) |
         (static_cast<u32>(prim->fogging) << 7) |
         (static_cast<u32>(prim->blending) << 8) |
         (static_cast<u32>(prim->antialiasing) << 9) |
         (static_cast<u32>(prim->mapping_type) << 10) |
         (static_cast<u32>(prim->colorfix) << 11);
}

// World AABB of the transformed object-space box: a superset of the oriented
// box, so "world AABB wholly inside" implies the exact test StaPipCore makes.
void StaPipQBufferRenderer::stampWorldBox(StampEntry& s) {
  const float* m = s.model.data;
  float mn[3] = {1e30F, 1e30F, 1e30F}, mx[3] = {-1e30F, -1e30F, -1e30F};
  for (int c = 0; c < 8; ++c) {
    const float x = (c & 1) ? s.hi.x : s.lo.x;
    const float y = (c & 2) ? s.hi.y : s.lo.y;
    const float z = (c & 4) ? s.hi.z : s.lo.z;
    const float w[3] = {m[0] * x + m[4] * y + m[8] * z + m[12],
                        m[1] * x + m[5] * y + m[9] * z + m[13],
                        m[2] * x + m[6] * y + m[10] * z + m[14]};
    for (int k = 0; k < 3; ++k) {
      if (w[k] < mn[k]) mn[k] = w[k];
      if (w[k] > mx[k]) mx[k] = w[k];
    }
  }
  s.wlo = Vec4(mn[0], mn[1], mn[2], 1.0F);
  s.whi = Vec4(mx[0], mx[1], mx[2], 1.0F);
  // The light pick's sphere, exactly as tryStamp derived it per frame.
  const Vec4 mid(0.5F * (s.lo.x + s.hi.x), 0.5F * (s.lo.y + s.hi.y),
                 0.5F * (s.lo.z + s.hi.z), 1.0F);
  s.worldCenter = s.model * mid;
  const float scale = Math::sqrtNonNegative(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  const float ex = s.hi.x - s.lo.x, ey = s.hi.y - s.lo.y, ez = s.hi.z - s.lo.z;
  s.worldRadius = 0.5F * Math::sqrtNonNegative(ex * ex + ey * ey + ez * ez) * scale;
  invertAffine(m, s.inv, s.invT);
}

static inline u32 stampContent(const StaPipBag* bag) {
  return stampOf(bag->contentVersion) * 0x9E3779B1U +
         stampOf(bag->color->contentVersion) * 0x85EBCA6BU +
         stampOf(bag->texture != nullptr ? bag->texture->contentVersion
                                         : nullptr) *
             0xC2B2AE35U;
}

void StaPipQBufferRenderer::armStampCapture(StaPipBag* bag, const Vec4& lo,
                                            const Vec4& hi, bool batchSized) {
  stampArmed = stampEnabled;
  lastCaptureSlot = -1;  // Modified by TyraX: captureRecord
  stampBag = bag;
  stampBatchSized = batchSized;
  stampLo = lo;
  stampHi = hi;
}

// Modified by TyraX: the REPEAT entry (TYRA_STAPIP_REPEAT_ENTRY). Copies
// src[0, qw) without the block at [stampEntryAt, +stampEntryQw); refuses
// anything but the shared REPEAT block at offset 1 (right after the FLUSHE
// tag, where sendObjectData writes it). The verify build also puts the block
// back and compares with what was captured.
bool StaPipQBufferRenderer::stripRepeatEntry(const qword_t* src, u32 qw,
                                             qword_t* dst) {
  const u32 at = stampEntryAt, eq = stampEntryQw;
  if (at != 1 || eq != kRepeatEntryQw || at + eq > qw || qw - eq > kStampMaxQw ||
      !sameAsRepeatEntry(src + at)) {
    ++repeatEntryBad;
#if TYRA_STAPIP_REPEAT_ENTRY_VERIFY
    if (repeatEntryBad <= 8)
      printf("REPEATVERIFY BUG strip at=%u qw=%u of %u\n", at, eq, qw);
#endif
    return false;
  }
  u32 k = 0;
  for (u32 i = 0; i < qw; ++i)
    if (i < at || i >= at + eq) dst[k++] = src[i];
#if TYRA_STAPIP_REPEAT_ENTRY_VERIFY
  {
    alignas(16) qword_t back[kStampMaxQw + kRepeatEntryQw];
    back[0] = dst[0];
    for (u32 i = 0; i < kRepeatEntryQw; ++i) back[1 + i] = repeatEntryBlock()[i];
    for (u32 i = 1; i < k; ++i) back[i + kRepeatEntryQw] = dst[i];
    bool same = true;
    for (u32 i = 0; i < qw && same; ++i) {
      const u64 m = (i == 1) ? ~0x03FF0000ull : ~0ull;  // the block's tag PAD
      same = (back[i].dw[0] & m) == (src[i].dw[0] & m) && back[i].dw[1] == src[i].dw[1];
    }
    if (!same) {
      ++repeatEntryBad;
      if (repeatEntryBad <= 8) printf("REPEATVERIFY BUG roundtrip qw=%u\n", qw);
      return false;
    }
  }
#endif
  ++repeatEntryCaptures;
  return true;
}

void StaPipQBufferRenderer::repeatEntryTick() {
#if TYRA_STAPIP_REPEAT_ENTRY_VERIFY
  static u32 frames = 0, last = 0;
  if (DrawEpochs::g.frame == last) return;
  last = DrawEpochs::g.frame;
  if (++frames % 600 == 0)
    printf("REPEATVERIFY frames=%u captures=%u bad=%u sent=%u\n", frames,
           repeatEntryCaptures, repeatEntryBad, repeatEntrySent);
#endif
}

void StaPipQBufferRenderer::captureStamp() {
  StaPipBakedEntry* entry = bakedCurrent;
  StaPipBag* bag = stampBag;
  if (entry == nullptr || bag == nullptr || stampHead == nullptr) return;
  const u32 qwAll = static_cast<u32>(packets[context]->next - stampHead);
  // Modified by TyraX: a REPEAT entry block is not part of the stamp.
  const u32 eq = stampEntryQw;
  const u32 qw = qwAll - eq;
  if (qwAll <= eq || qw > kStampMaxQw || stampMvpAt == 0 || stampOptAt == 0 ||
      (stampSpotActive && stampSpotAt == 0))
    return;
  alignas(16) qword_t stripped[kStampMaxQw];
  const qword_t* head = stampHead;
  if (eq != 0) {
    if (!stripRepeatEntry(stampHead, qwAll, stripped)) return;
    head = stripped;
  }
  auto sh = [this, eq](u16 at) -> u16 {
    return (eq == 0 || at <= stampEntryAt) ? at : static_cast<u16>(at - eq);
  };
  StampEntry* s = findStamp(bag, true);
  if (s == nullptr) return;
  s->bag = bag;
  stampKeys[s - stamps] = bag;
#if TYRA_STAPIP_CROSSING_ON
  CrossPass::xonly[s - stamps] = 0;  // Modified by TyraX: a real stamp
#endif
#if TYRA_STAPIP_STAMP_PASS
  // Modified by TyraX: a capture rewrites the head; its record and
  // revision answers are void.
  stampRecs[s - stamps].flags = 0;
  stampRevs[s - stamps].clsRev = 0;
#endif
  stampBakedOf[s - stamps] = entry;
  s->lo = stampLo;
  s->hi = stampHi;
  s->model = *bag->info->model;
  s->modelPtr = bag->info->model;
  stampWorldBox(*s);
  s->vertices = bag->vertices;
  s->texture = bag->texture != nullptr ? bag->texture->texture : nullptr;
  s->info = bag->info;
  memcpy(s->infoBytes, bag->info, sizeof(StaPipInfoBag));
  s->colorSingle = bag->color->single;
  s->sts = bag->texture != nullptr ? bag->texture->coordinates : nullptr;
  s->colors = bag->color->many;
  s->stripped = bag->stripped ? 1 : 0;
  s->entry = entry;
  s->arena = entry->arena;
  s->count = static_cast<u32>(bag->count);
  s->bboxVersion = bag->bboxVersion;
  s->contentStamp = stampContent(bag);
  s->eraseGen = baked.eraseGeneration();
  s->serial = entry->serial;
  const float depthScale = RendererCoreDepth::scale;
  memcpy(&s->depthScaleBits, &depthScale, sizeof(u32));
  s->residentClasses = residentClasses;
  s->primKey = stampPrimKey(bag);
  s->qw = static_cast<u16>(qw);
  s->mvpAt = sh(stampMvpAt);
  s->optAt = sh(stampOptAt);
  s->tex0At = sh(stampTex0At);
  s->hasTex0 = stampHasTex0;
  s->fogEligible = stampFogEligible;
  s->spotActive = stampSpotActive;
  s->fogOff = stampFogOff;
  s->wrapBlock = stampWrapBlock;
  s->wrap = stampWrap;
  s->singleAt = sh(stampSingleAt);
  s->envAt = sh(stampEnvAt);
  s->batchSized = stampBatchSized ? 1 : 0;
  s->spotAt = sh(stampSpotAt);
  s->program = static_cast<const StaPipVU1Program*>(entry->program)->getName();
  for (u32 i = 0; i < qw; ++i) s->bytes[i] = head[i];
  s->frame = stampHits + stampCaptures;
  bag->stampSlot = static_cast<s16>(s - stamps);
  lastCaptureSlot = static_cast<int>(s - stamps);  // Modified by TyraX
  ++stampCaptures;
}




#if TYRA_STAPIP_STAMP_PASS
// Modified by TyraX: the stamp record and the stamp pass
// (docs/dod-submission.md, "Phase 3, first slice: the stamp record").
//
// A stamp's replay re-asks ~25 questions whose inputs rarely change: texture
// residency, wrap and CLUT, the light pick and spot block, the program set,
// the fog words, batch candidacy, the producer facts. The record keeps the
// answers plus the epochs (renderer/core/draw_epochs.hpp) they were derived
// under; a replay re-derives only the groups whose epochs moved:
//   C camera   visibility, MVP, fog words                  (cam, model)
//   T texture  residency, wrap, candidacy, LRU slot, CLUT  (tex, function)
//   L light    light pick, spot block                      (light, receive)
// The cached WORDS (MVP, fog, TEX0, spot) live in the stamp's own head, so
// the head copy alone carries them. A record is refreshed by every full
// tryStamp and by every replay that re-derived a group.
//
// The stamp pass consumes the records: StaPipCore's frame-recorder flush
// hands the whole recorded list to stampPass, one tight loop over the draws
// in list order. A draw it cannot take (no stamp or record, an epoch it
// cannot re-derive, a straddling box, an upload) goes down renderNow from
// inside the loop, so the GS order is the list order and the loop's code
// stays in the I-cache for the whole flush. Measured on a PS2 (Motor
// District, same ELF): parked work -1.7..-1.8 ms, driving -0.38..-0.43 ms.
namespace {
// The light set as the last resolve saw it. The game re-registers its lights
// every frame, so the epoch moves only when this snapshot differs.
struct StampLightSnap {
  RendererCoreSpotLight spot;
  RendererCoreSpotLight dyn[RendererCore::DYN_LIGHTS_MAX];
  u32 count = 0, bakedMask = 0;
  u8 fast = 0, keep = 0, valid = 0;
};
StampLightSnap stampLightSnap;

void resolveLightEpoch(const RendererCore* rc) {
  DrawEpochs::State& g = DrawEpochs::g;
  if (!g.lightDirty) return;
  g.lightDirty = 0;
  StampLightSnap& s = stampLightSnap;
  const u32 n = rc->dynLightCount;
  const u8 fast = RendererCore::fastLightPick ? 1 : 0;
  const u8 keep = RendererCore::sceneLightsKeepMeshes ? 1 : 0;
  const bool same =
      s.valid && s.count == n && s.bakedMask == rc->dynBakedMask &&
      s.fast == fast && s.keep == keep &&
      memcmp(static_cast<const void*>(&s.spot), static_cast<const void*>(&rc->spot),
             sizeof(RendererCoreSpotLight)) == 0 &&
      (n == 0 || memcmp(static_cast<const void*>(s.dyn),
                        static_cast<const void*>(rc->dynLights),
                        n * sizeof(RendererCoreSpotLight)) == 0);
  if (same) return;
  memcpy(static_cast<void*>(&s.spot), static_cast<const void*>(&rc->spot),
         sizeof(RendererCoreSpotLight));
  if (n != 0)
    memcpy(static_cast<void*>(s.dyn), static_cast<const void*>(rc->dynLights),
           n * sizeof(RendererCoreSpotLight));
  s.count = n;
  s.bakedMask = rc->dynBakedMask;
  s.fast = fast;
  s.keep = keep;
  s.valid = 1;
  ++g.light;
}

// The per-camera-epoch plane set + identity MVP, and the per-glob-epoch fog
// words. Built on first use under a new epoch, never per stamp.
struct alignas(64) StampFrame {
  float fp[6][4];  // frustum planes: nx ny nz d
  float fa[6][4];  // |nx| |ny| |nz| (CoreBBox::frustumCheckAABB's radius)
  M4x4 vp;         // the viewProj it was built from (verify)
  M4x4 mvpIdent;   // vp * Identity: the multiply tryStamp makes, same inputs
  u32 cam = 0, glob = 0, camOk = 0, fogOk = 0;
  float fogScale = 0.0F, fogOffset = 255.0F, fogScaleFixed = 0.0F;
};
StampFrame stampFrame;

__attribute__((noinline)) void buildStampCamera(const M4x4& vp, const Plane* planes) {
  StampFrame& f = stampFrame;
  for (int i = 0; i < 6; ++i) {
    const Vec4& n = planes[i].normal;
    f.fp[i][0] = n.x;
    f.fp[i][1] = n.y;
    f.fp[i][2] = n.z;
    f.fp[i][3] = planes[i].distance;
    f.fa[i][0] = fabsf(n.x);
    f.fa[i][1] = fabsf(n.y);
    f.fa[i][2] = fabsf(n.z);
    f.fa[i][3] = 0.0F;
  }
  f.vp = vp;
  f.mvpIdent = vp * M4x4::Identity;
  f.cam = DrawEpochs::g.cam;
  f.camOk = 1;
}
// Verify: the table claims this epoch; do the live planes and viewProj agree?
__attribute__((noinline)) bool stampCameraStale(const M4x4& vp, const Plane* planes) {
  if (memcmp(static_cast<const void*>(stampFrame.vp.data),
             static_cast<const void*>(vp.data), sizeof(vp.data)) != 0)
    return true;
  for (int i = 0; i < 6; ++i) {
    const float q[4] = {planes[i].normal.x, planes[i].normal.y,
                        planes[i].normal.z, planes[i].distance};
    if (memcmp(stampFrame.fp[i], q, sizeof(q)) != 0) return true;
  }
  return false;
}
__attribute__((noinline)) void buildStampFog(const RendererCore* rc) {
  StampFrame& f = stampFrame;
  f.fogScale = rc->fog.scale;
  f.fogOffset = rc->fog.offset;
  float fixedScale = f.fogScale;
  u32 bits;
  memcpy(&bits, &fixedScale, 4);
  if ((bits & 0xFFFFU) == 0U && bits != 0U) {
    bits |= 1U;
    memcpy(&fixedScale, &bits, 4);
  }
  f.fogScaleFixed = fixedScale;
  f.glob = DrawEpochs::g.glob;
  f.fogOk = 1;
}
inline bool stampFogStale(const RendererCore* rc) {
  return memcmp(&stampFrame.fogScale, &rc->fog.scale, 4) != 0 ||
         memcmp(&stampFrame.fogOffset, &rc->fog.offset, 4) != 0;
}
// The stamp's visibility against the per-epoch planes: CoreBBox::
// frustumCheckAABB's arithmetic exactly (|n| precomputed), then the
// guard-band rule (activePlaneMaskAABB != 0 over the head's 8 band planes).
//   0 emit: wholly inside, or straddling the screen edge inside the band;
//   1 cull (only with `cull`): outside one frustum plane by more than a
//     margin, so the bag's object-space box (inside this world AABB) is
//     outside that plane too and the general path would emit nothing;
//   2 the general path decides (straddling, or outside but too close).
constexpr float kStampCullRel = 1.0e-3F, kStampCullAbs = 1.0e-2F;
__attribute__((always_inline)) inline int stampVisibility(const Plane* gb,
                                                          const Vec4& lo,
                                                          const Vec4& hi,
                                                          bool cull) {
  const float cx = (lo.x + hi.x) * 0.5F, ex = (hi.x - lo.x) * 0.5F;
  const float cy = (lo.y + hi.y) * 0.5F, ey = (hi.y - lo.y) * 0.5F;
  const float cz = (lo.z + hi.z) * 0.5F, ez = (hi.z - lo.z) * 0.5F;
  bool partial = false;
#pragma GCC unroll 1
  for (int i = 0; i < 6; ++i) {
    const float* n = stampFrame.fp[i];
    const float* a = stampFrame.fa[i];
    const float centre = n[3] + n[0] * cx + n[1] * cy + n[2] * cz;
    const float radius = a[0] * ex + a[1] * ey + a[2] * ez;
    if (centre + radius <= 0.0F) {
      if (!cull) return 2;
      const float margin = kStampCullRel * (fabsf(centre) + radius) + kStampCullAbs;
      return centre + radius <= -margin ? 1 : 2;
    }
    if (centre - radius <= 0.0F) partial = true;
  }
  if (!partial) return 0;
  if (gb == nullptr) return 2;
#pragma GCC unroll 1
  for (int i = 0; i < 8; ++i) {
    const Vec4& n = gb[i].normal;
    const float nX = n.x >= 0.0F ? lo.x : hi.x;
    const float nY = n.y >= 0.0F ? lo.y : hi.y;
    const float nZ = n.z >= 0.0F ? lo.z : hi.z;
    const float minDistance = gb[i].distance + n.x * nX + n.y * nY + n.z * nZ;
    if (minDistance <= 0.0F) return 2;
  }
  return 0;
}
inline bool isIdentityMatrix(const M4x4& m) {
  return memcmp(static_cast<const void*>(m.data),
                static_cast<const void*>(M4x4::Identity.data),
                sizeof(m.data)) == 0;
}
// The fog words of a stamp whose camera group is re-derived (options qword
// 0), exactly as sendObjectData / tryStamp.
inline void stampFogWords(qword_t& q, const float* m, u8 fogOff, u8 fogEligible,
                          const Vec4& lo, const Vec4& hi) {
  float fogScale = stampFrame.fogScale;
  float fogOffset = stampFrame.fogOffset;
  bool fogConstant = fogOff != 0;
  if (!fogConstant && fogEligible && fogScale < 0.0F) {
    const float cx = 0.5F * (lo.x + hi.x), hx = 0.5F * (hi.x - lo.x);
    const float cy = 0.5F * (lo.y + hi.y), hy = 0.5F * (hi.y - lo.y);
    const float cz = 0.5F * (lo.z + hi.z), hz = 0.5F * (hi.z - lo.z);
    const float maxW = m[3] * cx + m[7] * cy + m[11] * cz + m[15] +
                       fabsf(m[3]) * hx + fabsf(m[7]) * hy + fabsf(m[11]) * hz;
    fogConstant = maxW * fogScale + fogOffset >= 255.0F;
  }
  if (fogConstant) {
    fogScale = 0.0F;
    fogOffset = 255.0F;
  } else {
    fogScale = stampFrame.fogScaleFixed;
  }
  memcpy(&q.sw[2], &fogScale, 4);
  memcpy(&q.sw[3], &fogOffset, 4);
}
// The env (matcap) camera basis words, as tryStamp: out of line (rare).
__attribute__((noinline, cold)) void stampEnvWords(qword_t* d, const StaPipBag* bag) {
  const Vec4& rr = bag->texture->envRight;
  const Vec4& u = bag->texture->envUp;
  alignas(16) const float e[12] = {rr.x * 0.5F, u.x * -0.5F, 0.0F, 0.0F,
                                   rr.y * 0.5F, u.y * -0.5F, 0.0F, 0.0F,
                                   rr.z * 0.5F, u.z * -0.5F, 1.0F, 0.5F};
  memcpy(d, e, sizeof(e));
}
enum : int { kRecordFull = 0, kRecordHit = 1, kRecordReject = 2 };
constexpr bool kStampVerify = TYRA_STAPIP_STAMP_VERIFY != 0;
}  // namespace

void StaPipQBufferRenderer::allocateStampRecords() {
  static_assert(sizeof(StampRecord) == 64, "a stamp record is one cache line");
  static_assert(sizeof(StampRevision) == 64, "a stamp revision is one cache line");
  static_assert(sizeof(StampPlan) == 32, "two plan entries per line");
  stampRecs = static_cast<StampRecord*>(memalign(64, sizeof(StampRecord) * kStampSlots));
  stampRevs =
      static_cast<StampRevision*>(memalign(64, sizeof(StampRevision) * kStampSlots));
  stampPlan = static_cast<StampPlan*>(memalign(64, sizeof(StampPlan) * kStampPlan));
  memset(static_cast<void*>(stampRecs), 0, sizeof(StampRecord) * kStampSlots);
  memset(static_cast<void*>(stampRevs), 0, sizeof(StampRevision) * kStampSlots);
  memset(static_cast<void*>(stampPlan), 0, sizeof(StampPlan) * kStampPlan);
}

u32 StaPipQBufferRenderer::reserveStampPlan(u32 n) {
  if (stampPlanFrame != DrawEpochs::g.frame) {
    stampPlanFrame = DrawEpochs::g.frame;
    stampPlanPos = 0;
  }
  const u32 at = stampPlanPos;
  stampPlanPos += n;
  return at;
}

// One stamp replay from its record, after tryStamp's producer facts and
// model step. kRecordFull = nothing to reuse (tryStamp continues on the full
// path, which refreshes the record), kRecordReject = rejected as tryStamp
// would, kRecordHit = sent. dry (TYRA_STAPIP_STAMP_VERIFY): the same
// decisions with no side effect, the head written to the verify scratch.
template <bool dry>
int StaPipQBufferRenderer::replayRecord(StampEntry* s, StaPipBag* bag,
                                        const M4x4& viewProj, const Plane* planes,
                                        const Plane* guardBandPlanes,
                                        bool dynamicLightReceive,
                                        const RendererCoreSpotLight* noSpot,
                                        bool moved) {
  const DrawEpochs::State& g = DrawEpochs::g;
  const u32 slot = static_cast<u32>(s - stamps);
  StampRecord& r = stampRecs[slot];
  resolveLightEpoch(rendererCore);
  if (!(r.flags & kRecValid) || r.bag != bag || r.glob != g.glob ||
      r.prog != g.prog || !RendererCore::fastLightPick) {
    if (dry) StampVerify::predictFull();
    return kRecordFull;
  }
  const Texture* tex = static_cast<const Texture*>(s->texture);
  RendererCoreTexture& T = rendererCore->texture;
  const u8 func = tex != nullptr ? bag->texture->textureFunction : 0;
  // A moved model re-derives the camera group (world box -> visibility, MVP,
  // fog) and the light group (world sphere, inverse -> pick, spot); tryStamp
  // already re-derived the box, sphere and inverse with the model.
  const bool needC = moved || r.cam != g.cam;
  const bool needL = moved || r.light != g.light ||
                     ((r.flags & kRecReceive) != 0) != dynamicLightReceive;
  bool needT = tex != nullptr &&
               (r.tex != g.tex || r.func != func || !(r.flags & kRecTexCache));
  if (tex != nullptr && !needT) {
    // The residency entry the record names still holds the same buffer.
    const s32 i = r.allocIdx;
    if (i < 0 || static_cast<u32>(i) >= T.residentCount() ||
        T.residentAt(i).core != r.tb)
      needT = true;
  }
  if (needC && needL && (tex == nullptr || needT)) {
    if (dry) StampVerify::predictFull();
    return kRecordFull;
  }
  // Renderer facts: the entry's (the globals are under the glob/prog epochs).
  if (s->entry->serial != s->serial || s->entry->arena != s->arena ||
      !s->entry->complete) {
    if (dry) StampVerify::predictReject(3, slot);
    return kRecordReject;
  }
  // Prim state: live, exactly as tryStamp (dry leaves it to tryStamp).
  if (!dry) {
    prim->fogging = rendererCore->fog.enabled && !bag->info->fogDisabled
                        ? DRAW_ENABLE
                        : DRAW_DISABLE;
    if ((prim->fogging != DRAW_ENABLE ? 1 : 0) != s->fogOff) return kRecordReject;
    setInfo(bag->info);
    if (stampPrimKey(bag) != s->primKey) return kRecordReject;
  }
  // C: visibility against the per-epoch plane set.
  if (needC) {
    if (!stampFrame.camOk || stampFrame.cam != g.cam) {
      buildStampCamera(viewProj, planes);
    } else if (dry && stampCameraStale(viewProj, planes)) {
      StampVerify::frameBug(1);
    }
    if (stampVisibility(guardBandPlanes, s->wlo, s->whi, false) != 0) {
      if (dry) StampVerify::predictReject(5, slot);
      return kRecordReject;
    }
  }
  // T: residency, wrap, batch candidacy.
  const texwrap_t* wanted = r.wanted;
  bool cand = (r.flags & kRecCand) != 0;
  if (needT) {
    const texwrap_t& w = (tex->getWrapSettings()->horizontal != WRAP_REPEAT)
                             ? *tex->getWrapSettings()
                             : RendererCoreGS::repeatWrap();
    if (!T.isResident(tex) ||
        (s->wrapBlock ? !RendererCoreGS::wrapEquals(w, s->wrap)
                      : !RendererCoreGS::wrapEquals(w, RendererCoreGS::repeatWrap()))) {
      if (dry) StampVerify::predictReject(6, slot);
      return kRecordReject;
    }
    wanted = &w;
    const bool repeatNow = tex->getWrapSettings()->horizontal == WRAP_REPEAT &&
                           tex->getWrapSettings()->vertical == WRAP_REPEAT;
    cand = s->batchSized != 0 || (stampBatchAnySize && repeatNow);
  }
  // L: light pick + spot block (fast pick only: the record requires it).
  const RendererCoreSpotLight* light = r.lightPtr;
  bool spotOn = (r.flags & kRecSpotOn) != 0;
  StaPipClipperSpot meshSpot;
  if (needL) {
    light = nullptr;
    if (dynamicLightReceive && bag->info->dynLightPick)
      light = rendererCore->pickDynLight(
          s->worldCenter, s->worldRadius, bag->info->dynLightSkipSlot,
          bag->info->dynLightSkipBaked ? rendererCore->dynBakedMask : 0u,
          bag->info->spotLit, bag->info->torchPerPixel);
    if (!dynamicLightReceive ||
        (!bag->info->spotLit && (!bag->info->dynLightPick || light == nullptr)))
      light = noSpot;
    meshSpot = buildSpotForBagInverse(light ? *light : rendererCore->spot, s->inv,
                                      s->invT);
    if (meshSpot.enabled && stapipSpotHasNoInfluence(meshSpot, s->lo, s->hi))
      meshSpot.enabled = false;
    if ((meshSpot.enabled ? 1 : 0) != s->spotActive) {
      if (dry) StampVerify::predictReject(7, slot);
      return kRecordReject;
    }
    spotOn = meshSpot.enabled;
  }

  texbuffer_t* tb = r.tb;
  u32 clutAddr = r.clutAddr;
  u32 clutPsm = r.clutPsm, clutLoad = r.clutLoad;
  if (!dry) {
    setSubmissionBatchCandidate(cand, tex != nullptr);
    const texwrap_t& inForce =
        packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
    if (!RendererCoreGS::wrapEquals(RendererCoreGS::repeatWrap(), inForce))
      return kRecordReject;
    // clipper.setSpot is not needed: only the ordinary path's EE clipper reads
    // it, and sendObjectData sets it before every use.
    ensureProgramSet(false);
    if (tex != nullptr) {
      if (needT) {
        auto temp = T.useTexture(tex);
        T.updateClutBuffer(temp.clut);
        temp.core->info.function = bag->texture->textureFunction;
        tb = temp.core;
        clutAddr = T.clut.address;
        clutPsm = T.clut.psm;
        clutLoad = T.clut.load_method;
      } else {
        // useTexture's hit half (LRU touch + counters) on the remembered
        // entry, and updateClutBuffer's outcome from the record.
        T.touchResident(r.allocIdx);
        T.clut.psm = clutPsm;
        T.clut.load_method = clutLoad;
        T.clut.address = clutAddr;
        tb->info.function = func;
      }
    }
  } else if (tex != nullptr && needT) {
    tb = stampPassTexUse(tex, func, &clutAddr, &clutPsm, &clutLoad, true);
    if (tb == nullptr) {
      StampVerify::predictReject(6, slot);
      return kRecordReject;
    }
  }
  packet2_t* packet = nullptr;
  qword_t* dst;
  if (dry) {
    dst = StampVerify::v.words;
  } else {
    packet = packets[context];
    if (!objectDataPending) {
      startPacket(packet);
      packetWrapSet = false;
    }
    dst = packet->next;
  }
  for (u32 i = 0; i < s->qw; ++i) dst[i] = s->bytes[i];
  // C: MVP straight into the outgoing packet, fog from the per-glob table.
  bool ident = false;
  if (needC) {
    if (!stampFrame.fogOk || stampFrame.glob != g.glob) {
      buildStampFog(rendererCore);
    } else if (dry && stampFogStale(rendererCore)) {
      StampVerify::frameBug(2);
    }
    ident = (r.flags & kRecIdent) != 0 && !moved;
    alignas(16) M4x4 mvpLocal;
    const float* m;
    if (ident) {
      m = stampFrame.mvpIdent.data;
    } else {
      mvpLocal = viewProj * s->model;
      m = mvpLocal.data;
    }
    memcpy(&dst[s->mvpAt + 1], m, 16 * sizeof(float));
    stampFogWords(dst[s->optAt + 1], m, s->fogOff, s->fogEligible, s->lo, s->hi);
  }
  if (needT && s->hasTex0 && tb != nullptr)
    stampPassTex0(&dst[s->tex0At], tb, func, clutAddr, clutPsm, clutLoad);
  if (needL && s->spotActive) {
    alignas(16) const float sp[12] = {
        meshSpot.position.x,  meshSpot.position.y,  meshSpot.position.z,
        meshSpot.invRange2,   meshSpot.direction.x, meshSpot.direction.y,
        meshSpot.direction.z, meshSpot.cosCut2,     meshSpot.color[0],
        meshSpot.color[1],    meshSpot.color[2],    meshSpot.invSoft};
    memcpy(&dst[s->spotAt + 1], sp, sizeof(sp));
  }
  // Producer-owned words: live, as tryStamp.
  if (s->singleAt != 0) memcpy(&dst[s->singleAt + 1], bag->color->single->rgba, 16);
  if (s->envAt != 0) stampEnvWords(&dst[s->envAt + 1], bag);
  if (dry) {
    StampVerify::predictHit(slot, s->qw, needC, tex != nullptr && !needT, needL,
                            ident, moved, false, wanted, tb, light, clutAddr,
                            clutPsm, clutLoad, func, spotOn, cand, needT);
    return kRecordHit;
  }
  commitRecord(s, dst, needC, needT, needL, moved, wanted, tb, light, spotOn, cand,
               clutAddr, clutPsm, clutLoad, func, dynamicLightReceive);
  packet->next = dst + s->qw;
  if (s->wrapBlock) {
    packetWrap = *wanted;
    packetWrapSet = true;
  }
  // The same bookkeeping tryStamp does.
  s->entry->framesLeftToDestroy = StaPipBakedStreams::kLifetimeFrames;
  s->frame = stampHits + stampCaptures;
  lastProgramName = s->program;
  baked.countHit();
  objectDataPending = true;
  ++submissionBatchBags;
  if (!submissionBatchCandidate ||
      packet2_get_qw_count(packet) + kStampMaxQw > packetSize)
    flushPendingPacket();
  ++stampHits;
  return kRecordHit;
}

// The record and head after a replay that re-derived groups (or verify's
// confirmed prediction, so the record evolves the same way under both).
// Camera words are written back into the head only when this stamp saw the
// same camera epoch on its previous C replay (the camera is parked): on a
// moving camera the write-back is pure cost, and record.cam = 0 keeps the
// next replay in C mode.
void StaPipQBufferRenderer::commitRecord(StampEntry* s, const qword_t* dst,
                                         bool needC, bool needT, bool needL,
                                         bool moved, const texwrap_t* wanted,
                                         texbuffer_t* tb,
                                         const RendererCoreSpotLight* light,
                                         bool spotOn, bool cand, u32 clutAddr,
                                         u32 clutPsm, u32 clutLoad, u8 func,
                                         bool dynamicLightReceive) {
  const DrawEpochs::State& g = DrawEpochs::g;
  StampRecord& r = stampRecs[s - stamps];
  const Texture* tex = static_cast<const Texture*>(s->texture);
  if (needC) {
    if (r.camSeen == g.cam) {
      for (u32 k = 1; k <= 4; ++k) s->bytes[s->mvpAt + k] = dst[s->mvpAt + k];
      s->bytes[s->optAt + 1] = dst[s->optAt + 1];
      r.cam = g.cam;
    } else {
      r.camSeen = g.cam;
      r.cam = 0;
    }
    if (moved)
      r.flags = static_cast<u8>((r.flags & ~kRecIdent) |
                                (isIdentityMatrix(s->model) ? kRecIdent : 0));
  }
  if (needT && s->hasTex0 && tb != nullptr) s->bytes[s->tex0At] = dst[s->tex0At];
  if (needL && s->spotActive)
    for (u32 k = 1; k <= 3; ++k) s->bytes[s->spotAt + k] = dst[s->spotAt + k];
  if (needL) {
    r.light = g.light;
    r.lightPtr = light;
    r.flags = static_cast<u8>((r.flags & ~(kRecSpotOn | kRecReceive)) |
                              (spotOn ? kRecSpotOn : 0) |
                              (dynamicLightReceive ? kRecReceive : 0));
  }
  if (needT) {
    RendererCoreTexture& T = rendererCore->texture;
    const s32 idx = T.residentIndex(tex);
    r.tex = g.tex;
    r.func = func;
    r.wanted = wanted;
    r.tb = tb;
    r.allocIdx = static_cast<s16>(idx);
    r.clutAddr = clutAddr;
    r.clutPsm = static_cast<u8>(clutPsm);
    r.clutLoad = static_cast<u8>(clutLoad);
    r.flags = static_cast<u8>(
        (r.flags & ~(kRecCand | kRecTexCache)) | (cand ? kRecCand : 0) |
        ((idx >= 0 && tex->vramResident == nullptr) ? kRecTexCache : 0));
  }
}

// The full path's end (tryStamp, before its bookkeeping): verify compares
// with the dry prediction and, after a confirmed hit, commits as the replay
// would; otherwise the record is refreshed whole and the head written back.
void StaPipQBufferRenderer::afterFullStamp(StampEntry* s, StaPipBag* bag,
                                           const qword_t* dst,
                                           const texwrap_t* wanted,
                                           const RendererCoreSpotLight* light,
                                           bool spotOn, bool cand, texbuffer_t* tb,
                                           bool dynamicLightReceive) {
  const DrawEpochs::State& g = DrawEpochs::g;
  const u32 slot = static_cast<u32>(s - stamps);
  const Texture* tex = static_cast<const Texture*>(s->texture);
  RendererCoreTexture& T = rendererCore->texture;
  const u8 func = tex != nullptr ? bag->texture->textureFunction : 0;
#if TYRA_STAPIP_STAMP_VERIFY
  StampVerify::Prediction& v = StampVerify::v;
  if (v.armed) {
    if (StampVerify::compareSent(s->qw, dst, s->mvpAt, s->optAt, s->hasTex0 != 0,
                                 s->tex0At, s->spotActive != 0, s->spotAt,
                                 s->singleAt, s->envAt, wanted, cand, tex != nullptr,
                                 tb, T.clut.address, T.clut.psm, T.clut.load_method,
                                 func, light, spotOn)) {
      // The record and head as the replay would leave them.
      commitRecord(s, dst, v.needC != 0, v.needT != 0, v.needL != 0, v.moved != 0,
                   wanted, tb, light, spotOn, cand, T.clut.address, T.clut.psm,
                   T.clut.load_method, func, dynamicLightReceive);
      return;
    }
  }
#endif
  // Refresh: every answer from this full replay, under the current epochs.
  resolveLightEpoch(rendererCore);
  StampRecord& r = stampRecs[slot];
  r.bag = bag;
  r.cam = g.cam;
  r.camSeen = g.cam;
  r.light = g.light;
  r.tex = g.tex;
  r.glob = g.glob;
  r.prog = g.prog;
  r.wanted = wanted;
  r.tb = tb;
  r.lightPtr = light;
  const s32 idx = tex != nullptr ? T.residentIndex(tex) : -1;
  r.allocIdx = static_cast<s16>(idx);
  r.clutAddr = T.clut.address;
  r.clutPsm = static_cast<u8>(T.clut.psm);
  r.clutLoad = static_cast<u8>(T.clut.load_method);
  r.func = func;
  r.flags = static_cast<u8>(
      kRecValid | (spotOn ? kRecSpotOn : 0) | (cand ? kRecCand : 0) |
      (tex != nullptr ? kRecTextured : 0) | (dynamicLightReceive ? kRecReceive : 0) |
      ((tex != nullptr && idx >= 0 && tex->vramResident == nullptr) ? kRecTexCache
                                                                     : 0) |
      (isIdentityMatrix(s->model) ? kRecIdent : 0));
  for (u32 i = 0; i < s->qw; ++i) s->bytes[i] = dst[i];
}

namespace {
// MVP = viewProj * model: M4x4::cross's VU0 sequence verbatim (the same
// instructions in the same order on the same inputs, so the same bits as
// tryStamp's `viewProj * s->model`), written straight into the packet.
// Operands as cross(res, a, b): rows of b in vf1-4, a in vf5-8.
__attribute__((always_inline)) inline void stampMvp(float* res, const float* a,
                                                    const float* b) {
#ifdef _EE
  asm volatile(
      "lqc2         $vf1, 0x00(%1) \n\t"
      "lqc2         $vf2, 0x10(%1) \n\t"
      "lqc2         $vf3, 0x20(%1) \n\t"
      "lqc2         $vf4, 0x30(%1) \n\t"
      "lqc2         $vf5, 0x00(%2) \n\t"
      "lqc2         $vf6, 0x10(%2) \n\t"
      "lqc2         $vf7, 0x20(%2) \n\t"
      "lqc2         $vf8, 0x30(%2) \n\t"
      "vmulax.xyzw  $ACC, $vf5, $vf1 \n\t"
      "vmadday.xyzw $ACC, $vf6, $vf1 \n\t"
      "vmaddaz.xyzw $ACC, $vf7, $vf1 \n\t"
      "vmaddw.xyzw  $vf1, $vf8, $vf1 \n\t"
      "vmulax.xyzw  $ACC, $vf5, $vf2 \n\t"
      "vmadday.xyzw $ACC, $vf6, $vf2 \n\t"
      "vmaddaz.xyzw $ACC, $vf7, $vf2 \n\t"
      "vmaddw.xyzw  $vf2, $vf8, $vf2 \n\t"
      "vmulax.xyzw  $ACC, $vf5, $vf3 \n\t"
      "vmadday.xyzw $ACC, $vf6, $vf3 \n\t"
      "vmaddaz.xyzw $ACC, $vf7, $vf3 \n\t"
      "vmaddw.xyzw  $vf3, $vf8, $vf3 \n\t"
      "vmulax.xyzw  $ACC, $vf5, $vf4 \n\t"
      "vmadday.xyzw $ACC, $vf6, $vf4 \n\t"
      "vmaddaz.xyzw $ACC, $vf7, $vf4 \n\t"
      "vmaddw.xyzw  $vf4, $vf8, $vf4 \n\t"
      "sqc2         $vf1, 0x00(%0) \n\t"
      "sqc2         $vf2, 0x10(%0) \n\t"
      "sqc2         $vf3, 0x20(%0) \n\t"
      "sqc2         $vf4, 0x30(%0) \n\t"
      :
      : "r"(res), "r"(b), "r"(a)
      : "memory");
#else
  M4x4 ma, mb;
  memcpy(ma.data, a, sizeof(ma.data));
  memcpy(mb.data, b, sizeof(mb.data));
  const M4x4 out = ma * mb;
  memcpy(res, out.data, sizeof(out.data));
#endif
}
typedef u32 __attribute__((may_alias)) StampWord;
static_assert(sizeof(StaPipInfoBag) % 4 == 0, "info compare by words");
// tryStamp's producer facts (reject 2) and renderNow's stamp gate.
template <class E>
__attribute__((noinline)) bool stampProducerFacts(const E* s, const StaPipBag* bag) {
  if (bag->count <= 0 || bag->info->transformationType != TyraMVP) return false;
  const StampWord* ia = reinterpret_cast<const StampWord*>(s->infoBytes);
  const StampWord* ib = reinterpret_cast<const StampWord*>(bag->info);
  u32 diff = 0;
#pragma GCC unroll 1
  for (u32 w = 0; w < sizeof(StaPipInfoBag) / 4; ++w) diff |= ia[w] ^ ib[w];
  return diff == 0 && s->vertices == bag->vertices &&
         s->count == static_cast<u32>(bag->count) &&
         s->bboxVersion == bag->bboxVersion && s->info == bag->info &&
         s->modelPtr == bag->info->model && bag->billboard == nullptr &&
         bag->lighting == nullptr &&
         (bag->color->single != nullptr) == (s->singleAt != 0) &&
         (bag->texture != nullptr && bag->texture->coordinatesAreNormals) ==
             (s->envAt != 0) &&
         s->texture == (bag->texture != nullptr ? bag->texture->texture : nullptr) &&
         s->contentStamp == stampContent(bag) &&
         s->sts == (bag->texture != nullptr ? bag->texture->coordinates : nullptr) &&
         s->colors == bag->color->many && s->stripped == (bag->stripped ? 1 : 0);
}
// The revision path: the bag-line half of the facts plus the content stamps
// through the pointers cached at the last facts check (no companion line).
template <class E, class X>
inline bool stampRevisionLine(const E* s, const X& x, const StaPipBag* bag) {
  return s->vertices == bag->vertices && s->count == static_cast<u32>(bag->count) &&
         s->bboxVersion == bag->bboxVersion && s->info == bag->info &&
         bag->billboard == nullptr && bag->lighting == nullptr &&
         s->stripped == (bag->stripped ? 1 : 0) && x.colorBag == bag->color &&
         x.texBag == bag->texture && x.cv[0] == bag->contentVersion &&
         s->contentStamp == stampOf(x.cv[0]) * 0x9E3779B1U +
                                stampOf(x.cv[1]) * 0x85EBCA6BU +
                                stampOf(x.cv[2]) * 0xC2B2AE35U;
}
template <class X>
__attribute__((noinline)) void fillStampRevision(X& x, unsigned cls,
                                                 const StaPipBag* bag) {
  const StaPipInfoBag* in = bag->info;
  x.colorBag = bag->color;
  x.texBag = bag->texture;
  x.cv[0] = bag->contentVersion;
  x.cv[1] = bag->color->contentVersion;
  x.cv[2] = bag->texture != nullptr ? bag->texture->contentVersion : nullptr;
  x.single = bag->color->single;
  x.cls = static_cast<u8>(cls);
  x.aa = static_cast<u8>(in->antiAliasingEnabled);
  x.blend = static_cast<u8>(in->blendingEnabled);
  x.shading = static_cast<u8>(in->shadingType);
  x.linear = in->textureMappingType == TyraLinear ? 1 : 0;
  x.fullClip = in->fullClipChecks ? 1 : 0;
  x.tight = in->tightClipBand ? 1 : 0;
  x.receive = in->dynamicLightReceive ? 1 : 0;
  x.textured = (bag->texture != nullptr && bag->texture->texture != nullptr) ? 1 : 0;
  x.func = bag->texture != nullptr ? bag->texture->textureFunction : 0;
  x.clsRev = DrawEpochs::g.cls[cls];
}
// tryStamp's light step (fast pick) on the stamp's world sphere and inverse:
// out of line, run only for a moved model or a stale light answer. false =
// the spot block's presence differs from the capture (tryStamp's reject 7).
__attribute__((noinline)) bool stampSpot(RendererCore* rc, const StaPipInfoBag* info,
                                         bool receive,
                                         const RendererCoreSpotLight* noSpot,
                                         const Vec4& wc, float wr, const float* inv,
                                         const float* invT, const Vec4& lo,
                                         const Vec4& hi, u8 spotActive,
                                         const RendererCoreSpotLight** outLight,
                                         bool* spotOn, float* sp) {
  const RendererCoreSpotLight* light = nullptr;
  if (receive && info->dynLightPick)
    light = rc->pickDynLight(wc, wr, info->dynLightSkipSlot,
                             info->dynLightSkipBaked ? rc->dynBakedMask : 0u,
                             info->spotLit, info->torchPerPixel);
  if (!receive || (!info->spotLit && (!info->dynLightPick || light == nullptr)))
    light = noSpot;
  StaPipClipperSpot out = buildSpotForBagInverse(light ? *light : rc->spot, inv, invT);
  if (out.enabled && stapipSpotHasNoInfluence(out, lo, hi)) out.enabled = false;
  *outLight = light;
  *spotOn = out.enabled;
  const float w[12] = {out.position.x,  out.position.y,  out.position.z,
                       out.invRange2,   out.direction.x, out.direction.y,
                       out.direction.z, out.cosCut2,     out.color[0],
                       out.color[1],    out.color[2],    out.invSoft};
  memcpy(sp, w, sizeof(w));
  return (out.enabled ? 1 : 0) == spotActive;
}
}  // namespace

// The slot probe for a draw whose hint and plan entry both missed: out of
// line, so the pass keeps no copy of the hash probe.
__attribute__((noinline)) int StaPipQBufferRenderer::stampPassProbe(
    const StaPipBag* bag) const {
  return stampSlot(bag);
}

// The run state after a general-path draw (it may have destroyed baked
// entries or created the stamp table). The epochs are read per draw.
void StaPipQBufferRenderer::stampPassAfterGeneral() {
  if (stamps != nullptr) sweepStamps();
}

// Once a flush: tryStamp's first step and the per-epoch tables.
__attribute__((noinline)) void StaPipQBufferRenderer::stampPassBegin(PassContext& c,
                                                                     bool dry) {
  if (stamps != nullptr) sweepStamps();
  repeatEntryTick();  // Modified by TyraX: REPEATVERIFY (verify builds only)
  resolveLightEpoch(rendererCore);
  if (!stampFrame.camOk || stampFrame.cam != DrawEpochs::g.cam) {
    buildStampCamera(*c.viewProj, c.planes);
  } else if (kStampVerify && dry && stampCameraStale(*c.viewProj, c.planes)) {
    StampVerify::frameBug(1);
  }
  if (!stampFrame.fogOk || stampFrame.glob != DrawEpochs::g.glob) {
    buildStampFog(rendererCore);
  } else if (kStampVerify && dry && stampFogStale(rendererCore)) {
    StampVerify::frameBug(2);
  }
  c.begun = 1;
}

// A moved model: tryStamp's model step, exactly (copy the matrix,
// stampWorldBox). The record's camera and light answers are void from here
// whether or not the pass then sends the draw.
__attribute__((noinline)) void StaPipQBufferRenderer::stampPassMove(int slot,
                                                                    const M4x4* m) {
  StampEntry* s = &stamps[slot];
  StampRecord& r = stampRecs[slot];
  s->model = *m;
  stampWorldBox(*s);
  r.cam = 0;
  r.light = 0;
  r.flags = static_cast<u8>((r.flags & ~kRecIdent) |
                            (isIdentityMatrix(s->model) ? kRecIdent : 0));
}

// A texture re-resolve: tryStamp's texture checks (reject 6) and the batch
// candidacy it derives. false = the general path (an upload, a wrap the head
// was not captured with).
__attribute__((noinline)) bool StaPipQBufferRenderer::stampPassTexPick(
    int slot, const Texture* tex, const texwrap_t** wanted, bool* cand) {
  const StampEntry& s = stamps[slot];
  if (!rendererCore->texture.isResident(tex)) return false;
  const texwrap_t& repeat = RendererCoreGS::repeatWrap();
  const texwrap_t& w = (tex->getWrapSettings()->horizontal != WRAP_REPEAT)
                           ? *tex->getWrapSettings()
                           : repeat;
  if (s.wrapBlock ? !RendererCoreGS::wrapEquals(w, s.wrap)
                  : !RendererCoreGS::wrapEquals(w, repeat))
    return false;
  *wanted = &w;
  const bool repeatNow = tex->getWrapSettings()->horizontal == WRAP_REPEAT &&
                         tex->getWrapSettings()->vertical == WRAP_REPEAT;
  *cand = s.batchSized != 0 || (stampBatchAnySize && repeatNow);
  return true;
}

// useTexture + updateClutBuffer + the function, as tryStamp, or their
// read-only twin (dry). nullptr = dry could not name the allocation.
__attribute__((noinline)) texbuffer_t* StaPipQBufferRenderer::stampPassTexUse(
    const Texture* tex, u8 func, u32* ca, u32* cp, u32* cl, bool dry) {
  RendererCoreTexture& T = rendererCore->texture;
  if (!dry) {
    auto temp = T.useTexture(tex);
    T.updateClutBuffer(temp.clut);
    temp.core->info.function = func;
    *ca = T.clut.address;
    *cp = T.clut.psm;
    *cl = T.clut.load_method;
    return temp.core;
  }
  texbuffer_t* tb;
  const texbuffer_t* cb = nullptr;
  if (tex->vramResident != nullptr) {
    tb = tex->vramResident;
  } else {
    const s32 i = T.residentIndex(tex);
    if (i < 0) return nullptr;
    tb = T.residentAt(i).core;
    cb = T.residentAt(i).clut;
  }
  if (cb == nullptr || cb->width == 0) {
    *cp = 0;
    *cl = CLUT_NO_LOAD;
    *ca = 0;
  } else {
    *cp = cb->psm;
    *cl = CLUT_LOAD;
    *ca = cb->address;
  }
  return tb;
}

// The re-resolved TEX0 word, as tryStamp.
__attribute__((noinline)) void StaPipQBufferRenderer::stampPassTex0(
    qword_t* q, texbuffer_t* tb, u8 func, u32 ca, u32 cp, u32 cl) {
  const clutbuffer_t* c = &rendererCore->texture.clut;
  q->dw[0] = GS_SET_TEX0(tb->address >> 6, tb->width >> 6, tb->psm, tb->info.width,
                         tb->info.height, tb->info.components, func, ca >> 6, cp,
                         c->storage_mode, c->start, cl);
}

#if TYRA_STAPIP_CROSSING_ON
// ===== Modified by TyraX: the crossing pass (TYRA_STAPIP_CROSSING_PASS) =====
// docs/dod-submission.md, "Phase 3, second slice: the crossing pass"
// (stapip_crossing.hpp). A crossing bag that owns a stamp is, word for word,
// its stamp's head (the C / T / L groups re-derived by the stamp pass exactly
// as for a stamp, minus the stamp's whole-arena REF) with the VU1 clip
// block's REF inserted before the single colour (or the options), followed
// by emitPartialRuns over the cull entry (= the stamp's entry) and the
// record's clip entry. TYRA_STAPIP_CROSSING_VERIFY proves that equality
// against the general path on every crossing draw.
// The slot the stamp pass would find: hint, last frame's plan entry, probe.
int StaPipQBufferRenderer::crossSlotFor(const StaPipBag* bag, u32 pp) const {
  if (stamps == nullptr) return -1;
  if (stampSlotHint && bag->stampSlot >= 0 && stamps[bag->stampSlot].bag == bag)
    return bag->stampSlot;
  if (stampPlan != nullptr && pp < kStampPlan && stampPlan[pp].bag == bag &&
      stamps[stampPlan[pp].slot].bag == bag)
    return stampPlan[pp].slot;
  return stampSlot(bag);
}

void StaPipQBufferRenderer::crossSweepAll() {
  if (CrossPass::xrecs == nullptr) return;
  for (u32 i = 0; i < CrossPass::kSlots; ++i) CrossPass::xrecs[i].valid = 0;
}

void StaPipQBufferRenderer::crossSweepDestroyed(u32 n) {
  if (CrossPass::xrecs == nullptr) return;
  for (u32 i = 0; i < CrossPass::kSlots; ++i) {
    CrossPass::XRec& xr = CrossPass::xrecs[i];
    if (!xr.valid) continue;
    for (u32 k = 0; k < n; ++k) {
      const void* e = baked.destroyedAt(k);
      if (e == xr.clip || e == xr.cull) {
        xr.valid = 0;
        break;
      }
    }
  }
}

// After a successful lean partial replay of `bag` (bakedCurrent / bakedClip
// still set): the slot whose crossing record to (re)build, with the entry
// half filled, or -1.
__attribute__((cold, noinline)) int StaPipQBufferRenderer::crossCaptureSlot(
    const StaPipBag* bag, u32 size, u32 n) {
  static_assert(CrossPass::kSlots == kStampSlots, "one crossing record per stamp slot");
  if (CrossPass::xrecs == nullptr) {
    CrossPass::xrecs = static_cast<CrossPass::XRec*>(
        memalign(64, CrossPass::kSlots * sizeof(CrossPass::XRec)));
    if (CrossPass::xrecs == nullptr) return -1;
    memset(static_cast<void*>(CrossPass::xrecs), 0,
           CrossPass::kSlots * sizeof(CrossPass::XRec));
  }
  const int slot = stamps != nullptr ? stampSlot(bag) : -1;
  if (slot < 0 || stamps[slot].bag != bag) return -1;
  const StampEntry* s = &stamps[slot];
  StaPipBakedEntry* cull = bakedCurrent;
  StaPipBakedEntry* clip = bakedClip;
  if (cull == nullptr || clip == nullptr || s->entry != cull || !cull->complete ||
      !clip->complete || s->serial != cull->serial || cull->arenaQw > 0xFFFFu ||
      clip->arenaQw > 0xFFFFu || n > cull->packages || n > clip->packages)
    return -1;
  if (s->wrapBlock) return -1;
  // The stamp's head ends with the whole-arena REF (replayWholeBakedBag).
  if (s->qw < 3) return -1;
  const qword_t& last = s->bytes[s->qw - 1];
  if (((last.dw[0] >> 28) & 7U) != 3U ||
      static_cast<u32>(last.dw[0] >> 32) !=
          (reinterpret_cast<u32>(s->arena) & 0x0FFFFFFFU))
    return -1;
  const int cb = clipTight ? 1 : 0;
  if (!vu1Clipping || clipBlockVifQw[cb] == 0) return -1;
  const u32 ins = s->singleAt != 0 ? s->singleAt : s->optAt;
  if (ins <= s->mvpAt || ins >= static_cast<u32>(s->qw - 1) ||
      (s->spotActive && s->spotAt >= ins))
    return -1;
  CrossPass::XRec& xr = CrossPass::xrecs[slot];
  if (xr.valid && xr.bag == bag && xr.vertices == bag->vertices &&
      xr.count == static_cast<u32>(bag->count) && xr.bboxVersion == bag->bboxVersion &&
      xr.cull == cull && xr.cullSerial == cull->serial && xr.clip == clip &&
      xr.clipSerial == clip->serial && xr.clipArena == clip->arena &&
      xr.maxVert == size && xr.n == n && xr.glob == DrawEpochs::g.glob &&
      xr.prog == DrawEpochs::g.prog && xr.cb == cb && xr.ins == ins)
    return -1;  // the same record
  xr.valid = 0;
  xr.cull = cull;
  xr.cullSerial = cull->serial;
  xr.clip = clip;
  xr.clipSerial = clip->serial;
  xr.clipArena = clip->arena;
  xr.cb = static_cast<u8>(cb);
  xr.ins = static_cast<u16>(ins);
  return slot;
}

// The crossing draw the pre-pass classified at list index li, if it is this
// bag with this model value.
__attribute__((noinline)) bool StaPipQBufferRenderer::crossWant(
    const PassContext& c, u32 li, const StaPipBag* bag, const M4x4* modelLive,
    int slot) {
  const CrossPass::Flush& F = *static_cast<const CrossPass::Flush*>(c.cross);
  if (li >= F.n || F.vk[li] != CrossPass::VK_CROSS) return false;
  const CrossPass::Cross& cr = F.cl[F.xi[li]];
  return cr.bag == bag && cr.modelPtr == modelLive && cr.slot == slot &&
         mmi::equal64(cr.model, stamps[slot].model.data);
}

// emitPartialRuns's pricing (5 qwords a clip block, a REF a cull run).
static u32 crossNeed(const u8* routes, u32 count) {
  u32 need = 0;
  u8 last = 0;
  for (u32 i = 0; i < count;) {
    const u8 r = routes[i];
    if (r == 0) { ++i; continue; }
    u32 j = i + 1;
    while (j < count && routes[j] == r) ++j;
    if (r == 2) need += 5 * (j - i);
    else need += (r != last && i > 0) ? 6 : 1;
    last = r;
    i = j;
  }
  return need;
}

#if TYRA_STAPIP_CROSSING_VERIFY
namespace {
// The clip header's plane mask is stored bit-reversed (emitPartialRuns).
inline u32 crossReversedPlaneMask(u8 mask) {
  if (mask == 0) mask = 0x3F;
  u32 r = 0;
  for (u32 i = 0; i < 6; ++i)
    if (mask & (1u << i)) r |= 1u << (5 - i);
  return r;
}
}  // namespace

// emitPartialRuns's emission without its side effects (verify only): into
// `packet` (a scratch view), the program name in force cleared as renderNow
// clears it.
void StaPipQBufferRenderer::crossRunsDry(packet2_t* packet, const u8* routes,
                                         u32 count, const u8* clipMasks,
                                         StaPipBakedEntry* cull,
                                         StaPipBakedEntry* clip) {
  StaPipBakedEntry* entries[3] = {nullptr, cull, clip};
  StaPipProgramName lastName = StaPipUndefinedProgram;
  auto emitBlock = [&](StaPipBakedEntry* e, StaPipVU1Program* program, u32 k,
                       bool switched, int mask) {
    const qword_t* block = e->arena + e->offsets[k];
    qword_t* q = packet->next;
    q[0].dw[0] = 2U | (static_cast<u64>(1) << 28);
    q[0].dw[1] = block[0].dw[1];
    q[1] = block[1];
    q[2] = block[2];
    u32& packed = reinterpret_cast<u32*>(q)[StaPipVU1Program::kPackedCountWord];
    packed &= ~static_cast<u32>(VU1_STAPIP_EMIT_STATE_FLAG);
    if (switched && program->emitsStateFlag(true))
      packed |= VU1_STAPIP_EMIT_STATE_FLAG;
    if (mask >= 0) {
      packed &= ~(0x3Fu << VU1_STAPIP_CLIP_MASK_SHIFT);
      packed |= crossReversedPlaneMask(static_cast<u8>(mask))
                << VU1_STAPIP_CLIP_MASK_SHIFT;
    }
    packet->next = q + 3;
    emitRef(packet, block + 3, e->sizes[k] - 4);
    if (switched)
      packet2_utils_vu_add_start_program(packet, program->getDestinationAddress());
    else
      packet2_utils_vu_add_continue_program(packet);
  };
  for (u32 i = 0; i < count;) {
    const u8 r = routes[i];
    if (r == 0) { ++i; continue; }
    u32 j = i + 1;
    while (j < count && routes[j] == r) ++j;
    StaPipBakedEntry* e = entries[r];
    auto* program = static_cast<StaPipVU1Program*>(const_cast<void*>(e->program));
    const bool switched = program->getName() != lastName;
    if (r == 2 && clipMasks != nullptr) {
      for (u32 k = i; k < j; ++k)
        emitBlock(e, program, k, switched && k == i, clipMasks[k]);
    } else {
      u32 from = i;
      if (switched && i > 0) {
        emitBlock(e, program, i, true, -1);
        from = i + 1;
      }
      if (from < j) {
        u32 qw = 0;
        for (u32 k = from; k < j; ++k) qw += e->sizes[k];
        emitRef(packet, e->arena + e->offsets[from], qw);
      }
    }
    lastName = program->getName();
    i = j;
  }
}

// Verify: the general path's head + runs for the predicted bag, compared
// word for word (called by emitPartialRuns before finishPartialReplay).
__attribute__((cold, noinline)) void StaPipQBufferRenderer::crossVerifyCompare(
    const packet2_t* packet) {
  CrossPass::V& v = CrossPass::v;
  if (!v.armed || v.curBag != v.bag || stampHead == nullptr) return;
  if (++v.cmpWords > 1) return;
  ++v.cmp;
  if (CrossPass::curIdx != static_cast<int>(v.li)) ++v.order;
  const qword_t* a = stampHead;
  const u32 qw = static_cast<u32>(packet->next - stampHead);
  const u32 m = qw < v.qw ? qw : v.qw;
  // Semantic compare, walking the chain as the DMAC does. A DMA tag's PAD
  // field (bits 16..25 of its first word) is never written by packet2's tag
  // writer (packet2_chain_set_dma_tag sets QWC/PCE/ID/IRQ/ADDR/SPR by
  // bitfield), so it carries whatever the buffer held before and the DMAC
  // ignores it. Every other bit - tag QWC/ID/ADDR (REF targets), the tag's
  // VIFcodes and every CNT payload word - must match exactly.
  constexpr u32 kTagPad = 0x03FF0000U;
  u32 i = 0;
  bool padOnly = false;
  while (i < m) {
    const qword_t& s = a[i];
    const qword_t& p = CrossPass::vWords[i];
    const u32 id = (p.sw[0] >> 28) & 7U;
    const u32 n = p.sw[0] & 0xFFFFU;
    if ((s.sw[0] & ~kTagPad) != (p.sw[0] & ~kTagPad) || s.sw[1] != p.sw[1] ||
        s.sw[2] != p.sw[2] || s.sw[3] != p.sw[3])
      break;
    if (s.sw[0] != p.sw[0]) padOnly = true;
    if (id == 1U) {  // CNT: n payload qwords, exact
      if (i + n >= m) break;  // payload past the end: a real (length) difference
      u32 k = 1;
      while (k <= n && memcmp(&a[i + k], &CrossPass::vWords[i + k], 16) == 0) ++k;
      if (k <= n) { i += k; break; }
      i += 1 + n;
    } else if (id == 3U) {  // REF: the tag itself is the whole qword
      i += 1;
    } else {
      break;  // a tag the writers never emit here: a real difference
    }
  }
  if (padOnly && i == m && qw == v.qw) ++v.padOnly;
  if (i != m || qw != v.qw) {
    ++v.wordBad;
    if (i < m) {
      if (i < v.headQw) ++v.headBad; else ++v.runsBad;
      CrossPass::logBug(i < v.headQw ? "head" : "runs", i, v.qw, qw, &a[i],
                        &CrossPass::vWords[i]);
    } else {
      ++v.lenBad;
      CrossPass::logBug("len", i, v.qw, qw, nullptr, nullptr);
    }
  }
}
#endif

// The crossing draw at list index li (the stamp pass reached it straddling,
// with every stamp check passed and the T / L answers resolved): emit it from
// the record (1), or leave it to the general path (0). Verify: the
// prediction only (always 0).
__attribute__((noinline)) int StaPipQBufferRenderer::crossEmit(
    PassContext& c, u32 li, int slot, StaPipBag* bag, bool qok, bool needT,
    bool needL, bool moved, const Texture* tex, u8 func, texbuffer_t* tb,
    u32 clutAddr, u32 clutPsm, u32 clutLoad, const RendererCoreSpotLight* light,
    bool spotOn, const float* sp, const texwrap_t* wanted, bool cand,
    bool receive) {
  CrossPass::Flush& F = *static_cast<CrossPass::Flush*>(c.cross);
  const CrossPass::Cross& cr = F.cl[F.xi[li]];
  StampEntry* s = &stamps[slot];
  StampRecord& r = stampRecs[slot];
  StampRevision& x = stampRevs[slot];
  CrossPass::XRec& xr = CrossPass::xrecs[slot];
  const DrawEpochs::State& g = DrawEpochs::g;
  // ---- the record's own facts (the stamp's were checked by the pass)
  if (!xr.valid || xr.bag != bag || xr.n != cr.n) return 0;
  auto* clip = static_cast<StaPipBakedEntry*>(const_cast<void*>(xr.clip));
  if (xr.cull != s->entry || xr.cullSerial != s->entry->serial ||
      clip->serial != xr.clipSerial || clip->arena != xr.clipArena ||
      !clip->complete || xr.glob != g.glob || xr.prog != g.prog)
    return 0;
  const bool fullClip = qok ? x.fullClip != 0 : bag->info->fullClipChecks;
  const bool tight = qok ? x.tight != 0 : bag->info->tightClipBand;
  if (!fullClip || (tight ? 1 : 0) != xr.tight || s->wrapBlock) return 0;
  if (!vu1Clipping || clipBlockVifQw[xr.cb] == 0) return 0;
  const u8* routes = F.routes + cr.off;
  const u8* masks = F.masks + cr.off;
  const u32 n = cr.n;
  const u32 hq = static_cast<u32>(s->qw) - 1;  // the head without the REF
  const u32 ins = xr.ins;
  const u32 need = crossNeed(routes, n);
  qword_t clipRef;
  clipRef.dw[0] = static_cast<u64>(clipBlockVifQw[xr.cb] & 0xFFFFU) |
                  (static_cast<u64>(3) << 28) |
                  (static_cast<u64>(reinterpret_cast<u32>(clipBlockVif[xr.cb]) &
                                    0x0FFFFFFFU)
                   << 32);
  clipRef.dw[1] = 0;
  const texwrap_t& repeat = RendererCoreGS::repeatWrap();
  u32 entryQw = 0;  // Modified by TyraX: REPEAT entry block, when sent
#if TYRA_STAPIP_CROSSING_VERIFY
  if (hq + 1 + need + 8 > CrossPass::kVWords) return 0;
  {
    const texwrap_t& inForce =
        packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
    if (!RendererCoreGS::wrapEquals(repeat, inForce)) return 0;
  }
  if (tex != nullptr && needT) {
    tb = stampPassTexUse(tex, func, &clutAddr, &clutPsm, &clutLoad, true);
    if (tb == nullptr) return 0;
  }
#else
  RendererCoreTexture& T = rendererCore->texture;
  {
    // setSubmissionBatchCandidate(candidate, tex != nullptr) with renderNow's
    // crossing candidacy: setPartialBatch, resident (the T answers), REPEAT.
    bool rep = true;
    if (tex != nullptr) {
      if (xr.repTex != g.tex) {
        xr.rep = tex->getWrapSettings()->horizontal == WRAP_REPEAT &&
                         tex->getWrapSettings()->vertical == WRAP_REPEAT
                     ? 1
                     : 0;
        xr.repTex = g.tex;
      }
      rep = xr.rep != 0;
    }
    const bool enabled = submissionBatchScope && F.partialBatch && rep;
    if (objectDataPending &&
        (!enabled || packet2_get_qw_count(packets[context]) + kWorstBagPacketSize >
                         packetSize))
      flushPendingPacket();
    submissionBatchCandidate = enabled;
    if (enabled && tex != nullptr) submissionPacketHasTexture = true;
    if (enabled && telemetry) ++telemetry->submissionBatchEligibleBags;
    const texwrap_t& inForce =
        packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
    // Modified by TyraX: the REPEAT entry (a crossing record only ever
    // holds a REPEAT bag: crossSynth / crossCapture).
    if (!RendererCoreGS::wrapEquals(repeat, inForce)) {
#if TYRA_STAPIP_REPEAT_ENTRY
      if (s->wrapBlock != 0) return 0;
      entryQw = kRepeatEntryQw;
#else
      return 0;
#endif
    }
    const u32 have = objectDataPending ? packet2_get_qw_count(packets[context]) : 0;
    if (have + hq + 1 + need + 4 + entryQw > packetSize) return 0;
  }
  if (!billboardsResident && billboardSetActive) ensureProgramSet(false);
  if (tex != nullptr) {
    if (needT) {
      tb = stampPassTexUse(tex, func, &clutAddr, &clutPsm, &clutLoad, false);
    } else {
      T.touchResident(r.allocIdx);
      T.clut.psm = r.clutPsm;
      T.clut.load_method = r.clutLoad;
      T.clut.address = r.clutAddr;
      tb->info.function = func;
    }
  }
#endif
  // ---- the head, in the stamp's layout: the stamp pass's words exactly
  // (the camera group always re-derived: a crossing draw never has r.cam).
  alignas(16) qword_t h[kStampMaxQw];
#pragma GCC unroll 1
  for (u32 i = 0; i < hq; ++i) h[i] = s->bytes[i];
  {
    qword_t* mq = &h[s->mvpAt + 1];
    if ((r.flags & kRecIdent) != 0) {
      const qword_t* id = reinterpret_cast<const qword_t*>(stampFrame.mvpIdent.data);
      mq[0] = id[0];
      mq[1] = id[1];
      mq[2] = id[2];
      mq[3] = id[3];
    } else {
      stampMvp(reinterpret_cast<float*>(mq), c.viewProj->data, s->model.data);
    }
    if (stampFrame.glob != g.glob) buildStampFog(rendererCore);
    stampFogWords(h[s->optAt + 1], reinterpret_cast<const float*>(mq), s->fogOff,
                  s->fogEligible, s->lo, s->hi);
  }
  if (needT && s->hasTex0 && tb != nullptr)
    stampPassTex0(&h[s->tex0At], tb, func, clutAddr, clutPsm, clutLoad);
  if (needL && s->spotActive) memcpy(&h[s->spotAt + 1], sp, 12 * sizeof(float));
  if (s->singleAt != 0)
    memcpy(&h[s->singleAt + 1], qok ? x.single->rgba : bag->color->single->rgba, 16);
  if (s->envAt != 0) stampEnvWords(&h[s->envAt + 1], bag);
#if TYRA_STAPIP_CROSSING_VERIFY
  (void)moved;
  (void)light;
  (void)spotOn;
  (void)wanted;
  (void)cand;
  (void)receive;
  qword_t* dst = CrossPass::vWords;
#else
  // The T / L halves of the stamp's record (never its camera group: that
  // would claim the bag visible as a stamp under this camera).
  commitRecord(s, h, false, needT, needL, moved, wanted, tb, light, spotOn, cand,
               clutAddr, clutPsm, clutLoad, func, receive);
  packet2_t* packet = packets[context];
  if (!objectDataPending) {
    startPacket(packet);
    packetWrapSet = false;
  }
  qword_t* dst = packet->next;
#endif
  qword_t* const start = dst;
  if (entryQw != 0) {  // FLUSHE, the REPEAT entry, then the rest shifted
    const qword_t* eb = repeatEntryBlock();
    dst[0] = h[0];
    for (u32 i = 0; i < kRepeatEntryQw; ++i) dst[1 + i] = eb[i];
    dst += kRepeatEntryQw;
#pragma GCC unroll 1
    for (u32 i = 1; i < ins; ++i) dst[i] = h[i];
  } else {
#pragma GCC unroll 1
    for (u32 i = 0; i < ins; ++i) dst[i] = h[i];
  }
  dst[ins] = clipRef;
#pragma GCC unroll 1
  for (u32 i = ins; i < hq; ++i) dst[i + 1] = h[i];
  auto* cull = s->entry;
#if TYRA_STAPIP_CROSSING_VERIFY
  packet2_t view = *packets[context];
  view.base = view.next = CrossPass::vWords + hq + 1;
  view.tag_opened_at = nullptr;
  view.vif_code_opened_at = nullptr;
  crossRunsDry(&view, routes, n, masks, cull, clip);
  CrossPass::V& v = CrossPass::v;
  if (v.armed) ++v.once;  // a prediction left over from an earlier draw
  v.armed = 1;
  v.li = li;
  v.bag = bag;
  v.curBag = nullptr;
  v.qw = static_cast<u32>(view.next - CrossPass::vWords);
  v.headQw = hq + 1;
  v.n = n;
  v.routes = routes;
  v.masks = masks;
  v.cmpWords = 0;
  v.cmpCls = 0;
  ++v.preds;
  return 0;
#else
  (void)li;
  packet->next = dst + hq + 1;
  bakedCurrent = cull;
  bakedClip = clip;
  bakedPartial = true;
  lastProgramName = StaPipUndefinedProgram;  // renderNow's clearLastProgramName
  const bool ok = emitPartialRuns(routes, n, masks);
  bakedCurrent = nullptr;
  if (!ok) {  // priced above, so never: undo the head, the general path decides
    packet->next = start;
    return 0;
  }
#if TYRA_STAPIP_REPEAT_ENTRY
  if (entryQw != 0) {  // the REPEAT entry is in force from here
    packetWrap = repeat;
    packetWrapSet = true;
    ++repeatEntrySent;
  }
#endif
  // replayWholeBakedBag / acquire's liveness for both entries, the stamp's
  // recency (tryStamp's bookkeeping).
  cull->framesLeftToDestroy = StaPipBakedStreams::kLifetimeFrames;
  clip->framesLeftToDestroy = StaPipBakedStreams::kLifetimeFrames;
  s->frame = stampHits + stampCaptures;
  return 1;
#endif
}

// The general path's crossing head, just before emitPartialRuns (which may
// send the packet): [stampHead, next) and sendObjectData's stamp offsets.
void StaPipQBufferRenderer::crossSnap() {
  CrossPass::Snap& sn = CrossPass::snap;
  sn.ok = 0;
  if (stampHead == nullptr) return;
  const u32 qwAll = static_cast<u32>(packets[context]->next - stampHead);
  // Modified by TyraX: without a REPEAT entry block, as captureStamp.
  const u32 eq = stampEntryQw;
  const u32 qw = qwAll - eq;
  if (qwAll <= eq || qw > kStampMaxQw) return;
  if (eq != 0) {
    if (!stripRepeatEntry(stampHead, qwAll, sn.q)) return;
  } else {
#pragma GCC unroll 1
    for (u32 i = 0; i < qw; ++i) sn.q[i] = stampHead[i];
  }
  auto sh = [this, eq](u16 at) -> u16 {
    return (eq == 0 || at <= stampEntryAt) ? at : static_cast<u16>(at - eq);
  };
  sn.qw = qw;
  sn.mvpAt = sh(stampMvpAt);
  sn.optAt = sh(stampOptAt);
  sn.tex0At = sh(stampTex0At);
  sn.spotAt = sh(stampSpotAt);
  sn.singleAt = sh(stampSingleAt);
  sn.envAt = sh(stampEnvAt);
  sn.fogEligible = stampFogEligible;
  sn.hasTex0 = stampHasTex0;
  sn.spotActive = stampSpotActive;
  sn.fogOff = stampFogOff;
  sn.wrapBlock = stampWrapBlock;
  sn.ok = 1;
}

// A crossing-only stamp slot for a bag that has none (a bag that has never been
// wholly visible - a chunk under the car, everything straddling a parked
// view - never earns a real stamp, so its crossing record would have nowhere
// to live). Built from this general draw's crossing head (the snapshot) exactly
// as captureStamp builds a stamp from a whole replay's head: the clip REF is
// taken out, the whole-arena REF appended, the offsets behind the clip REF
// moved back by one. The record gets every T / L answer of this draw, as
// afterFullStamp's refresh does, and NEVER the camera group. CrossPass::xonly
// marks the slot: tryStamp rejects it, the stamp pass sends it only as a crossing
// draw, a real capture (captureStamp) clears it.
__attribute__((cold, noinline)) void StaPipQBufferRenderer::crossSynth(
    StaPipBag* bag, const Vec4& lo, const Vec4& hi, texbuffer_t* tb,
    const RendererCoreSpotLight* light, bool receive, const texwrap_t* wanted,
    bool batchSized) {
  if (!stampEnabled || stamps == nullptr) return;
  if (stampSlot(bag) >= 0) return;
  const CrossPass::Snap& sn = CrossPass::snap;
  if (bag->billboard != nullptr || bag->lighting != nullptr ||
      bag->info->transformationType != TyraMVP || repository.hasAnyOverride() ||
      vuCustomEnabled || !kInlineUniforms) return;
  if (!sn.ok || sn.wrapBlock || sn.mvpAt != 1 || sn.optAt == 0 ||
      (sn.spotActive && sn.spotAt == 0)) return;
  // Modified by TyraX (2.13.1): a crossing-only slot is only ever emitted with
  // REPEAT in force and no wrap block (crossEmit), so it may only hold a bag
  // that WANTS REPEAT. A CLAMP bag captured while the same CLAMP was already
  // in force has no block in its head; emitted later under REPEAT it would
  // sample with the wrong wrap. Such a bag stays on the general path.
  if (wanted == nullptr ||
      !RendererCoreGS::wrapEquals(*wanted, RendererCoreGS::repeatWrap())) return;
  // sendObjectData's order: ... MVP, [spot], clip REF, [single], options ...
  const u32 p = static_cast<u32>(sn.singleAt != 0 ? sn.singleAt : sn.optAt) - 1;
  const int cb = clipTight ? 1 : 0;
  if (!vu1Clipping || clipBlockVifQw[cb] == 0 || p < static_cast<u32>(sn.mvpAt) + 5 ||
      (sn.spotActive && static_cast<u32>(sn.spotAt) + 4 > p) ||
      (sn.envAt != 0 && sn.envAt < p) || p >= sn.qw) return;
  qword_t clipRef;
  clipRef.dw[0] = static_cast<u64>(clipBlockVifQw[cb] & 0xFFFFU) |
                  (static_cast<u64>(3) << 28) |
                  (static_cast<u64>(reinterpret_cast<u32>(clipBlockVif[cb]) &
                                    0x0FFFFFFFU)
                   << 32);
  clipRef.dw[1] = 0;
  if (memcmp(&sn.q[p], &clipRef, 16) != 0) return;
  StaPipBakedEntry* entry = bakedCurrent;
  if (entry == nullptr || !entry->complete || entry->arenaQw == 0 ||
      entry->arenaQw > 0xFFFFu) return;
  const u32 qw = sn.qw;  // (qw - 1) head words without the clip REF + 1 REF
  if (qw > kStampMaxQw) return;
  StampEntry* s = findStamp(bag, true);
  if (s == nullptr) return;
  const u32 slot = static_cast<u32>(s - stamps);
  // ---- as captureStamp
  s->bag = bag;
  stampKeys[slot] = bag;
  stampRecs[slot].flags = 0;
  stampRevs[slot].clsRev = 0;
  stampBakedOf[slot] = entry;
  s->lo = lo;
  s->hi = hi;
  s->model = *bag->info->model;
  s->modelPtr = bag->info->model;
  stampWorldBox(*s);
  s->vertices = bag->vertices;
  s->texture = bag->texture != nullptr ? bag->texture->texture : nullptr;
  s->info = bag->info;
  memcpy(s->infoBytes, bag->info, sizeof(StaPipInfoBag));
  s->colorSingle = bag->color->single;
  s->sts = bag->texture != nullptr ? bag->texture->coordinates : nullptr;
  s->colors = bag->color->many;
  s->stripped = bag->stripped ? 1 : 0;
  s->entry = entry;
  s->arena = entry->arena;
  s->count = static_cast<u32>(bag->count);
  s->bboxVersion = bag->bboxVersion;
  s->contentStamp = stampContent(bag);
  s->eraseGen = baked.eraseGeneration();
  s->serial = entry->serial;
  const float depthScale = RendererCoreDepth::scale;
  memcpy(&s->depthScaleBits, &depthScale, sizeof(u32));
  s->residentClasses = residentClasses;
  s->primKey = stampPrimKey(bag);
  auto back = [p](u16 at) -> u16 {
    return at == 0 ? 0 : static_cast<u16>(at > p ? at - 1 : at);
  };
  s->qw = static_cast<u16>(qw);
  s->mvpAt = sn.mvpAt;
  s->optAt = back(sn.optAt);
  s->tex0At = back(sn.tex0At);
  s->hasTex0 = sn.hasTex0;
  s->fogEligible = sn.fogEligible;
  s->spotActive = sn.spotActive;
  s->fogOff = sn.fogOff;
  s->wrapBlock = 0;
  s->wrap = texwrap_t{};
  s->singleAt = back(sn.singleAt);
  s->envAt = back(sn.envAt);
  s->batchSized = batchSized ? 1 : 0;
  s->spotAt = sn.spotAt;
  s->program = static_cast<const StaPipVU1Program*>(entry->program)->getName();
  for (u32 i = 0; i < p; ++i) s->bytes[i] = sn.q[i];
  for (u32 i = p + 1; i < qw; ++i) s->bytes[i - 1] = sn.q[i];
  qword_t& ref = s->bytes[qw - 1];
  ref.dw[0] = static_cast<u64>(entry->arenaQw & 0xFFFFU) | (static_cast<u64>(3) << 28) |
              (static_cast<u64>(reinterpret_cast<u32>(entry->arena) & 0x0FFFFFFFU)
               << 32);
  ref.dw[1] = 0;
  s->frame = stampHits + stampCaptures;
  bag->stampSlot = static_cast<s16>(slot);
  CrossPass::xonly[slot] = 1;
  // ---- the record: this draw's T / L answers (afterFullStamp's refresh),
  // no camera group (r.cam 0: a crossing-only slot is never "visible").
  const DrawEpochs::State& g = DrawEpochs::g;
  resolveLightEpoch(rendererCore);
  RendererCoreTexture& T = rendererCore->texture;
  const Texture* tex = static_cast<const Texture*>(s->texture);
  StampRecord& r = stampRecs[slot];
  r.bag = bag;
  r.cam = 0;
  r.camSeen = 0;
  r.light = g.light;
  r.tex = g.tex;
  r.glob = g.glob;
  r.prog = g.prog;
  r.wanted = wanted;
  r.tb = tb;
  r.lightPtr = light;
  const s32 idx = tex != nullptr ? T.residentIndex(tex) : -1;
  r.allocIdx = static_cast<s16>(idx);
  r.clutAddr = T.clut.address;
  r.clutPsm = static_cast<u8>(T.clut.psm);
  r.clutLoad = static_cast<u8>(T.clut.load_method);
  r.func = tex != nullptr ? bag->texture->textureFunction : 0;
  const bool repeatNow = tex == nullptr ||
                         (tex->getWrapSettings()->horizontal == WRAP_REPEAT &&
                          tex->getWrapSettings()->vertical == WRAP_REPEAT);
  const bool cand = s->batchSized != 0 || (stampBatchAnySize && repeatNow);
  r.flags = static_cast<u8>(
      kRecValid | (sn.spotActive ? kRecSpotOn : 0) | (cand ? kRecCand : 0) |
      (tex != nullptr ? kRecTextured : 0) | (receive ? kRecReceive : 0) |
      ((tex != nullptr && idx >= 0 && tex->vramResident == nullptr) ? kRecTexCache
                                                                     : 0) |
      (isIdentityMatrix(s->model) ? kRecIdent : 0));
}

#endif

// The stamp pass over list[0, n) (list[0] is list index `at`, frame-plan
// position `planAt`), in list order. A draw the pass cannot take goes down
// renderNow through c.general from inside the loop, so one call is the
// whole flush. dry (TYRA_STAPIP_STAMP_VERIFY): one draw, no side effect
// beyond tryStamp's own first mutations (sweep, prim/setInfo, the plane and
// fog tables, a moved model's matrix and world box); the words go to the
// verify scratch and the prediction is armed for afterFullStamp; returns
// 1 = would send, 2 = would cull, 0 = would leave it to the general path.
template <bool dry>
u32 StaPipQBufferRenderer::stampPass(const PassDraw* list, u32 n, PassContext& c,
                                     u32 at, u32 planAt) {
  if (!dry || c.begun == 0) stampPassBegin(c, dry);
  const DrawEpochs::State& g = DrawEpochs::g;
  const u32 cam = g.cam;  // a flush never spans a camera change
  const bool gbBags = c.guardBandBags && vu1Clipping;
  const float* vpd = c.viewProj->data;
  const texwrap_t& repeat = RendererCoreGS::repeatWrap();
  RendererCoreTexture& T = rendererCore->texture;
  for (u32 k = 0; k < n; ++k) {
    const PassDraw& d = list[k];
    if (!dry) {
      // Draw k+2's bag line; draw k+1's record, revision, stamp, model and
      // entry from the frame plan (no dependent load).
      if (k + 2 < n && list[k + 2].bag != nullptr) prefetchLine(list[k + 2].bag);
      const u32 pn = planAt + k + 1;
      if (k + 1 < n && pn < kStampPlan && stamps != nullptr &&
          list[k + 1].bag != nullptr && stampPlan[pn].bag == list[k + 1].bag) {
        const StampPlan& p = stampPlan[pn];
        const u8* e = reinterpret_cast<const u8*>(&stamps[p.slot]);
#pragma GCC unroll 1
        for (u32 off = 0; off < sizeof(StampEntry); off += 64) prefetchLine(e + off);
        prefetchLine(&stampRecs[p.slot]);
        prefetchLine(&stampRevs[p.slot]);
        if (p.contentWord != nullptr) prefetchLine(p.contentWord);
        prefetchLine(p.model);
        prefetchLine(p.entry);
      }
    }
    if (d.op != 0) {
      if (dry) return 0;  // verify: the list loop handles markers
      // A submission-batch marker, as the list loop handles it.
      if (d.op == 1) beginSubmissionBatch(); else endSubmissionBatch();
      continue;
    }
    {
    StaPipBag* bag = d.bag;
    if (stamps == nullptr) goto general;
    {
    // The slot: the bag's hint, else last frame's plan entry, else the probe.
    int slot;
    {
      const u32 pp = planAt + k;
      if (stampSlotHint && bag->stampSlot >= 0 && stamps[bag->stampSlot].bag == bag)
        slot = bag->stampSlot;
      else if (pp < kStampPlan && stampPlan[pp].bag == bag &&
               stamps[stampPlan[pp].slot].bag == bag)
        slot = stampPlan[pp].slot;
      else
        slot = stampPassProbe(bag);
    }
    if (slot < 0) goto general;
    {
    StampEntry* s = &stamps[slot];
    StampRecord& r = stampRecs[slot];
    StampRevision& x = stampRevs[slot];
    if (!(r.flags & kRecValid) || r.bag != bag) goto general;
    // ---- producer facts: the class revision (tagged bag) or the full check
    const unsigned cls = bag->revisionClass;
    bool qok = false;
    if (cls != 0 && cls < DrawEpochs::kClasses && x.cls == cls &&
        x.clsRev == g.cls[cls])
      qok = stampRevisionLine(s, x, bag);
    if (qok) {
      if (dry) {
        // The revision said "unchanged"; so must the full facts and every
        // answer the revision stands in for. A difference = a writer that
        // did not bump its class.
        ++StampVerify::vd.revChecked;
        if (!stampProducerFacts(s, bag)) {
          StampVerify::revBug(cls, StampVerify::R_FACTS, bag, slot);
        } else {
          const StaPipInfoBag* in = bag->info;
          if (x.single != bag->color->single)
            StampVerify::revBug(cls, StampVerify::R_SINGLE, bag, slot);
          if (x.receive != (in->dynamicLightReceive ? 1 : 0))
            StampVerify::revBug(cls, StampVerify::R_RECEIVE, bag, slot);
          if (x.fullClip != (in->fullClipChecks ? 1 : 0) ||
              x.tight != (in->tightClipBand ? 1 : 0))
            StampVerify::revBug(cls, StampVerify::R_BAND, bag, slot);
          const bool texd = bag->texture != nullptr && bag->texture->texture != nullptr;
          if (x.textured != (texd ? 1 : 0))
            StampVerify::revBug(cls, StampVerify::R_TEXBAG, bag, slot);
          if (bag->texture != nullptr && x.func != bag->texture->textureFunction)
            StampVerify::revBug(cls, StampVerify::R_FUNC, bag, slot);
        }
      }
    } else {
      if (!stampProducerFacts(s, bag)) goto general;
      if (cls != 0 && cls < DrawEpochs::kClasses) {
        fillStampRevision(x, cls, bag);
        qok = true;  // the revision now holds this draw's answers
      }
    }
    // ---- the model: unchanged, or moved and re-derived here
    const M4x4* modelLive = qok ? s->modelPtr : bag->info->model;
    bool moved = false;
    if (!mmi::equal64(s->model.data, modelLive->data)) {
      stampPassMove(slot, modelLive);
      moved = true;
    }
    // ---- C: visibility (a record exists only for a stamp that passed it
    // under r.cam with this model).
#if TYRA_STAPIP_CROSSING_ON
    bool xing = false;  // Modified by TyraX: a crossing draw (crossing pass)
#endif
    const bool needC = moved || r.cam != cam;
    if (needC) {
      const bool fullClip = qok ? x.fullClip != 0 : bag->info->fullClipChecks;
      const bool tight = qok ? x.tight != 0 : bag->info->tightClipBand;
      const Plane* gb = (gbBags && fullClip) ? (tight ? c.gbTight : c.gbLoose) : nullptr;
      const int vis = stampVisibility(gb, s->wlo, s->whi, true);
      if (vis == 1) {
        // ---- culled in the pass: nothing emitted, as the general path.
        if (dry) {
          StampVerify::v.cull = 1;
          ++StampVerify::vd.cullPredicted;
          return 2;
        }
        const u32 pp = planAt + k;
        if (pp < kStampPlan) {
          StampPlan& p = stampPlan[pp];
          p.bag = bag;
          p.info = bag->info;
          p.model = modelLive;
          p.entry = s->entry;
          p.slot = slot;
          p.contentWord = qok ? x.cv[0] : nullptr;
        }
        continue;
      }
#if TYRA_STAPIP_CROSSING_ON
      if (vis == 2) {
        // Modified by TyraX: straddling - a crossing draw the pre-pass
        // classified goes on through the stamp checks (crossing pass).
        if (dry || c.cross == nullptr || !crossWant(c, at + k, bag, modelLive, slot))
          goto general;
        xing = true;
      }
#else
      if (vis == 2) goto general;
#endif
    }
#if TYRA_STAPIP_CROSSING_ON
    // Modified by TyraX: a crossing-only slot goes on only as a crossing
    // draw (a visible or straddling-in-band one is the general path's, as
    // if it had no stamp; an in-pass cull is the same either way).
    if (CrossPass::xonly[slot] && !xing) goto general;
#endif
    // ---- epochs (read live: a general draw inside the run may move them)
    if (r.glob != g.glob || r.prog != g.prog || !RendererCore::fastLightPick)
      goto general;
    const bool receive = (qok ? x.receive != 0 : bag->info->dynamicLightReceive) &&
                         d.reception != 1;
    const bool needL =
        moved || r.light != g.light || ((r.flags & kRecReceive) != 0) != receive;
    // ---- T: cached, or re-resolved here
    const Texture* tex = static_cast<const Texture*>(s->texture);
    u8 func = 0;
    bool needT = false;
    if (tex != nullptr) {
      func = qok ? x.func : bag->texture->textureFunction;
      const s32 ai = r.allocIdx;
      needT = r.tex != g.tex || r.func != func || !(r.flags & kRecTexCache) || ai < 0 ||
              static_cast<u32>(ai) >= T.residentCount() || T.residentAt(ai).core != r.tb;
    }
    // ---- renderer facts of the entry (tryStamp's reject 3)
    if (s->entry->serial != s->serial || s->entry->arena != s->arena ||
        !s->entry->complete)
      goto general;
    // ---- prim state (tryStamp's reject 4): from the revision when it holds
    if (qok) {
      prim->fogging = s->fogOff ? DRAW_DISABLE : DRAW_ENABLE;
      prim->antialiasing = x.aa;
      prim->blending = x.blend;
      prim->shading = x.shading;
      lod->mag_filter = x.linear ? LOD_MAG_LINEAR : LOD_MAG_NEAREST;
      lod->min_filter = x.linear ? LOD_MIN_LINEAR : LOD_MIN_NEAREST;
      if (dry) {
        // setInfo's effect and the prim key, live, must be what it gave.
        const prim_t p0 = *prim;
        const int mag0 = lod->mag_filter, min0 = lod->min_filter;
        prim->fogging = rendererCore->fog.enabled && !bag->info->fogDisabled
                            ? DRAW_ENABLE
                            : DRAW_DISABLE;
        setInfo(bag->info);
        if (memcmp(&p0, prim, sizeof(p0)) != 0 || mag0 != lod->mag_filter ||
            min0 != lod->min_filter)
          StampVerify::revBug(cls, StampVerify::R_PRIM, bag, slot);
        if (stampPrimKey(bag) != s->primKey)
          StampVerify::revBug(cls, StampVerify::R_KEY, bag, slot);
      }
    } else {
      prim->fogging = rendererCore->fog.enabled && !bag->info->fogDisabled
                          ? DRAW_ENABLE
                          : DRAW_DISABLE;
      if ((prim->fogging != DRAW_ENABLE ? 1 : 0) != s->fogOff) goto general;
      setInfo(bag->info);
      if (stampPrimKey(bag) != s->primKey) goto general;
    }
    // ---- T resolve (resident only) and L resolve
    const texwrap_t* wanted = r.wanted;
    bool cand = (r.flags & kRecCand) != 0;
    if (needT && !stampPassTexPick(slot, tex, &wanted, &cand)) goto general;
    const RendererCoreSpotLight* light = r.lightPtr;
    bool spotOn = (r.flags & kRecSpotOn) != 0;
    alignas(16) float sp[12];
    if (needL && !stampSpot(rendererCore, bag->info, receive, c.noSpot,
                            s->worldCenter, s->worldRadius, s->inv, s->invT, s->lo,
                            s->hi, s->spotActive, &light, &spotOn, sp))
      goto general;
    texbuffer_t* tb = r.tb;
    u32 clutAddr = r.clutAddr, clutPsm = r.clutPsm, clutLoad = r.clutLoad;
    packet2_t* packet = nullptr;
    qword_t* dst;
    u32 entryQw = 0;  // Modified by TyraX: REPEAT entry block, when sent
    if (dry) {
      const texwrap_t& inForce =
          packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
      if (!RendererCoreGS::wrapEquals(repeat, inForce)) goto general;
      if (needT) {
        tb = stampPassTexUse(tex, func, &clutAddr, &clutPsm, &clutLoad, true);
        if (tb == nullptr) goto general;
      }
      dst = StampVerify::v.words;
    } else {
#if TYRA_STAPIP_CROSSING_ON
      if (xing) {  // Modified by TyraX: emitted from the crossing record
        if (crossEmit(c, at + k, slot, bag, qok, needT, needL, moved, tex, func, tb,
                      clutAddr, clutPsm, clutLoad, light, spotOn, sp, wanted, cand,
                      receive) == 1) {
          const u32 pp = planAt + k;
          if (pp < kStampPlan) {
            StampPlan& p = stampPlan[pp];
            p.bag = bag;
            p.info = bag->info;
            p.model = modelLive;
            p.entry = s->entry;
            p.slot = slot;
            p.contentWord = qok ? x.cv[0] : nullptr;
          }
          continue;
        }
        goto general;
      }
#endif
      // setSubmissionBatchCandidate(cand, tex != nullptr), inline.
      const bool enabled = submissionBatchScope && cand;
      if (objectDataPending &&
          (!enabled || packet2_get_qw_count(packets[context]) + kWorstBagPacketSize >
                           packetSize))
        flushPendingPacket();
      submissionBatchCandidate = enabled;
      if (enabled && tex != nullptr) submissionPacketHasTexture = true;
      if (enabled && telemetry) ++telemetry->submissionBatchEligibleBags;
      // The wrap in force (tryStamp's reject 8): after the candidacy flush.
      // Modified by TyraX: a REPEAT bag under another wrap takes the REPEAT
      // entry block (TYRA_STAPIP_REPEAT_ENTRY) instead of the general path.
      const texwrap_t& inForce =
          packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
      if (!RendererCoreGS::wrapEquals(repeat, inForce)) {
#if TYRA_STAPIP_REPEAT_ENTRY
        if (s->wrapBlock != 0) goto general;
        entryQw = kRepeatEntryQw;
#else
        goto general;
#endif
      }
      if (!billboardsResident && billboardSetActive) ensureProgramSet(false);
      if (tex != nullptr) {
        if (needT) {
          tb = stampPassTexUse(tex, func, &clutAddr, &clutPsm, &clutLoad, false);
        } else {
          // useTexture's hit half + updateClutBuffer's outcome.
          T.touchResident(r.allocIdx);
          T.clut.psm = r.clutPsm;
          T.clut.load_method = r.clutLoad;
          T.clut.address = r.clutAddr;
          tb->info.function = func;
        }
      }
      packet = packets[context];
      if (!objectDataPending) {
        startPacket(packet);
        packetWrapSet = false;
      }
      dst = packet->next;
    }
    const u32 qw = s->qw;
    const qword_t* src = s->bytes;
    if (entryQw != 0) {
      // FLUSHE, the REPEAT entry, then the rest: every offset >= 1 moves by
      // the block, so the patches below write through dst shifted by it.
      const qword_t* eb = repeatEntryBlock();
      dst[0] = src[0];
      for (u32 i = 0; i < kRepeatEntryQw; ++i) dst[1 + i] = eb[i];
      dst += kRepeatEntryQw;
#pragma GCC unroll 1
      for (u32 i = 1; i < qw; ++i) dst[i] = src[i];
    } else {
#pragma GCC unroll 1
      for (u32 i = 0; i < qw; ++i) dst[i] = src[i];
    }
    bool ident = false;
    if (needC) {
      qword_t* mq = &dst[s->mvpAt + 1];
      ident = (r.flags & kRecIdent) != 0;
      if (ident) {
        const qword_t* id = reinterpret_cast<const qword_t*>(stampFrame.mvpIdent.data);
        mq[0] = id[0];
        mq[1] = id[1];
        mq[2] = id[2];
        mq[3] = id[3];
      } else {
        stampMvp(reinterpret_cast<float*>(mq), vpd, s->model.data);
      }
      // The fog table follows the glob epoch live (cheap).
      if (stampFrame.glob != g.glob) buildStampFog(rendererCore);
      stampFogWords(dst[s->optAt + 1], reinterpret_cast<const float*>(mq), s->fogOff,
                    s->fogEligible, s->lo, s->hi);
    }
    if (needT && s->hasTex0 && tb != nullptr)
      stampPassTex0(&dst[s->tex0At], tb, func, clutAddr, clutPsm, clutLoad);
    if (needL && s->spotActive) memcpy(&dst[s->spotAt + 1], sp, sizeof(sp));
    // Producer-owned words: live, as tryStamp (the colour through the
    // revision when it holds).
    if (s->singleAt != 0)
      memcpy(&dst[s->singleAt + 1], qok ? x.single->rgba : bag->color->single->rgba,
             16);
    if (s->envAt != 0) stampEnvWords(&dst[s->envAt + 1], bag);  // matcap bags only
    if (dry) {
      StampVerify::predictHit(static_cast<u32>(slot), qw, needC,
                              tex != nullptr && !needT, needL, ident, moved, true,
                              wanted, tb, light, clutAddr, clutPsm, clutLoad, func,
                              spotOn, cand, needT);
      return 1;
    }
    packet->next = dst + qw;
    if (s->wrapBlock) {
      packetWrap = *wanted;
      packetWrapSet = true;
    }
#if TYRA_STAPIP_REPEAT_ENTRY
    if (entryQw != 0) {  // the REPEAT entry is in force from here
      packetWrap = repeat;
      packetWrapSet = true;
      ++repeatEntrySent;
    }
#endif
    // The record.
    if (needT || needL || moved) {
      commitRecord(s, dst, needC, needT, needL, moved, wanted, tb, light, spotOn, cand,
                   clutAddr, clutPsm, clutLoad, func, receive);
    } else if (needC) {
      // commitRecord's camera half, inline (the common driving case).
      if (r.camSeen == cam) {
#pragma GCC unroll 1
        for (u32 j = 1; j <= 4; ++j) s->bytes[s->mvpAt + j] = dst[s->mvpAt + j];
        s->bytes[s->optAt + 1] = dst[s->optAt + 1];
        r.cam = cam;
      } else {
        r.camSeen = cam;
        r.cam = 0;
      }
    }
    // tryStamp's bookkeeping.
    s->entry->framesLeftToDestroy = StaPipBakedStreams::kLifetimeFrames;
    s->frame = stampHits + stampCaptures;
    lastProgramName = s->program;
    baked.countHit();
    objectDataPending = true;
    ++submissionBatchBags;
    if (!submissionBatchCandidate ||
        packet2_get_qw_count(packet) + kStampMaxQw > packetSize) {
      flushPendingPacket();
    } else {
      // The next stamp's destination lines (inside this packet only).
      const qword_t* nd = packet->next;
      const qword_t* end = packet->base + packetSize;
#pragma GCC unroll 1
      for (u32 j = 0; j < 20; j += 4)
        if (nd + j < end) prefetchLine(nd + j);
    }
    ++stampHits;
    {
      const u32 pp = planAt + k;
      if (pp < kStampPlan) {
        StampPlan& p = stampPlan[pp];
        p.bag = bag;
        p.info = bag->info;
        p.model = modelLive;
        p.entry = s->entry;
        p.slot = slot;
        p.contentWord = qok ? x.cv[0] : nullptr;
      }
    }
    continue;
    }  // s
    }  // slot
    }  // bag
  general:
    if (dry) return 0;
    // Down the general path from inside the run, in list order.
    c.general(c.core, at + k);
#if TYRA_STAPIP_CROSSING_VERIFY
    CrossPass::afterGeneral(at + k);  // Modified by TyraX: crossing pass verify
#endif
    stampPassAfterGeneral();
  }
  return n;
}
template u32 StaPipQBufferRenderer::stampPass<false>(const PassDraw*, u32,
                                                     PassContext&, u32, u32);
#if TYRA_STAPIP_STAMP_VERIFY
template u32 StaPipQBufferRenderer::stampPass<true>(const PassDraw*, u32,
                                                    PassContext&, u32, u32);
#endif
#endif  // TYRA_STAPIP_STAMP_PASS

bool StaPipQBufferRenderer::tryStamp(StaPipBag* bag, const M4x4& viewProj,
                                     const Plane* planes,
                                     const Plane* guardBandPlanes,
                                     bool dynamicLightReceive,
                                     const RendererCoreSpotLight* noSpot) {
// Modified by TyraX: a reject after a dry prediction is a verify verdict.
#if TYRA_STAPIP_STAMP_VERIFY
#define STAMP_VERIFY_REJECT(n) \
  if (StampVerify::v.armed) StampVerify::rejected(n)
#else
#define STAMP_VERIFY_REJECT(n) (void)0
#endif
#if TYRA_STAPIP_STAMP_DIAG
  static u32 why[12];
  static u32 tries = 0;
  if (stampEnabled && ++tries % 4000 == 0) {
    printf("STAMPMISS noScope=%u none=%u producer=%u renderer=%u prim=%u "
           "view=%u tex=%u spot=%u wrap=%u\n",
           why[0], why[1], why[2], why[3], why[4], why[5], why[6], why[7],
           why[8]);
    fflush(stdout);
  }
#define STAMP_REJECT(n) do { ++why[n]; STAMP_VERIFY_REJECT(n); return false; } while (0)
#else
#define STAMP_REJECT(n) do { STAMP_VERIFY_REJECT(n); return false; } while (0)
#endif
  if (!stampEnabled || stamps == nullptr) return false;
  sweepStamps();
  StampEntry* s = findStamp(bag, false);
  if (s == nullptr) STAMP_REJECT(1);
#if TYRA_STAPIP_CROSSING_ON
  // Modified by TyraX: a crossing-only slot is no stamp.
  if (CrossPass::xonly[s - stamps]) return false;
#endif
  // Producer-owned facts: the same bag, the same streams, the same versions.
  if (s->vertices != bag->vertices || s->count != static_cast<u32>(bag->count) ||
      s->bboxVersion != bag->bboxVersion || s->info != bag->info ||
      s->modelPtr != bag->info->model || bag->billboard != nullptr ||
      bag->lighting != nullptr ||
      (bag->color->single != nullptr) != (s->singleAt != 0) ||
      (bag->texture != nullptr && bag->texture->coordinatesAreNormals) !=
          (s->envAt != 0) ||
      s->texture != (bag->texture != nullptr ? bag->texture->texture : nullptr) ||
      s->contentStamp != stampContent(bag) ||
      s->sts != (bag->texture != nullptr ? bag->texture->coordinates : nullptr) ||
      s->colors != bag->color->many || s->stripped != (bag->stripped ? 1 : 0) ||
      memcmp(s->infoBytes, bag->info, sizeof(StaPipInfoBag)) != 0)
    STAMP_REJECT(2);
  // A moved model is not a different submission: MVP, fog, light and spot
  // are re-derived from the live matrix below; only the world box used for
  // visibility has to follow it.
  bool modelMoved = false;  // Modified by TyraX: stamp record
  if (!mmi::equal64(s->model.data, bag->info->model->data)) {  // Modified by TyraX: MMI compare (docs/vu0-macro-mode.md)
    s->model = *bag->info->model;
    stampWorldBox(*s);
    modelMoved = true;
  }
#if TYRA_STAPIP_STAMP_PASS
  // Modified by TyraX: the record (docs/dod-submission.md, phase 3): reuse
  // the answers whose epochs did not move. Verify predicts dry (unless the
  // stamp pass already did) and lets the full path below send and compare.
#if TYRA_STAPIP_STAMP_VERIFY
  if (!StampVerify::v.armed)
    replayRecord<true>(s, bag, viewProj, planes, guardBandPlanes,
                       dynamicLightReceive, noSpot, modelMoved);
#else
  {
    const int rec = replayRecord<false>(s, bag, viewProj, planes, guardBandPlanes,
                                        dynamicLightReceive, noSpot, modelMoved);
    if (rec == kRecordHit) return true;
    if (rec == kRecordReject) return false;
  }
#endif
#else
  (void)modelMoved;
#endif
  // Renderer-owned facts. eraseGeneration guards the raw entry pointer (an
  // entry was destroyed somewhere); serial/arena say THIS entry was rebuilt.
  float depthScale = RendererCoreDepth::scale;
  u32 depthBits;
  memcpy(&depthBits, &depthScale, sizeof(u32));
  if (depthBits != s->depthScaleBits || residentClasses != s->residentClasses ||
      repository.hasAnyOverride() || vuCustomEnabled ||
      s->entry->serial != s->serial ||
      s->entry->arena != s->arena || !s->entry->complete)
    STAMP_REJECT(3);
  // Same prim state as the capture (the baked arena's GIF tags encode it).
  prim->fogging = rendererCore->fog.enabled && !bag->info->fogDisabled
                      ? DRAW_ENABLE
                      : DRAW_DISABLE;
  if ((prim->fogging != DRAW_ENABLE ? 1 : 0) != s->fogOff) STAMP_REJECT(4);
  setInfo(bag->info);
  if (stampPrimKey(bag) != s->primKey) STAMP_REJECT(4);
  // Visibility: the stamp only ever stands for the wholly-inside route - the
  // view frustum, or (StaPipCore's guard-band rule) a box that straddles only
  // the screen edge but lies inside all eight VU1 clip planes.
  {
    const CoreBBoxFrustum f = CoreBBox::frustumCheckAABB(planes, s->wlo, s->whi);
    if (f == OUTSIDE_FRUSTUM) STAMP_REJECT(5);
    if (f != IN_FRUSTUM &&
        (guardBandPlanes == nullptr ||
         CoreBBox::activePlaneMaskAABB(guardBandPlanes, s->wlo, s->whi, 8) != 0))
      STAMP_REJECT(5);
  }
  // Texture: resident (a stamp never triggers an upload), and the bag's
  // wanted wrap chosen exactly as StaPipCore chooses it.
  const Texture* tex = static_cast<const Texture*>(s->texture);
  if (tex != nullptr && !rendererCore->texture.isResident(tex)) STAMP_REJECT(6);
  const texwrap_t& wanted =
      (tex != nullptr && tex->getWrapSettings()->horizontal != WRAP_REPEAT)
          ? *tex->getWrapSettings()
          : RendererCoreGS::repeatWrap();
  if (s->wrapBlock ? !RendererCoreGS::wrapEquals(wanted, s->wrap)
                   : !RendererCoreGS::wrapEquals(wanted,
                                                 RendererCoreGS::repeatWrap()))
    STAMP_REJECT(6);
  // The dynamic light: the same pick StaPipCore makes (world sphere of the
  // box - identity model) and the same zero-influence test sendObjectData
  // makes. The spot block's PRESENCE is layout, so it must match the capture;
  // its numbers are rewritten below from the live light.
  const RendererCoreSpotLight* light = nullptr;
  const bool fastLight = RendererCore::fastLightPick;
  if (dynamicLightReceive && bag->info->dynLightPick) {
    if (fastLight) {
      // Modified by TyraX: the sphere is the stamp's, derived with its world
      // box (stampWorldBox) - the same numbers, not re-derived per frame.
      light = rendererCore->pickDynLight(
          s->worldCenter, s->worldRadius, bag->info->dynLightSkipSlot,
          bag->info->dynLightSkipBaked ? rendererCore->dynBakedMask : 0u,
          bag->info->spotLit, bag->info->torchPerPixel);
    } else {
      const M4x4& m = s->model;
      const Vec4 mid(0.5F * (s->lo.x + s->hi.x), 0.5F * (s->lo.y + s->hi.y),
                     0.5F * (s->lo.z + s->hi.z), 1.0F);
      const Vec4 worldCenter = m * mid;
      const float scale = Math::sqrtNonNegative(
          m.data[0] * m.data[0] + m.data[1] * m.data[1] + m.data[2] * m.data[2]);
      const float ex = s->hi.x - s->lo.x, ey = s->hi.y - s->lo.y,
                  ez = s->hi.z - s->lo.z;
      light = rendererCore->pickDynLight(
          worldCenter,
          0.5F * Math::sqrtNonNegative(ex * ex + ey * ey + ez * ez) * scale,
          bag->info->dynLightSkipSlot,
          bag->info->dynLightSkipBaked ? rendererCore->dynBakedMask : 0u,
          bag->info->spotLit, bag->info->torchPerPixel);
    }
  }
  // Modified by TyraX: as StaPipCore - spotLit = false drops the torch from
  // the pick, not the scene's lights.
  if (!dynamicLightReceive ||
      (!bag->info->spotLit && (!bag->info->dynLightPick || light == nullptr)))
    light = noSpot;
  auto meshSpot =
      fastLight ? buildSpotForBagInverse(light ? *light : rendererCore->spot,
                                         s->inv, s->invT)
                : buildSpotForBag(light ? *light : rendererCore->spot,
                                  bag->info->model);
  if (meshSpot.enabled && stapipSpotHasNoInfluence(meshSpot, s->lo, s->hi))
    meshSpot.enabled = false;
  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) STAMP_REJECT(7);

  bool stampCand = false;  // Modified by TyraX: stamp record
  // Batch membership exactly as StaPipCore decides it for this bag. This is
  // also the ordinary path's first mutation (it may flush the pending packet),
  // so a rejection below still leaves the ordinary path a valid start.
  // Modified by TyraX: the size cap in batchSized bounds an ORDINARY bag's
  // packet (one command group per package). A stamp appends ~20 qwords and a
  // REF to an immutable arena whatever the bag's size, so only the REPEAT half
  // of the rule still applies: large road and terrain strips used to send one
  // packet per stamp (docs/stapip-stamps.md, "Batching").
  {
    const bool repeatNow =
        tex == nullptr ||
        (tex->getWrapSettings()->horizontal == WRAP_REPEAT &&
         tex->getWrapSettings()->vertical == WRAP_REPEAT);
    stampCand = s->batchSized != 0 || (stampBatchAnySize && repeatNow);
    setSubmissionBatchCandidate(stampCand, tex != nullptr);
  }
  // The wrap write is state-dependent: both captured forms assume REPEAT in
  // force, the state the wrap block (or its absence) was recorded under.
  {
    const texwrap_t& inForce =
        packetWrapSet ? packetWrap : rendererCore->gs.currentTextureWrap();
    if (!RendererCoreGS::wrapEquals(RendererCoreGS::repeatWrap(), inForce))
      STAMP_REJECT(8);
  }
#undef STAMP_REJECT
#undef STAMP_VERIFY_REJECT

  // ---- committed: from here on this IS the bag's submission ----
  clipper.setSpot(meshSpot);
  ensureProgramSet(false);
  texbuffer_t* tb = nullptr;
  if (tex != nullptr) {
    auto temp = rendererCore->texture.useTexture(tex);
    rendererCore->texture.updateClutBuffer(temp.clut);
    temp.core->info.function = bag->texture->textureFunction;
    tb = temp.core;
  }
  auto* packet = packets[context];
  if (!objectDataPending) {
    startPacket(packet);
    packetWrapSet = false;
  }
  qword_t* dst = packet->next;
  for (u32 i = 0; i < s->qw; ++i) dst[i] = s->bytes[i];
  // MVP: identity model was proven above by the matrix compare at capture.
  const M4x4 mvp = viewProj * s->model;
  memcpy(&dst[s->mvpAt + 1], mvp.data, sizeof(mvp.data));
  // Fog words (options qword 0), re-derived exactly as sendObjectData does.
  {
    qword_t& q = dst[s->optAt + 1];
    float fogScale = rendererCore->fog.scale;
    float fogOffset = rendererCore->fog.offset;
    bool fogConstant = s->fogOff != 0;  // as sendObjectData: fog off => 255
    if (!fogConstant && s->fogEligible && fogScale < 0.0F) {
      const Vec4& lo = s->lo;
      const Vec4& hi = s->hi;
      const float* m = mvp.data;
      const float cx = 0.5F * (lo.x + hi.x), hx = 0.5F * (hi.x - lo.x);
      const float cy = 0.5F * (lo.y + hi.y), hy = 0.5F * (hi.y - lo.y);
      const float cz = 0.5F * (lo.z + hi.z), hz = 0.5F * (hi.z - lo.z);
      const float maxW = m[3] * cx + m[7] * cy + m[11] * cz + m[15] +
                         fabsf(m[3]) * hx + fabsf(m[7]) * hy +
                         fabsf(m[11]) * hz;
      fogConstant = maxW * fogScale + fogOffset >= 255.0F;
    }
    if (fogConstant) {
      fogScale = 0.0F;
      fogOffset = 255.0F;
    } else {
      u32 bits;
      memcpy(&bits, &fogScale, 4);
      if ((bits & 0xFFFFU) == 0U && bits != 0U) {
        bits |= 1U;
        memcpy(&fogScale, &bits, 4);
      }
    }
    memcpy(&q.sw[2], &fogScale, 4);
    memcpy(&q.sw[3], &fogOffset, 4);
  }
  if (s->hasTex0 && tb != nullptr) {
    const clutbuffer_t* cl = &rendererCore->texture.clut;
    dst[s->tex0At].dw[0] = GS_SET_TEX0(
        tb->address >> 6, tb->width >> 6, tb->psm, tb->info.width,
        tb->info.height, tb->info.components, tb->info.function,
        cl->address >> 6, cl->psm, cl->storage_mode, cl->start,
        cl->load_method);
  }
  if (s->spotActive) {
    alignas(16) const float sp[12] = {
        meshSpot.position.x,  meshSpot.position.y,  meshSpot.position.z,
        meshSpot.invRange2,   meshSpot.direction.x, meshSpot.direction.y,
        meshSpot.direction.z, meshSpot.cosCut2,     meshSpot.color[0],
        meshSpot.color[1],    meshSpot.color[2],    meshSpot.invSoft};
    memcpy(&dst[s->spotAt + 1], sp, sizeof(sp));
  }
  if (s->singleAt != 0)
    memcpy(&dst[s->singleAt + 1], bag->color->single->rgba, 16);
  if (s->envAt != 0) {  // as sendObjectData: transposed, ST scale folded in
    const Vec4& r = bag->texture->envRight;
    const Vec4& u = bag->texture->envUp;
    alignas(16) const float e[12] = {r.x * 0.5F, u.x * -0.5F, 0.0F, 0.0F,
                                     r.y * 0.5F, u.y * -0.5F, 0.0F, 0.0F,
                                     r.z * 0.5F, u.z * -0.5F, 1.0F, 0.5F};
    memcpy(&dst[s->envAt + 1], e, sizeof(e));
  }
  packet->next = dst + s->qw;
  if (s->wrapBlock) {  // the re-emitted CLAMP write is now the packet's state
    packetWrap = wanted;
    packetWrapSet = true;
  }
#if TYRA_STAPIP_STAMP_PASS
  // Modified by TyraX: refresh (or, verifying, compare) the record.
  afterFullStamp(s, bag, dst, &wanted, light, meshSpot.enabled, stampCand, tb,
                 dynamicLightReceive);
#endif

  // The same bookkeeping replayWholeBakedBag does after its REF.
  s->entry->framesLeftToDestroy = StaPipBakedStreams::kLifetimeFrames;
  s->frame = stampHits + stampCaptures;  // recency for slot recycling
  lastProgramName = s->program;
  baked.countHit();
  objectDataPending = true;
  ++submissionBatchBags;
  // A stamp is ~20 qwords, not the worst-case 196 the 4-bag cap was sized
  // for: keep packing stamps until the packet itself is nearly full.
  // A later ordinary bag still reserves its own worst case before appending
  // (setSubmissionBatchCandidate), so only the next stamp needs room here.
  if (!submissionBatchCandidate ||
      packet2_get_qw_count(packet) + kStampMaxQw > packetSize)
    flushPendingPacket();
  ++stampHits;
  return true;
}
#endif  // TYRA_STAPIP_STAMP

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
#if TYRA_STAPIP_BAKED_ANY_VERIFY
u32 StaPipQBufferRenderer::getVerifyChecked() const {
  return baked.verifyChecked;
}
u32 StaPipQBufferRenderer::getVerifyFailed() const {
  return baked.verifyFailed;
}
#else
u32 StaPipQBufferRenderer::getVerifyChecked() const { return 0; }
u32 StaPipQBufferRenderer::getVerifyFailed() const { return 0; }
#endif

#else

bool StaPipQBufferRenderer::fillCachedStrip(StaPipQBuffer*,
    const StaPipBagPackage&, u32, u32) { return false; }
bool StaPipQBufferRenderer::fillCachedPackage(StaPipQBuffer*,
    const StaPipBagPackage&) { return false; }

bool StaPipQBufferRenderer::beginBakedBag(StaPipBag*, const u32&, bool) {
  return false;
}
bool StaPipQBufferRenderer::replayWholeBakedBag() { return false; }
bool StaPipQBufferRenderer::beginPartialReplay(StaPipBag*, u32) { return false; }
bool StaPipQBufferRenderer::emitPartialRuns(const u8*, u32, const u8*) { return false; }
void StaPipQBufferRenderer::finishPartialReplay() {}
void StaPipQBufferRenderer::endBakedBag() {}
u32 StaPipQBufferRenderer::takeBakedHits() { return 0; }
u32 StaPipQBufferRenderer::takeBakedBuilds() { return 0; }
u32 StaPipQBufferRenderer::getBakedBytes() const { return 0; }
const u32* StaPipQBufferRenderer::getBakedMisses() const { return nullptr; }
u32 StaPipQBufferRenderer::getBakedLoudCount() const { return 0; }
u32 StaPipQBufferRenderer::getBakedLoudPackages() const { return 0; }
u32 StaPipQBufferRenderer::getBakedLoudReason() const { return 0; }
void StaPipQBufferRenderer::clearBakedMisses() {}
u32 StaPipQBufferRenderer::getVerifyChecked() const { return 0; }
u32 StaPipQBufferRenderer::getVerifyFailed() const { return 0; }

#endif  // TYRA_STAPIP_BAKED_STREAM

void StaPipQBufferRenderer::addBuffersDataToPacket(const u32& from,
                                                   const u32& to,
                                                   const bool& finalize) {
  HardwareTrace::Scope trace("Packet_build");
  const u32 buildStart = telemetry ? readTelemetryTicks() : 0;
  auto* currentPacket = packets[context];
  if (!objectDataPending) startPacket(currentPacket);

  for (u32 i = from; i < to; i++) {
    if (!buffers[i]->any()) continue;

    auto* program = dBufferPrograms[i];
    const bool emitState = lastProgramName != program->getName();

    // Modified by TyraX: every vertex that actually reaches a VU1 buffer,
    // counted where it is packetised rather than where it was classified -
    // the number the EE's per-package bill scales with. See StaPipTelemetry.
    if (telemetry) telemetry->verticesSubmitted += buffers[i]->size;
#if TYRA_STAPIP_PACKET_PROFILE
    if (telemetry) {
      recordGsPacket(telemetry, program, buffers[i]->size, emitState);
    }
#endif

#if TYRA_STAPIP_BAKED_STREAM
    if (replayPartialBakedBuffer(currentPacket, buffers[i], program, emitState)) continue;
    // Modified by TyraX: the baked VIF stream. A run of consecutive packages
    // of one complete bag is ONE DMA REF tag - the arena holds them in package
    // order, so a run is contiguous - and that REF's payload carries the
    // unpacks, the inline vertex data AND the MSCAL/MSCNT of every package in
    // the run. The run stops at the flush boundary `to`, which is why a mesh
    // that straddles one costs two tags instead of one.
    // A VERIFIED bag must not replay HERE either, or the ordinary writers
    // never run and there is nothing to compare the cache against. This is the
    // per-package run replay - the fallback for a bag replayWholeBakedBag
    // refused - and the verify arms have to see through both.
    {
      StaPipBakedEntry* bakedEntry = bakedCurrent;
      const int bakeIdx = buffers[i]->bakeIndex;
      if (!bakedPartial && !baked.verifying(bakedEntry) && bakedEntry != nullptr &&
          bakedEntry->complete && bakeIdx >= 0 &&
          bakeIdx < bakedEntry->packages) {
        u32 runQw = bakedEntry->sizes[bakeIdx];
        u32 next = i + 1;
        u32 nextIdx = static_cast<u32>(bakeIdx) + 1;
        while (next < to && nextIdx < bakedEntry->packages &&
               buffers[next]->any() &&
               buffers[next]->bakeIndex == static_cast<int>(nextIdx)) {
          if (telemetry) telemetry->verticesSubmitted += buffers[next]->size;
#if TYRA_STAPIP_PACKET_PROFILE
          if (telemetry) {
            recordGsPacket(telemetry, dBufferPrograms[next],
                           buffers[next]->size, false);
          }
#endif
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
                                     entry->slotQw],
                        entry->blockQw);
      // Modified by TyraX: re-flag the replayed header for THIS position in
      // the packet. Its packed count carries VU1_STAPIP_EMIT_STATE_FLAG -
      // "(re)send the GS state" - which is set only when the buffer's program
      // differs from the previous buffer's, so it is a property of where the
      // block lands, not of the block. Replaying a block captured mid-run
      // (flag clear) right after a different program - a stripped bag's
      // partially visible package goes to the clip program as an expanded
      // LIST - told VU1 to keep the GS state that list left behind, and the
      // next STRIP was drawn as a triangle list: road packages full of holes
      // and slivers, stable per camera, in PCSX2 and on hardware alike
      // (docs/roads.md, "Holes in the road").
      {
        u32* packed =
            reinterpret_cast<u32*>(currentPacket->next - entry->blockQw) +
            StaPipVU1Program::kPackedCountWord;
        if (program->emitsStateFlag(emitState))
          *packed |= VU1_STAPIP_EMIT_STATE_FLAG;
        else
          *packed &= ~static_cast<u32>(VU1_STAPIP_EMIT_STATE_FLAG);
      }
      retained.countHit();
    } else {
      const u32 blockStart = packet2_get_qw_count(currentPacket);
      program->addBufferDataToPacket(currentPacket, buffers[i], prim,
                                     emitState);
      retained.countBuild();
      if (retainable) {
        const u32 blockQw = packet2_get_qw_count(currentPacket) - blockStart;
        // A block that does not fit, or that disagrees with the length this
        // bag's other packages produced, is simply never retained - the entry
        // keeps rebuilding, which is slower and always right.
        if (blockQw > 0 && blockQw <= entry->slotQw &&
            (entry->blockQw == 0 || entry->blockQw == blockQw)) {
          entry->blockQw = static_cast<u16>(blockQw);
          memcpy(&entry->data[static_cast<u32>(retainIdx) *
                              entry->slotQw],
                 reinterpret_cast<const u8*>(currentPacket->base) +
                     blockStart * 16,
                 blockQw * 16);
          TYRA_ASSERT(
              (reinterpret_cast<const u32*>(currentPacket->base +
                                            blockStart)[StaPipVU1Program::
                                                            kPackedCountWord] &
               VU1_STAPIP_COUNT_MASK) == buffers[i]->size,
              "Retained block header moved: the replay re-flags the packed "
              "count at StaPipVU1Program::kPackedCountWord");
          entry->ready[retainIdx] = 1;
        }
      }
    }
#else
    program->addBufferDataToPacket(currentPacket, buffers[i], prim,
                                   emitState);
#endif

    Verbose("Send ", program->getStringName(), "[", i, "]: ", buffers[i]->size);

    if (buffers[i]->nativePacked) {
      // The decoder jumps to the selected draw entry within this MSCAL.
      packet2_utils_vu_add_start_program(
          currentPacket, StaPipVU1Program::quantizedDecoder().getDestinationAddress());
      lastProgramName = program->getName();
    } else if (lastProgramName != program->getName()) {
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
      // A VERIFIED entry is rebuilt even when COMPLETE - that is the whole
      // point of the arm - so the "not complete yet" guard is relaxed for it
      // and for nothing else.
      const bool bakeWanted =
          bakedEntry != nullptr &&
          (!bakedEntry->complete || baked.verifying(bakedEntry));
      if (bakeWanted && bakeIdx >= 0 && bakeIdx < bakedEntry->packages &&
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
    packetWrapSet = false;  // Modified by TyraX: its wrap write goes too
    objectDataPending = false;
    submissionPacketHasTexture = false;
    return;
  }
  if (HardwareTrace::active && HardwareTrace::detailed && submissionBatchBags > 0) {
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

// Modified by TyraX: instancing - see the header. Bounded by the packet, not by
// kSubmissionBatchSize: each copy is seven quadwords whatever the mesh holds,
// so ~110 copies share one chain and one VIF1 kick.
void StaPipQBufferRenderer::appendBakedInstance(const M4x4& mvp,
                                                bool textured) {
  constexpr u32 kInstanceQw = 7;  // FLUSHE + MVP header + 4 MVP + REF
  TYRA_ASSERT(wholeArena != nullptr, "appendBakedInstance with no arena");
  if (objectDataPending &&
      packet2_get_qw_count(packets[context]) + kInstanceQw + 1 > packetSize)
    flushPendingPacket();
  auto* packet = packets[context];
  if (!objectDataPending) {
    startPacket(packet);
    packetWrapSet = false;
    objectDataPending = true;
  }
  alignas(16) M4x4 m = mvp;  // emitUnpack copies whole qwords
  emitQword(packet, flusheHeader());
  emitUnpack(packet, VU1_MVP_MATRIX_ADDR, m.data, 4);
  emitRef(packet, wholeArena, wholeArenaQw);
  if (textured) submissionPacketHasTexture = true;
  // flushPendingPacket throws an unsent packet away when this is 0.
  if (submissionBatchBags < 255) ++submissionBatchBags;
}

void StaPipQBufferRenderer::finishInstances() { flushPendingPacket(); }

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

// Modified by TyraX (R3): an empty packet is about to be written. While a
// frame is being recorded it is written straight into the frame bank (see
// packetOrigin); otherwise, or when the bank refuses, into its own buffer.
// Only ever re-pointed while EMPTY - everything that remembers a position in
// the packet (stampHead, the retained/baked captures) is set after this.
void StaPipQBufferRenderer::startPacket(packet2_t* packet) {
  packet2_reset(packet, false);
#if TYRA_VIF1_QUEUE && TYRA_VIF1_OWNED_PACKETS && !TYRA_STAPIP_PROBE_UNCACHED_CHAIN
  void* owned = Vif1Queue::reservePacket(static_cast<u32>(packetSize) * 16,
                                         packet->base);
  qword_t* base = owned ? static_cast<qword_t*>(owned) : packetOrigin[context];
  packet->base = packet->next = base;
#endif
}

void StaPipQBufferRenderer::sendPacket() {
  auto* currentPacket = packets[context];

  TYRA_ASSERT(packet2_get_qw_count(currentPacket) <= packetSize,
              "Packet is too big. Internal error");

#if TYRA_VIF1_QUEUE
  // Modified by TyraX: no wait for the previous chain here - it may still be
  // running, and the next one may be queued behind it (vif1_queue.hpp). The
  // wait that protects buffer reuse happens after the context flip below, for
  // the one buffer about to be rewritten.
  if (telemetry != nullptr) ++telemetry->packetFlushes;
#else
  const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
  { HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    frameWaitVif();
    HardwareTrace::state("VIF1_WAIT_END"); }
  if (telemetry != nullptr) {
    ++telemetry->packetFlushes;
    telemetry->vu1WaitTicks += readTelemetryTicks() - waitStart;
  }
#endif
  // Added by TyraX: this wait was inside no bracket at all, so it was
  // indistinguishable from `dispatch`'s package creation and classification.
  // Opt-in only - see stapip_attrib.hpp.
#if TYRA_STAPIP_ATTRIB
  const u32 gifWaitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
#endif
  { HardwareTrace::Scope trace("GIF_DMA_wait");
    frameWaitGif(DMA_CHANNEL_GIF, 0); }  // Wait for texture. Issue #182.
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

#if TYRA_STAPIP_PACKET_PROFILE
  recordVifChain(telemetry, packetTelemetryProducer, currentPacket->base,
                 packet2_get_qw_count(currentPacket));
#endif

  // dma_wait_fast(); // This have no impact on performance

  const u32 submitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
  { HardwareTrace::Scope trace("VIF1_submit");
    if (HardwareTrace::active && HardwareTrace::detailed) { const u32 t=HardwareTrace::ticks(); HardwareTrace::record("Packet_qwords", t, t, packet2_get_qw_count(currentPacket)); }
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
    frameSendPacket(currentPacket, DMA_CHANNEL_VIF1, probeFlush);
    probePacketUsesPool = false;
#elif TYRA_VIF1_QUEUE
    // The same write-back dma_channel_send_packet2 performs, then the chain
    // goes to the queue instead of straight to the channel.
#if !TYRA_VIF1_QUEUE_LAZY_FLUSH
    FlushCache(0);
#endif
    bool sourceCopied = false;
#if TYRA_VIF1_OWNED_PACKETS
    // Modified by TyraX (R3): a packet built in the frame bank is handed over
    // where it lies; its own buffer was never touched, so nothing to wait for.
    const u32 sequence =
        currentPacket->base != packetOrigin[context]
            ? Vif1Queue::submitOwned(currentPacket->base,
                                     packet2_get_qw_count(currentPacket),
                                     &sourceCopied)
            : Vif1Queue::submit(currentPacket->base,
                                packet2_get_qw_count(currentPacket),
                                &sourceCopied);
#else
    const u32 sequence = Vif1Queue::submit(
        currentPacket->base, packet2_get_qw_count(currentPacket), &sourceCopied);
#endif
    packetSequence[context] = sourceCopied ? 0 : sequence;
#else
    frameSendPacket(currentPacket, DMA_CHANNEL_VIF1, true);
#endif
  }
  // Modified by TyraX: the packet's wrap write is on its way (setBagWrap).
  if (packetWrapSet) {
    rendererCore->gs.noteTextureWrap(packetWrap);
    packetWrapSet = false;
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
    Vif1Queue::drain();
    HardwareTrace::state("VIF1_WAIT_END"); }
    // VIF1_STAT: VPS (bits 0-1) = VIF status, VEW (bit 2) = waiting for VU1.
    volatile u32* const vif1Stat = (volatile u32*)0x10003c00;
    for (int spin = 0; spin < 2000000 && (*vif1Stat & 0x7) != 0; ++spin) {
    }
    g_vuMemHook((const void*)0x1100c000, 1024 * 16);
  }

  // Switch packet, so we can proceed during DMA transfer - and switch the
  // slots' copy pools with it, they are referenced by the packet just sent.
#if TYRA_VIF1_QUEUE
  // Modified by TyraX: rotate through kPacketCount buffers. The buffer (and the
  // copy-pool side that goes with it) is rewritten from here on, so wait for
  // the chain that last read it - usually long finished, since kPacketCount - 1
  // newer chains were queued behind it. This is the queue's only EE wait, and
  // it is what vu1WaitTicks reports in this arm.
  context = static_cast<u8>((context + 1) % kPacketCount);
  {
    const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
    HardwareTrace::Scope trace("VIF1_DMA_wait");
    HardwareTrace::state("VIF1_WAIT_START");
    if (packetSequence[context]) Vif1Queue::waitFor(packetSequence[context]);
    HardwareTrace::state("VIF1_WAIT_END");
    if (telemetry != nullptr)
      telemetry->vu1WaitTicks += readTelemetryTicks() - waitStart;
  }
#else
  context = !context;
#endif
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
  if (buffers)  // Modified by TyraX: not allocated outside onUse
    for (u32 i = 0; i < buffersCount; i++) buffers[i]->setMaxVertCount(count);
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

#if TYRA_STAPIP_STAMP
// Modified by TyraX: the capture record (TYRA_STAPIP_CAPTURE_RECORD). The
// whole replay that captured this stamp resolved every answer a record holds:
// the camera (the head carries this draw's MVP and fog words; the bag was
// wholly inside the frustum by the precise object-space test, the condition
// of the capture), the texture (residency, CLUT, wrap, batch candidacy) and
// the light (the pick and the spot block in the head). They are written as
// afterFullStamp's refresh writes them.
void StaPipQBufferRenderer::captureRecord(StaPipBag* bag, texbuffer_t* tb,
                                          const RendererCoreSpotLight* light,
                                          bool receive, const texwrap_t* wanted) {
#if TYRA_STAPIP_CAPTURE_RECORD && TYRA_STAPIP_STAMP_PASS
  const int slot = lastCaptureSlot;
  lastCaptureSlot = -1;
  if (slot < 0 || stamps == nullptr || stamps[slot].bag != bag) return;
#if TYRA_STAPIP_CROSSING_ON
  if (CrossPass::xonly[slot]) return;
#endif
  StampEntry* s = &stamps[slot];
  const DrawEpochs::State& g = DrawEpochs::g;
  resolveLightEpoch(rendererCore);
  RendererCoreTexture& T = rendererCore->texture;
  const Texture* tex = static_cast<const Texture*>(s->texture);
  StampRecord& r = stampRecs[slot];
  r.bag = bag;
  r.cam = g.cam;
  r.camSeen = g.cam;
  r.light = g.light;
  r.tex = g.tex;
  r.glob = g.glob;
  r.prog = g.prog;
  r.wanted = wanted;
  r.tb = tb;
  r.lightPtr = light;
  const s32 idx = tex != nullptr ? T.residentIndex(tex) : -1;
  r.allocIdx = static_cast<s16>(idx);
  r.clutAddr = T.clut.address;
  r.clutPsm = static_cast<u8>(T.clut.psm);
  r.clutLoad = static_cast<u8>(T.clut.load_method);
  r.func = tex != nullptr ? bag->texture->textureFunction : 0;
  const bool repeatNow = tex == nullptr ||
                         (tex->getWrapSettings()->horizontal == WRAP_REPEAT &&
                          tex->getWrapSettings()->vertical == WRAP_REPEAT);
  const bool cand = s->batchSized != 0 || (stampBatchAnySize && repeatNow);
  r.flags = static_cast<u8>(
      kRecValid | (s->spotActive ? kRecSpotOn : 0) | (cand ? kRecCand : 0) |
      (tex != nullptr ? kRecTextured : 0) | (receive ? kRecReceive : 0) |
      ((tex != nullptr && idx >= 0 && tex->vramResident == nullptr) ? kRecTexCache
                                                                     : 0) |
      (isIdentityMatrix(s->model) ? kRecIdent : 0));
#else
  (void)bag;
  (void)tb;
  (void)light;
  (void)receive;
  (void)wanted;
  lastCaptureSlot = -1;
#endif
}
#endif

}  // namespace Tyra

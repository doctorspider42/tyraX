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

#include <dma.h>
#include <packet2_utils.h>
#include <memory>
#include <vector>
#include "debug/debug.hpp"
#include "math/m4x4.hpp"
#include "renderer/renderer_settings.hpp"
#include "renderer/models/color.hpp"
#include "./stapip_qbuffer.hpp"
#include "./stapip_program_type.hpp"
#include "./stapip_program_name.hpp"
#include "./stapip_programs_repository.hpp"
#include "./stapip_clipper.hpp"
#include "./stapip_telemetry.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "renderer/core/renderer_core.hpp"
#include "renderer/core/texture/renderer_core_texture_buffers.hpp"

/**
 * Modified by TyraX: retained static geometry COMMAND data
 * (docs/retained-static-commands.md). Compile it out to get exactly the
 * pre-1.96 packet construction back - that is the A/B control arm.
 */
#ifndef TYRA_STAPIP_RETAINED_COMMANDS
#define TYRA_STAPIP_RETAINED_COMMANDS 1
#endif

// The periodic STAPIPRET readout. Separate from the feature and OFF by default:
// it is a timed host: write, and a timed host: write inside a measurement
// window is noise with a period. Build with -DTYRA_STAPIP_RETAINED_REPORT=1.
#ifndef TYRA_STAPIP_RETAINED_REPORT
#define TYRA_STAPIP_RETAINED_REPORT 0
#endif

namespace Tyra {

#if TYRA_STAPIP_RETAINED_COMMANDS

/**
 * Modified by TyraX: one bag's retained VU1 command data.
 *
 * A wholly visible static bag hands VU1 the same DMA/VIF command block every
 * frame: a CNT tag carrying the scale quadword and the prim GIFtag, then one
 * DMA REF tag per vertex stream. None of it depends on the camera - the MVP,
 * the picked light and the visibility classification are the per-frame half
 * and stay per-frame. So the block is CAPTURED the first time it is built, out
 * of the packet the ordinary builders just wrote it into, and replayed with a
 * memcpy afterwards. Capturing rather than re-deriving is what makes the
 * replay byte-identical by construction, for every program variant including
 * a game-supplied one.
 *
 * Two properties make this safe, and any edit must keep both:
 *
 * - **The block is COPIED into the packet, never referenced by DMA.** Its REF
 *   tags name the bag's own vertex arrays exactly as before, so this adds no
 *   new DMA-lifetime exposure at all: the retained storage is EE-private and
 *   the packet is as self-contained as it was. (The slot pool's lifetime rule
 *   is untouched - a copied qbuffer never gets a retained block.)
 * - **The key is every input the block encodes.** Stream pointers, vertex
 *   count, package size, the bag's `bboxVersion`, the resolved VU1 program,
 *   the prim state and the single-colour/strip flags. Anything that moves
 *   rebuilds; a bag whose array was freed and reallocated elsewhere fails the
 *   pointer compare, which is the case a version stamp alone would miss.
 */
struct StaPipRetainedEntry {
  // The key. Compared as a whole; a mismatch discards the blocks.
  const void* vertices;
  const void* sts;
  const void* colors;
  const void* normals;
  const void* program;
  u32 count;
  u32 maxVertCount;
  u32 bboxVersion;
  u32 primKey;
  u32 depthScaleBits;
  u8 singleColor;
  u8 stripped;

  /** Packages = ceil(count / maxVertCount). */
  u16 packages;
  /** Qwords per package block. 0 until the first capture. */
  u16 blockQw;
  /** packages * kMaxBlockQw quadwords; each package's block at its own slot. */
  std::unique_ptr<qword_t[]> data;
  /** One byte per package: has that block been captured yet? */
  std::unique_ptr<u8[]> ready;

  int framesLeftToDestroy;
  int nextInBucket;
};

/**
 * Modified by TyraX: the bag -> retained-block cache. Shaped like
 * StapipBagBBoxesCacher on purpose - a 256-bucket index over vector indices
 * (relocation-safe), a per-entry unused-frame countdown, and a compaction that
 * rebuilds the index. What is different is the hard BYTE cap: a scene with
 * more visible static geometry than the cap simply keeps rebuilding the bags
 * that did not fit, which is slower and always correct.
 */
class StaPipRetainedCommands {
 public:
  /** At most this many qwords in one package block; a program that needs more
   * is never retained. It is 3 for the scale/GIFtag group plus one DMA REF per
   * vertex stream, so the fattest BUILT-IN class is 6 (position + ST + one of
   * colours/normals) and this leaves one stream of headroom. Raising it costs
   * capacity for every entry, not just the fat ones - a slot is reserved
   * before the first capture measures the block. */
  static const u16 kMaxBlockQw = 7;
  /** ~128 KB of EE RAM, i.e. about 1170 VU1 packages - more than the Motor
   * District garage submits in a frame. The cap is a hard budget, not a hint:
   * when it binds, the least recently used entries are dropped to make room
   * (bounded per frame, see kMaxEvictionsPerFrame). */
  static const u32 kMaxQwords = 8192;
  /** Same 5-second retention the bbox cacher uses. */
  static const int kLifetimeFrames = 50 * 5;
  /** How many entries one frame may drop to make room. Without a bound, a
   * scene whose working set genuinely exceeds the cap would evict and
   * re-capture the same bags every frame - strictly worse than simply not
   * retaining them. With it, such a scene settles on the subset that fits. */
  static const int kMaxEvictionsPerFrame = 8;

  StaPipRetainedCommands();

  void onFrameEnd();
  void clear();

  /**
   * The entry for this bag, or nullptr when it cannot be retained (the cap is
   * reached, or the bag has more packages than one entry may hold). A key
   * mismatch invalidates in place rather than allocating a second entry.
   */
  StaPipRetainedEntry* acquire(const StaPipRetainedEntry& key);

  u32 getBytes() const { return usedQwords * 16; }
  u32 takeHits() { const u32 v = hits; hits = 0; return v; }
  u32 takeBuilds() { const u32 v = builds; builds = 0; return v; }
  void countHit() { ++hits; }
  void countBuild() { ++builds; }

 private:
  static const u32 kBucketCount = 256;

  u32 getBucket(const void* vertices, u32 maxVertCount) const;
  void rebuildIndex();
  /** Drop least-recently-used entries until `wanted` quadwords fit. Never
   * takes an entry this frame has already touched, and never more than
   * kMaxEvictionsPerFrame in one frame. */
  bool evictFor(u32 wanted);
  static bool keyMatches(const StaPipRetainedEntry& a,
                         const StaPipRetainedEntry& b);

  std::vector<StaPipRetainedEntry> storage;
  int indexBuckets[kBucketCount];
  u32 usedQwords = 0;
  u32 hits = 0, builds = 0;
  int evictionsThisFrame = 0;
};

#endif  // TYRA_STAPIP_RETAINED_COMMANDS

class StaPipQBufferRenderer {
 public:
  StaPipQBufferRenderer();
  ~StaPipQBufferRenderer();

  void init(RendererCore* t_core, prim_t* prim, lod_t* lod);

  void reinitVU1();

  void setClipperMVP(M4x4* mvp) { clipper.setMVP(mvp); }

  StaPipQBuffer* getBuffer();

  // Modified by TyraX: non-const - also pushes the per-mesh
  // object-space spot light to the EE clipper.
  void sendObjectData(StaPipBag* bag, M4x4* mvp,
                      RendererCoreTextureBuffers* texBuffers);

  // Modified by TyraX: the dynamic light this bag renders with (picked per
  // bag by StaPipCore::render from the flashlight + scene lights). Null =
  // fall back to the global flashlight state.
  void setBagLight(const RendererCoreSpotLight* light) { bagLight = light; }

  void setMaxVertCount(const u32& count);

  void setInfo(PipelineInfoBag* bag);

  /** TyraX addition: null keeps the hot path free of counter/timer reads. */
  void setTelemetry(StaPipTelemetry* value) { telemetry = value; }

  /** Fast render with culling */
  void cull(StaPipQBuffer* buffer);

  /** Slower render with clipping */
  void clip(StaPipQBuffer* buffer);

  /**
   * Modified by TyraX: route clip-classified packages to the VU1 clip
   * program family instead of the EE clipper + as_is programs. Swaps the
   * uploaded program set (micro memory can't hold cull + as_is + clip at
   * once), so call it right after setRenderer() / between frames only.
   */
  void setVU1Clipping(const bool& enabled);

  /** TyraX addition: which MATERIAL CLASSES keep a resident VU1 program
   * (docs/vu-framework.md). VU1 micro memory holds ~2042 instruction slots and
   * the full set of ten sits right under that ceiling, so a project that never
   * draws a lit mesh is paying ~380 instructions for programs it cannot reach.
   * Dropping a class frees that room for a program the user wrote.
   *
   * One bit per class; Color is always kept, because it is what everything else
   * falls back to. Never calling this keeps every class, i.e. exactly the
   * behaviour before it existed. */
  enum StaPipProgramClass {
    StaPipClassColor = 1 << 0,
    StaPipClassDirLights = 1 << 1,
    StaPipClassTextureDirLights = 1 << 2,
    StaPipClassTextureColor = 1 << 3,
    StaPipClassTextureEnv = 1 << 4,
    StaPipClassAll = 0x1F,
  };
  void setResidentClasses(const u32& mask);
  const u32& getResidentClasses() const { return residentClasses; }

  /** TyraX addition: install a game-supplied microprogram over a built-in slot
   * and make it resident (docs/vu-framework.md). Rebuilds the program cache and
   * re-uploads it, so it is safe to call after init - and it must be, because a
   * game only learns about its own programs once its scene is up. */
  void setProgramOverride(const StaPipProgramName& name,
                          StaPipVU1Program* program);
  /** TyraX addition: several overrides, ONE rebuild. Swapping a whole look at
   * run time touches every material class, and doing that through the single
   * setter above would drain the pipeline and re-upload the program cache once
   * PER CLASS. A null entry restores the engine's own program for that slot.
   *
   * Microcode is a u32 range in EE memory and createProgramsCache assigns the
   * micro-memory addresses when it builds the packet, so alternative programs
   * cost EE RAM and nothing in VU1 micro memory - only the active set is ever
   * uploaded. That is what makes swapping a look affordable at all. */
  void setProgramOverrides(const StaPipProgramName* names,
                           StaPipVU1Program* const* programs, u32 count);
  const bool& isVU1ClippingEnabled() const { return vu1Clipping; }

  /** TyraX addition: the two quadwords a project's own microprogram reads
   * (docs/vu-authoring.md) - four numbers the game sets per mesh, and the
   * clock. They land at VU1_CUSTOM_PARAMS_ADDR / VU1_CUSTOM_TIME_ADDR, which
   * are inside the DIRECTIONAL-LIGHTS colour block, so they are uploaded only
   * for a bag with no lighting: a lit bag needs those addresses for its light
   * colours and would be corrupted by them.
   *
   * Off by default. A project with no custom program must not pay two extra
   * unpacked quadwords per mesh for a feature it does not use, so codegen turns
   * this on once at startup and never otherwise. */
  void setVuCustomEnabled(const bool& enabled) { vuCustomEnabled = enabled; }
  void setVuParams(const float& x, const float& y, const float& z,
                   const float& w) {
    vuParams[0] = x, vuParams[1] = y, vuParams[2] = z, vuParams[3] = w;
  }
  /** Seconds, plus its sine and cosine - computed here so a program that only
   * needs the whole mesh to pulse can skip its own 17-instruction series.
   * WRAP the value: the microprogram's range reduction folds through a 2^23
   * add and loses precision long before a float would. */
  void setVuTime(const float& seconds);

  /**
   * Modified by TyraX: particle billboards. The resident program set has no
   * room for the billboard family (the VU1-clipping set fills micro memory
   * to the brim), so the two billboard programs live in their own small
   * packet and are swapped in when a billboard bag renders - the same
   * upload mechanism a StaPip<->DynPip pipeline switch uses every frame.
   * The main set is lazily restored by the next non-billboard bag.
   */
  void ensureProgramSet(const bool& billboard);

  void flushBuffers();

  /**
   * Modified by TyraX: opt one known-owned sequence into bounded submission
   * batching. The caller guarantees that every direct vertex, texture-coordinate,
   * colour and normal stream remains alive and unchanged through the next VIF1
   * synchronization after
   * endSubmissionBatch(); end submits asynchronously and is not a fence.
   * Texture and copy/clip paths force an internal flush.
   */
  void beginSubmissionBatch();
  void endSubmissionBatch();
  void onFrameEnd();
  bool isSubmissionBatchOpen() const { return submissionBatchScope; }
  void setSubmissionBatchCandidate(const bool& candidate,
                                   const bool& textured = false);

  void clearLastProgramName();

  /**
   * Modified by TyraX: open the retained-command scope for one bag, right
   * before its packages are submitted (so after setInfo - the prim state is
   * part of the key). Returns true when this bag's cull-routed packages may
   * carry a retain index; false means every package is built the old way.
   *
   * Only the CULL route is ever retained: a clip buffer's count word carries a
   * plane mask that changes with the camera, and a copied or strip-expanded
   * buffer points into the double-buffered slot pool, whose address is not a
   * property of the bag.
   */
  bool beginRetainedBag(StaPipBag* bag, const u32& packageSize);
  void endRetainedBag();

  /** TyraX diagnostics: how many package command blocks were replayed from
   * retained storage against how many were built, and how much EE RAM the
   * cache holds. Reading the two counters clears them. */
  u32 takeRetainedHits();
  u32 takeRetainedBuilds();
  u32 getRetainedBytes() const;

  StaPipVU1Program* getCullProgramByBag(const StaPipBag* bag);
  bool hasProgramOverrides() const { return repository.hasAnyOverride(); }

  StaPipVU1Program* getCullProgramByParams(const bool& isLightingEnabled,
                                           const bool& isTextureEnabled);

  const u16& getBufferSize() { return bufferSize; }

  void allocateOnUse();
  void deallocateOnUse();

 private:
  prim_t* prim;
  lod_t* lod;

  bool is1stDBufferFlushTime();
  bool is2ndDBufferFlushTime();

  void sendStaticData() const;
  void setProgramsCache();
  void uploadPrograms();
  void setDoubleBuffer();
  u16 getQBufferIndex(StaPipQBuffer* buffer);
  u16 packetSize;

#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
  /**
   * Probe B only (stapip_probes.hpp): any buffer committed into the packet
   * currently being built had its streams written into the qbuffer copy pool,
   * so this send must keep the whole-data-cache write-back. Set in
   * addBuffersDataToPacket, consumed and cleared in sendPacket.
   */
  bool probePacketUsesPool;
#endif

  static const u16 buffersCount;

  StaPipVU1Program* getProgramByName(const StaPipProgramName& name);
  void addBuffersDataToPacket(const u32& from, const u32& to,
                              const bool& finalize = true);
  // Modified by TyraX: the per-mesh VU1 clipping uniform chain, lifted out of
  // sendObjectData so the retained-command path can capture and replay it.
  void addClipChain(packet2_t* objectDataPacket) const;
  void sendPacket();
  void flushPendingPacket();
  static void textureMutationBarrier(void* context);
  void beforeTextureMutation();
  StaPipVU1Program* getAsIsProgramByBag(const StaPipBag* bag);
  // Modified by TyraX: VU1 clipping.
  StaPipVU1Program* getClipProgramByBag(const StaPipBag* bag);
  StaPipVU1Program* getCullProgramByType(const StaPipProgramType& programType);
  StaPipProgramType getDrawProgramTypeByBag(const StaPipBag* bag) const;
  StaPipProgramType getDrawProgramTypeByParams(
      const bool& isLightingEnabled, const bool& isTextureEnabled) const;
  packet2_t* programsPacket;
  // Modified by TyraX: on-demand billboard program set (see
  // ensureProgramSet).
  packet2_t* billboardProgramsPacket;
  bool billboardSetActive = false;

  packet2_t** packets;
  StaPipVU1Program** dBufferPrograms;
  StaPipQBuffer** buffers;
  packet2_t* staticDataPacket;
  // Modified by TyraX: uniforms already occupy the current geometry packet;
  // append the first buffer flush instead of resetting that packet.
  bool objectDataPending = false;
  bool submissionBatchScope = false;
  bool submissionBatchCandidate = false;
  u8 submissionBatchBags = 0;
  bool submissionPacketHasTexture = false;
  bool submissionTextureReadersOutstanding = false;

  RendererCore* rendererCore;

  StaPipProgramName lastProgramName;
  Path1* path1;
  StaPipClipper clipper;
  StaPipProgramsRepository repository;
  /** TyraX addition: see setResidentClasses. */
  u32 residentClasses = StaPipClassAll;
  /** TyraX addition: see setVuCustomEnabled. */
  bool vuCustomEnabled = false;
  float vuParams[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  float vuTime[4] = {0.0F, 0.0F, 1.0F, 1.0F};
  /** TyraX addition: the requested program's class is not resident - walk down
   * to one that is, rather than MSCAL-ing to an address nothing was uploaded
   * to. A dropped class then draws in a simpler style instead of tearing the
   * screen, which is the right failure for something the editor is supposed to
   * have proven unnecessary in the first place. */
  StaPipProgramName residentFallback(const StaPipProgramName& name) const;

  u16 bufferSize, nextBufferIndex, currentBufferIndex;
  // Modified by TyraX: VU1 buffer capacity, used by clip() to drain the
  // clipper output in buffer-sized chunks.
  u32 maxVertCount = 0;
  u8 context;
  // Modified by TyraX: VU1 clipping mode + the clip-space constants the
  // clip programs consume (see VU1_CLIP_CONSTS_ADDR / VU1_CLIP_PLANES_ADDR).
  bool vu1Clipping = false;
  float clipNearZ = 0.0F, clipFarZ = 0.0F;
  // Modified by TyraX: per-bag dynamic light (see setBagLight).
  const RendererCoreSpotLight* bagLight = nullptr;
  // Modified by TyraX: opt-in routing/VU1 back-pressure telemetry.
  StaPipTelemetry* telemetry = nullptr;

#if TYRA_STAPIP_RETAINED_COMMANDS
  // Modified by TyraX: retained command data (see StaPipRetainedCommands).
  StaPipRetainedCommands retained;
  /** The bag currently being submitted, or nullptr. Every buffer in a flush
   * belongs to one bag - flushBuffers resets the slot indices per bag - so one
   * pointer is enough to resolve a buffer's retainIndex. */
  StaPipRetainedEntry* retainedCurrent = nullptr;

  /**
   * The VU1 clip constants and the six clip planes, captured once. They are
   * uploaded per mesh and are the same fifteen quadwords every time: their
   * only inputs are the renderer's near/far and the guard-band constant. 52
   * float stores per bag became a memcpy, and the block is thrown away
   * whenever those inputs could have moved (init, setVU1Clipping).
   */
  qword_t clipBlock[16] __attribute__((aligned(16)));
  u16 clipBlockQw = 0;
#endif
};

}  // namespace Tyra

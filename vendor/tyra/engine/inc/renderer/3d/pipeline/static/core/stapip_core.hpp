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

#include <tamtypes.h>
#include <memory>
#include "./stapip_strip_subranges.hpp"
#include "./bag/stapip_bag.hpp"
#include "./bag/packaging/stapip_bag_packages_bbox.hpp"
#include "./bag/packaging/stapip_bag_package.hpp"
#include "./bag/packaging/stapip_bag_packager.hpp"
#include "./stapip_qbuffer_renderer.hpp"
#include "./stapip_telemetry.hpp"
#include "./stapip_bag_bboxes_cacher.hpp"
#include "renderer/3d/pipeline/shared/pipeline_frustum_culling.hpp"

namespace Tyra {

// Modified by TyraX: synchronous per-draw suppression for shared info bags.
// FromInfo preserves the descriptor's opt-in; Suppress never changes it.
enum class StaPipLightReception : u8 { FromInfo, Suppress };

class StaPipCore {
 public:
  /**
   * Modified by TyraX: bounded batching for caller-owned immutable bag
   * streams. Every begin must be paired with end; referenced streams remain
   * immutable until the renderer's next VIF1 synchronization.
   */
  void beginSubmissionBatch() { qbufferRenderer.beginSubmissionBatch(); }
  void endSubmissionBatch() {
    flushDeferred();
    qbufferRenderer.endSubmissionBatch();
  }
  /** Modified by TyraX: stamp deferral (docs/stapip-stamps.md, "Stamps in
   * runs"). Inside a submission batch, a bag that has a stamp is queued and
   * the queue is replayed in one tight run - at the batch's end, before any
   * bag that takes the ordinary path, when the view changes or the queue is
   * full - so the stamp path's code and data stay in cache instead of being
   * evicted by the game code between two bags. StaPip's own draw order is
   * unchanged. Default on; false submits every bag at its call. */
  void setStampDeferral(bool on) {
    flushDeferred();
    stampDeferral = on;
  }
  /** Replays the queued stamps now. Cheap when the queue is empty. */
  void flushDeferred() {
#if TYRA_STAPIP_STAMP
    if (deferredCount != 0) flushDeferredRun();
#endif
  }
#if TYRA_STAPIP_STAMP
  /** Modified by TyraX: bag stamps (docs/stapip-stamps.md). */
  void setStampEnabled(bool on) {
    flushDeferred();
    qbufferRenderer.setStampEnabled(on);
  }
  void setStampBatchAnySize(bool on) { qbufferRenderer.setStampBatchAnySize(on); }
  /** Modified by TyraX (R1): chain stamps - see StaPipQBufferRenderer. */
  void setStampChain(bool on) {
    flushDeferred();
    qbufferRenderer.setStampChain(on);
  }
  /** Modified by TyraX: StaPipBag::stampSlot as the first lookup. */
  void setStampSlotHint(bool on) { qbufferRenderer.setStampSlotHint(on); }
  void takeReplayStats(u32* chain, u32* whole, u32* runs) {
    qbufferRenderer.takeReplayStats(chain, whole, runs);
  }
  u32 takeStampHits() { return qbufferRenderer.takeStampHits(); }
  u32 takeStampCaptures() { return qbufferRenderer.takeStampCaptures(); }
#endif
  /** Modified by TyraX: slice clipping (docs/vu1-clipping.md, "Slice clipping"). With VU1
   * clipping on, a frustum-crossing package of a list bag is cut into
   * clipPackageSize slices BY POINTER and each slice goes to the VU1 clip
   * program, which decides inside / outside / cut per triangle. The old
   * route re-classified every slice on the EE (a bbox walk over its
   * vertices) and COPIED the inside ones together into the copy pool, every
   * frame. Default on; false restores it for an A/B. */
  void setSliceClip(bool on) { sliceClip = on; }
  /** Modified by TyraX: a bag that crosses the frustum joins the open
   * submission batch too (docs/stapip-stamps.md, "Batching"). It used to close
   * and submit a packet of its own - ~70 us a bag on a physical PS2, mostly the
   * VIF1 queue's per-packet copy and tag walk. It stays pending only when it
   * left nothing in the slot copy pools and sent nothing mid-bag; otherwise it
   * sends at its end exactly as before. VU1 clipping only. Default on; false
   * restores the old rule for an A/B. */
  void setPartialBatch(bool on) { partialBatch = on; }
  /** Modified by TyraX: whole-package clipping (docs/vu1-clipping.md, "A
   * whole package, streamed"). With VU1 clipping on, a frustum-crossing
   * package - list or strip - goes to the clip program WHOLE and by pointer;
   * the program streams its output with mid-program XGKICKs. False restores
   * the slice / strip-subrange routes for an A/B. Default on. */
  void setStreamClip(bool on) { streamClip = on; }
  /** Modified by TyraX (R5): a frustum-crossing bag replayed from immutable
   * blocks (StaPipQBufferRenderer::beginPartialReplay). 0 = the qbuffer loop;
   * 1 = runs - per-package classification kept, each run of same-route
   * packages one REF into its baked cull or whole-clip blocks; 2 = whole -
   * the bag is ONE REF into the streamed VU1 clip program with all six planes,
   * no EE classification (VU1 judges every triangle); 3 = whole, and the bag
   * takes a stamp (its head is replayed too while it keeps crossing). VU1
   * clipping with whole-package streaming only. See
   * docs/tyrax2-arch-audit-ee-submission-2026-10-20.md, "R5". */
  void setPartialReplay(u8 mode) {
    flushDeferred();
    partialReplay = mode;
#if TYRA_STAPIP_STAMP
    qbufferRenderer.setClipStamps(mode == 3);
#endif
  }
  StaPipCore();
  ~StaPipCore();

  void init(RendererCore* t_core);

  void onFrameEnd();
#if TYRA_STAPIP_PREPARED_STATIC
  // Modified by TyraX: private bounded scene-residency trial, outside recording.
  bool prepareStaticGeometry(StaPipBag* bag);
  void releasePreparedGeometry() { qbufferRenderer.releasePreparedGeometry(); }
#endif

  /** Render 3D via "bags" */
  void render(StaPipBag* bag,
              StaPipLightReception reception = StaPipLightReception::FromInfo);

  /** Get max vert count of VU1 qbuffer (for optimizations) */
  u32 getMaxVertCountByParams(const bool& isSingleColor,
                              const bool& isLightingEnabled,
                              const bool& isTextureEnabled);

  /** Get max vert count of VU1 qbuffer (for optimizations) */
  u32 getMaxVertCountByBag(const StaPipBag* bag);

  /**
   * - Uploads standard VU1 programs.
   * - Sends static "Tyra Renderer3D" VU1 data.
   * - Sets double buffers exactly for "Tyra Renderer3D"
   * Should be called if VU1 was used by your non standard programs.
   */
  void reinitVU1Programs();

  /**
   * Modified by TyraX: clip frustum-crossing packages on VU1 (clip
   * program family) instead of the EE clipper. Call right after
   * setRenderer() or between frames.
   */
  void setVU1Clipping(const bool& enabled);

  bool supportsPaintVu(const StaPipBag* bag) const {
    return qbufferRenderer.supportsPaintVu(bag);
  }

  /** TyraX addition: which material classes keep a resident VU1 program
   * (docs/vu-framework.md). Safe at run time: a level that stops needing, say,
   * the matcap class can hand that micro memory to something else. It costs a
   * pipeline drain and an upload, so call it at a zone or level boundary. */
  void setResidentClasses(const u32& mask) {
    qbufferRenderer.setResidentClasses(mask);
  }
  u32 getResidentClasses() const { return qbufferRenderer.getResidentClasses(); }

  /** TyraX addition: install a game-supplied VU1 microprogram over a built-in
   * slot (docs/vu-framework.md). Reachable from game code as
   * engine.renderer.renderer3D.staticPipeline.core.setProgramOverride(...). */
  void setProgramOverride(const StaPipProgramName& name,
                          StaPipVU1Program* program) {
    flushDeferred();
    qbufferRenderer.setProgramOverride(name, program);
  }
  /** TyraX addition: swap a whole set at once - one pipeline drain and one
   * upload instead of one per class. A null entry restores the built-in. */
  void setProgramOverrides(const StaPipProgramName* names,
                           StaPipVU1Program* const* programs, u32 count) {
    flushDeferred();
    qbufferRenderer.setProgramOverrides(names, programs, count);
  }

  /** TyraX addition: the per-mesh numbers and the clock a project's own
   * microprogram reads (docs/vu-authoring.md). setVuParams is set before each
   * mesh is submitted - it is renderer state, exactly like the fog or the
   * flashlight, not a field on the bag - and only reaches VU1 for a bag with
   * no lighting, because the two quadwords live in the lights-colour block. */
  void setVuCustomEnabled(const bool& enabled) {
    flushDeferred();
    qbufferRenderer.setVuCustomEnabled(enabled);
  }
  void setVuParams(const float& x, const float& y, const float& z,
                   const float& w) {
    flushDeferred();
    qbufferRenderer.setVuParams(x, y, z, w);
  }
  void setVuTime(const float& seconds) { qbufferRenderer.setVuTime(seconds); }

  /**
   * TyraX diagnostics: opt in to package routing, active-plane and VIF1/VU1
   * wait counters. Disabled by default, so release games keep the original
   * AABB early-out and perform no COP0 timing reads. `takeTelemetry` returns
   * the accumulated interval and resets it.
   */
  void setTelemetryEnabled(const bool& enabled);
  void setTelemetryProducer(const StaPipTelemetryProducer& producer) {
#if TYRA_STAPIP_PACKET_PROFILE
    if (telemetryEnabled) telemetry.producer = static_cast<u8>(producer);
#else
    (void)producer;
#endif
  }
  bool isTelemetryEnabled() const { return telemetryEnabled; }
  StaPipTelemetry takeTelemetry();

  /**
   * TyraX diagnostics: retained static command data
   * (docs/retained-static-commands.md). How many VU1 package command blocks
   * were REPLAYED from retained storage since the last read, how many were
   * built, and how much EE RAM the cache holds. Always compiled - they are
   * three loads - and always zero when the feature is compiled out, so a
   * game's HUD can print them in either arm.
   */
  u32 takeRetainedCommandHits() {
    return qbufferRenderer.takeRetainedHits();
  }
  u32 takeRetainedCommandBuilds() {
    return qbufferRenderer.takeRetainedBuilds();
  }
  u32 getRetainedCommandBytes() const {
    return qbufferRenderer.getRetainedBytes();
  }

  void allocateOnUse() { qbufferRenderer.allocateOnUse(); }
  void deallocateOnUse() { qbufferRenderer.deallocateOnUse(); }

 private:
  // Modified by TyraX: the stamp deferral queue (setStampDeferral).
  struct DeferredBag {
    StaPipBag* bag;
    StaPipLightReception reception;
  };
  static const u32 kDeferredMax = 48;
  DeferredBag deferred[kDeferredMax];
  u32 deferredCount = 0;
  bool stampDeferral = true;
  bool flushingDeferred = false;
  M4x4 deferredViewProj;
  void flushDeferredRun();
  void renderNow(StaPipBag* bag, StaPipLightReception reception);
  std::unique_ptr<StaPipStripSubranges::Cache> stripSubranges;
  void setPrim();
  void setLod();

  /** Modified by TyraX: experiment (c), TYRA_VU1_EXP_ENV_NORMALIZED
   * (stapip_vu1_experiments.hpp) - normalize an env bag's normal array in
   * place, once. Remembered by array pointer, count, bboxVersion and
   * contentVersion in a small direct-mapped table; a miss only costs a
   * repeated (idempotent) normalize. */
  void ensureEnvNormalsUnit(StaPipBag* bag);
  struct EnvNormalsSeen {
    const void* coordinates = nullptr;
    u32 count = 0, bboxVersion = 0, content = 0;
  };
  EnvNormalsSeen envNormalsSeen[32];

  prim_t prim;
  lod_t lod;

  u32 maxVertCount;
  RendererCore* rendererCore;
  StapipBagBBoxesCacher cacher;

  void setMaxVertCount(const u32& count);
  // Modified by TyraX: VU1 clipping.
  u32 clipDivisor() const;
  u32 clipPackageSize() const;
  // Modified by TyraX: frustum planes in the current bag's object space -
  // computed once per render(), shared by the main-bbox check and every
  // package classification in the packager.
  Plane objectSpacePlanes[6];
  // Consecutive material bags of one model share the same model matrix and
  // camera. Cache their expensive plane transform and MVP for this frame.
  bool transformCacheValid = false;
  bool transformCachePlanesValid = false;
  const M4x4* transformCacheModelPtr = nullptr;
  M4x4 transformCacheModel;
  M4x4 transformCacheViewProj;
  M4x4 transformCacheMvp;
  Plane transformCacheObjectSpacePlanes[6];
  // Modified by TyraX: EIGHT entries. 0..5 are the VU1 clip planes (near, far
  // and the X/Y guard band) - the only ones uploaded to VU1 and the only ones
  // the clip mask covers. 6..7 are the EXACT near/far pair (|z| <= w), which
  // exists on the EE alone, to answer whether a package may take the cull
  // path: that program's clipw judgement tests z against +/-w, not against
  // the guard band's near/far constants.
  Plane clipObjectSpacePlanes[8];
  void computeClipObjectSpacePlanes(const M4x4& mvp);
  void computeClipPlanesInto(const M4x4& mvp, Plane* dst);
#if TYRA_STAPIP_STAMP
  // Modified by TyraX: world-space clip planes for stamps, keyed by the VP.
  M4x4 stampClipViewProj;
  Plane stampClipPlanes[8];
  bool stampClipValid = false;
#endif
  // Modified by TyraX: a package that leaves the VIEW frustum but stays inside
  // the guard band needs no clipping at all - the GS scissor crops it. See
  // the comment on the definition.
  bool isGuardBandOnly(const StaPipBagPackage& package) const;
  StaPipBagPackager packager;
  StaPipQBufferRenderer qbufferRenderer;
  // Modified by TyraX: may this bag's cull-routed packages carry a retained
  // command block? Set once per render() and read by the package loops.
  bool retainCurrentBag = false;
  bool telemetryEnabled = false;
  StaPipTelemetry telemetry;
  void recordPackage(const StaPipBagPackage& package,
                     const CoreBBoxFrustum& route);
  void recordGuardBandPackage(const StaPipBagPackage& package);
  void recordOutsideBag(const StaPipBag* bag);
  void renderPkgs(StaPipBagPackage* packages, const bool& doClip, u16 count);
  /**
   * Modified by TyraX: the partial-frustum route for a STRIPPED bag
   * (StaPipBag::stripped). Its packages are the baked strip RUNS and must
   * never be sub-split - a 1/3 subpackage of a strip is not a strip, and the
   * fillByCopy* merges would fuse two of them. A package that needs real
   * clipping is expanded back into a triangle list instead.
   */
  void renderStrippedPkgs(StaPipBagPackage* packages, const bool& doClip,
                          u16 count);
  void renderSubpkgs(StaPipBagPackage* packages, u16 count);
  // Slice clipping (setSliceClip): [offset, offset + count) of the bag to the
  // VU1 clip program by pointer, in clipPackageSize slices.
  void renderClipSlices(StaPipBag* bag, u32 offset, u32 count, u8 planeMask);
  bool sliceClip = true;
  bool partialBatch = true;
  bool streamClip = true;
  u8 partialReplay = 0;
  bool wholeClip() const {
    return streamClip && qbufferRenderer.isVU1ClippingEnabled();
  }
};

}  // namespace Tyra

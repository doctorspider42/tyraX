/*
# Added by TyraX: render-submission ATTRIBUTION counters
# (docs/render-submission-attribution.md).
#
# The three shipped telemetry brackets - bounds, prepare, dispatch - do not
# add up to render submission. On the Motor District garage-day pose they
# account for 22.2 ms of 29.7, and nothing measured the other 7.5. These
# counters close that gap: one bracket around the WHOLE of StaPipCore::render
# (so "inside render but inside no bracket" becomes a subtraction rather than
# a guess) and a split of `prepare` into the six things it actually does.
#
# A second round then split the two buckets that survived that attribution as
# the largest unopened boxes in the frame: `bounds` (five children plus the
# bbox cacher's own counters, including the per-frame expiry scan that lives
# OUTSIDE render() and had therefore never been measured at all) and the
# package creation and classification residual inside `dispatch`.
#
# ZERO-COST RULE. This is diagnostic code and it must not ship
# (docs/devkit.md). `#ifndef NDEBUG` is NOT the gate in this engine: a game
# build NEVER defines NDEBUG - the release profile only drops -g and sets
# KEEPSYM=0 - and a GS VRAM census that made exactly that mistake shipped live
# and cost about 1 ms a frame. So the gate is an explicit opt-in macro that
# defaults to 0, like TYRA_FRAME_PROFILE. At 0 not one field, not one COP0
# read and not one branch of any of this is compiled.
#
# The engine and the generated game both read THIS header, so one edit moves
# both halves - or pass -DTYRA_STAPIP_ATTRIB=1 to both builds.
*/

#pragma once

#ifndef TYRA_STAPIP_ATTRIB
#define TYRA_STAPIP_ATTRIB 0
#endif

#if TYRA_STAPIP_ATTRIB

#include <tamtypes.h>

namespace Tyra {

/**
 * Attribution counters, in EE COP0 Count ticks (294.912 MHz; divide by
 * 294912 for milliseconds). Accumulated until StaPipCore::takeTelemetry(),
 * which clears them with the rest of StaPipTelemetry.
 *
 * They are gathered only while telemetry is ENABLED, exactly like the three
 * existing brackets, so the reader is the same one-line opt-in.
 *
 * READ THEM AS A SUBTRACTION, in this order:
 *
 *   renderTicks                                    the whole of render()
 *     - boundsTicks - prepareTicks - dispatchTicks the three shipped brackets
 *     = headTicks + tailTicks                      what no bracket covered
 *
 * and inside prepare:
 *
 *   prepareTicks - (packager + texture + program + light + blss + objectData)
 *     = the arithmetic between them (the MVP multiply, the transform-cache
 *       store, the clip-plane transform)
 *
 * and inside bounds:
 *
 *   boundsTicks - (bdSize + bdXform + bdCache + bdPlanes + bdMain)
 *     = the branches between them
 *
 * and inside dispatch, where the four EXISTING nested buckets (packet
 * construction, the send bracket, VIF1 wait, GIF wait) are what the
 * "package creation and classification" residual was defined as the
 * complement of:
 *
 *   dispatchTicks - (dsRetain + dsDirect + dsCreate + dsRender + dsFlush)
 *     = the route decision
 *
 * with dsDirect / dsRender / dsFlush INCLUSIVE of that nested packet work,
 * so the EE's own share of each is that bracket minus its nested part.
 *
 * Brackets NEST but do not overlap each other at one level, so a level sums
 * to at most its parent. `renderCalls` is the denominator for every per-bag
 * figure and is the only honest way to say "about 120 bags".
 */
struct StaPipAttrib {
  /** StaPipCore::render, first instruction to last. The parent of all. */
  u32 renderTicks = 0;
  /** Entry to the `bounds` bracket: the fog decision, the frustum-culling
   * read, and the THIRTEEN TYRA_ASSERTs - which are live in a release game,
   * because nothing in a game build defines NDEBUG. */
  u32 headTicks = 0;
  /** The `dispatch` bracket's end to the return: the clamped-wrap restore. */
  u32 tailTicks = 0;
  /** How many times render() was entered (bags SUBMITTED, including the ones
   * that leave early on an empty count or an outside-frustum bbox). */
  u32 renderCalls = 0;
  /** Bags that returned early from the bounds bracket (outside the frustum).
   * Their whole cost is headTicks + boundsTicks and none of the rest. */
  u32 renderCallsCulled = 0;

  /* --- the six parts of `prepare` ------------------------------------- */

  /** packager.setRenderBBox + setObjectSpacePlanes, the MVP multiply (or its
   * reuse from the transform cache), the transform-cache store, and
   * computeClipObjectSpacePlanes + setClipObjectSpacePlanes when the bag is
   * clip-classified. NOTE the two `memcmp`s the transform cache compares are
   * NOT here - they run before the bounds bracket closes and are charged to
   * `boundsTicks`. */
  u32 prepPackagerTicks = 0;
  /** The submission-batch candidacy test, the VRAM residency lookup, the wrap
   * settings, useTexture(), and the clamped-bag drain when one is needed. */
  u32 prepTextureTicks = 0;
  /** ensureProgramSet + clearLastProgramName - the program-name comparisons
   * and, on a billboard boundary, the micro-memory upload. */
  u32 prepProgramTicks = 0;
  /** The world bounding sphere, pickDynLight and setBagLight. */
  u32 prepLightTicks = 0;
  /** The BLSS bag-proxy feed. Zero whenever the upscaler wants no proxies. */
  u32 prepBlssTicks = 0;
  /** sendObjectData + setClipperMVP + setInfo - the per-bag uniform packet. */
  u32 prepObjectDataTicks = 0;

  /* --- one hole inside `dispatch` ------------------------------------- */

  /** `dma_channel_wait(GIF)` in sendPacket - the "wait for texture" of issue
   * #182. It sits BETWEEN the vu1Wait bracket and the dmaSubmit bracket and is
   * inside neither, so until now it was indistinguishable from the package
   * creation and classification that the dispatch residual is attributed to.
   * A non-trivial value here means the frame is waiting on PATH3, not on the
   * EE. */
  u32 gifWaitTicks = 0;

  /* --- the five parts of `bounds` -------------------------------------- *
   *
   * Two hypotheses about this bracket have already been measured and BOTH
   * were wrong, so the point of this split is to stop guessing which part of
   * it is the cost:
   *
   *   1. NOT the arithmetic. `frustumCheckAABB` was made branchless
   *      (host-oracle-verified bit-equivalent over 6 000 000 cases) and the
   *      bucket moved 4.132 -> 4.073 ms.
   *   2. NOT the merge loop's stride. Compacting corners 0 and 7 into a
   *      contiguous `partBounds` moved it 4.073 -> 4.051 ms.
   *
   * Together: 2% of the bucket for two careful attacks. So the cost is in
   * one of the five children below, and the remaining suspect is the cacher.
   *
   * These SUM to `boundsTicks` minus the arithmetic between them; read the
   * remainder as the residual, exactly like `prepare`.
   */

  /** `getMaxVertCountByBag` + `setMaxVertCount` - the package-size
   * derivation, including the `packageSize` pin and the program-class lookup
   * behind `getCullProgramByParams`. Split into the two below. */
  u32 bdSizeTicks = 0;
  /** The half of `bdSizeTicks` that RESOLVES THE PROGRAM:
   * `getCullProgramByBag` -> `getDrawProgramTypeByBag` -> `getCullProgramByType`
   * -> `getProgramByName`, including the four-step `residentFallback` walk and
   * the repository lookup. Pure branching on fields the bag already has. */
  u32 bdProgTicks = 0;
  /** The other half: `StaPipVU1Program::getMaxVertCount` (TWO integer
   * divisions - `/= (colorElements + reglistCount)`, then `/3`) plus the
   * `packageSize` pin's `(packageSize / 3) * 3`, plus `setMaxVertCount`'s
   * three stores. The EE has no integer divide unit worth the name, so this
   * is the half a lookup table would remove. */
  u32 bdSizeCalcTicks = 0;
  /** The transform cache's key test: `getViewProj()` plus the TWO 64-byte
   * `memcmp`s (model matrix and view-projection). Charged here rather than to
   * `prepPackagerTicks`, which covers the STORE side of the same cache. */
  u32 bdXformTicks = 0;
  /** `StapipBagBBoxesCacher::getBBoxes` - the hashed lookup, the bucket walk,
   * the version/count checks, and whatever recompute or allocation they ask
   * for. The counters below say WHICH of those three happened. */
  u32 bdCacheTicks = 0;
  /** The six object-space frustum planes: either copied from the transform
   * cache or built by `CoreBBox::computeObjectSpacePlanes`. */
  u32 bdPlanesTicks = 0;
  /** The ONE `frustumCheckAABB` on the bag's main bounding box - the test
   * that rejects half the bags in the frame. Per BAG, not per package. */
  u32 bdMainTicks = 0;

  /* --- what the bbox cacher actually did -------------------------------- *
   *
   * Counts, not ticks, and they are the denominator for `bdCacheTicks`. A
   * frame whose cache is all hits and whose `bdCacheTicks` is still large is
   * a lookup problem; one with many recalculations is a `bboxVersion`
   * problem in the CALLER, which is a different fix entirely.
   */

  /** Entry found, `version` and vertex count both matched - the pure-lookup
   * path, which allocates and computes nothing. */
  u32 bboxCacheHits = 0;
  /** Entry found but stale: same vertex count, so the boxes were rebuilt in
   * place by `StaPipBagPackagesBBox::recalculate`. This is what a caller
   * bumping `bboxVersion` every frame costs - see `renderVehicleWheels`. */
  u32 bboxCacheRecalcs = 0;
  /** A whole `StaPipBagPackagesBBox` was constructed: either no entry existed
   * (first sight of this buffer), or the vertex count changed and the entry's
   * boxes were replaced. Both ALLOCATE. */
  u32 bboxCacheFresh = 0;
  /** Bucket-walk steps taken by `getCache` across the frame. Divided by
   * `bboxCacheHits + recalcs` this is the average chain length, which is the
   * only way to tell a hash that is doing its job from one that is not. */
  u32 bboxCacheProbes = 0;
  /** Ticks spent INSIDE the recompute branch - `recalculate()` or a fresh
   * `StaPipBagPackagesBBox` - and nothing else. `bdCacheTicks` minus this is
   * the pure lookup, which is the only way to tell "the hash is slow" from
   * "a caller is invalidating its boxes every frame". The two answers have
   * completely different fixes and the first round could not separate them:
   * garage day spends 5x more per lookup than outer day while the probe
   * depth is identical (1.42 against 1.50), which is already a recompute
   * signature rather than a lookup one. */
  u32 bboxCacheRecalcTicks = 0;
  /** `storage.size()` at the last frame end - how many entries the 250-frame
   * retention is holding. Not accumulated; overwritten each frame. */
  u32 bboxCacheEntries = 0;
  /** `StapipBagBBoxesCacher::onFrameEnd` - the per-frame expiry scan over
   * every entry, plus the `remove_if` compaction and index rebuild when one
   * expires. This is OUTSIDE `render()` and therefore outside `boundsTicks`
   * entirely, which is exactly why it has never been measured. */
  u32 bboxCacheFrameEndTicks = 0;

  /* --- inside `dispatch`: the package creation and classification box ---- *
   *
   * `dispatch` minus (packet construction + the send bracket + VIF1 wait +
   * GIF wait) is about 56% of the bucket and nothing has ever looked inside
   * it. The name it was given - "package creation and classification" - is
   * only half right, and the split below is what shows that: a WHOLLY
   * VISIBLE bag never calls `packager.create` at all, so for those bags the
   * residual is the direct fill-and-cull loop and no classification happens.
   *
   * These brackets are INCLUSIVE of the nested packet/send/wait work that
   * `cull()` and `flushBuffers()` perform, because that work is reached from
   * inside them. Subtract the four existing nested buckets to get the EE's
   * own share; `dsClassifyTicks` is the one bracket here that is exclusive.
   */

  /** `beginRetainedBag` - the retained-command key comparison (stream
   * pointers, count, package size, bboxVersion, program pointer, prim state,
   * Z scale) and the hashed entry lookup behind it. */
  u32 dsRetainTicks = 0;
  /** The wholly-visible route: the whole `getBuffer` / `fillByPointer` /
   * `cull` loop. Inclusive of the half-buffer flushes `cull` triggers. */
  u32 dsDirectTicks = 0;
  /** `StaPipBagPackager::create` - building the pooled package descriptors
   * AND classifying them. Inclusive of `dsClassifyTicks`. */
  u32 dsCreateTicks = 0;
  /** `StaPipBagPackager::checkFrustum` alone, summed over every package and
   * subpackage: the merged min/max read plus the AABB test and the
   * eight-plane clip mask. EXCLUSIVE - this is the "classification" half of
   * the box's name, measured on its own at last. */
  u32 dsClassifyTicks = 0;
  /** `renderPkgs` / `renderStrippedPkgs` / `renderSubpkgs` - the partial
   * route's per-package submission, inclusive of subpackage splits, qbuffer
   * copies, strip expansion and the clipper. */
  u32 dsRenderTicks = 0;
  /** `flushBuffers` - the end-of-bag flush, inclusive of packet construction
   * and the send it performs. */
  u32 dsFlushTicks = 0;

  /** Bags that took the wholly-visible direct route (no classification). */
  u32 dsDirectBags = 0;
  /** Bags that took the partially-in-frustum route (classification runs). */
  u32 dsPartialBags = 0;
  /** Package descriptors `packager.create` produced this frame, both
   * overloads. The denominator for `dsCreateTicks` and `dsClassifyTicks`. */
  u32 dsPackages = 0;
  /** Parts visited by `getMergedMinMax` across every classification. Divided
   * by `dsPackages` this is the merge loop's average trip count, which says
   * whether the classification is the WALK over the part bounds or the plane
   * arithmetic after it - the previous round compacted that walk's stride and
   * moved the bucket 0.5%, so its length is the thing still unmeasured. */
  u32 dsMergeParts = 0;
  /** Classifications that ran the SECOND pass over the box,
   * `activePlaneMaskAABB` with eight planes, to derive the VU1 clip mask and
   * the guard-band answer. Only a PARTIALLY_IN_FRUSTUM package pays it, so
   * this is how much of `dsClassifyTicks` is the clip-mask half. */
  u32 dsMaskCalls = 0;
};

}  // namespace Tyra

#endif  // TYRA_STAPIP_ATTRIB

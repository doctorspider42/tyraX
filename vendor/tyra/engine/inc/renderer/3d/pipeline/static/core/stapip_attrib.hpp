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
};

}  // namespace Tyra

#endif  // TYRA_STAPIP_ATTRIB

/*
# Modified by TyraX: opt-in static-pipeline routing and VU1 back-pressure
# telemetry. Kept in a standalone header so StaPipCore and the qbuffer renderer
# share the counters without exposing either implementation to the other.
*/

#pragma once

#include <tamtypes.h>

#include "debug/frame_profile.hpp"
#include "./stapip_attrib.hpp"
#include "./stapip_probes.hpp"

#ifndef TYRA_STAPIP_PACKET_PROFILE
// Level 1 of TYRA_FRAME_PROFILE only - level 2 is the transparent timing
// mode and this walker is the reason level 1 is not (debug/frame_profile.hpp).
#define TYRA_STAPIP_PACKET_PROFILE (TYRA_FRAME_PROFILE == 1)
#endif

namespace Tyra {

/**
 * Structural cost of the finished VIF1/GIF stream. Unlike the timing fields
 * below these are deterministic counts, so a noisy hardware run can still
 * say which producer is paying for repeated command/state traffic.
 */
#if TYRA_STAPIP_PACKET_PROFILE
struct StaPipPacketCounters {
  u32 dmaTags[8] = {};
  u32 chainQwords = 0;
  u32 refPayloadQwords = 0;
  u32 inlinePayloadQwords = 0;
  u32 refsAligned128 = 0;
  u32 refsUnaligned128 = 0;
  u32 vifWords = 0;
  u32 vifNops = 0;
  u32 vifStcycl = 0;
  u32 vifStrow = 0;
  u32 vifStcol = 0;
  u32 vifUnpack = 0;
  u32 vifFlush = 0;
  u32 vifFlushe = 0;
  u32 vifFlusha = 0;
  u32 vifMscal = 0;
  u32 vifMscalf = 0;
  u32 vifMscnt = 0;
  u32 gifTags = 0;
  u32 adWrites = 0;
  u32 gsPayloadQwords = 0;
  u32 xgkicks = 0;
  /** Supported non-clip textured packages that reused their bag's GS state. */
  u32 gsStateReuses = 0;
  u32 malformedChains = 0;
};
#endif

enum StaPipTelemetryProducer {
  StaPipProducerMixed = 0,
  StaPipProducerSky,
  StaPipProducerTerrain,
  StaPipProducerStaticBatches,
  StaPipProducerRoads,
  StaPipProducerProcedural,
  StaPipProducerObjects,
  StaPipProducerEffects,
  StaPipProducerCount
};

/**
 * Diagnostic counters accumulated until StaPipCore::takeTelemetry().
 *
 * Timing values are EE COP0 Count ticks (294.912 MHz; divide by 294912 for
 * milliseconds). `vu1WaitTicks` is a legacy field name: it measures EE waiting
 * for VIF1 DMA consumption, not VU1 arithmetic time or utilization. Downstream
 * backpressure may contribute to that wait.
 */
struct StaPipTelemetry {
#if TYRA_STAPIP_PACKET_PROFILE
  /** Set by the generated game's render scopes; not itself an accumulated
   * counter. A packet spanning scopes is charged to Mixed conservatively. */
  u8 producer = StaPipProducerMixed;
  StaPipPacketCounters packet[StaPipProducerCount];
#endif
  /**
   * Modified by TyraX: WHAT THE TRIANGLE COUNTERS COUNT, and why they are not
   * a geometry-equality check across two builds.
   *
   * Every `triangles*` field below is the number of primitives the GS is
   * asked to rasterise, which for a triangle LIST package is `size / 3` and
   * for a STRIP package is `size - 2`. Both are correct, and they are NOT the
   * same quantity as the number of triangles the SURFACE has, because a baked
   * strip array deliberately contains degenerate triangles:
   *
   *   - two vertices are repeated either side of every seam where one run
   *     holds more than one strip, and
   *   - the tail of every run but a bag's last is padded with repeats of the
   *     last vertex, so the run is exactly the pinned package size.
   *
   * Both rasterise to nothing, and both are counted. So a stripped build of
   * ONE surface reports MORE triangles than the list build of it - the Motor
   * District garage-day pose reports 25 650 as a list and 38 427 as strips,
   * a 1.498x that is measured, not a miscount: the strip sends 11.3% fewer
   * VERTICES (76 951 -> 68 235) and asks the GS for about half as many more
   * primitives, nearly all of them zero-area. The saving is on the EE, which
   * pays per package, and the primitives are what it costs on the GS.
   *
   * Consequence for an A/B: `trianglesCull` CANNOT show that two arms draw
   * the same geometry when the arms differ in representation. Use
   * `verticesSubmitted` for the EE bill, and the producer's own build-time
   * surface count for the equality check - the runtime cannot recover it,
   * because the degenerate count depends on how the bake packed the runs and
   * nothing in a package records it. The generated game logs one per producer
   * at scene load (`ROADSTRIP ... triangles`, `TERRAINSTRIP ... triangles`).
   * Between two arms of one representation they are comparable as before.
   */
  u32 packagesCull = 0;
  u32 packagesClip = 0;
  u32 packagesOutside = 0;
  u32 trianglesCull = 0;
  u32 trianglesClip = 0;
  u32 trianglesOutside = 0;

  /**
   * Modified by TyraX: packages that leave the VIEW frustum but cross no VU1
   * clip plane, so the GS scissor crops them and they take the cull path.
   * A subset of packagesCull/trianglesCull - these would have been clipped
   * before StaPipCore::isGuardBandOnly existed.
   *
   * KNOWN DEFECT, not fixed here: StaPipCore::recordGuardBandPackage counts
   * `package.size / 3` with no strip branch, while the same package's
   * trianglesCull contribution uses `size - 2`. So for a stripped package the
   * subset is counted on a different rule from the set it is a subset of -
   * 24 against 70 for a 72-vertex run - and `guard` cannot be subtracted from
   * `cull`. `packagesGuardBand` is unaffected; only the triangle half is.
   * StaPipCore::recordOutsideBag has the mirror of it: it charges the whole
   * BAG `count - 2`, but the bag is sliced into ceil(count / maxVertCount)
   * runs that are each their own strip, so the primitive total is
   * `count - 2 * packages` and it over-counts by 2 * (packages - 1).
   */
  u32 packagesGuardBand = 0;
  u32 trianglesGuardBand = 0;
  /**
   * Modified by TyraX: WHOLE BAGS that left the view but not the guard band
   * and were therefore sent down the direct route (TYRA_STAPIP_GUARD_BAND_BAGS
   * in stapip_core.cpp). Their packages count as `cull`, not `guard`.
   */
  u32 bagsGuardBandDirect = 0;

  /**
   * Modified by TyraX: triangle-strip routing (StaPipBag::stripped).
   * `packagesStrip` is the subset of packagesCull submitted AS a strip -
   * i.e. where the EE paid for one package instead of ~3. `packagesStripExpanded`
   * counts the stripped packages that had to be expanded back into a triangle
   * list for the clipper, which is the cost side of the same change.
   * `verticesSubmitted` is every vertex handed to a VU1 buffer this frame,
   * strips and lists alike: it is the number the EE's per-package bill scales
   * with, and the one to quote when comparing two builds of one view.
   */
  u32 packagesStrip = 0;
  u32 packagesStripExpanded = 0;
  u32 verticesSubmitted = 0;

  /** Clip-routed packages by conservative active-plane mask population. */
  u32 activePlanePopcount[7] = {};

  // Modified by TyraX: nested render-attribution timings, opt-in only.
  u32 dmaSubmitTicks = 0;
  u32 packetBuildTicks = 0;
  u32 boundsTicks = 0;
  u32 prepareTicks = 0;
  u32 dispatchTicks = 0;
  u32 packetFlushes = 0;
  // Modified by TyraX: explicit retained-source submission batching. These
  // make a zero-hit fixture visible instead of looking like a failed probe.
  u32 submissionBatchEligibleBags = 0;
  u32 submissionBatchedBags = 0;
  u32 submissionBatchPackets = 0;
  u32 vu1WaitTicks = 0;
  u32 programSetSwaps = 0;
  u32 programSetWaitTicks = 0;

#if TYRA_STAPIP_ATTRIB
  /**
   * Added by TyraX: the attribution counters that close the gap between the
   * three brackets above and render submission
   * (docs/render-submission-attribution.md). Compiled out by default - the
   * macro lives in stapip_attrib.hpp and defaults to 0.
   */
  StaPipAttrib attrib;
#endif

#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
  /**
   * Added by TyraX, Probe B only (stapip_probes.hpp). The arm is BOUNDED: a
   * send whose chain references the qbuffer copy pool keeps `FlushCache`,
   * every other send drops it. The saving is proportional to the split, so
   * the split is counted rather than assumed - `probeUnflushedSends` is the
   * only part of `packetFlushes` this arm can have made cheaper.
   */
  u32 probeFlushedSends = 0;
  u32 probeUnflushedSends = 0;
#endif
};

}  // namespace Tyra

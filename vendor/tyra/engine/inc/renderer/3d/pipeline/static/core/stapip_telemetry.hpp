/*
# Modified by TyraX: opt-in static-pipeline routing and VU1 back-pressure
# telemetry. Kept in a standalone header so StaPipCore and the qbuffer renderer
# share the counters without exposing either implementation to the other.
*/

#pragma once

#include <tamtypes.h>

namespace Tyra {

/**
 * Diagnostic counters accumulated until StaPipCore::takeTelemetry().
 *
 * Timing values are EE COP0 Count ticks (294.912 MHz; divide by 294912 for
 * milliseconds). `vu1WaitTicks` is a legacy field name: it measures EE waiting
 * for VIF1 DMA consumption, not VU1 arithmetic time or utilization. Downstream
 * backpressure may contribute to that wait.
 */
struct StaPipTelemetry {
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
};

}  // namespace Tyra

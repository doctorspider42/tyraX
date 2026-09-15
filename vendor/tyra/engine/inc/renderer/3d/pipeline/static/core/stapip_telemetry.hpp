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

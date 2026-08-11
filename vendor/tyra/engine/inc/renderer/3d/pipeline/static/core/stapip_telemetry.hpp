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
 * milliseconds). `vu1WaitTicks` is time the EE spent waiting for the previous
 * VIF1/VU1 chain before submitting another static-pipeline packet.
 */
struct StaPipTelemetry {
  u32 packagesCull = 0;
  u32 packagesClip = 0;
  u32 packagesOutside = 0;
  u32 trianglesCull = 0;
  u32 trianglesClip = 0;
  u32 trianglesOutside = 0;

  /** Clip-routed packages by conservative active-plane mask population. */
  u32 activePlanePopcount[7] = {};

  u32 packetFlushes = 0;
  u32 vu1WaitTicks = 0;
  u32 programSetSwaps = 0;
  u32 programSetWaitTicks = 0;
};

}  // namespace Tyra

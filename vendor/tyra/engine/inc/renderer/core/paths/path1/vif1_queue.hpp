/*
# Modified by TyraX - new file: an interrupt-driven VIF1 submission queue.
# Licensed under Apache License 2.0, like the engine it belongs to.
*/

#pragma once

#include <tamtypes.h>

/**
 * TYRA_VIF1_QUEUE: 1 = the static pipeline hands finished packets to Vif1Queue
 * and keeps building the next one while VIF1 is still busy; 0 = the stock
 * ping-pong, where every send first waits for the previous transfer. The two
 * arms differ in nothing else, so 0 is the A/B control. drain() exists in both
 * arms (in the 0 arm it is exactly the old dma_channel_wait), so callers are
 * written once. Measured on a physical PS2 (Motor District, four parked poses,
 * 0.019 ms repeatability floor): `work` -0.72 / -0.63 / -0.55 / -0.50 ms,
 * triangles and flushes identical. docs/ee-submission-rearchitecture.md.
 */
#ifndef TYRA_VIF1_QUEUE
#define TYRA_VIF1_QUEUE 1
#endif
/** Packets in flight. 2 reproduces the stock ping-pong's buffer count. */
#ifndef TYRA_VIF1_QUEUE_DEPTH
#define TYRA_VIF1_QUEUE_DEPTH 4
#endif
/** 1 = a DMAC completion interrupt starts the next chain; 0 = only the EE
 * does, at its next submit or wait, and no interrupt handler is installed.
 * 0 is the default because 1 CRASHES A REAL PS2: with EnableDmac(VIF1) the
 * kernel took an instruction fetch at address 0 inside the interrupt path
 * (BadVAddr 0, Status.EXL set, EPC = the interrupted EE instruction just after
 * EIntr), twice, under ps2link - while PCSX2 ran it clean. Do not turn it on
 * again without first finding which DMAC handler the kernel called. */
#ifndef TYRA_VIF1_QUEUE_ISR
#define TYRA_VIF1_QUEUE_ISR 0
#endif

namespace Tyra {

/**
 * Up to kDepth packets in flight on VIF1. The EE starts the next queued chain
 * whenever it submits or waits and finds the channel idle, instead of waiting
 * for each transfer before building the next packet (a DMAC-interrupt variant
 * exists behind TYRA_VIF1_QUEUE_ISR and is OFF - see there).
 *
 * Why: the static pipeline sends ~110 packets a frame from TWO buffers, and
 * each send waits for the one before it - so the EE runs in lock-step with VU1,
 * and the frame pays every stretch in which VU1 is behind as EE idle time
 * (5.7 ms of `VIF1 wait` in the Motor District garage, measured on hardware;
 * docs/ee-submission-rearchitecture.md). A queue lets the EE build ahead.
 *
 * What it deliberately is NOT: a chain that gets extended while it runs. Every
 * queued packet stays its own complete chain ending in its own END tag. Joining
 * separately allocated chains with a NEXT tag was tried in this engine and
 * froze a physical PS2 on the first gameplay frame (tyra-engine-dev, "Do not
 * fuse already-finished uniform and geometry chains"). Here the DMAC runs one
 * intact chain at a time, exactly as before; only WHO starts the next one
 * changed.
 *
 * The contract every other VIF1 user must keep: call drain() before its own
 * dma_channel_wait/send on VIF1. A plain dma_channel_wait is NOT enough while
 * packets are queued - between one chain's END and the interrupt that starts
 * the next, CHCR.STR reads 0 for a few cycles, and a caller that took that for
 * "idle" would start its own chain under the queue.
 */
class Vif1Queue {
 public:
  static constexpr u32 kDepth = TYRA_VIF1_QUEUE_DEPTH;

  /** Installs the DMAC VIF1 handler once. Safe to call more than once. */
  static void init();

  /**
   * Queues a finished chain for VIF1. The caller has already written back the
   * data cache for everything the chain reads (FlushCache, as the stock send
   * does). Returns the chain's sequence number for waitFor().
   */
  static u32 submit(const void* chain);

  /** Blocks until the chain with this sequence number has been transferred. */
  static void waitFor(u32 sequence);

  /** Blocks until nothing is queued and VIF1 is idle. */
  static void drain();

  /** True while a chain submitted here is still owned by the DMAC. */
  static bool busy();

  /** Interrupt context - public only so the C handler can reach it. */
  static void onComplete();

 private:
  static void start(u32 chain);
};

}  // namespace Tyra

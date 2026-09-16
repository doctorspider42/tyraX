/*
# Added by TyraX: BOUNDING PROBES for the EE submission rearchitecture
# (docs/ee-submission-rearchitecture.md, "The two probes that come first").
#
# NONE OF THIS SHIPS. Each macro defaults to 0 and at 0 not one field, not one
# branch and not one instruction of any of it is compiled. The arms exist to
# decide how much of the planned rearchitecture is worth building; an honest
# "this direction is capped at X" is the intended outcome.
#
# ZERO-COST RULE, same as stapip_attrib.hpp. `#ifndef NDEBUG` is NOT the gate
# in this engine - a game build NEVER defines NDEBUG, the release profile only
# drops -g and sets KEEPSYM=0 - so diagnostics hung off it ship live. See
# docs/render-submission-attribution.md. The gate is an explicit opt-in macro.
#
# The engine and the generated game both read this header, so one edit moves
# both halves - or pass -DTYRA_STAPIP_PROBE_<name>=1 to both builds.
*/

#pragma once

/* ------------------------------------------------------------------ *
 * Probe A - what is per-package frustum classification worth?
 * ------------------------------------------------------------------ */

/**
 * A1 "accept-all": the classification still RUNS, its rejection is discarded.
 *
 * Every OUTSIDE_FRUSTUM verdict - the per-package one and the coarse
 * eight-package one - is reported as IN_FRUSTUM. PARTIALLY_IN_FRUSTUM is left
 * exactly alone, which is the whole safety property of this arm: a package
 * that straddles a plane still goes to the clip route. Forcing IN_FRUSTUM
 * *without* running the test would route near-plane-crossing geometry to the
 * cull programs, which do not clip, and smear wedges across the screen.
 *
 * The branch structure is unchanged, so the EE pays the same classification it
 * pays today and the delta is the BENEFIT side alone: the VU1, DMA and packet
 * time the rejected packages would have cost. The picture must stay
 * byte-identical - the extra packages are off screen and the cull programs'
 * per-vertex ADC judgement drops them.
 */
#ifndef TYRA_STAPIP_PROBE_ACCEPT_ALL
#define TYRA_STAPIP_PROBE_ACCEPT_ALL 0
#endif

/**
 * A2 "coarse classify": one classification per EIGHT-package coarse group,
 * reused by every package in the group, with rejection and clip routing
 * intact.
 *
 * The coarse level already exists (`StaPipBagPackagesBBox::coarseMin/Max`, one
 * box per 24 one-third parts = 8 full VU packages) and already short-circuits
 * a group that is wholly in or wholly out. This arm extends the same reuse to
 * a PARTIALLY_IN_FRUSTUM group: the group box is run through the identical
 * pair of tests `checkFrustum` runs (`frustumCheckAABB` plus, for a partial
 * box, `activePlaneMaskAABB` over the eight clip planes) and the resulting
 * verdict, VU1 plane mask and guard-band answer are copied to all eight
 * packages. Per-package classification then never runs at all.
 *
 * NOTE WHY THE GROUP IS THE COARSE GROUP AND NOT A "1/3-BBOX PART".
 * docs/ee-submission-rearchitecture.md proposes S3 as "classify per 1/3-bbox
 * part instead of per package". A part is one THIRD OF A PACKAGE here
 * (`StaPipBagPackagesBBox` splits the bag into `maxVertCount / 3`-vertex
 * parts and a full package spans exactly three of them), so classifying per
 * part is three tests where there is now one, and the parts are not shared
 * between packages, so nothing amortises. Coarsening has to go the other way
 * to remove work at all.
 *
 * The picture MAY differ: a bigger box is outside less often, so this arm
 * rejects less and clip-routes more. That is the trade being priced, and it
 * is measured rather than assumed.
 */
#ifndef TYRA_STAPIP_PROBE_COARSE_CLASSIFY
#define TYRA_STAPIP_PROBE_COARSE_CLASSIFY 0
#endif

/* ------------------------------------------------------------------ *
 * Probe B - what does FlushCache really cost, refill included?
 * ------------------------------------------------------------------ */

/**
 * B "uncached chain": allocate the static pipeline's two geometry packets
 * outside cached memory and stop asking `dma_channel_send_packet2` for the
 * whole-data-cache write-back invalidate (syscall 100) on every send.
 *
 * 1 = P2_TYPE_UNCACHED. 2 = P2_TYPE_UNCACHED_ACCL. 0 = off.
 *
 * *** READ THIS BEFORE SETTING IT TO 2. ***
 *
 * docs/ee-submission-rearchitecture.md names `P2_TYPE_UNCACHED_ACCL` for this
 * arm and calls the risk low because "packet2_create already accepts the
 * type". Accepting the type is not the question. The stock ps2sdk packet
 * BUILDER back-patches bytes it has already written:
 * `packet2_vif_close_unpack_auto` does an `lbu` of byte 3 of the open unpack
 * VIFcode and an `sb` into byte 2 of it (disassembled from
 * ps2sdk/ee/lib/libpacket2.a), and the chain helpers patch a DMA tag's QWC the
 * same way. Under UNCACHED_ACCL those accesses go through the EE's 128-byte
 * write-gather buffer: the read can see memory the buffer has not yet flushed,
 * and the sub-word back-patch of an already-gathered block is exactly the case
 * the write-gather buffer does not define. A corrupt VIFcode is a hung VIF1,
 * which is how the earlier `flush_cache = false` arm wedged ps2link until a
 * physical Reset.
 *
 * P2_TYPE_UNCACHED (1) has no write-gather buffer. Every store goes straight
 * to memory and every read-back is coherent, so the back-patching builder is
 * correct as written and the arm is safe. It is also the PESSIMISTIC bound on
 * the write side - `packet2` writes floats one at a time, and uncached stores
 * do not gather - so this arm can legitimately come out SLOWER. That is a real
 * result about the direction, not a failed measurement.
 *
 * Note `packet2_create` asserts `qwords % 4 == 0` for either uncached type
 * (same disassembly). `packetSize` is 784, which passes; a future change to
 * `kWorstBagPacketSize` or `kSubmissionBatchSize` must keep it a multiple of 4
 * or this arm traps at allocation instead of running.
 */
#ifndef TYRA_STAPIP_PROBE_UNCACHED_CHAIN
#define TYRA_STAPIP_PROBE_UNCACHED_CHAIN 0
#endif

#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
/**
 * THE COPY POOLS ARE THE CORRECTNESS HALF OF PROBE B.
 *
 * `StaPipQBuffer::fillByCopyMax` / `fillByCopy1By2` / `fillByCopy1By3`,
 * `fillByStripExpand` and `StaPipClipper::writeChunk` write vertices into a
 * per-slot pool BETWEEN sends, and the packet's REF tags point at that pool.
 * Those writes are ordinary cached stores; the packet being uncached says
 * nothing about them. Dropping the flush for a send whose chain references the
 * pool is a real correctness bug, and it is the intermittent kind - the
 * slot-pool race in docs/vu1-clipping.md took 4 to 19 frames of 30 to appear
 * on hardware.
 *
 * So this arm is BOUNDED rather than unconditional: a send keeps the flush if
 * any buffer in the packet was filled by a copy, and drops it otherwise. At
 * the garage-day pose the copy paths are the clip route only (36.5 packages of
 * 1050 drawn), so nearly every send takes the cheap path - but the split is
 * COUNTED (`StaPipTelemetry::probeFlushedSends` /`probeUnflushedSends`) and
 * reported rather than assumed, because the saving is proportional to it.
 */
#define TYRA_STAPIP_PROBE_POOL_KEEPS_FLUSH 1

/**
 * Print the flushed/unflushed send split to the game log every 3000 sends.
 *
 * SEPARATE from the arm on purpose, and OFF by default: `TYRA_LOG` over
 * `host:` is network I/O inside the thing being measured
 * (docs/vu1-and-dma-cache-cost.md is explicit about it). The split is a
 * property of the SCENE, not of the timing, so read it in PCSX2 with this at
 * 1 and run the console arm with it at 0.
 */
#ifndef TYRA_STAPIP_PROBE_LOG_SENDS
#define TYRA_STAPIP_PROBE_LOG_SENDS 0
#endif

/**
 * Keep `FlushCache` even though the packets are uncached - the SECOND CONTROL
 * that makes the arm readable.
 *
 * The uncached arm changes two things at once: it stops flushing AND it makes
 * every packet write go to memory instead of to the data cache. One delta
 * against the stock build cannot tell those apart, and on this scene the
 * second effect is by far the larger, so the naive reading ("uncaching the
 * chain costs 6 ms") hides the number the plan actually wants.
 *
 * With this at 1 the arm holds the write cost constant and pays the flush:
 *
 *   uncached+flush  -  stock           = the cost of writing uncached
 *   uncached        -  uncached+flush  = the cost of the flush, refill included
 *
 * The second row is the honest bound on what S1 can ever be worth, and it is
 * measured with everything else equal.
 */
#ifndef TYRA_STAPIP_PROBE_FORCE_FLUSH
#define TYRA_STAPIP_PROBE_FORCE_FLUSH 0
#endif
#endif

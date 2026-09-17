// Modified by TyraX: DIAGNOSTIC-ONLY renderer probes, configured from a file at
// boot. Absent file = every knob zero and no behaviour change at all. These are
// measurement instruments, never shipped behaviour switches.
#pragma once
#include <tamtypes.h>

namespace Tyra { namespace PerfProbe {

/** Extra FlushCache(0) calls issued immediately before each static-pipeline
 * VIF1 submission, ON TOP of the one dma_channel_send_packet2 already makes.
 *
 * Prices the whole "explicit DMA cache ownership" direction before anyone
 * rewrites buffer ownership for it: the EE data cache is 8 KiB and FlushCache(0)
 * write-back-INVALIDATES all of it, so each call costs its own syscall PLUS the
 * misses every later EE read pays on a cold cache. That second half lands in
 * Package_create / Packet_build / Bounds rather than in VIF1_submit, which is
 * why the submit bracket alone under-reports it. Adding flushes is correctness-
 * neutral by construction (a redundant write-back invalidates nothing the DMA
 * needs), so the slope over 0/1/3 extra flushes measures the real per-flush cost
 * with no risk of the DMA-lifetime bugs a removal experiment would carry. */
extern u32 extraFlushes;

/** DIAGNOSTIC SUPPRESSION, never a shipping behaviour: submit the static
 * pipeline's packets with dma_channel_send_packet2's flush_cache argument
 * FALSE, so the per-submission FlushCache(0) disappears entirely.
 *
 * This is deliberately unsound in general - the SDK flushes because it cannot
 * know which EE writes the DMA is about to read - so an arm using it is only
 * evidence when its triangle counts and its GS capture match the control. It
 * exists to price the ceiling of an explicit-ownership redesign (which would
 * have to make the same submission safe) before anyone builds one. Where
 * extraFlushes measures the marginal cost of a flush on an ALREADY cold cache,
 * this measures the whole cost including the cache refill the first flush
 * forces - the half no amount of added flushes can show. */
extern bool suppressFlush;

void configure();

} }  // namespace Tyra::PerfProbe

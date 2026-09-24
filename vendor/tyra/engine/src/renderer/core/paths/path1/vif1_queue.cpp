/*
# Modified by TyraX - new file: an interrupt-driven VIF1 submission queue.
# See inc/renderer/core/paths/path1/vif1_queue.hpp for why it exists and the
# contract its callers keep. Licensed under Apache License 2.0.
*/

#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <kernel.h>
#include <dma.h>

namespace Tyra {

namespace {

// DMAC channel 1 (VIF1). The same registers ps2sdk's dma_channel_send_chain
// writes, taken from libdma's own register tables.
volatile u32* const kChcr = reinterpret_cast<volatile u32*>(0x10009000);
volatile u32* const kMadr = reinterpret_cast<volatile u32*>(0x10009010);
volatile u32* const kQwc = reinterpret_cast<volatile u32*>(0x10009020);
volatile u32* const kTadr = reinterpret_cast<volatile u32*>(0x10009030);
volatile u32* const kDStat = reinterpret_cast<volatile u32*>(0x1000E010);
constexpr u32 kStr = 0x100;
// DIR from memory | chain mode | TIE | STR | TTE: exactly the CHCR
// dma_channel_send_chain writes for a transfer-tag chain (0x185 | 0x40).
constexpr u32 kChainChcr = 0x1C5;

// Ring of chains waiting for the channel. Written by the EE with interrupts
// off, consumed by the interrupt handler - so every field is volatile and no
// lock is needed beyond DIntr/EIntr on the submitting side.
volatile u32 ring[Vif1Queue::kDepth];
volatile u32 ringSeq[Vif1Queue::kDepth];  // sequence number of each entry
volatile u32 head = 0;  // next ring entry the handler starts
volatile u32 tail = 0;  // next free ring entry
volatile bool running = false;  // a chain OF OURS owns the channel
volatile u32 submitted = 0;
volatile u32 completed = 0;
s32 handlerId = -1;
// The newest sequence number the last data-cache write-back covered.
u32 flushedUpTo = 0;
void (*openChainCloser)() = nullptr;  // see setOpenChainCloser()

// Submits a chain still being built by someone else, ahead of the caller's.
void closeOpenChain() {
  if (openChainCloser == nullptr) return;
  auto closer = openChainCloser;
  openChainCloser = nullptr;  // the closer submits, and must not recurse
  closer();
}

}  // namespace

extern "C" s32 tyraxVif1QueueHandler(s32 channel) {
  (void)channel;
#if TYRA_VIF1_QUEUE_ISR
  Tyra::Vif1Queue::onComplete();
#endif
  // NO ExitHandler() here. It is `ei`, and re-enabling interrupts inside the
  // handler lets the next one (another VIF1 completion, a vblank) in while the
  // kernel is still restoring the interrupted thread's registers. On a real
  // PS2 that returned to waitFor() with a garbage s0 - an EE exception, TLB
  // load miss at 0x1473A38C - on the first queued frame; PCSX2 never showed
  // it. The engine's vblank handler, proven on hardware, does not call it
  // either (renderer_core_gs.cpp).
  return 0;
}

void Vif1Queue::init() {
#if !TYRA_VIF1_QUEUE_ISR
  return;  // the EE advances the queue itself; see TYRA_VIF1_QUEUE_ISR
#endif
  // No handler is not fatal: every wait also advances the queue itself
  // (advanceIfIdle), so the queue degrades to "started by the next waiter"
  // rather than hanging.
  if (handlerId >= 0) return;
  DIntr();
  handlerId = AddDmacHandler(DMAC_VIF1, tyraxVif1QueueHandler, 0);
  if (handlerId >= 0) EnableDmac(DMAC_VIF1);
  EIntr();
}

void Vif1Queue::start(u32 chain, u32 sequence) {
#if TYRA_VIF1_QUEUE_LAZY_FLUSH
  static_assert(!TYRA_VIF1_QUEUE_ISR,
                "lazy flush writes back from start(), which must not run in an "
                "interrupt (FlushCache is a syscall)");
  if (static_cast<s32>(flushedUpTo - sequence) < 0) {
    FlushCache(0);
    flushedUpTo = submitted;  // everything submitted so far is now in RAM
  }
#else
  (void)sequence;
#endif
  *kDStat = 1U << 1;  // clear the channel's completion status (write-1-clears)
  *kQwc = 0;
  *kMadr = 0;
  *kTadr = chain & 0x0FFFFFFF;
  *kChcr = kChainChcr;
}

void Vif1Queue::onComplete() {
  // Only ever act on a completion of OUR chain. Two ways a foreign one gets
  // here: a caller that drained and then sent its own chain directly, and a
  // completion interrupt that was still pending when submit() started a chain
  // on a channel it had just seen go idle. The first is recognised by
  // `running`, the second by STR still being set - either way the channel is
  // not ours to advance.
  if (!running || (*kChcr & kStr)) return;
  completed = completed + 1;
  if (head != tail) {
    const u32 next = ring[head % kDepth];
    const u32 nextSeq = ringSeq[head % kDepth];
    head = head + 1;
    start(next, nextSeq);
  } else {
    running = false;
  }
}

// A waiter's safety net. If the channel is idle while we still believe a chain
// of ours owns it, the completion interrupt has not been serviced yet (or was
// lost): advance the queue from here. Interrupts are off while we look, so the
// handler cannot advance it a second time - a late interrupt then finds STR set
// on the next chain, or `running` false, and does nothing.
static void advanceIfIdle() {
#if TYRA_VIF1_QUEUE_ISR
  DIntr();
  Vif1Queue::onComplete();
  EIntr();
#else
  // No handler exists, so nothing can race us: no interrupt masking needed.
  Vif1Queue::onComplete();
#endif
}

u32 Vif1Queue::submit(const void* chain) {
  closeOpenChain();
  const u32 addr = reinterpret_cast<u32>(chain);
  advanceIfIdle();  // also what starts the queue when the interrupt does not
  // Back-pressure: never hold more than kDepth chains. The static pipeline has
  // fewer packet buffers than that, so in practice this never spins.
  while (tail - head >= kDepth) advanceIfIdle();
  if (!running) {
    // Idle as far as the queue knows, but a caller outside the queue may have
    // a chain of its own on the channel: wait it out before taking the
    // channel, exactly as the stock send does.
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  }
#if TYRA_VIF1_QUEUE_ISR
  DIntr();
#endif
  const u32 sequence = submitted + 1;
  submitted = sequence;
  if (!running) {
    running = true;
    start(addr, sequence);
  } else {
    ring[tail % kDepth] = addr;
    ringSeq[tail % kDepth] = sequence;
    tail = tail + 1;
  }
#if TYRA_VIF1_QUEUE_ISR
  EIntr();
#endif
  return sequence;
}

void Vif1Queue::waitFor(u32 sequence) {
  // Wrap-safe comparison: sequence numbers are u32 and a long session wraps.
  while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();
}

void Vif1Queue::drain() {
  closeOpenChain();
  while (running) advanceIfIdle();
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
}

bool Vif1Queue::busy() { return running; }

void Vif1Queue::setOpenChainCloser(void (*closer)()) {
  openChainCloser = closer;
}

}  // namespace Tyra

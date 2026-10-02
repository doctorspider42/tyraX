/*
# Modified by TyraX - new file: an interrupt-driven VIF1 submission queue.
# See inc/renderer/core/paths/path1/vif1_queue.hpp for why it exists and the
# contract its callers keep. Licensed under Apache License 2.0.
*/

#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <kernel.h>
#include <dma.h>
#if TYRA_VIF1_VALIDATE
#include "debug/debug.hpp"
#endif

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
#if TYRA_VIF1_VALIDATE
const void* seqWho[64];  // who submitted each recent sequence number
void noteStarted(u32 chain, u32 seq, const void* who);
void checkFinished();
#endif

// Submits a chain still being built by someone else, ahead of the caller's.
#if TYRA_VIF1_QUEUE_HOLD
bool holding = false;  // chains queued, none started (the probe)
#endif

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
#if TYRA_VIF1_VALIDATE
  // At START, not at submit: this is when the DMAC begins reading, so a chain
  // (or the data it REFs) rewritten while it waited in the ring shows up here.
  validate(reinterpret_cast<const void*>(chain), "queue-start",
           seqWho[sequence & 63]);
  checkFinished();
  noteStarted(chain, sequence, seqWho[sequence & 63]);
#endif
  *kDStat = 1U << 1;  // clear the channel's completion status (write-1-clears)
  *kQwc = 0;
  *kMadr = 0;
  *kTadr = chain & 0x0FFFFFFF;
  *kChcr = kChainChcr;
}

void Vif1Queue::onComplete() {
#if TYRA_VIF1_QUEUE_HOLD
  if (holding) return;  // nothing started yet (the probe)
#endif
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

#if TYRA_VIF1_QUEUE_HOLD
u32 Vif1Queue::holdReleases = 0;
u32 Vif1Queue::holdChains = 0;
u32 Vif1Queue::holdBusyTicks = 0;
u32 Vif1Queue::holdSegStart = 0;
u32 Vif1Queue::holdLastClose = 0;
bool Vif1Queue::holdSegOpen = false;

static u32 holdNow() {
  u32 now;
  __asm__ volatile("mfc0 %0, $9" : "=r"(now));
  return now;
}

// Closes the open segment once nothing of ours is left on the channel.
static void closeSegmentIfIdle() {
  if (!Vif1Queue::holdSegOpen || running) return;
  const u32 now = holdNow();
  Vif1Queue::holdBusyTicks += now - Vif1Queue::holdSegStart;
  Vif1Queue::holdLastClose = now;
  Vif1Queue::holdSegOpen = false;
}

// Starts the held chains: the first one goes to the channel, the rest follow
// through the normal advance.
void Vif1Queue::releaseHeld() {
  if (!holding) return;
  holding = false;
  holdSegStart = holdNow();
  holdSegOpen = true;
  holdReleases = holdReleases + 1;
  const u32 next = ring[head % kDepth];
  const u32 nextSeq = ringSeq[head % kDepth];
  head = head + 1;
  start(next, nextSeq);
}
#endif

u32 Vif1Queue::submit(const void* chain) {
  closeOpenChain();
#if TYRA_VIF1_QUEUE_HOLD
  {
    static_assert(!TYRA_VIF1_QUEUE_ISR, "the hold probe runs without the ISR");
    const u32 addr = reinterpret_cast<u32>(chain);
    if (tail - head >= kDepth) releaseHeld();  // too many: measurement void
    while (tail - head >= kDepth) advanceIfIdle();
    const u32 sequence = submitted + 1;
    submitted = sequence;
    ++holdChains;
    if (!running) {
      // Nothing of ours on the channel: hold instead of starting. A caller
      // outside the queue may still own it - wait that out, as below.
      dma_channel_wait(DMA_CHANNEL_VIF1, 0);
      running = true;
      holding = true;
    }
    ring[tail % kDepth] = addr;
    ringSeq[tail % kDepth] = sequence;
    tail = tail + 1;
    return sequence;
  }
#endif
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
#if TYRA_VIF1_VALIDATE
  seqWho[sequence & 63] = __builtin_return_address(0);
#endif
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
#if TYRA_VIF1_QUEUE_HOLD
  if (static_cast<s32>(completed - sequence) < 0) releaseHeld();
#endif
  // Wrap-safe comparison: sequence numbers are u32 and a long session wraps.
  while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();
#if TYRA_VIF1_QUEUE_HOLD
  advanceIfIdle();  // notice an idle channel, so the segment can close
  closeSegmentIfIdle();
#endif
}

void Vif1Queue::drain() {
  closeOpenChain();
#if TYRA_VIF1_QUEUE_HOLD
  releaseHeld();
#endif
  while (running) advanceIfIdle();
#if TYRA_VIF1_QUEUE_HOLD
  closeSegmentIfIdle();
#endif
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
#if TYRA_VIF1_VALIDATE
  checkFinished();
#endif
}

bool Vif1Queue::busy() { return running; }

#if TYRA_VIF1_VALIDATE
namespace {
// VIF1 state that survives a chain boundary, as on the hardware.
u32 vCl = 1, vWl = 1;   // STCYCL
u32 vPending = 0;       // data words still owed to the last VIFcode
u32 vPendingCode = 0;   // ... and that code
const void* vPendingWho = nullptr;
const char* vPendingName = "";
u32 vReports = 0;
u32 vChains = 0;

const u32* ramWords(u32 addr) {
  if (addr & 0x80000000) return reinterpret_cast<const u32*>(0x70000000 | (addr & 0x3FFF));
  return reinterpret_cast<const u32*>(addr & 0x01FFFFFF);
}

bool validCmd(u32 c) {
  if (c <= 0x07) return true;
  if (c >= 0x60) return true;  // UNPACK, all variants
  switch (c) {
    case 0x10: case 0x11: case 0x13: case 0x14: case 0x15: case 0x17:
    case 0x20: case 0x30: case 0x31: case 0x4A: case 0x50: case 0x51:
      return true;
  }
  return false;
}

u32 dataWords(u32 code) {
  const u32 cmd = (code >> 24) & 0x7F;
  const u32 num = (code >> 16) & 0xFF;
  const u32 imm = code & 0xFFFF;
  if (cmd == 0x20) return 1;
  if (cmd == 0x30 || cmd == 0x31) return 4;
  if (cmd == 0x4A) return (num ? num : 256) * 2;
  if (cmd == 0x50 || cmd == 0x51) return (imm ? imm : 65536) * 4;
  if (cmd >= 0x60) {
    const u32 vn = (cmd >> 2) & 3, vl = cmd & 3;
    u32 n = num ? num : 256;
    if (vWl > vCl) n = (n / vWl) * vCl + ((n % vWl) < vCl ? (n % vWl) : vCl);
    const u32 bits = (vn == 3 && vl == 3) ? 16 : (vn + 1) * (32 >> vl);
    return (n * bits + 31) / 32;
  }
  return 0;
}

void report(const char* what, const char* name, const void* who, u32 tagIdx, u32 tagW0, u32 word,
            u32 wordIdx) {
  if (vReports >= 6) return;
  ++vReports;
  TYRA_LOG("VIF1CHECK chain#", vChains, " ", what, " from=", name, " who=", who, " tag#",
           tagIdx, " tagw0=", tagW0, " word#", wordIdx, " value=", word,
           " pendingFrom=", vPendingName, "@", vPendingWho, " pendingCode=", vPendingCode);
}

void feed(u32 w, const char* name, const void* who, u32 tagIdx, u32 tagW0, u32 wordIdx) {
  if (vPending) {
    --vPending;
    return;
  }
  const u32 cmd = (w >> 24) & 0x7F;
  if (!validCmd(cmd)) {
    report("BAD-VIFCODE", name, who, tagIdx, tagW0, w, wordIdx);
    return;
  }
  if (cmd == 0x01) {  // STCYCL
    vCl = w & 0xFF;
    vWl = (w >> 8) & 0xFF;
    if (vWl == 0) vWl = 256;
    if (vCl == 0) vCl = 256;
  }
  vPending = dataWords(w);
  vPendingCode = w;
  vPendingWho = who;
  vPendingName = name;
}

// Hash of every word the chain hands VIF1 (tags' VIF halves + payloads).
u32 chainHash(u32 tagAddr) {
  u32 h = 2166136261u;
  for (u32 t = 0; t < 20000; ++t) {
    const u32* tag = ramWords(tagAddr);
    const u32 w0 = tag[0], addr = tag[1];
    const u32 qwc = w0 & 0xFFFF, id = (w0 >> 28) & 7;
    for (u32 i = 0; i < 4; ++i) h = (h ^ tag[i]) * 16777619u;
    u32 payload, next;
    switch (id) {
      case 1: payload = tagAddr + 16; next = payload + qwc * 16; break;
      case 7: payload = tagAddr + 16; next = 0; break;
      case 0: payload = addr; next = 0; break;
      case 3: case 4: payload = addr; next = tagAddr + 16; break;
      default: return h;
    }
    const u32* p = ramWords(payload);
    for (u32 i = 0; i < qwc * 4; ++i) h = (h ^ p[i]) * 16777619u;
    if (next == 0) return h;
    tagAddr = next;
  }
  return h;
}

u32 inflightChain = 0, inflightHash = 0, inflightSeq = 0;
const void* inflightWho = nullptr;
u32 raceReports = 0;

void noteStarted(u32 chain, u32 seq, const void* who) {
  inflightChain = chain;
  inflightSeq = seq;
  inflightWho = who;
  inflightHash = chainHash(chain);
}

void checkFinished() {
  if (inflightChain == 0) return;
  const u32 h = chainHash(inflightChain);
  if (h != inflightHash && raceReports < 6) {
    ++raceReports;
    TYRA_LOG("VIF1CHECK RACE: chain seq ", inflightSeq, " at ", inflightChain,
             " from ", inflightWho,
             " changed while VIF1 was reading it (hash ", inflightHash, " -> ",
             h, ")");
  }
  inflightChain = 0;
}

void Vif1QueueValidateHeartbeat() {
  if ((vChains % 5000) == 0)
    TYRA_LOG("VIF1CHECK ok: ", vChains, " chains walked, ", vReports,
             " defects, ", raceReports, " races");
}
}  // namespace

void Vif1Queue::validate(const void* chain, const char* name, const void* who) {
  ++vChains;
  Vif1QueueValidateHeartbeat();
  if (name[0] != 'q') {  // a direct sender: it is about to own the channel
    checkFinished();
    noteStarted(reinterpret_cast<u32>(chain), 0, who);
  }
  if (vPending) report("STREAM-ENTERS-MID-DATA", name, who, 0, 0, vPending, 0);
  u32 tagAddr = reinterpret_cast<u32>(chain);
  u32 callDepth = 0;
  u32 callRet[2] = {0, 0};
  for (u32 t = 0; t < 20000; ++t) {
    const u32* tag = ramWords(tagAddr);
    const u32 w0 = tag[0], addr = tag[1];
    const u32 qwc = w0 & 0xFFFF, id = (w0 >> 28) & 7;
    feed(tag[2], name, who, t, w0, 0);
    feed(tag[3], name, who, t, w0, 1);
    u32 payload, next;
    switch (id) {
      case 1: payload = tagAddr + 16; next = payload + qwc * 16; break;  // cnt
      case 7: payload = tagAddr + 16; next = 0; break;                   // end
      case 0: payload = addr; next = 0; break;                           // refe
      case 3: case 4: payload = addr; next = tagAddr + 16; break;        // ref/refs
      case 2: payload = tagAddr + 16; next = addr; break;                // next
      case 5:                                                            // call
        payload = tagAddr + 16;
        if (callDepth >= 2) { report("CALL-TOO-DEEP", name, who, t, w0, 0, 0); return; }
        callRet[callDepth++] = payload + qwc * 16;
        next = addr;
        break;
      case 6:                                                            // ret
        payload = tagAddr + 16;
        if (callDepth == 0) { next = 0; break; }
        next = callRet[--callDepth];
        break;
      default: report("BAD-DMATAG", name, who, t, w0, addr, 0); return;
    }
    if (qwc && !(payload & 0x80000000) && (payload & 0x0FFFFFFF) >= 0x02000000) {
      report("BAD-PAYLOAD-ADDR", name, who, t, w0, payload, 0);
      return;
    }
    const u32* p = ramWords(payload);
    for (u32 i = 0; i < qwc * 4; ++i) feed(p[i], name, who, t, w0, 2 + i);
    if (next == 0) return;
    tagAddr = next;
  }
  report("CHAIN-TOO-LONG", name, who, 0, 0, 0, 0);
}
#endif

void Vif1Queue::setOpenChainCloser(void (*closer)()) {
  openChainCloser = closer;
}

}  // namespace Tyra

/*
# Modified by TyraX - new file: an interrupt-driven VIF1 submission queue.
# See inc/renderer/core/paths/path1/vif1_queue.hpp for why it exists and the
# contract its callers keep. Licensed under Apache License 2.0.
*/

#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <kernel.h>
#include "renderer/core/paths/path3/path3_fence.hpp"
#include <dma.h>
#include <draw.h>
#include <gs_privileged.h>
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
#include <malloc.h>
#include <stdio.h>
#include "info/info.hpp"
#endif
#if TYRA_VIF1_CHAIN_CHECK
#include <stdio.h>
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
// Queue metadata grows independently of the producer's packet/copy pools.
constexpr u32 kRingDepth = TYRA_FRAME_CHAIN_ARENA ? 128 : Vif1Queue::kDepth;
volatile u32 ring[kRingDepth];
volatile u32 ringSeq[kRingDepth];  // sequence number of each entry
volatile u32 head = 0;  // next ring entry the handler starts
volatile u32 tail = 0;  // next free ring entry
volatile bool running = false;  // a chain OF OURS owns the channel
volatile u32 submitted = 0;
volatile u32 completed = 0;
volatile u32 runningSequence = 0;
s32 handlerId = -1;
// The newest sequence number the last data-cache write-back covered.
u32 flushedUpTo = 0;
void (*openChainCloser)() = nullptr;  // see setOpenChainCloser()

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
  runningSequence = sequence;
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
  completed = runningSequence;
  if (head != tail) {
    const u32 next = ring[head % kRingDepth];
    const u32 nextSeq = ringSeq[head % kRingDepth];
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

#if TYRA_VIF1_CHAIN_CHECK || TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
static const uint32_t* resolveChainRef(uint32_t address, uint32_t qwords,
                                       void*) {
  // Only ordinary EE RAM is supported here; no MMIO or scratchpad reads.
  if (address < 0x1000 || address >= 0x02000000 ||
      qwords > (0x02000000 - address) / 16) return nullptr;
  return reinterpret_cast<const uint32_t*>(address);
}
#endif

#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
namespace {
void* arenaMemory = nullptr;
FrameChainArena arenaStorage[2] = {FrameChainArena(nullptr, 0),
                                 FrameChainArena(nullptr, 0)};
void* arenaBanks[2] = {nullptr, nullptr};
u32 bankSequences[2] = {0, 0}, currentBank = 0;
FrameChainArena* chainArena = nullptr;
bool arenaAttempted[2] = {false, false};
bool nativeAttempted[2] = {false, false};
bool runtimePipeline = false;
bool frameRecording = false;
u32 arenaPeak = 0, arenaResets = 0, arenaFallbacks = 0;
u32 arenaFrameBytes = 0, arenaMaxFrameBytes = 0;
u32 arenaFrameBorrowed = 0, arenaMaxBorrowed = 0;
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
constexpr u32 kNativeQwords = 8192;
void* nativeBanks[2] = {nullptr, nullptr};
FrameVifWriter nativeStorage[2] = {FrameVifWriter(nullptr, 0),
                                 FrameVifWriter(nullptr, 0)};
FrameVifWriter* nativeWriter = nullptr;
u32 nativePendingSequence = 0, nativeInFlight = 0, nativeBatches = 0;
bool framePipelined = false, protectRecording = false;
void (*beforeFrameSubmit)(void*) = nullptr;
void* beforeFrameUser = nullptr;
u32 frameBatchStart = 0, frameSplitCount = 0;
u32 pipelineStarts = 0, pipelineBusyStarts = 0;
#endif
// The callback may issue real fences; never let those reset the new bank.
void completeBeforeSubmission() {
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
  if (!beforeFrameSubmit) return;
  const bool wasRecording = frameRecording;
  const bool recordedHudPending = path3FencePending;
  // Presentation belongs to the prior GPU job, not the HUD captured in the
  // new unsubmitted prefix. Its PATH3 fence would wait for our own prefix
  // while the callback is blocking that very submission.
  path3FencePending = false;
  frameRecording = false;
  protectRecording = true;
  beforeFrameSubmit(beforeFrameUser);
  protectRecording = false;
  path3FencePending = recordedHudPending;
  frameRecording = wasRecording;
#endif
}
ImmutableSpanTable immutableSpans;
bool isImmutableSpan(uint32_t address, uint32_t qwords) {
  return immutableSpans.borrow(address, qwords, 1u << currentBank);
}
void prepareArena() {
  const u32 banks = (TYRA_FRAME_PIPELINE || runtimePipeline) ? 2 : 1;
  const bool wantNative = TYRA_NATIVE_VIF_RECORD || runtimePipeline;
  if (arenaAttempted[0] && (banks == 1 || arenaAttempted[1]) &&
      (!wantNative || (nativeAttempted[0] &&
                       (banks == 1 || nativeAttempted[1])))) return;
  for (u32 i = 0; i < banks; ++i) {
    if (!arenaAttempted[i]) {
      arenaAttempted[i] = true;
      arenaBanks[i] = memalign(64, TYRA_FRAME_CHAIN_ARENA_BYTES);
      if (arenaBanks[i]) arenaStorage[i] =
          FrameChainArena(arenaBanks[i], TYRA_FRAME_CHAIN_ARENA_BYTES);
    }
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
    if (wantNative && !nativeAttempted[i]) {
      nativeAttempted[i] = true;
      nativeBanks[i] = memalign(64, kNativeQwords * 16);
      if (nativeBanks[i]) nativeStorage[i] = FrameVifWriter(nativeBanks[i], kNativeQwords);
    }
#endif
  }
#if TYRA_FRAME_PIPELINE_SUPPORT
  if (!TYRA_FRAME_CHAIN_ARENA && banks == 2 &&
      (!arenaBanks[0] || !arenaBanks[1] || !nativeBanks[0] || !nativeBanks[1])) {
    // Transactional first enable: refusing the pipeline must not leave a
    // partially allocated bank eating the synchronous game's remaining heap.
    for (u32 i = 0; i < 2; ++i) {
      free(arenaBanks[i]); free(nativeBanks[i]);
      arenaBanks[i] = nativeBanks[i] = nullptr;
      arenaStorage[i] = FrameChainArena(nullptr, 0);
      nativeStorage[i] = FrameVifWriter(nullptr, 0);
    }
    chainArena = nullptr; arenaMemory = nullptr; nativeWriter = nullptr;
  }
#endif
  if (!chainArena) {
    arenaMemory = arenaBanks[0];
    if (arenaMemory) chainArena = &arenaStorage[0];
  }
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
  if (!nativeWriter && nativeStorage[0].availableQwords()) nativeWriter = &nativeStorage[0];
#endif
  printf("FRAMEARENA allocated=%u bytes=%u\n", chainArena ? 1u : 0u,
         static_cast<unsigned>(TYRA_FRAME_CHAIN_ARENA_BYTES));
}
}
#endif

u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {
  if (sourceCopied) *sourceCopied = false;
#if TYRA_VIF1_CHAIN_CHECK
  const auto check = Vif1ChainCheck::validate(chain, qwords, resolveChainRef);
  if (!check) {
    // Keep the guard effective even when NDEBUG removes TYRA_ASSERT.
    printf("VIFCHECK rejected error=%u tag=%lu cmd=%lu pending=%lu\n",
           static_cast<unsigned>(check.error), static_cast<unsigned long>(check.tag),
           static_cast<unsigned long>(check.command),
           static_cast<unsigned long>(check.pending));
    fflush(stdout);
    SleepThread();
    return 0;
  }
  static u32 checked = 0;
  if (++checked == 1 || checked % 4096 == 0) {
    printf("VIFCHECK accepted chains=%lu\n", static_cast<unsigned long>(checked));
    fflush(stdout);
  }
#else
  (void)qwords;
#endif
  closeOpenChain();
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  static_assert(!TYRA_FRAME_CHAIN_ARENA || (TYRA_VIF1_QUEUE &&
                !TYRA_VIF1_QUEUE_ISR && !TYRA_VIF1_QUEUE_HOLD),
                "frame arena needs normal polled queue");
  if (TYRA_FRAME_CHAIN_ARENA || frameRecording) {
  prepareArena();
  if (chainArena) {
    auto snapshot = chainArena->copy(chain, qwords, resolveChainRef, nullptr,
                                    reinterpret_cast<u32>(arenaMemory),
                                    isImmutableSpan);
    if (!snapshot.chain && chainArena->used()) {
      // A bounded arena may fill mid-frame. Complete every previous reader
      // before reclaiming; preserve submission order and retry once.
      drain();
      ++arenaResets;
      snapshot = chainArena->copy(chain, qwords, resolveChainRef, nullptr,
                                 reinterpret_cast<u32>(arenaMemory),
                                 isImmutableSpan);
    }
    if (snapshot.chain) {
      chain = snapshot.chain;
      if (sourceCopied) *sourceCopied = true;
      if (chainArena->used() > arenaPeak) arenaPeak = chainArena->used();
      arenaFrameBytes += snapshot.bytes;
      arenaFrameBorrowed += snapshot.borrowedBytes;
    } else ++arenaFallbacks;
  } else ++arenaFallbacks;
#if !TYRA_VIF1_QUEUE_LAZY_FLUSH
  // Snapshot writes occurred after the producer's eager write-back.
  FlushCache(0);
#endif
  }
#endif
#if TYRA_VIF1_QUEUE_HOLD
  {
    static_assert(!TYRA_VIF1_QUEUE_ISR, "the hold probe runs without the ISR");
    const u32 addr = reinterpret_cast<u32>(chain);
    if (tail - head >= kDepth) releaseHeld();  // too many: measurement void
    while (tail - head >= kDepth) advanceIfIdle();
    u32 sequence = submitted + 1;
    if (!sequence) sequence = 1; // zero is the source-free/no-fence sentinel
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
  u32 sequence = submitted + 1;
  if (!sequence) sequence = 1;
  submitted = sequence;
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
  static_assert(!TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_CHAIN_ARENA,
                "native recording needs owned sources");
  if ((TYRA_NATIVE_VIF_RECORD || frameRecording) && nativeWriter &&
      sourceCopied && *sourceCopied) {
    u32 records = 0;
    const u32* ownedWords = static_cast<const u32*>(chain);
    for (u32 at = 0; at < qwords; ++records) {
      const u32 header = ownedWords[at * 4];
      const u32 id = (header >> 28) & 7;
      at += (id == 1 || id == 7) ? 1 + (header & 65535) : 1;
    }
    if (nativeInFlight) {
      waitFor(nativeInFlight);
      nativeInFlight = 0;
      nativeWriter->reset();
    }
    if (records > nativeWriter->availableQwords()) {
      flushRecording();
      if (nativeInFlight) waitFor(nativeInFlight);
      nativeInFlight = 0;
      nativeWriter->reset();
    }
    if (records <= nativeWriter->availableQwords()) {
      const u32 mark = nativeWriter->checkpoint();
      const u32* words = static_cast<const u32*>(chain);
      bool recorded = true;
      for (u32 at = 0; at < qwords;) {
        const u32* tag = words + at * 4;
        const u32 size = tag[0] & 65535, id = (tag[0] >> 28) & 7;
        if (id == 1 || id == 7) {
          // The snapshot already owns inline data. Reference it directly
          // rather than copying it a second time into the native prefix.
          recorded = size ? nativeWriter->referencedVif(tag[2], tag[3],
              reinterpret_cast<u32>(tag + 4), size, resolveChainRef) :
              nativeWriter->inlineVif(tag[2], tag[3], nullptr, 0);
          at += 1 + size;
        } else if (id == 0 || id == 3) {
          recorded = nativeWriter->referencedVif(tag[2], tag[3], tag[1], size,
                                                resolveChainRef);
          ++at;
        } else recorded = false;
        if (!recorded) break;
      }
      if (recorded) { nativePendingSequence = sequence; return sequence; }
      nativeWriter->rewind(mark);
    }
  }
  // Unsupported/unowned or oversized operations retain immediate ordering.
  flushRecording();
#endif
  enqueue(chain, sequence);
  return sequence;
}

void Vif1Queue::enqueue(const void* chain, u32 sequence) {
#if TYRA_FRAME_PIPELINE_SUPPORT
  // An oversized/unowned first operation has no prefix to flush. It still
  // must hand off the previous display target before starting direct DMA.
  if (frameRecording) completeBeforeSubmission();
#endif
  const u32 addr = reinterpret_cast<u32>(chain);
  advanceIfIdle();  // also what starts the queue when the interrupt does not
  // Back-pressure is separate from the producer's work-buffer count in the
  // arena arm; every queued source is an immutable snapshot there.
  while (tail - head >= kRingDepth) advanceIfIdle();
  if (!running) {
    // Idle as far as the queue knows, but a caller outside the queue may have
    // a chain of its own on the channel: wait it out before taking the
    // channel, exactly as the stock send does.
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  }
#if TYRA_VIF1_QUEUE_ISR
  DIntr();
#endif
  if (!running) {
    running = true;
    start(addr, sequence);
  } else {
    ring[tail % kRingDepth] = addr;
    ringSeq[tail % kRingDepth] = sequence;
    tail = tail + 1;
  }
#if TYRA_VIF1_QUEUE_ISR
  EIntr();
#endif
}

void Vif1Queue::waitFor(u32 sequence) {
  if (!sequence) return;
  flushRecording();
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
  flushRecording();
#if TYRA_FRAME_PIPELINE_SUPPORT
  if (frameRecording) completeBeforeSubmission();
#endif
#if TYRA_VIF1_QUEUE_HOLD
  releaseHeld();
#endif
  while (running) advanceIfIdle();
#if TYRA_VIF1_QUEUE_HOLD
  closeSegmentIfIdle();
#endif
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  // Actual DMA completion, never a guessed number of frame ticks.
  if (chainArena
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
      && !protectRecording
#endif
      ) {
    chainArena->reset();
    immutableSpans.releaseBanks(3);
  }
#endif
}

bool Vif1Queue::busy() { return running; }

bool Vif1Queue::recordingFrame() {
#if TYRA_ORDERED_FRAME || TYRA_FRAME_PIPELINE_SUPPORT
  return frameRecording;
#else
  return false;
#endif
}

void Vif1Queue::synchronizeExternal() {
#if TYRA_FRAME_PIPELINE_SUPPORT
  if (runtimePipeline && !frameRecording && !protectRecording)
    completeBeforeSubmission();
#endif
}

void Vif1Queue::beginRecordingFrame(bool pipeline) {
#if TYRA_ORDERED_FRAME || TYRA_FRAME_PIPELINE_SUPPORT
  if (!TYRA_ORDERED_FRAME && !pipeline) return;
  static_assert(!TYRA_ORDERED_FRAME || (TYRA_NATIVE_VIF_RECORD && TYRA_FRAME_CHAIN_ARENA),
                "ordered frame requires native owned recording");
  prepareArena();
  if (pipeline) {
    ++pipelineStarts;
    if (*kChcr & kStr) ++pipelineBusyStarts;
    currentBank ^= 1;
    if (!arenaBanks[currentBank] ||
        !nativeStorage[currentBank].data()) pipeline = false;
    else {
      waitFor(bankSequences[currentBank]);
      arenaMemory = arenaBanks[currentBank];
      chainArena = &arenaStorage[currentBank];
      nativeWriter = &nativeStorage[currentBank];
      immutableSpans.releaseBanks(1u << currentBank);
      chainArena->reset();
    }
  }
  if (!pipeline) drain();
  if (!nativeWriter || !chainArena) return;
  nativeInFlight = 0;
  nativeWriter->reset();
  frameBatchStart = nativeBatches;
  framePipelined = pipeline;
  frameRecording = true;
#else
  (void)pipeline;
#endif
}

void Vif1Queue::endRecordingFrame() {
#if TYRA_ORDERED_FRAME || TYRA_FRAME_PIPELINE_SUPPORT
  if (!frameRecording) return;
  closeOpenChain();
  recordBarrier();
  if (framePipelined) {
    // The sole presentation FINISH lives in this frame's native chain. CPU
    // presentation consumes it; it must not send a second VIF1 helper chain.
    alignas(16) qword_t finish[2];
    packet2_t packet = {};
    packet.base = finish;
    packet.next = draw_finish(finish);
    packet.mode = P2_MODE_NORMAL;
    if (!recordGif(&packet)) {
      printf("FRAMEPIPELINE final FINISH recording failed\n");
      fflush(stdout); SleepThread(); return;
    }
  }
  frameRecording = false;
  flushRecording();
  if (!framePipelined) {
    drain();
    volatile u32* const stat = reinterpret_cast<volatile u32*>(0x10003c00);
    while (*stat & 0x1f000003) {}
  }
  if (nativeBatches - frameBatchStart > 1) ++frameSplitCount;
#endif
}

u32 Vif1Queue::lastSequence() { return submitted; }
bool Vif1Queue::pipelineAvailable() {
#if TYRA_FRAME_PIPELINE_SUPPORT || (TYRA_FRAME_PIPELINE && TYRA_NATIVE_VIF_RECORD && TYRA_FRAME_CHAIN_ARENA)
  if (!TYRA_VIF1_QUEUE || TYRA_VIF1_QUEUE_ISR || TYRA_VIF1_QUEUE_HOLD) return false;
  prepareArena();
  return arenaBanks[0] && arenaBanks[1] && nativeStorage[0].data() &&
         nativeStorage[1].data();
#else
  return false;
#endif
}
void Vif1Queue::setRuntimePipeline(bool enabled) {
#if TYRA_FRAME_PIPELINE_SUPPORT || TYRA_FRAME_CHAIN_ARENA
  runtimePipeline = enabled;
#else
  (void)enabled;
#endif
}
void Vif1Queue::setBeforeFrameSubmit(void (*callback)(void*), void* user) {
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
  beforeFrameSubmit = callback; beforeFrameUser = user;
#else
  (void)callback; (void)user;
#endif
}

void Vif1Queue::recordBarrier() {
#if TYRA_ORDERED_FRAME || TYRA_FRAME_PIPELINE_SUPPORT
  if (!frameRecording) return;
  closeOpenChain();
  if (nativeInFlight || !nativeWriter->availableQwords()) {
    flushRecording();
    if (nativeInFlight) waitFor(nativeInFlight);
    nativeInFlight = 0;
    nativeWriter->reset();
  }
  // FLUSHA orders all GIF paths and VU1 without stopping the EE recorder.
  nativeWriter->inlineVif(0x13000000, 0, nullptr, 0);
  u32 sequence = submitted + 1;
  if (!sequence) sequence = 1;
  submitted = nativePendingSequence = sequence;
#endif
}

bool Vif1Queue::recordGif(const packet2_t* packet) {
#if TYRA_ORDERED_FRAME || TYRA_FRAME_PIPELINE_SUPPORT
  if (!frameRecording || !packet) return false;
  if (packet->mode != P2_MODE_CHAIN && packet->mode != P2_MODE_NORMAL) return false;
  closeOpenChain();
  const u32 qwords = packet2_get_qw_count(const_cast<packet2_t*>(packet));
  recordBarrier();
  const void* source = packet->base;
  if (packet->mode == P2_MODE_CHAIN) {
    auto snapshot = chainArena->copy(source, qwords, resolveChainRef, nullptr,
                                    reinterpret_cast<u32>(arenaMemory));
    if (!snapshot.chain) {
      drain();
      ++arenaResets;
      snapshot = chainArena->copy(source, qwords, resolveChainRef, nullptr,
                                 reinterpret_cast<u32>(arenaMemory));
    }
    if (!snapshot.chain) { ++arenaFallbacks; return false; }
    source = snapshot.chain;
    arenaFrameBytes += snapshot.bytes;
    if (chainArena->used() > arenaPeak) arenaPeak = chainArena->used();
  }
  u32 required = qwords + 1;
  if (packet->mode == P2_MODE_CHAIN) {
    required = 0;
    const u32* words = static_cast<const u32*>(source);
    for (u32 at = 0; at < qwords;) {
      const u32 header = words[at * 4], id = (header >> 28) & 7;
      const u32 size = header & 65535;
      if (size) ++required;
      at += (id == 1 || id == 7) ? 1 + size : 1;
    }
  }
  if (required > nativeWriter->availableQwords()) {
    flushRecording();
    if (nativeInFlight) waitFor(nativeInFlight);
    nativeInFlight = 0;
    nativeWriter->reset();
  }
  const u32 mark = nativeWriter->checkpoint();
  bool recorded = false;
  if (packet->mode == P2_MODE_NORMAL) {
    recorded = !qwords || nativeWriter->inlineVif(0, 0x50000000 | qwords,
                                                 source, qwords);
  } else {
    const u32* words = static_cast<const u32*>(source);
    recorded = true;
    for (u32 at = 0; at < qwords;) {
      const u32* tag = words + at * 4;
      const u32 size = tag[0] & 65535, id = (tag[0] >> 28) & 7;
      if (id == 1 || id == 7) {
        if (size) recorded = nativeWriter->referencedVif(0, 0x50000000 | size,
                          reinterpret_cast<u32>(tag + 4), size, resolveChainRef);
        at += 1 + size;
      } else if (id == 0 || id == 3) {
        if (size) recorded = nativeWriter->referencedVif(0, 0x50000000 | size,
                                      tag[1], size, resolveChainRef);
        ++at;
      } else recorded = false;
      if (!recorded) break;
    }
  }
  if (!recorded) { nativeWriter->rewind(mark); ++arenaFallbacks; return false; }
  u32 sequence = submitted + 1;
  if (!sequence) sequence = 1;
  submitted = nativePendingSequence = sequence;
  return true;
#else
  (void)packet;
  return false;
#endif
}

void Vif1Queue::flushRecording() {
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
  if (!nativePendingSequence) return;
  const u32 sequence = nativePendingSequence;
  nativePendingSequence = 0;
  const u32 qwords = nativeWriter->finish();
  completeBeforeSubmission();
  // Previous presentation consumed its bit before this prefix starts. Only
  // the final prefix emits FINISH, so overflow prefixes can clear it safely.
  if (framePipelined) *GS_REG_CSR |= 2;
#if TYRA_VIF1_CHAIN_CHECK
  const auto checked = Vif1ChainCheck::validate(nativeWriter->data(), qwords,
                                               resolveChainRef);
  if (!checked) {
    printf("NATIVEVIF rejected error=%u tag=%lu cmd=%lu pending=%lu\n",
           static_cast<unsigned>(checked.error),
           static_cast<unsigned long>(checked.tag),
           static_cast<unsigned long>(checked.command),
           static_cast<unsigned long>(checked.pending));
    fflush(stdout); SleepThread(); return;
  }
#else
  (void)qwords;
#endif
  // Logical source sequence numbers were reserved while recording. Force a
  // write-back of the finalized native prefix before its actual DMA starts.
#if TYRA_VIF1_QUEUE_LAZY_FLUSH
  flushedUpTo = sequence - 1;
#else
  FlushCache(0);
#endif
  enqueue(nativeWriter->data(), sequence);
  nativeInFlight = sequence;
  bankSequences[currentBank] = sequence;
  ++nativeBatches;
#endif
}

bool Vif1Queue::registerImmutableSpan(const void* base, u32 qwords) {
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  const u32 address = reinterpret_cast<u32>(base);
  if ((address & 15) || !qwords || !resolveChainRef(address, qwords, nullptr))
    return false;
  return immutableSpans.add(address, qwords);
#else
  (void)base; (void)qwords;
#endif
  return false; // safe copy fallback when metadata is full
}

void Vif1Queue::unregisterImmutableSpan(const void* base) {
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  const u32 address = reinterpret_cast<u32>(base);
  if (address && immutableSpans.hasBase(address)) {
      // Real DMA readers, not the baked cache's historical two-frame delay.
      if (chainArena) drain();
      immutableSpans.remove(address);
      return;
  }
#else
  (void)base;
#endif
}

void Vif1Queue::retireImmutableSpan(const void* base) {
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  immutableSpans.retire(reinterpret_cast<u32>(base));
#else
  (void)base;
#endif
}

bool Vif1Queue::immutableSpanReady(const void* base) {
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  return immutableSpans.reclaimable(reinterpret_cast<u32>(base));
#else
  (void)base;
  return true;
#endif
}

bool Vif1Queue::collectRetiredImmutableSpan(const void* base) {
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  return immutableSpans.collectRetired(reinterpret_cast<u32>(base));
#else
  (void)base;
  return true;
#endif
}

void Vif1Queue::arenaFrameEnd() {
#if TYRA_FRAME_CHAIN_ARENA || TYRA_FRAME_PIPELINE_SUPPORT
  if (!TYRA_FRAME_CHAIN_ARENA && !runtimePipeline) return;
  static u32 frames = 0;
  if (arenaFrameBytes > arenaMaxFrameBytes) arenaMaxFrameBytes = arenaFrameBytes;
  if (arenaFrameBorrowed > arenaMaxBorrowed) arenaMaxBorrowed = arenaFrameBorrowed;
  arenaFrameBytes = 0;
  arenaFrameBorrowed = 0;
  if (++frames % 120 == 0) {
    Info memoryInfo;
    printf("FRAMEARENA frames=%lu peak=%lu resets=%lu fallbacks=%lu "
           "maxFrameBytes=%lu maxBorrowed=%lu freeKiB=%lu\n",
           static_cast<unsigned long>(frames),
           static_cast<unsigned long>(arenaPeak),
           static_cast<unsigned long>(arenaResets),
           static_cast<unsigned long>(arenaFallbacks),
           static_cast<unsigned long>(arenaMaxFrameBytes),
           static_cast<unsigned long>(arenaMaxBorrowed),
           static_cast<unsigned long>(memoryInfo.getAvailableRAM() * 1024.0F));
    fflush(stdout);
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
    printf("NATIVEVIF frames=%lu batches=%lu\n",
           static_cast<unsigned long>(frames),
           static_cast<unsigned long>(nativeBatches));
    fflush(stdout);
#if TYRA_ORDERED_FRAME || TYRA_FRAME_PIPELINE_SUPPORT
    printf("ORDEREDFRAME frames=%lu splitFrames=%lu\n",
           static_cast<unsigned long>(frames),
           static_cast<unsigned long>(frameSplitCount));
    fflush(stdout);
    printf("FRAMEPIPELINE starts=%lu dmaBusyStarts=%lu\n",
           static_cast<unsigned long>(pipelineStarts),
           static_cast<unsigned long>(pipelineBusyStarts));
    fflush(stdout);
#endif
#endif
  }
#endif
}

void Vif1Queue::setOpenChainCloser(void (*closer)()) {
  openChainCloser = closer;
}

}  // namespace Tyra

/* Modified by TyraX: ordered frame recording adapters. Apache 2.0. */
#pragma once
#include <packet2.h>
#include <draw.h>
#include <dma.h>
#include "vif1_queue.hpp"

namespace Tyra {
// These are ordering waits, not resource destruction/readback fences.
inline void frameWaitVif() {
  if (Vif1Queue::recordingFrame()) Vif1Queue::recordBarrier();
  else Vif1Queue::drain();
}
inline void frameWaitGif(int channel, int timeout) {
  if (!Vif1Queue::recordingFrame()) dma_channel_wait(channel, timeout);
}
inline void frameWaitFinish() {
  if (!Vif1Queue::recordingFrame()) draw_wait_finish();
}
inline qword_t* frameDrawFinish(qword_t* q) {
  if (!Vif1Queue::recordingFrame()) return draw_finish(q);
  // Preserve EOP, but avoid a stray FINISH releasing the final frame fence.
  q->dw[0] = 0x8000; q->dw[1] = 0;
  return q + 1;
}
inline void frameSendPacket(packet2_t* packet, int channel, bool flush) {
  const bool recording = Vif1Queue::recordingFrame();
  if (recording) {
    if (channel == DMA_CHANNEL_GIF && Vif1Queue::recordGif(packet)) return;
    if (channel == DMA_CHANNEL_VIF1 && packet->mode == P2_MODE_CHAIN) {
      bool copied = false;
      const u32 sequence = Vif1Queue::submit(packet->base,
          packet2_get_qw_count(packet), &copied);
      if (!copied) Vif1Queue::waitFor(sequence);
      return;
    }
    // Order an unsupported direct transfer after all recorded VU/GIF work.
    // DMA idle alone can leave the final FLUSHA waiting in the VIF FIFO.
    Vif1Queue::recordBarrier();
    Vif1Queue::drain();
    volatile u32* const stat = reinterpret_cast<volatile u32*>(0x10003c00);
    while (*stat & 0x1f000003) {}

  }
  if (!recording) Vif1Queue::synchronizeExternal();
  // Presentation above can itself start GIF DMA (hybrid framebuffer copy).
  // A producer's earlier pre-wait cannot protect this send from that work.
  // Unsupported recording fallbacks also present before sending directly.
  dma_channel_wait(channel, 0);
  dma_channel_send_packet2(packet, channel, flush);
  // The producer may immediately reuse/free a source after an ordering-only
  // wait. An unsupported borrowed transfer must finish before returning.
  if (recording) dma_channel_wait(channel, 0);
}
} // namespace Tyra

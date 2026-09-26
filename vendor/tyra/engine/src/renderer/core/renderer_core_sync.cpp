/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: align3D feeds the TYRA_VIF1_QUEUE_HOLD probe.
*/

#include "debug/hardware_trace.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"

namespace Tyra {

RendererCoreSync::RendererCoreSync() {}
RendererCoreSync::~RendererCoreSync() {}

void RendererCoreSync::init(Path3* t_path3, Path1* t_path1) {
  path3 = t_path3;
  path1 = t_path1;
}

// Modified by TyraX (comment only): these barriers spin-wait on the GS
// FINISH flag, a single event bit shared by every path. Any FINISH giftag
// that nothing consumes (there used to be one per 2D sprite and one in the
// screen clear) can land after clear() and release the wait before the real
// drain - which let late scene triangles erase the post fx film grain. Keep
// FINISH exclusive to send-then-wait handshakes.
void RendererCoreSync::align3D() {
  HardwareTrace::Scope trace("Align3D");
#if TYRA_VIF1_QUEUE_HOLD
  // Modified by TyraX: the GPU-only frame probe - a barrier's segment closes
  // at the last DMA, so the GS tail up to this FINISH is added to it.
  const u32 closedBefore = Vif1Queue::holdLastClose;
#endif
  clear();
  sendPath1Req();
  waitAndClear();
#if TYRA_VIF1_QUEUE_HOLD
  if (Vif1Queue::holdLastClose != closedBefore) {
    u32 now;
    __asm__ volatile("mfc0 %0, $9" : "=r"(now));
    Vif1Queue::holdBusyTicks += now - Vif1Queue::holdLastClose;
    Vif1Queue::holdLastClose = now;
  }
#endif
}

void RendererCoreSync::align2D() {
  clear();
  sendPath3Req();
  waitAndClear();
}

void RendererCoreSync::sendPath1Req() { path1->sendDrawFinishTag(); }

void RendererCoreSync::sendPath3Req() { path3->sendDrawFinishTag(); }

void RendererCoreSync::addPath1Req(packet2_t* packet) {
  path1->addDrawFinishTag(packet);
}

u8 RendererCoreSync::check() { return *GS_REG_CSR & 2; }

void RendererCoreSync::clear() { *GS_REG_CSR |= 2; }

void RendererCoreSync::waitAndClear() {
  HardwareTrace::Scope trace("GS_FINISH_wait");
  while (!check()) {
  }
  clear();
}

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: split-screen viewports (two-player games).
# Modified by TyraX: VIF1 waits go through Vif1Queue::drain(), so a chain
# queued by the static pipeline is finished before this code takes the channel
# (renderer/core/paths/path1/vif1_queue.hpp).
# Modified by TyraX: GIF-channel sends pass path3Fence() first.
*/

#include "renderer/core/paths/path1/frame_submission.hpp"
#include <dma.h>
#include "renderer/core/paths/path3/path3_fence.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "renderer/core/splitview/renderer_core_splitview.hpp"
#include "debug/debug.hpp"

namespace Tyra {

RendererCoreSplitView::RendererCoreSplitView() {}

RendererCoreSplitView::~RendererCoreSplitView() {
  if (beginPackets[0]) packet2_free(beginPackets[0]);
  if (beginPackets[1]) packet2_free(beginPackets[1]);
  if (endPacket) packet2_free(endPacket);
}

void RendererCoreSplitView::init(RendererSettings* t_settings,
                                 RendererCoreGS* t_gs, RendererCoreSync* t_sync,
                                 Path1* t_path1) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  if (endPacket == nullptr) {
    endPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  }
}

// The two per-half raster-shift packets are pure constants once the display
// geometry is known, so they are built exactly once and resent verbatim
// every frame - the DMA re-reads immutable memory, nothing is rebuilt per
// call.
//
// The projection is untouched, so geometry rasterizes into the usual h-tall
// window centered at GS y 2048. Shifting XYOFFSET so the CENTRAL halfH rows
// of that window land on framebuffer rows [y0, y0+halfH) gives each player a
// vertical crop of the normal view - per-pixel scale (and circle roundness)
// is identical to full screen.
//
// No per-half clear: beginFrame's full-screen clear already reset the whole
// framebuffer AND z-buffer this frame, and the halves cannot dirty each
// other's region - the scissor clips every raster write, z included.
void RendererCoreSplitView::prepareBeginPackets() {
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getHeight());
  const int halfH = h / 2;

  for (int half = 0; half < 2; ++half) {
    const int y0 = half * halfH;
    const float xyoffX = 2048.0F - (w / 2.0F);
    const float xyoffY = (2048.0F - (halfH / 2.0F)) - static_cast<float>(y0);

    // Modified by TyraX: a one-tag DMA CHAIN, not a NORMAL packet.
    // frameSendPacket records a chain into the frame like any other VIF1
    // work, but treats a NORMAL packet as an unsupported direct transfer:
    // barrier, full Vif1Queue::drain() and a waited DMA - the EE sat out the
    // previous frame and its vsync. On two-players (PAL, split) begin(0) was
    // ~21 ms and begin(1) ~7 ms of a 25 ms frame on a PS2. TTE: the END
    // tag's upper half carries FLUSH + DIRECT, the payload is one PACKED A+D
    // giftag with exactly two register rows (an undercounted NLOOP would
    // stall the GIF forever).
    packet2_t* p = packet2_create(6, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
    packet2_chain_open_end(p, 0, 0);
    packet2_vif_flush(p, 0);
    packet2_vif_open_direct(p, 0);
    packet2_add_2x_s64(p, GIF_SET_TAG(2, 1, 0, 0, GIF_FLG_PACKED, 1),
                       GIF_REG_AD);
    packet2_add_2x_s64(p,
                       GS_SET_XYOFFSET(static_cast<int>(xyoffX * 16.0F),
                                       static_cast<int>(xyoffY * 16.0F)),
                       GS_REG_XYOFFSET);  // drawing context 1
    packet2_add_2x_s64(p, GS_SET_SCISSOR(0, w - 1, y0, y0 + halfH - 1),
                       GS_REG_SCISSOR_1);
    packet2_vif_close_direct_auto(p);
    packet2_chain_close_tag(p);
    beginPackets[half] = p;
  }
}

void RendererCoreSplitView::begin(const int& half) {
  if (beginPackets[0] == nullptr) prepareBeginPackets();

  // Queued on VIF1 behind everything already submitted (the previous half's
  // 3D, or pre-split geometry): the in-stream FLUSH makes the VIF wait, not
  // the EE. The channel wait below only covers the DMA queue - by the time
  // the next half's first mesh is packaged, it has drained.
  frameWaitVif();
  frameSendPacket(beginPackets[half], DMA_CHANNEL_VIF1, true);
}

void RendererCoreSplitView::end() {
  // Drain the last half's PATH1 stream before widening the raster back out -
  // late triangles would otherwise escape their half. This one stays a CPU
  // handshake: the HUD/menus/post-fx that follow arrive over PATH3, which a
  // VIF-queued restore could not order against.
  if (path1->isVU1Configured()) sync->align3D();

  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getHeight());

  packet2_reset(endPacket, false);
  qword_t* q = endPacket->base;
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, w - 1, 0, h - 1), GS_REG_SCISSOR_1);
  q++;
  packet2_update(endPacket, q);
  packet2_update(endPacket,
                 draw_primitive_xyoffset(endPacket->next, 0,
                                         2048.0F - (w / 2.0F),
                                         2048.0F - (h / 2.0F)));
  packet2_update(endPacket, frameDrawFinish(endPacket->next));
  frameWaitGif(DMA_CHANNEL_GIF, 0);
  path3Fence();  // Modified by TyraX: path3_fence.hpp
  frameSendPacket(endPacket, DMA_CHANNEL_GIF, true);
  frameWaitFinish();
}

}  // namespace Tyra

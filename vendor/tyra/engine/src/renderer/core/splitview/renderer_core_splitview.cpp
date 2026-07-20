/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: split-screen viewports (two-player games).
*/

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "renderer/core/splitview/renderer_core_splitview.hpp"
#include "debug/debug.hpp"

namespace Tyra {

// VIF1 opcodes (ps2sdk vif_codes.h values, inlined as literals): FLUSH makes
// the VIF itself wait for the running microprogram and the end of PATH1/PATH2
// transfers; DIRECT streams the following qwords to the GIF over PATH2. The
// DIRECT immediate counts qwords INCLUDING the giftag: 1 tag + 2 A+D rows.
static constexpr u32 kVifNop = 0x00000000u;
static constexpr u32 kVifFlush = 0x11000000u;
static constexpr u32 kVifDirect3 = 0x50000003u;

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

    packet2_t* p = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    qword_t* q = p->base;
    // qword 0: [FLUSH, NOP, NOP, DIRECT(3)] - VIF words are consumed in
    // order, so the flush lands before the register writes and the DIRECT
    // payload below starts qword-aligned.
    q->sw[0] = (s32)kVifFlush;
    q->sw[1] = (s32)kVifNop;
    q->sw[2] = (s32)kVifNop;
    q->sw[3] = (s32)kVifDirect3;
    q++;
    // qwords 1..3: one PACKED A+D giftag with exactly two register rows.
    // (An undercounted NLOOP here would stall the GIF forever - 2 rows,
    // NREG 1, EOP set, 3 qwords total, matching kVifDirect3.)
    PACK_GIFTAG(q, GIF_SET_TAG(2, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    q++;
    PACK_GIFTAG(q,
                GS_SET_XYOFFSET(static_cast<int>(xyoffX * 16.0F),
                                static_cast<int>(xyoffY * 16.0F)),
                GS_REG_XYOFFSET);  // + 0: drawing context 1
    q++;
    PACK_GIFTAG(q, GS_SET_SCISSOR(0, w - 1, y0, y0 + halfH - 1),
                GS_REG_SCISSOR_1);
    q++;
    packet2_update(p, q);
    beginPackets[half] = p;
  }
}

void RendererCoreSplitView::begin(const int& half) {
  if (beginPackets[0] == nullptr) prepareBeginPackets();

  // Queued on VIF1 behind everything already submitted (the previous half's
  // 3D, or pre-split geometry): the in-stream FLUSH makes the VIF wait, not
  // the EE. The channel wait below only covers the DMA queue - by the time
  // the next half's first mesh is packaged, it has drained.
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(beginPackets[half], DMA_CHANNEL_VIF1, true);
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
  packet2_update(endPacket, draw_finish(endPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(endPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

}  // namespace Tyra

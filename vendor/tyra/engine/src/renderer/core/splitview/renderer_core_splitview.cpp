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

RendererCoreSplitView::RendererCoreSplitView() {}

RendererCoreSplitView::~RendererCoreSplitView() {
  if (beginPacket) packet2_free(beginPacket);
  if (endPacket) packet2_free(endPacket);
}

void RendererCoreSplitView::init(RendererSettings* t_settings,
                                 RendererCoreGS* t_gs, RendererCoreSync* t_sync,
                                 Path1* t_path1) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  if (beginPacket == nullptr) {
    beginPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    endPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  }
}

void RendererCoreSplitView::begin(const int& half, const Color& clearColor) {
  // Drain in-flight PATH1 3D work (the previous half, or pre-split geometry) -
  // the raster shift below is global GS state.
  if (path1->isVU1Configured()) sync->align3D();

  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getHeight());
  const int halfH = h / 2;
  const int y0 = half * halfH;

  // The projection is untouched, so geometry rasterizes into the usual
  // h-tall window centered at GS y 2048. Shifting XYOFFSET so the CENTRAL
  // halfH rows of that window land on framebuffer rows [y0, y0+halfH) gives
  // each player a vertical crop of the normal view - per-pixel scale (and
  // circle roundness) is identical to full screen.
  const float xyoffX = 2048.0F - (w / 2.0F);
  const float xyoffY = (2048.0F - (halfH / 2.0F)) - static_cast<float>(y0);

  packet2_reset(beginPacket, false);
  packet2_update(beginPacket,
                 draw_primitive_xyoffset(beginPacket->base, 0, xyoffX,
                                         xyoffY));
  qword_t* q = beginPacket->next;
  // NLOOP = 6: SCISSOR, TEST, RGBAQ, PRIM and the clear sprite's two XYZ2 -
  // a miscount stalls the GIF (the stray qword parses as a new giftag).
  PACK_GIFTAG(q, GIF_SET_TAG(6, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, w - 1, y0, y0 + halfH - 1),
              GS_REG_SCISSOR_1);
  q++;
  // All-pass test with unmasked ZBUF: the sprite below paints the half's
  // background color AND resets its region of the shared z-buffer to 0.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_RGBAQ(static_cast<u8>(clearColor.r),
                           static_cast<u8>(clearColor.g),
                           static_cast<u8>(clearColor.b), 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  // Sprite in the shifted offset's space: framebuffer row r sits at GS y
  // r + xyoffY, so the half [y0, y0+halfH) spans the central rows of the
  // 2048-centered window.
  PACK_GIFTAG(q,
              GS_SET_XYZ((2048 - (w / 2)) << 4,
                         (2048 - (halfH / 2)) << 4, 0),
              GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ((2048 + (w / 2)) << 4,
                         (2048 + (halfH / 2)) << 4, 0),
              GS_REG_XYZ2);
  q++;
  packet2_update(beginPacket, q);
  // Restore the normal depth test for the half's real geometry.
  packet2_update(beginPacket,
                 draw_enable_tests(beginPacket->next, 0, &gs->zBuffer));
  packet2_update(beginPacket, draw_finish(beginPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(beginPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreSplitView::end() {
  // Drain the last half's PATH1 stream before widening the raster back out -
  // late triangles would otherwise escape their half.
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

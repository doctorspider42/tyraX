/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: destination-alpha shadow mask (flashlight shadow volumes).
*/

#include "renderer/core/alphamask/renderer_core_alpha_mask.hpp"

#include <dma.h>
#include <draw.h>
#include <gs_gp.h>

namespace Tyra {

RendererCoreAlphaMask::RendererCoreAlphaMask() {}

RendererCoreAlphaMask::~RendererCoreAlphaMask() {
  if (beginPacket) packet2_free(beginPacket);
  if (endPacket) packet2_free(endPacket);
  if (repaintPacket) packet2_free(repaintPacket);
  if (keepPacket) packet2_free(keepPacket);
}

void RendererCoreAlphaMask::init(RendererSettings* t_settings,
                                 RendererCoreGS* t_gs,
                                 RendererCoreSync* t_sync, Path1* t_path1) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  if (!beginPacket)
    beginPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!endPacket)
    endPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!repaintPacket)
    repaintPacket = packet2_create(24, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!keepPacket)
    keepPacket = packet2_create(8, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
}

void RendererCoreAlphaMask::begin() {
  // The redirect below is global GS state - anything still in flight must
  // land in the unmasked frame first.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  // Expose ONLY the alpha bits. FBMSK is specified in the frame format's own
  // pixel layout: PSMCT32 (psm 0) has A in the top byte; the 16-bit formats
  // pack two pixels per word with A as bit 15 of each half (docs/gs-vram.md).
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;

  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(beginPacket, false);
  qword_t* q = beginPacket->next;
  // NLOOP counts every register row - a mismatch stalls the GIF forever
  // (the shadow-map bracket's warning, and this packet froze a frame at
  // exactly the moment the first volume drew until the count was right).
  // Rows: FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2, ZBUF = 8.
  PACK_GIFTAG(q, GIF_SET_TAG(8, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // Same frame, color channels masked.
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  // The clear sprite: alpha 0 across the raster, no tests, and Z WRITES
  // MASKED - a full-screen sprite that stamped depth would erase the very
  // buffer the volume passes are about to test against.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(t.offsetX16, t.offsetY16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (w << 4), t.offsetY16 + (h << 4), 0),
              GS_REG_XYZ2);
  q++;
  // The scene z comes back before any volume bag runs; their in-band TEST
  // qwords re-arm the depth test themselves, exactly as they do everywhere.
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm,
                          gs->zBuffer.mask),
              GS_REG_ZBUF_1);
  q++;
  packet2_update(beginPacket, q);
  packet2_update(beginPacket, draw_finish(beginPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(beginPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::beginKeep() {
  // Re-mask the color channels without touching the alpha already written -
  // the walk in the generated game brackets each caster's volume separately
  // so its own light can draw first, and brackets after the first must not
  // reset what earlier casters put in the mask.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;

  packet2_reset(keepPacket, false);
  qword_t* q = keepPacket->next;
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  packet2_update(keepPacket, q);
  packet2_update(keepPacket, draw_finish(keepPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(keepPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::repaintAlpha() {
  // The mask LIVES in the framebuffer's alpha, and on the SDTV interlaced
  // modes that channel is live display state: presentFrameBuffer programs
  // ps2sdk's flicker filter (graph_set_framebuffer_filtered), whose PMODE
  // blends the two line-offset read circuits by PER-PIXEL alpha. Leave the
  // mask in place and the CRTC shows it - the volume shapes appear as
  // soft translucent wedges over the picture (found by frame-stepping a
  // torch toggle in PCSX2). So once the last DATE-gated light pass has
  // consumed the mask, the alpha byte is repainted to the 0x80 the rest of
  // the scene writes, colors untouched. Costs one alpha-only raster fill,
  // paid only on frames that drew a mask.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(repaintPacket, false);
  qword_t* q = repaintPacket->next;
  // Rows: FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2 = 7. The raster
  // restore below re-emits FRAME/tests/ZBUF with its own tag.
  PACK_GIFTAG(q, GIF_SET_TAG(7, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0x80, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(t.offsetX16, t.offsetY16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (w << 4), t.offsetY16 + (h << 4), 0),
              GS_REG_XYZ2);
  q++;
  packet2_update(repaintPacket, q);
  packet2_update(repaintPacket,
                 gs->emitRasterRestore(repaintPacket->next, false));
  packet2_update(repaintPacket, draw_finish(repaintPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(repaintPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::end() {
  // Drain the volume draws, then put the whole raster environment back -
  // emitRasterRestore rewrites FRAME with FBMSK 0, the scissor, the offset,
  // the tests and the scene ZBUF (the shadow-map bracket's lesson).
  if (path1->isVU1Configured()) sync->align3D();

  packet2_reset(endPacket, false);
  qword_t* q = gs->emitRasterRestore(endPacket->base, false);
  packet2_update(endPacket, q);
  packet2_update(endPacket, draw_finish(endPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(endPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

}  // namespace Tyra

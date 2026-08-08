/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: the frame-timing rig - see inc/debug/frame_profile.hpp.
*/

#include "debug/frame_profile.hpp"

#if TYRA_FRAME_PROFILE

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"

namespace Tyra {
namespace FrameProfile {

u32 tFrameWork = 0;
u32 tDrain = 0;
u32 tBlssBegin = 0;
u32 tBlssEnd = 0;
u32 tBlssComposite = 0;
u32 tBlssCompositeEe = 0;
u32 tBlssProxy = 0;
u32 tBlssReproj = 0;
u32 tBlssFeat = 0;
u32 tBlssNet = 0;
u32 tBlssPacket = 0;
u32 tExcluded = 0;

namespace {

/** log2 of a power of two, the way RendererCoreBlss::lg2 does it. */
int lg2(int v) {
  int n = 0;
  while ((1 << n) < v) n++;
  return n;
}

packet2_t* probePacket = nullptr;

}  // namespace

u32 gsFillProbe(RendererCoreGS* gs, RendererCoreSync* sync, Path1* path1,
                int k) {
  if (gs == nullptr || k <= 0) return 0;
  if (probePacket == nullptr)
    probePacket = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);

  // Start from a drained GS, or the first sprite's wait would also be paid for
  // whatever the scene left in flight and the k = 0 baseline would not be one.
  if (path1 != nullptr && path1->isVU1Configured() && sync != nullptr)
    sync->align3D();

  const framebuffer_t* fb = gs->getCurrentFrameBuffer();
  const framebuffer_t* src = gs->getPreviousFrameBuffer();
  const int w = static_cast<int>(fb->width);
  const int h = static_cast<int>(fb->height);
  const int offX16 = 2048 * 16;
  const int offY16 = 2048 * 16 + gs->getFieldYOffset16();

  const u32 t0 = ticks();
  for (int i = 0; i < k; i++) {
    packet2_reset(probePacket, false);
    qword_t* q = probePacket->base;
    // NLOOP = 17: TEXFLUSH TEX0 TEX1 CLAMP TEXA COLCLAMP FRAME ALPHA TEST ZBUF
    // XYOFFSET PRIM RGBAQ + (UV XYZ2) x 2. Miscount it and the GIF parses the
    // stray qword as a giftag with a garbage NLOOP and stalls forever.
    PACK_GIFTAG(q, GIF_SET_TAG(17, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    q++;
    PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
    q++;
    PACK_GIFTAG(q,
                GS_SET_TEX0(static_cast<int>(src->address) >> 6,
                            static_cast<int>(src->width) >> 6, GS_PSM_32,
                            lg2(w), lg2(h), 0 /* TCC: RGB */,
                            0 /* TFX: MODULATE */, 0, 0, 0, 0, 0),
                GS_REG_TEX0_1);
    q++;
    PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);  // linear
    q++;
    PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, 0, w - 1, 0, h - 1), GS_REG_CLAMP_1);
    q++;
    PACK_GIFTAG(q, GS_SET_TEXA(0x80, 0, 0x80), GS_REG_TEXA);
    q++;
    PACK_GIFTAG(q, GS_SET_COLCLAMP(COLOR_CLAMP_ENABLE), GS_REG_COLCLAMP);
    q++;
    PACK_GIFTAG(q,
                GS_SET_FRAME(static_cast<int>(fb->address) >> 11,
                             static_cast<int>(fb->width) >> 6, GS_PSM_32, 0),
                GS_REG_FRAME_1);
    q++;
    // Cs*As + Cd*(1-As) - a real blended pass, which is what BLSS' own passes
    // are; an opaque one would not exercise the same GS read-modify-write.
    PACK_GIFTAG(q, GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA_1);
    q++;
    PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
                GS_REG_TEST_1);
    q++;
    PACK_GIFTAG(q,
                GS_SET_ZBUF(static_cast<int>(gs->zBuffer.address) >> 11,
                            static_cast<int>(gs->zBuffer.zsm), 1 /* no z write */),
                GS_REG_ZBUF_1);
    q++;
    PACK_GIFTAG(q, GS_SET_XYOFFSET(offX16, offY16), GS_REG_XYOFFSET_1);
    q++;
    PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 1 /* tme */, 0, 1 /* abe */,
                               0, 1 /* fst: UV */, 0, 0),
                GS_REG_PRIM);
    q++;
    PACK_GIFTAG(q, GS_SET_RGBAQ(128, 128, 128, 0x80, 0x3F800000), GS_REG_RGBAQ);
    q++;
    PACK_GIFTAG(q, GS_SET_UV(0, 0), GS_REG_UV);
    q++;
    PACK_GIFTAG(q, GS_SET_XYZ(offX16, offY16, 0), GS_REG_XYZ2);
    q++;
    PACK_GIFTAG(q, GS_SET_UV(w << 4, h << 4), GS_REG_UV);
    q++;
    PACK_GIFTAG(q, GS_SET_XYZ(offX16 + w * 16, offY16 + h * 16, 0),
                GS_REG_XYZ2);
    q++;
    packet2_update(probePacket, q);
    packet2_update(probePacket, draw_finish(probePacket->next));
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_channel_send_packet2(probePacket, DMA_CHANNEL_GIF, true);
    // One handshake PER SPRITE: the point is to measure the raster, not to
    // find out how deep the GS queue is.
    draw_wait_finish();
  }
  return ticks() - t0;
}

}  // namespace FrameProfile
}  // namespace Tyra

#endif  // TYRA_FRAME_PROFILE

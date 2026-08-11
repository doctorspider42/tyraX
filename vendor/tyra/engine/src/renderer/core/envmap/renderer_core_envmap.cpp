/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: dynamic environment map (GT3-style reflective materials).
*/

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "renderer/core/envmap/renderer_core_envmap.hpp"
#include "debug/debug.hpp"

namespace Tyra {

RendererCoreEnvMap::RendererCoreEnvMap() {}

RendererCoreEnvMap::~RendererCoreEnvMap() {
  if (beginPacket) packet2_free(beginPacket);
  if (endPacket) packet2_free(endPacket);
  if (texture) delete texture;
}

void RendererCoreEnvMap::init(RendererSettings* t_settings,
                              RendererCoreGS* t_gs, RendererCoreSync* t_sync,
                              Path1* t_path1) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;

  // The target sits right above the frame/z/post-fx buffers, below every
  // texture: allocateBuffer() puts it in the permanent region under the
  // texture heap's floor, which texture eviction cannot reach. The dedicated
  // z-buffer lets "reflected" scene objects occlude each other in the map.
  vramAddress = gs->vram.allocateBuffer(size, size, GS_PSM_32);
  zVramAddress = gs->vram.allocateBuffer(size, size, GS_PSM_32);

  texBuffer.address = vramAddress;
  texBuffer.width = size;
  texBuffer.psm = GS_PSM_32;
  texBuffer.info.width = draw_log2(size);
  texBuffer.info.height = draw_log2(size);
  texBuffer.info.components = TEXTURE_COMPONENTS_RGB;
  texBuffer.info.function = TEXTURE_FUNCTION_MODULATE;

  if (texture == nullptr) {
    TextureBuilderData data;
    data.name = "dynamic-env-map";
    data.width = size;
    data.height = size;
    data.data = nullptr;  // VRAM-resident: rendered, never uploaded
    data.bpp = bpp32;
    data.gsComponents = TEXTURE_COMPONENTS_RGB;
    data.clut = nullptr;
    texture = new Texture(&data);
  }
  texture->vramResident = &texBuffer;

  if (beginPacket == nullptr) {
    beginPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    endPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  }

  TYRA_LOG("Dynamic env map initialized (VRAM at ", (int)vramAddress, ")");
}

void RendererCoreEnvMap::begin(const Color& clearColor) {
  // Drain in-flight PATH1 3D work - the raster redirect below is global GS
  // state. Before any 3D pipeline is up there is nothing to drain (and the
  // FINISH handshake would spin forever).
  if (path1->isVU1Configured()) sync->align3D();

  const int envZbp = static_cast<int>(zVramAddress) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);
  const int half = size / 2;

  packet2_reset(beginPacket, false);
  // Center the raster window on the target FIRST (the VU1 pipeline's fixed
  // 2048 scale expects a window-centered offset) - the clear sprite below is
  // addressed in this offset's space. With the offset written after it, the
  // sprite rasterized against the main window's offset, landed outside the
  // 0..size-1 scissor and never painted: the below-horizon half of the map
  // stayed black VRAM garbage, which reflective surfaces sampled as dark
  // feathering.
  packet2_update(beginPacket,
                 draw_primitive_xyoffset(beginPacket->base, 0,
                                         2048.0F - half, 2048.0F - half));
  qword_t* q = beginPacket->next;
  // NLOOP = 8: FRAME, SCISSOR, ZBUF, TEST, RGBAQ, PRIM and the clear
  // sprite's two XYZ2 - a miscount here stalls the GIF (parses the extra
  // qword as a new giftag) and hangs the frame in draw_wait_finish.
  PACK_GIFTAG(q, GIF_SET_TAG(8, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(vramAddress >> 11, size >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, size - 1, 0, size - 1), GS_REG_SCISSOR_1);
  q++;
  // Dedicated env z-buffer, writes ON - the pass renders real geometry
  // ("reflected" objects) that must occlude correctly inside the map.
  PACK_GIFTAG(q, GS_SET_ZBUF(envZbp, zsm, 0), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  // Clear sprite: paints the whole target with the horizon color (wide-FOV
  // sky views leave the below-horizon half uncovered by dome geometry) and,
  // with the all-pass test + unmasked ZBUF, resets the env depth to 0.
  PACK_GIFTAG(q,
              GS_SET_RGBAQ(static_cast<u8>(clearColor.r),
                           static_cast<u8>(clearColor.g),
                           static_cast<u8>(clearColor.b), 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 - half) << 4, (2048 - half) << 4, 0),
              GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + half) << 4, (2048 + half) << 4, 0),
              GS_REG_XYZ2);
  q++;
  packet2_update(beginPacket, q);
  packet2_update(beginPacket, draw_finish(beginPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(beginPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreEnvMap::end() {
  // Drain the env pass itself, then restore the frame drawing environment.
  if (path1->isVU1Configured()) sync->align3D();

  packet2_reset(endPacket, false);
  // Modified by TyraX: restore whatever was redirected BEFORE this bracket,
  // not the display buffer unconditionally. This pass runs inside the
  // generated renderScene(), which BLSS brackets - the old restore cancelled
  // that redirect and silently drew the rest of the frame at full resolution
  // (docs/neural-upscaler.md). RendererCoreGS::getRasterTarget() is the one
  // place that knows which it is.
  qword_t* q = gs->emitRasterRestore(endPacket->base, true);
  packet2_update(endPacket, q);
  packet2_update(endPacket, draw_finish(endPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(endPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

}  // namespace Tyra

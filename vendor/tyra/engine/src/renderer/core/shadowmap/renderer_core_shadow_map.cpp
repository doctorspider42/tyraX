/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: projected silhouette shadows (per-object render targets).
*/

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "renderer/core/shadowmap/renderer_core_shadow_map.hpp"
#include "debug/debug.hpp"

namespace Tyra {

RendererCoreShadowMap::RendererCoreShadowMap() {}

RendererCoreShadowMap::~RendererCoreShadowMap() {
  if (beginPacket) packet2_free(beginPacket);
  if (endPacket) packet2_free(endPacket);
  for (int i = 0; i < slots; i++)
    if (textures[i]) delete textures[i];
}

void RendererCoreShadowMap::init(RendererSettings* t_settings,
                                 RendererCoreGS* t_gs,
                                 RendererCoreSync* t_sync, Path1* t_path1) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  // A display-mode switch resets the whole VRAM map and re-runs init();
  // re-place our buffers when the game had turned them on.
  if (allocated) {
    allocated = false;
    allocate();
  }
}

void RendererCoreShadowMap::allocate() {
  if (allocated) return;

  // Same discipline as post-fx/env-map: allocated below every texture, so
  // the FIFO texture free can never rewind past them.
  for (int i = 0; i < slots; i++)
    vramAddress[i] = gs->vram.allocateBuffer(size, size, GS_PSM_32);
  zVramAddress = gs->vram.allocateBuffer(size, size, GS_PSM_32);

  for (int i = 0; i < slots; i++) {
    texBuffers[i].address = vramAddress[i];
    texBuffers[i].width = size;
    texBuffers[i].psm = GS_PSM_32;
    texBuffers[i].info.width = draw_log2(size);
    texBuffers[i].info.height = draw_log2(size);
    texBuffers[i].info.components = TEXTURE_COMPONENTS_RGBA;
    texBuffers[i].info.function = TEXTURE_FUNCTION_MODULATE;

    if (textures[i] == nullptr) {
      TextureBuilderData data;
      data.name = "shadow-map-slot";
      data.width = size;
      data.height = size;
      data.data = nullptr;  // VRAM-resident: rendered, never uploaded
      data.bpp = bpp32;
      data.gsComponents = TEXTURE_COMPONENTS_RGBA;
      data.clut = nullptr;
      textures[i] = new Texture(&data);
    }
    textures[i]->vramResident = &texBuffers[i];
  }

  if (beginPacket == nullptr) {
    beginPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    endPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  }

  allocated = true;
  TYRA_LOG("Shadow map slots initialized (VRAM at ", (int)vramAddress[0], ")");
}

void RendererCoreShadowMap::begin(const int slot) {
  TYRA_ASSERT(allocated, "Shadow map used before allocate()!");
  // Drain in-flight PATH1 work - the redirect below is global GS state.
  // Called per caster: the previous caster's silhouette must land in ITS
  // target before FRAME moves to the next one.
  if (path1->isVU1Configured()) sync->align3D();

  const int zbp = static_cast<int>(zVramAddress) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);
  const int half = size / 2;

  packet2_reset(beginPacket, false);
  // Window-centered raster offset first - the clear sprite below addresses
  // this space (see the env map's ordering pitfall).
  packet2_update(beginPacket,
                 draw_primitive_xyoffset(beginPacket->base, 0,
                                         2048.0F - half, 2048.0F - half));
  qword_t* q = beginPacket->next;
  // NLOOP counts every register write below - an undercount stalls the GIF.
  PACK_GIFTAG(q, GIF_SET_TAG(8, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(vramAddress[slot] >> 11, size >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, size - 1, 0, size - 1), GS_REG_SCISSOR_1);
  q++;
  // Shared shadow z-buffer, writes on - cleared by the sprite below, so a
  // multi-part caster rasterizes like normal geometry inside the slot.
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 0), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  // Clear: color AND alpha to 0 (alpha IS the silhouette coverage the
  // receiver samples; uncovered texels must stay fully transparent) and the
  // z to 0 through the all-pass test.
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
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

void RendererCoreShadowMap::end() {
  // Drain the last silhouette, then restore the frame drawing environment.
  if (path1->isVU1Configured()) sync->align3D();

  const auto* fb = gs->getCurrentFrameBuffer();
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);
  const int w = static_cast<int>(settings->getWidth());
  // Physical buffer height (half the logical one in InterlacedField) - this
  // restores the screen FRAME/SCISSOR/XYOFFSET after the silhouette pass.
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(endPacket, false);
  qword_t* q = endPacket->base;
  PACK_GIFTAG(q, GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // The targets were just rendered - drop stale texels from the GS texture
  // cache before the receiver patches sample them.
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(static_cast<int>(fb->address) >> 11,
                           static_cast<int>(fb->width) >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, w - 1, 0, h - 1), GS_REG_SCISSOR_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 0), GS_REG_ZBUF_1);
  q++;
  packet2_update(endPacket, q);
  packet2_update(endPacket,
                 draw_enable_tests(endPacket->next, 0, &gs->zBuffer));
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

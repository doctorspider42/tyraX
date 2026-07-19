/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: render() no longer emits a FINISH giftag.
*/

#include "renderer/core/2d/renderer_core_2d.hpp"
#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>

namespace Tyra {

RendererCore2D::RendererCore2D() {
  context = 0;
  packets[0] = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packets[1] = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  rects[0] = new texrect_t;
  rects[1] = new texrect_t;

  setPrim();
  setLod();
}

RendererCore2D::~RendererCore2D() {
  packet2_free(packets[0]);
  packet2_free(packets[1]);
  delete rects[0];
  delete rects[1];
}

const float RendererCore2D::GS_DRAW_AREA = 4096.0F;
const float RendererCore2D::SCREEN_CENTER = 4096.0F / 2.0F;

void RendererCore2D::setPrim() {
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = DRAW_ENABLE;
  prim.fogging = DRAW_DISABLE;
  prim.blending = DRAW_ENABLE;
  prim.antialiasing = DRAW_DISABLE;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;
}

void RendererCore2D::setLod() {
  lod.calculation = LOD_USE_K;
  lod.max_level = 0;
  lod.mag_filter = LOD_MAG_LINEAR;
  lod.min_filter = LOD_MIN_LINEAR;
  lod.mipmap_select = LOD_MIPMAP_REGISTER;
  lod.l = 0;
  lod.k = 0.0F;
}

void RendererCore2D::init(RendererSettings* t_settings,
                          clutbuffer_t* t_clutBuffer) {
  settings = t_settings;
  clutBuffer = t_clutBuffer;
}

void RendererCore2D::render(const Sprite& sprite,
                            const RendererCoreTextureBuffers& texBuffers,
                            Texture* texture) {
  auto* rect = rects[context];
  float sizeX, sizeY;

  if (sprite.mode == MODE_REPEAT) {
    sizeX = sprite.size.x;
    sizeY = sprite.size.y;
  } else {
    sizeX = static_cast<float>(texture->getWidth());
    sizeY = static_cast<float>(texture->getHeight());
  }

  float texS, texT;
  float texMax = texT = texS = sizeX > sizeY ? sizeX : sizeY;

  if (sizeX > sizeY)
    texT = texMax / (sizeX / sizeY);
  else if (sizeY > sizeX)
    texS = texMax / (sizeY / sizeX);

  rect->t0.s =
      sprite.flipHorizontal ? (texS + sprite.offset.x) : sprite.offset.x;
  rect->t0.t = sprite.flipVertical ? (texT + sprite.offset.y) : sprite.offset.y;
  rect->t1.s =
      sprite.flipHorizontal ? sprite.offset.x : (texS + sprite.offset.x);
  rect->t1.t = sprite.flipVertical ? sprite.offset.y : (texT + sprite.offset.y);

  rect->color.r = sprite.color.r;
  rect->color.g = sprite.color.g;
  rect->color.b = sprite.color.b;
  rect->color.a = sprite.color.a;
  rect->color.q = 0;

  rect->v0.x = sprite.position.x;
  rect->v0.y = sprite.position.y;
  // rect->v0.y /= 2.0F;  // interlacing
  rect->v0.z = (u32)-1;

  rect->v1.x = (sprite.size.x * sprite.scale) + sprite.position.x;
  rect->v1.y = (sprite.size.y * sprite.scale) + sprite.position.y;
  // rect->v1.y /= 2.0F;  // interlacing
  rect->v1.z = (u32)-1;

  auto* packet = packets[context];

  packet2_reset(packet, false);
  packet2_update(packet, draw_primitive_xyoffset(packet->base, 0, SCREEN_CENTER,
                                                 SCREEN_CENTER));

  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_lod(packet, &lod);
  // Modified by TyraX: pin the 2D blend equation. StaPip meshes carry
  // their blend equation IN-BAND (VU1_ALPHA_ADDR), so after the 3D scene
  // the GS ALPHA register holds whatever the last mesh set - after a
  // reflective env pass that is the ADDITIVE equation, and sprites
  // inheriting it lose their dark texels (on hardware the debug HUD
  // font's black outline visibly vanished). Every sprite sets its blend
  // explicitly: standard source-alpha, or additive (Cs*As + Cd) for
  // light-like overlays (Sprite::additive - lens flares, glows).
  packet2_utils_gif_add_set(packet, 1);
  packet2_add_2x_s64(packet,
                     sprite.additive ? GS_SET_ALPHA(0, 2, 0, 1, 0)
                                     : GS_SET_ALPHA(0, 1, 0, 1, 0),
                     GS_REG_ALPHA_1);
  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core, clutBuffer);
  draw_enable_blending();
  packet2_update(packet, draw_rect_textured(packet->next, 0, rect));

  packet2_update(packet, draw_primitive_xyoffset(
                             packet->next, 0,
                             SCREEN_CENTER - (settings->getWidth() / 2.0F),
                             SCREEN_CENTER - (settings->getHeight() / 2.0F)));
  draw_disable_blending();
  // Upstream ended this packet with draw_finish(), which does two distinct
  // jobs: its giftag carries EOP=1 (terminates the PATH3 stream at the GIF -
  // without it PATH1/XGKICK starves at GIF arbitration and the GS deadlocks
  // on the first 3D frame), and it writes the FINISH register. The FINISH
  // write is the harmful part: the GS FINISH flag is shared by every path,
  // and a stray un-consumed sprite FINISH landing inside the window where
  // RendererCoreSync::align3D() spin-waits released the post fx barrier
  // early, letting late scene triangles erase the film grain. So terminate
  // the stream with a data-less EOP giftag and skip the FINISH write -
  // FINISH stays exclusive to handshakes that consume it (align3D/align2D,
  // flip, post fx).
  {
    qword_t* q = packet->next;
    PACK_GIFTAG(q, GIF_SET_TAG(0, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    q++;
    packet2_update(packet, q);
  }

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);

  context = !context;
}

void RendererCore2D::setTextureMappingType(
    const PipelineTextureMappingType textureMappingType) {
  lod.mag_filter = textureMappingType;
  lod.min_filter = textureMappingType;
}

}  // namespace Tyra

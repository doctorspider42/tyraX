/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include "debug/debug.hpp"
#include "renderer/core/2d/sprite/sprite.hpp"
#include "renderer/core/texture/renderer_core_texture_buffers.hpp"
#include "renderer/3d/pipeline/shared/pipeline_texture_mapping_type.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include "renderer/renderer_settings.hpp"
#include <packet2_utils.h>
#include <draw2d.h>

namespace Tyra {

class RendererCore2D {
 public:
  RendererCore2D();
  ~RendererCore2D();

  void init(RendererSettings* settings, clutbuffer_t* clutBuffer);

  void render(const Sprite& sprite,
              const RendererCoreTextureBuffers& texBuffers, Texture* texture);

  void setTextureMappingType(
      const PipelineTextureMappingType textureMappingType);

  // Modified by TyraX: the logical height sprites are authored against (448).
  // render() centres that space in the actual framebuffer, so the 2D origin
  // stays on the top of the picture in the taller display modes.
  //
  // PUBLIC because it is a CONTRACT, not an implementation detail: anything
  // that centres a sprite for itself has to divide THIS, not
  // RendererSettings::getHeight(). Doing the latter centres a second time on
  // top of render()'s own centring and lands (height - 448) / 2 rows low - 46
  // in 1080i, 32 in PAL 576i - which is exactly how the boot banner regressed
  // the moment render() stopped assuming 448 (see info/banner.cpp).
  static const float SPRITE_SPACE_HEIGHT;

 private:
  void setPrim();
  void setLod();

  prim_t prim;
  lod_t lod;

  static const float GS_DRAW_AREA;
  static const float SCREEN_CENTER;

  u8 context;
  RendererSettings* settings;
  clutbuffer_t* clutBuffer;
  packet2_t* packets[2];
  texrect_t* rects[2];
};

}  // namespace Tyra

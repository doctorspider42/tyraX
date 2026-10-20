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

#include <dma.h>
#include <packet2_chain.h>
#include <draw.h>
#include "renderer/core/texture/models/texture.hpp"
#include "renderer/renderer_settings.hpp"
#include "renderer/models/color.hpp"
#include "renderer/core/texture/renderer_core_texture_buffers.hpp"

namespace Tyra {

/**
 * Modified by TyraX: the CLUT load method (docs/gs-vram.md, "CLUT loads").
 * true: every TEX0 the renderer emits asks for CLD = 4 ("load only when CBP
 * differs from CBP0, then CBP0 = CBP"), and every texture upload that carries a
 * CLUT re-syncs the GS-side bookkeeping with a context-2 TEX0 at CLD = 2 (load
 * it, CBP0 = it), so CBP0 always names the palette that is in the CLUT buffer
 * and the buffer is never stale after a CLUT is rewritten at a reused address.
 * false: the upstream CLD = 1 on every TEX0 (a reload per bag). Runtime so a
 * same-ELF A/B can flip it; the value is read when a TEX0 is built.
 */
extern bool gsClutLoadOnChange;
/**
 * Set by whoever may have desynchronised CBP0 from the CLUT buffer (turning
 * gsClutLoadOnChange on at runtime after CLD = 1 loads, a raw GS write that
 * loaded a palette): the next TEX0 that carries a CLUT is built with CLD = 2
 * (load, CBP0 = CBP) instead of 4, which puts the bookkeeping right again.
 */
extern bool gsClutResync;

class Path3 {
 public:
  Path3();
  ~Path3();

  void init(RendererSettings* settings);

  void sendDrawFinishTag();
  void clearScreen(zbuffer_t* z, const Color& color);
  void sendTexture(const Texture* texture,
                   const RendererCoreTextureBuffers& texBuffers);

 private:
  packet2_t* drawFinishPacket;
  packet2_t* clearScreenPacket;
  packet2_t* texturePacket;
  RendererSettings* settings;
};

}  // namespace Tyra

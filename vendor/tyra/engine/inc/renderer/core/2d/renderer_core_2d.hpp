/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: sprites can ride a VIF1 DIRECT chain (TYRA_2D_VIF1_DIRECT).
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

/**
 * TYRA_2D_VIF1_DIRECT: 1 = once VU1 is up, sprites are appended as DIRECT
 * (PATH2) data to a VIF1 chain that queues behind the 3D chains in Vif1Queue,
 * so the order on VIF1 replaces the once-a-frame PATH1 drain before the first
 * sprite, and no sprite waits for the GIF channel; 0 = the stock path, every
 * sprite its own PATH3 send after a sync.align3D(). The A/B control.
 * docs/ee-submission-rearchitecture.md, "The HUD on VIF1".
 */
#ifndef TYRA_2D_VIF1_DIRECT
#define TYRA_2D_VIF1_DIRECT 1
#endif

// Modified by TyraX: sprites in the VIF1 chain skip the GS state a sprite
// before them in the same chain already set (XYOFFSET, TEX1, ALPHA, TEX0)
// and are written straight into the chain - see renderIntoChain. 0 = every
// sprite carries its whole PATH3 packet, the 1.126.1 behaviour.
#ifndef TYRA_2D_CHAIN_FAST
#define TYRA_2D_CHAIN_FAST 1
#endif

namespace Tyra {

class RendererCore2D {
 public:
  RendererCore2D();
  ~RendererCore2D();

  void init(RendererSettings* settings, clutbuffer_t* clutBuffer);

  /**
   * Modified by TyraX: viaChain = append the sprite to the VIF1 DIRECT chain
   * instead of sending it over PATH3 (TYRA_2D_VIF1_DIRECT; the caller decides,
   * because the chain needs VU1 up). restoreRepeat = the chain opened for
   * this sprite must first program CLAMP back to REPEAT (the 3D pass leaves a
   * clamped bag's wrap behind; the stock path does that with its own drain).
   */
  void render(const Sprite& sprite,
              const RendererCoreTextureBuffers& texBuffers, Texture* texture,
              bool viaChain = false, bool restoreRepeat = false);

  /** Modified by TyraX: submits the open chain and waits until VIF1 has
   * passed every sprite on to the GIF - path3FenceFlush() lands here. */
  void fence();

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

  // Modified by TyraX: the VIF1 DIRECT chain (TYRA_2D_VIF1_DIRECT).
  void appendToChain(const qword_t* data, u32 qwc, bool restoreRepeat);
  void renderIntoChain(const Sprite& sprite,
                       const RendererCoreTextureBuffers& texBuffers,
                       texrect_t* rect, bool restoreRepeat);
  // The GS state the open chain last set, for renderIntoChain. Compared as
  // raw bytes, so it is zero-filled before every fill (padding included).
  struct ChainState {
    u32 tbAddress, tbWidth, tbPsm, tbInfoW, tbInfoH, tbComponents, tbFunction;
    u32 clutAddress, clutPsm, clutStorage, clutStart, clutLoad;
    u32 additive, magFilter, minFilter;
    float originY;
  };
  ChainState chainState;
  bool chainStateValid = false;  // the open chain has set chainState
  bool chainXyo2D = false;  // the open chain moved XYOFFSET to the 2D origin
  void openChain(bool restoreRepeat);
  void closeChain();
  static void closeOpenChain();
  static constexpr u32 kChainQw = 4096;
  qword_t* chains[2];
  u32 chainSeq[2] = {0, 0};  // Vif1Queue sequence of each side's last chain
  u32 lastSeq = 0;           // the newest chain submitted
  u32 chainQw = 0;           // qwords written into the open chain
  u8 chainSide = 0;
  bool chainOpen = false;

  u8 context;
  RendererSettings* settings;
  clutbuffer_t* clutBuffer;
  packet2_t* packets[2];
  texrect_t* rects[2];
};

}  // namespace Tyra

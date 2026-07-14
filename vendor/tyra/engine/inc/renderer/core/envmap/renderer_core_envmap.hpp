/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: dynamic environment map (GT3-style reflective materials).
*/

#pragma once

#include <packet2.h>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include "renderer/models/color.hpp"

namespace Tyra {

/**
 * Dynamic environment map: a small VRAM render target the game re-renders
 * every frame (typically the sky dome from the camera position) and then
 * samples as the sphere map of reflective materials - the GT3 trick.
 *
 * Usage per frame, right after beginFrame() and before the scene render:
 *   core.envMap.begin(horizonColor);   // raster -> the 128x128 target
 *   core.renderer3D.pushEnvView(...);  // wide-FOV square projection
 *   ... submit sky dome (AllPass z-test) via the static pipeline ...
 *   core.renderer3D.popEnvView(mainCamera);
 *   core.envMap.end();                 // drain + restore the framebuffer
 * Then bind texture() on any StaPip texture bag (it is VRAM-resident: no
 * PATH3 upload, never evicted).
 *
 * The VRAM (a 128x128x32 buffer, 64 KB) is allocated at init time BELOW the
 * texture region - the bump allocator's FIFO free can then never reclaim it
 * (same discipline as the post-fx scratch buffers).
 */
class RendererCoreEnvMap {
 public:
  static constexpr int size = 128;

  RendererCoreEnvMap();
  ~RendererCoreEnvMap();

  /** Called by RendererCore, after postFx.init and before texture use. */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1);

  /** The VRAM-resident texture to bind on reflective materials' env bags. */
  Texture* getTexture() { return texture; }

  /**
   * Drains in-flight PATH1 rendering, points FRAME/SCISSOR/XYOFFSET at the
   * env target (z writes masked - the pass shares the main z-buffer address
   * but must not scribble on it) and clears the target to the given color.
   */
  void begin(const Color& clearColor);

  /** Drains the env pass and restores the frame drawing environment. */
  void end();

 private:
  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  u32 vramAddress = 0;
  texbuffer_t texBuffer;
  Texture* texture = nullptr;
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
};

}  // namespace Tyra

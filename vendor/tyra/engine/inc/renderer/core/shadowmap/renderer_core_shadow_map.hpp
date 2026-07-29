/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: projected silhouette shadows (per-object render targets).
*/

#pragma once

#include <packet2.h>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

/**
 * Projected silhouette shadows: small VRAM render targets the game renders a
 * shadow caster's silhouette into (from a "light camera" looking along the
 * sun direction), then samples on a terrain patch under the caster - the
 * real-shape shadow trick of the PS2 era. Sibling of RendererCoreEnvMap;
 * same raster-redirect bracket, but per-caster slots and no color fidelity
 * (only the alpha coverage matters - the receiver draws black, modulated by
 * the silhouette's alpha).
 *
 * Usage per frame, at the END of the scene render (so receiver patches
 * z-test against the finished frame):
 *   for each caster c (up to `slots`):
 *     core.shadowMap.begin(slot);            // raster -> slot target, clear
 *     core.renderer3D.pushEnvView(light cam) // square light view
 *     ... resubmit the caster's existing bags via the static pipeline ...
 *     core.renderer3D.popEnvView(mainCamera);
 *   core.shadowMap.end();                    // restore the frame raster
 *   ... draw each caster's receiver patch bound to getTexture(slot) ...
 *
 * VRAM is allocated LAZILY (allocate(), called from the game's init only
 * when the project uses projected shadows): slots * 16 KB color + one
 * shared 16 KB z-buffer (cleared per begin - silhouette parts of one
 * caster may self-occlude freely, slots never overlap in time).
 */
class RendererCoreShadowMap {
 public:
  static constexpr int size = 64;
  static constexpr int slots = 4;

  RendererCoreShadowMap();
  ~RendererCoreShadowMap();

  /** Called by RendererCore (dependency wiring only - no VRAM). */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1);

  /**
   * Allocate the render targets. Call once from the game's init, BEFORE the
   * first frame uploads any texture (the bump allocator's FIFO free must
   * never be able to reclaim these). No-op when already allocated. Also
   * called by RendererCore on a display-mode switch when it was allocated
   * (the VRAM map is rebuilt from scratch there).
   */
  void allocate();
  bool isAllocated() const { return allocated; }

  /** The VRAM-resident silhouette texture of a slot (bind on the receiver
   * patch's texture bag; alpha = coverage, RGB = irrelevant). */
  Texture* getTexture(const int slot) { return textures[slot]; }

  /**
   * Drain in-flight PATH1 work and point the raster at a slot's target:
   * FRAME = slot color (cleared to alpha 0), ZBUF = the shared shadow
   * z-buffer (cleared - real z so multi-part casters rasterize sanely),
   * SCISSOR/XYOFFSET = the 64x64 window. Call per caster; end() once after
   * the last caster.
   */
  void begin(const int slot);

  /** Drain the last silhouette and restore the frame drawing environment. */
  void end();

 private:
  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  bool allocated = false;
  u32 vramAddress[slots] = {};
  u32 zVramAddress = 0;  // shared, cleared per begin
  texbuffer_t texBuffers[slots];
  Texture* textures[slots] = {};
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
};

}  // namespace Tyra

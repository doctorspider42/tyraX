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
 * The VRAM (a 128x128x32 target plus its own z buffer, 128 KB together) is
 * allocated at init time BELOW the texture region - the bump allocator's FIFO
 * free can then never reclaim it (same discipline as the post-fx scratch
 * buffers).
 *
 * Modified by TyraX: that allocation is now OPT-OUT (setEnabled). Both
 * instances RendererCore owns - the reflection env map and the camera feed -
 * used to be allocated unconditionally, so a project with no reflective
 * material and no texture feed still spent 256 KB of a ~1.08 MB texture
 * heap, a quarter of it, on two targets it never read. A disabled instance
 * reserves nothing and getTexture() returns nullptr; every caller has to
 * treat that as "this project has no env map" rather than assume a texture.
 */
class RendererCoreEnvMap {
 public:
  static constexpr int size = 128;

  RendererCoreEnvMap();
  ~RendererCoreEnvMap();

  /**
   * Reserve VRAM for this target, or not (TyraX fork). Call BEFORE init -
   * RendererCore does, from the renderer options the generated game passes.
   * A disabled target costs nothing: no VRAM, no packets, no texture.
   * The flag is remembered across the re-init a display-mode switch runs.
   */
  void setEnabled(const bool& on) { enabled = on; }

  /** True when this target owns VRAM and may be rendered into. */
  bool isAllocated() const { return allocated; }

  /** Called by RendererCore, after postFx.init and before texture use. */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1);

  /**
   * The VRAM-resident texture to bind on reflective materials' env bags -
   * or nullptr when this target is disabled (see setEnabled). Callers must
   * null-check: binding nothing is the correct "no reflections" behaviour.
   */
  Texture* getTexture() { return texture; }

  /**
   * Drains in-flight PATH1 rendering, points FRAME/SCISSOR/XYOFFSET/ZBUF at
   * the env target (it owns a dedicated 128x128 z-buffer, so "reflected"
   * scene objects submitted inside the bracket occlude each other properly)
   * and clears the target color + depth. A no-op when disabled.
   */
  void begin(const Color& clearColor);

  /** Drains the env pass and restores the frame drawing environment. */
  void end();

 private:
  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  bool enabled = true;    // does this project want the target at all
  bool allocated = false; // ...and has init() actually reserved it
  u32 vramAddress = 0;
  u32 zVramAddress = 0;  // dedicated env z-buffer (128x128, 32-bit)
  texbuffer_t texBuffer;
  Texture* texture = nullptr;
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
};

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: split-screen viewports (two-player games).
*/

#pragma once

#include <packet2.h>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "renderer/models/color.hpp"

namespace Tyra {

/**
 * Horizontal split-screen: render the 3D scene twice per frame, once into the
 * top half of the framebuffer and once into the bottom half.
 *
 * No projection change is involved: each half shows the CENTRAL height/2 rows
 * of the ordinary full-screen projection (a vertical crop, so per-pixel scale
 * and circles stay correct), placed into its half by shifting XYOFFSET and
 * clamping with SCISSOR. The z-buffer is shared and there is NO per-half
 * clear: beginFrame's full-screen clear covers both halves, and the scissor
 * clips every raster write (z included), so the halves cannot dirty each
 * other's region.
 *
 * Usage per frame (after beginFrame(cam1), which set the P1 view and cleared
 * the whole frame + z):
 *   core.splitView.begin(0);                // raster -> top half
 *   ... submit the scene ...
 *   core.renderer3D.update(cam2Info);       // swap view + frustum to P2
 *   core.splitView.begin(1);                // drains P1's half, raster -> bottom
 *   ... submit the scene again ...
 *   core.splitView.end();                   // drain + restore full-screen raster
 *   ... 2D / HUD / post fx as usual (full screen) ...
 *
 * Every begin()/end() drains in-flight PATH1 work first - the raster redirect
 * is global GS state (same discipline as RendererCoreEnvMap).
 */
class RendererCoreSplitView {
 public:
  RendererCoreSplitView();
  ~RendererCoreSplitView();

  /** Called by RendererCore. */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1);

  /**
   * Drain PATH1 and shift XYOFFSET/SCISSOR to the given half (0 = top,
   * 1 = bottom).
   */
  void begin(const int& half);

  /** Drain PATH1 and restore the full-screen raster window. */
  void end();

 private:
  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
};

}  // namespace Tyra

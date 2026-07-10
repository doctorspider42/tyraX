/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by tyra-editor: drained3DFor2D flag (PATH1 drain before 2D sprites)
*/

#pragma once

#include <tamtypes.h>
#include "./2d/renderer_core_2d.hpp"
#include "./3d/renderer_core_3d.hpp"
#include "./gs/renderer_core_gs.hpp"
#include "./texture/renderer_core_texture.hpp"
#include "./postfx/renderer_core_postfx.hpp"
#include "./paths/path3/path3.hpp"
#include "./paths/path1/path1.hpp"
#include "./renderer_core_sync.hpp"

namespace Tyra {

class RendererCore {
 public:
  RendererCore();
  ~RendererCore();

  /** Responsible for initializing GS. */
  RendererCoreGS gs;

  /** All logic responsible for 3D drawing. */
  RendererCore3D renderer3D;

  /** All logic responsible for 2D drawing. */
  RendererCore2D renderer2D;

  /** Texture transferring. */
  RendererCoreTexture texture;

  /** Full screen post effects: bloom, film grain (tyra-editor fork). */
  RendererCorePostFx postFx;

  /** EE <-> VU1 synchronization */
  RendererCoreSync sync;

  // Set once Renderer2D has drained PATH1 this frame (sprites race the tail
  // of the async 3D stream otherwise - see Renderer2D::render). Reset by
  // beginFrame.
  bool drained3DFor2D = false;

  /** Called by renderer */
  void init(VideoMode videoMode = VideoMode::Auto);

  /** World background color */
  void setClearScreenColor(const Color& color);

  /** Clear screen and update view frustum for frustum culling. NO 3D support */
  void beginFrame();

  /** Clear screen and update view frustum for frustum culling. 3D support */
  void beginFrame(const CameraInfo3D& cameraInfo);

  /** VSync and swap frame double buffer. */
  void endFrame();

  void setFrameLimit(const bool& onoff) { isFrameLimitOn = onoff; }

  /** Get screen settings */
  const RendererSettings& getSettings() const { return settings; }

  Path1* getPath1() { return &path1; }

  Path3* getPath3() { return &path3; }

 private:
  bool isFrameLimitOn;
  Color bgColor;
  RendererSettings settings;
  Path3 path3;
  Path1 path1;
};

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/core/renderer_core.hpp"
#include "thread/threading.hpp"

namespace Tyra {

RendererCore::RendererCore() { isFrameLimitOn = true; }
RendererCore::~RendererCore() {}

void RendererCore::init() {
  path3.init(&settings);
  sync.init(&path3, &path1);
  gs.init(&settings);
  // Post fx VRAM sits right above the frame/z buffers; allocate it before
  // any texture buffer so texture free (FIFO) never reclaims it.
  postFx.init(&settings, &gs);
  texture.init(&gs, &path3);
  renderer3D.init(&settings, &path1);
  renderer2D.init(&settings, &texture.clut);
}

void RendererCore::setClearScreenColor(const Color& color) { bgColor = color; }

void RendererCore::beginFrame() {
  renderer3D.update();
  Threading::switchThread();
  path3.clearScreen(&gs.zBuffer, bgColor);
}

void RendererCore::beginFrame(const CameraInfo3D& cameraInfo) {
  renderer3D.update(cameraInfo);
  Threading::switchThread();
  path3.clearScreen(&gs.zBuffer, bgColor);
}

void RendererCore::endFrame() {
  Threading::switchThread();
  // The dynamic pipeline kicks the scene on PATH1/VU1 asynchronously (double
  // buffered - sendPacket() returns while the DMA is still draining). PostFx
  // composites over the framebuffer via PATH3 and writes no z, so any scene
  // triangles the GS is still rasterizing would draw back over the grain/bloom
  // (they pass the GEQUAL z-test) and erase it - a few frames every so often,
  // exactly when VU1 lags. Drain PATH1 first so we composite over a finished
  // frame. Only pay the barrier when an effect is actually on, and only once
  // a 3D pipeline has brought VU1 up (VIF1 DMA init + double buffer): before
  // that - e.g. the pure-2D loading screen - there is nothing on PATH1 to
  // drain and the draw-finish handshake would spin forever waiting for a
  // FINISH that VU1 can't deliver yet.
  if (postFx.isEnabled() && path1.isVU1Configured()) sync.align3D();
  postFx.apply();
  if (isFrameLimitOn) graph_wait_vsync();
  gs.flipBuffers();
}

}  // namespace Tyra

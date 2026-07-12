/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <math.h>
#include "renderer/core/renderer_core.hpp"
#include "thread/threading.hpp"
#include "debug/debug.hpp"

namespace Tyra {

RendererCore::RendererCore() { isFrameLimitOn = true; }
RendererCore::~RendererCore() {}

void RendererCore::init(VideoMode videoMode, DisplayMode displayMode,
                        bool widescreen) {
  settings.setVideoMode(videoMode);
  // Must precede gs.init - it sizes the frame/z buffers (tyra-editor fork).
  settings.setDisplayMode(displayMode);
  settings.setWidescreen(widescreen);
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

// Modified by tyra-editor: runtime video output switch - see the header.
void RendererCore::setDisplayOutput(const DisplayMode& mode,
                                    const bool& widescreen) {
  const bool modeChanged = settings.getDisplayMode() != mode;
  const bool wsChanged = settings.getWidescreen() != widescreen;
  if (!modeChanged && !wsChanged) return;

  settings.setDisplayMode(mode);
  settings.setWidescreen(widescreen);

  if (modeChanged) {
    // The framebuffer size changes, so the whole VRAM layout does: drop
    // every texture allocation (they lazily re-upload), rebuild the
    // frame/z buffers + display, and re-place the post-fx scratch buffers
    // right above them (same order as init, so texture eviction can never
    // reclaim them).
    texture.evictAll();
    gs.reinit();
    postFx.init(&settings, &gs);
  } else {
    // Same buffers - only the display window shape changes (1080i widens;
    // the SDTV modes are stretched by the TV, their window stays as-is).
    gs.reprogramDisplay();
  }

  // Re-derive the projection (and thus next frame's frustum planes) from
  // the new framebuffer size / aspect.
  renderer3D.setFov(renderer3D.getFov());
}

// Modified by tyra-editor: GS hardware distance fog.
void RendererCore::setFog(const Color& color, const float& start,
                          const float& end) {
  TYRA_ASSERT(end > start, "Fog end distance must be greater than start!");
  fog.enabled = true;
  fog.color = color;
  fog.start = start;
  fog.end = end;
  const float range = end - start;
  fog.scale = -255.0F / range;
  fog.offset = 255.0F * end / range;
  gs.setFogColor(static_cast<u8>(color.r), static_cast<u8>(color.g),
                 static_cast<u8>(color.b));
}

void RendererCore::disableFog() {
  fog.enabled = false;
  fog.scale = 0.0F;
  fog.offset = 255.0F;
}

// Modified by tyra-editor: dynamic spot light (flashlight).
void RendererCore::setSpotLight(const Color& color, const Vec4& position,
                                const Vec4& direction, const float& range,
                                const float& cutoffDegrees,
                                const float& softness) {
  TYRA_ASSERT(range > 0.0F, "Spot light range must be positive!");
  spot.enabled = true;
  spot.color = color;
  spot.position = position;
  spot.direction = direction;
  const float len = sqrtf(direction.x * direction.x +
                          direction.y * direction.y +
                          direction.z * direction.z);
  if (len > 1e-5F) {
    spot.direction.x /= len;
    spot.direction.y /= len;
    spot.direction.z /= len;
  }
  spot.direction.w = 0.0F;
  spot.range = range;
  const float halfAngle = cutoffDegrees * 3.14159265F / 180.0F;
  spot.cosCutoff = cosf(halfAngle);
  spot.softness = softness < 1.0F ? 1.0F : softness;
}

void RendererCore::beginFrame() {
  renderer3D.update();
  drained3DFor2D = false;
  postFxAppliedMask = 0;
  postFxDrained = false;
  Threading::switchThread();
  path3.clearScreen(&gs.zBuffer, bgColor);
}

void RendererCore::beginFrame(const CameraInfo3D& cameraInfo) {
  renderer3D.update(cameraInfo);
  drained3DFor2D = false;
  postFxAppliedMask = 0;
  postFxDrained = false;
  Threading::switchThread();
  path3.clearScreen(&gs.zBuffer, bgColor);
}

// Modified by tyra-editor: mid-frame post fx (Tools > UI Editor screen stack).
// Applies only the not-yet-applied selected passes; the PATH1 drain barrier
// (endFrame's) runs once, before the first pass that actually draws - after
// that no more 3D is submitted this frame, so later passes composite safely
// over a finished frame (and over any HUD sprites drawn in between).
void RendererCore::applyPostFx(int passes) {
  passes &= ~postFxAppliedMask;
  if (passes == 0) return;
  postFxAppliedMask |= passes;
  if (!postFx.isEnabled(passes)) return;
  if (!postFxDrained && path1.isVU1Configured()) {
    sync.align3D();
    postFxDrained = true;
  }
  postFx.apply(passes);
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
  applyPostFx();
  if (isFrameLimitOn) graph_wait_vsync();
  gs.flipBuffers();
}

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: drained3DFor2D flag (PATH1 drain before 2D sprites),
# GS hardware distance fog state (setFog/disableFog), camera spot light
# (setSpotLight/disableSpotLight - the camera flashlight), applyPostFx()
# (mid-frame post fx, per-pass, so HUD sprites can draw crisp on top of
# bloom while film grain still sits over everything), applyCustomPostFx()
# (user-authored full-screen effects composited at a stack slot)
*/

#pragma once

#include <tamtypes.h>
#include "./2d/renderer_core_2d.hpp"
#include "./3d/renderer_core_3d.hpp"
#include "./gs/renderer_core_gs.hpp"
#include "./texture/renderer_core_texture.hpp"
#include "./postfx/renderer_core_postfx.hpp"
#include "./envmap/renderer_core_envmap.hpp"
#include "./splitview/renderer_core_splitview.hpp"
#include "./paths/path3/path3.hpp"
#include "./paths/path1/path1.hpp"
#include "./renderer_core_sync.hpp"

namespace Tyra {

/**
 * GS hardware distance fog state (TyraX fork). The 3D pipelines read
 * this every frame: `enabled` drives the PRIM FGE bit, scale/offset are
 * uploaded to VU1 which computes the per-vertex fog coefficient
 * F = clamp(w * scale + offset, 0, 255) from the view distance w
 * (F = 255 means no fog - the GS blends towards FOGCOL as F drops).
 */
struct RendererCoreFog {
  bool enabled = false;
  Color color = Color(128.0F, 128.0F, 128.0F, 128.0F);
  float start = 0.0F;
  float end = 0.0F;
  float scale = 0.0F;
  float offset = 255.0F;
};

/**
 * Dynamic spot light state (TyraX fork) - the "flashlight". Applied
 * per-vertex on VU1 in the StaPip color pipelines (the ones the editor's
 * generated games use): additive cone + distance falloff on top of the baked
 * vertex colors, no N.L term (there are no normals in the color paths -
 * the same trick early hardware-lit games used). The per-mesh object-space
 * transform happens on the EE (see StaPipQBufferRenderer::sendObjectData);
 * EE-clipped triangles get the light injected into their colors before
 * interpolation (see StaPipClipper).
 */
struct RendererCoreSpotLight {
  bool enabled = false;
  Vec4 position = Vec4(0.0F, 0.0F, 0.0F, 1.0F);   // world space
  Vec4 direction = Vec4(0.0F, 0.0F, -1.0F, 0.0F); // world space, normalized
  Color color = Color(96.0F, 96.0F, 80.0F, 128.0F); // additive, 128 = +1.0
  float range = 30.0F;      // world units
  float cosCutoff = 0.9F;   // cos of the cone half-angle
  float softness = 3.0F;    // >=1; higher = sharper cone edge
};

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

  /** Full screen post effects: bloom, film grain (TyraX fork). */
  RendererCorePostFx postFx;

  /** Dynamic environment map for reflective materials (TyraX fork). */
  RendererCoreEnvMap envMap;

  /** Split-screen viewports for two-player games (TyraX fork). */
  RendererCoreSplitView splitView;

  /** EE <-> VU1 synchronization */
  RendererCoreSync sync;

  /** GS hardware distance fog (TyraX fork). */
  RendererCoreFog fog;

  /** Dynamic spot light - the flashlight (TyraX fork). */
  RendererCoreSpotLight spot;

  // Set once Renderer2D has drained PATH1 this frame (sprites race the tail
  // of the async 3D stream otherwise - see Renderer2D::render). Reset by
  // beginFrame.
  bool drained3DFor2D = false;

  /** Called by renderer */
  void init(VideoMode videoMode = VideoMode::Auto,
            DisplayMode displayMode = DisplayMode::Interlaced,
            bool widescreen = false);

  /**
   * Runtime video output switch (TyraX fork): scan mode
   * (480i/480p/1080i) and/or 16:9 widescreen. Call between frames (i.e.
   * before beginFrame, after the previous endFrame). Changing the scan mode
   * re-allocates the frame/z/post-fx buffers (full VRAM reset) and evicts
   * every texture from VRAM - they re-upload on their next use, so expect
   * one heavier frame. A widescreen-only change just reprograms the display
   * window and the projection.
   */
  void setDisplayOutput(const DisplayMode& mode, const bool& widescreen);

  /** World background color */
  void setClearScreenColor(const Color& color);

  /**
   * Enable GS hardware distance fog (TyraX fork). Geometry fades to
   * `color` between view distances `start` and `end`. For an atmospheric
   * fade-out, match `color` with the clear screen color and keep
   * `end` at (or before) the far plane.
   */
  void setFog(const Color& color, const float& start, const float& end);

  /** Disable GS hardware distance fog. */
  void disableFog();

  /**
   * Enable the dynamic spot light (TyraX fork). Position/direction are
   * world space (direction gets normalized); cutoffDegrees is the cone
   * half-angle; color is additive on top of the baked vertex colors with
   * 128 = +1.0. Update position/direction every frame to attach it to the
   * camera (flashlight).
   */
  void setSpotLight(const Color& color, const Vec4& position,
                    const Vec4& direction, const float& range,
                    const float& cutoffDegrees, const float& softness = 3.0F);

  /** Disable the dynamic spot light. */
  void disableSpotLight() { spot.enabled = false; }

  /** Clear screen and update view frustum for frustum culling. NO 3D support */
  void beginFrame();

  /** Clear screen and update view frustum for frustum culling. 3D support */
  void beginFrame(const CameraInfo3D& cameraInfo);

  /**
   * Apply a subset of the full-screen post effects NOW instead of at endFrame
   * (TyraX fork). `passes` is a RendererCorePostFx::Pass bitmask - the
   * UI Editor screen stack applies bloom(+grading) and grain at independent
   * points, e.g. bloom under the HUD, grain over everything. Call it
   * mid-frame - after the scene, before the HUD sprites you want on top.
   * Each pass runs at most once per frame; endFrame() composites whatever is
   * still unapplied. The PATH1 drain barrier (endFrame's) runs once, on the
   * first pass that actually draws.
   */
  void applyPostFx(int passes = RendererCorePostFx::PassAll);

  /**
   * Run one user-authored full-screen post effect NOW (TyraX fork,
   * "custom screen effects"). Like applyPostFx() it runs the PATH1 drain
   * barrier once so the pass composites over finished 3D, then hands off to
   * RendererCorePostFx::applyCustom(): the `build` callback appends raw GS
   * blits and returns the advanced packet cursor. Called mid-frame at the
   * effect's slot in the UI Editor screen stack, so HUD sprites drawn after it
   * stay crisp on top. Unlike the built-in passes there is no once-per-frame
   * mask - a custom pass always draws when reached.
   */
  void applyCustomPostFx(RendererCorePostFx::CustomFxBuild build, void* user);

  /**
   * TyraX portals: bracket the in-place through-view render (see
   * RendererCorePostFx::portalMaskBegin/End). Both drain PATH1
   * unconditionally - this runs MID-frame (more 3D follows), so the
   * once-per-frame post-fx drain latch must not be set.
   */
  void portalViewBegin(int x0, int y0, int x1, int y1);
  void portalViewEnd(const float* xy, const u32* z, int count, u8 clearR,
                     u8 clearG, u8 clearB);

  /** VSync and swap frame double buffer. */
  void endFrame();

  void setFrameLimit(const bool& onoff) { isFrameLimitOn = onoff; }

  /** Get screen settings */
  const RendererSettings& getSettings() const { return settings; }

  Path1* getPath1() { return &path1; }

  Path3* getPath3() { return &path3; }

 private:
  bool isFrameLimitOn;
  // Which post fx passes already ran this frame (RendererCorePostFx::Pass
  // bits) - endFrame composites the rest. postFxDrained: the PATH1 barrier
  // has run once this frame (only needed before the first pass that draws).
  // Both reset by beginFrame.
  int postFxAppliedMask = 0;
  bool postFxDrained = false;
  Color bgColor;
  RendererSettings settings;
  Path3 path3;
  Path1 path1;
};

}  // namespace Tyra

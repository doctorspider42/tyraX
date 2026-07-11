/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by tyra-editor: drained3DFor2D flag (PATH1 drain before 2D sprites),
# GS hardware distance fog state (setFog/disableFog), camera spot light
# (setSpotLight/disableSpotLight - the camera flashlight)
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

/**
 * GS hardware distance fog state (tyra-editor fork). The 3D pipelines read
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
 * Dynamic spot light state (tyra-editor fork) - the "flashlight". Applied
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

  /** Full screen post effects: bloom, film grain (tyra-editor fork). */
  RendererCorePostFx postFx;

  /** EE <-> VU1 synchronization */
  RendererCoreSync sync;

  /** GS hardware distance fog (tyra-editor fork). */
  RendererCoreFog fog;

  /** Dynamic spot light - the flashlight (tyra-editor fork). */
  RendererCoreSpotLight spot;

  // Set once Renderer2D has drained PATH1 this frame (sprites race the tail
  // of the async 3D stream otherwise - see Renderer2D::render). Reset by
  // beginFrame.
  bool drained3DFor2D = false;

  /** Called by renderer */
  void init(VideoMode videoMode = VideoMode::Auto);

  /** World background color */
  void setClearScreenColor(const Color& color);

  /**
   * Enable GS hardware distance fog (tyra-editor fork). Geometry fades to
   * `color` between view distances `start` and `end`. For an atmospheric
   * fade-out, match `color` with the clear screen color and keep
   * `end` at (or before) the far plane.
   */
  void setFog(const Color& color, const float& start, const float& end);

  /** Disable GS hardware distance fog. */
  void disableFog();

  /**
   * Enable the dynamic spot light (tyra-editor fork). Position/direction are
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

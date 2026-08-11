/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include <algorithm>
#include "renderer/renderer_settings.hpp"
#include "./renderer_3d_frustum_planes.hpp"
#include "./camera_info_3d.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include "renderer/core/3d/bbox/core_bbox.hpp"
#include "renderer/core/paths/path1/path1.hpp"

namespace Tyra {

class RendererCore3D {
 public:
  RendererCore3D();
  ~RendererCore3D();

  /** Current camera frustum planes. */
  Renderer3DFrustumPlanes frustumPlanes;

  /** Called by renderer. */
  void init(RendererSettings* settings, Path1* t_path1);

  const float& getFov() const { return fov; }

  void setFov(const float& t_fov);

  /**
   * Called by beginFrame();
   * Sets 3D support to off
   */
  void update();

  /**
   * Called by beginFrame();
   * Updates camera info, to get proper frustum culling.
   * Sets 3D support to on
   */
  void update(const CameraInfo3D& cameraInfo);

  /** Get projection (screen) matrix */
  const M4x4& getProjection() { return projection; }

  /**
   * Get view (camera) matrix
   * Updated at every beginFrame()
   */
  const M4x4& getView();

  /**
   * Get projection * view matrix
   * Updated at every beginFrame()
   */
  const M4x4& getViewProj();

  /**
   * @brief
   * Upload VU1 program.
   * Please pay attention that you will be REPLACING
   * current VU1 programs
   *
   * @param address Starting address of your program.
   * @return Address of the end of your program, so next program can start from
   * this + 1
   */
  u32 uploadVU1Program(VU1Program* program, const u32& address);

  /**
   * @brief Set VU1 double buffer
   *
   * @param startingAddress Starting address. Example 10.
   * @param bufferSize Buffer size. Example 490, so second buffer will start
   * from 490+10
   */
  void setVU1DoubleBuffers(const u16& startingAddress, const u16& bufferSize);

  /**
   * Modified by TyraX: dynamic env map pass - swap in a square wide-FOV
   * view/projection (and matching frustum planes, widened for the aspect
   * mismatch) for the bracketed submissions. Restore with popEnvView(main
   * camera info) before rendering the scene.
   */
  void pushEnvView(const Vec4& position, const Vec4& lookAt,
                   const float& envFov, const float& size);

  /**
   * Modified by TyraX: portal through-view - swap ONLY the view matrix (the
   * projection stays the main screen one; the destination renders in-place
   * into the real framebuffer) and retarget the frustum planes at the
   * virtual camera. Restore with popEnvView(main camera info).
   */
  void pushPortalView(const Vec4& position, const Vec4& lookAt);

  void popEnvView(const CameraInfo3D& cameraInfo);

  /**
   * Modified by TyraX: true between pushEnvView/pushPortalView and
   * popEnvView, i.e. while the submitted geometry is being seen through a
   * camera that is NOT the frame's. RendererCoreBlss reads it to keep those
   * submissions out of its per-tile feature grid - the grid is indexed by
   * SCREEN tiles, and a reflection probe's bags projected with the probe's own
   * view land in tiles they have nothing to do with.
   */
  bool isForeignViewActive() const { return foreignView; }

 private:
  M4x4 savedView, savedProjection, savedViewProj;  // pushEnvView state
  bool foreignView = false;  // Modified by TyraX (see isForeignViewActive)
  M4x4 view, projection, viewProj;
  float fov;
  bool is3DSupportEnabled;

  RendererSettings* settings;

  Path1* path1;
  void setProjection();
};

}  // namespace Tyra

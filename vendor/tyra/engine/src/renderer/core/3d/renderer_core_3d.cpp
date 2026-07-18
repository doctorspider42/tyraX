/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/core/3d/renderer_core_3d.hpp"

namespace Tyra {

RendererCore3D::RendererCore3D() {
  fov = 60.0F;
  is3DSupportEnabled = false;
}
RendererCore3D::~RendererCore3D() {}

void RendererCore3D::update() { is3DSupportEnabled = false; }

void RendererCore3D::update(const CameraInfo3D& cameraInfo) {
  frustumPlanes.update(cameraInfo, fov);
  M4x4::lookAt(&view, *cameraInfo.position, *cameraInfo.looksAt);
  viewProj = projection * view;
  is3DSupportEnabled = true;
}

void RendererCore3D::init(RendererSettings* t_settings, Path1* t_path1) {
  settings = t_settings;
  path1 = t_path1;
  frustumPlanes.init(settings, fov);
  setProjection();
  TYRA_LOG("RendererCore3D initialized!");
}

const M4x4& RendererCore3D::getView() {
  TYRA_ASSERT(is3DSupportEnabled,
              "You can't compute 3D without camera information. Please correct "
              "your beginFrame()");
  return view;
}

const M4x4& RendererCore3D::getViewProj() {
  TYRA_ASSERT(is3DSupportEnabled,
              "You can't compute 3D without camera information. Please correct "
              "your beginFrame()");
  return viewProj;
}

void RendererCore3D::setFov(const float& t_fov) {
  fov = t_fov;
  setProjection();
}

void RendererCore3D::setProjection() {
  projection = M4x4::perspective(
      fov, settings->getWidth(), settings->getHeight(),
      settings->getProjectionScale(), settings->getAspectRatio(),
      settings->getNear(), settings->getFar());
}

// Modified by TyraX: dynamic env map pass camera. The frustum planes are
// recomputed with a widened FOV - Renderer3DFrustumPlanes derives its aspect
// from the SCREEN settings while the env target is square, so exact planes
// would misclassify; the margin only costs some extra clipping work, never
// wrongly culls.
void RendererCore3D::pushEnvView(const Vec4& position, const Vec4& lookAt,
                                 const float& envFov, const float& size) {
  savedView = view;
  savedProjection = projection;
  savedViewProj = viewProj;
  projection = M4x4::perspective(envFov, size, size,
                                 settings->getProjectionScale(), 1.0F,
                                 settings->getNear(), settings->getFar());
  Vec4 pos = position;
  Vec4 look = lookAt;
  M4x4::lookAt(&view, pos, look);
  viewProj = projection * view;
  const float planesFov = envFov * 1.4F > 170.0F ? 170.0F : envFov * 1.4F;
  frustumPlanes.update(CameraInfo3D(&pos, &look), planesFov);
}

// Modified by TyraX: portal through-view camera. The projection is left
// untouched (the destination renders in-place into the real framebuffer,
// so the frustum SHAPE is the screen's own); only the view swaps and the
// frustum planes follow the virtual camera - exact planes, no widening.
void RendererCore3D::pushPortalView(const Vec4& position, const Vec4& lookAt) {
  savedView = view;
  savedProjection = projection;
  savedViewProj = viewProj;
  Vec4 pos = position;
  Vec4 look = lookAt;
  M4x4::lookAt(&view, pos, look);
  viewProj = projection * view;
  frustumPlanes.update(CameraInfo3D(&pos, &look), fov);
}

void RendererCore3D::popEnvView(const CameraInfo3D& cameraInfo) {
  view = savedView;
  projection = savedProjection;
  viewProj = savedViewProj;
  frustumPlanes.update(cameraInfo, fov);
}

u32 RendererCore3D::uploadVU1Program(VU1Program* program, const u32& address) {
  return path1->uploadProgram(program, address);
}

void RendererCore3D::setVU1DoubleBuffers(const u16& startingAddress,
                                         const u16& bufferSize) {
  path1->setDoubleBuffer(startingAddress, bufferSize);
}

}  // namespace Tyra

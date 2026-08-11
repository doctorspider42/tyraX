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

#include <tamtypes.h>
#include "./bag/stapip_bag.hpp"
#include "./bag/packaging/stapip_bag_packages_bbox.hpp"
#include "./bag/packaging/stapip_bag_package.hpp"
#include "./bag/packaging/stapip_bag_packager.hpp"
#include "./stapip_qbuffer_renderer.hpp"
#include "./stapip_telemetry.hpp"
#include "./stapip_bag_bboxes_cacher.hpp"
#include "renderer/3d/pipeline/shared/pipeline_frustum_culling.hpp"

namespace Tyra {

class StaPipCore {
 public:
  StaPipCore();
  ~StaPipCore();

  void init(RendererCore* t_core);

  void onFrameEnd();

  /** Render 3D via "bags" */
  void render(StaPipBag* bag);

  /** Get max vert count of VU1 qbuffer (for optimizations) */
  u32 getMaxVertCountByParams(const bool& isSingleColor,
                              const bool& isLightingEnabled,
                              const bool& isTextureEnabled);

  /** Get max vert count of VU1 qbuffer (for optimizations) */
  u32 getMaxVertCountByBag(const StaPipBag* bag);

  /**
   * - Uploads standard VU1 programs.
   * - Sends static "Tyra Renderer3D" VU1 data.
   * - Sets double buffers exactly for "Tyra Renderer3D"
   * Should be called if VU1 was used by your non standard programs.
   */
  void reinitVU1Programs();

  /**
   * Modified by TyraX: clip frustum-crossing packages on VU1 (clip
   * program family) instead of the EE clipper. Call right after
   * setRenderer() or between frames.
   */
  void setVU1Clipping(const bool& enabled);

  /** TyraX addition: which material classes keep a resident VU1 program
   * (docs/vu-framework.md). Safe at run time: a level that stops needing, say,
   * the matcap class can hand that micro memory to something else. It costs a
   * pipeline drain and an upload, so call it at a zone or level boundary. */
  void setResidentClasses(const u32& mask) {
    qbufferRenderer.setResidentClasses(mask);
  }
  u32 getResidentClasses() const { return qbufferRenderer.getResidentClasses(); }

  /** TyraX addition: install a game-supplied VU1 microprogram over a built-in
   * slot (docs/vu-framework.md). Reachable from game code as
   * engine.renderer.renderer3D.staticPipeline.core.setProgramOverride(...). */
  void setProgramOverride(const StaPipProgramName& name,
                          StaPipVU1Program* program) {
    qbufferRenderer.setProgramOverride(name, program);
  }
  /** TyraX addition: swap a whole set at once - one pipeline drain and one
   * upload instead of one per class. A null entry restores the built-in. */
  void setProgramOverrides(const StaPipProgramName* names,
                           StaPipVU1Program* const* programs, u32 count) {
    qbufferRenderer.setProgramOverrides(names, programs, count);
  }

  /** TyraX addition: the per-mesh numbers and the clock a project's own
   * microprogram reads (docs/vu-authoring.md). setVuParams is set before each
   * mesh is submitted - it is renderer state, exactly like the fog or the
   * flashlight, not a field on the bag - and only reaches VU1 for a bag with
   * no lighting, because the two quadwords live in the lights-colour block. */
  void setVuCustomEnabled(const bool& enabled) {
    qbufferRenderer.setVuCustomEnabled(enabled);
  }
  void setVuParams(const float& x, const float& y, const float& z,
                   const float& w) {
    qbufferRenderer.setVuParams(x, y, z, w);
  }
  void setVuTime(const float& seconds) { qbufferRenderer.setVuTime(seconds); }

  /**
   * TyraX diagnostics: opt in to package routing, active-plane and VIF1/VU1
   * wait counters. Disabled by default, so release games keep the original
   * AABB early-out and perform no COP0 timing reads. `takeTelemetry` returns
   * the accumulated interval and resets it.
   */
  void setTelemetryEnabled(const bool& enabled);
  bool isTelemetryEnabled() const { return telemetryEnabled; }
  StaPipTelemetry takeTelemetry();

  void allocateOnUse() { qbufferRenderer.allocateOnUse(); }
  void deallocateOnUse() { qbufferRenderer.deallocateOnUse(); }

 private:
  void setPrim();
  void setLod();

  prim_t prim;
  lod_t lod;

  u32 maxVertCount;
  RendererCore* rendererCore;
  StapipBagBBoxesCacher cacher;

  void setMaxVertCount(const u32& count);
  // Modified by TyraX: VU1 clipping.
  u32 clipDivisor() const;
  u32 clipPackageSize() const;
  // Modified by TyraX: frustum planes in the current bag's object space -
  // computed once per render(), shared by the main-bbox check and every
  // package classification in the packager.
  Plane objectSpacePlanes[6];
  Plane clipObjectSpacePlanes[6];
  void computeClipObjectSpacePlanes(const M4x4& mvp);
  StaPipBagPackager packager;
  StaPipQBufferRenderer qbufferRenderer;
  bool telemetryEnabled = false;
  StaPipTelemetry telemetry;
  void recordPackage(const StaPipBagPackage& package,
                     const CoreBBoxFrustum& route);
  void recordOutsideBag(const StaPipBag* bag);
  void renderPkgs(StaPipBagPackage* packages, const bool& doClip, u16 count);
  void renderSubpkgs(StaPipBagPackage* packages, u16 count);
};

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <gs_gp.h>
#include "renderer/3d/pipeline/static/core/stapip_core.hpp"
#include "renderer/core/renderer_core.hpp"
#include "thread/threading.hpp"

// #define TYRA_RENDERER_VERBOSE_LOG 1

#ifdef TYRA_RENDERER_VERBOSE_LOG
#define Verbose(...) TyraDebug::writeLines("VRB: ", ##__VA_ARGS__, "\n")
#else
#define Verbose(...) ((void)0)
#endif

namespace Tyra {

StaPipCore::StaPipCore() {
  maxVertCount = 0;
  setPrim();
  setLod();
}

StaPipCore::~StaPipCore() {}

void StaPipCore::init(RendererCore* t_core) {
  rendererCore = t_core;
  qbufferRenderer.init(t_core, &prim, &lod);
  packager.init(&rendererCore->renderer3D.frustumPlanes);
}

void StaPipCore::setPrim() {
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = DRAW_ENABLE;
  prim.fogging = DRAW_DISABLE;
  prim.blending = DRAW_ENABLE;
  prim.antialiasing = DRAW_DISABLE;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;
}

void StaPipCore::setLod() {
  lod.calculation = LOD_USE_K;
  lod.max_level = 0;
  lod.mag_filter = LOD_MAG_LINEAR;
  lod.min_filter = LOD_MIN_LINEAR;
  lod.mipmap_select = LOD_MIPMAP_REGISTER;
  lod.l = 0;
  lod.k = 0.0F;
}

void StaPipCore::onFrameEnd() { cacher.onFrameEnd(); }

void StaPipCore::reinitVU1Programs() { qbufferRenderer.reinitVU1(); }

u32 StaPipCore::getMaxVertCountByBag(const StaPipBag* bag) {
  return qbufferRenderer.getCullProgramByBag(bag)->getMaxVertCount(
      bag->color->many == nullptr, qbufferRenderer.getBufferSize());
}

u32 StaPipCore::getMaxVertCountByParams(const bool& isSingleColor,
                                        const bool& isLightingEnabled,
                                        const bool& isTextureEnabled) {
  return qbufferRenderer
      .getCullProgramByParams(isLightingEnabled, isTextureEnabled)
      ->getMaxVertCount(isSingleColor, qbufferRenderer.getBufferSize());
}

void StaPipCore::render(StaPipBag* bag) {
  if (bag->count <= 0) return;

  // Modified by TyraX: GS hardware fog - the PRIM FGE bit follows the
  // renderer-level fog state (see RendererCore::setFog), with a per-bag
  // opt-out (sky dome).
  prim.fogging = rendererCore->fog.enabled && !bag->info->fogDisabled
                     ? DRAW_ENABLE
                     : DRAW_DISABLE;

  bool frustumCull =
      bag->info->frustumCulling == PipelineInfoBagFrustumCulling_Precise;

  TYRA_ASSERT(bag->vertices != nullptr,
              "Vertices are required in 3D render bag!");
  TYRA_ASSERT(bag->info != nullptr, "Info bag is required in 3D render bag!");
  TYRA_ASSERT(bag->info->model != nullptr,
              "Info bag's model pointer is empty!");
  TYRA_ASSERT(bag->color != nullptr, "Color bag is required in 3D render bag!");
  TYRA_ASSERT(bag->color->single || bag->color->many,
              "At least one color is required in 3D render bag!");
  TYRA_ASSERT((!bag->color->many && !bag->lighting) ||
                  (bag->color->many && !bag->lighting) ||
                  (!bag->color->many && bag->lighting),
              "Multicolor is not supported with lighting, please choose one!");
  TYRA_ASSERT(
      !bag->lighting || (bag->lighting->lightMatrix && bag->lighting->normals &&
                         bag->lighting->dirLights),
      "If you want lighting, please provide light matrix normals and dir "
      "lights!");
  TYRA_ASSERT(
      !bag->texture || (bag->texture->texture && bag->texture->coordinates),
      "If you want texture, please provide texture and coordinates!");
  TYRA_ASSERT(bag->info->transformationType == TyraMVP ||
                  (!bag->info->fullClipChecks && !frustumCull),
              "Please disable clip checks and frustum culling if not using MVP "
              "matrix!");
  TYRA_ASSERT(!(!frustumCull && bag->info->fullClipChecks == true),
              "Full clip checks are not supported with frustum culling = off!");

  u32 maxVertCount = getMaxVertCountByBag(bag);

  StaPipBagPackagesBBox* bbox = nullptr;
  if (bag->info->frustumCulling == PipelineInfoBagFrustumCulling_Precise) {
    // TyraX: the bag's bboxVersion invalidates the cached boxes for
    // reused vertex buffers with new content (the cacher recomputes the
    // entry in place - per-frame bumps stay allocation-free)
    bbox = cacher.getBBoxes(bag->vertices, bag->count,
                            reinterpret_cast<u32>(bag->vertices),
                            bag->bboxVersion, maxVertCount);
  }

  setMaxVertCount(maxVertCount);

  CoreBBoxFrustum frustumCheck = OUTSIDE_FRUSTUM;

  if (frustumCull) {
    frustumCheck = bbox->getMainBBox()->clipFrustumCheck(
        rendererCore->renderer3D.frustumPlanes.getAll(), *bag->info->model);

    if (frustumCheck == OUTSIDE_FRUSTUM) {
      return;
    }
  }

  // Modified by TyraX: per-bag additive blend equation (the spherical
  // environment-map pass of reflective materials). The GS ALPHA register is
  // global state, so switching it must drain the in-flight PATH1 rendering
  // first (FINISH handshake); restored after this bag's own drain below.
  // Placed after the frustum early-out so a culled bag never flips the state.
  const u8 additiveFix = bag->info->additiveBlendFix;
  if (additiveFix != 0) {
    rendererCore->sync.align3D();
    // Cv = Cs * FIX/128 + Cd
    rendererCore->gs.setAlpha(GS_SET_ALPHA(0, 2, 2, 1, additiveFix));
  }

  packager.setRenderBBox(bbox);

  M4x4 mvp;

  if (bag->info->transformationType == TyraMP) {
    mvp = rendererCore->renderer3D.getProjection() * *bag->info->model;
  } else {
    mvp = rendererCore->renderer3D.getViewProj() * *bag->info->model;
  }

  // Modified by TyraX: stack storage - sendObjectData consumes the
  // struct immediately, the old per-bag new/delete was pure heap churn.
  RendererCoreTextureBuffers texBuffersStorage;
  RendererCoreTextureBuffers* texBuffers = nullptr;
  if (bag->texture) {
    auto temp = rendererCore->texture.useTexture(bag->texture->texture);
    texBuffersStorage = {temp.id, temp.core, temp.clut};
    texBuffers = &texBuffersStorage;
  }

  qbufferRenderer.clearLastProgramName();

  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);

  qbufferRenderer.setClipperMVP(&mvp);

  qbufferRenderer.setInfo(bag->info);

  auto checkYesFrustumInClipYes =  // cull all
      frustumCull && frustumCheck == IN_FRUSTUM && bag->info->fullClipChecks;

  auto checkYesFrustumPartialClipYes =  // pkgs, cull + clip
      frustumCull && frustumCheck == PARTIALLY_IN_FRUSTUM &&
      bag->info->fullClipChecks;

  auto checkYesFrustumInClipNo =  // cull all
      frustumCull && frustumCheck == IN_FRUSTUM && !bag->info->fullClipChecks;

  auto checkYesFrustumPartialClipNo =  // pkgs, cull all
      frustumCull && frustumCheck == PARTIALLY_IN_FRUSTUM &&
      !bag->info->fullClipChecks;

  auto checkNoClipNo =  // cull all
      !frustumCull && !bag->info->fullClipChecks;

  // Modified by TyraX: packager.create returns pooled arrays - no
  // delete[] here (see StaPipBagPackager).
  if (checkYesFrustumInClipYes || checkYesFrustumInClipNo || checkNoClipNo) {
    u16 packagesCount = 0;
    auto* biggerPkgs = packager.create(&packagesCount, bag, maxVertCount);
    Verbose("Material - in frustum. Pkgs: ", packagesCount,
            " size: ", static_cast<int>(biggerPkgs[0].size));
    for (u16 i = 0; i < packagesCount; i++) {
      Verbose(i, " package - cull by data pointer");
      auto buffer = qbufferRenderer.getBuffer();
      buffer->fillByPointer(biggerPkgs[i]);
      qbufferRenderer.cull(buffer);
    }
  } else if (checkYesFrustumPartialClipYes || checkYesFrustumPartialClipNo) {
    u16 packagesCount = 0;
    auto doClip = checkYesFrustumPartialClipYes;
    if (!doClip || bag->count >= maxVertCount * 2) {
      auto packages = packager.create(&packagesCount, bag, maxVertCount);
      Verbose("Material - partial. Packages: ", packagesCount);
      renderPkgs(packages, doClip, packagesCount);
    } else {
      auto subpkgs = packager.create(&packagesCount, bag, clipPackageSize());
      Verbose("Material - partial. Subpackages: ", packagesCount);
      renderSubpkgs(subpkgs, packagesCount);
    }
  }

  qbufferRenderer.flushBuffers();

  if (additiveFix != 0) {
    // Drain this bag's triangles, then restore the standard alpha-over
    // equation ((Cs-Cd)*As/128 + Cd, the draw_setup_environment default).
    rendererCore->sync.align3D();
    rendererCore->gs.setAlpha(GS_SET_ALPHA(0, 1, 0, 1, 0));
  }

  Verbose("Render finished");
}

void StaPipCore::renderPkgs(StaPipBagPackage* packages, const bool& doClip,
                            u16 count) {
  for (u16 i = 0; i < count; i++) {
    auto cull = (doClip && packages[i].isInFrustum == IN_FRUSTUM) || !doClip;
    auto doSubpkgs = doClip && packages[i].isInFrustum == PARTIALLY_IN_FRUSTUM;

    if (cull) {
      Verbose(i, " - package in frustum -> cull");
      auto buffer = qbufferRenderer.getBuffer();
      buffer->fillByPointer(packages[i]);
      qbufferRenderer.cull(buffer);
    } else if (doSubpkgs) {
      u16 subpkgsSize = 0;
      auto packages1By3 =
          packager.create(&subpkgsSize, packages[i], clipPackageSize());
      Verbose(i, " - partial package. Created subpkgs: ", subpkgsSize);

      renderSubpkgs(packages1By3, subpkgsSize);
    }
    Verbose(i, " - package skipped (outside)");
  }
}

void StaPipCore::renderSubpkgs(StaPipBagPackage* subpkgs, u16 count) {
  // Modified by TyraX: reused across calls (the renderer is
  // single-threaded) - the two per-call heap allocations were measurable
  // next to the pooled packager.
  static std::vector<u16> doneIndexes;
  static std::vector<u16> loadedIndexes;
  doneIndexes.clear();
  loadedIndexes.clear();

  // Check if some subpkgs are full in frustum
  for (u16 i = 0; i < count; i++) {
    if (subpkgs[i].isInFrustum == IN_FRUSTUM) {
      if (loadedIndexes.size() <= 1) {
        Verbose(i, " - subpackage in frustum -> load");
        loadedIndexes.push_back(i);
      } else {  // Hmm, this will never happen?
        Verbose(i, " - subpackage in frustum, cull all 3 subpkgs");
        auto buffer = qbufferRenderer.getBuffer();
        buffer->fillByCopyMax(subpkgs[loadedIndexes[0]],
                              subpkgs[loadedIndexes[1]], subpkgs[i]);
        qbufferRenderer.cull(buffer);
        doneIndexes.push_back(loadedIndexes[0]);
        doneIndexes.push_back(loadedIndexes[1]);
        doneIndexes.push_back(i);
        loadedIndexes.clear();
      }
    }
  }

  if (loadedIndexes.size() == 2) {
    Verbose("2 in frustum subpkgs left -> cull them");
    auto buffer = qbufferRenderer.getBuffer();
    buffer->fillByCopy1By2(subpkgs[loadedIndexes[0]],
                           subpkgs[loadedIndexes[1]]);
    qbufferRenderer.cull(buffer);
    doneIndexes.push_back(loadedIndexes[0]);
    doneIndexes.push_back(loadedIndexes[1]);
  } else if (loadedIndexes.size() == 1) {
    Verbose("1 in frustum subpkg left -> cull it");
    auto buffer = qbufferRenderer.getBuffer();
    buffer->fillByPointer(subpkgs[loadedIndexes[0]]);
    qbufferRenderer.cull(buffer);
    doneIndexes.push_back(loadedIndexes[0]);
  }

  for (u16 i = 0; i < count; i++) {
    bool isSkip = subpkgs[i].isInFrustum == OUTSIDE_FRUSTUM ||
                  std::find(doneIndexes.begin(), doneIndexes.end(), i) !=
                      doneIndexes.end();

    if (isSkip) {
      Verbose(i, " - subpkg skipped, already rendered/outside");
      continue;
    }

    auto buffer = qbufferRenderer.getBuffer();
    buffer->fillByCopy1By3(subpkgs[i]);
    Verbose(i, " - subpkg out/partial -> send to clipper");
    qbufferRenderer.clip(buffer);
  }
}

void StaPipCore::setMaxVertCount(const u32& count) {
  maxVertCount = count;
  packager.setMaxVertCount(count);
  qbufferRenderer.setMaxVertCount(count);
}

// Modified by TyraX: occupancy cap for clip-classified packages.
// EE clipper: 1/3 of a VU1 buffer (its fan-out is drained in chunks on the
// EE). VU1 clipping: 1/5, so the worst-case Sutherland-Hodgman fan-out
// (7 output triangles per input triangle across 6 planes) still fits in the
// output area of one VU1 double-buffer half for every program variant.
u32 StaPipCore::clipDivisor() const {
  return qbufferRenderer.isVU1ClippingEnabled() ? 5 : 3;
}

// The size must stay a multiple of 3 - a package boundary through the middle
// of a triangle corrupts the geometry, and the VU1 clip programs loop by
// whole triangles (maxVertCount/5 is NOT always a multiple of 3: 72/5 = 14
// sent the tc program into an infinite loop over VU1 memory).
u32 StaPipCore::clipPackageSize() const {
  const u32 size = maxVertCount / clipDivisor();
  return (size / 3) * 3;
}

}  // namespace Tyra

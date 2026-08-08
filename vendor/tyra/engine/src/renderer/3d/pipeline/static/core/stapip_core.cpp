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
#include <math.h>
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
  const u32 derived = qbufferRenderer.getCullProgramByBag(bag)->getMaxVertCount(
      bag->color->many == nullptr, qbufferRenderer.getBufferSize());

  // Modified by TyraX: an explicit package size pins coplanar passes over one
  // vertex array to the same package boundaries, so they classify against the
  // frustum identically and take the same route (VU1 divide vs EE clipper) -
  // see StaPipBag::packageSize. Never above the class's own capacity (that
  // overflows the VU1 buffer) and always a multiple of 9, the invariant
  // getMaxVertCount itself keeps: divisible by 3 for whole triangles, and the
  // /3 subpackage split divisible by 3 again.
  if (bag->packageSize == 0 || bag->packageSize >= derived) return derived;
  const u32 pinned = (bag->packageSize / 9) * 9;
  return pinned < 9 ? derived : pinned;
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
  // Modified by TyraX: a billboard bag carries a texture bag purely for the
  // per-particle params channel - the image itself is optional there.
  TYRA_ASSERT(!bag->texture || ((bag->texture->texture || bag->billboard) &&
                                bag->texture->coordinates),
              "If you want texture, please provide texture and coordinates!");
  // Modified by TyraX: particle billboards (centers expanded on VU1).
  // frustumCulling None is SAFE here (unlike ordinary bags - see the
  // "never submit with None" pitfall): the billboard programs cull every
  // quad whose corner leaves the GS raster window / depth range, so
  // off-screen centers never wrap the 4096-px window.
  TYRA_ASSERT(!bag->billboard ||
                  (bag->texture && bag->texture->coordinates &&
                   bag->color->many && !bag->lighting &&
                   !bag->info->fullClipChecks &&
                   bag->info->frustumCulling ==
                       PipelineInfoBagFrustumCulling_None),
              "Billboard bags need per-particle params in the texture "
              "coordinates slot, per-particle colors, no lighting, no "
              "frustum culling and no clip checks (VU1 culls per quad)!");
  // Modified by TyraX: env (matcap) bags - normals in the ST slot, ST
  // computed on VU1 (cull_tce + as_is_tce / clip_tce). No lighting - the
  // env programs derive no dir-light color.
  TYRA_ASSERT(!bag->texture || !bag->texture->coordinatesAreNormals ||
                  bag->lighting == nullptr,
              "Env (matcap) bags do not support lighting!");
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
    // Modified by TyraX: transform the 6 frustum planes into this bag's
    // object space once; the main-bbox check and every package
    // classification then run the two-corner AABB test instead of
    // transforming 8 corners per box and dotting each against every plane.
    CoreBBox::computeObjectSpacePlanes(
        objectSpacePlanes, rendererCore->renderer3D.frustumPlanes.getAll(),
        *bag->info->model);

    frustumCheck = bbox->getMainBBox()->frustumCheckAABB(objectSpacePlanes);

    if (frustumCheck == OUTSIDE_FRUSTUM) {
      return;
    }
  }

  // Modified by TyraX: the per-bag blend equation (additiveBlendFix - the
  // reflective materials' env pass) travels IN-BAND with the mesh's tags
  // (sendObjectData uploads the ALPHA A+D qword, every program emits it),
  // so no FINISH barriers are needed here anymore.

  packager.setRenderBBox(bbox);
  packager.setObjectSpacePlanes(frustumCull ? objectSpacePlanes : nullptr);

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
  // Modified by TyraX: billboard bags may carry a texture bag with no image
  // (params channel only) - nothing to bind then.
  if (bag->texture && bag->texture->texture) {
    auto temp = rendererCore->texture.useTexture(bag->texture->texture);
    texBuffersStorage = {temp.id, temp.core, temp.clut};
    texBuffers = &texBuffersStorage;
  }

  // Modified by TyraX: billboard bags run from their own on-demand program
  // set (micro memory is full - see ensureProgramSet); non-billboard bags
  // lazily restore the resident set.
  qbufferRenderer.ensureProgramSet(bag->billboard != nullptr);

  qbufferRenderer.clearLastProgramName();

  // Modified by TyraX: pick this bag's dynamic light (flashlight vs scene
  // point lights - the color programs have ONE light slot per mesh). The
  // pick runs on the bag's world-space bounding sphere; without a bbox
  // (frustumCulling != Precise, e.g. the sky dome) the model translation
  // stands in with radius 0. Bags with dynLightPick = false (terrain
  // chunks, the sky dome) keep the global flashlight state - a per-chunk
  // pick shows a hard seam wherever neighbors pick different lights.
  //
  // Modified by TyraX: the SAME world bounding sphere also feeds the BLSS
  // neural upscaler's per-tile features (docs/neural-upscaler.md), so it is
  // computed once here and consumed by both. Everything below is inert -
  // and the sphere is not computed at all - when neither consumer wants it.
  // Modified by TyraX: a bag may opt OUT of the BLSS grid (blssProxy). A shell
  // centred on the camera - the sky dome, the star field, the sun and moon
  // discs - has no describable screen box: it wraps the near plane, so its
  // proxy is "the whole frame, at the nearest representable depth" and it
  // flattens every channel it touches. See PipelineInfoBag::blssProxy.
  const bool blssOn =
      rendererCore->blss.isEnabled() && bag->info->blssProxy;
  const bool wantsLightPick = !bag->lighting && bag->info->dynLightPick;

  const M4x4& m = *bag->info->model;
  Vec4 worldCenter(m.data[12], m.data[13], m.data[14], 1.0F);
  float worldRadius = 0.0F;
  if ((wantsLightPick || blssOn) && bbox) {
    const auto* mb = bbox->getMainBBox();
    const Vec4& lo = (*mb)[0];
    const Vec4& hi = (*mb)[7];
    const Vec4 mid((lo.x + hi.x) * 0.5F, (lo.y + hi.y) * 0.5F,
                   (lo.z + hi.z) * 0.5F, 1.0F);
    worldCenter = m * mid;
    // Near-uniform scale assumed (same as the light's object-space
    // transform in sendObjectData) - column 0 length is the scale.
    const float scale = sqrtf(m.data[0] * m.data[0] + m.data[1] * m.data[1] +
                              m.data[2] * m.data[2]);
    const float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
    worldRadius = 0.5F * sqrtf(ex * ex + ey * ey + ez * ez) * scale;
  }

  const RendererCoreSpotLight* bagLight = nullptr;
  if (wantsLightPick) {
    bagLight = rendererCore->pickDynLight(worldCenter, worldRadius);
  }
  qbufferRenderer.setBagLight(bagLight);

  // Modified by TyraX: the BLSS bag feed. Inert when BLSS is off (and when it
  // is on but we are not inside its beginScene/endScene bracket - the
  // RendererCoreBlss entry points check). The upscaler never reads the
  // framebuffer back, so a bag's screen bbox + w range + two material scalars
  // is ALL the network ever learns about a frame; see
  // docs/blss-reconstruction.md section 2.
  //
  // ONE BAG IS NOT ONE PROXY, and that used to be the feature's biggest lie.
  // A bag carries one bbox and one w range, so a floor or terrain mesh
  // reported "fully covered, at my nearest depth" for every tile it touched -
  // on a still `fpp` scene that came out as depth = grad = cover = 1 in all
  // 224 tiles, a network output that was the same constant everywhere, and a
  // sky reconstructed from history. The corpus never had the problem because
  // it chunks its floors 8x8 and its walls x6.
  //
  // The packages this bag is about to be split into already carry their own
  // axis-aligned boxes - cached, and computed for frustum classification
  // whether or not BLSS is on - so the grid gets the corpus' granularity for
  // the cost of walking a vector. Bags with no bbox (frustumCulling != Precise
  // - particle billboards) fall back to the bounding-sphere proxy, which for
  // them means contributing nothing at all: without a bbox the sphere is the
  // model translation at radius 0 and addBag rejects the empty box. That is
  // the behaviour those bags already had, not a new hole.
  if (blssOn) {
    // texDetail is the minification proxy - texels per screen pixel - so BLSS
    // gets the raw texel area and finishes the ratio once it knows the screen
    // footprint it just computed. Untextured bags carry no texture aliasing.
    float texelArea = 0.0F;
    if (bag->texture && bag->texture->texture) {
      texelArea =
          static_cast<float>(bag->texture->texture->getWidth()) *
          static_cast<float>(bag->texture->texture->getHeight());
    }
    // There used to be a `luma` here too - the bag's own brightness, plus
    // whatever dynamic light was picked for it. It is gone with the channel it
    // fed, and THIS is the code that killed it: `bag->color->single` is null
    // for every per-vertex-lit mesh a generated game submits, so the value it
    // could actually compute was the fallback 0.5 in nearly every frame, while
    // the corpus trained the network on a real spread. See
    // RendererCoreBlss::kFeatures.

    if (bbox != nullptr) {
      // bbox is non-null only for Precise frustum culling, and the assert
      // above makes that imply TyraMVP - so `mvp` below really is the
      // view-projection times this bag's model matrix, which is the space
      // addBagBox projects from.
      const std::vector<CoreBBox>& parts = bbox->getParts();
      const u32 partsCount = static_cast<u32>(parts.size());
      if (partsCount == 0) {
        rendererCore->blss.addBagSphere(worldCenter, worldRadius, texelArea);
      } else {
        // At most kMaxProxiesPerBag boxes: merge consecutive parts when a mesh
        // has more. Merging by vertex range (not by space) can only ENLARGE a
        // box, never move it, so the worst case degrades toward the whole-bag
        // proxy instead of lying about where the geometry is.
        const u32 cap =
            static_cast<u32>(RendererCoreBlss::kMaxProxiesPerBag);
        const u32 group = partsCount <= cap ? 1u : (partsCount + cap - 1) / cap;
        for (u32 i = 0; i < partsCount; i += group) {
          const u32 end = i + group < partsCount ? i + group : partsCount;
          Vec4 lo = parts[i].vertices[0];
          Vec4 hi = parts[i].vertices[7];
          for (u32 k = i + 1; k < end; k++) {
            const Vec4& l = parts[k].vertices[0];
            const Vec4& h = parts[k].vertices[7];
            if (l.x < lo.x) lo.x = l.x;
            if (l.y < lo.y) lo.y = l.y;
            if (l.z < lo.z) lo.z = l.z;
            if (h.x > hi.x) hi.x = h.x;
            if (h.y > hi.y) hi.y = h.y;
            if (h.z > hi.z) hi.z = h.z;
          }
          rendererCore->blss.addBagBox(mvp, lo, hi, texelArea);
        }
      }
    } else {
      rendererCore->blss.addBagSphere(worldCenter, worldRadius, texelArea);
    }
  }

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

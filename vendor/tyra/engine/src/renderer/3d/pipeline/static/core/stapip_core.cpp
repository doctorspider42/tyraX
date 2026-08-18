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
#include "debug/frame_profile.hpp"

// #define TYRA_RENDERER_VERBOSE_LOG 1

#ifdef TYRA_RENDERER_VERBOSE_LOG
#define Verbose(...) TyraDebug::writeLines("VRB: ", ##__VA_ARGS__, "\n")
#else
#define Verbose(...) ((void)0)
#endif

namespace Tyra {

// Modified by TyraX: the light a `spotLit = false` bag is handed - off and
// black, which the color programs compute with as a no-op. At namespace scope
// rather than as a function static so the per-bag path pays no init guard.
static const RendererCoreSpotLight kNoSpotLight = [] {
  RendererCoreSpotLight l;
  l.color = Color(0.0F, 0.0F, 0.0F, 128.0F);
  l.range = 0.0F;
  return l;
}();

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

void StaPipCore::setVU1Clipping(const bool& enabled) {
  qbufferRenderer.setVU1Clipping(enabled);
  // Functional plane masks are required by the clip microprograms. Telemetry
  // also requests them while the EE clipper is selected, for its histogram.
  packager.setCapturePlaneMasks(enabled || telemetryEnabled);
}

void StaPipCore::setTelemetryEnabled(const bool& enabled) {
  telemetryEnabled = enabled;
  telemetry = StaPipTelemetry{};
  packager.setCapturePlaneMasks(
      enabled || qbufferRenderer.isVU1ClippingEnabled());
  qbufferRenderer.setTelemetry(enabled ? &telemetry : nullptr);
}

StaPipTelemetry StaPipCore::takeTelemetry() {
  const StaPipTelemetry result = telemetry;
  telemetry = StaPipTelemetry{};
  return result;
}

void StaPipCore::computeClipObjectSpacePlanes(const M4x4& mvp) {
  const float nearZ = rendererCore->getSettings().getNear() -
                      (-PlanesClipAlgorithm::clipMargin);
  const float farZ = -rendererCore->getSettings().getFar();
  const float b = VU1_CLIP_XY_BAND;
  const float planes[8][5] = {
      {0.0F, 0.0F, -1.0F, 0.0F, nearZ},  // near
      {0.0F, 0.0F, 1.0F, 0.0F, -farZ},   // far
      {-1.0F, 0.0F, 0.0F, b, 0.0F},      // right
      {1.0F, 0.0F, 0.0F, b, 0.0F},       // left
      {0.0F, -1.0F, 0.0F, b, 0.0F},      // bottom (projection flips Y)
      {0.0F, 1.0F, 0.0F, b, 0.0F},       // top
      // Modified by TyraX: EE-only, never uploaded. The cull program's clipw
      // tests |z| < |w| on top of x and y, so a package may only take that
      // path when it is inside the exact near (z <= w) and far (z >= -w)
      // planes too. The guard band's own near constant is DELIBERATELY looser
      // (PlanesClipAlgorithm::clipMargin), which leaves a thin shell in front
      // of the near plane where the clipper draws a triangle the cull program
      // would ADC away - a hole at point blank range.
      {0.0F, 0.0F, -1.0F, 1.0F, 0.0F},  // exact near
      {0.0F, 0.0F, 1.0F, 1.0F, 0.0F},   // exact far
  };

  const float* m = mvp.data;
  for (u8 i = 0; i < 8; ++i) {
    const float* p = planes[i];
    Plane& out = clipObjectSpacePlanes[i];
    out.normal.x = p[0] * m[0] + p[1] * m[1] + p[2] * m[2] + p[3] * m[3];
    out.normal.y = p[0] * m[4] + p[1] * m[5] + p[2] * m[6] + p[3] * m[7];
    out.normal.z = p[0] * m[8] + p[1] * m[9] + p[2] * m[10] + p[3] * m[11];
    out.normal.w = 1.0F;
    out.distance = p[4] + p[0] * m[12] + p[1] * m[13] + p[2] * m[14] +
                   p[3] * m[15];
  }
}

// Modified by TyraX: guard-band routing (docs/vu1-clipping.md).
//
// A package is classified against the VIEW frustum (the screen edge), but the
// VU1 clip planes are the near/far pair plus an X/Y band at
// VU1_CLIP_XY_BAND * w - about seven times the screen's half extent, still
// inside the GS raster window. So a package that merely straddles the screen
// border crosses no VU clip plane at all, and the packager says so:
// `activePlaneMaskAABB` sets a bit when the box crosses OR lies outside a
// plane, hence an all-clear over its eight planes means the whole box is
// inside every one of them - including w > 0, because for a negative w the
// two side half-spaces are contradictory and at least one bit would be set.
//
// Such a package needs no cutting: every vertex passes the cull program's own
// clipw judgement and the GS scissor crops the raster. Sending it to the
// clipper anyway cost a 1/3-size split (3x the VU1 kicks), a memcpy of every
// vertex stream instead of a DMA by reference, and the clip program's scratch
// stores - all to run a plane loop with nothing active.
//
// The flag is only ever set with VU1 clipping on: with the EE clipper the
// packager has no clip planes to test and fills clipPlaneMask with the
// VIEW-plane crossing mask for telemetry instead.
bool StaPipCore::isGuardBandOnly(const StaPipBagPackage& package) const {
  return package.guardBandOnly;
}

void StaPipCore::recordPackage(const StaPipBagPackage& package,
                               const CoreBBoxFrustum& route) {
  if (!telemetryEnabled) return;

  const u32 triangles = package.size / 3;
  if (route == IN_FRUSTUM) {
    ++telemetry.packagesCull;
    telemetry.trianglesCull += triangles;
  } else if (route == PARTIALLY_IN_FRUSTUM) {
    ++telemetry.packagesClip;
    telemetry.trianglesClip += triangles;
    u8 bits = package.clipPlaneMask;
    u8 count = 0;
    while (bits != 0) {
      count += bits & 1U;
      bits >>= 1;
    }
    ++telemetry.activePlanePopcount[count <= 6 ? count : 6];
  } else {
    ++telemetry.packagesOutside;
    telemetry.trianglesOutside += triangles;
  }
}

// Modified by TyraX: the subset of packagesCull that took the cull path ONLY
// because of the guard band - i.e. what the clipper no longer sees. Recorded
// at the routing sites rather than inside recordPackage, so a bag that culls
// everything anyway (fullClipChecks off) is not counted.
void StaPipCore::recordGuardBandPackage(const StaPipBagPackage& package) {
  if (!telemetryEnabled) return;
  ++telemetry.packagesGuardBand;
  telemetry.trianglesGuardBand += package.size / 3;
}

void StaPipCore::recordOutsideBag(const StaPipBag* bag) {
  if (!telemetryEnabled) return;
  telemetry.packagesOutside +=
      (bag->count + maxVertCount - 1) / maxVertCount;
  telemetry.trianglesOutside += bag->count / 3;
}

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
      recordOutsideBag(bag);
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

  if (qbufferRenderer.isVU1ClippingEnabled()) {
    computeClipObjectSpacePlanes(mvp);
    packager.setClipObjectSpacePlanes(clipObjectSpacePlanes);
  } else {
    packager.setClipObjectSpacePlanes(nullptr);
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

  // Modified by TyraX: per-bag texture wrap. GS_REG_CLAMP is global state and
  // the VU1 programs have no micro memory left to carry it in-band the way
  // the ALPHA qword is (the clip family sits at 1992/2042), so a bag whose
  // texture asked for anything but REPEAT gets it the way the blend equation
  // used to: drain PATH1, write the register, draw, put REPEAT back.
  // Path3::clearScreen guarantees REPEAT for every other mesh in the frame,
  // which is what the terrain needs (its STs are world position x tile
  // factor). Only the render targets ask - camera feeds and the raytraced
  // mirror, whose edge rows must not bilinear-wrap into the opposite side -
  // so an ordinary mesh pays one pointer comparison.
  const bool clampedBag =
      bag->texture && bag->texture->texture &&
      bag->texture->texture->getWrapSettings()->horizontal != WRAP_REPEAT;
  if (clampedBag) {
    rendererCore->sync.align3D();
    rendererCore->gs.setTextureWrap(*bag->texture->texture->getWrapSettings());
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
  // wantsProxies(), NOT isEnabled(): in BLSS' PLAIN mode there is no network
  // to describe the frame to, so the whole branch below - and the world
  // bounding sphere it shares with the light pick - must not run at all. The
  // entry points are inert there anyway, but "inert" still pays two sqrtf and
  // a texel-area lookup per bag, and `proxy` is 2.34 ms of a 4.60 ms bill.
  const bool blssOn =
      rendererCore->blss.wantsProxies() && bag->info->blssProxy;
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
  // Modified by TyraX: a bag may opt out of the camera spot as well
  // (PipelineInfoBag::spotLit - the terrain, whose light comes from the
  // flashlight's projected pool instead). A null bagLight means "the global
  // flashlight", so opting out needs a light OBJECT rather than a null: the
  // one below is off and black, which the programs compute with as a no-op.
  if (!bag->info->spotLit) bagLight = &kNoSpotLight;
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
  // the cost of walking a vector. Bags with no bbox (frustumCulling != Precise)
  // fall back to the bounding-sphere proxy, which for them means contributing
  // nothing at all: without a bbox the sphere is the model translation at
  // radius 0 and addBag rejects the empty box.
  //
  // THAT SILENCE IS NOT HARMLESS FOR ONE BAG SHAPE, and it is the one the
  // straddle rule and the blssProxy opt-out cannot help with. A particle
  // emitter runs frustumCulling None deliberately (VU1 culls per quad), so it
  // has never contributed a proxy - while on the fixtures BLSS is measured on
  // the emitters are 95-99 % of the frame's fill. The network therefore chose
  // its kernels over fire, fog and rain entirely from the geometry BEHIND
  // them. TYRA_BLSS_EMITTER_PROXY is the sixth twin rule that closes it; it
  // ships at 0, because turning it on moves every label and needs a refit.
  if (blssOn) {
#if TYRA_FRAME_PROFILE
    // Charged to FrameProfile::tBlssProxy, which beginScene clears - so the
    // "extra scene submission" term is READ rather than inferred by
    // subtracting everything else from the A/B difference.
    const u32 fpP0 = FrameProfile::ticks();
#endif
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

#if TYRA_BLSS_EMITTER_PROXY
    // THE SIXTH RULE: an emitter bag describes itself from the centres it is
    // about to submit. It is the one bag shape that reaches here with no
    // package bbox BY DESIGN rather than by omission - a billboard bag runs
    // frustumCulling None because VU1 culls per quad - so without this branch
    // it falls to the radius-0 sphere below and addBag rejects it. On the
    // fixtures this feature is measured on that silence is 95-99 % of the
    // frame's fill. See RendererCoreBlss::addBagBillboard for the box, and the
    // BLSS header for why it is one box and not one per VU1 package.
    if (bag->billboard != nullptr) {
      rendererCore->blss.addBagBillboard(
          mvp, bag->vertices, bag->count, bag->texture->coordinates,
          bag->billboard->right, bag->billboard->up, texelArea);
    } else
#endif
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
#if TYRA_BLSS_PROXY_BUDGET
        // THE PROXY BUDGET (twin switch - see the BLSS header). The cap is the
        // number of grid tiles this bag's whole box covers, so a bag the grid
        // can only resolve into four tiles is described by four boxes instead
        // of thirty-two. The main bbox is the one the frustum classification
        // above already fetched.
        const u32 cap = static_cast<u32>(rendererCore->blss.proxyBudget(
            mvp, (*bbox->getMainBBox())[0], (*bbox->getMainBBox())[7]));
#else
        const u32 cap =
            static_cast<u32>(RendererCoreBlss::kMaxProxiesPerBag);
#endif
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
#if TYRA_FRAME_PROFILE
    FrameProfile::tBlssProxy += FrameProfile::ticks() - fpP0;
#endif
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
      recordPackage(biggerPkgs[i], IN_FRUSTUM);
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

  if (clampedBag) {  // Modified by TyraX: restore the frame's REPEAT contract
    rendererCore->sync.align3D();
    rendererCore->gs.setTextureWrap(RendererCoreGS::repeatWrap());
  }

  Verbose("Render finished");
}

void StaPipCore::renderPkgs(StaPipBagPackage* packages, const bool& doClip,
                            u16 count) {
  for (u16 i = 0; i < count; i++) {
    // Modified by TyraX: a package that only leaves the screen, not the guard
    // band, is culled whole and by POINTER - no 1/3 split, no copy, no clipper.
    const bool guardBandOnly = doClip && isGuardBandOnly(packages[i]);
    auto cull = (doClip && packages[i].isInFrustum == IN_FRUSTUM) || !doClip ||
                guardBandOnly;
    auto doSubpkgs = doClip && !guardBandOnly &&
                     packages[i].isInFrustum == PARTIALLY_IN_FRUSTUM;

    if (cull) {
      Verbose(i, " - package in frustum -> cull");
      recordPackage(packages[i], IN_FRUSTUM);
      if (guardBandOnly) recordGuardBandPackage(packages[i]);
      auto buffer = qbufferRenderer.getBuffer();
      buffer->fillByPointer(packages[i]);
      qbufferRenderer.cull(buffer);
    } else if (doSubpkgs) {
      u16 subpkgsSize = 0;
      auto packages1By3 =
          packager.create(&subpkgsSize, packages[i], clipPackageSize());
      Verbose(i, " - partial package. Created subpkgs: ", subpkgsSize);

      renderSubpkgs(packages1By3, subpkgsSize);
    } else {
      recordPackage(packages[i], OUTSIDE_FRUSTUM);
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
  // Modified by TyraX: a subpackage inside the guard band joins them - it needs
  // no cutting either, so it batches into the same cull buffers.
  for (u16 i = 0; i < count; i++) {
    if (subpkgs[i].isInFrustum == IN_FRUSTUM || isGuardBandOnly(subpkgs[i])) {
      recordPackage(subpkgs[i], IN_FRUSTUM);
      if (isGuardBandOnly(subpkgs[i])) recordGuardBandPackage(subpkgs[i]);
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
      if (subpkgs[i].isInFrustum == OUTSIDE_FRUSTUM)
        recordPackage(subpkgs[i], OUTSIDE_FRUSTUM);
      Verbose(i, " - subpkg skipped, already rendered/outside");
      continue;
    }

    auto buffer = qbufferRenderer.getBuffer();
    recordPackage(subpkgs[i], PARTIALLY_IN_FRUSTUM);
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

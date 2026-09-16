/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "math/math.hpp"

#include <gs_gp.h>
#include <math.h>
#include <string.h>
#include "debug/hardware_trace.hpp"
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

// Modified by TyraX: diagnostic COP0 reads are entirely opt-in.
static inline u32 readCoreTelemetryTicks() {
  u32 ticks;
  asm volatile("mfc0 %0, $9" : "=r"(ticks));
  return ticks;
}

// Added by TyraX: the render-submission attribution brackets
// (stapip_attrib.hpp, docs/render-submission-attribution.md). Compiled out
// entirely at TYRA_STAPIP_ATTRIB 0, which is the default - the macros below
// expand to nothing at all, so the shipped pipeline gains no field, no COP0
// read and no branch. When compiled in they still respect `telemetryEnabled`,
// exactly like the three brackets that already exist.
#if TYRA_STAPIP_ATTRIB
#define TYRA_ATTRIB_MARK(var) \
  const u32 var = telemetryEnabled ? readCoreTelemetryTicks() : 0
#define TYRA_ATTRIB_ADD(field, var)                            \
  if (telemetryEnabled)                                        \
  telemetry.attrib.field += readCoreTelemetryTicks() - (var)
#define TYRA_ATTRIB_INC(field) \
  if (telemetryEnabled) ++telemetry.attrib.field
#define TYRA_ATTRIB_SPAN(field, from, to) \
  if (telemetryEnabled) telemetry.attrib.field += (to) - (from)
#else
#define TYRA_ATTRIB_MARK(var) ((void)0)
#define TYRA_ATTRIB_ADD(field, var) ((void)0)
#define TYRA_ATTRIB_INC(field) ((void)0)
#define TYRA_ATTRIB_SPAN(field, from, to) ((void)0)
#endif


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

void StaPipCore::onFrameEnd() {
// Modified by TyraX: OPT-IN, not `!defined(NDEBUG)`. No game build defines
// NDEBUG - release only drops -g and KEEPSYM (docs/devkit.md) - so this
// printed in a RELEASE game, once every 300 frames, which lands inside the
// 240-row sampling window of every performance pose and breaks
// benchmark-district.py's own "no sample-time host writes" contract. Over
// ps2link a host: write is a network round trip.
#if TYRA_STAPIP_RETAINED_COMMANDS && TYRA_STAPIP_RETAINED_REPORT
  // Modified by TyraX: the retained-command readout, once every 300 frames.
  // A debug build is the only in-engine consumer of these counters, so a
  // release game that wants them still gets every one (the take* accessors
  // reset on read - see docs/retained-static-commands.md).
  {
    static u32 retFrames = 0, retHits = 0, retBuilds = 0;
    retHits += qbufferRenderer.takeRetainedHits();
    retBuilds += qbufferRenderer.takeRetainedBuilds();
    if (++retFrames >= 300) {
      TYRA_LOG("STAPIPRET retained=", retHits / retFrames,
               " rebuilt=", retBuilds / retFrames, " per frame, cache=",
               qbufferRenderer.getRetainedBytes() / 1024, " KB");
      retFrames = 0;
      retHits = 0;
      retBuilds = 0;
    }
  }
#endif
  qbufferRenderer.onFrameEnd();
  cacher.onFrameEnd();
  transformCacheValid = false;
  transformCachePlanesValid = false;
  transformCacheModelPtr = nullptr;
}

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
#if TYRA_STAPIP_ATTRIB
  // Added by TyraX: the cacher and the packager keep their own counters -
  // neither has a view of `telemetry` - so they are folded in here and
  // cleared, which gives them the same reset-on-read contract as everything
  // else in StaPipTelemetry. `entries` is a level, not an accumulation, so it
  // is assigned rather than added and is NOT zeroed with the rest.
  telemetry.attrib.bboxCacheHits += cacher.stats.hits;
  telemetry.attrib.bboxCacheRecalcs += cacher.stats.recalcs;
  telemetry.attrib.bboxCacheFresh += cacher.stats.fresh;
  telemetry.attrib.bboxCacheProbes += cacher.stats.probes;
  telemetry.attrib.bboxCacheRecalcTicks += cacher.stats.recalcTicks;
  telemetry.attrib.bboxCacheEntries = cacher.stats.entries;
  telemetry.attrib.bboxCacheFrameEndTicks += cacher.stats.frameEndTicks;
  const u32 keptEntries = cacher.stats.entries;
  cacher.stats = StapipBagBBoxesCacher::Stats{};
  cacher.stats.entries = keptEntries;

  telemetry.attrib.dsClassifyTicks += packager.stats.classifyTicks;
  telemetry.attrib.dsPackages += packager.stats.packages;
  telemetry.attrib.dsMergeParts += packager.stats.mergeParts;
  telemetry.attrib.dsMaskCalls += packager.stats.maskCalls;
  packager.stats = StaPipBagPackager::Stats{};
#endif
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

  // Modified by TyraX: a strip of N vertices is N-2 triangles, not N/3.
  const u32 triangles =
      package.bag != nullptr && package.bag->stripped
          ? (package.size >= 2 ? package.size - 2 : 0)
          : package.size / 3;
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
  // Modified by TyraX: the SAME rule recordPackage uses, or this counter
  // measures a different thing from the `cull` population it documents itself
  // as a subset of. It did: over one garage capture guard averaged 21.5
  // triangles per package against cull's 37.9, purely because a strip package
  // was charged size/3 here and size-2 there.
  telemetry.trianglesGuardBand +=
      package.bag != nullptr && package.bag->stripped
          ? (package.size >= 2 ? package.size - 2 : 0)
          : package.size / 3;
}

void StaPipCore::recordOutsideBag(const StaPipBag* bag) {
  if (!telemetryEnabled) return;
  // Modified by TyraX: a rejected bag is sliced into the same packages it would
  // have been submitted as, and a stripped one gives EACH of those its own
  // strip - so the bag is packages * (runLength - 2) triangles, not
  // count - 2. Charging the whole bag as one strip over-counted by
  // 2 * (packages - 1), which is exactly the population this counter exists to
  // compare against the submitted one.
  const u32 outsidePackages = (bag->count + maxVertCount - 1) / maxVertCount;
  telemetry.packagesOutside += outsidePackages;
  if (!bag->stripped) {
    telemetry.trianglesOutside += bag->count / 3;
  } else {
    const u32 whole = bag->count / maxVertCount;
    const u32 tail = bag->count - whole * maxVertCount;
    telemetry.trianglesOutside +=
        whole * (maxVertCount >= 2 ? maxVertCount - 2 : 0) +
        (tail >= 2 ? tail - 2 : 0);
  }
}

u32 StaPipCore::getMaxVertCountByBag(const StaPipBag* bag) {
  // Added by TyraX: the two halves of this function are charged separately
  // (stapip_attrib.hpp) - resolving the program against doing the arithmetic -
  // because they have completely different fixes.
  TYRA_ATTRIB_MARK(attribProgStart);
  StaPipVU1Program* const cullProgram = qbufferRenderer.getCullProgramByBag(bag);
  TYRA_ATTRIB_ADD(bdProgTicks, attribProgStart);

  TYRA_ATTRIB_MARK(attribCalcStart);
  const u32 derived = cullProgram->getMaxVertCount(
      bag->color->many == nullptr, qbufferRenderer.getBufferSize());
  TYRA_ATTRIB_ADD(bdSizeCalcTicks, attribCalcStart);

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
  HardwareTrace::Scope traceBag("Static_bag");
  TYRA_ATTRIB_MARK(attribRenderStart);
  TYRA_ATTRIB_INC(renderCalls);
  if (bag->count <= 0) {
    TYRA_ATTRIB_ADD(renderTicks, attribRenderStart);
    return;
  }

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

  const u32 traceBoundsStart = HardwareTrace::active ? HardwareTrace::ticks() : 0;
  const u32 boundsStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;
  // The head costs no clock of its own: the bounds bracket's own first read
  // is also the head's last one. Everything above this line - the fog
  // decision, the frustum-culling read and the thirteen TYRA_ASSERTs, which
  // a release game really does execute - is charged here.
  TYRA_ATTRIB_SPAN(headTicks, attribRenderStart, boundsStart);
  // Added by TyraX: the five parts of `bounds` (stapip_attrib.hpp). Two
  // careful attacks on this bucket - a branchless AABB test and a compacted
  // partBounds stride - together recovered 2% of it, so the cost is in one of
  // these five and guessing which has already been paid for twice.
  TYRA_ATTRIB_MARK(attribSizeStart);
  u32 maxVertCount = getMaxVertCountByBag(bag);
  TYRA_ATTRIB_ADD(bdSizeTicks, attribSizeStart);

  // Imported models commonly submit one bag per material. Consecutive parts
  // point at the same model matrix, so their object-space frustum planes and
  // MVP are identical. Include the matrix values and current view-projection
  // in the key: live transforms and portal/split views remain exact.
  TYRA_ATTRIB_MARK(attribXformStart);
  const M4x4& currentViewProj = rendererCore->renderer3D.getViewProj();
  const bool reuseTransform =
      transformCacheValid &&
      bag->info->transformationType == TyraMVP &&
      transformCacheModelPtr == bag->info->model &&
      memcmp(transformCacheModel.data, bag->info->model->data,
             sizeof(transformCacheModel.data)) == 0 &&
      memcmp(transformCacheViewProj.data, currentViewProj.data,
             sizeof(transformCacheViewProj.data)) == 0;
  TYRA_ATTRIB_ADD(bdXformTicks, attribXformStart);

  StaPipBagPackagesBBox* bbox = nullptr;
  if (bag->info->frustumCulling == PipelineInfoBagFrustumCulling_Precise) {
    // TyraX: the bag's bboxVersion invalidates the cached boxes for
    // reused vertex buffers with new content (the cacher recomputes the
    // entry in place - per-frame bumps stay allocation-free)
    TYRA_ATTRIB_MARK(attribCacheStart);
    bbox = cacher.getBBoxes(bag->vertices, bag->count,
                            reinterpret_cast<u32>(bag->vertices),
                            bag->bboxVersion, maxVertCount);
    TYRA_ATTRIB_ADD(bdCacheTicks, attribCacheStart);
  }

  // Three stores, charged to the same bucket as the derivation that produced
  // the value. Kept HERE rather than hoisted next to getMaxVertCountByBag so
  // the instrumented build's call order is the shipped one.
  TYRA_ATTRIB_MARK(attribSetSizeStart);
  setMaxVertCount(maxVertCount);
  TYRA_ATTRIB_ADD(bdSizeTicks, attribSetSizeStart);

  CoreBBoxFrustum frustumCheck = OUTSIDE_FRUSTUM;

  if (frustumCull) {
    // Modified by TyraX: transform the 6 frustum planes into this bag's
    // object space once; the main-bbox check and every package
    // classification then run the two-corner AABB test instead of
    // transforming 8 corners per box and dotting each against every plane.
    TYRA_ATTRIB_MARK(attribPlanesStart);
    if (reuseTransform && transformCachePlanesValid) {
      for (u8 i = 0; i < 6; ++i)
        objectSpacePlanes[i] = transformCacheObjectSpacePlanes[i];
    } else {
      CoreBBox::computeObjectSpacePlanes(
          objectSpacePlanes, rendererCore->renderer3D.frustumPlanes.getAll(),
          *bag->info->model);
    }
    TYRA_ATTRIB_ADD(bdPlanesTicks, attribPlanesStart);

    TYRA_ATTRIB_MARK(attribMainStart);
    frustumCheck = bbox->getMainBBox()->frustumCheckAABB(objectSpacePlanes);
    TYRA_ATTRIB_ADD(bdMainTicks, attribMainStart);

    if (frustumCheck == OUTSIDE_FRUSTUM) {
      if (telemetryEnabled) telemetry.boundsTicks += readCoreTelemetryTicks()-boundsStart;
      TYRA_ATTRIB_ADD(renderTicks, attribRenderStart);
      TYRA_ATTRIB_INC(renderCallsCulled);
      if (HardwareTrace::active) HardwareTrace::record("Bounds", traceBoundsStart, HardwareTrace::ticks());
      recordOutsideBag(bag);
      return;
    }
  }

  if (telemetryEnabled) telemetry.boundsTicks += readCoreTelemetryTicks()-boundsStart;
  if (HardwareTrace::active) HardwareTrace::record("Bounds", traceBoundsStart, HardwareTrace::ticks());
  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;

  // Modified by TyraX: the per-bag blend equation (additiveBlendFix - the
  // reflective materials' env pass) travels IN-BAND with the mesh's tags
  // (sendObjectData uploads the ALPHA A+D qword, every program emits it),
  // so no FINISH barriers are needed here anymore.

  TYRA_ATTRIB_MARK(attribPackagerStart);
  packager.setRenderBBox(bbox);
  // Modified by TyraX: a wholly visible bag needs no per-package tests.
  // That route submits every package directly; its classifications are unused.
  const bool classifyPackages = frustumCull && frustumCheck == PARTIALLY_IN_FRUSTUM;
  packager.setObjectSpacePlanes(classifyPackages ? objectSpacePlanes : nullptr);

  M4x4 mvp;

  if (bag->info->transformationType == TyraMP) {
    mvp = rendererCore->renderer3D.getProjection() * *bag->info->model;
  } else if (reuseTransform) {
    mvp = transformCacheMvp;
  } else {
    mvp = currentViewProj * *bag->info->model;
  }

  if (bag->info->transformationType == TyraMVP && !reuseTransform) {
    transformCacheValid = true;
    transformCachePlanesValid = frustumCull;
    transformCacheModelPtr = bag->info->model;
    transformCacheModel = *bag->info->model;
    transformCacheViewProj = currentViewProj;
    transformCacheMvp = mvp;
    if (frustumCull)
      for (u8 i = 0; i < 6; ++i)
        transformCacheObjectSpacePlanes[i] = objectSpacePlanes[i];
  } else if (reuseTransform && frustumCull &&
             !transformCachePlanesValid) {
    for (u8 i = 0; i < 6; ++i)
      transformCacheObjectSpacePlanes[i] = objectSpacePlanes[i];
    transformCachePlanesValid = true;
  }

  if (classifyPackages && qbufferRenderer.isVU1ClippingEnabled()) {
    computeClipObjectSpacePlanes(mvp);
    packager.setClipObjectSpacePlanes(clipObjectSpacePlanes);
  } else {
    packager.setClipObjectSpacePlanes(nullptr);
  }
  TYRA_ATTRIB_ADD(prepPackagerTicks, attribPackagerStart);

  TYRA_ATTRIB_MARK(attribTextureStart);
  const bool directBag =
      (frustumCull && frustumCheck == IN_FRUSTUM) ||
      (!frustumCull && !bag->info->fullClipChecks);
  const bool batchScope = qbufferRenderer.isSubmissionBatchOpen();
  const bool hasTexture = bag->texture && bag->texture->texture;
  const bool cheapBatchCandidate =
      batchScope && directBag && bag->billboard == nullptr &&
      bag->count <= maxVertCount * 15 &&
      !qbufferRenderer.hasProgramOverrides();
  bool residentTexture = true;
  bool repeatTexture = true;
  if (cheapBatchCandidate && hasTexture) {
    residentTexture =
        bag->texture->texture->vramResident != nullptr ||
        rendererCore->texture
                .getAllocatedBuffersByTextureId(bag->texture->texture->id)
                .id != 0;
    repeatTexture =
        bag->texture->texture->getWrapSettings()->horizontal == WRAP_REPEAT &&
        bag->texture->texture->getWrapSettings()->vertical == WRAP_REPEAT;
  }
  // Modified by TyraX: decide before useTexture(), because a GIF upload must
  // never overtake geometry retained in an earlier PATH1 packet.
  qbufferRenderer.setSubmissionBatchCandidate(
      cheapBatchCandidate && residentTexture && repeatTexture,
      hasTexture);

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
  TYRA_ATTRIB_ADD(prepTextureTicks, attribTextureStart);

  TYRA_ATTRIB_MARK(attribProgramStart);
  // Modified by TyraX: billboard bags run from their own on-demand program
  // set (micro memory is full - see ensureProgramSet); non-billboard bags
  // lazily restore the resident set.
  qbufferRenderer.ensureProgramSet(bag->billboard != nullptr);

  qbufferRenderer.clearLastProgramName();
  TYRA_ATTRIB_ADD(prepProgramTicks, attribProgramStart);

  TYRA_ATTRIB_MARK(attribLightStart);
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
    const float scale = Math::sqrtNonNegative(m.data[0] * m.data[0] + m.data[1] * m.data[1] +
                              m.data[2] * m.data[2]);
    const float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
    worldRadius = 0.5F * Math::sqrtNonNegative(ex * ex + ey * ey + ez * ez) * scale;
  }

  const RendererCoreSpotLight* bagLight = nullptr;
  if (wantsLightPick) {
    bagLight = rendererCore->pickDynLight(worldCenter, worldRadius,
                                          bag->info->dynLightSkipSlot);
  }
  // Modified by TyraX: a bag may opt out of the camera spot as well
  // (PipelineInfoBag::spotLit - the terrain, whose light comes from the
  // flashlight's projected pool instead). A null bagLight means "the global
  // flashlight", so opting out needs a light OBJECT rather than a null: the
  // one below is off and black, which the programs compute with as a no-op.
  if (!bag->info->spotLit) bagLight = &kNoSpotLight;
  qbufferRenderer.setBagLight(bagLight);
  TYRA_ATTRIB_ADD(prepLightTicks, attribLightStart);

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
  TYRA_ATTRIB_MARK(attribBlssStart);
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
  TYRA_ATTRIB_ADD(prepBlssTicks, attribBlssStart);

  TYRA_ATTRIB_MARK(attribObjectDataStart);
  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);

  qbufferRenderer.setClipperMVP(&mvp);

  qbufferRenderer.setInfo(bag->info);
  TYRA_ATTRIB_ADD(prepObjectDataTicks, attribObjectDataStart);

  if (telemetryEnabled) telemetry.prepareTicks += readCoreTelemetryTicks()-prepareStart;
  HardwareTrace::Scope traceDispatch("Dispatch");
  const u32 dispatchStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;
  // Modified by TyraX: retained command data. Opened here rather than at the
  // top of render(), because the prim state is part of the key and setInfo()
  // above is what finished writing it. `retainBag` false means every package
  // below is built the way it always was.
  TYRA_ATTRIB_MARK(attribRetainStart);
  const bool retainBag = qbufferRenderer.beginRetainedBag(bag, maxVertCount);
  TYRA_ATTRIB_ADD(dsRetainTicks, attribRetainStart);
  retainCurrentBag = retainBag;
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
    // The whole-bag bbox already proved every range visible. Point qbuffers
    // straight at the bag streams instead of filling pooled package records
    // whose classification result this branch ignores.
    //
    // NOTE for anyone reading the "package creation and classification"
    // residual: this route calls neither the packager nor checkFrustum, so
    // for these bags that residual is THIS loop and nothing else.
    TYRA_ATTRIB_MARK(attribDirectStart);
    TYRA_ATTRIB_INC(dsDirectBags);
    u16 packageIndex = 0;
    for (u32 offset = 0; offset < bag->count;
         offset += maxVertCount, ++packageIndex) {
      const u32 remaining = bag->count - offset;
      const u32 count = remaining < maxVertCount ? remaining : maxVertCount;
      Verbose(packageIndex, " package - direct cull by data pointer");
      if (telemetryEnabled) {
        ++telemetry.packagesCull;
        telemetry.trianglesCull +=
            bag->stripped ? (count >= 2 ? count - 2 : 0) : count / 3;
        if (bag->stripped) ++telemetry.packagesStrip;
      }
      auto buffer = qbufferRenderer.getBuffer();
      buffer->fillByPointer(bag, offset, count);
      // Modified by TyraX: this package's slice of the bag is fixed, so its
      // command block is too - see StaPipRetainedCommands.
      if (retainBag) buffer->retainIndex = static_cast<int>(packageIndex);
      qbufferRenderer.cull(buffer);
    }
    TYRA_ATTRIB_ADD(dsDirectTicks, attribDirectStart);
  } else if (checkYesFrustumPartialClipYes || checkYesFrustumPartialClipNo) {
    TYRA_ATTRIB_INC(dsPartialBags);
    u16 packagesCount = 0;
    auto doClip = checkYesFrustumPartialClipYes;
    if (bag->stripped) {
      // Modified by TyraX: packages ARE the baked strip runs here, so the
      // subpackage branch below (which cuts at clipPackageSize, and merges
      // three cuts back into one buffer) must not be reached - either would
      // splice unrelated vertices into one strip.
      TYRA_ATTRIB_MARK(attribCreateStart);
      auto packages = packager.create(&packagesCount, bag, maxVertCount);
      TYRA_ATTRIB_ADD(dsCreateTicks, attribCreateStart);
      Verbose("Material - partial, stripped. Packages: ", packagesCount);
      TYRA_ATTRIB_MARK(attribRenderStripStart);
      renderStrippedPkgs(packages, doClip, packagesCount);
      TYRA_ATTRIB_ADD(dsRenderTicks, attribRenderStripStart);
    } else if (!doClip || bag->count >= maxVertCount * 2) {
      TYRA_ATTRIB_MARK(attribCreateStart);
      auto packages = packager.create(&packagesCount, bag, maxVertCount);
      TYRA_ATTRIB_ADD(dsCreateTicks, attribCreateStart);
      Verbose("Material - partial. Packages: ", packagesCount);
      TYRA_ATTRIB_MARK(attribRenderPkgsStart);
      renderPkgs(packages, doClip, packagesCount);
      TYRA_ATTRIB_ADD(dsRenderTicks, attribRenderPkgsStart);
    } else {
      TYRA_ATTRIB_MARK(attribCreateStart);
      auto subpkgs = packager.create(&packagesCount, bag, clipPackageSize());
      TYRA_ATTRIB_ADD(dsCreateTicks, attribCreateStart);
      Verbose("Material - partial. Subpackages: ", packagesCount);
      TYRA_ATTRIB_MARK(attribRenderSubStart);
      renderSubpkgs(subpkgs, packagesCount);
      TYRA_ATTRIB_ADD(dsRenderTicks, attribRenderSubStart);
    }
  }

  TYRA_ATTRIB_MARK(attribFlushStart);
  qbufferRenderer.flushBuffers();
  TYRA_ATTRIB_ADD(dsFlushTicks, attribFlushStart);
  qbufferRenderer.endRetainedBag();  // Modified by TyraX
  retainCurrentBag = false;
  const u32 dispatchEnd = telemetryEnabled ? readCoreTelemetryTicks() : 0;
  if (telemetryEnabled) telemetry.dispatchTicks += dispatchEnd - dispatchStart;

  if (clampedBag) {  // Modified by TyraX: restore the frame's REPEAT contract
    rendererCore->sync.align3D();
    rendererCore->gs.setTextureWrap(RendererCoreGS::repeatWrap());
  }

  Verbose("Render finished");
  // Added by TyraX: one read closes both the tail and the whole function, so
  // the deepest bracket costs the same clock as the shallowest.
#if TYRA_STAPIP_ATTRIB
  if (telemetryEnabled) {
    const u32 attribEnd = readCoreTelemetryTicks();
    telemetry.attrib.tailTicks += attribEnd - dispatchEnd;
    telemetry.attrib.renderTicks += attribEnd - attribRenderStart;
  }
#endif
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
      // Modified by TyraX: these packages ARE the bag's fixed maxVertCount
      // slices - the packager cuts at i * size - so package i's command block
      // is the same one the wholly-visible route would build for it.
      if (retainCurrentBag) buffer->retainIndex = static_cast<int>(i);
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

// Modified by TyraX: see the header. Three routes, and only the third is new.
void StaPipCore::renderStrippedPkgs(StaPipBagPackage* packages,
                                    const bool& doClip, u16 count) {
  // The expansion is 3x, so a chunk carries the same TRIANGLE budget a list
  // subpackage does - clipPackageSize vertices' worth.
  const u32 trisPerChunk = clipPackageSize() / 3;

  for (u16 i = 0; i < count; i++) {
    const bool guardBandOnly = doClip && isGuardBandOnly(packages[i]);
    const bool cull = (doClip && packages[i].isInFrustum == IN_FRUSTUM) ||
                      !doClip || guardBandOnly;

    if (cull) {
      recordPackage(packages[i], IN_FRUSTUM);
      if (guardBandOnly) recordGuardBandPackage(packages[i]);
      if (telemetryEnabled) ++telemetry.packagesStrip;
      auto buffer = qbufferRenderer.getBuffer();
      buffer->fillByPointer(packages[i]);
      // Modified by TyraX: a strip RUN is a package, and the runs are baked -
      // so a stripped bag's retained blocks are exactly as stable as a list
      // bag's. Only the expansion branch below is per-frame.
      if (retainCurrentBag) buffer->retainIndex = static_cast<int>(i);
      qbufferRenderer.cull(buffer);
    } else if (packages[i].isInFrustum == PARTIALLY_IN_FRUSTUM) {
      // The package genuinely crosses a VU clip plane. `clip_*` and the EE
      // clipper are both per-triangle over a triangle LIST, so the strip is
      // expanded back into one on the EE - into the qbuffer copy pool, whose
      // double-buffered lifetime the DMA already relies on - and the ordinary
      // clip route runs unchanged. This costs 3x the vertices of the strip it
      // replaces, which is exactly what the list path would have sent for the
      // same triangles: the strip is a saving on the packages that do NOT need
      // cutting, and the guard band means that is nearly all of them.
      recordPackage(packages[i], PARTIALLY_IN_FRUSTUM);
      if (packages[i].size < 3 || trisPerChunk == 0) continue;
      if (telemetryEnabled) ++telemetry.packagesStripExpanded;
      const u32 triangles = packages[i].size - 2;
      for (u32 t = 0; t < triangles; t += trisPerChunk) {
        const u32 remaining = triangles - t;
        const u32 n = remaining < trisPerChunk ? remaining : trisPerChunk;
        auto buffer = qbufferRenderer.getBuffer();
        buffer->fillByStripExpand(packages[i], t, n);
        qbufferRenderer.clip(buffer);
      }
    } else {
      recordPackage(packages[i], OUTSIDE_FRUSTUM);
    }
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
    // Modified by TyraX: VU1 clips into its own scratch/output memory and
    // never modifies the EE input. Keep DMA references just like the cull
    // path; only the legacy EE clipper needs a writable qbuffer copy.
    if (qbufferRenderer.isVU1ClippingEnabled())
      buffer->fillByPointer(subpkgs[i]);
    else
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

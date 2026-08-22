/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Modified by TyraX: PipelineZTest_TestOnly branch in sendObjectData;
// per-mesh object-space spot light (flashlight) upload for the color VU1
// programs + EE clipper; alpha-test AFAIL fixed to ATEST_KEEP_ALL (see
// sendObjectData) so cutout textures stop stamping the z buffer.

#include <math.h>
#include "renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp"
#include "renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h"
#include "packet2/packet2_tyra_utils.hpp"
#include "renderer/3d/pipeline/static/core/stapip_vu_tap.hpp"

// #define TYRA_QBUFF_RENDERER_VERBOSE_LOG 1

#ifdef TYRA_QBUFF_RENDERER_VERBOSE_LOG
#define Verbose(...) TyraDebug::writeLines("VRB: ", ##__VA_ARGS__, "\n")
#else
#define Verbose(...) ((void)0)
#endif

namespace Tyra {

static inline u32 readTelemetryTicks() {
  u32 result;
  asm volatile("mfc0 %0, $9" : "=r"(result));
  return result;
}

/**
 * VU1 = 1000 vert
 *
 * Quadbuffering:
 * 2 main buffers = 1000 / 2 = 500 vert
 * 2 kick buffers =  500 / 2 = 250 vert
 *
 * Vert data:
 * Pos + Normal + ST + Color = 4
 * = 4 * 48 = 192
 *
 * Other data:
 * mvp matrix, light matrix, tags = 14
 * 20 light vectors, light intesities = 25
 * = 14 + 25 = 39
 *
 * All data:
 * = 192 + 39 = 231
 *
 */

const u16 StaPipQBufferRenderer::buffersCount = 32;

StaPipQBufferRenderer::StaPipQBufferRenderer() {
  currentBufferIndex = 0;
  nextBufferIndex = 0;
  context = 0;
  lastProgramName = StaPipUndefinedProgram;

  qbuffersPacketSize = 4 * buffersCount;
  programsPacket = nullptr;
  billboardProgramsPacket = nullptr;
}

void StaPipQBufferRenderer::allocateOnUse() {
  staticDataPacket = packet2_create(3, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  // Modified by TyraX: +6 qwords for the spot light unpack, +16 for the
  // VU1 clipping constants + plane table.
  // Modified by TyraX: 42 -> 48 - the per-mesh ALPHA qword and the env
  // (matcap) camera basis added two unpack blocks to sendObjectData.
  // Modified by TyraX: 48 -> 52 - the billboard basis unpack (2 qwords
  // + headers).
  objectDataPacket = packet2_create(52, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);

  packets = new packet2_t*[2];
  for (u16 i = 0; i < 2; i++)
    packets[i] =
        packet2_create(qbuffersPacketSize, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);

  buffers = new StaPipQBuffer*[buffersCount];
  for (u16 i = 0; i < buffersCount; i++) {
    buffers[i] = new StaPipQBuffer();
  }

  dBufferPrograms = new StaPipVU1Program*[buffersCount];

  sendStaticData();
}

void StaPipQBufferRenderer::deallocateOnUse() {
  packet2_free(staticDataPacket);
  packet2_free(objectDataPacket);

  for (u16 i = 0; i < 2; i++) packet2_free(packets[i]);
  delete[] packets;

  for (u16 i = 0; i < buffersCount; i++) delete buffers[i];
  delete[] buffers;

  delete[] dBufferPrograms;
}

StaPipQBufferRenderer::~StaPipQBufferRenderer() {
  if (programsPacket) packet2_free(programsPacket);
  if (billboardProgramsPacket) packet2_free(billboardProgramsPacket);
}

void StaPipQBufferRenderer::init(RendererCore* t_core, prim_t* t_prim,
                                 lod_t* t_lod) {
  path1 = t_core->getPath1();
  clipper.init(t_core->getSettings());
  rendererCore = t_core;
  prim = t_prim;
  lod = t_lod;

  // Modified by TyraX: the clip-space planes the VU1 clip programs cut
  // against - the same ones the EE clipper uses (see PlanesClipAlgorithm::init;
  // clipMargin must be set before setRenderer, like the EE path requires).
  clipNearZ =
      t_core->getSettings().getNear() - (-PlanesClipAlgorithm::clipMargin);
  clipFarZ = -t_core->getSettings().getFar();

  dma_channel_initialize(DMA_CHANNEL_VIF1, nullptr, 0);

  setProgramsCache();

  reinitVU1();

  TYRA_LOG("StaPipQBufferRenderer initialized");
}

void StaPipQBufferRenderer::reinitVU1() {
  uploadPrograms();
  setDoubleBuffer();
}

namespace {
// Inverse of an affine model matrix (rotation * scale + translation).
// M4x4 is column-major: data[0..3] = X basis, [4..7] = Y, [8..11] = Z,
// [12..14] = translation. inv3x3 comes out row-major.
void invertAffine(const float* m, float* inv3x3, float* invT) {
  const float a = m[0], b = m[4], c = m[8];
  const float d = m[1], e = m[5], f = m[9];
  const float g = m[2], h = m[6], i = m[10];
  float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (det > -1e-12F && det < 1e-12F) det = 1e-12F;
  const float id = 1.0F / det;
  inv3x3[0] = (e * i - f * h) * id;
  inv3x3[1] = (c * h - b * i) * id;
  inv3x3[2] = (b * f - c * e) * id;
  inv3x3[3] = (f * g - d * i) * id;
  inv3x3[4] = (a * i - c * g) * id;
  inv3x3[5] = (c * d - a * f) * id;
  inv3x3[6] = (d * h - e * g) * id;
  inv3x3[7] = (b * g - a * h) * id;
  inv3x3[8] = (a * e - b * d) * id;
  invT[0] = -(inv3x3[0] * m[12] + inv3x3[1] * m[13] + inv3x3[2] * m[14]);
  invT[1] = -(inv3x3[3] * m[12] + inv3x3[4] * m[13] + inv3x3[5] * m[14]);
  invT[2] = -(inv3x3[6] * m[12] + inv3x3[7] * m[13] + inv3x3[8] * m[14]);
}

// Builds the object-space spot light for this mesh: transforms the world
// light through the inverse model matrix, expresses the range in object
// units via the (assumed near-uniform) mesh scale, and precomputes the
// constants the cull VU1 programs and the EE clipper both consume.
StaPipClipperSpot buildSpotForBag(const RendererCoreSpotLight& spot,
                                  const M4x4* model) {
  StaPipClipperSpot out;
  out.enabled = spot.enabled;
  if (!spot.enabled) return out;

  float inv[9], invT[3];
  invertAffine(model->data, inv, invT);

  out.position.x = inv[0] * spot.position.x + inv[1] * spot.position.y +
                   inv[2] * spot.position.z + invT[0];
  out.position.y = inv[3] * spot.position.x + inv[4] * spot.position.y +
                   inv[5] * spot.position.z + invT[1];
  out.position.z = inv[6] * spot.position.x + inv[7] * spot.position.y +
                   inv[8] * spot.position.z + invT[2];
  out.position.w = 1.0F;

  Vec4 dir;
  dir.x = inv[0] * spot.direction.x + inv[1] * spot.direction.y +
          inv[2] * spot.direction.z;
  dir.y = inv[3] * spot.direction.x + inv[4] * spot.direction.y +
          inv[5] * spot.direction.z;
  dir.z = inv[6] * spot.direction.x + inv[7] * spot.direction.y +
          inv[8] * spot.direction.z;
  float dirLen2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
  if (dirLen2 < 1e-10F) dirLen2 = 1.0F;

  // A world direction through the inverse scales by 1/s (uniform scale s),
  // so |dir|^2 = 1/s^2 - reuse it to express the range in object units.
  const float objRange2 = spot.range * spot.range * dirLen2;

  const float invDirLen = 1.0F / sqrtf(dirLen2);
  out.direction.x = dir.x * invDirLen;
  out.direction.y = dir.y * invDirLen;
  out.direction.z = dir.z * invDirLen;
  out.direction.w = 0.0F;

  out.color[0] = spot.color.r;
  out.color[1] = spot.color.g;
  out.color[2] = spot.color.b;
  out.invRange2 = 1.0F / objRange2;

  // Point (omni) light through the SAME spot constants: zero direction makes
  // the axial term t = max(0, d.dir) collapse to 0, so the cone factor
  // becomes (0 - cosCut2*dist2)*invSoft = dist2 * invSoft with cosCut2 = -1.
  // invSoft is sized to saturate that to 1 within ~1% of the range - the
  // radial falloff alone shapes the light. No VU1 change, no micro memory.
  if (spot.point) {
    out.direction.x = 0.0F;
    out.direction.y = 0.0F;
    out.direction.z = 0.0F;
    out.cosCut2 = -1.0F;
    out.invSoft = 1.0e4F / objRange2;
    return out;
  }

  out.cosCut2 = spot.cosCutoff * spot.cosCutoff;
  // The VU1/EE cone term is clamp01((t^2 - cosCut2*dist2) * invSoft). Its
  // SIGN is the exact angular cutoff and is distance-independent, but its
  // MAGNITUDE scales with dist2 - so sizing invSoft off the full range
  // (objRange2 * (1 - cosCut2)) made the beam ramp up across the whole
  // range: dim on everything close to the lamp, fully bright only near its
  // far end. On the camera flashlight that reads as "it doesn't light what
  // I'm looking at". Saturate at a fraction of the range instead; the
  // cutoff ANGLE is unchanged, the edge just gets crisper.
  constexpr float kFullBrightAt = 0.18F;  // of the range, on the axis
  const float coneBase =
      objRange2 * kFullBrightAt * kFullBrightAt * (1.0F - out.cosCut2);
  out.invSoft = coneBase > 1e-10F ? spot.softness / coneBase : 0.0F;
  return out;
}
}  // namespace

void StaPipQBufferRenderer::sendObjectData(
    StaPipBag* bag, M4x4* mvp, RendererCoreTextureBuffers* texBuffers) {
  packet2_reset(objectDataPacket, false);
  packet2_utils_vu_add_unpack_data(objectDataPacket, VU1_MVP_MATRIX_ADDR,
                                   mvp->data, 4, false);

  if (bag->lighting) {
    packet2_utils_vu_add_unpack_data(objectDataPacket, VU1_LIGHTS_MATRIX_ADDR,
                                     bag->lighting->lightMatrix, 3, false);

    packet2_utils_vu_add_unpack_data(
        objectDataPacket, VU1_LIGHTS_DIRS_ADDR,
        bag->lighting->dirLights->getLightDirections(), 3, false);

    packet2_utils_vu_add_unpack_data(objectDataPacket, VU1_LIGHTS_COLORS_ADDR,
                                     bag->lighting->dirLights->getLightColors(),
                                     4, false);
  }

  // Modified by TyraX: dynamic light for the color programs - the per-bag
  // pick from StaPipCore (flashlight or the strongest scene point light),
  // falling back to the global flashlight state when no pick was made.
  // The dir-lights addresses are free when the bag has no lighting - the
  // C/TC programs read the three spot quads from there. Always uploaded
  // (the programs always compute; a zero color makes it a no-op) and the
  // same numbers go to the EE clipper for the as_is path.
  if (!bag->lighting) {
    const auto& light = bagLight ? *bagLight : rendererCore->spot;
    const auto meshSpot = buildSpotForBag(light, bag->info->model);
    clipper.setSpot(meshSpot);

    packet2_utils_vu_open_unpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, false);
    {
      packet2_add_float(objectDataPacket, meshSpot.position.x);
      packet2_add_float(objectDataPacket, meshSpot.position.y);
      packet2_add_float(objectDataPacket, meshSpot.position.z);
      packet2_add_float(objectDataPacket, meshSpot.invRange2);
      packet2_add_float(objectDataPacket, meshSpot.direction.x);
      packet2_add_float(objectDataPacket, meshSpot.direction.y);
      packet2_add_float(objectDataPacket, meshSpot.direction.z);
      packet2_add_float(objectDataPacket, meshSpot.cosCut2);
      packet2_add_float(objectDataPacket, meshSpot.enabled ? meshSpot.color[0]
                                                           : 0.0F);
      packet2_add_float(objectDataPacket, meshSpot.enabled ? meshSpot.color[1]
                                                           : 0.0F);
      packet2_add_float(objectDataPacket, meshSpot.enabled ? meshSpot.color[2]
                                                           : 0.0F);
      packet2_add_float(objectDataPacket, meshSpot.invSoft);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);

    // Modified by TyraX: the two quadwords a project's own microprogram reads
    // (docs/vu-authoring.md). Inside the `if (!bag->lighting)` on purpose -
    // they occupy the directional-lights COLOUR block, which a lit bag needs.
    if (vuCustomEnabled) {
      packet2_utils_vu_open_unpack(objectDataPacket, VU1_CUSTOM_PARAMS_ADDR,
                                   false);
      {
        for (u32 i = 0; i < 4; i++)
          packet2_add_float(objectDataPacket, vuParams[i]);
        for (u32 i = 0; i < 4; i++)
          packet2_add_float(objectDataPacket, vuTime[i]);
      }
      packet2_utils_vu_close_unpack(objectDataPacket);
    }
  }

  // Modified by TyraX: VU1 clipping data. One quad of constants for the
  // per-triangle crossing test (see stapip_vu1_shared_defines.h) and the six
  // clip planes as (A,B,C,D)+(E,0,0,0) pairs; inside = dot4(v,ABCD) + E >= 0.
  // Uploaded per mesh: other pipelines may reuse this VU1 memory in between.
  if (vu1Clipping) {
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_CLIP_CONSTS_ADDR, false);
    {
      packet2_add_float(objectDataPacket, clipNearZ - VU1_CLIP_GUARD);
      packet2_add_float(objectDataPacket, -clipFarZ - VU1_CLIP_GUARD);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, VU1_CLIP_GUARD);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);

    const float planes[6][8] = {
        // near: z <= clipNearZ (exact, matches the EE clipper)
        {0.0F, 0.0F, -1.0F, 0.0F, clipNearZ, 0.0F, 0.0F, 0.0F},
        // far: z >= clipFarZ (exact, matches the EE clipper)
        {0.0F, 0.0F, 1.0F, 0.0F, -clipFarZ, 0.0F, 0.0F, 0.0F},
        // guard band X/Y at +/-VU1_CLIP_XY_BAND * w - strictly inside the
        // GS raster window; the scissor trims the rest of the way
        {-1.0F, 0.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, -1.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, VU1_CLIP_XY_BAND, 0.0F, 0.0F, 0.0F, 0.0F},
    };
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_CLIP_PLANES_ADDR, false);
    {
      for (u32 i = 0; i < 6; i++)
        for (u32 j = 0; j < 8; j++)
          packet2_add_float(objectDataPacket, planes[i][j]);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  u8 singleColorEnabled = bag->color->single != nullptr;

  if (singleColorEnabled)  // Color is placed in 4th slot of
                           // VU1_LIGHTS_MATRIX_ADDR
    Packet2TyraUtils::addUnpackData(objectDataPacket, VU1_SINGLE_COLOR_ADDR,
                                    bag->color->single->rgba, 1, false);

  packet2_utils_vu_open_unpack(objectDataPacket, VU1_OPTIONS_ADDR, false);
  {
    const u32 sharedClipVariant =
        bag->lighting != nullptr ||
                (bag->texture != nullptr && bag->texture->coordinatesAreNormals)
            ? 1
            : 0;
    packet2_add_u32(objectDataPacket,
                    singleColorEnabled);   // Single color enabled.
    // Static-pipeline-only use of the old dynpip lerp lane: C/D and TC/TCE
    // share one clip image per ABI-compatible pair. Other programs ignore it.
    packet2_add_u32(objectDataPacket, sharedClipVariant);
    // Modified by TyraX: GS hardware fog params (see RendererCoreFog)
    packet2_add_float(objectDataPacket, rendererCore->fog.scale);
    packet2_add_float(objectDataPacket, rendererCore->fog.offset);

    packet2_utils_gs_add_lod(objectDataPacket, lod);

    // Modified by TyraX: the destination-alpha gate (PipelineInfoBag::
    // dateLit) rides the same in-band TEST qword every mesh already emits -
    // DATE = 1 draws this bag's pixels only where the framebuffer alpha's
    // MSB is 0, which is how the flashlight's shadow volumes mask its light.
    const int date = bag->info->dateLit ? 1 : 0;
    if (bag->info->zTestType == PipelineZTest_AllPass) {
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(0, 0, 0, 0, date, 0, 0, ZTEST_METHOD_ALLPASS),
          GS_REG_TEST);
    } else if (bag->info->zTestType == PipelineZTest_TestOnly) {
      // Depth-tested, no z write: alpha test fails every pixel and AFAIL
      // keeps the z buffer (GS FB_ONLY - color still written). The ZBUF
      // register (and thus the VU1 options layout) stays untouched.
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_ALLFAIL, 0x00,
                      ATEST_KEEP_ZBUFFER, date, DRAW_DISABLE,
                      DRAW_ENABLE, rendererCore->gs.zBuffer.method),
          GS_REG_TEST);
    } else {
      // Cutout alpha: texels with alpha 0 fail the test and must write
      // NOTHING. Upstream passed ATEST_KEEP_FRAMEBUFFER, whose ps2sdk name
      // reads backwards - it is AFAIL=ZB_ONLY (2), "keep the framebuffer,
      // update z". So every transparent texel stamped the z buffer while
      // drawing no colour, and the invisible part of an alpha-cutout card
      // (foliage, decals, grates) occluded whatever was drawn behind it
      // later. ATEST_KEEP_ALL (0) leaves both buffers alone, which is what a
      // cutout means. Opaque geometry carries alpha 0x80 and never fails the
      // test, so nothing else changes.
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_NOTEQUAL, 0x00, ATEST_KEEP_ALL,
                      date, DRAW_DISABLE, DRAW_ENABLE,
                      rendererCore->gs.zBuffer.method),
          GS_REG_TEST);
    }

    if (texBuffers != nullptr) {
      rendererCore->texture.updateClutBuffer(texBuffers->clut);

      packet2_utils_gs_add_texbuff_clut(objectDataPacket, texBuffers->core,
                                        &rendererCore->texture.clut);
    }
  }
  packet2_utils_vu_close_unpack(objectDataPacket);

  // Modified by TyraX: particle billboard camera basis (right, up - world
  // space). Reuses the lights-matrix area like the env basis; billboard
  // bags never carry lighting (asserted in StaPipCore::render). Swapping
  // this basis and re-rendering the same bag draws the same centers for
  // another view (e.g. a portal's virtual camera).
  if (bag->billboard != nullptr) {
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_BILLBOARD_BASIS_ADDR,
                                 false);
    {
      packet2_add_float(objectDataPacket, bag->billboard->right.x);
      packet2_add_float(objectDataPacket, bag->billboard->right.y);
      packet2_add_float(objectDataPacket, bag->billboard->right.z);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, bag->billboard->up.x);
      packet2_add_float(objectDataPacket, bag->billboard->up.y);
      packet2_add_float(objectDataPacket, bag->billboard->up.z);
      packet2_add_float(objectDataPacket, 0.0F);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  // Modified by TyraX: env (matcap) camera basis for the TCE programs.
  // Transpose it and fold in the ST scale here so CalculateTyraEnvStq can
  // evaluate both scaled dot products in one VU1 accumulator chain. Reuses
  // the lights-matrix area; env bags never carry lighting.
  if (bag->texture != nullptr && bag->texture->coordinatesAreNormals) {
    const Vec4& r = bag->texture->envRight;
    const Vec4& u = bag->texture->envUp;
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_ENV_BASIS_ADDR, false);
    {
      packet2_add_float(objectDataPacket, r.x * 0.5F);
      packet2_add_float(objectDataPacket, u.x * -0.5F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, r.y * 0.5F);
      packet2_add_float(objectDataPacket, u.y * -0.5F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, 0.0F);
      packet2_add_float(objectDataPacket, r.z * 0.5F);
      packet2_add_float(objectDataPacket, u.z * -0.5F);
      packet2_add_float(objectDataPacket, 1.0F);
      packet2_add_float(objectDataPacket, 0.5F);
    }
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  // Modified by TyraX: per-mesh GS blend equation, emitted in-band with the
  // other tags by every program (StoreTyraGifTags*Alpha) - no FINISH barrier
  // to switch it. Default alpha-over; the reflective-material env pass sets
  // additiveBlendFix for Cv = Cs*FIX/128 + Cd.
  {
    const u8 fix = bag->info->additiveBlendFix;
    const u8 sub = bag->info->subtractiveBlendFix;
    // Subtractive wins over additive when both are set: (0 - Cs)*FIX + Cd,
    // clamped at 0 - the shadow volumes' count-down pass.
    packet2_utils_vu_open_unpack(objectDataPacket, VU1_ALPHA_ADDR, false);
    packet2_add_2x_s64(objectDataPacket,
                       sub != 0   ? GS_SET_ALPHA(2, 0, 2, 1, sub)
                       : fix != 0 ? GS_SET_ALPHA(0, 2, 2, 1, fix)
                                  : GS_SET_ALPHA(0, 1, 0, 1, 0),
                       GS_REG_ALPHA);
    packet2_utils_vu_close_unpack(objectDataPacket);
  }

  packet2_utils_vu_add_end_tag(objectDataPacket);
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(objectDataPacket, DMA_CHANNEL_VIF1, true);
}

void StaPipQBufferRenderer::setInfo(PipelineInfoBag* bag) {
  prim->antialiasing = bag->antiAliasingEnabled;
  prim->blending = bag->blendingEnabled;
  prim->shading = bag->shadingType;

  if (bag->textureMappingType == TyraLinear) {
    lod->mag_filter = LOD_MAG_LINEAR;
    lod->min_filter = LOD_MIN_LINEAR;
  } else {
    lod->mag_filter = LOD_MAG_NEAREST;
    lod->min_filter = LOD_MIN_NEAREST;
  }
}

void StaPipQBufferRenderer::sendStaticData() const {
  packet2_reset(staticDataPacket, false);
  packet2_utils_vu_open_unpack(staticDataPacket, VU1_SET_GIFTAG_ADDR, false);
  { packet2_utils_gif_add_set(staticDataPacket, 1); }
  packet2_utils_vu_close_unpack(staticDataPacket);

  packet2_utils_vu_add_end_tag(staticDataPacket);
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(staticDataPacket, DMA_CHANNEL_VIF1, true);
}

void StaPipQBufferRenderer::setProgramsCache() {
  // Modified by TyraX: in VU1 clipping mode the clip programs replace
  // the as_is family (both plus cull would overflow VU1 micro memory, and
  // as_is is only ever fed by the retired EE clipper path).
  // The env (matcap) variants ride along in both sets: cull_tce +
  // as_is_tce with the EE clipper, cull_tce + clip_tce in VU1-clipping
  // mode. The clip family uses three resident images: C/D share the C image,
  // TC/TCE share TC, and TD stays specialised. Their input/scratch/output ABIs
  // match within each pair; VU1_OPTIONS_ADDR.y selects only the per-corner
  // shading path. Path1 aliases identical source ranges to one destination.
  // Check the exact micro-memory budget with nm after touching any program -
  // the createProgramsCache overflow assert is compiled out in release.
  // Modified by TyraX: the set is DATA-DRIVEN now (setResidentClasses). Each
  // material class contributes a pair - the cull program plus its clipped or
  // as_is twin - and a class the project never draws can be dropped to buy
  // micro memory for a program the user wrote. Every class is kept unless a
  // game says otherwise, so this is the old hardcoded ten by default.
  VU1Program* programs[10];
  u32 count = 0;
  const struct {
    u32 bit;
    StaPipProgramName cull, clipped, asIs;
  } classes[] = {
      {StaPipClassColor, StaPipCullColor, StaPipClipColor, StaPipAsIsColor},
      {StaPipClassDirLights, StaPipCullDirLights, StaPipClipDirLights,
       StaPipAsIsDirLights},
      {StaPipClassTextureDirLights, StaPipCullTextureDirLights,
       StaPipClipTextureDirLights, StaPipAsIsTextureDirLights},
      {StaPipClassTextureColor, StaPipCullTextureColor, StaPipClipTextureColor,
       StaPipAsIsTextureColor},
      {StaPipClassTextureEnv, StaPipCullTextureEnv, StaPipClipTextureEnv,
       StaPipAsIsTextureEnv},
  };
  for (const auto& c : classes) {
    if ((residentClasses & c.bit) == 0) continue;
    programs[count++] = repository.getProgram(c.cull);
    programs[count++] =
        repository.getProgram(vu1Clipping ? c.clipped : c.asIs);
  }
  programsPacket = path1->createProgramsCache(programs, count, 0);

  // Modified by TyraX: the billboard family lives in its own small packet,
  // swapped in on demand (ensureProgramSet). Built once; independent of the
  // clipping mode, even though shared clip images now leave useful headroom.
  if (billboardProgramsPacket == nullptr) {
    VU1Program* billboardPrograms[2];
    billboardPrograms[0] = repository.getProgram(StaPipBillboardColor);
    billboardPrograms[1] = repository.getProgram(StaPipBillboardTexture);
    billboardProgramsPacket = path1->createProgramsCache(billboardPrograms, 2, 0);
  }
}

// TyraX addition: see the header. Safe to call at run time - a level that stops
// needing a material class can hand its micro memory to something else - but it
// is a full pipeline drain plus an upload, so it belongs at a zone or level
// boundary, never per bag.
void StaPipQBufferRenderer::setResidentClasses(const u32& mask) {
  const u32 wanted = mask | StaPipClassColor;  // colour is the fallback floor
  if (wanted == residentClasses) return;
  residentClasses = wanted;
  if (programsPacket == nullptr) return;  // init() will build the right set

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

// TyraX addition: which material class a program name belongs to, so residency
// can be tested before anything is substituted.
static u32 classOfProgram(const StaPipProgramName& name) {
  switch (name) {
    case StaPipCullColor:
    case StaPipClipColor:
    case StaPipAsIsColor:
      return StaPipQBufferRenderer::StaPipClassColor;
    case StaPipCullDirLights:
    case StaPipClipDirLights:
    case StaPipAsIsDirLights:
      return StaPipQBufferRenderer::StaPipClassDirLights;
    case StaPipCullTextureDirLights:
    case StaPipClipTextureDirLights:
    case StaPipAsIsTextureDirLights:
      return StaPipQBufferRenderer::StaPipClassTextureDirLights;
    case StaPipCullTextureColor:
    case StaPipClipTextureColor:
    case StaPipAsIsTextureColor:
      return StaPipQBufferRenderer::StaPipClassTextureColor;
    case StaPipCullTextureEnv:
    case StaPipClipTextureEnv:
    case StaPipAsIsTextureEnv:
      return StaPipQBufferRenderer::StaPipClassTextureEnv;
    default:
      return 0;  // billboards and anything else: not class-managed
  }
}

// TyraX addition: see the header.
StaPipProgramName StaPipQBufferRenderer::residentFallback(
    const StaPipProgramName& name) const {
  const u32 cls = classOfProgram(name);
  // Not class-managed, or its class is resident: nothing to substitute.
  if (cls == 0 || (residentClasses & cls) != 0) return name;

  switch (name) {
    case StaPipCullTextureDirLights:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipCullTextureColor
                 : StaPipCullColor;
    case StaPipClipTextureDirLights:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipClipTextureColor
                 : StaPipClipColor;
    case StaPipAsIsTextureDirLights:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipAsIsTextureColor
                 : StaPipAsIsColor;
    case StaPipCullTextureEnv:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipCullTextureColor
                 : StaPipCullColor;
    case StaPipClipTextureEnv:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipClipTextureColor
                 : StaPipClipColor;
    case StaPipAsIsTextureEnv:
      return (residentClasses & StaPipClassTextureColor)
                 ? StaPipAsIsTextureColor
                 : StaPipAsIsColor;
    case StaPipCullDirLights:
    case StaPipCullTextureColor:
      return StaPipCullColor;
    case StaPipClipDirLights:
    case StaPipClipTextureColor:
      return StaPipClipColor;
    case StaPipAsIsDirLights:
    case StaPipAsIsTextureColor:
      return StaPipAsIsColor;
    default:
      return name;
  }
}

// TyraX addition: see the header. sinf/cosf on the EE once per call is nothing
// next to what the same series costs three times per vertex on VU1.
void StaPipQBufferRenderer::setVuTime(const float& seconds) {
  vuTime[0] = seconds;
  vuTime[1] = sinf(seconds);
  vuTime[2] = cosf(seconds);
  vuTime[3] = 1.0F;
}

void StaPipQBufferRenderer::setProgramOverride(const StaPipProgramName& name,
                                               StaPipVU1Program* program) {
  repository.setOverride(name, program);
  if (programsPacket == nullptr) return;  // init() will pick it up

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

// TyraX addition: see the header. Same work as setProgramOverride, once.
void StaPipQBufferRenderer::setProgramOverrides(
    const StaPipProgramName* names, StaPipVU1Program* const* programs,
    u32 count) {
  for (u32 i = 0; i < count; i++) repository.setOverride(names[i], programs[i]);
  if (programsPacket == nullptr) return;  // init() will pick them up

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

void StaPipQBufferRenderer::setVU1Clipping(const bool& enabled) {
  if (vu1Clipping == enabled) return;
  vu1Clipping = enabled;

  if (programsPacket == nullptr) return;  // init() will build the right set

  packet2_free(programsPacket);
  setProgramsCache();
  uploadPrograms();
  clearLastProgramName();
}

void StaPipQBufferRenderer::uploadPrograms() {
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(programsPacket, DMA_CHANNEL_VIF1, true);
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  billboardSetActive = false;  // Modified by TyraX: main set is resident now
}

// Modified by TyraX: swap between the resident program set and the
// billboard set (both packets are prebuilt - this is one VIF1 MPG upload,
// the VIF stalls it until VU1 halts, so it is safe mid-frame).
void StaPipQBufferRenderer::ensureProgramSet(const bool& billboard) {
  if (billboardSetActive == billboard) return;
  billboardSetActive = billboard;

  const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;

  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(
      billboard ? billboardProgramsPacket : programsPacket, DMA_CHANNEL_VIF1,
      true);
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  if (telemetry != nullptr) {
    ++telemetry->programSetSwaps;
    telemetry->programSetWaitTicks += readTelemetryTicks() - waitStart;
  }
  clearLastProgramName();
}

void StaPipQBufferRenderer::setDoubleBuffer() {
  u16 startingAddr = VU1_STAPIP_LAST_ITEM_ADDR + 1;
  // Modified by TyraX: the double buffer stops below the VU1 clipping
  // scratch area (plane table + Sutherland-Hodgman polygons) at the top of
  // VU1 data memory - see stapip_vu1_shared_defines.h.
  const u16 bufferMaxSize = VU1_STAPIP_DBUFFER_END;
  bufferSize = (bufferMaxSize - startingAddr) / 2;

  path1->setDoubleBuffer(startingAddr, bufferSize);

  bufferSize -= 1;  // Because we don't want to upload anything from first
                    // buffer, to first addr of second buffer
}

StaPipQBuffer* StaPipQBufferRenderer::getBuffer() {
  currentBufferIndex = nextBufferIndex++;
  Verbose("Proposing buffer: ", currentBufferIndex);
  auto* result = buffers[currentBufferIndex];
  if (nextBufferIndex >= buffersCount) {
    Verbose("Rollup - clearing buffer indices. currentBufferIndex: ",
            currentBufferIndex, " (before)nextBufferIndex: ", nextBufferIndex);
    nextBufferIndex = 0;
  }
  return result;
}

u16 StaPipQBufferRenderer::getQBufferIndex(StaPipQBuffer* buffer) {
  for (u16 i = 0; i < buffersCount; i++) {
    if (buffers[i] == buffer) return i;
  }
  TYRA_TRAP("Buffer not found!");
  return 0;
}

bool StaPipQBufferRenderer::is1stDBufferFlushTime() {
  return nextBufferIndex == buffersCount / 2;
}

bool StaPipQBufferRenderer::is2ndDBufferFlushTime() {
  return nextBufferIndex == 0;
}

void StaPipQBufferRenderer::flushBuffers() {
  auto is1stDBuffer = is1stDBufferFlushTime();
  auto is2ndDBuffer = is2ndDBufferFlushTime();

  if (!is1stDBuffer && !is2ndDBuffer) {
    auto offset = currentBufferIndex >= buffersCount / 2 ? buffersCount / 2 : 0;
    auto size = (currentBufferIndex + 1) - offset;
    Verbose("-- End flush. from: ", offset, " to: ", offset + size);
    addBuffersDataToPacket(offset, offset + size);
    sendPacket();
  }

  currentBufferIndex = 0;
  nextBufferIndex = 0;

  Verbose("End flush - zeroing buffer indices.");
}

void StaPipQBufferRenderer::cull(StaPipQBuffer* buffer) {
  if (buffer->size == 0) {
    return;
  }

  dBufferPrograms[getQBufferIndex(buffer)] = getCullProgramByBag(buffer->bag);

  Verbose("Add cull[", getQBufferIndex(buffer), "]: ", buffer->size);

  auto is1stDBuffer = is1stDBufferFlushTime();
  auto is2ndDBuffer = is2ndDBufferFlushTime();

  if (is1stDBuffer || is2ndDBuffer) {
    auto from = is1stDBuffer ? 0 : buffersCount / 2;
    auto to = from + buffersCount / 2;

    Verbose("-- Half flush at ", getQBufferIndex(buffer), ". from: ", from,
            " to: ", to);

    addBuffersDataToPacket(from, to);
    sendPacket();
  }
}

void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {
  if (buffer->size == 0) {
    return;
  }

  // Modified by TyraX: VU1 clipping - clip-classified packages go to
  // the clip VU1 programs with raw object-space vertices (exactly like the
  // cull path); the EE clipper is bypassed entirely. The package occupancy
  // cap (StaPipCore::getClipPackageDivisor) bounds the on-VU1 fan-out.
  if (vu1Clipping) {
    dBufferPrograms[getQBufferIndex(buffer)] = getClipProgramByBag(buffer->bag);

    Verbose("Add vu1-clip[", getQBufferIndex(buffer), "]: ", buffer->size);

    auto is1stDBuffer = is1stDBufferFlushTime();
    auto is2ndDBuffer = is2ndDBufferFlushTime();

    if (is1stDBuffer || is2ndDBuffer) {
      auto from = is1stDBuffer ? 0 : buffersCount / 2;
      auto to = from + buffersCount / 2;

      addBuffersDataToPacket(from, to);
      sendPacket();
    }
    return;
  }

  // Modified by TyraX: a subpackage clipped against the frustum can fan
  // out into more verts than one VU1 buffer holds (maxVertCount). Drain the
  // clip result across as many buffer slots / VU1 draws as needed instead of
  // overflowing a single buffer (that tripped the "Max buffer size in VU1"
  // assert in stapip_qbuffer.cpp while walking around a scene).
  StaPipBag* bag = buffer->bag;
  const u32 total = clipper.clipToPool(buffer);

  // chunk is a multiple of 3 so a triangle is never split across two draws.
  const u32 chunk = (maxVertCount / 3) * 3;

  u32 emitted = 0;
  StaPipQBuffer* target = buffer;  // reuse the slot the caller already acquired
  do {
    const u32 remaining = total - emitted;
    const u32 n = remaining < chunk ? remaining : chunk;

    target->bag = bag;
    clipper.writeChunk(target, emitted, n);  // n == 0 -> empty slot, skipped
    dBufferPrograms[getQBufferIndex(target)] = getAsIsProgramByBag(bag);

    Verbose("Add clip[", getQBufferIndex(target), "]: ", target->size);

    auto is1stDBuffer = is1stDBufferFlushTime();
    auto is2ndDBuffer = is2ndDBufferFlushTime();

    if (is1stDBuffer || is2ndDBuffer) {
      auto from = is1stDBuffer ? 0 : buffersCount / 2;
      auto to = from + buffersCount / 2;

      Verbose("-- Half flush at ", getQBufferIndex(target), ". from: ", from,
              " to: ", to);

      addBuffersDataToPacket(from, to);
      sendPacket();
    }

    emitted += n;
    if (emitted >= total) break;

    target = getBuffer();  // next slot for the next chunk
  } while (true);
}

void StaPipQBufferRenderer::clearLastProgramName() {
  lastProgramName = StaPipUndefinedProgram;
}

void StaPipQBufferRenderer::addBuffersDataToPacket(const u32& from,
                                                   const u32& to) {
  auto* currentPacket = packets[context];
  packet2_reset(currentPacket, false);

  for (u32 i = from; i < to; i++) {
    if (!buffers[i]->any()) continue;

    auto* program = dBufferPrograms[i];

    program->addBufferDataToPacket(currentPacket, buffers[i], prim);

    Verbose("Send ", program->getStringName(), "[", i, "]: ", buffers[i]->size);

    if (lastProgramName != program->getName()) {
      packet2_utils_vu_add_start_program(currentPacket,
                                         program->getDestinationAddress());
      lastProgramName = program->getName();
    } else {
      packet2_utils_vu_add_continue_program(currentPacket);
    }
  }

  packet2_utils_vu_add_end_tag(currentPacket);
}

void StaPipQBufferRenderer::sendPacket() {
  auto* currentPacket = packets[context];

  TYRA_ASSERT(packet2_get_qw_count(currentPacket) <= qbuffersPacketSize,
              "Packet is too big. Internal error");

  const u32 waitStart = telemetry != nullptr ? readTelemetryTicks() : 0;
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  if (telemetry != nullptr) {
    ++telemetry->packetFlushes;
    telemetry->vu1WaitTicks += readTelemetryTicks() - waitStart;
  }
  dma_channel_wait(DMA_CHANNEL_GIF, 0);  // Wait for texture. Issue #182.

  // TyraX: the VU1 packet tap (docs/devkit.md). Null in any build whose devkit
  // layer does not exist, so this is one load + branch per bag flush and the
  // capture code is not linked at all.
  if (g_vuPacketHook)
    g_vuPacketHook(currentPacket->base, packet2_get_qw_count(currentPacket),
                   "");  // the program is in the packet's MSCAL address

  // dma_wait_fast(); // This have no impact on performance

  dma_channel_send_packet2(currentPacket, DMA_CHANNEL_VIF1, true);

  // TyraX: with a VU1 memory hook installed (a devkit capture is in flight),
  // wait for the transfer AND for VU1 to finish its microprogram, then hand the
  // whole of VU1 data memory over. This stalls the pipeline on purpose - it runs
  // for the one frame a capture was armed for, never otherwise.
  if (g_vuMemHook) {
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    // VIF1_STAT: VPS (bits 0-1) = VIF status, VEW (bit 2) = waiting for VU1.
    volatile u32* const vif1Stat = (volatile u32*)0x10003c00;
    for (int spin = 0; spin < 2000000 && (*vif1Stat & 0x7) != 0; ++spin) {
    }
    g_vuMemHook((const void*)0x1100c000, 1024 * 16);
  }

  // Switch packet, so we can proceed during DMA transfer
  context = !context;
}

void StaPipQBufferRenderer::setMaxVertCount(const u32& count) {
  maxVertCount = count;
  for (u32 i = 0; i < buffersCount; i++) {
    buffers[i]->setMaxVertCount(count);
  }
  clipper.setMaxVertCount(count);
}

StaPipVU1Program* StaPipQBufferRenderer::getAsIsProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);

  // Modified by TyraX: billboard bags always use the billboard programs
  // (per-quad culling happens on VU1; there is no as_is/clip variant).
  if (programType == StaPipVU1BillboardTexture)
    return getProgramByName(StaPipBillboardTexture);
  else if (programType == StaPipVU1BillboardColor)
    return getProgramByName(StaPipBillboardColor);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipAsIsTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipAsIsDirLights);
  else if (programType == StaPipVU1TextureEnvColor)  // TyraX: env (matcap)
    return getProgramByName(StaPipAsIsTextureEnv);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipAsIsTextureColor);
  else
    return getProgramByName(StaPipAsIsColor);
}

// Modified by TyraX: VU1 clipping.
StaPipVU1Program* StaPipQBufferRenderer::getClipProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);

  // Modified by TyraX: billboard bags always use the billboard programs.
  if (programType == StaPipVU1BillboardTexture)
    return getProgramByName(StaPipBillboardTexture);
  else if (programType == StaPipVU1BillboardColor)
    return getProgramByName(StaPipBillboardColor);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipClipTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipClipDirLights);
  else if (programType == StaPipVU1TextureEnvColor)  // TyraX: env (matcap)
    return getProgramByName(StaPipClipTextureEnv);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipClipTextureColor);
  else
    return getProgramByName(StaPipClipColor);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);
  return getCullProgramByType(programType);
}

StaPipVU1Program* StaPipQBufferRenderer::getProgramByName(
    const StaPipProgramName& name) {
  // Modified by TyraX: a class the project dropped has no program on VU1, and
  // MSCAL-ing to an address nothing was uploaded to draws garbage. Walk down to
  // a resident relative instead - the mesh loses a feature, not the frame.
  StaPipProgramName resolved = name;
  for (int guard = 0; guard < 4; ++guard) {
    const StaPipProgramName next = residentFallback(resolved);
    if (next == resolved) break;
    resolved = next;
  }
  return repository.getProgram(resolved);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByParams(
    const bool& isLightingEnabled, const bool& isTextureEnabled) {
  auto type = getDrawProgramTypeByParams(isLightingEnabled, isTextureEnabled);
  return getCullProgramByType(type);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByType(
    const StaPipProgramType& programType) {
  // Modified by TyraX: billboard bags always use the billboard programs.
  if (programType == StaPipVU1BillboardTexture)
    return getProgramByName(StaPipBillboardTexture);
  else if (programType == StaPipVU1BillboardColor)
    return getProgramByName(StaPipBillboardColor);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipCullTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipCullDirLights);
  else if (programType == StaPipVU1TextureEnvColor)  // TyraX: env (matcap)
    return getProgramByName(StaPipCullTextureEnv);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipCullTextureColor);
  else
    return getProgramByName(StaPipCullColor);
}

StaPipProgramType StaPipQBufferRenderer::getDrawProgramTypeByBag(
    const StaPipBag* bag) const {
  // Modified by TyraX: particle billboards - the vertex slot carries
  // centers, the ST slot the per-particle basis weights. The texture bag is
  // always present (params channel); a real image selects the T variant.
  if (bag->billboard != nullptr)
    return bag->texture->texture != nullptr ? StaPipVU1BillboardTexture
                                            : StaPipVU1BillboardColor;
  // Modified by TyraX: env (matcap) - the ST slot carries normals, the
  // texture ST is computed on VU1. Lighting is unsupported with env.
  if (bag->texture != nullptr && bag->texture->coordinatesAreNormals)
    return StaPipVU1TextureEnvColor;
  auto isLightingEnabled = bag->lighting != nullptr;
  auto isTextureEnabled = bag->texture != nullptr;
  return getDrawProgramTypeByParams(isLightingEnabled, isTextureEnabled);
}

StaPipProgramType StaPipQBufferRenderer::getDrawProgramTypeByParams(
    const bool& isLightingEnabled, const bool& isTextureEnabled) const {
  if (isLightingEnabled && isTextureEnabled)
    return StaPipVU1TextureDirLights;
  else if (isLightingEnabled)
    return StaPipVU1DirLights;
  else if (isTextureEnabled)
    return StaPipVU1TextureColor;
  else
    return StaPipVU1Color;
}

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Modified by tyra-editor: PipelineZTest_TestOnly branch in sendObjectData;
// per-mesh object-space spot light (flashlight) upload for the color VU1
// programs + EE clipper.

#include <math.h>
#include "renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp"
#include "renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h"
#include "packet2/packet2_tyra_utils.hpp"

// #define TYRA_QBUFF_RENDERER_VERBOSE_LOG 1

#ifdef TYRA_QBUFF_RENDERER_VERBOSE_LOG
#define Verbose(...) TyraDebug::writeLines("VRB: ", ##__VA_ARGS__, "\n")
#else
#define Verbose(...) ((void)0)
#endif

namespace Tyra {

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
}

void StaPipQBufferRenderer::allocateOnUse() {
  staticDataPacket = packet2_create(3, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  // Modified by tyra-editor: +6 qwords for the spot light unpack, +16 for the
  // VU1 clipping constants + plane table.
  objectDataPacket = packet2_create(42, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);

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
}

void StaPipQBufferRenderer::init(RendererCore* t_core, prim_t* t_prim,
                                 lod_t* t_lod) {
  path1 = t_core->getPath1();
  clipper.init(t_core->getSettings());
  rendererCore = t_core;
  prim = t_prim;
  lod = t_lod;

  // Modified by tyra-editor: the clip-space planes the VU1 clip programs cut
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
  out.cosCut2 = spot.cosCutoff * spot.cosCutoff;
  const float coneBase = objRange2 * (1.0F - out.cosCut2);
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

  // Modified by tyra-editor: spot light (flashlight) for the color programs.
  // The dir-lights addresses are free when the bag has no lighting - the
  // C/TC programs read the three spot quads from there. Always uploaded
  // (the programs always compute; a zero color makes it a no-op) and the
  // same numbers go to the EE clipper for the as_is path.
  if (!bag->lighting) {
    const auto meshSpot = buildSpotForBag(rendererCore->spot, bag->info->model);
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
  }

  // Modified by tyra-editor: VU1 clipping data. One quad of constants for the
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
    packet2_add_u32(objectDataPacket,
                    singleColorEnabled);   // Single color enabled.
    packet2_add_u32(objectDataPacket, 0);  // not used (dynpip lerp slot)
    // Modified by tyra-editor: GS hardware fog params (see RendererCoreFog)
    packet2_add_float(objectDataPacket, rendererCore->fog.scale);
    packet2_add_float(objectDataPacket, rendererCore->fog.offset);

    packet2_utils_gs_add_lod(objectDataPacket, lod);

    if (bag->info->zTestType == PipelineZTest_AllPass) {
      packet2_add_2x_s64(objectDataPacket,
                         GS_SET_TEST(0, 0, 0, 0, 0, 0, 0, ZTEST_METHOD_ALLPASS),
                         GS_REG_TEST);
    } else if (bag->info->zTestType == PipelineZTest_TestOnly) {
      // Depth-tested, no z write: alpha test fails every pixel and AFAIL
      // keeps the z buffer (GS FB_ONLY - color still written). The ZBUF
      // register (and thus the VU1 options layout) stays untouched.
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_ALLFAIL, 0x00,
                      ATEST_KEEP_ZBUFFER, DRAW_DISABLE, DRAW_DISABLE,
                      DRAW_ENABLE, rendererCore->gs.zBuffer.method),
          GS_REG_TEST);
    } else {
      packet2_add_2x_s64(
          objectDataPacket,
          GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_NOTEQUAL, 0x00,
                      ATEST_KEEP_FRAMEBUFFER, DRAW_DISABLE, DRAW_DISABLE,
                      DRAW_ENABLE, rendererCore->gs.zBuffer.method),
          GS_REG_TEST);
    }

    if (texBuffers != nullptr) {
      rendererCore->texture.updateClutBuffer(texBuffers->clut);

      packet2_utils_gs_add_texbuff_clut(objectDataPacket, texBuffers->core,
                                        &rendererCore->texture.clut);
    }
  }
  packet2_utils_vu_close_unpack(objectDataPacket);

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
  // Modified by tyra-editor: in VU1 clipping mode the clip programs replace
  // the as_is family (both plus cull would overflow VU1 micro memory, and
  // as_is is only ever fed by the retired EE clipper path).
  VU1Program** programs = new VU1Program*[8];
  programs[0] = repository.getProgram(StaPipCullColor);
  programs[1] =
      repository.getProgram(vu1Clipping ? StaPipClipColor : StaPipAsIsColor);
  programs[2] = repository.getProgram(StaPipCullDirLights);
  programs[3] = repository.getProgram(vu1Clipping ? StaPipClipDirLights
                                                  : StaPipAsIsDirLights);
  programs[4] = repository.getProgram(StaPipCullTextureDirLights);
  programs[5] = repository.getProgram(vu1Clipping ? StaPipClipTextureDirLights
                                                  : StaPipAsIsTextureDirLights);
  programs[6] = repository.getProgram(StaPipCullTextureColor);
  programs[7] = repository.getProgram(vu1Clipping ? StaPipClipTextureColor
                                                  : StaPipAsIsTextureColor);
  programsPacket = path1->createProgramsCache(programs, 8, 0);
  delete[] programs;
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
}

void StaPipQBufferRenderer::setDoubleBuffer() {
  u16 startingAddr = VU1_STAPIP_LAST_ITEM_ADDR + 1;
  // Modified by tyra-editor: the double buffer stops below the VU1 clipping
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

  // Modified by tyra-editor: VU1 clipping - clip-classified packages go to
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

  // Modified by tyra-editor: a subpackage clipped against the frustum can fan
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

  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);  // Wait for texture. Issue #182.

  // dma_wait_fast(); // This have no impact on performance

  dma_channel_send_packet2(currentPacket, DMA_CHANNEL_VIF1, true);

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

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipAsIsTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipAsIsDirLights);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipAsIsTextureColor);
  else
    return getProgramByName(StaPipAsIsColor);
}

// Modified by tyra-editor: VU1 clipping.
StaPipVU1Program* StaPipQBufferRenderer::getClipProgramByBag(
    const StaPipBag* bag) {
  auto programType = getDrawProgramTypeByBag(bag);

  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipClipTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipClipDirLights);
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
  return repository.getProgram(name);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByParams(
    const bool& isLightingEnabled, const bool& isTextureEnabled) {
  auto type = getDrawProgramTypeByParams(isLightingEnabled, isTextureEnabled);
  return getCullProgramByType(type);
}

StaPipVU1Program* StaPipQBufferRenderer::getCullProgramByType(
    const StaPipProgramType& programType) {
  if (programType == StaPipVU1TextureDirLights)
    return getProgramByName(StaPipCullTextureDirLights);
  else if (programType == StaPipVU1DirLights)
    return getProgramByName(StaPipCullDirLights);
  else if (programType == StaPipVU1TextureColor)
    return getProgramByName(StaPipCullTextureColor);
  else
    return getProgramByName(StaPipCullColor);
}

StaPipProgramType StaPipQBufferRenderer::getDrawProgramTypeByBag(
    const StaPipBag* bag) const {
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

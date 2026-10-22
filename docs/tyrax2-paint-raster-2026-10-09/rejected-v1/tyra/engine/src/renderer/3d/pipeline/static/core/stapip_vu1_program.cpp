/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: emitsStateFlag()/kPackedCountWord for retained replays.
*/

#include "renderer/3d/pipeline/static/core/stapip_vu1_program.hpp"
#include "renderer/core/gs/renderer_core_depth.hpp"

namespace Tyra {

namespace {

bool isClipProgram(const StaPipProgramName name) {
  return name == StaPipClipColor || name == StaPipClipDirLights ||
         name == StaPipClipTextureDirLights ||
         name == StaPipClipTextureColor || name == StaPipClipTextureEnv;
}

bool supportsGsStateReuse(const StaPipProgramName name) {
  return name == StaPipCullTextureColor ||
         name == StaPipCullTextureDirLights ||
         name == StaPipAsIsTextureColor ||
         name == StaPipAsIsTextureDirLights;
}

u8 reverseSixBits(const u8 value) {
  // This runs once per clip qbuffer on the EE hot path. Keep it branchless so
  // an older GCC does not preserve a six-iteration helper loop.
  return static_cast<u8>(((value & 0x01U) << 5) |
                         ((value & 0x02U) << 3) |
                         ((value & 0x04U) << 1) |
                         ((value & 0x08U) >> 1) |
                         ((value & 0x10U) >> 3) |
                         ((value & 0x20U) >> 5));
}

}  // namespace

StaPipVU1Program::StaPipVU1Program(const StaPipProgramName& t_name,
                                   u32* t_start, u32* t_end,
                                   const u32& t_reglist,
                                   const u8& t_reglistCount,
                                   const u8& t_elementsPerVertex)
    : VU1Program(t_start, t_end),
      name(t_name),
      reglistCount(t_reglistCount),
      elementsPerVertex(t_elementsPerVertex),
      reglist(t_reglist) {
  packetSize = packet2_utils_get_packet_size_for_program(start, end);
  programSize = calculateProgramSize();
}

StaPipVU1Program::~StaPipVU1Program() {}

const StaPipProgramName& StaPipVU1Program::getName() const { return name; }

u32& StaPipVU1Program::getReglist() { return reglist; }

void StaPipVU1Program::addBufferDataToPacket(packet2_t* packet,
                                             StaPipQBuffer* buffer,
                                             prim_t* prim,
                                             const bool& emitState) {
  addStandardBufferDataToPacket(packet, buffer, prim, emitState);
  addProgramQBufferDataToPacket(packet, buffer);
}

bool StaPipVU1Program::emitsStateFlag(const bool& emitState) const {
  return supportsGsStateReuse(name) && emitState;
}

void StaPipVU1Program::addStandardBufferDataToPacket(packet2_t* packet,
                                                     StaPipQBuffer* buffer,
                                                     prim_t* prim,
                                                     const bool& emitState) {
  // Modified by TyraX: a billboard bag carries a texture bag purely for the
  // per-particle params channel - only a real image enables mapping.
  if (buffer->bag->texture && buffer->bag->texture->texture)
    prim->mapping = 1;
  else
    prim->mapping = 0;

  packet2_utils_vu_open_unpack(packet, 0, true);
  {
    packet2_add_float(packet, 2048.0F);     // scale
    packet2_add_float(packet, 2048.0F);     // scale
    // Modified by TyraX: 16x the upstream Z scale. Vertices are sent
    // as packed XYZF2 (for GS hardware fog), which reads Z from bits 4-27
    // of the word - ftoi4 already shifts by 4, so the float range must be
    // the full 24 bits for the same effective depth precision as before.
    // The RANGE itself is RendererCoreDepth's (16 bits when the framebuffer
    // is PSMCT16, whose z buffer must be PSMZ16 for page geometry), so this
    // constant lives in exactly one place now.
    packet2_add_float(packet, RendererCoreDepth::scale);  // scale
    u32 packedCount = buffer->size;
    if (supportsGsStateReuse(name) && emitState)
      packedCount |= VU1_STAPIP_EMIT_STATE_FLAG;
    if (isClipProgram(name)) {
      TYRA_ASSERT(buffer->size <= VU1_STAPIP_COUNT_MASK,
                  "Clip vertex count does not fit packed VU1 header");
      // A zero mask cannot safely skip anything. It should only be possible
      // for a direct/internal caller which bypassed StaPipCore's classifier;
      // fall back to all six planes instead of risking missing geometry.
      u8 planeMask = buffer->clipPlaneMask;
      if (planeMask == 0) planeMask = 0x3F;
      // Reverse the six bits so the next plane is always the sign bit of the
      // VU's 16-bit VI register. The microprogram can test it with ibltz and
      // advance with one doubling, without keeping a second plane-bit VI live.
      packedCount |= static_cast<u32>(reverseSixBits(planeMask))
                     << VU1_STAPIP_CLIP_MASK_SHIFT;
    }
    packet2_add_u32(packet, packedCount);  // vertex count + clip-plane mask

    // Modified by TyraX: the GS primitive is a property of the BUFFER, not of
    // the pipeline. A stripped buffer (StaPipBag::stripped) carries a triangle
    // STRIP, so the GS must take one vertex per triangle after the first two -
    // and a stripped bag's clip-routed packages travel in the same flush as
    // ordinary triangle lists, so the decision cannot live on the shared
    // prim_t. NLOOP is unchanged either way: it counts the GS vertices the
    // program EMITS, which is still one per input vertex (6x for the billboard
    // family - gsVertexCount).
    prim_t bufferPrim = *prim;
    if (buffer->stripped) bufferPrim.type = PRIM_TRIANGLE_STRIP;
    packet2_utils_gs_add_prim_giftag(packet, &bufferPrim,
                                     gsVertexCount(buffer->size), reglist,
                                     reglistCount, 0);
  }
  packet2_utils_vu_close_unpack(packet);
}

u16 StaPipVU1Program::getMaxVertCount(const bool& singleColorEnabled,
                                      const u16& bufferSize) const {
  // Modified by TyraX: 9 because of -> StoreTyraGifTags*Alpha{} (the tag
  // block grew by a (set, ALPHA) pair - the in-band per-mesh blend equation)
  u16 res = bufferSize - 9;
  u8 colorElementsPerVertex =
      singleColorEnabled ? elementsPerVertex - 1 : elementsPerVertex;
  res /= (colorElementsPerVertex + reglistCount);

  // Buffer size = VU1 double buffer size (xtop)
  // QBufferSize = res (it is placed inside VU1)

  // Must be divisible by 3: a package boundary through the middle of a
  // triangle corrupts the geometry, and every loop in the pipeline - the cull
  // programs, the clip programs, the EE clipper - walks whole triangles.
  //
  // Modified by TyraX: this used to round to a multiple of NINE, so that the
  // 1/3 subpackage split came out divisible by 3 as well. That second
  // condition is not required by any live path, and it cost the textured +
  // per-vertex-colour class - the one every static pass of a real scene takes
  // - three vertices a package (75 -> 72, 4% more packages, and almost every
  // per-package term in StaPipCore::dispatch scales with the count). The two
  // places that actually CUT triangles round for themselves and do not care
  // what this returns:
  //   - StaPipCore::clipPackageSize() is (maxVertCount / clipDivisor / 3) * 3
  //   - the clip drain chunk in StaPipQBufferRenderer is (maxVertCount / 3) * 3
  // and maxVertCount / 3 survives elsewhere only as the 1/3-bbox granularity
  // (StaPipBagPackagesBBox), which is a ceiling division with a remainder part
  // and is conservative by construction.
  //
  // Raising this REQUIRES re-baking the strip runs that are pinned to it
  // (meshstrip::kRun and the road/terrain emitters) - see docs/model-pipeline.md.
  res = (res / 3) * 3;
  return res;
}

}  // namespace Tyra

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

extern "C" {
extern u32 StaPipQuantizedDecode_CodeStart[], StaPipQuantizedDecode_CodeEnd[];
}

namespace Tyra {

namespace {

// Modified by TyraX: one common decoder runs before the stock draw program.

class QuantizedDecoder final : public VU1Program {
 public:
  QuantizedDecoder()
      : VU1Program(StaPipQuantizedDecode_CodeStart, StaPipQuantizedDecode_CodeEnd) {}
  std::string getStringName() const override { return "StaPip quantized decode"; }
};

void addQuantizedStream(packet2_t* packet, u32 address, const void* data,
                        u32 count, UnpackMode mode, u32 bytesPerVertex) {
  const u32 qwords = (count * bytesPerVertex + 15u) / 16u;
  packet2_chain_ref(packet, const_cast<void*>(data), qwords, 0, 0, 0);
  packet2_vif_stcycl(packet, 0, 0x0101, 0);
  packet2_vif_open_unpack(packet, mode, address, 1, 0, 1, 0);
  packet2_vif_close_unpack_manual(packet, count);
}

void addPackedMesh(packet2_t* packet, const StaPipQBuffer& buffer, u32 target) {
  const auto& mesh = *buffer.bag->packedMesh;
  const u32 count = buffer.size, offset = buffer.packedOffset;
  u32 address = VU1_STAPIP_VERT_DATA_ADDR;
  const bool textured = buffer.bag->texture != nullptr;
  // Relative to TOPS so the next package cannot overwrite a live decoder's
  // constants. This tail is free while decoding and reusable by the draw.
  packet2_utils_vu_open_unpack(packet, VU1_STAPIP_QUANT_META_ADDR, true);
  const Vec4 pos = mesh.positionBase(), st = mesh.texcoordBase();
  packet2_add_float(packet, pos.x);
  packet2_add_float(packet, pos.y);
  packet2_add_float(packet, pos.z);
  packet2_add_float(packet, pos.w);
  packet2_add_float(packet, st.x);
  packet2_add_float(packet, st.y);
  packet2_add_u32(packet, 0);
  packet2_add_u32(packet, target | (textured ? 0x8000u : 0u));
  packet2_utils_vu_close_unpack(packet);

  addQuantizedStream(packet, address, mesh.quantizedPositions(offset), count,
                     P2_UNPACK_V4_16, 8);
  address += count;
  if (textured) {
    // Real hardware stalled on the final element of a V2-16 REF. V4-16
    // consumes the same bytes as pairs, with the odd last lane zero-padded.
    static_assert(VU1_STAPIP_VERT_DATA_ADDR + 3 * StaPipPackedMesh::kSliceVertices
                  <= VU1_STAPIP_QUANT_UV_ADDR);
    addQuantizedStream(packet, VU1_STAPIP_QUANT_UV_ADDR,
                       mesh.quantizedTexcoords(offset), (count + 1u) / 2u,
                       P2_UNPACK_V4_16, 8);
    address += count;
  }
  addQuantizedStream(packet, address, mesh.quantizedColors(offset), count,
                     P2_UNPACK_V4_8, 4);
}

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

VU1Program& StaPipVU1Program::quantizedDecoder() {
  static QuantizedDecoder decoder;
  return decoder;
}

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
  if (buffer->nativePacked) addPackedMesh(packet, *buffer, getDestinationAddress());
  else addProgramQBufferDataToPacket(packet, buffer);
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
    // Modified by TyraX: 2048 except inside BLSS' particle bracket (see
    // RendererCoreDepth::screenScaleX).
    packet2_add_float(packet, RendererCoreDepth::screenScaleX);  // scale
    packet2_add_float(packet, RendererCoreDepth::screenScaleY);  // scale
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
      TYRA_ASSERT(buffer->size <= VU1_STAPIP_CLIP_COUNT_MASK,
                  "Clip vertex count does not fit packed VU1 header");
      // Modified by TyraX: a strip package reaches the clip program whole
      // and is walked one vertex per triangle there (bit 9).
      if (buffer->stripped) packedCount |= VU1_STAPIP_CLIP_STRIP_FLAG;
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
    // A clip program reads the strip but emits a fan-triangulated LIST.
    if (buffer->stripped && !isClipProgram(name))
      bufferPrim.type = PRIM_TRIANGLE_STRIP;
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

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/3d/pipeline/static/core/stapip_vu1_program.hpp"

namespace Tyra {

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
                                             prim_t* prim) {
  addStandardBufferDataToPacket(packet, buffer, prim);
  addProgramQBufferDataToPacket(packet, buffer);
}

void StaPipVU1Program::addStandardBufferDataToPacket(packet2_t* packet,
                                                     StaPipQBuffer* buffer,
                                                     prim_t* prim) {
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
    packet2_add_float(packet, static_cast<float>(0xFFFFFF) / 2.0F);  // scale
    packet2_add_u32(packet, buffer->size);  // vertex count

    // Modified by TyraX: NLOOP counts the GS vertices the program EMITS -
    // 6x the input count for the billboard family (gsVertexCount).
    packet2_utils_gs_add_prim_giftag(packet, prim, gsVertexCount(buffer->size),
                                     reglist, reglistCount, 0);
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

  // Must be dividable by 3 and the result also dividable by 3. Why?
  // 1st dividable reason - triangle, and packaging system in 3d rendering
  // 2nd dividable reason - subpackaging system. We are splitting packages into
  // 3 subpackages in 3d renderer.
  res = res / 3 / 3;
  res = res * 3 * 3;
  return res;
}

}  // namespace Tyra

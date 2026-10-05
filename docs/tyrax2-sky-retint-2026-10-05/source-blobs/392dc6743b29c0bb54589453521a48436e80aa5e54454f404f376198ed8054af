/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: particle billboard expansion on VU1 (textured).
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/billboard/stapip_billboard_t_vu1_program.hpp"

extern u32 StaPipVU1Billboard_T_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Billboard_T_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipBillboardTVU1Program::StaPipBillboardTVU1Program()
    : StaPipVU1Program(StaPipBillboardTexture, &StaPipVU1Billboard_T_CodeStart,
                       &StaPipVU1Billboard_T_CodeEnd,
                       ((u64)GIF_REG_ST) << 0 | ((u64)GIF_REG_RGBAQ) << 4 |
                           ((u64)GIF_REG_XYZF2) << 8,
                       3, 3) {}

StaPipBillboardTVU1Program::~StaPipBillboardTVU1Program() {}

std::string StaPipBillboardTVU1Program::getStringName() const {
  return std::string("StaPip - Billboard - T");
}

void StaPipBillboardTVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;

  // Add particle centers
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,
                                   qbuffer->size, true);
  addr += qbuffer->size;

  // Add per-particle 2x2 basis weights (the "ST" channel)
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->sts, qbuffer->size,
                                   true);
  addr += qbuffer->size;

  // Add per-particle colors (billboard bags are always multi-color)
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->colors,
                                   qbuffer->size, true);
}

// One input center costs 3 input qwords (center, weights, color) and
// 6 x 3 = 18 output qwords; the base-class 9-qword tag reserve applies.
u16 StaPipBillboardTVU1Program::getMaxVertCount(const bool& singleColorEnabled,
                                                const u16& vu1DBufferSize) const {
  (void)singleColorEnabled;
  return (vu1DBufferSize - 9) / 21;
}

// 6 GS vertices per input center - keeps the EE-built prim giftag NLOOP in
// sync with the expanded output (an undercounting NLOOP stalls the GIF).
u32 StaPipBillboardTVU1Program::gsVertexCount(const u32& inputCount) const {
  return inputCount * 6;
}

}  // namespace Tyra

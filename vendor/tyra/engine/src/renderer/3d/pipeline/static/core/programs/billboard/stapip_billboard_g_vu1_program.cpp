/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: ground cover cards on VU1 (docs/ground-cover.md).
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/billboard/stapip_billboard_g_vu1_program.hpp"

extern u32 StaPipVU1Billboard_G_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Billboard_G_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

// GS registers per output vertex: ST + XYZF2 (colour and Q = 1.0 come from
// the one RGBAQ the program writes before the prim tag).
StaPipBillboardGVU1Program::StaPipBillboardGVU1Program()
    : StaPipVU1Program(StaPipBillboardGrass, &StaPipVU1Billboard_G_CodeStart,
                       &StaPipVU1Billboard_G_CodeEnd,
                       ((u64)GIF_REG_ST) << 0 | ((u64)GIF_REG_XYZF2) << 4, 2,
                       1) {}

StaPipBillboardGVU1Program::~StaPipBillboardGVU1Program() {}

std::string StaPipBillboardGVU1Program::getStringName() const {
  return std::string("StaPip - Billboard - Grass");
}

void StaPipBillboardGVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  // The cards only: (x, ground y, z, signed size) - one qword each.
  packet2_utils_vu_add_unpack_data(packet, VU1_STAPIP_VERT_DATA_ADDR,
                                   qbuffer->vertices, qbuffer->size, true);
}

// One card = 1 input qword + 4 x 2 output qwords; the tag block is the
// standard 9 plus the (set, RGBAQ) pair.
u16 StaPipBillboardGVU1Program::getMaxVertCount(
    const bool& singleColorEnabled, const u16& vu1DBufferSize) const {
  (void)singleColorEnabled;
  return ((vu1DBufferSize - 11) / 9 / 3) * 3;
}

// 4 strip vertices per card - the EE-built prim giftag NLOOP.
u32 StaPipBillboardGVU1Program::gsVertexCount(const u32& inputCount) const {
  return inputCount * 4;
}

}  // namespace Tyra

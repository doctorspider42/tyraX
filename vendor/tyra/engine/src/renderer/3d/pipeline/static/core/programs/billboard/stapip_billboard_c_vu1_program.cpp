/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: particle billboard expansion on VU1 (untextured).
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/billboard/stapip_billboard_c_vu1_program.hpp"

extern u32 StaPipVU1Billboard_C_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Billboard_C_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipBillboardCVU1Program::StaPipBillboardCVU1Program()
    : StaPipVU1Program(StaPipBillboardColor, &StaPipVU1Billboard_C_CodeStart,
                       &StaPipVU1Billboard_C_CodeEnd,
                       ((u64)GIF_REG_RGBAQ) << 0 | ((u64)GIF_REG_XYZF2) << 4,
                       2, 3) {}

StaPipBillboardCVU1Program::~StaPipBillboardCVU1Program() {}

std::string StaPipBillboardCVU1Program::getStringName() const {
  return std::string("StaPip - Billboard - C");
}

void StaPipBillboardCVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;

  // Add particle centers
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,
                                   qbuffer->size, true);
  addr += qbuffer->size;

  // Add per-particle 2x2 basis weights (the "ST" channel - present even
  // without a texture, see StaPipBillboardBag)
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->sts, qbuffer->size,
                                   true);
  addr += qbuffer->size;

  // Add per-particle colors (billboard bags are always multi-color)
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->colors,
                                   qbuffer->size, true);
}

// One input center costs 3 input qwords (center, weights, color) and
// 6 x 2 = 12 output qwords; the base-class 9-qword tag reserve applies.
u16 StaPipBillboardCVU1Program::getMaxVertCount(const bool& singleColorEnabled,
                                                const u16& vu1DBufferSize) const {
  (void)singleColorEnabled;
  return (vu1DBufferSize - 9) / 15;
}

// 6 GS vertices per input center (see the T variant).
u32 StaPipBillboardCVU1Program::gsVertexCount(const u32& inputCount) const {
  return inputCount * 6;
}

}  // namespace Tyra

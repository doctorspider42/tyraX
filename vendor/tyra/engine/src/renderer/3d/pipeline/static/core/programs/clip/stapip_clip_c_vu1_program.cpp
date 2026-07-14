/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: VU1 clipping program (clip family).
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/clip/stapip_clip_c_vu1_program.hpp"

extern u32 StaPipVU1Clip_C_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Clip_C_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipClipCVU1Program::StaPipClipCVU1Program()
    : StaPipVU1Program(
          StaPipClipColor, &StaPipVU1Clip_C_CodeStart, &StaPipVU1Clip_C_CodeEnd,
          ((u64)GIF_REG_RGBAQ) << 0 | ((u64)GIF_REG_XYZF2) << 4, 2, 2) {}

StaPipClipCVU1Program::~StaPipClipCVU1Program() {}

std::string StaPipClipCVU1Program::getStringName() const {
  return std::string("StaPip - Clip - C");
}

void StaPipClipCVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;

  // Add vertices
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,
                                   qbuffer->size, true);

  // Add colors
  if (qbuffer->bag->color->single == nullptr) {
    addr += qbuffer->size;
    packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->colors,
                                     qbuffer->size, true);
  }
}

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by tyra-editor: VU1 clipping program (clip family).
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/clip/stapip_clip_d_vu1_program.hpp"

extern u32 StaPipVU1Clip_D_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Clip_D_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipClipDVU1Program::StaPipClipDVU1Program()
    : StaPipVU1Program(StaPipClipDirLights, &StaPipVU1Clip_D_CodeStart,
                       &StaPipVU1Clip_D_CodeEnd,
                       ((u64)GIF_REG_RGBAQ) << 0 | ((u64)GIF_REG_XYZF2) << 4, 2,
                       3) {}

StaPipClipDVU1Program::~StaPipClipDVU1Program() {}

std::string StaPipClipDVU1Program::getStringName() const {
  return std::string("StaPip - Clip - D");
}

void StaPipClipDVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;

  // Add vertices
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,
                                   qbuffer->size, true);
  addr += qbuffer->size;

  // Add normal
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->normals,
                                   qbuffer->size, true);

  // Add colors
  if (qbuffer->bag->color->single == nullptr) {
    addr += qbuffer->size;
    packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->colors,
                                     qbuffer->size, true);
  }
}

}  // namespace Tyra

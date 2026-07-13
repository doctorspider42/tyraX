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
#include "renderer/3d/pipeline/static/core/programs/clip/stapip_clip_td_vu1_program.hpp"

extern u32 StaPipVU1Clip_TD_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Clip_TD_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipClipTDVU1Program::StaPipClipTDVU1Program()
    : StaPipVU1Program(StaPipClipTextureDirLights, &StaPipVU1Clip_TD_CodeStart,
                       &StaPipVU1Clip_TD_CodeEnd,
                       ((u64)GIF_REG_ST) << 0 | ((u64)GIF_REG_RGBAQ) << 4 |
                           ((u64)GIF_REG_XYZF2) << 8,
                       3, 4) {}

StaPipClipTDVU1Program::~StaPipClipTDVU1Program() {}

std::string StaPipClipTDVU1Program::getStringName() const {
  return std::string("StaPip - Clip - TD");
}

void StaPipClipTDVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;

  // Add vertices
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,
                                   qbuffer->size, true);
  addr += qbuffer->size;

  // Add sts
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->sts, qbuffer->size,
                                   true);
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

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: env (matcap) variant of the clip TC program - the ST slot
# carries object-space normals, the sphere-map ST is computed on VU1 before
# the Sutherland-Hodgman pass.
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/clip/stapip_clip_tce_vu1_program.hpp"

// TC and TCE have the same three-stream input/scratch/output ABI. The shared
// TC image selects matcap ST generation through VU1_OPTIONS_ADDR.y.
extern u32 StaPipVU1Clip_TC_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Clip_TC_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipClipTCEVU1Program::StaPipClipTCEVU1Program()
    : StaPipVU1Program(StaPipClipTextureEnv, &StaPipVU1Clip_TC_CodeStart,
                       &StaPipVU1Clip_TC_CodeEnd,
                       ((u64)GIF_REG_ST) << 0 | ((u64)GIF_REG_RGBAQ) << 4 |
                           ((u64)GIF_REG_XYZF2) << 8,
                       3, 3) {}

StaPipClipTCEVU1Program::~StaPipClipTCEVU1Program() {}

std::string StaPipClipTCEVU1Program::getStringName() const {
  return std::string("StaPip - Clip - TCE");
}

void StaPipClipTCEVU1Program::addProgramQBufferDataToPacket(
    packet2_t* packet, StaPipQBuffer* qbuffer) const {
  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;

  // Add vertices
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,
                                   qbuffer->size, true);
  addr += qbuffer->size;

  // Add normals (they ride in the ST slot - see StaPipTextureBag)
  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->sts, qbuffer->size,
                                   true);

  // Add colors
  if (qbuffer->bag->color->single == nullptr) {
    addr += qbuffer->size;
    packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->colors,
                                     qbuffer->size, true);
  }
}

}  // namespace Tyra

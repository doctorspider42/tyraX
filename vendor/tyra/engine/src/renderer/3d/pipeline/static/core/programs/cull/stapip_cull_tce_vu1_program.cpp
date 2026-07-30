/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: env (matcap) variant of the cull TC program - the ST slot
# carries object-space normals, the sphere-map ST is computed on VU1.
*/

#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tce_vu1_program.hpp"

extern u32 StaPipVU1Cull_TCE_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Cull_TCE_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

StaPipCullTCEVU1Program::StaPipCullTCEVU1Program()
    : StaPipVU1Program(StaPipCullTextureEnv, &StaPipVU1Cull_TCE_CodeStart,
                       &StaPipVU1Cull_TCE_CodeEnd,
                       ((u64)GIF_REG_ST) << 0 | ((u64)GIF_REG_RGBAQ) << 4 |
                           ((u64)GIF_REG_XYZF2) << 8,
                       3, 3) {}

StaPipCullTCEVU1Program::~StaPipCullTCEVU1Program() {}

std::string StaPipCullTCEVU1Program::getStringName() const {
  return std::string("StaPip - Cull - TCE");
}

void StaPipCullTCEVU1Program::addProgramQBufferDataToPacket(
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

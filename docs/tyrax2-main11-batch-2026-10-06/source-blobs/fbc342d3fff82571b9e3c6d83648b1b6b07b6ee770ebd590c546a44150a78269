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

#include "renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp"

// Modified by TyraX: a measured-only experiment can swap in another image
// (stapip_vu1_experiments.hpp). The #if branch is the experiment; the
// #else branch is the shipping image `--vu-check` verifies.
#if TYRA_VU1_EXP_ENV_NORMALIZED
extern u32 StaPipVU1Cull_TCE_ENVN_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Cull_TCE_ENVN_CodeEnd __attribute__((section(".vudata")));
#define TYRA_WRAPPER_IMAGE_START (&StaPipVU1Cull_TCE_ENVN_CodeStart)
#define TYRA_WRAPPER_IMAGE_END (&StaPipVU1Cull_TCE_ENVN_CodeEnd)
#else
extern u32 StaPipVU1Cull_TCE_CodeStart __attribute__((section(".vudata")));
extern u32 StaPipVU1Cull_TCE_CodeEnd __attribute__((section(".vudata")));
#define TYRA_WRAPPER_IMAGE_START (&StaPipVU1Cull_TCE_CodeStart)
#define TYRA_WRAPPER_IMAGE_END (&StaPipVU1Cull_TCE_CodeEnd)
#endif

namespace Tyra {

StaPipCullTCEVU1Program::StaPipCullTCEVU1Program()
    : StaPipVU1Program(StaPipCullTextureEnv, TYRA_WRAPPER_IMAGE_START,
                       TYRA_WRAPPER_IMAGE_END,
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

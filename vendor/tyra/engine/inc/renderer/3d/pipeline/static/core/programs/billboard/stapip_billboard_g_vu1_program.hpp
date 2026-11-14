/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: ground cover cards on VU1 (docs/ground-cover.md).
*/

#pragma once

#include <packet2_utils.h>
#include <string>
#include "../../stapip_vu1_program.hpp"

namespace Tyra {

/** One qword per card in, a 4-vertex ST + XYZF2 triangle strip out (see
 * stapip_billboard_g_vu1.vclpp). Selected for a billboard bag whose
 * StaPipBillboardBag::grass is set. */
class StaPipBillboardGVU1Program : public StaPipVU1Program {
 public:
  StaPipBillboardGVU1Program();
  ~StaPipBillboardGVU1Program();

  std::string getStringName() const;
  void addProgramQBufferDataToPacket(packet2_t* packet,
                                     StaPipQBuffer* qbuffer) const;

  u16 getMaxVertCount(const bool& singleColorEnabled,
                      const u16& vu1DBufferSize) const override;
  u32 gsVertexCount(const u32& inputCount) const override;
};

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: particle billboard expansion on VU1 (textured).
*/

#pragma once

#include <packet2_utils.h>
#include <string>
#include "../../stapip_vu1_program.hpp"

namespace Tyra {

class StaPipBillboardTVU1Program : public StaPipVU1Program {
 public:
  StaPipBillboardTVU1Program();
  ~StaPipBillboardTVU1Program();

  std::string getStringName() const;
  void addProgramQBufferDataToPacket(packet2_t* packet,
                                     StaPipQBuffer* qbuffer) const;

  u16 getMaxVertCount(const bool& singleColorEnabled,
                      const u16& vu1DBufferSize) const override;
  u32 gsVertexCount(const u32& inputCount) const override;
};

}  // namespace Tyra

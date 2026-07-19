/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: env (matcap) variant of the as_is TC program.
*/

#pragma once

#include <packet2_utils.h>
#include <string>
#include "../../stapip_vu1_program.hpp"

namespace Tyra {

class StaPipAsIsTCEVU1Program : public StaPipVU1Program {
 public:
  StaPipAsIsTCEVU1Program();
  ~StaPipAsIsTCEVU1Program();

  std::string getStringName() const;
  void addProgramQBufferDataToPacket(packet2_t* packet,
                                     StaPipQBuffer* qbuffer) const;
};

}  // namespace Tyra

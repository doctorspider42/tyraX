/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include "./stapip_program_name.hpp"
#include "renderer/core/paths/path1/vu1_program.hpp"
#include "./stapip_qbuffer.hpp"
#include "./programs/stapip_vu1_shared_defines.h"

namespace Tyra {

class StaPipVU1Program : public VU1Program {
 public:
  StaPipVU1Program(const StaPipProgramName& name, u32* start, u32* end,
                   const u32& t_reglist, const u8& t_reglistCount,
                   const u8& t_elementsPerVertex);
  ~StaPipVU1Program();

  u32& getReglist();

  const StaPipProgramName& getName() const;

  // Modified by TyraX: virtual - the billboard programs have their own
  // input/output qword budget (6 GS verts per input center).
  virtual u16 getMaxVertCount(const bool& singleColorEnabled,
                              const u16& vu1DBufferSize) const;

  void addBufferDataToPacket(packet2_t* packet, StaPipQBuffer* buffer,
                             prim_t* prim);

 protected:
  StaPipProgramName name;
  u8 reglistCount, elementsPerVertex;
  u32 destinationAddress, reglist;

  virtual void addProgramQBufferDataToPacket(packet2_t* packet,
                                             StaPipQBuffer* qbuffer) const = 0;

  // Modified by TyraX: GS vertices produced per input vertex - the prim
  // giftag NLOOP is built from this on the EE. 1:1 for every program
  // except the billboard family (1 center -> 6 GS vertices).
  virtual u32 gsVertexCount(const u32& inputCount) const { return inputCount; }

 private:
  void addStandardBufferDataToPacket(packet2_t* packet, StaPipQBuffer* buffer,
                                     prim_t* prim);
};

}  // namespace Tyra

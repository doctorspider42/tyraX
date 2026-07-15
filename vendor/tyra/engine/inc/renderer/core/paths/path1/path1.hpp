/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: track whether the VU1 double buffer was configured,
# so RendererCore::endFrame() only arms the PATH1 drain barrier once the VU1
# side (VIF1 DMA init + BASE/OFST) exists - see renderer_core.cpp.
*/

#pragma once

#include <dma.h>
#include <packet2_utils.h>
#include "debug/debug.hpp"
#include "./vu1_program.hpp"

namespace Tyra {

class Path1 {
 public:
  Path1();
  ~Path1();

  u32 uploadProgram(VU1Program* program, const u32& address);

  void sendDrawFinishTag();

  void addDrawFinishTag(packet2_t* packet);

  packet2_t* createProgramsCache(VU1Program** programs, const u32& count,
                                 const u32& address);

  void setDoubleBuffer(const u16& startingAddress, const u16& bufferSize);

  /** True once any 3D pipeline configured the VU1 double buffer. */
  bool isVU1Configured() const { return vu1Configured; }

 private:
  void uploadDrawFinishProgram();
  void prepareDrawFinishPacket();

  packet2_t* doubleBufferPacket;
  packet2_t* drawFinishPacket;
  u32 drawFinishAddr;
  bool vu1Configured;
};

}  // namespace Tyra

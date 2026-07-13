/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Wellington Carvalho <wellcoj@gmail.com>
*/

#pragma once

#include <tamtypes.h>
#include <stddef.h>
#include "time/timer.hpp"
#include "./version.hpp"

namespace Tyra {

class Info {
 public:
  Info();
  ~Info();

  Version version;

  static bool writeLogsToFile;

  // Modified by tyra-editor: when false (the default) a failed TYRA_ASSERT /
  // TYRA_TRAP no longer seizes the whole screen with the kernel debug console -
  // it prints to the console / host log.txt (which the editor tails into a
  // copyable error dialog) and then halts quietly. Set true to restore the
  // upstream on-screen dump (useful when debugging a standalone build on real
  // hardware, with no editor and no console attached). See debug/debug.hpp.
  static bool drawAssertScreen;

  /** Called by engine */
  void update();

  const u32& getFps() const { return fps; };

  /** @return Available RAM in MB */
  float getAvailableRAM();

 private:
  float calcFps();
  void* allocateLargestFreeRAMBlock(size_t* size);
  size_t getFreeRAMSize();

  u8 fpsDelayer;
  u32 fps;
  Timer timer;
};

}  // namespace Tyra

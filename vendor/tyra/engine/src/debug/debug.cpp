/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Wellington Carvalho <wellcoj@gmail.com>
*/

#include "debug/debug.hpp"

// Modified by tyra-editor: never append the log to read-only media. When the
// game boots from a disc image the cwd is cdrom0: and opening
// "cdrom0:LOG.TXT;1" for write wedges the CDVD driver - the game hangs on its
// very first TYRA_LOG, before drawing a frame.
void TyraDebug::writeInLogFile(std::stringstream* ss) {
  static int writable = -1;  // -1 = unknown, 0 = read-only medium, 1 = ok
  if (writable == -1)
    writable = Tyra::FileUtils::getCwd().rfind("cdrom", 0) == 0 ? 0 : 1;
  if (!writable) return;
  std::ofstream logFile;
  logFile.open(Tyra::FileUtils::fromCwd("log.txt"),
               std::ofstream::out | std::ofstream::app);
  logFile << ss->str();
  logFile.flush();
  // logFile.close();
}

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Wellington Carvalho <wellcoj@gmail.com>
# Modified by tyra-editor: keepIopResident flag (run under ps2link)
*/

#pragma once

#include <tamtypes.h>

namespace Tyra {

class IrxLoader {
 public:
  IrxLoader();
  ~IrxLoader();

  // Skip the SifIopReset in the constructor. Required when the game is
  // launched over the network by ps2link (ps2client execee): the reset would
  // unload ps2link from the IOP and kill the host: filesystem mid-boot.
  // Must be set before the Engine (and thus this loader) is constructed.
  // The generated games set it when started with a "-ps2link" argument.
  static bool keepIopResident;

  void loadAll(const bool& withUsb, const bool& isLoggingToFile);

 private:
  static bool isLoaded;

  void loadSio2man(const bool& verbose);
  void loadPadman(const bool& verbose);
  void loadLibsd(const bool& verbose);
  void loadIO(const bool& verbose);
  void loadUsbModules(const bool& verbose);
  void loadAudsrv(const bool& verbose);

  int applyRpcPatches();
  void waitUntilUsbDeviceIsReady();
  void delay(int count);
};

}  // namespace Tyra

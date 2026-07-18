/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Wellington Carvalho <wellcoj@gmail.com>
# Modified by TyraX: optional second pad (port parametric, non-blocking,
# hot-join) for two-player games.
*/

#include <kernel.h>
#include <libpad.h>

#pragma once

namespace Tyra {

struct PadButtons {
  u8 Cross, Square, Triangle, Circle, DpadUp, DpadDown, DpadLeft, DpadRight, L1,
      L2, L3, R1, R2, R3, Start, Select;
};

struct PadJoy {
  u8 h, v, isCentered, isMoved;
};

/** Class responsible for player pad */
class Pad {
 public:
  Pad();
  ~Pad();

  void init();
  /**
   * Modified by TyraX: open an additional pad without requiring a controller.
   * A missing/unplugged controller never blocks or asserts; update() keeps
   * polling so a controller plugged in mid-game starts reporting (hot-join).
   * @param t_port 0 -> Connector 1, 1 -> Connector 2
   * @param t_slot Always zero if not using multitap
   */
  void initOptional(const int& t_port, const int& t_slot = 0);
  void update();

  /** Modified by TyraX: false while an optional pad has no controller. */
  inline bool isConnected() const { return connected; }
  inline int getPort() const { return port; }

  inline const PadButtons& getClicked() const { return clicked; }
  inline const PadButtons& getPressed() const { return pressed; }
  inline const PadJoy& getLeftJoyPad() const { return leftJoyPad; }
  inline const PadJoy& getRightJoyPad() const { return rightJoyPad; }

 private:
  char padBuf[256] alignas(sizeof(char) * 256);
  char actAlign[6];
  int actuators, ret, port, slot;
  padButtonStatus buttons;
  u32 padData, oldPad, newPad;
  PadButtons pressed, clicked;
  PadJoy leftJoyPad, rightJoyPad;
  bool optional, opened, ready, connected;

  void reset();
  void resetJoys();
  void handleClickedButtons();
  void handlePressedButtons();
  int waitPadReady();
  int waitPadReadyBounded();
  int initPad();
  int initPadSoft();
  static void ensurePadmanInit();
};

}  // namespace Tyra
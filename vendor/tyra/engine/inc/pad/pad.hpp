/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Wellington Carvalho <wellcoj@gmail.com>
# Modified by TyraX: injectVirtual - overlay keyboard/mouse input on the pad
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
  void update();

  inline const PadButtons& getClicked() const { return clicked; }
  inline const PadButtons& getPressed() const { return pressed; }
  inline const PadJoy& getLeftJoyPad() const { return leftJoyPad; }
  inline const PadJoy& getRightJoyPad() const { return rightJoyPad; }

  /** TyraX: overlay virtual input (e.g. USB keyboard mapped to buttons)
   * on top of the physical pad. Call once per frame, after update() -
   * update() rebuilds the state from hardware, so a skipped frame simply
   * drops the overlay. held = buttons currently down; the joy args are
   * -127..127 offsets added to the stick axes (0 = leave alone). Click
   * edges are derived from the previous overlay internally. */
  void injectVirtual(const PadButtons& held, s16 leftJoyH, s16 leftJoyV,
                     s16 rightJoyH, s16 rightJoyV);

 private:
  char padBuf[256] alignas(sizeof(char) * 256);
  char actAlign[6];
  int actuators, ret, port, slot;
  padButtonStatus buttons;
  u32 padData, oldPad, newPad;
  PadButtons pressed, clicked;
  PadButtons virtPrev;  // TyraX: last frame's injectVirtual held set
  PadJoy leftJoyPad, rightJoyPad;

  void reset();
  void handleClickedButtons();
  void handlePressedButtons();
  int waitPadReady();
  int initPad();
};

}  // namespace Tyra
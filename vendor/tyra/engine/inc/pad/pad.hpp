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
# Modified by TyraX: optional second pad (port parametric, non-blocking,
# hot-join) for two-player games.
*/

// Modified by TyraX: setActuators() - runtime DualShock vibration control.

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

  /** Drives the DualShock vibration motors. smallMotor is the on/off buzz
   * engine, bigPower the heavy motor's strength (0 = off, 255 = full).
   * The state persists until the next call; no-op without actuators. */
  void setActuators(const bool& smallMotor, const u8& bigPower);

  inline const PadButtons& getClicked() const { return clicked; }
  inline const PadButtons& getPressed() const { return pressed; }
  inline const PadJoy& getLeftJoyPad() const { return leftJoyPad; }
  inline const PadJoy& getRightJoyPad() const { return rightJoyPad; }

  /** TyraX: overlay virtual input (e.g. USB keyboard mapped to buttons)
   * on top of the physical pad. Call once per frame, after update() -
   * update() rebuilds the state from hardware, so a skipped frame simply
   * drops the overlay. held = buttons currently down; the joy args are
   * -127..127 offsets added to the stick axes (0 = leave alone). Click
   * edges are derived from the previous overlay internally.
   *
   * `slot` picks which overlay's history the click edges come from, so two
   * independent virtual sources can both inject in one frame: 0 = the USB
   * keyboard/mouse fold, 1 = the editor's Remote Pad (docs/remote-pad.md).
   * Sharing a slot between two sources makes each call look like the other
   * one released everything, which turns held buttons into a click every
   * frame - hence one slot per source, not one shared previous state. */
  void injectVirtual(const PadButtons& held, s16 leftJoyH, s16 leftJoyV,
                     s16 rightJoyH, s16 rightJoyV, u8 slot = 0);

  /** Overlay slots - see injectVirtual. */
  static const int VIRT_SLOTS = 2;

  /** TyraX: OVERWRITE the whole polled state (docs/input-replay.md).
   *
   * injectVirtual MERGES on top of the hardware, which is right for a pad
   * overlay and wrong for a replay: a recording has to WIN over whatever a
   * physical controller is doing, or a hand resting on a stick silently
   * changes the run being reproduced. So this replaces `pressed`, `clicked`
   * and both stick axes outright, and recomputes isCentered/isMoved by the
   * same rule injectVirtual uses.
   *
   * Call it as the LAST stage of a frame's input - after update() and after
   * every overlay - or whatever runs later overwrites it back. */
  void setState(const PadButtons& pressedIn, const PadButtons& clickedIn,
                u8 leftH, u8 leftV, u8 rightH, u8 rightV);

 private:
  /** TyraX: how long waitPadReady() gives a controller to settle, in ~1 ms
   * polls. Generous, because it only ever costs this much when there is no
   * usable pad - and then the game boots without one instead of hanging. */
  static const int PAD_READY_POLLS = 3000;

  char padBuf[256] alignas(sizeof(char) * 256);
  char actAlign[6];
  int actuators, ret, port, slot;
  padButtonStatus buttons;
  u32 padData, oldPad, newPad;
  PadButtons pressed, clicked;
  // TyraX: last frame's injectVirtual held set, per overlay slot.
  PadButtons virtPrev[VIRT_SLOTS];
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
/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: USB keyboard + mouse input (ps2kbd/ps2mouse drivers)
*/

#pragma once

#include <tamtypes.h>

namespace Tyra {

/** Per-frame USB mouse state. x/y/wheel are deltas since the previous
 * update() (the driver runs in DIFF mode); buttons use the PS2MOUSE_BTN*
 * bit layout: bit 0 = left, bit 1 = right, bit 2 = middle. */
struct MouseState {
  int dx, dy, wheel;
  u8 buttons;  /** currently held */
  u8 clicked;  /** went down this frame */
};

/**
 * USB keyboard + mouse (TyraX fork addition).
 *
 * Dormant unless EngineOptions::loadUsbKbdMouse made the IrxLoader load
 * usbd + ps2kbd + ps2mouse and Engine called init(); every query then
 * returns "no input". Devices hot-plug: the drivers enumerate whatever
 * appears on the USB ports, reads just return nothing while absent.
 *
 * Keys are USB HID usage codes (the table in the USB HID Usage Tables
 * spec, e.g. 0x1A = W, 0x2C = Space, 0xE1 = LeftShift) - the same codes
 * the ps2kbd raw read mode reports.
 */
class KbdMouse {
 public:
  KbdMouse();

  /** Open the drivers. Call once, after the IRX modules are loaded.
   * underPs2Link=true means the drivers are not ours but the custom TyraX
   * ps2link's resident ones; the mouse is then only initialised if the
   * keyboard device opened (proof that stack is really there). Without that
   * guard, PS2MouseInit on a stock ps2link spins forever binding an RPC
   * server that never registered and hangs the boot on the Tyra logo. */
  void init(bool underPs2Link = false);

  /** Poll both devices. Called by Engine::realLoop() once per frame,
   * before the game loop runs. No-op when init() was skipped/failed. */
  void update();

  /** True when init() opened at least one driver (keyboard or mouse). */
  bool isEnabled() const { return kbdOk || mouseOk; }

  /** Key currently held (USB HID usage code). */
  bool isKeyDown(const u8& usbCode) const {
    return (held[usbCode >> 3] >> (usbCode & 7)) & 1;
  }

  /** Key went down this frame (USB HID usage code). */
  bool isKeyClicked(const u8& usbCode) const {
    return (clicked[usbCode >> 3] >> (usbCode & 7)) & 1;
  }

  const MouseState& getMouse() const { return mouse; }

 private:
  bool kbdOk, mouseOk;
  u8 held[32], clicked[32];  // 256-bit key bitmaps
  MouseState mouse;
  u8 prevButtons;
};

}  // namespace Tyra

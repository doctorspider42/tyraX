/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: USB keyboard + mouse input (ps2kbd/ps2mouse drivers)
*/

#include <string.h>
#include <libkbd.h>
#include <libmouse.h>
#include "pad/kbd_mouse.hpp"
#include "debug/debug.hpp"

namespace Tyra {

KbdMouse::KbdMouse() {
  kbdOk = false;
  mouseOk = false;
  memset(held, 0, sizeof(held));
  memset(clicked, 0, sizeof(clicked));
  memset(&mouse, 0, sizeof(mouse));
  prevButtons = 0;
}

void KbdMouse::init() {
  // PS2KbdInit: 1 = opened, 2 = already open, 0 = device file missing
  // (driver not loaded). The driver defaults to non-blocking reads, so a
  // frame with no key events costs one empty read.
  kbdOk = PS2KbdInit() > 0;
  if (kbdOk) {
    PS2KbdSetReadmode(PS2KBD_READMODE_RAW);
    TYRA_LOG("KbdMouse: keyboard driver ready");
  }

  // PS2MouseInit would spin forever binding the RPC if ps2mouse.irx were
  // absent - only reached when the IrxLoader loaded it. Default read mode
  // is DIFF: x/y/wheel accumulate on the IOP and reset on every read.
  mouseOk = PS2MouseInit() >= 0;
  if (mouseOk) TYRA_LOG("KbdMouse: mouse driver ready");
}

void KbdMouse::update() {
  memset(clicked, 0, sizeof(clicked));
  mouse.dx = 0;
  mouse.dy = 0;
  mouse.wheel = 0;
  mouse.clicked = 0;

  if (kbdOk) {
    PS2KbdRawKey key;
    while (PS2KbdReadRaw(&key) > 0) {
      const u8 byteIdx = key.key >> 3;
      const u8 bit = 1 << (key.key & 7);
      if (key.state == PS2KBD_RAWKEY_DOWN) {
        if (!(held[byteIdx] & bit)) clicked[byteIdx] |= bit;
        held[byteIdx] |= bit;
      } else {
        held[byteIdx] &= ~bit;
      }
    }
  }

  if (mouseOk) {
    PS2MouseData data;
    if (PS2MouseRead(&data) >= 0) {
      mouse.dx = data.x;
      mouse.dy = data.y;
      mouse.wheel = data.wheel;
      mouse.buttons = data.buttons & 0x07;  // strip double-click bits
      mouse.clicked = mouse.buttons & ~prevButtons;
      prevButtons = mouse.buttons;
    }
  }
}

}  // namespace Tyra

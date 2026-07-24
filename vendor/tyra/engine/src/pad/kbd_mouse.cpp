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

void KbdMouse::init(bool withMouse) {
  // PS2KbdInit: 1 = opened, 2 = already open, 0 = device file missing
  // (driver not loaded). The driver defaults to non-blocking reads, so a
  // frame with no key events costs one empty read.
  kbdOk = PS2KbdInit() > 0;
  if (kbdOk) {
    PS2KbdSetReadmode(PS2KBD_READMODE_RAW);
    TYRA_LOG("KbdMouse: keyboard driver ready");
  } else {
    // Driver file missing / RPC never bound. On real hardware this usually
    // means no usbd is serving the port (e.g. a network-booted ps2link) or
    // the keyboard did not enumerate - not a code bug. Logged so the cause
    // is visible over the EE console / bin/log.txt.
    TYRA_LOG("KbdMouse: keyboard driver NOT ready (no device / no usbd?)");
  }

  // PS2MouseInit binds the ps2mouse RPC server in a while(server==0) spin.
  // With ps2mouse.irx loaded off ps2link that returns at once (DIFF read
  // mode: x/y/wheel accumulate on the IOP and reset on every read). Under a
  // resident-IOP ps2link the RPC server never comes up and the spin never
  // ends - so withMouse=false there (keyboard only) to keep the boot alive.
  if (withMouse) {
    mouseOk = PS2MouseInit() >= 0;
    if (mouseOk) {
      // Force relative deltas. We treat every read as a per-frame delta (the
      // walkers add dx/dy straight to yaw/pitch); in ABS mode PS2MouseRead
      // returns the accumulated absolute position instead, which reads as a
      // large constant delta and spins the camera forever. Don't trust the
      // driver default - set it explicitly.
      PS2MouseSetReadMode(PS2MOUSE_READMODE_DIFF);
      TYRA_LOG("KbdMouse: mouse driver ready");
    } else {
      TYRA_LOG("KbdMouse: mouse driver NOT ready (no device / no usbd?)");
    }
  } else {
    mouseOk = false;
    TYRA_LOG("KbdMouse: mouse skipped (keyboard only under ps2link)");
  }
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
    // Zero-init: a real USB mouse only sends packets on activity, so on a
    // still frame PS2MouseRead can report success without writing the struct.
    // Left uninitialised that was stack garbage read as a constant delta -
    // the camera orbited on its own on hardware (PCSX2's emulated mouse
    // delivers a packet every frame, so it never showed there).
    PS2MouseData data = {};
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

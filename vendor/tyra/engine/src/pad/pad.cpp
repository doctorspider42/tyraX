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
# hot-join) for two-player games. padInit() is called once globally.
*/

#include <tamtypes.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include "debug/debug.hpp"
#include "pad/pad.hpp"

namespace Tyra {

/** Init vars, load modules, opens pad port and initializes pad */
Pad::Pad() {
  optional = false;
  opened = false;
  ready = false;
  connected = false;
  oldPad = 0;
  resetJoys();
}

Pad::~Pad() {}

// ----
// Methods
// ----

/** padInit initializes the whole padman library, not a port - it must run
 * exactly once no matter how many Pad instances open ports. */
void Pad::ensurePadmanInit() {
  static bool done = false;
  if (done) return;
  padInit(0);
  done = true;
}

void Pad::init() {
  this->oldPad = 0;
  ensurePadmanInit();
  this->port = 0;  // 0 -> Connector 1, 1 -> Connector 2
  this->slot = 0;  // Always zero if not using multitap

  this->ret = padPortOpen(this->port, this->slot, padBuf);
  TYRA_ASSERT(this->ret != 0,
              "padPortOpen failed! padPortOpen returned: ", this->ret);
  TYRA_ASSERT(this->initPad(), "initPad failed!");
  this->opened = true;
  this->ready = true;
  this->connected = true;
}

void Pad::initOptional(const int& t_port, const int& t_slot) {
  this->oldPad = 0;
  ensurePadmanInit();
  this->optional = true;
  this->port = t_port;
  this->slot = t_slot;
  this->ret = padPortOpen(this->port, this->slot, padBuf);
  this->opened = this->ret != 0;
  if (!this->opened)
    printf("Pad(%d, %d) padPortOpen failed (%d) - will retry\n", this->port,
           this->slot, this->ret);
}

/** Wait when pad will be ready (stable and ready) */
int Pad::waitPadReady() {
  int state;
  int lastState;
  char* stateString = new char[16];
  state = padGetState(this->port, this->slot);
  lastState = -1;
  while ((state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1)) {
    if (state != lastState) {
      padStateInt2String(state, stateString);
      TYRA_LOG("Pad state changed");
      printf("Curent pad(%d,%d) status: %s\n", this->port, this->slot,
             stateString);
    }
    lastState = state;
    state = padGetState(this->port, this->slot);
  }
  // Were the pad ever 'out of sync'?
  if (lastState != -1) TYRA_LOG("Pad is ready!");

  delete[] stateString;
  return 0;
}

/** Like waitPadReady, but gives up instead of spinning forever - an optional
 * pad can be unplugged in the middle of its mode setup. */
int Pad::waitPadReadyBounded() {
  for (int i = 0; i < 200000; i++) {
    int state = padGetState(this->port, this->slot);
    if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) return 0;
    if (state == PAD_STATE_DISCONN) return -1;
  }
  return -1;
}

/** Initializes and checks type of pad */
int Pad::initPad() {
  TYRA_LOG("Initializing pad");
  this->waitPadReady();

  int modes = padInfoMode(this->port, this->slot, PAD_MODETABLE, -1);
  TYRA_ASSERT(modes, "Connected device is not a dual shock controller!");

  // Verify that the controller has a DUAL SHOCK mode
  int i = 0;
  do {
    if (padInfoMode(this->port, this->slot, PAD_MODETABLE, i) ==
        PAD_TYPE_DUALSHOCK)
      break;
    i++;
  } while (i < modes);

  TYRA_ASSERT(i < modes, "Connected device is not a dual shock controller!");

  // If ExId != 0x0 => This controller has actuator engines
  // This check should always pass if the Dual Shock test above passed
  this->ret = padInfoMode(this->port, this->slot, PAD_MODECUREXID, 0);
  TYRA_ASSERT(this->ret, "Connected device is not a dual shock controller!");

  TYRA_LOG("Enabling dual shock functions.");

  // When using MMODE_LOCK, user cant change mode with Select button
  padSetMainMode(this->port, this->slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

  this->waitPadReady();
  TYRA_LOG("Pad has pressure sensitive buttons? ",
           padInfoPressMode(this->port, this->slot));
  this->waitPadReady();
  padEnterPressMode(this->port, this->slot);  // Set pressure sensitive mode
  this->waitPadReady();
  this->actuators = padInfoAct(this->port, this->slot, -1, 0);
  TYRA_LOG("# of actuators: ", this->actuators);
  if (actuators != 0) {
    this->actAlign[0] = 0;  // Enable small engine
    this->actAlign[1] = 1;  // Enable big engine
    this->actAlign[2] = 0xff;
    this->actAlign[3] = 0xff;
    this->actAlign[4] = 0xff;
    this->actAlign[5] = 0xff;
    this->waitPadReady();
    TYRA_LOG("padSetActAlign: ",
             padSetActAlign(this->port, this->slot, actAlign));
  } else
    TYRA_LOG("Did not find any actuators.");
  this->waitPadReady();
  TYRA_LOG("Pad initialized!");
  return 1;
}

/** initPad without the DualShock asserts and with bounded waits, so an
 * absent/odd controller on an optional port degrades instead of halting.
 * Returns 1 when the mode setup completed. */
int Pad::initPadSoft() {
  if (this->waitPadReadyBounded() != 0) return 0;
  int modes = padInfoMode(this->port, this->slot, PAD_MODETABLE, -1);
  if (modes > 0) {
    padSetMainMode(this->port, this->slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    if (this->waitPadReadyBounded() != 0) return 0;
    padEnterPressMode(this->port, this->slot);
    if (this->waitPadReadyBounded() != 0) return 0;
    this->actuators = padInfoAct(this->port, this->slot, -1, 0);
    if (this->actuators != 0) {
      this->actAlign[0] = 0;
      this->actAlign[1] = 1;
      this->actAlign[2] = 0xff;
      this->actAlign[3] = 0xff;
      this->actAlign[4] = 0xff;
      this->actAlign[5] = 0xff;
      padSetActAlign(this->port, this->slot, actAlign);
      if (this->waitPadReadyBounded() != 0) return 0;
    }
  }
  TYRA_LOG("Optional pad initialized (port ", this->port, ")");
  return 1;
}

/** Updates state of joys/buttons. Called by engine */
void Pad::update() {
  if (this->optional) {
    if (!this->opened) {
      this->ret = padPortOpen(this->port, this->slot, padBuf);
      this->opened = this->ret != 0;
      if (!this->opened) return;
    }
    int state = padGetState(this->port, this->slot);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
      // No controller (or mid-plug): report a centered, silent pad and keep
      // polling - this is what makes mid-game hot-join work.
      this->connected = false;
      this->ready = false;
      this->oldPad = 0;
      this->reset();
      this->resetJoys();
      return;
    }
    if (!this->ready) {
      this->ready = this->initPadSoft() != 0;
      if (!this->ready) return;
    }
    this->connected = true;
  } else {
    int x = 0;
    this->ret = padGetState(this->port, this->slot);
    while ((this->ret != PAD_STATE_STABLE) &&
           (this->ret != PAD_STATE_FINDCTP1)) {
      if (this->ret == PAD_STATE_DISCONN)
        printf("Pad(%d, %d) is disconnected\n", this->port, this->slot);
      this->ret = padGetState(this->port, this->slot);
    }
    if (x == 1) TYRA_LOG("Pad: OK!\n");
  }

  this->ret = padRead(this->port, this->slot, &this->buttons);

  if (this->ret != 0) {
    this->padData = 0xffff ^ this->buttons.btns;

    this->newPad = this->padData & ~this->oldPad;
    this->oldPad = this->padData;
    this->reset();

    // Digital buttons
    this->rightJoyPad.h = this->buttons.rjoy_h;
    this->rightJoyPad.v = this->buttons.rjoy_v;
    this->leftJoyPad.h = this->buttons.ljoy_h;
    this->leftJoyPad.v = this->buttons.ljoy_v;

    this->rightJoyPad.isCentered =
        this->buttons.rjoy_h == 127 && this->buttons.rjoy_v == 127;
    this->rightJoyPad.isMoved = !this->rightJoyPad.isCentered;

    this->leftJoyPad.isCentered =
        this->buttons.ljoy_h == 127 && this->buttons.ljoy_v == 127;
    this->leftJoyPad.isMoved = !this->leftJoyPad.isCentered;

    this->handleClickedButtons();
    this->handlePressedButtons();
  }
}

/** Update clicked buttons state */
void Pad::handleClickedButtons() {
  if (this->newPad & PAD_CROSS) this->clicked.Cross = 1;
  if (this->newPad & PAD_SQUARE) this->clicked.Square = 1;
  if (this->newPad & PAD_TRIANGLE) this->clicked.Triangle = 1;
  if (this->newPad & PAD_CIRCLE) this->clicked.Circle = 1;
  if (this->newPad & PAD_UP) this->clicked.DpadUp = 1;
  if (this->newPad & PAD_DOWN) this->clicked.DpadDown = 1;
  if (this->newPad & PAD_LEFT) this->clicked.DpadLeft = 1;
  if (this->newPad & PAD_RIGHT) this->clicked.DpadRight = 1;
  if (this->newPad & PAD_L1) this->clicked.L1 = 1;
  if (this->newPad & PAD_L2) this->clicked.L2 = 1;
  if (this->newPad & PAD_L3) this->clicked.L3 = 1;
  if (this->newPad & PAD_R1) this->clicked.R1 = 1;
  if (this->newPad & PAD_R2) this->clicked.R2 = 1;
  if (this->newPad & PAD_R3) this->clicked.R3 = 1;
  if (this->newPad & PAD_START) this->clicked.Start = 1;
  if (this->newPad & PAD_SELECT) this->clicked.Select = 1;
}

/** Update pressed buttons state */
void Pad::handlePressedButtons() {
  if (this->buttons.cross_p) this->pressed.Cross = 1;
  if (this->buttons.square_p) this->pressed.Square = 1;
  if (this->buttons.triangle_p) this->pressed.Triangle = 1;
  if (this->buttons.circle_p) this->pressed.Circle = 1;
  if (this->buttons.up_p) this->pressed.DpadUp = 1;
  if (this->buttons.down_p) this->pressed.DpadDown = 1;
  if (this->buttons.left_p) this->pressed.DpadLeft = 1;
  if (this->buttons.right_p) this->pressed.DpadRight = 1;
  if (this->buttons.l1_p) this->pressed.L1 = 1;
  if (this->buttons.l2_p) this->pressed.L2 = 1;
  if (this->buttons.r1_p) this->pressed.R1 = 1;
  if (this->buttons.r2_p) this->pressed.R2 = 1;
}

/** Resets state of joys/buttons */
void Pad::reset() {
  this->clicked.Cross = 0;
  this->clicked.Square = 0;
  this->clicked.Triangle = 0;
  this->clicked.Circle = 0;
  this->clicked.DpadUp = 0;
  this->clicked.DpadDown = 0;
  this->clicked.DpadLeft = 0;
  this->clicked.DpadRight = 0;
  this->clicked.L1 = 0;
  this->clicked.L2 = 0;
  this->clicked.L3 = 0;
  this->clicked.R1 = 0;
  this->clicked.R2 = 0;
  this->clicked.R3 = 0;
  this->clicked.Start = 0;
  this->clicked.Select = 0;

  this->pressed.Cross = 0;
  this->pressed.Square = 0;
  this->pressed.Triangle = 0;
  this->pressed.Circle = 0;
  this->pressed.DpadUp = 0;
  this->pressed.DpadDown = 0;
  this->pressed.DpadLeft = 0;
  this->pressed.DpadRight = 0;
  this->pressed.L1 = 0;
  this->pressed.L2 = 0;
  this->pressed.R1 = 0;
  this->pressed.R2 = 0;
}

/** Center both sticks - the resting state a game must see for a pad that has
 * no controller attached (fresh Pad members are otherwise uninitialized). */
void Pad::resetJoys() {
  this->leftJoyPad.h = 127;
  this->leftJoyPad.v = 127;
  this->leftJoyPad.isCentered = 1;
  this->leftJoyPad.isMoved = 0;
  this->rightJoyPad.h = 127;
  this->rightJoyPad.v = 127;
  this->rightJoyPad.isCentered = 1;
  this->rightJoyPad.isMoved = 0;
}

}  // Namespace Tyra
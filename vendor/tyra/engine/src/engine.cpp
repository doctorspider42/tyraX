/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: demote the main thread below the audio threads;
#                    USB keyboard/mouse device (kbdMouse)
*/

#include "engine.hpp"
#include <kernel.h>

namespace Tyra {

Engine::Engine() { initAll(EngineOptions()); }

Engine::Engine(const EngineOptions& options) {
  info.writeLogsToFile = options.writeLogsToFile;
  initAll(options);
}

Engine::~Engine() {}

void Engine::run(Game* t_game) {
  game = t_game;
  game->init();
  while (true) {
    realLoop();
  }
}

void Engine::realLoop() {
  pad.update();
  if (kbdMouse.isEnabled()) kbdMouse.update();
  game->loop();
  info.update();
}

void Engine::initAll(const EngineOptions& options) {
  // The audio thread (0x5) and the song streamer (0x6) must be able to
  // PREEMPT the game: depending on the boot path the main thread arrives
  // here at priority 0 (nothing preempts it) or whatever the loader used,
  // and the engine's GS waits are busy-spins that never yield the CPU. A
  // compute-bound frame then starves the audio threads and streamed music
  // audibly drags exactly when the game dips below full frame rate. The
  // audio threads cost microseconds per wake, so the game loses nothing.
  //
  // The value must stay numerically ABOVE 20: ps2link's EE command thread
  // (the one that services "ps2client reset"/execee while a game runs) is
  // priority 20, and the EE scheduler is strictly priority-based. An earlier
  // 0x10 here starved it forever - the first network deploy worked, but Stop
  // and every redeploy hung because the console never processed the reset.
  ChangeThreadPriority(GetThreadId(), 0x40);

  srand(time(nullptr));
  // No keyboard/mouse under ps2link by default: a USB-booted ps2link already
  // runs usbd, and loading a second USB stack wedges the first. Skipping the
  // drivers also means PS2MouseInit() must not run - without a usbd, ps2mouse
  // self-unloads and PS2MouseInit spins forever binding an RPC server that is
  // gone. The experimental loadUsbKbdMouseUnderPs2Link override forces them on
  // anyway (debug aid for the network-deploy dev loop): a NETWORK-booted
  // ps2link has no usbd resident, so loadAll loads its own and the HID drivers
  // attach to it. On a USB-booted ps2link the override may wedge - boot the
  // game from that USB instead.
  const bool underPs2Link = IrxLoader::keepIopResident;
  const bool withKbdMouse =
      options.loadUsbKbdMouse &&
      (!underPs2Link || options.loadUsbKbdMouseUnderPs2Link);
  // A CUSTOM ps2link with usbd+ps2kbd+ps2mouse baked in already has the whole
  // stack resident (RPC servers registered at its clean boot): reuse it. Do
  // NOT load our own (a second usbd would wedge it), and the mouse works too
  // because PS2MouseInit binds the already-registered server instead of
  // spinning. Stock ps2link (no USB) keeps the load-our-own, keyboard-only
  // path.
  const bool reuseResidentHid =
      withKbdMouse && underPs2Link && options.ps2LinkHasUsbHid;
  const bool loadOwnHid = withKbdMouse && !reuseResidentHid;
  irx.loadAll(options.loadUsbDriver, loadOwnHid, info.writeLogsToFile);
  renderer.init(options.videoMode, options.displayMode, options.widescreen);
  banner.show(&renderer);
  audio.init();
  pad.init();
  // Mouse runs off ps2link (PCSX2 / exported ISO) and on a custom ps2link with
  // the stack resident; it is skipped only on a stock ps2link where we loaded
  // our own drivers - there PS2MouseInit spins forever (see KbdMouse::init).
  if (withKbdMouse) kbdMouse.init(!underPs2Link || reuseResidentHid);
}

}  // namespace Tyra

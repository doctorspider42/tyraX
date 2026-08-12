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
  // No keyboard/mouse under ps2link by default: USB drivers cannot be added
  // safely to a ps2link that is already running (its IOP is never reset, and
  // ps2kbd/ps2mouse need a usbd that a network-booted ps2link does not carry).
  // The loadUsbKbdMouseUnderPs2Link override targets the TyraX ps2link
  // (tools/ps2link), which bakes usbd + ps2kbd + ps2mouse into its OWN boot:
  // we then reuse that resident stack and load nothing of our own - a second
  // usbd would wedge it. The editor deploys to no other ps2link, so it turns
  // the override on by default (docs/ps2link-setup.md).
  const bool underPs2Link = IrxLoader::keepIopResident;
  const bool withKbdMouse =
      options.loadUsbKbdMouse &&
      (!underPs2Link || options.loadUsbKbdMouseUnderPs2Link);
  const bool loadOwnHid = withKbdMouse && !underPs2Link;
  irx.loadAll(options.loadUsbDriver, loadOwnHid, info.writeLogsToFile);
  renderer.init(options.videoMode, options.displayMode, options.widescreen,
                options.tripleBuffering);
  banner.show(&renderer);
  audio.init();
  pad.init();
  if (withKbdMouse) kbdMouse.init(underPs2Link);
}

}  // namespace Tyra

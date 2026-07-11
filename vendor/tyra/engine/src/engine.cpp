/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by tyra-editor: demote the main thread below the audio threads
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
  irx.loadAll(options.loadUsbDriver, info.writeLogsToFile);
  renderer.init(options.videoMode);
  banner.show(&renderer);
  audio.init();
  pad.init();
}

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "engine.hpp"

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
  srand(time(nullptr));
  irx.loadAll(options.loadUsbDriver, info.writeLogsToFile);
  renderer.init(options.videoMode);
  banner.show(&renderer);
  audio.init();
  pad.init();
}

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include "./renderer/renderer.hpp"
#include "./pad/pad.hpp"
#include "./audio/audio.hpp"
#include "./irx/irx_loader.hpp"
#include "./info/info.hpp"
#include "./info/banner.hpp"
#include "./game.hpp"

namespace Tyra {

struct EngineOptions {
  /**
   * True -> logs will be written to file.
   * False -> logs will be displayed in console
   */
  bool writeLogsToFile = false;

  bool loadUsbDriver = false;

  /** Forced output video signal; Auto follows the console region. */
  VideoMode videoMode = VideoMode::Auto;

  /** Output scan mode (tyra-editor fork): stock interlaced 480i/576i,
   * progressive 480p, or 1080i. The DTV modes need component cables on
   * real hardware and always run at 60 Hz (videoMode only picks the
   * region/refresh of the interlaced mode). */
  DisplayMode displayMode = DisplayMode::Interlaced;
};

class Engine {
 public:
  Engine();
  Engine(const EngineOptions& options);
  ~Engine();

  Renderer renderer;
  Pad pad;
  Audio audio;
  Info info;

  void run(Game* t_game);

 private:
  IrxLoader irx;

  Game* game;
  Banner banner;

  void realLoop();
  void initAll(const EngineOptions& options);
};

}  // namespace Tyra

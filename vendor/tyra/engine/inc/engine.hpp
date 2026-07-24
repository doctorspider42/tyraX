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
#include "./pad/kbd_mouse.hpp"
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

  /** Load the USB keyboard/mouse drivers (TyraX fork) and poll them each
   * frame through Engine::kbdMouse. Games map that state onto the pad
   * (Pad::injectVirtual) / camera themselves. */
  bool loadUsbKbdMouse = false;

  /** Experimental (TyraX fork): keep the keyboard/mouse drivers on even under
   * a ps2link deploy, where loadUsbKbdMouse is otherwise ignored. The engine
   * then does NOT load its own usbd (a second one wedges ps2link's resident
   * stack); it reuses the resident usbd and only adds ps2kbd/ps2mouse - so it
   * only works if that IOP actually carries a usbd (ps2link booted from USB).
   * A debug aid: the driver-load logs reach the EE console live. */
  bool loadUsbKbdMouseUnderPs2Link = false;

  /** Forced output video signal; Auto follows the console region. */
  VideoMode videoMode = VideoMode::Auto;

  /** Output scan mode (TyraX fork): stock interlaced 480i/576i,
   * progressive 480p, 1080i, interlaced with true field rendering
   * (half-height buffers, a fresh image per field - see
   * DisplayMode::InterlacedField), or the full-height 512-line PAL frame
   * (DisplayMode::Pal576i, always a 50 Hz PAL signal). The DTV modes need
   * component cables on real hardware and always run at 60 Hz (videoMode
   * only picks the region/refresh of the region-following interlaced
   * modes). */
  DisplayMode displayMode = DisplayMode::Interlaced;

  /** 16:9 anamorphic output (TyraX fork): widen the projection for a
   * widescreen display. Both can also be changed at runtime through
   * Renderer::core.setDisplayOutput. */
  bool widescreen = false;
};

class Engine {
 public:
  Engine();
  Engine(const EngineOptions& options);
  ~Engine();

  Renderer renderer;
  Pad pad;
  KbdMouse kbdMouse;
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

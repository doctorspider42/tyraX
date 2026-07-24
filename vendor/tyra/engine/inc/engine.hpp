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
   * a ps2link deploy, where loadUsbKbdMouse is otherwise ignored. Intended for
   * the network-deploy dev loop: a network-booted ps2link (SMAP/dev9) carries
   * no usbd, so the engine loads its own plus ps2kbd/ps2mouse and the EE
   * console shows the load live. On a USB-booted ps2link (usbd already
   * resident) this may wedge the USB stack - boot the game from that USB
   * instead. */
  bool loadUsbKbdMouseUnderPs2Link = false;

  /** Experimental (TyraX fork): set with loadUsbKbdMouseUnderPs2Link when the
   * ps2link in use is a CUSTOM build with usbd+ps2kbd+ps2mouse already baked
   * in. The engine then REUSES that resident stack (loads none of its own - a
   * second usbd would wedge it) and enables the mouse: its RPC server
   * registered at ps2link's clean boot, so PS2MouseInit binds instead of
   * spinning. Ignored off ps2link. */
  bool ps2LinkHasUsbHid = false;

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

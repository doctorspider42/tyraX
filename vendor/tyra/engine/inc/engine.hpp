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

  /** TyraX fork: keep the keyboard/mouse drivers on even under a ps2link
   * deploy, where loadUsbKbdMouse is otherwise ignored. REQUIRES the TyraX
   * ps2link (tools/ps2link), which bakes usbd + ps2kbd + ps2mouse into its own
   * boot: the engine REUSES that resident stack and loads none of its own (a
   * second usbd would wedge it, and drivers added to a running ps2link's IOP
   * never come up cleanly). That is the only ps2link the editor deploys to, so
   * generated games set this true by default; with a stock ps2link there is
   * nothing to reuse and the drivers just report "not ready". */
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

  /**
   * Framebuffer colour depth (TyraX fork). Bits16 halves what the frame
   * buffers cost in GS memory and hands the difference to the texture heap;
   * see ColorDepth in renderer_settings.hpp for what it costs in picture
   * quality, and `dither` for the mitigation.
   */
  ColorDepth colorDepth = ColorDepth::Bits32;

  /** GS ordered dithering (TyraX fork). Only does anything at Bits16, where
   * it is what keeps skies and blurs from banding. */
  bool dither = true;

  /**
   * Reserve the dynamic env-map render target - 128 KB (TyraX fork). Only
   * projects with a reflective "@sky" material read it; the editor's codegen
   * turns it off for the rest, and then the whole feature is absent rather
   * than idle.
   */
  bool envMapTarget = true;

  /** Reserve the camera-feed render target - another 128 KB (TyraX fork).
   * Only projects with a texture feed read it. */
  bool camFeedTarget = true;
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

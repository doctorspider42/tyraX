/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Wellington Carvalho <wellcoj@gmail.com>
*/

#pragma once

#include <tamtypes.h>
#include <stddef.h>
#include "./version.hpp"
// Modified by TyraX: no longer includes time/timer.hpp - the FPS counter reads
// COP0 Count directly (see getFps). Timer is still a public engine class and
// still reachable through the <tyra> umbrella header.

namespace Tyra {

class Info {
 public:
  Info();
  ~Info();

  Version version;

  static bool writeLogsToFile;

  // Modified by TyraX: when false (the default) a failed TYRA_ASSERT /
  // TYRA_TRAP no longer seizes the whole screen with the kernel debug console -
  // it prints to the console / host log.txt (which the editor tails into a
  // copyable error dialog) and then halts quietly. Set true to restore the
  // upstream on-screen dump (useful when debugging a standalone build on real
  // hardware, with no editor and no console attached). See debug/debug.hpp.
  static bool drawAssertScreen;

  /** Called by engine */
  void update();

  /**
   * Modified by TyraX: RENDERED frames per second - loop iterations, i.e.
   * calls to update() - averaged over the last completed window of
   * kFpsWindowTicks (0.5 s). 0 until the first window closes.
   *
   * The clock is COP0 `Count` (294.912 MHz, the EE core clock), NOT the
   * H-BLNK-driven EE Timer 3 this used to read. T3 counts SCANLINES, so its
   * rate is a property of the VIDEO MODE - 15 625 Hz on PAL 576i, 15 734 on
   * NTSC 480i and about TWICE that in the progressive modes - while the
   * constant divided into it was hardcoded at PAL's 15 625. A progressive
   * project therefore read half its real frame rate here, on the HUD and in
   * the Live Debugger's Stats tab alike (docs/profiling.md, "The three frame
   * rate counters").
   *
   * Returned by VALUE and as a float: it used to be a `const u32&` fed from a
   * float, so 19.6 FPS displayed as 19.
   */
  float getFps() const { return fps; };

  /**
   * Modified by TyraX: PRESENTED frames per second over the same window -
   * every buffer flip, including ones no game loop rendered.
   *
   * Frame extrapolation presents a synthesised frame after endFrame() has
   * returned (docs/frame-extrapolation.md), so a game can show about twice
   * the rate it renders. Everything else in the engine counts rendered
   * frames; this is the only counter that answers "how often did the picture
   * change", which is the question a player is asking.
   */
  float getPresentedFps() const { return presentedFps; };

  /** True when the two above describe different things (see getPresentedFps).
   * The margin absorbs a window boundary landing between a render and its
   * warped twin. */
  bool isPresentingExtraFrames() const {
    return presentedFps > fps * 1.15F;
  }

  /**
   * Counts one buffer flip. Called by RendererCoreGS::flipBuffers for real
   * and synthesised presents alike - static because the GS layer owns no
   * pointer back to the Engine, and there is exactly one Engine per game.
   */
  static void countPresentedFrame() { ++presentCounter; }

  /** @return Available RAM in MB */
  float getAvailableRAM();

  /** COP0 Count ticks per millisecond - the same 294.912 MHz constant the
   * frame-timing rig divides by (inc/debug/frame_profile.hpp). */
  static constexpr float kTicksPerMs = 294912.0F;
  /** The averaging window, in those ticks: 0.5 s. Stated because a frame rate
   * without its window is not a measurement. */
  static constexpr u32 kFpsWindowTicks = 147456000U;

 private:
  void* allocateLargestFreeRAMBlock(size_t* size);
  size_t getFreeRAMSize();

  float fps;
  float presentedFps;
  u32 lastCount;       // COP0 Count at the previous update()
  bool primed;         // the first update() only seeds lastCount
  u64 windowTicks;     // COP0 ticks accumulated in the open window
  u32 windowFrames;    // update() calls in it
  u32 windowPresents;  // flips in it
  u32 lastPresents;    // presentCounter at the previous update()

  static u32 presentCounter;
};

}  // namespace Tyra

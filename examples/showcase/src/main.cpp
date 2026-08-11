#include <tyra>
#include <cstdio>
#include <cstring>
#include <graph.h>  // graph_get_region - the PAL-picture promotion below
#include "terrain_game.hpp"

int main(int argc, char** argv) {
  // "Run on PS2" launches this game over the network (ps2client execee).
  // ps2link stays resident on the IOP serving the host: filesystem, so the
  // Engine must not reset the IOP - that would kill it. Detection is
  // two-fold: the "-ps2link" execee argument (only delivered by toolchains
  // with a current crt0 - ps2link passes args in a non-standard way), plus
  // a "ps2link.run" marker the editor writes next to the ELF on PS2 deploys
  // and deletes on PCSX2 launches. The marker is read over host: BEFORE the
  // Engine boots: on a real PS2 host: only exists while ps2link is alive.
  bool ps2link = false;
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "-ps2link") == 0) ps2link = true;
  if (!ps2link) {
    if (FILE* marker = fopen(Tyra::FileUtils::fromCwd("ps2link.run").c_str(), "rb")) {
      fclose(marker);
      ps2link = true;
    }
  }
  Tyra::IrxLoader::keepIopResident = ps2link;

  // Route TYRA_LOG / TYRA_WARN / TYRA_ERROR and assertion dumps to a host-side
  // "log.txt" (next to the ELF) instead of the EE console, which does not
  // reach PCSX2's emulog. The TyraX Debug window tails that file. Must
  // be set before the Engine is constructed (its init logging is the first to
  // hit the file). No cost in a release (NDEBUG) build - the macros compile out.
  // Under ps2link the EE console is BETTER than the file: ps2link forwards
  // printf over the network and the editor shows it live in the Output panel.
  Tyra::Info::writeLogsToFile = !ps2link;

  Tyra::EngineOptions options;
  // The Engine(options) ctor re-applies this flag, so it must be set here
  // too or the static above gets reset to the default (console logging).
  options.writeLogsToFile = !ps2link;
  // Target system (Project > Preferences > Display): Auto follows the console
  // region, NTSC forces 60 Hz, PAL forces 50 Hz.
  options.videoMode = Tyra::VideoMode::Auto;
  // Scan mode (Project > Preferences > Display > Display mode): interlaced
  // 480i/576i (whole frames or true field rendering), progressive 480p,
  // 1080i, or the full-height PAL 576i frame (always 50 Hz). The DTV modes
  // need component cables on a real console and always run at 60 Hz.
  options.displayMode = Tyra::DisplayMode::Interlaced;
  // PAL picture (Preferences > Display > PAL picture): with the
  // region-following interlaced mode, a PAL console (or a forced-PAL
  // target system) boots the full-height 512-line 576i frame instead of
  // the letterboxed NTSC-size picture. Resolved here, before engine init,
  // so the whole boot (logo, loading screen) already runs in it; the menu
  // "DEFAULT" display option maps back to whatever this resolves to.
  if (false &&
      options.displayMode == Tyra::DisplayMode::Interlaced &&
      (options.videoMode == Tyra::VideoMode::PAL ||
       (options.videoMode == Tyra::VideoMode::Auto &&
        graph_get_region() == GRAPH_MODE_PAL)))
    options.displayMode = Tyra::DisplayMode::Pal576i;
  // 16:9 anamorphic output (Preferences > Display > Widescreen).
  options.widescreen = false;
  // Framebuffer colour depth (Preferences > Build > Colour depth) and the
  // GS's ordered dithering. 16bpp halves what the two frame buffers cost in
  // GS memory and hands it to the texture heap; the dither is what keeps the
  // 5-bit channels from banding. See docs/gs-vram.md.
  options.colorDepth = Tyra::ColorDepth::Bits32;
  options.dither = true;
  // Optional GS render targets, 128 KB each, reserved only when this project
  // has something that reads them: a reflective "@sky" material for the env
  // map, a feed camera for the camera feed. Computed at build time - see
  // projectNeedsEnvMap / projectNeedsCamFeed in the editor's templates.cpp.
  options.envMapTarget = false;
  options.camFeedTarget = false;
  // Triple buffering (Preferences > Display > Triple buffering, docs/
  // frame-pacing.md): present from a vblank interrupt instead of stalling
  // the EE on vsync, so a frame that overruns its field is shown one field
  // late instead of halving the frame rate. Costs a third display buffer of
  // GS VRAM; the engine reports and stays double buffered if it does not fit.
  options.tripleBuffering = false;
  // USB keyboard & mouse (Preferences > Build > Keyboard & mouse): loads the
  // usbd + ps2kbd + ps2mouse drivers; controls.hpp maps the keys onto a
  // virtual pad every frame. Works in PCSX2 (the editor sets USB1=hidkbd,
  // USB2=hidmouse in PCSX2.ini) and with real USB devices on a console.
  options.loadUsbKbdMouse = true;
  // Preferences > Build > Keyboard & mouse > Also over ps2link (on by
  // default): the engine reuses the USB stack of the TyraX ps2link
  // (tools/ps2link), which bakes usbd+ps2kbd+ps2mouse into its own boot - it
  // loads none of its own. See docs/ps2link-setup.md and docs/keyboard-mouse.md.
  options.loadUsbKbdMouseUnderPs2Link = false;
  Tyra::Engine engine(options);
  Showcase::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}

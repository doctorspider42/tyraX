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
  // Target system (Project > Preferences > Build): Auto follows the console
  // region, NTSC forces 60 Hz, PAL forces 50 Hz.
  options.videoMode = Tyra::VideoMode::Auto;
  // Scan mode (Project > Preferences > Build > Display mode): interlaced
  // 480i/576i (whole frames or true field rendering), progressive 480p,
  // 1080i, or the full-height PAL 576i frame (always 50 Hz). The DTV modes
  // need component cables on a real console and always run at 60 Hz.
  options.displayMode = Tyra::DisplayMode::Interlaced;
  // PAL picture (Preferences > Build > PAL picture): with the
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
  // 16:9 anamorphic output (Preferences > Build > Widescreen).
  options.widescreen = false;
  // USB keyboard & mouse (Preferences > Build > Keyboard & mouse): loads the
  // usbd + ps2kbd + ps2mouse drivers; controls.hpp maps the keys onto a
  // virtual pad every frame. Works in PCSX2 (the editor sets USB1=hidkbd,
  // USB2=hidmouse in PCSX2.ini) and with real USB devices on a console.
  options.loadUsbKbdMouse = true;
  // Experimental (Preferences > Build > Keyboard & mouse > Force under
  // ps2link): normally the drivers are skipped under ps2link because a second
  // usbd wedges the resident one. With this on the engine keeps them, reuses
  // ps2link's usbd instead of loading its own, and logs the load over the EE
  // console. See docs/keyboard-mouse.md (Debugging on real hardware).
  options.loadUsbKbdMouseUnderPs2Link = false;
  Tyra::Engine engine(options);
  Material_lab::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}

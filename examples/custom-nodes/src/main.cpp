#include <tyra>
#include <cstdio>
#include <cstring>
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
  // 480i/576i, progressive 480p, or 1080i. The DTV modes need component
  // cables on a real console and always run at 60 Hz.
  options.displayMode = Tyra::DisplayMode::Interlaced;
  // 16:9 anamorphic output (Preferences > Build > Widescreen).
  options.widescreen = false;
  Tyra::Engine engine(options);
  Custom_nodes::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}

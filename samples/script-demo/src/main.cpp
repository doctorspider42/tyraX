#include <tyra>
#include "terrain_game.hpp"

int main() {
  // Route TYRA_LOG / TYRA_WARN / TYRA_ERROR and assertion dumps to a host-side
  // "log.txt" (next to the ELF) instead of the EE console, which does not
  // reach PCSX2's emulog. The tyra-editor Debug window tails that file. Must
  // be set before the Engine is constructed (its init logging is the first to
  // hit the file). No cost in a release (NDEBUG) build - the macros compile out.
  Tyra::Info::writeLogsToFile = true;

  Tyra::EngineOptions options;
  // The Engine(options) ctor re-applies this flag, so it must be set here
  // too or the static above gets reset to the default (console logging).
  options.writeLogsToFile = true;
  // Target system (Project > Preferences > Build): Auto follows the console
  // region, NTSC forces 60 Hz, PAL forces 50 Hz.
  options.videoMode = Tyra::VideoMode::Auto;
  Tyra::Engine engine(options);
  Script_demo::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}

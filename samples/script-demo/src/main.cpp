#include <tyra>
#include "terrain_game.hpp"

int main() {
  Tyra::Engine engine;
  Script_demo::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}

// Frozen vantage for an engine A/B. A fixed eye/at, driven from the script's
// own FRAME INDEX rather than wall clock, so two boots traverse identical
// content (docs/profiling.md: never drive a measurement camera from --pad,
// whose driver refreshes off the host clock).
//
// High and pitched down over the middle of the terrain, which is where this
// map's 1,180 objects are: the spawn pose sees 2,528 triangles in 11 packet
// flushes and measures nothing.
#include "scripts/script.hpp"
namespace Large_terrain {
class Vantage : public Script {
 public:
  void update(ScriptContext& ctx) override {
    ctx.cameraOverride = true;
    ctx.cameraEye = Tyra::Vec4(0, 90, -300, 1);
    ctx.cameraAt = Tyra::Vec4(0, 0, 60, 1);
    ctx.cameraUp = Tyra::Vec4(0, 1, 0, 0);
  }
};
}
TYRA_SCRIPT(Large_terrain::Vantage);

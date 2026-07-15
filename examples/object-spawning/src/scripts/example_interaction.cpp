// Example TyraX script. This file is yours - it is never regenerated.
// Walk close to the box and press X: the sky changes color (and a message
// lands in the PCSX2 log via TYRA_LOG).
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace Object_spawning {

class ExampleInteraction : public Script {
 public:
  void update(ScriptContext& ctx) override {
    // Find the first box in the scene
    RuntimeObject* box = nullptr;
    for (int i = 0; i < ctx.objectCount; ++i) {
      if (ctx.objects[i].data.type == 0) {  // 0 = box
        box = &ctx.objects[i];
        break;
      }
    }
    if (!box) return;

    const float dx = ctx.playerPosition.x - box->data.position[0];
    const float dz = ctx.playerPosition.z - box->data.position[2];
    const bool nearBox = (dx * dx + dz * dz) < 8.0F * 8.0F;

    if (nearBox && ctx.engine->pad.getClicked().Cross) {
      toggled = !toggled;
      TYRA_LOG("Box says hello! Sky toggled: ", (int)toggled);
      ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
                             : Tyra::Color(SKY_R, SKY_G, SKY_B);
    }
  }

 private:
  bool toggled = false;
};

}  // namespace Object_spawning

TYRA_SCRIPT(Object_spawning::ExampleInteraction);

// Example TyraX script. This file is yours - it is never regenerated.
// Says hello in the PCSX2 log at startup. Uncomment the block in update()
// for a working example that reacts to the pad.
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace Layer_streaming {

class ExampleInteraction : public Script {
 public:
  void init(ScriptContext& ctx) override {
    (void)ctx;
    TYRA_LOG("Hello from TyraX! Edit src/scripts/example_interaction.cpp.");
  }

  void update(ScriptContext& ctx) override {
    (void)ctx;
    // Example: walk up to the first box and press X to toggle the sky color.
    // Commented out so a jump (X) does not recolor the sky - uncomment to try.
    //
    // static bool toggled = false;
    // RuntimeObject* box = nullptr;
    // for (int i = 0; i < ctx.objectCount; ++i)
    //   if (ctx.objects[i].data.type == 0) { box = &ctx.objects[i]; break; }
    // if (!box) return;
    // const float dx = ctx.playerPosition.x - box->data.position[0];
    // const float dz = ctx.playerPosition.z - box->data.position[2];
    // if ((dx * dx + dz * dz) < 8.0F * 8.0F && ctx.engine->pad.getClicked().Cross) {
    //   toggled = !toggled;
    //   TYRA_LOG("Box says hello! Sky toggled: ", (int)toggled);
    //   ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
    //                          : Tyra::Color(SKY_R, SKY_G, SKY_B);
    // }
  }
};

}  // namespace Layer_streaming

TYRA_SCRIPT(Layer_streaming::ExampleInteraction);

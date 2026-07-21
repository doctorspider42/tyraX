// Example TyraX script. This file is yours - it is never regenerated.
// Says hello in the PCSX2 log at startup. Uncomment the block in update()
// for a working example that reacts to the pad.
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace Video_modes {

class ExampleInteraction : public Script {
 public:
  void init(ScriptContext& ctx) override {
    (void)ctx;
    TYRA_LOG("Hello from TyraX! Edit src/scripts/example_interaction.cpp.");
  }

  void update(ScriptContext& ctx) override {
    (void)ctx;
    // Example: press X to toggle the sky color. Commented out so it does not
    // fire by surprise - uncomment to try it.
    //
    // static bool toggled = false;
    // if (ctx.engine->pad.getClicked().Cross) {
    //   toggled = !toggled;
    //   TYRA_LOG("X pressed! Sky toggled: ", (int)toggled);
    //   ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
    //                          : Tyra::Color(SKY_R, SKY_G, SKY_B);
    // }
  }
};

}  // namespace Video_modes

TYRA_SCRIPT(Video_modes::ExampleInteraction);

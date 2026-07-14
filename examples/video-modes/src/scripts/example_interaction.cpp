// Example TyraX script. This file is yours - it is never regenerated.
// Press X on the pad: the sky changes color (and a message lands in the
// PCSX2 log via TYRA_LOG).
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace Video_modes {

class ExampleInteraction : public Script {
 public:
  void update(ScriptContext& ctx) override {
    if (ctx.engine->pad.getClicked().Cross) {
      toggled = !toggled;
      TYRA_LOG("X pressed! Sky toggled: ", (int)toggled);
      ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
                             : Tyra::Color(SKY_R, SKY_G, SKY_B);
    }
  }

 private:
  bool toggled = false;
};

}  // namespace Video_modes

TYRA_SCRIPT(Video_modes::ExampleInteraction);

// Motor District: the pause-menu value selects a fixed day/night hour.
// This is project-owned game logic; normal code generation preserves it.
#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "daynight.gen.hpp"

namespace Vehicle_playground {
class DistrictMood : public Script {
  int previous = -1;
  unsigned int generation = ~0U;
 public:
  void update(ScriptContext& ctx) override {
    const bool night = ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount &&
                       ctx.saveValues[DISTRICT_NIGHT_VALUE] >= 0.5F;
    // Pin the live clock every simulation frame. The regular render tick
    // evaluates sky, moon, stars, fog and the grade from the same hour.
    daynight::g_hour = night ? 0.0F : 12.0F;
    if (previous == (int)night && generation == ctx.sceneGeneration) return;
    generation = ctx.sceneGeneration;
    previous = (int)night;
    for (int i = 0; i < ctx.objectCount; ++i) {
      if (ctx.objects[i].data.type == 9 && ctx.lightRequest)
        ctx.lightRequest[i] = night ? 1 : 0;
    }
    const unsigned long long* ids = SCENE_OBJECT_ID_TABLES[ctx.scene];
    for (const unsigned long long wanted : DISTRICT_NIGHT_OBJECTS)
      for (int i = 0; i < ctx.objectCount; ++i)
        if (ids[i] == wanted) {
          ctx.objects[i].visible = night;
          break;
        }
    TYRA_LOG("Motor District mood: ", night ? "NIGHT" : "DAY");
  }
};
}
TYRA_SCRIPT(Vehicle_playground::DistrictMood);

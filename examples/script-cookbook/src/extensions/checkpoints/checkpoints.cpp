// checkpoints.cpp - the Checkpoints script extension, written inside this
// example to show how an extension is made (README "Write your own
// extension", docs/script-extensions.md).
//
// One object script, Checkpoint, configured per object through public
// fields, and the state the Checkpoints Done flow node reads
// (checkpoints_nodes.hpp).
#include "scripts/script.hpp"

#include "checkpoints_nodes.hpp"

TYRAX_NAMESPACE_BEGIN

// Shared by every Checkpoint of the scene: which one is next, and how many
// there are. A scene restart starts over (onSceneEnd).
namespace {
int g_next = 1;
int g_total = 0;
}  // namespace

int checkpointsReached() { return g_next - 1; }
int checkpointsTotal() { return g_total; }

/** A pad the player reaches in order: it lights up, spins and sends an event. */
class Checkpoint : public ObjectScript {
 public:
  [[tyra::prop(min = 1, max = 32, tooltip = "Its place in the course: 1 is reached first")]]
  int order = 1;
  [[tyra::prop(min = 0.5, max = 10, tooltip = "How close the player has to come, on the ground")]]
  float radius = 1.8F;
  [[tyra::prop(color, header = "Look", tooltip = "Colour until it is reached")]]
  tyrax::Vec3 idleColor{0.35F, 0.35F, 0.4F};
  [[tyra::prop(color, tooltip = "Colour once it is reached")]]
  tyrax::Vec3 reachedColor{0.2F, 1.0F, 0.45F};
  [[tyra::prop(tooltip = "Spin once reached")]]
  bool spinWhenReached = true;
  [[tyra::prop(header = "Wiring", tooltip = "Sent when reached; the value is its order")]]
  tyrax::EventId reachedEvent = {-1};

  void onStart(ScriptContext&) override {
    ++g_total;
    object.setColor(idleColor);
  }

  void onUpdate(ScriptContext& ctx) override {
    if (reached || order != g_next) return;  // only the next one counts
    // On the ground: the player position is the EYE, well above a pad.
    const tyrax::Vec3 p = tyrax::playerPosition(ctx), at = object.position();
    if (tyrax::distanceXZ(p, at) > radius || fabsf(p.y - at.y) > 3.0F) return;
    reached = true;
    ++g_next;
    object.setColor(reachedColor);
    if (spinWhenReached) object.spin(tyrax::Vec3(0.0F, 120.0F, 0.0F));
    tyrax::debugLog("Checkpoint ", order, " of ", g_total);
    tyrax::sendEvent(reachedEvent, (float)order);
  }

  void onSceneEnd(ScriptContext&) override {
    g_next = 1;
    g_total = 0;
  }

 private:
  bool reached = false;
};
TYRA_OBJECT_SCRIPT(Checkpoint);

TYRAX_NAMESPACE_END

// door.cpp - Essentials: a door that swings open around its own Y axis.
// Part of the Essentials script extension (docs/script-extensions.md).
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

/** Swings between its placed yaw and placed yaw + openAngle. Opens with USE
 * when the object is Usable, from a flow graph's Call Script
 * (SwingDoor.open / close / toggle), or on an event. */
class SwingDoor : public ObjectScript {
 public:
  [[tyra::prop(min = -180, max = 180, tooltip = "Degrees it turns when open")]]
  float openAngle = 90.0F;
  [[tyra::prop(min = 1, max = 720, tooltip = "Degrees per second")]]
  float speed = 120.0F;
  [[tyra::prop(tooltip = "Played when it starts to move")]]
  tyrax::SoundId sound = {-1};
  [[tyra::prop(tooltip = "Toggles when this event arrives (a switch, a trigger)")]]
  tyrax::EventId toggleOn = {-1};
  [[tyra::prop(tooltip = "Sent when it finishes opening")]]
  tyrax::EventId openedEvent = {-1};
  [[tyra::prop(tooltip = "Start open")]]
  bool startOpen = false;

  void onStart(ScriptContext&) override {
    closedYaw = object.yaw();
    isOpen = startOpen;
    if (isOpen) object.setYaw(closedYaw + openAngle);
  }
  void onUsed(ScriptContext& ctx) override { toggle(ctx); }
  void onEvent(ScriptContext& ctx, tyrax::EventId e, float) override {
    if (e.index >= 0 && e.index == toggleOn.index) toggle(ctx);
  }
  void onUpdate(ScriptContext&) override {
    const float target = closedYaw + (isOpen ? openAngle : 0.0F);
    const float yaw = object.yaw();
    if (fabsf(tyrax::deltaAngle(yaw, target)) < 0.01F) {
      if (moving && isOpen) tyrax::sendEvent(openedEvent);
      moving = false;
      return;
    }
    object.setYaw(tyrax::moveTowardsAngle(yaw, target, speed * tyrax::dt()));
  }

  void open(ScriptContext& ctx) { set(ctx, true); }
  void close(ScriptContext& ctx) { set(ctx, false); }
  void toggle(ScriptContext& ctx) { set(ctx, !isOpen); }

 private:
  void set(ScriptContext& ctx, bool o) {
    if (o == isOpen) return;
    isOpen = o;
    moving = true;
    tyrax::playSound(ctx, sound);
  }
  float closedYaw = 0.0F;
  bool isOpen = false, moving = false;
};
TYRA_OBJECT_SCRIPT(SwingDoor);
TYRA_SCRIPT_METHOD(SwingDoor, open);
TYRA_SCRIPT_METHOD(SwingDoor, close);
TYRA_SCRIPT_METHOD(SwingDoor, toggle);

/** Clones a template object around itself every few seconds, up to a limit. */
class Spawner : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "The object to clone (hide the original if it should not show)")]]
  tyrax::ObjectRef templateObject;
  [[tyra::prop(min = 0.1, max = 60, tooltip = "Seconds between spawns")]]
  float interval = 3.0F;
  [[tyra::prop(min = 1, max = 64, tooltip = "Stops at this many")]]
  int maxCount = 5;
  [[tyra::prop(min = 0, max = 50, tooltip = "Random scatter around the spawner")]]
  float scatter = 2.0F;

  void onStart(ScriptContext&) override { next.start(interval); }
  void onUpdate(ScriptContext& ctx) override {
    if (count >= maxCount || !next.finished()) return;
    next.start(interval);
    const tyrax::Vec3 at = object.position() + tyrax::randomInCircle(scatter);
    if (tyrax::spawn(ctx, templateObject, at, tyrax::randomRange(0.0F, 360.0F))) ++count;
  }

  // Call Script: start over (a wave reset).
  void reset(ScriptContext&) { count = 0; }

 private:
  tyrax::Timer next;
  int count = 0;
};
TYRA_OBJECT_SCRIPT(Spawner);
TYRA_SCRIPT_METHOD(Spawner, reset);

TYRAX_NAMESPACE_END

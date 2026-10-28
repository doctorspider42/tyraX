// motion.cpp - Essentials: things that move by themselves.
// Part of the Essentials script extension (docs/script-extensions.md).
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

/** Turns continuously - degrees per second around each axis. Uses the Spin
 * Object rate, so the object rides the cheap per-object matrix path. */
class Rotator : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Degrees per second around X, Y and Z")]]
  tyrax::Vec3 speed{0.0F, 90.0F, 0.0F};

  void onStart(ScriptContext&) override { object.spin(speed); }
};
TYRA_OBJECT_SCRIPT(Rotator);

/** Floats up and down around where it was placed. */
class Bobber : public ObjectScript {
 public:
  [[tyra::prop(min = 0, max = 5, tooltip = "Units above and below the placed height")]]
  float height = 0.25F;
  [[tyra::prop(min = 0.1, max = 10, tooltip = "Seconds for one full up-and-down")]]
  float period = 2.0F;
  [[tyra::prop(min = 0, max = 1, tooltip = "Where in the cycle it starts, 0..1 - stagger a row of them")]]
  float phase = 0.0F;

  void onStart(ScriptContext&) override { base = object.position(); }
  void onUpdate(ScriptContext&) override {
    const float t = (tyrax::sceneTime() / period + phase) * 2.0F * tyrax::kPi;
    object.setPosition(base + tyrax::Vec3(0.0F, height * sinf(t), 0.0F));
  }

 private:
  tyrax::Vec3 base;
};
TYRA_OBJECT_SCRIPT(Bobber);

/** Moves toward another object (or the player when none is picked) and
 * stops at a distance. */
class Follower : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "What to follow - empty follows the player")]]
  tyrax::ObjectRef target;
  [[tyra::prop(min = 0, max = 50, tooltip = "Stops this far from the target")]]
  float distance = 3.0F;
  [[tyra::prop(min = 0, max = 30, tooltip = "Units per second")]]
  float speed = 4.0F;
  [[tyra::prop(tooltip = "Turn to face the target while moving")]]
  bool face = true;
  [[tyra::prop(tooltip = "Stay at the placed height instead of flying")]]
  bool keepHeight = true;

  void onUpdate(ScriptContext& ctx) override {
    tyrax::Vec3 goal = target.valid() ? target.position() : tyrax::playerPosition(ctx);
    tyrax::Vec3 at = object.position();
    if (keepHeight) goal.y = at.y;
    const float d = tyrax::distance(at, goal);
    if (d > distance) {
      const float step = speed * tyrax::dt();
      object.setPosition(tyrax::moveTowards(at, goal, d - distance < step ? d - distance : step));
    }
    if (face) object.setYaw(tyrax::moveTowardsAngle(object.yaw(), tyrax::yawTo(at, goal),
                                                    360.0F * tyrax::dt()));
  }
};
TYRA_OBJECT_SCRIPT(Follower);

/** Keeps facing the player (a sign, a turret, a curious NPC). */
class LookAtPlayer : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Also tilt up and down, not only turn")]]
  bool tilt = false;
  [[tyra::prop(min = 0, max = 1080, tooltip = "Degrees per second; 0 = snap")]]
  float turnSpeed = 180.0F;

  void onUpdate(ScriptContext& ctx) override {
    const tyrax::Vec3 p = tyrax::playerPosition(ctx);
    if (tilt || turnSpeed <= 0.0F) {
      object.lookAt(p, tilt);
      return;
    }
    object.setYaw(tyrax::moveTowardsAngle(object.yaw(), tyrax::yawTo(object.position(), p),
                                          turnSpeed * tyrax::dt()));
  }
};
TYRA_OBJECT_SCRIPT(LookAtPlayer);

TYRAX_NAMESPACE_END

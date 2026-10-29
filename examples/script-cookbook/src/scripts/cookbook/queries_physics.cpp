// queries_physics.cpp - Script cookbook, rows B and C: raycasts, line of
// sight, searches over the scene, physics impulses, spawning.
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// B5. LaserTurret - sweeps left and right, casts a ray along its own forward
// (object.forward()) every frame and parks `laserDot` on what it hits:
// tyrax::raycast() stops at real collision boxes and the terrain.
// ---------------------------------------------------------------------------
class LaserTurret : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "A small object moved to the hit point")]]
  tyrax::ObjectRef laserDot;
  [[tyra::prop(min = 1, max = 100, tooltip = "Ray length")]]
  float range = 30.0F;
  [[tyra::prop(min = 0, max = 90, tooltip = "Sweep half-angle, degrees")]]
  float sweep = 50.0F;

  void onStart(ScriptContext&) override { baseYaw = object.yaw(); }

  void onUpdate(ScriptContext& ctx) override {
    object.setYaw(baseYaw + sweep * sinf(tyrax::sceneTime() * 0.8F));
    // Start the ray just in front of the muzzle and skip ourselves.
    const tyrax::Vec3 dir = tyrax::forwardFromYaw(object.yaw());
    const tyrax::Vec3 from = object.position() + dir * 0.8F;
    const tyrax::RayHit hit = tyrax::raycast(ctx, from, dir, range, 0.0F, object.index());
    laserDot.setPosition(hit.point);
    // Red on an object, yellow on terrain or nothing.
    laserDot.setColor(hit.object ? tyrax::Vec3(1, 0.1F, 0.1F) : tyrax::Vec3(1, 0.9F, 0.2F));
  }

 private:
  float baseYaw = 0.0F;
};
TYRA_OBJECT_SCRIPT(LaserTurret);

// ---------------------------------------------------------------------------
// B6. Sentry - green while it can see you (tyrax::lineOfSight), red when a
// wall is in the way; turns toward you either way (moveTowardsAngle/yawTo).
// ---------------------------------------------------------------------------
class Sentry : public ObjectScript {
 public:
  [[tyra::prop(min = 1, max = 60, tooltip = "Only watches inside this distance")]]
  float watchRadius = 15.0F;

  void onUpdate(ScriptContext& ctx) override {
    const tyrax::Vec3 eye = object.position() + tyrax::Vec3(0.0F, 1.0F, 0.0F);
    const tyrax::Vec3 me = tyrax::playerPosition(ctx);
    const bool near = tyrax::distance(eye, me) <= watchRadius;
    const bool sees = near && tyrax::lineOfSight(ctx, eye, me, object.index());
    object.setColor(sees ? tyrax::Vec3(0.2F, 1.0F, 0.3F) : tyrax::Vec3(1.0F, 0.2F, 0.2F));
    if (near)
      object.setYaw(tyrax::moveTowardsAngle(object.yaw(), tyrax::yawTo(eye, me),
                                            120.0F * tyrax::dt()));
    if (sees != seen) {
      seen = sees;
      tyrax::debugLog("COOKBOOK sentry sees player=", (int)sees);
    }
  }

 private:
  bool seen = false;
};
TYRA_OBJECT_SCRIPT(Sentry);

// ---------------------------------------------------------------------------
// C1. Magnet - tyrax::overlapSphere with an ObjectFilter (your own test as a
// plain function), addImpulse toward the magnet, and findNearest to tint the
// closest crate.
// ---------------------------------------------------------------------------
namespace {
bool isPhysicsBody(const RuntimeObject& o) { return o.data.physics != 0; }
}  // namespace

class Magnet : public ObjectScript {
 public:
  [[tyra::prop(min = 0, max = 20, tooltip = "Pull radius")]]
  float radius = 6.0F;
  [[tyra::prop(min = 0, max = 30, tooltip = "Pull, units per second squared")]]
  float strength = 6.0F;
  [[tyra::prop(tooltip = "Pull only while the player is near")]]
  bool needsPlayer = true;

  void onUpdate(ScriptContext& ctx) override {
    if (needsPlayer && tyrax::distanceToPlayer(ctx, object.position()) > radius * 2.0F) return;
    tyrax::ObjectFilter bodies;
    bodies.exclude = object.index();
    bodies.accept = &isPhysicsBody;
    tyrax::ObjectRef found[16];
    const int n = tyrax::overlapSphere(ctx, object.position(), radius, found, 16, bodies);
    for (int i = 0; i < n; ++i) {
      const tyrax::Vec3 toMagnet = tyrax::normalize(object.position() - found[i].position());
      found[i].addImpulse(toMagnet * (strength * tyrax::dt()));
    }
    const tyrax::ObjectRef nearest = tyrax::findNearest(ctx, object.position(), radius, bodies);
    if (nearest != lastNearest) {
      if (lastNearest) lastNearest.setColor(tyrax::Vec3(0.7F, 0.5F, 0.3F));
      if (nearest) nearest.setColor(tyrax::Vec3(1.0F, 0.9F, 0.2F));
      lastNearest = nearest;
    }
  }

 private:
  tyrax::ObjectRef lastNearest;
};
TYRA_OBJECT_SCRIPT(Magnet);

// ---------------------------------------------------------------------------
// C2. LaunchPad - stand on it and press the action picked in `launchAction`
// (an Input Map action, so pause-menu rebinds work): every physics body on
// the pad flies up, the pad rumbles the controller and shakes the camera.
// ---------------------------------------------------------------------------
class LaunchPad : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "The Input Map action that fires the pad")]]
  tyrax::ActionId launchAction = {-1};
  [[tyra::prop(min = 0, max = 30, tooltip = "Launch speed, units per second")]]
  float power = 9.0F;
  [[tyra::prop(min = 0, max = 10, tooltip = "Radius that counts as 'on the pad'")]]
  float radius = 3.0F;

  void onUpdate(ScriptContext& ctx) override {
    const tyrax::Vec3 p = tyrax::playerPosition(ctx), pad = object.position();
    if (tyrax::distanceXZ(p, pad) > radius || !tyrax::actionPressed(ctx, launchAction)) return;
    tyrax::ObjectFilter bodies;
    bodies.exclude = object.index();
    bodies.accept = &isPhysicsBody;
    tyrax::ObjectRef found[16];
    const int n = tyrax::overlapSphere(ctx, pad, radius + 1.0F, found, 16, bodies);
    for (int i = 0; i < n; ++i) found[i].addImpulse(tyrax::Vec3(0.0F, power, 0.0F));
    tyrax::vibrate(ctx, 0.6F, false, 0.25F);
    tyrax::cameraShake(ctx, 0.15F, 0.3F);
    tyrax::debugLog("COOKBOOK launch pad fired bodies=", n);
  }
};
TYRA_OBJECT_SCRIPT(LaunchPad);

// ---------------------------------------------------------------------------
// C3. SpawnDemo - tyrax::spawn clones `templateObject` every `interval`
// seconds around itself; past `maxAlive` the oldest clone is despawned.
// ---------------------------------------------------------------------------
class SpawnDemo : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "What to clone")]]
  tyrax::ObjectRef templateObject;
  [[tyra::prop(min = 0.2, max = 10, tooltip = "Seconds between clones")]]
  float interval = 1.5F;
  [[tyra::prop(min = 1, max = 8, tooltip = "Clones alive at once")]]
  int maxAlive = 4;

  void onStart(ScriptContext&) override { next.start(interval); }
  void onUpdate(ScriptContext& ctx) override {
    if (!next.finished()) return;
    next.start(interval);
    if (count >= maxAlive && count > 0) {  // free the oldest slot
      clones[0].despawn();
      for (int i = 1; i < count; ++i) clones[i - 1] = clones[i];
      --count;
    }
    const tyrax::Vec3 at = object.position() + tyrax::randomInCircle(2.0F) +
                           tyrax::Vec3(0.0F, 1.0F, 0.0F);
    tyrax::ObjectRef c = tyrax::spawn(ctx, templateObject, at, tyrax::randomRange(0.0F, 360.0F));
    if (c && count < 8) clones[count++] = c;
  }

 private:
  tyrax::Timer next;
  tyrax::ObjectRef clones[8];
  int count = 0;
};
TYRA_OBJECT_SCRIPT(SpawnDemo);

TYRAX_NAMESPACE_END

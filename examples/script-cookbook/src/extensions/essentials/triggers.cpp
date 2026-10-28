// triggers.cpp - Essentials: things that notice the player.
// Part of the Essentials script extension (docs/script-extensions.md).
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

/** Sends an event when the player comes within a radius, and another when
 * they leave - wire On Event in any graph, or onEvent in any script. */
class ProximityTrigger : public ObjectScript {
 public:
  [[tyra::prop(min = 0, max = 100, tooltip = "How close counts as near")]]
  float radius = 4.0F;
  [[tyra::prop(tooltip = "Sent when the player comes near")]]
  tyrax::EventId enterEvent = {-1};
  [[tyra::prop(tooltip = "Sent when the player leaves")]]
  tyrax::EventId exitEvent = {-1};
  [[tyra::prop(tooltip = "Fire the enter event once per scene, then stop")]]
  bool once = false;

  void onUpdate(ScriptContext& ctx) override {
    const bool in = object.active() &&
                    tyrax::distanceToPlayer(ctx, object.position()) <= radius;
    if (in == inside) return;
    inside = in;
    if (in && (!once || !fired)) {
      fired = true;
      tyrax::sendEvent(enterEvent, (float)object.index());
    } else if (!in) {
      tyrax::sendEvent(exitEvent, (float)object.index());
    }
  }

 private:
  bool inside = false, fired = false;
};
TYRA_OBJECT_SCRIPT(ProximityTrigger);

/** Put on an Area object: turns its enter / exit into events. */
class AreaRelay : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Sent when a player steps in")]]
  tyrax::EventId enterEvent = {-1};
  [[tyra::prop(tooltip = "Sent when a player steps out")]]
  tyrax::EventId exitEvent = {-1};

  void onPlayerEnter(ScriptContext&) override { tyrax::sendEvent(enterEvent); }
  void onPlayerExit(ScriptContext&) override { tyrax::sendEvent(exitEvent); }
};
TYRA_OBJECT_SCRIPT(AreaRelay);

/** Picked up by walking into it: adds to a save value, plays a sound, hides
 * itself (and can respawn). */
class Collectible : public ObjectScript {
 public:
  [[tyra::prop(min = 0, max = 10, tooltip = "Pick-up distance")]]
  float radius = 1.2F;
  [[tyra::prop(tooltip = "The save value that counts them (coins, keys...)")]]
  tyrax::SaveValueId counter = {-1};
  [[tyra::prop(tooltip = "Added to the counter per pick-up")]]
  float amount = 1.0F;
  [[tyra::prop(tooltip = "Played on pick-up")]]
  tyrax::SoundId sound = {-1};
  [[tyra::prop(tooltip = "Sent on pick-up (value = the new counter)")]]
  tyrax::EventId pickedEvent = {-1};
  [[tyra::prop(min = 0, max = 600, tooltip = "Seconds until it comes back; 0 = never")]]
  float respawn = 0.0F;

  void onUpdate(ScriptContext& ctx) override {
    if (!object.visible()) {
      if (respawn > 0.0F && back.finished()) object.show();
      return;
    }
    // On the ground plane, with a generous height band: the player position is
    // the EYE, 1.8 units above a coin lying at their feet.
    const tyrax::Vec3 p = tyrax::playerPosition(ctx), at = object.position();
    if (tyrax::distanceXZ(p, at) > radius || fabsf(p.y - at.y) > 2.5F) return;
    tyrax::addSaveValue(ctx, counter, amount);
    tyrax::playSound(ctx, sound);
    tyrax::sendEvent(pickedEvent, tyrax::saveValue(ctx, counter));
    object.hide();
    if (respawn > 0.0F) back.start(respawn);
  }

 private:
  tyrax::Timer back;
};
TYRA_OBJECT_SCRIPT(Collectible);

TYRAX_NAMESPACE_END

// motion_math.cpp - Script cookbook, rows A and B: math, easing, random,
// time, and everything an ObjectRef can do to its object.
// Every class here is attached in the level (script-cookbook.tyra); the
// numbers you see in Properties > Scripts are its [[tyra::prop]] fields.
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// A. EaseShowcase - nine spheres, one per easing curve.
// tyrax::ease(t, curve) shapes a 0..1 ramp; tyrax::tween() is the Tween Value
// node; sceneTime() the Scene Time node. Each sphere rises `height` and falls
// back over `period` seconds using the curve picked in its `curve` field.
// ---------------------------------------------------------------------------
class EaseShowcase : public ObjectScript {
 public:
  [[tyra::prop(options = "Linear|In|Out|Smooth|InCubic|OutCubic|InOutCubic|OutBack|OutBounce",
               tooltip = "The tyrax::Ease curve this sphere demonstrates")]]
  int curve = 0;
  [[tyra::prop(min = 0, max = 5, tooltip = "How far up it travels")]]
  float height = 2.0F;
  [[tyra::prop(min = 0.2, max = 6, tooltip = "Seconds for one rise (and as long to fall)")]]
  float period = 1.5F;

  void onStart(ScriptContext&) override { base = object.position(); }

  void onUpdate(ScriptContext&) override {
    // A ping-pong clock: 0 -> period rising, period -> 2*period falling.
    const float t = fmodf(tyrax::sceneTime(), period * 2.0F);
    const tyrax::Ease e = static_cast<tyrax::Ease>(curve);
    const float y = t < period ? tyrax::tween(t, period, 0.0F, height, e)
                               : tyrax::tween(t - period, period, height, 0.0F, e);
    object.setPosition(base + tyrax::Vec3(0.0F, y, 0.0F));
  }

 private:
  tyrax::Vec3 base;
};
TYRA_OBJECT_SCRIPT(EaseShowcase);

// ---------------------------------------------------------------------------
// B1. DiceRoller - press USE: a die roll (randomInt includes both ends), a
// colour per face, and a 1-in-`luckyChance` "lucky" jump (chance()).
// Also counts the rolls in a save value - tyrax::addSaveValue.
// ---------------------------------------------------------------------------
class DiceRoller : public ObjectScript {
 public:
  [[tyra::prop(min = 0, max = 1, tooltip = "Probability of a lucky hop per roll")]]
  float luckyChance = 0.25F;
  [[tyra::prop(tooltip = "The save value that counts rolls")]]
  tyrax::SaveValueId rollCounter = {-1};

  int lastRoll = 0;  // public: GameManager reads it through getScript<DiceRoller>

  void onUsed(ScriptContext& ctx) override {
    static const tyrax::Vec3 kFaces[6] = {{1, 0.2F, 0.2F}, {1, 0.6F, 0.1F}, {1, 1, 0.2F},
                                          {0.3F, 1, 0.3F}, {0.2F, 0.6F, 1}, {0.7F, 0.3F, 1}};
    lastRoll = tyrax::randomInt(1, 6);
    object.setColor(kFaces[lastRoll - 1]);
    object.rotateBy(tyrax::Vec3(tyrax::randomRange(-90.0F, 90.0F), 90.0F, 0.0F));
    if (tyrax::chance(luckyChance)) object.moveBy(tyrax::Vec3(0.0F, 0.5F, 0.0F));
    tyrax::addSaveValue(ctx, rollCounter, 1.0F);
    tyrax::debugLog("COOKBOOK dice rolled ", lastRoll, " total=",
                    tyrax::saveValue(ctx, rollCounter));
  }
};
TYRA_OBJECT_SCRIPT(DiceRoller);

// ---------------------------------------------------------------------------
// B2. Blinker - tyrax::Timer: start(), finished() fires once, restart it.
// ---------------------------------------------------------------------------
class Blinker : public ObjectScript {
 public:
  [[tyra::prop(min = 0.1, max = 5, tooltip = "Seconds visible / hidden")]]
  float interval = 0.6F;

  void onStart(ScriptContext&) override { timer.start(interval); }
  void onUpdate(ScriptContext&) override {
    if (!timer.finished()) return;
    object.toggleVisible();
    timer.start(interval);
  }

 private:
  tyrax::Timer timer;
};
TYRA_OBJECT_SCRIPT(Blinker);

// ---------------------------------------------------------------------------
// B3. CooldownButton - tyrax::Cooldown: USE works, then refuses for a while.
// Green = ready, red = cooling down.
// ---------------------------------------------------------------------------
class CooldownButton : public ObjectScript {
 public:
  [[tyra::prop(min = 0.1, max = 10, tooltip = "Seconds before it can be used again")]]
  float cooldownSeconds = 2.0F;
  [[tyra::prop(color, tooltip = "Colour while ready")]]
  tyrax::Vec3 readyColor{0.2F, 0.9F, 0.3F};
  [[tyra::prop(color, tooltip = "Colour while cooling down")]]
  tyrax::Vec3 waitColor{0.9F, 0.2F, 0.2F};

  void onUsed(ScriptContext&) override {
    if (cooldown.use(cooldownSeconds)) tyrax::debugLog("COOKBOOK cooldown button pressed");
    else tyrax::debugLog("COOKBOOK cooldown still running");
  }
  void onUpdate(ScriptContext&) override {
    const tyrax::Vec3 want = cooldown.ready() ? readyColor : waitColor;
    if (tyrax::distance(object.color(), want) > 0.01F) object.setColor(want);
  }

 private:
  tyrax::Cooldown cooldown;
};
TYRA_OBJECT_SCRIPT(CooldownButton);

// ---------------------------------------------------------------------------
// B4. TransformTour - every ObjectRef transform call, one per step:
// moveBy, rotateBy, setYaw, setScale, lookAt, spin, then back home.
// ---------------------------------------------------------------------------
class TransformTour : public ObjectScript {
 public:
  [[tyra::prop(min = 0.3, max = 5, tooltip = "Seconds per step")]]
  float stepSeconds = 1.2F;

  void onStart(ScriptContext&) override {
    home = object.position();
    next.start(stepSeconds);
  }
  void onUpdate(ScriptContext& ctx) override {
    if (!next.finished()) return;
    next.start(stepSeconds);
    switch (step++ % 7) {
      case 0: object.moveBy(tyrax::Vec3(0.0F, 1.0F, 0.0F)); break;          // up one unit
      case 1: object.rotateBy(tyrax::Vec3(0.0F, 0.0F, 45.0F)); break;      // roll 45 deg
      case 2: object.setYaw(object.yaw() + 90.0F); break;                  // quarter turn
      case 3: object.setScale(tyrax::Vec3(1.5F, 0.5F, 1.5F)); break;       // squash
      case 4: object.lookAt(tyrax::playerPosition(ctx), true); break;      // face you
      case 5: object.spin(tyrax::Vec3(0.0F, 180.0F, 0.0F)); break;         // keep turning
      default:                                                             // reset
        object.spin(tyrax::Vec3());
        object.setRotation(tyrax::Vec3());
        object.setScale(1.0F);
        object.setPosition(home);
        break;
    }
  }

 private:
  tyrax::Vec3 home;
  tyrax::Timer next;
  int step = 0;
};
TYRA_OBJECT_SCRIPT(TransformTour);

TYRAX_NAMESPACE_END

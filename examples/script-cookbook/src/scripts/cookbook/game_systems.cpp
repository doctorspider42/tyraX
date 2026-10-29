// game_systems.cpp - Script cookbook, rows D and E: input, audio, save data,
// World Facts, the HUD, events (to and from flow graphs), scripts finding
// scripts, Area callbacks, the camera and the scene.
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// D1. StickTilt - the left stick (tyrax::stick) tilts a plank; holding the
// action in `boostAction` (actionHeld) doubles the tilt.
// ---------------------------------------------------------------------------
class StickTilt : public ObjectScript {
 public:
  [[tyra::prop(min = 0, max = 60, tooltip = "Degrees at full stick")]]
  float maxTilt = 25.0F;
  [[tyra::prop(tooltip = "Held = twice the tilt")]]
  tyrax::ActionId boostAction = {-1};

  void onUpdate(ScriptContext& ctx) override {
    const tyrax::Vec3 s = tyrax::stick(ctx, false);
    const float k = tyrax::actionHeld(ctx, boostAction) ? 2.0F : 1.0F;
    object.setRotation(tyrax::Vec3(s.y * maxTilt * k, object.rotation().y, -s.x * maxTilt * k));
  }
};
TYRA_OBJECT_SCRIPT(StickTilt);

// ---------------------------------------------------------------------------
// D2. SoundBox - USE plays the sound picked in its field (tyrax::playSound
// shares the voice table with the graphs' Play Sound).
// ---------------------------------------------------------------------------
class SoundBox : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Played on USE")]]
  tyrax::SoundId sound = {-1};
  [[tyra::prop(min = 0, max = 100, tooltip = "Volume 0..100")]]
  int volume = 90;
  [[tyra::prop(min = 0, max = 10, tooltip = "Who loses a voice when all are busy")]]
  int priority = 5;

  void onUsed(ScriptContext& ctx) override {
    const int ch = tyrax::playSound(ctx, sound, volume, priority);
    tyrax::debugLog("COOKBOOK sound box channel=", ch);
    object.moveBy(tyrax::Vec3(0.0F, 0.1F, 0.0F));  // a little hop so you see the press
    hop.start(0.15F);
  }
  void onUpdate(ScriptContext&) override {
    if (hop.finished()) object.moveBy(tyrax::Vec3(0.0F, -0.1F, 0.0F));
  }

 private:
  tyrax::Timer hop;
};
TYRA_OBJECT_SCRIPT(SoundBox);

// ---------------------------------------------------------------------------
// D3. SaveCounter - USE: a save value (survives memory-card saves), the HUD
// bar bound to it, a HUD text shown for 2 s, a save text, and a World Fact.
// ---------------------------------------------------------------------------
class SaveCounter : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Counted presses")]]
  tyrax::SaveValueId presses = {-1};
  [[tyra::prop(tooltip = "A HUD bar showing the count")]]
  tyrax::HudBarId bar = {-1};
  [[tyra::prop(tooltip = "Shown for two seconds on every press")]]
  tyrax::HudTextId banner = {-1};
  [[tyra::prop(tooltip = "Written into the 'last-press' save text")]]
  const char* message = "pressed the counter";

  void onUsed(ScriptContext& ctx) override {
    tyrax::addSaveValue(ctx, presses, 1.0F);
    const float n = tyrax::saveValue(ctx, presses);
    tyrax::setHudBar(ctx, bar, n);
    tyrax::showText(ctx, banner, 2.0F);
    tyrax::setSaveText(ctx, tyrax::SaveTexts::LastPress, message);
    // A World Fact (Tools > World Facts): the same store the graphs' Set Fact uses.
    tyrax::setFact(tyrax::Facts::CookbookPresses, n);
    tyrax::debugLog("COOKBOOK presses=", n, " fact=", tyrax::fact(tyrax::Facts::CookbookPresses),
                    " text=", tyrax::saveText(ctx, tyrax::SaveTexts::LastPress));
  }
};
TYRA_OBJECT_SCRIPT(SaveCounter);

// ---------------------------------------------------------------------------
// D4. EventButton + EventLamp - USE sends an event from C++
// (tyrax::sendEvent). Every graph's On Event hears it, and so does every
// script's onEvent. The lamp also exposes `flash` to graphs with
// TYRA_SCRIPT_METHOD - its own graph calls it on the Square button.
// ---------------------------------------------------------------------------
class EventButton : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "The event USE sends")]]
  tyrax::EventId event = {-1};

  void onUsed(ScriptContext&) override {
    ++presses;
    tyrax::sendEvent(event, (float)presses);
    tyrax::debugLog("COOKBOOK event sent value=", presses);
  }

 private:
  int presses = 0;
};
TYRA_OBJECT_SCRIPT(EventButton);

class EventLamp : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Flashes when this event arrives")]]
  tyrax::EventId listenTo = {-1};
  [[tyra::prop(color, tooltip = "Flash colour")]]
  tyrax::Vec3 flashColor{1.0F, 1.0F, 0.3F};

  void onStart(ScriptContext&) override { idle = object.color(); }
  void onEvent(ScriptContext&, tyrax::EventId e, float value) override {
    if (e.index != listenTo.index) return;
    tyrax::debugLog("COOKBOOK lamp got event value=", value);
    flashFor(0.4F);
  }
  // Called by the lamp's flow graph (On Button Square -> Call Script).
  void flash(ScriptContext&) {
    tyrax::debugLog("COOKBOOK lamp flashed by a graph");
    flashFor(0.8F);
  }
  void onUpdate(ScriptContext&) override {
    if (timer.finished()) object.setColor(idle);
  }

 private:
  void flashFor(float s) {
    object.setColor(flashColor);
    timer.start(s);
  }
  tyrax::Vec3 idle;
  tyrax::Timer timer;
};
TYRA_OBJECT_SCRIPT(EventLamp);
TYRA_SCRIPT_METHOD(EventLamp, flash);

// ---------------------------------------------------------------------------
// GameManager - one per level, on an Empty. Scripts find it with
// tyrax::findScript<GameManager>(); it finds every die with findScripts.
// ---------------------------------------------------------------------------
class DiceRoller;  // motion_math.cpp - only pointers are used here
class GameManager : public ObjectScript {
 public:
  [[tyra::prop(min = 1, max = 60, tooltip = "Seconds between status lines in the log")]]
  float reportEvery = 10.0F;

  int areaVisits = 0;  // MoodZone reports here

  void onStart(ScriptContext& ctx) override {
    tyrax::debugLog("COOKBOOK manager start scene=", ctx.scene, " objects=", ctx.objectCount);
    report.start(reportEvery);
  }
  void onUpdate(ScriptContext& ctx) override {
    if (!report.finished()) return;
    report.start(reportEvery);
    tyrax::debugLog("COOKBOOK status t=", tyrax::sceneTime(), " areaVisits=", areaVisits,
                    " player=", tyrax::playerPosition(ctx).x, ",", tyrax::playerPosition(ctx).z);
  }
  void onSceneEnd(ScriptContext&) override {
    tyrax::debugLog("COOKBOOK manager sceneEnd areaVisits=", areaVisits);
  }

 private:
  tyrax::Timer report;
};
TYRA_OBJECT_SCRIPT(GameManager);

// ---------------------------------------------------------------------------
// E1. MoodZone - on an Area: entering changes the sky and switches fog on,
// with a small camera shake; leaving puts both back. Reports to the manager.
// ---------------------------------------------------------------------------
class MoodZone : public ObjectScript {
 public:
  [[tyra::prop(color, tooltip = "Sky while inside (0..1 RGB)")]]
  tyrax::Vec3 insideSky{0.45F, 0.2F, 0.55F};

  void onPlayerEnter(ScriptContext& ctx) override {
    savedSky = tyrax::Vec3(ctx.skyColor.r, ctx.skyColor.g, ctx.skyColor.b);
    tyrax::setSkyColor(ctx, insideSky * 255.0F);
    tyrax::setFog(ctx, true);
    tyrax::cameraShake(ctx, 0.08F, 0.4F);
    if (GameManager* gm = tyrax::findScript<GameManager>()) ++gm->areaVisits;
    tyrax::debugLog("COOKBOOK mood zone enter");
  }
  void onPlayerExit(ScriptContext& ctx) override {
    tyrax::setSkyColor(ctx, savedSky);
    tyrax::setFog(ctx, false);
    tyrax::debugLog("COOKBOOK mood zone exit");
  }

 private:
  tyrax::Vec3 savedSky{120.0F, 170.0F, 230.0F};
};
TYRA_OBJECT_SCRIPT(MoodZone);

// ---------------------------------------------------------------------------
// E2. CameraGlance - USE: fade a little, look from `eyeOffset` at `lookAt`
// (tyrax::setCamera) for `seconds`, then hand the camera back.
// ---------------------------------------------------------------------------
class CameraGlance : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "What the camera looks at")]]
  tyrax::ObjectRef lookAt;
  [[tyra::prop(tooltip = "Camera position relative to this object")]]
  tyrax::Vec3 eyeOffset{0.0F, 4.0F, -6.0F};
  [[tyra::prop(min = 0.5, max = 10, tooltip = "How long the glance lasts")]]
  float seconds = 2.5F;

  void onUsed(ScriptContext&) override { glance.start(seconds); }
  void onUpdate(ScriptContext& ctx) override {
    if (glance.running()) {
      const float left = glance.remaining();
      tyrax::setFade(ctx, left < 0.3F ? (0.3F - left) : 0.0F);
      tyrax::setCamera(ctx, object.position() + eyeOffset,
                       lookAt ? lookAt.position() : object.position(),
                       reinterpret_cast<uintptr_t>(this));
      active = true;
    } else if (active) {
      active = false;
      tyrax::releaseCamera(ctx);
      tyrax::setFade(ctx, 0.0F);
    }
  }

 private:
  tyrax::Timer glance;
  bool active = false;
};
TYRA_OBJECT_SCRIPT(CameraGlance);

// ---------------------------------------------------------------------------
// E3. RestartButton - USE reloads the scene (tyrax::restartScene); a scene
// picked in `target` switches to it instead (tyrax::switchScene).
// ---------------------------------------------------------------------------
class RestartButton : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Empty = reload this scene")]]
  tyrax::SceneId target = {-1};

  void onUsed(ScriptContext& ctx) override {
    tyrax::debugLog("COOKBOOK restart pressed");
    if (target.index >= 0) tyrax::switchScene(ctx, target);
    else tyrax::restartScene(ctx);
  }
};
TYRA_OBJECT_SCRIPT(RestartButton);

// ---------------------------------------------------------------------------
// PlayerWatch - a GLOBAL script (TYRA_SCRIPT, not attached to anything):
// catches a player who fell off the world and puts them back at the start.
// ---------------------------------------------------------------------------
class PlayerWatch : public Script {
 public:
  void update(ScriptContext& ctx) override {
    if (tyrax::playerPosition(ctx).y < -20.0F) {
      tyrax::teleportPlayer(ctx, tyrax::Vec3(0.0F, 1.0F, 0.0F), 0.0F);
      tyrax::debugLog("COOKBOOK player fell - teleported home");
    }
  }
};
// Inside the namespace block the plain class name is enough.
TYRA_SCRIPT(PlayerWatch);

TYRAX_NAMESPACE_END

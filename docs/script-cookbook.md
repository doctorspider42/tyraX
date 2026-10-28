# Script cookbook

This page is a collection of short, working examples for every part of the
[script API](script-api.md): each helper with a typical call, each kind of
public field with its declaration, and each callback with what it is good
for. The [script-cookbook example](../examples/script-cookbook) is a level
built from these snippets. Every station in it is one section here, so you
can read the code, select the object in the editor to see its fields, and
walk up to it in the game.

All snippets assume they are inside an object script (`object` is the
script's own object, and `ctx` comes from the callback) and that
`#include "scripts/script.hpp"` is at the top of the file. `tyrax::` is
spelled out every time. Write `using namespace tyrax;` inside a function to
drop it. Typed names such as `tyrax::Sounds::Knock` or
`tyrax::Layers::Main::Cellar` stand for your project's own sounds, layers
and so on: `inc/tyrax/names.gen.hpp` lists what your project has.

## Contents

- [Public fields, every kind](#public-fields-every-kind)
- [Vectors and math](#vectors-and-math)
- [Easing and tweens](#easing-and-tweens) (station A)
- [Random numbers](#random-numbers) (station B1)
- [Time: dt, Timer, Cooldown](#time-dt-timer-cooldown) (B2, B3)
- [Moving and changing objects](#moving-and-changing-objects) (B4)
- [Finding objects](#finding-objects) (C1)
- [Raycasts and line of sight](#raycasts-and-line-of-sight) (B5, B6)
- [Physics](#physics) (C1, C2)
- [Spawning](#spawning) (C3)
- [The player and input](#the-player-and-input) (C2, D1)
- [Sound and music](#sound-and-music) (D2)
- [Save data, HUD and World Facts](#save-data-hud-and-world-facts) (D3)
- [Events and flow graphs](#events-and-flow-graphs) (D4)
- [Scripts finding scripts](#scripts-finding-scripts) (GameManager)
- [Lifecycle callbacks](#lifecycle-callbacks) (E1, E4)
- [Camera and screen](#camera-and-screen) (E2)
- [Scenes and layers](#scenes-and-layers) (E3)
- [Global scripts](#global-scripts) (PlayerWatch)
- [Performance rules](#performance-rules)

## Public fields, every kind

Mark a member with `[[tyra::prop]]` and it becomes a field in
*Properties > Scripts*, with its own value on every object
([public fields](script-api.md#public-fields)). The station **F1
PropsGallery** declares one of each:

```cpp
// Numbers
[[tyra::prop(min = 0, max = 10, tooltip = "Slider when min and max are set")]]
float speed = 2.5F;
[[tyra::prop(tooltip = "A drag field without limits")]]
float offsetY = -1.0F;
[[tyra::prop(min = 1, max = 8)]] int count = 3;          // integer slider
[[tyra::prop]] bool enabled = true;                       // checkbox
[[tyra::prop(options = "Still|Bob|Spin|Both")]] int mode = 3;  // drop-down, stores the index

// Vectors and colours
[[tyra::prop]] tyrax::Vec3 offset{0.0F, 0.5F, 0.0F};      // three drags
[[tyra::prop(color)]] tyrax::Vec3 tint{0.95F, 0.45F, 0.85F};  // colour picker, 0..1

// Text
[[tyra::prop]] const char* label = "Hello";

// References to the project - picked from lists in Properties
[[tyra::prop]] tyrax::ObjectRef buddy;          // an object of the same scene
[[tyra::prop]] tyrax::SoundId sound = {-1};     // Project > Audio
[[tyra::prop]] tyrax::MusicId music = {nullptr};
[[tyra::prop]] tyrax::EventId event = {-1};     // typed, or picked from the graphs' events
[[tyra::prop]] tyrax::SceneId scene = {-1};
[[tyra::prop]] tyrax::SaveValueId counter = {-1};
[[tyra::prop]] tyrax::ActionId action = {-1};   // Tools > Input Map
[[tyra::prop]] tyrax::HudTextId banner = {-1};
[[tyra::prop]] tyrax::HudBarId bar = {-1};
[[tyra::prop]] tyrax::MenuId menu = {-1};
[[tyra::prop]] tyrax::PrefabId prefab = {-1};
[[tyra::prop]] tyrax::SequenceId cutscene = {-1};

// Decoration: a caption above a group of fields
[[tyra::prop(header = "Targeting")]] tyrax::ObjectRef target;
```

The values arrive **before `onStart`**. A field nobody set keeps the
initializer above. An id field left empty is `-1` (`ObjectRef` is empty), and
every API function treats it as "nothing": `playSound(ctx, sound)` with no
sound does nothing.

## Vectors and math

```cpp
tyrax::Vec3 a(1.0F, 0.0F, 2.0F), b = object.position();
tyrax::Vec3 mid = tyrax::lerp(a, b, 0.5F);
float d = tyrax::distance(a, b);              // also distanceSq, distanceXZ (ignores height)
tyrax::Vec3 dir = tyrax::normalize(b - a);    // zero vector stays zero, never NaN
float facing = tyrax::dot(dir, object.forward());   // 1 = straight ahead
tyrax::Vec3 side = tyrax::cross(dir, tyrax::Vec3::up());

float x = tyrax::clamp(value, 0.0F, 1.0F);
float y = tyrax::remap(health, 0.0F, 100.0F, 0.2F, 1.0F);  // clamped, like the Remap Range node
float z = tyrax::safeDiv(a.x, a.z);           // 0 instead of a division by zero

float yaw = tyrax::yawTo(object.position(), target.position());    // degrees, 0 = +Z
float step = tyrax::moveTowardsAngle(object.yaw(), yaw, 90.0F * tyrax::dt());
tyrax::Vec3 ahead = tyrax::forwardFromYaw(yaw) * 3.0F;
float wrapped = tyrax::wrapDeg(370.0F);       // 10
float turn = tyrax::deltaAngle(350.0F, 10.0F);   // 20: the short way round
```

## Easing and tweens

**A: EaseShowcase** is nine spheres, one per curve, so the curves can be
compared side by side:

```cpp
const float t = fmodf(tyrax::sceneTime(), period * 2.0F);           // ping-pong clock
const tyrax::Ease e = static_cast<tyrax::Ease>(curve);              // an options field
const float y = t < period ? tyrax::tween(t, period, 0.0F, height, e)
                           : tyrax::tween(t - period, period, height, 0.0F, e);
object.setPosition(base + tyrax::Vec3(0.0F, y, 0.0F));
```

`tyrax::ease(t, Ease::OutBounce)` shapes a 0..1 value on its own. `tween` also
takes `Vec3` endpoints. The curves are `Linear`, `In`, `Out` and `Smooth`
(the *Tween Value* node's four), plus `InCubic`, `OutCubic`, `InOutCubic`,
`OutBack` and `OutBounce`.

## Random numbers

**B1: DiceRoller** (press USE):

```cpp
const int face = tyrax::randomInt(1, 6);           // both ends included
if (tyrax::chance(0.25F)) object.moveBy({0, 0.5F, 0});   // 25 % of the time
float angle = tyrax::randomRange(-90.0F, 90.0F);
tyrax::Vec3 spot = object.position() + tyrax::randomInCircle(2.0F);
tyrax::seedRandom((uint32_t)tyrax::saveValue(ctx, tyrax::SaveValues::Seed));  // a different run
```

Graphs and scripts draw from one sequence. It starts from a constant, so a
run replays the same way unless you call `seedRandom`.

## Time: dt, Timer, Cooldown

```cpp
object.rotateBy({0.0F, 90.0F * tyrax::dt(), 0.0F});   // 90 deg/s at any frame rate
float t = tyrax::sceneTime();                        // seconds since the scene loaded
```

**B2: Blinker** shows `tyrax::Timer`, whose `finished()` is true once:

```cpp
tyrax::Timer timer;                       // a member
void onStart(ScriptContext&) override { timer.start(interval); }
void onUpdate(ScriptContext&) override {
  if (!timer.finished()) return;
  object.toggleVisible();
  timer.start(interval);                  // again
}
// timer.running(), timer.remaining(), timer.stop()
```

**B3: CooldownButton** shows `tyrax::Cooldown`:

```cpp
tyrax::Cooldown cooldown;
void onUsed(ScriptContext&) override {
  if (cooldown.use(2.0F)) { /* happens */ } else { /* still cooling down */ }
}
// cooldown.ready(), cooldown.reset()
```

## Moving and changing objects

**B4: TransformTour** cycles through one call per step:

```cpp
object.moveBy({0.0F, 1.0F, 0.0F});
object.setPosition(home);
object.rotateBy({0.0F, 0.0F, 45.0F});
object.setRotation(tyrax::Vec3());
object.setYaw(object.yaw() + 90.0F);
object.setScale({1.5F, 0.5F, 1.5F});        // or setScale(1.0F)
object.lookAt(tyrax::playerPosition(ctx), /*tilt*/ true);
object.spin({0.0F, 180.0F, 0.0F});          // keeps turning on its own - degrees per second
object.setColor({1.0F, 0.2F, 0.2F});
object.hide(); object.show(); object.toggleVisible();
object.setUsable(false);
object.setLight(true, 1.5F);                // Point Light with "Dynamic (live)"
object.playAnimation("Run", /*loop*/ true, /*speed*/ 1.0F, /*fade*/ 0.2F);
if (object.animationFinished()) { /* one-shot done */ }
```

Getters: `position()`, `rotation()`, `yaw()`, `scale()`, `forward()`,
`color()`, `visible()`, `usable()`, `active()`, `type()`, `index()`. The raw
`RuntimeObject` is `object.get()` or `object->`.

## Finding objects

```cpp
tyrax::ObjectRef gate = tyrax::object(ctx, tyrax::Objects::Main::Gate);  // typed, checked at compile time
tyrax::ObjectRef byName = tyrax::findObject(ctx, "gate");                 // runtime name, hashed once

// C1: Magnet - your own test as a plain function
bool isPhysicsBody(const RuntimeObject& o) { return o.data.physics != 0; }
tyrax::ObjectFilter bodies;
bodies.exclude = object.index();       // not myself
bodies.accept = &isPhysicsBody;        // or bodies.type = 0 (boxes), bodies.visibleOnly = true
tyrax::ObjectRef found[16];
int n = tyrax::overlapSphere(ctx, object.position(), 6.0F, found, 16, bodies);
tyrax::ObjectRef nearest = tyrax::findNearest(ctx, object.position(), 6.0F, bodies);
if (nearest) nearest.setColor({1.0F, 0.9F, 0.2F});
```

## Raycasts and line of sight

**B5: LaserTurret** sweeps a ray and parks a dot where it hits:

```cpp
const tyrax::Vec3 dir = tyrax::forwardFromYaw(object.yaw());
const tyrax::RayHit hit = tyrax::raycast(ctx, object.position() + dir * 0.8F, dir,
                                         /*maxDist*/ 30.0F, /*radius*/ 0.0F,
                                         /*skip*/ object.index());
laserDot.setPosition(hit.point);
if (hit.object) { /* a collision box */ } else if (hit.hit) { /* the terrain */ }
// hit.distance
```

`raycast` hits what blocks movement. `tyrax::raycastFromPlayer(ctx, 50.0F)`
is the *Raycast* node: what the player looks at. Any visible object counts,
with collision or without.

**B6: Sentry** turns green while it can see you:

```cpp
const tyrax::Vec3 eye = object.position() + tyrax::Vec3(0, 1, 0);
const bool sees = tyrax::lineOfSight(ctx, eye, tyrax::playerPosition(ctx), object.index());
float ground = tyrax::terrainHeight(ctx, x, z);
```

## Physics

For objects with *Physics: rigid body*. Speeds are in units per second:

```cpp
crate.addImpulse({0.0F, 9.0F, 0.0F});      // C2: LaunchPad
crate.setVelocity({2.0F, 0.0F, 0.0F});
tyrax::Vec3 v = crate.velocity();
crate.stopMotion();                         // velocity, tumble and spin to zero
if (crate.asleep()) { /* settled */ }
```

## Spawning

**C3: SpawnDemo** keeps four clones of a template alive:

```cpp
tyrax::ObjectRef clone = tyrax::spawn(ctx, templateObject, position, /*yaw*/ 45.0F);
if (clone) clone.setColor({0.5F, 1.0F, 0.5F});
clone.despawn();
int handle = tyrax::spawnPrefab(ctx, prefab, position, /*yaw*/ 0.0F, /*scale*/ 1.0F);
tyrax::despawnPrefabs(ctx, prefab);
```

## The player and input

```cpp
tyrax::Vec3 eye = tyrax::playerPosition(ctx);        // the camera / eye
tyrax::Vec3 look = tyrax::playerLook(ctx);
float d = tyrax::distanceToPlayer(ctx, object.position());   // nearer of two players
tyrax::teleportPlayer(ctx, {0.0F, 1.0F, 0.0F}, /*yaw*/ 180.0F);
tyrax::setPlayerInput(ctx, false);                    // freeze the walker (cutscene)
tyrax::setFlashlight(ctx, true);
tyrax::vibrate(ctx, /*big*/ 0.6F, /*small*/ false, /*seconds*/ 0.25F);

// Through the Input Map, so a rebind in the pause menu reaches scripts:
if (tyrax::actionPressed(ctx, tyrax::Actions::Jump)) { /* the frame it went down */ }
if (tyrax::actionHeld(ctx, boostAction)) { /* an ActionId field */ }
float trigger = tyrax::actionValue(ctx, tyrax::Actions::VehThrottle);
tyrax::Vec3 s = tyrax::stick(ctx, /*right*/ false);   // D1: StickTilt, -1..1 per axis
```

## Sound and music

**D2: SoundBox**:

```cpp
int ch = tyrax::playSound(ctx, sound, /*volume*/ 90, /*priority*/ 5);  // -1 = dropped
tyrax::playSound(ctx, tyrax::Sounds::Knock);                          // by its typed name
tyrax::playMusic(ctx, tyrax::Music::Theme, /*volume*/ 80, /*loop*/ true);
tyrax::stopMusic(ctx);
tyrax::setReverb(ctx, /*preset*/ 3, /*amount*/ 60);   // -1 = back to the zones
tyrax::setSoundBankLoaded(tyrax::SoundBanks::Mission1, true);
```

A sound only scripts use stays loaded. A sound named as
`tyrax::Sounds::X` is loaded for the whole game, and one picked in a field
loads with the object's scene.

## Save data, HUD and World Facts

**D3: SaveCounter** writes all of these on every press:

```cpp
tyrax::addSaveValue(ctx, presses, 1.0F);                  // a SaveValueId field
float n = tyrax::saveValue(ctx, tyrax::SaveValues::Presses);
tyrax::setSaveValue(ctx, presses, 0.0F);
tyrax::setSaveText(ctx, tyrax::SaveTexts::LastPress, "pressed the counter");
const char* last = tyrax::saveText(ctx, tyrax::SaveTexts::LastPress);
tyrax::setHudBar(ctx, bar, n);                            // instant = true to jump
tyrax::showText(ctx, banner, /*seconds*/ 2.0F);          // hideText(ctx, banner)
tyrax::setHudVisible(ctx, false);
tyrax::setFact(tyrax::Facts::CookbookPresses, n);         // Tools > World Facts
float v = tyrax::fact(tyrax::Facts::CookbookPresses);
tyrax::saveCheckpoint(ctx); tyrax::loadCheckpoint(ctx); tyrax::openSaveMenu(ctx);
```

## Events and flow graphs

**D4: EventButton and EventLamp**:

```cpp
// Send: every graph's On Event and every script's onEvent hear it next frame.
tyrax::sendEvent(event, /*value*/ 1.0F);                  // an EventId field
tyrax::sendEvent(tyrax::Events::CookbookPing);            // or its typed name

// Receive in a script:
void onEvent(ScriptContext&, tyrax::EventId e, float value) override {
  if (e.index == listenTo.index) flash();
}
// or poll: float v; if (tyrax::eventReceived(tyrax::Events::CookbookPing, &v)) ...

// Let a graph call into the script: Call Script "EventLamp.flash"
void flash(ScriptContext&) { /* ... */ }
TYRA_SCRIPT_METHOD(EventLamp, flash);   // after the class
```

The lamp's own graph is *On Event "CookbookPing" → Log*, plus *On Button
Square → Call Script "EventLamp.flash"*. Naming `tyrax::Events::X` in code,
or typing a name into an `EventId` field, creates the event.

## Scripts finding scripts

```cpp
GameManager* gm = tyrax::findScript<GameManager>();          // the first one in the scene
DiceRoller* die = tyrax::getScript<DiceRoller>(someObject);  // the one on that object
DiceRoller* all[8];
int n = tyrax::findScripts<DiceRoller>(all, 8);
```

All of these work from `onStart` already, because every instance of the scene
exists before the first `onStart` runs. **E1: MoodZone** reports each visit
to the level's `GameManager`.

## Lifecycle callbacks

**E4: LifecycleLogger** sits on an Area and logs every callback as it fires:

```cpp
void onStart(ScriptContext&) override;              // fields are already set
void onUpdate(ScriptContext&) override;
void onLateUpdate(ScriptContext&) override;         // after everyone's onUpdate
void onUsed(ScriptContext&) override;               // USE on a Usable object
void onEvent(ScriptContext&, tyrax::EventId, float) override;
void onPlayerEnter(ScriptContext&) override;        // self is an Area
void onPlayerExit(ScriptContext&) override;
void onAnimationFinished(ScriptContext&) override;
void onActivated(ScriptContext&) override;          // streaming layer back
void onDeactivated(ScriptContext&) override;        // streaming layer gone
void onSceneEnd(ScriptContext&) override;           // self is already empty here
```

**E1: MoodZone** changes the sky and turns fog on while you are inside:

```cpp
void onPlayerEnter(ScriptContext& ctx) override {
  tyrax::setSkyColor(ctx, insideSky * 255.0F);    // a color field, 0..1 -> 0..255
  tyrax::setFog(ctx, true);
  tyrax::cameraShake(ctx, 0.08F, 0.4F);
}
```

## Camera and screen

**E2: CameraGlance** looks somewhere else for a moment:

```cpp
tyrax::setCamera(ctx, eye, target, reinterpret_cast<uintptr_t>(this));   // every frame while it lasts
tyrax::releaseCamera(ctx);
tyrax::setFade(ctx, 0.5F);                    // 0 clear .. 1 black
tyrax::cameraShake(ctx, /*amplitude*/ 0.15F, /*seconds*/ 0.3F);
tyrax::setBloom(ctx, 0.5F); tyrax::setGrain(ctx, 0.2F);
tyrax::setLensFlare(ctx, 1.0F); tyrax::setGodRays(ctx, 0.5F); tyrax::setParticles(ctx, false);
```

## Scenes and layers

**E3: RestartButton**:

```cpp
tyrax::restartScene(ctx);
tyrax::switchScene(ctx, target);                      // a SceneId field, or tyrax::Scenes::Level2
tyrax::setLayerLoaded(ctx, tyrax::Layers::Main::Cellar, true);
if (tyrax::layerLoaded(ctx, tyrax::Layers::Main::Cellar)) { /* resident */ }
tyrax::playSequence(tyrax::Sequences::Intro); tyrax::stopSequence();
tyrax::playCredits(tyrax::Credits::Ending);
tyrax::openMenu(ctx, menu);
```

## Global scripts

A global script is not attached to any object. It runs every frame in every
scene. **PlayerWatch** puts a player who fell off the world back at the
start:

```cpp
class PlayerWatch : public Script {
 public:
  void update(ScriptContext& ctx) override {
    if (tyrax::playerPosition(ctx).y < -20.0F)
      tyrax::teleportPlayer(ctx, tyrax::Vec3(0.0F, 1.0F, 0.0F), 0.0F);
  }
};
TYRA_SCRIPT(PlayerWatch);   // inside the namespace block, plain name
```

## Performance rules

- **Moving is cheap, recolouring is not.** The transform calls put the object
  on the matrix path. `setColor` and `setScale` rebuild the mesh, so call
  them when the value changes. Passing the same value again is free.
- **Searches run over the scene's objects**: `overlapSphere`,
  `findNearest` and `findObject`. A few per frame are nothing. Hundreds per
  frame on a large scene are not.
- **Raycasts** test the objects near the ray, then march over the terrain.
  The cookbook's turret and sentry cast every frame at 50 FPS.
- Profile on the console. PCSX2 shows when something is wrong, not how fast
  the real EE is.

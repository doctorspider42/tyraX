# script-cookbook example

![script-cookbook example in PCSX2](preview.png)

A level that uses the whole C++ script API. Each station shows one part of
it working: easing, random numbers, timers, every transform call,
raycasts, searches, physics, spawning, input, sound, save data, the HUD,
World Facts, events to and from flow graphs, scripts finding scripts, every
lifecycle callback, the camera and the scene. Every object is configured
only through its scripts' **public fields** (`[[tyra::prop]]`). Select one
and look at *Properties > Scripts*.

It is the playable companion of [docs/script-cookbook.md](../../docs/script-cookbook.md),
which has the same snippets with explanations, section by section.

```powershell
build\tyrax-editor.exe --build examples\script-cookbook --run
```

The code is in [`src/scripts/cookbook/`](src/scripts/cookbook), four files
with one class per station:

| File | Stations |
|---|---|
| [`motion_math.cpp`](src/scripts/cookbook/motion_math.cpp) | A EaseShowcase, B1 DiceRoller, B2 Blinker, B3 CooldownButton, B4 TransformTour |
| [`queries_physics.cpp`](src/scripts/cookbook/queries_physics.cpp) | B5 LaserTurret, B6 Sentry, C1 Magnet, C2 LaunchPad, C3 SpawnDemo |
| [`game_systems.cpp`](src/scripts/cookbook/game_systems.cpp) | D1 StickTilt, D2 SoundBox, D3 SaveCounter, D4 EventButton + EventLamp, GameManager, E1 MoodZone, E2 CameraGlance, E3 RestartButton, the global PlayerWatch |
| [`props_gallery.cpp`](src/scripts/cookbook/props_gallery.cpp) | F1 PropsGallery (one field of every kind), E4 LifecycleLogger |

## Walking through it

You start facing down the course. Objects named `a-…` to `f-…` in the
Outliner match the rows. Press **USE** (X) on anything usable. Debug builds
print every station's reaction to `bin/log.txt` and the Output panel as
`COOKBOOK …` lines.

| Row | What to do | What it shows |
|---|---|---|
| **A** (z 6) | watch | nine spheres, one per `tyrax::Ease` curve, set with an options field |
| **B** (z 13) | USE the white die, the green button; walk behind the wall at the right | `randomInt`/`chance`/`addSaveValue`; a blinking `Timer`; a `Cooldown` (red while cooling); `TransformTour` cycling through `moveBy`/`rotateBy`/`setYaw`/`setScale`/`lookAt`/`spin`; a turret whose `raycast` parks a red dot on what it hits; a sentry that is green while `lineOfSight` reaches you |
| **C** (z 22) | walk near the red magnet; stand on the blue pad and press **Jump** | `overlapSphere` + `addImpulse` pull the crates, `findNearest` tints the closest; the pad's `ActionId` field + `actionPressed` launches the balls, with `vibrate` and `cameraShake`; `spawn`/`despawn` keep four clones alive |
| **D** (z 30) | tilt the left stick (hold Sprint); USE the cyan box, the white counter, the orange button; press **Square** | `stick`/`actionHeld`; `playSound` from a `SoundId` field; a save value, its HUD bar, a HUD text, a save text and a World Fact in one press; an event sent from C++ that both a script (`onEvent`) and the lamp's graph (*On Event → Log*) hear, and the graph's *Call Script* running `EventLamp.flash` |
| **E** (z 38) | walk into the purple box, then the green box; USE the yellow pillar and the red button | `onPlayerEnter`/`onPlayerExit` changing the sky and fog; `setCamera` + `setFade` for a glance; `restartScene` (with `onSceneEnd` in the log); a `LifecycleLogger` printing callbacks |
| **F** (z 44) | select the pink box in the editor; USE it in the game | `PropsGallery`: a slider, a drag, an int slider, a checkbox, a drop-down, a vector, a colour, text, and object, sound, event, scene, save-value, action, HUD-text, HUD-bar and menu references. Its log line prints what the game received |

## Project data the stations use

The level adds a little project data for the stations to use: the sound
`res/sfx/knock.wav`, the save values `rolls` and `presses`, the save text
`last-press`, the HUD text `pressed`, the HUD bar `presses` (bound to the
save value), and the World Fact `cookbook.presses`. The scripts reach all of
them through typed names (`tyrax::SaveTexts::LastPress`,
`tyrax::Facts::CookbookPresses`) or through fields.

[`authoring/build-scene.py`](authoring/build-scene.py) rebuilds the whole
level (objects, field values, graphs and project data) on top of a fresh
`--new script-cookbook . 80 80 fpp`.

## Measured

PCSX2, native build, debug profile: **50 FPS** standing at the start and
next to the turret and sentry, which raycast every frame. Detaching every
script also gives 50. Before the script API put moved objects on the
matrix path, this level ran at 12.5 FPS. See *Performance rules* in the
cookbook.

## Try next

- Change a field (a sphere's `curve`, the turret's `sweep`, the pad's
  `launchAction`) in *Properties > Scripts* and Build & Run.
- Add your own station: copy a class, change it, attach it, and give it
  fields.
- Package the stations you like as a [script extension](../../docs/script-extensions.md).

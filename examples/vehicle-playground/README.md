# Motor District

Version 1.177.0 turns on *Reflect a baked background*: the car's reflection
probe draws the ground and the box-proxy buildings from an editor-baked
panorama per road cell instead of re-rendering them
([the baked background](../../docs/reflective-materials.md#the-baked-background-11770)).
Bloom is off (0, was 0.12): on a physical PS2 it cost 0.32 ms of GS and
~0.3 ms of parked EE work for a glow that changed at most 6 colour levels
([measurement](../../docs/emissive-materials.md#what-the-pass-costs-on-a-ps2-measured-2026-10-20)).
*Bake static lamps* is on and night lamp 7 no longer flickers (0.025 → 0),
so all eight district lamps fold into the static vertex colours at the mood
change. Physical PS2, one ELF (rig CZ17, 10 cycles an arm): parked -0.12 ms,
driving -0.32..-0.67 ms; under 0.5 % of pixels change, at most 30 levels
([baked lamp light](../../docs/dynamic-light-receivers.md)).
The ground shadow maps draw as their own pass (*Fold into the terrain colour*
is off since 1.177.6). The fold saved 0.4-0.75 ms on a physical PS2, but it
turned building shadows into soft 4-unit blobs; with the pass the district
still holds 60 fps ([shadows](../../docs/shadows.md)).

Version 1.173.5 separates terrain residency/LOD focus from visual camera shake,
avoiding repeated terrain builds while the car sits on a detail snap boundary.
Roads, normal detail and live reflections remain enabled. Both-order physical
day/night A/B qualifies the fix, not stable 60 fps. Rebuild with the updated
editor; see [the qualification](../../docs/tyrax2-terrain-focus-2026-10-11/README.md).

Generated paint code includes the default-OFF `TYRA_VU1_EXP_PAINT` experiment.
Both engine and game must use the same flag. It retains accepted paint basis
across VU/EE fallback and invalidates normal maps on content changes; this is
not yet a hardware-qualified default. See [the paint investigation](../../docs/tyrax2-reflection-turn.md).

Version 1.173.3 preloads qualifying ground-shadow sets during scene loading,
with a 512 KiB accounted EE-memory cap. Rebuild from the updated editor to
regenerate this runtime path; no authored scene or vehicle setting changes.
See [the hitch investigation](../../docs/tyrax2-road-free-hitch.md).

Version 1.173.2 enables [terrain strip refinement](../../docs/tyrax2-strip-subranges.md)
for every generated terrain pass. Rebuild with the current in-tree engine;
the example's authored scene and vehicle settings do not need changing.

A playable PS2 district for TyraX vehicles. Open `vehicle-playground.tyra` in
TyraX and build the project. The main scene starts in the Ravager; press
**Square** to get out and enter the nearby Pica Turbo or Strix V12. The first
time you sit in each car a controls card lists its buttons with their glyphs,
dimming each one as you try it ([Controls card](../../docs/vehicles.md#controls-card));
**Select** hides it. **D-pad down** repairs the car you are driving (the
Ravager's flow graph, a *Repair Vehicle* node).

The current seated start is the primary [TyraX2 hardware control](../../docs/tyrax2.md).
`tools/tyrax2-fixture.py` makes isolated day/night copies with host polling off,
including separate public-runtime `pipeline-check` and `pipeline-timing` arms.
Enable **Project > Preferences > Display > TyraX2 frame pipeline** and rebuild
to try ordered single-chain rendering with EE/GPU overlap (off by default;
about 2.25 MiB EE RAM and one queued frame of latency). Keep the authored
camera, video mode and quality when comparing frame times. The runtime record
separates the first final day hardware result from pending day/night repeats.

For native build timing, use a scratch copy of this example and warm the cache
with one build before timing unchanged, script-only and scene-edit iterations.
Unchanged builds keep the ELF timestamp; scene-table edits can still recompile
the large generated `terrain_game.cpp`. See [native builds](../../docs/native-toolchain.md#incremental-builds).
On a warmed Windows/WSL debug build, a one-script comment edit measured 50.5 s
with the old Windows-path build and 12.8 s with the WSL cache; unchanged native
builds measured 6.7-7.6 s. This excludes editor generation/baking and cold setup.

![Motor District running in PCSX2](preview/district.png)

## What's in the project

- **main** is the driving district: seven roads, a garage, a cobbled crossing,
  dirt service lane, buildings, lights, pushable props and three cars.
- **dense** adds buildings for a busier drive.
- **procedural** demonstrates painted building and tree placement. Open its
  Procedural layout to inspect the saved graph.

The three vehicle definitions use Blender-built full and far models. Their
shared defaults provide engine loops, tyre squeal, shift sound, headlights and
damage. Open **Tools > Vehicle Editor** to inspect each model, tune driving,
disable visual or performance damage, preview the wheels and audition the
engine. The high-rev recording can be switched off while the idle recording
continues to follow RPM.

The buildings and props cast baked sun shadows. On the ground they come from
one 4-bit shadow map per terrain chunk (128 px). On roads, walls and plinths
they come from decals (64 px per shadow). See *Ambience Editor > Baked
lighting* and [docs/shadows.md](../../docs/shadows.md).

The sky is a painted panorama (FreeStylized Skybox 131) on the ambience
preset, tinted by the day/night cycle - see [docs/sky-texture.md](../../docs/sky-texture.md).

## Make your own vehicle

Start with the [Blender vehicle tutorial](../../docs/blender-vehicle-modeling.md).
It covers wheel objects, axes, materials, export and inspection. The editable
[`ravager.blend`](authoring/ravager.blend) and
[`make-ravager.py`](authoring/make-ravager.py) show two ways to start. The
current cars are generated by `make-ravager.py`, `make-pica.py` and
`make-strix.py`; their `-far.py` companions build distant models from the same
atlas. `carkit.py` is shared by Pica and Strix.

Run the scripts from the project directory with Blender. Generate each full
model before its far companion, then reopen the project and rebuild so TyraX
rebakes the imported assets. For example:

```sh
blender -b --factory-startup --python authoring/make-ravager.py
blender -b --factory-startup --python authoring/make-ravager-far.py
```

`authoring/add-fast-wheels.py` makes the optional fast-wheel variants, and
`authoring/add-exhaust-markers.py` writes each car's `exhaust` empties (its
`EXHAUSTS` list) into the shipped GLB without re-exporting it - the nitrous
flame, the upshift pop and the exhaust smoke come out of those
([Exhaust pipes](../../docs/vehicles.md#exhaust-pipes)). The
district and procedural scene have their own `build-district.py`,
`make-plaza.py` and `make-procedural-scene.py` sources. Run scene authoring
scripts on a copy when you want to replace an edited scene.

For model import, driving controls, damage and sounds, see the
[vehicle guide](../../docs/vehicles.md). Road editing is covered in
[Roads](../../docs/roads.md), and scatter placement in
[Procedural generation](../../docs/procedural-generation.md).

## Credits

The shipped [third-party notices](THIRD-PARTY-NOTICES.txt) list the engine and
assets used by this project. Keep them with redistributed builds.

Generated object values live in `src/gen/scene_objects.gen.cpp`;
`inc/scene_data.hpp` keeps stable declarations. Counts and object IDs live in
the same data file, so ordinary moves, color edits, additions and removals can
rebuild it alone. Changes to features or derived tables can still rebuild consumers.

Generated game methods are split between `src/terrain_game.cpp` and the
`src/gen/game_*.gen.cpp` subsystems, with shared inline helpers/state in
`inc/game_runtime.gen.hpp`. Header changes can compile these units in parallel.
The main file remains user-ownable; generated subsystem files refresh on build.


Hardware timeline instrumentation matches the 1.170 coarse/detail emitter.
Use the [capture and observer-control guide](../../docs/hardware-profiler.md)
when comparing performance; traces add measurable work.

This example explicitly requests `framePipeline: true` (TyraX2). Its authored progressive output preserves the existing scan mode and quality; ordinary interlaced output is eligible, unlike true field rendering. True field rendering, BLSS, unlimited triple buffering and unavailable queue memory fall back safely. This request does not guarantee 60 FPS. See [TyraX2](../../docs/tyrax2.md).

The menu day/night mood selects and pauses the cycle once per mood or scene
change. Physics keeps real dt; the sky, fog, moon/stars and grading still
evaluate at the exact selected hour. The generated script API is described in
[day and night cycle](../../docs/day-night-cycle.md#a-fixed-mood-selected-in-a-game-menu).

Generated runtime snapshot refreshed for [dynamic light receivers](../../docs/dynamic-light-receivers.md). This example retains the default **All objects** policy; Players and driven vehicle is an optional project or scene setting.

The TyraX 2.0 integration keeps the redesigned Default layout and the TyraX2
renderer settings. The batch-twin oracle models the current reflected-batch
and baked-lamp fields when compiling the live generated grouping function.

TyraX 2.0 integration: generated sources have been refreshed with the combined
TyraX2 renderer and mainline collision broad phase; authored settings and
user-owned source files retain their existing ownership rules.

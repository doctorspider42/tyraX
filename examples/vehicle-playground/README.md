# Motor District — vehicle playground

A compact PS2 driving district with a connected street network, three driveable
car models and live scenery reflections. Open `vehicle-playground.tyra` in
TyraX, build and run, then press Square beside the gold coupe.

![Motor District in PCSX2](preview/district.png)

Actual software-renderer captures: [Tristar Racer](preview/tristar.png), [coupe](preview/coupe.png) and
[Rally 04](preview/rally.png). Night mode: [street](preview/night.png) and
[pause-menu selection](preview/night-menu.png).

## The district

- Seven spline roads: a wide perimeter loop, Garage Boulevard, two cross-city
  links, Market Street, a western service lane and the eastern crest run. The
  three eastern crossings use generated four-triangle asphalt junction patches;
  the older central/western crossings retain their deliberately larger drift
  aprons. Both road and junction surfaces are assigned through reusable `.mtl`
  assets; changing either material's `map_Kd` updates every road that uses it.
- Fourteen workshop, loft and tower blocks assembled from Kenney's Retro Urban
  Kit, with pavements, trees, benches, traffic signals, streetlights, dumpsters
  and barriers. The garage sign and road/ground textures are original assets.
- A garage apron and western yard for handbrake turns. Loose crates and pallets
  can be pushed; buildings, street furniture and perimeter walls collide.
- A flat city floor and gentle eastern crests. Roads and wheel contacts use the
  same terrain. The district fits within the existing 320 × 320 metre boundary.
- Five placed vehicles: the hero CC96 coupe, a parked orange Rally 04, a Tristar Racer and two AI
  patrols. The rivals remain driveable and resume their route after you get out.

The CC96 has a 29 m/s target speed before nitrous, more steering authority at
speed and a four-second refillable tank. Rally 04 is a lighter, deliberately
more slippery alternative with a longer wheelbase and more suspension travel.
These are arcade settings, not a real-world vehicle simulation.

The Tristar Racer parks west of the coupe, opposite the Rally. It has a 32 m/s
target speed, stronger grip and the same refillable nitrous controls. Its original prepared source
body has 2276 triangles. The lean game variant bakes to 789 body triangles and
139 per wheel (1345 total near geometry, versus the CC96's 2372). The original
prepared GLB remains available; the game uses `tristar-lean.glb`. It uses a cheap
blob shadow and a 48 m distance tier.

The five-speed boxes use a 1.28 spread, 0.10 s shifts and reduced ratio torque.
Body lean is 0.25 on the coupe/Tristar and 0.35 on the van. A 60 Hz flat-ground
full-throttle comparison against the previous settings measured:

| Vehicle | First upshift before → after | Largest shift pitch swing before → after |
|---|---|---|
| CC96 | 18.7 → 36.8 km/h | 3.94° → 1.24° |
| Rally 04 | 17.0 → 32.7 km/h | 3.93° → 1.60° |
| Tristar Racer | 20.4 → 41.0 km/h | 3.95° → 1.24° |

All three completed four clean upshifts. Target top speeds remain 29 / 26 / 32 m/s;
the lower gears are longer rather than the whole car becoming faster.

## Controls

| Input | Action |
|---|---|
| Start | Pause menu, including Day / Night |
| Square | Enter / leave the nearest vehicle |
| Left stick | Steer; vertical axis also supplies throttle / reverse |
| R2 / L2 | Throttle / brake |
| Circle | Handbrake |
| Cross | Nitrous |
| Triangle | Chase / bumper / far camera |
| Right stick / R3 | Look around / rear view |
| D-pad up | CC96 headlamps |

The coupe retains its engine/rev crossfade, tyre squeal, gear-shift sound,
brake lamps, suspension and projected silhouette. The parked Rally also uses
a projected silhouette; AI cars use cheaper blob shadows.

## Reflections and cost

All three definitions use the dynamic `@sky` paint map, rather than the former
`nfs-streaks.png`. Seven nearby building blocks have **Show in reflections**
enabled: their geometry is rendered into the live 128 × 128 environment target
along with the sky. Other scenery stays out of this extra pass. This is the
shared PS2 sphere-map approximation, refreshed every other frame; it is not
ray tracing, a cubemap or an accurate mirror of everything around the car.
The editor's sky-only approximation cannot prove scenery reflections: inspect
those in the running game. The target retains the level camera basis from its
own capture, so a skipped update cannot make reflected buildings swim when the
driver turns the camera.

The CC96 bake uses 1116 body triangles and 314 per wheel. Rally 04 has only
364 body triangles and 28 per wheel. Wheels share a submission within each vehicle definition; distinct models keep
their own texture bindings.
The GGBot source has disconnected wheel islands inside one mesh: preparation
separates them and gives each node a distinct material slot so the importer
retains ownership. The source texture and silhouettes are preserved.

Road spans retain their sampled shoulders and discard redundant interior vertices
when a non-flat span lies on the same 3-D surface plane and its station pair is
affine, preserving texture coordinates exactly. The existing curved-flat
reduction remains unchanged. A 13 m planar street uses 26 times fewer vertices
per station; curved non-flat spans, crowns, banks, saddles and changing terrain
retain the dense 0.5 m cross samples.
Chunk culling and the 1 m longitudinal sampling remain in place. This matters
for EE RAM as well as drawing: the initial dense district exhausted its budget.
The boot log reports `ROADS ... chunks ... vertices ...` for inspection.

## Reproduce and verify

The committed scene and assets are ready to build. To regenerate the district:

```
python authoring/build-district.py
```

Requires Python 3 and Pillow (including the sized default font API). It reads
only the bundled Kenney OBJ inputs, writes the deterministic terrain, textures,
building kitbashes and scene objects, and replaces authored roads / the former
pillar course. Keep hand-authored map changes separately before rerunning it.
Afterward use `tyrax-editor --resave <project>` and `--refresh-gen <project>`.

`authoring/prepare-ggbot.py` documents the optional Blender preparation from
the original `Car4.blend` and `car4_lightorange.png`; the resulting GLB is
already included, so Blender and `C:\Assets` are not build dependencies.

Run `python authoring/verify-road-twins.py` with g++ on PATH to compare the
actual editor tessellator against the extracted generated runtime and a compiled
preserved baseline (flat collapse retained; non-flat spans dense), including flat
terrain, crowns with equal-height shoulders, slopes, saddles, curves, three
heightfield fixtures on the district's own 4-unit ground, and scene revisits.
It also pins the automatic-junction primitive: perpendicular centre lines
produce one finite 12-vertex patch, while a near-parallel pair produces none.
It prints its worst surface error, its worst UV drift in texels and its worst
T-vertex seam per fixture, so the numbers behind the lateral budget
([docs/roads.md](../../docs/roads.md)) can be read rather than trusted. Also run
`tyrax-editor --vehicle-check`, build and boot the game, then drive with `--pad`
and capture with `--capture-frame`. Host checks alone are not evidence of
console frame rate or reflection correctness.

For physical-console smoke testing, the game must reach its first idle frame
after the loading screen with empty tyre-smoke and skid pools and with the
parked vehicles' lights visible. This covers every fixed-capacity vehicle
effect buffer's scene-setup lifetime: PCSX2 maps address zero and can hide a
null `Vec4::set` that real hardware reports as a cause-3 TLB store miss.

`python authoring/road-lod-2026-09-16/road-budget-sweep.py` sweeps the road's
two lateral budgets over the district's REAL roads and heightfield and prices
each setting against a reference commit's surface. It reproduces the console's
own `ROADSTRIP` producer line exactly, so it is an oracle rather than an
estimate, and it needs neither a console nor an emulator.

`python authoring/make-lod-variants.py <empty-directory>` makes isolated
model-only and model-plus-terrain LOD candidates. The checked-in project keeps
both distances at zero until an equal-camera PCSX2 run verifies the vehicle
silhouettes, lights, reflections and every terrain-projected road crossing;
coarse terrain can otherwise appear through or over a road. That last risk is
now a number rather than a warning — `authoring/road-lod-2026-09-16/terrain-lod-burial.py`
measures the coarse ground against the road's 0.12-unit lift at every dense
sample of all seven roads — and so is the transition
(`terrain-lod-step.py`, 0.57 pixels at a 160-unit band). What is still missing
is the cost a parked fixture cannot see: the tile rebuild while the player
moves.

## Verified on Windows / PCSX2

The release editor and native PS2 game build successfully. The road twin oracle
and `--vehicle-check` pass; the game was booted and driven in PCSX2's software
renderer. The district uses 93,150 road vertices instead of 281,748 (66.9% fewer).
Bounded span packing reduces the runtime road chunk count from 171 to 90 while
preserving those vertices and UVs. Chunks contain at most 36 spans and target
1,800 vertices, keeping culling local on dense slopes. The seven terrain fixtures
in the road twin oracle still match the editor geometry, including scene revisits.
A stationary hide/show/restore probe changes 204 car pixels when reflected
buildings disappear and restores the original car image exactly. The embedded
128 × 128 Rally texture is preserved byte-for-byte. These are emulator checks,
not a hardware PS2 or Linux editor validation.
Both definitions were inspected together after separating their wheel batches:
the coupe retains its black/white tyres and Rally retains its textured wheels.
Throttle, nitrous, handbrake and braking were exercised with automated pad input.

### Performance check (2026-09-12)

The district combines bounded road chunks with the conservative whole-model
reject and cross-material transform cache from `codex/static-model-performance`
(`54703b12`, `2afe5566`). Assets, lights and reflections remain enabled.

At the unchanged on-foot spawn camera in PCSX2's software renderer (PAL,
512 x 512, debug build), medians of 12 fresh telemetry samples at 0.5-second
intervals were:

| Build | Day FPS | Night FPS |
|---|---:|---:|
| District after merging main (`0f35b451`) | 20.0 | 17.3 |
| Bounded road chunks (`bbf9ae96`) | 23.6 | 19.2 |
| Road chunks + static-model optimizations | 25.0 | 22.1 |

Samples were taken outside render-cost captures. Separate serialized day
captures reported Total 42.861 -> 36.562 ms; those instrumented timings are
attribution evidence, not ordinary frame times. The original and final day
GS images differed in **zero of 262,144 pixels**. A subsequent night drive
covered about 100 m, with the car body/wheels staying together and a working
streetlight visible after braking. These are short emulator comparisons with
active AI traffic, not a minimum FPS promise for the entire map or real PS2.

## Day / night from the pause menu

Press **Start**, select **TIME OF DAY**, and use Cross or left/right to choose
**DAY** or **NIGHT**. Resume with Start or Triangle. The mood applies on resume
without reloading the scene or moving the player, cars or AI traffic.

The same district uses a live day/night ambience track pinned to noon or
midnight by `src/scripts/district_mood.cpp`. The menu writes the named
`district-night` save value. The script drives the existing sky, moon, stars,
fog and runtime world grade, then switches eight dynamic street/garage spots
and eleven emissive window/neon pieces together. One service lamp flickers
subtly. The day starts with the night dressing off.

Eight lights are the existing scene budget; their projected pools and coronas
provide local illumination without a second terrain or a second scene. Shadow
volumes are disabled on these lamps. The generated authoring header records the
night dressing indices; rerun the district authoring script after changing its
object order. Other scene objects keep their normal visibility.

This uses the hybrid runtime lighting path: geometry shading stays baked at
noon, while the world grade supplies the night brightness/tint and dynamic
spots supply local light. It does not claim separately baked night GI or
perfect moonlit shadows. See [Day and night cycle](../../docs/day-night-cycle.md).

`authoring/prepare-tristar.py` converts the supplied FBX with Blender, detaches
its wheel hierarchy while preserving world transforms, copies shared mesh data
and material slots, makes the rims visible from both sides of the repeated
wheel mesh, and exports GLB. The vehicle bake performs wheel reduction.
The model is included as part of this game example, not as a standalone asset
pack. See its usage notice below before reusing it elsewhere.

## Hardware timeline tools

Build with the current engine and use [hardware-trace.py](../../tools/hardware-trace.py)
to arm a bounded boot capture and export HTML/Perfetto. Keep fixed cameras and
complete assets; capture after the benchmark window so trace export does not
contaminate the 960 timing rows. [Profiler guide](../../docs/hardware-profiler.md)
explains the VIF/GS interpretation limits and isolated raster/lighting probes.
The [seven physical controls](../../docs/hardware-profiler-results.md) identify
host I/O and EE-side static submission as priorities; suppressing GS raster area
alone saves only a small part of the missing frame budget.

## Physical full-asset recheck (2026-09-14)

The coordinator independently measured both experimental engine patches on PS2,
with 960 warmed frame samples per arm and a repeated baseline. Neither the
transform cache nor DMA clip-table candidate demonstrates a useful net gain.
Earlier agent fixtures lacked 66 PNGs and 11 TMDLs and are invalid as full-scene
evidence. Complete deployments and matching day-view GS captures correct that
failure. See [hardware recheck](../../docs/performance-hardware-recheck.md)
and [raw evidence](authoring/hardware-recheck-2026-09-14/).

## Repeating performance comparisons

`python authoring/benchmark-district.py <new-directory>` creates an isolated
fixture with parked traffic, fixed cameras and an identical sampler in debug
and release. Add `--profile quiet-debug` or `--profile release` to compare live
tooling overhead; add `--mesh-lod 64` and/or `--terrain-lod 96` for LOD trials.
The original example is never overwritten.

**`python authoring/inventory-frame.py <fixture>` answers a different question
from the frame-cost instrumenter**: not where the milliseconds go, but what the
frame is MADE OF. It brackets every producer in `renderScene` with a telemetry
drain — which is an exclusive split, because `takeTelemetry()` clears as it
reads — and writes `bin/frame-inventory.csv` with triangles, VU1 packages,
bags, vertices and packet flushes per producer, per pose, plus one row per solo
object and per object drawn into the reflection probe.
`python authoring/summarize_inventory.py <fixture> --phase 0` prints it. Counts
only: it is applied INSTEAD of `instrument-frame-cost.py`, PCSX2 is enough, and
its own milliseconds are meaningless by construction. The garage-day reading —
solo static models 44.8%, terrain 13.4%, the reflection probe 13.1%, roads
8.1% — is in
[the reflection-probe round's evidence](authoring/reflection-probe-2026-09-16/README.md).

It also splits `proj_shadows` three ways — the caster's own bags, the receiver
patch and the torch's wall copy — because "projected shadows are badly packed"
turned out to be a statement about the CASTERS' models rather than about the
shadow. The split, and the packing round it settled, are in
[the wheel-strip round's evidence](authoring/wheel-strip-2026-09-17/README.md).
`python authoring/wheel-strip-2026-09-17/compare_packing.py ctl=<dir> ...`
prints any number of arms side by side, and
`wheel-strip-report.py <dir>/.res-baked/vehicles` says what the baked wheel
models actually carry — which a generated-source grep cannot see, because half
of that change lives in an asset.

**`authoring/world-visibility-2026-09-17/` asks what the frame is SEEN to draw**,
which the inventory above cannot say. `visibility-sampler.py` parks the camera
at the garage-day pose and hides objects named in a command file at runtime;
`probe-visibility.ps1` photographs one probe after another **from a single
boot** through the game's own `--capture-frame` channel, so the whole
142-object population costs one build and one boot instead of an hour per
object. An object whose removal changes **zero pixels** contributed nothing by
any path — silhouette, shadow, baked AO or reflection — and is free to cull.
`analyze_visibility.py` joins that with the counts arm's per-object rows and
prints **triangles per visible pixel**.

The garage-day answer: **the frame is not overdrawing.** Twelve of the fourteen
solo objects that submit triangles are seen; only Tower block 06 and the
pavement under it are strictly occluded (3 509 triangles, 51 packages, 6 bags).
But **6 532 triangles — 18.6% of the frame — buy 327 pixels between them**, and
that ratio, not "occluded", is what ranks the population.
[Raw evidence](authoring/world-visibility-2026-09-17/README.md).

**`authoring/impostor-threshold-2026-09-17/` builds the lever that finding
pointed at, and prices it in PACKAGES.** A VU1 package costs the EE about
19.5 us in garage day and 31.8 at night, measured on the console, so the
package column leads and the triangle column follows — in the round that
measured the rate, triangles ROSE while the frame got faster.
`set-impostors.py` assigns eight-view impostors to the district's two building
models and sets the switch distance; the two arms differ in that one number.
At **100 units** — placed in the gap between the furthest building the frame is
SEEN to draw (82) and the nearest one it is not (117) — garage day falls from
**750 to 670 packages (−10.7%)** and garage night from 803 to 723, while the
outer poses correctly do not move at all, because the same buildings are 32 and
44 units away there. `verify_scene_identity.py` proves collisions and picking
unchanged in the generated scene data, and `route-sampler.py` parks the camera
at stations along a drive-in approach and an orbit so the swap can be checked
for popping. [Raw evidence](authoring/impostor-threshold-2026-09-17/README.md).

**The camera is parked too, and that is the other half of the same hazard.**
`authoring/reflection-probe-2026-09-16/motion-sampler.py` and
`content-sampler.py` replace the fixture's sampler so the four phases become
four camera MOTION regimes (idle, straight, 20 deg/s, 90 deg/s) or four
INVALIDATION regimes (nothing changes, a reflected object hidden and shown, a
reflected object sliding, the day/night value flipping) with the camera still.
Both keep the 1440-frame window and the `bin/district-benchmark.csv` "safe to
write now" signal, so every other recipe on this page still applies. They were
built for the reflection reuse budget, which is exactly a
skip-when-unchanged change and could not honestly be measured on the parked
fixture alone.

**`--keep-routes` leaves the AI drivers driving**, and there is one class of
change that must not be measured without it. Parking the traffic is what makes
this fixture repeatable, but it also means every car is still, every frame,
forever — so any change that *skips work when an input did not change* scores a
perfect result here whether or not it would ever fire in a real district. Build
both fixtures, quote both numbers, and say which is which; a number from the
parked fixture alone is not evidence for such a change. The district has five
vehicles and **two** of them are routed, so even the moving fixture is a mixed
population rather than a worst case — read the per-car counters, not only the
milliseconds. `docs/wheel-rebake-skip.md` is the worked example. The moving
fixture is not deterministic, so a `--capture-frame` pixel A/B still belongs on
the parked one.

Build/run each fixture with the editor revision being tested. After 1,440 game
updates, `bin/district-benchmark.csv` contains 32 samples across garage/outer-road
day/night views. Each phase warms up for 120 updates; the sampler retains results
in memory and writes only after all measurement phases finish. Use the same
script with `--report` to summarize a completed fixture. These are rolling engine
FPS samples, not individual-frame percentiles. Remove that fixture's old CSV
before a repeat boot and alternate baseline/candidate runs without competing
builds. Visual and normal-traffic driving checks are separate acceptance steps.

## Integrated performance results (2026-09-12)

Frozen parked-camera measurements with the helper above, PCSX2 software rendering,
PAL 512x512, no competing builds. Values are medians of eight rolling FPS samples.

| Build / settings | Garage day | Garage night | Outer day | Outer night |
| --- | ---: | ---: | ---: | ---: |
| Baseline `1ce38d2b` | 25.00 | 20.37 | 50.00 | 50.00 |
| Integrated `af8e6762` | 25.00 | 25.00 | 50.00 | 50.00 |
| Integrated, quiet debug | 25.00 | 25.00 | 50.00 | 50.00 |
| Integrated, release | 25.00 | 25.00 | 50.00 | 50.00 |
| Integrated, model LOD 64 | 25.00 | 25.00 | 50.00 | 50.00 |
| Integrated, model LOD 64 + terrain LOD 96 | 25.00 | 25.00 | 50.00 | 50.00 |
| Baseline repeated after candidates | 25.00 | 20.00 | 50.00 | 50.00 |

The integrated build includes the latest main renderer submission improvements
as well as wheel batching/culling and reflection-basis correctness. These results
do not isolate each change's contribution. Shared reflections already updated
every second frame; this pass does not claim a new cadence saving. Both build
profiles use optimization; disabling live tools or using release did not raise
these median FPS values.

Both LOD trials are **rejected for the shipped map**: no observed FPS improvement
in these views. Their outer-road night captures retain the visible road surface,
but no full driving/crossing acceptance is claimed for these discarded settings.
Authoring keeps model and terrain LOD distances at zero. The additional exact
planar road optimization passes the geometry/UV oracle but leaves this district
at 93,150 road vertices and 90 chunks.

**Both of those paragraphs were re-measured on a physical PS2 on 2026-09-16, and
both of them understated what was there** — see
[the raw evidence](authoring/road-lod-2026-09-16/README.md). Displayed FPS could
not have separated the LOD trials: the frame was pinned to a vsync rung, so no
reduction in work could move it. Against frame `work` and the triangle counter,
mesh LOD 64 is 0.19 ms **slower** (the per-object tier selection costs more than
the geometry it saves) while terrain LOD is worth −0.44 ms of garage day at 96
and −0.38 at 160, which is the largest distance the district's terrain-glued
roads allow. And the road reduction that "leaves this district at 93,150
vertices" was all-or-nothing: merging maximal coplanar lateral runs instead
takes it to **21 252 road triangles in 337 packages** from 31 050 in 470, with
the surface and every seam exactly unchanged, for −0.358 ms of garage day and
−0.591 ms in the outer poses. Authored LOD distances still ship at zero, because
a parked fixture cannot see a terrain tile rebuild.

[Raw samples and fixture settings](authoring/performance-results.json) retain the
measurement evidence. PAL tops out at 50 FPS; the garage still misses that budget.
These parked-camera tests differ from the older active-traffic spawn comparison
above and do not establish a map-wide minimum or physical PS2 performance.

The regenerated 1.86.5 / format-52 example was also built and driven with normal
AI traffic: coupe and Tristar entry, acceleration, braking, steering and day/night menu
switching. This smoke test is separate from the frozen FPS measurements.

## Credits and licenses

- **Kenney** — Retro Urban Kit 2.0, CC0. Included source OBJ/MTL files, textures
  and `res/models/urban/LICENSE.txt`; building kitbashes are adaptations.
- **GGBotNet** — PSX Style Cars, Car 04, CC0. Wheels separated, source scaled to
  about 4.1 m long, orange texture retained. `res/models/ggbot-CC0.txt`.
- **designersoup** — Tristar Racer, Low Poly Car Starter Pack. Source: [author page](https://designersoup.itch.io/low-poly-car-pack-1). The page permits use and modification in games, but pairs its CC0 label with conflicting standalone redistribution restrictions. We do not describe this model as unambiguously CC0; see `res/models/tristar-USAGE.txt`.
- **CC96** — original coupe supplied with this example under CC0. The supplied
  license does not identify an author; `res/models/car1-CC0-licence.txt`.
- **TyraX contributors** — district layout, sign, asphalt and ground textures,
  preparation scripts and synthesized vehicle sounds.

The shipped `THIRD-PARTY-NOTICES.txt` repeats the asset credits. Tristar is a
game-use asset with the published terms recorded below; the other imported
district assets retain their CC0 notices.


## Lean vehicles (2026-09-14)

The authored budgets are CC96 body 1200 / wheel 480 and Tristar body 1200 /
wheel 700. Budgets are decimator inputs, not guaranteed output counts. Rally
already has only 476 triangles including its wheels and stays unchanged.

| Near vehicle | Previous triangles | Lean triangles | Reduction |
| --- | ---: | ---: | ---: |
| CC96 | 4288 | 2372 | 44.7% |
| Tristar Racer | 3940 | 1345 | 65.9% |
| Rally 04 | 476 | 476 | 0% |

CC96's more aggressive wheel trials (160 and 320 budget) reduced its baked
radius from about 0.232 to 0.190 and were rejected. The chosen wheel is about
0.228; the existing bake derives the runtime ride height from that geometry.
Tristar's source wheels resisted the runtime seam-preserving decimator, so
`reduce-tristar-wheels.py` performs a Blender collapse at 12% and restores each
wheel's local bounds. The baked radius is about 0.303. Reproduce with Blender:

```
blender -b --python authoring/reduce-tristar-wheels.py -- res/models/tristar-racer.glb res/models/tristar-lean.glb
```

The body remains in the original GLB and is reduced by the existing vehicle
bake. Lamps, paint/reflections, wheel nodes and vehicle handling parameters
remain supported. `build-district.py` retains the selected asset and budgets.
The near triangle reduction does not imply the same percentage FPS gain.

The native PS2 build succeeds and `--vehicle-check` passes. Close garage and
rear/side views were inspected in PCSX2's software renderer at PAL 512x512.
The retained silhouette, spoiler and rims remain recognizable; this is a
purposeful geometry/quality tradeoff, not pixel-identical output. A separate
normal-traffic coupe drive exercised entry, acceleration, steering and braking;
the body/wheels remained aligned. These visual checks do not validate console
performance. The Release editor compiles and links as `tyrax-editor-check.exe`.

![Lean Tristar and CC96, actual GS capture](preview/lean-vehicles.png)

### Per-frame cost fixture

`instrument-frame-cost.py FIXTURE` runs **after** `--refresh-gen` / the initial
build of an isolated `benchmark-district.py` fixture. Build its generated files
with `tools/toolchain/native-build.ps1` or `native-build.sh` directly; another
editor build regenerates and removes the instrumentation. Never apply it to the
checked-in example. The engine sources do not change.

It records 960 individual frames (240 after each 120-frame phase warmup), stores
them in RAM, and writes `bin/frame-cost.csv` after all four phases. No sampling
writes go over the host filesystem. Do not request captures during this window.
The existing 32 rolling FPS samples are a separate instrument.

Update, submission, finish and presentation are disjoint wall-clock buckets;
vehicle time is included in update. Finish includes pending pipeline work and
postprocessing and is **not a GS-only measurement**. Bounds, preparation,
dispatch, packet construction, DMA and VIF waits overlap their parent buckets
and each other. Triangle counts are submitted static-pipeline triangles, not
unique scene geometry. Texture uploads/reuploads are per-frame counter deltas.
The loop measurement excludes the engine's pad poll and info update outside
`TerrainGame::loop`; telemetry adds CPU overhead. Frame extrapolation, adaptive
resolution and external capture commands must remain disabled for this fixture.
Use repeated hardware runs and an uninstrumented control before claiming gains.

After the CSVs are complete, write `0`, `1`, `2` or `3` into
`bin/district-benchmark-pose.txt` to hold garage day, garage night, outer day or
outer night for separate render-cost captures. This command file is polled only
after sampling; it cannot alter the 1,440-update benchmark route. Do not use
post-benchmark FPS as an ordinary control, because pose polling adds host I/O.

**`submit_ms` is `beginFrame()`..`endFrame()`, not the static pipeline.** It
carries the post-process passes, the 2D HUD and every per-object test the
generated game's Objects loop runs, while `bounds`/`prepare`/`dispatch` cover
only `StaPipCore::render` — so the three do not add up to it and were never
meant to. `instrument-frame-cost.py FIXTURE --attribute` writes a second file,
`bin/frame-attrib.csv`, that brackets every `renderScene` phase, splits the
object loop into its submits and its tests, and splits the post-fx and HUD
blocks out; pair it with `TYRA_STAPIP_ATTRIB` = 1 (default 0, and it must never
ship on) for the engine's own per-bag split of the same frame. The method, the
hook cost and the measured table are in
[docs/render-submission-attribution.md](../../docs/render-submission-attribution.md).

### Initial attribution results (2026-09-14)

Clean baseline then lean runs, after all builds finished, PCSX2 software renderer,
PAL 512x512, 32bpp, native raster, parked traffic, identical telemetry. Earlier
exploratory runs were excluded because one overlapped compilation. Each row uses
240 individual frames after warmup. Work is update + submit + finish, excluding
presentation wait and the engine's outer pad/info processing.

| View | Baseline mean work | Lean mean work | Baseline / lean p95 work |
| --- | ---: | ---: | ---: |
| Garage day | 31.06 ms | 24.64 ms | 35.11 / 28.58 ms |
| Garage night | 35.23 ms | 28.64 ms | 39.07 / 32.22 ms |
| Outer day | 12.26 ms | 12.19 ms | 15.63 / 15.47 ms |
| Outer night | 14.69 ms | 14.46 ms | 17.44 / 17.34 ms |

The garage still presents at about 25 FPS; removing 6.4–6.6 ms does not reach
PAL's 20 ms field budget. Near-garage submission drops from 27.73 to 21.22 ms by
day and 31.87 to 25.22 ms by night. Update remains about 2.8 ms, including about
1.5 ms of vehicle update. There are no texture uploads/reuploads in the warmed
sample windows. These are emulator attribution results, not GS timings, a
hardware speedup, or a map-wide frame-rate guarantee. The normal-traffic drive
is a separate visual/handling smoke test, not this benchmark.

[Summary and configuration](authoring/frame-cost-2026-09-14.json) and
[raw per-frame CSVs](authoring/frame-cost-2026-09-14/) retain the evidence.

### Physical PS2 attribution (2026-09-14)

After restarting into ps2link, fresh advancing telemetry and completed CSVs
confirmed hardware execution on 192.168.100.150. These fixtures use the same
native PAL raster and parked traffic as above. Existing local generated files
enable adaptive plain BLSS despite the saved manifest disabling it; those files
were preserved and are not the configuration measured here.

| View | Old models work / median FPS | Lean models work / median FPS | Lean, live tools off work / median FPS |
| --- | ---: | ---: | ---: |
| Garage day | 57.50 ms / 15.14 | 48.80 ms / 16.33 | 41.65 ms / 19.64 |
| Garage night | 65.34 ms / 12.96 | 56.06 ms / 16.07 | 48.87 ms / 16.67 |
| Outer day | 26.45 ms / 22.54 | 26.22 ms / 22.22 | 20.63 ms / 33.33 |
| Outer night | 31.08 ms / 22.65 | 30.44 ms / 23.15 | 24.20 ms / 25.00 |

The lean column is the initial run; a repeated lean run is retained separately
in the hardware summary. FPS is the median of eight rolling engine samples per
phase, not the reciprocal of mean work. These short runs establish attribution,
not a guaranteed minimum FPS or a precise run-to-run speedup.

Removing live-tool channels lowers update from roughly 8 ms to 2.8 ms; vehicle
update itself is about 2.2 ms. Hardware VIF waits around the garage are roughly
7–8 ms with lean models, much larger than the emulator's approximately 0.1 ms.
They include downstream backpressure and do not isolate VU arithmetic from GS
work. Bounds processing costs roughly 7–8.5 ms there, including repeated passes.
All warmed sample windows have zero texture uploads and reuploads, so texture
thrashing is not the bottleneck in these views.

A separate serialized outer-night render capture measures 20.85 ms total,
including 8.00 ms procedural roads, 3.24 ms terrain and 5.56 ms objects. It is
phase attribution only. Next candidates are less frequent/asynchronous live-tool
polling, cheaper reusable bounds/preparation, and road submission/LOD; test each
independently before deciding on a renderer rewrite. No engine optimization is
implemented by this profiling pass.

[Hardware summary](authoring/frame-cost-2026-09-14/hardware-summary.json) and
[raw evidence](authoring/frame-cost-2026-09-14/) retain the measured frames.

The repeated lean run measured 48.80 / 56.62 / 27.44 / 30.65 ms mean work
in phase order. Its garage FPS medians were 16.04 / 15.38, illustrating the
variation in these short network-debug runs. The quiet arm is still a debug
build with the same instrumentation, not a release-build claim.

Separate garage captures measured 40.00 ms day and 52.92 ms night; the latter
includes a 6.21 ms shared-reflection probe update that the day capture did not
contain. Wheels cost about 4.1 ms in both. Do not subtract these two single
captures to estimate lighting cost: their probe cadence differs.

Physical-console visual validation also exercised entry, acceleration, steering
and braking with normal traffic and without the per-frame instrument. The car
reached the outer roadside from the garage. Subsequent rolling FPS samples at
that roadside are retained separately, including attempted pad input; unchanged
before/after images mean they must not be presented as a moving-drive benchmark
or a matched uninstrumented control.

![Lean vehicles on physical PS2, garage day](preview/lean-vehicles-ps2.png)

The subsequent generic bounds-cache experiment is recorded in the
[engine work plan](../../docs/motor-district-performance-plan.md#next-universal-experiment-indexed-bounds-cache-2026-09-14).
It uses the lean geometry and unchanged devkit cadence; do not merge its
results into the original model-reduction A/B above.

The hardware capture can also be inspected in **Debugger > Hardware timeline**
(1.92+), including frame selection and zoom. Detailed dispatch evidence and the
rejected classification-reuse experiment are under
`authoring/hardware-profiler-2026-09-14/dispatch-detail/`; see
[the updated hardware report](../../docs/hardware-profiler-results.md).

## Static submission batching (2026-09-14)

The generated main Objects pass now uses the engine's bounded resident-draw
submission scope. Reflected probes and serialized cost measurements close the
scope first. Vehicle assets, road geometry, render order and native raster are
unchanged by this step. See [the batching report](../../docs/static-submission-batching.md)
for physical PS2 measurements, rejected larger packets, and the complete
resource/image/lifetime acceptance evidence.

## VU1 and DMA cache cost (2026-09-15)

Seven physical-PS2 boots of this district priced the two remaining candidate
directions. Nothing in the example or the engine changed; both probes were
reverted.

A VU1 cycle per triangle is worth **0.0768 ms of garage-day frame time**, and
almost all of it shows up as VIF1 DMA wait, so arithmetic removed from the
colour microprograms comes straight off the frame. The per-submission
`FlushCache(0)` that ps2sdk performs is bounded at 2.36 ms of a 50 ms frame and
scales with the number of submissions, so batching removes it rather than an SDK
fork. Suppressing the flush outright hangs the console, because the memory that
needs coherency is the packet itself.

The garage poses also recorded 12.17 texture re-uploads per frame against zero
on September 14. **That figure was a stale fixture, not a regression** — see
below.

## GS VRAM in the garage (2026-09-15)

**First, the retraction.** `benchmark-district.py` copies this example's
*committed* generated sources, and those drift behind the editor. Every fixture
since regenerated with the editor under test records **0.000 re-uploads and 0
evictions in all four poses on the console**, and the stale build also showed
3.6 ms more submission than the regenerated one. A performance fixture built
from committed generated sources is measuring a different game; regenerate
before you measure. The script's docstring says so now.

What remains is not a thrash but a **cliff**, and it is worth knowing where it
is.

At `Pal576i` 512×512 and 32-bit colour, the two frame buffers and the z buffer
take 786 432 of the PS2's 1 048 576 words before a single texture loads, and
post-fx, the env-map target the three shiny car bodies need and the projected-
shadow slots take another 65 536. **The texture heap is 196 608 words
(0.75 MB)**, and the garage held 165 440 of them — 84% — in 27 allocations.

The census (`VRAMRES` lines, a debug build) says where they went:

| | words | share of the heap |
|---|---:|---:|
| `veh-tristarplay01-palette-image-0.png` (256×256 RGBA32) | 65 536 | 33% |
| the other two vehicle textures | 17 856 | 9% |
| HUD sprites (`loading`, `icons`, `flare-corona`, …) | 57 344 | 29% |
| **every building, tree, wall, sign and the terrain** | **24 704** | **13%** |

One car's body texture costs more than four times the entire city, because
`vehbake` ships the `.glb`'s embedded PNG verbatim and never meets the quantizer
that takes every `res/models/` texture to the project's declared 4-bit. That is
a real bug and **fixing it here is a trade, not a win** — palettizing the two
body images buys 280 KB of heap this scene does not currently need and costs GS
time it does. It has its own commit and its own measurements; see
[vehicles.md](../../docs/vehicles.md).

Two things the measurement ruled out rather than confirmed. **Nothing is
allocated per frame** — 4 440 frames performed 28 uploads and zero re-uploads —
and **the eviction policy was never the problem**, because parked in the garage
nothing is evicted at all. The scene simply sits 4% of VRAM from its ceiling:
pressing Start binds the pause menu's 40 960 words against 31 168 free, and the
reading drops to **`freeMB=0.0483`, `largestKB=25`, eight evictions** — the same
`freeMB=0.048` the console reported. That is where the cliff is, reproduced by
one button press.

Levers, cheapest first, and note that only the last one is free of a GS-time
cost: palettizing the pause menu (40 960 words at 32-bit against 5 248 at
4-bit), palettizing the vehicle bodies (71 552 words), and **`palFullHeight`,
worth 384 KB of texture heap for 64 scan lines and no sampling cost at all**.
See [gs-vram.md](../../docs/gs-vram.md).

See [the report](../../docs/vu1-and-dma-cache-cost.md) and
[the raw arms, probe sources and reproduction recipe](authoring/vu-cost-dma-cache-2026-09-15/README.md).

### The two EE-submission bounding probes (2026-09-16)

Physical PS2, six hashed ELFs, four parked poses, 240 recorded rows each, and a
0.135 ms repeatability floor from two boots of the control. Both probes **cap**
the direction they were aimed at, which is what they existed to do:

| garage day, frame `work` | |
| --- | ---: |
| what per-package frustum rejection BUYS | 2.60 ms |
| what per-package classification COSTS | 1.79 ms |
| what coarsening the classification costs | +4.59 ms |
| what `FlushCache` costs, refill included | 1.09 ms |
| what the cheapest legal way to stop calling it costs | +7.55 ms |

So the rejection pays for itself and the planned redesign must keep an
equivalent test; classifying more coarsely is a net loss; and the arm that
stopped calling `FlushCache` **corrupted the picture** — a 14-row band at the
horizon — even with the packet allocated in uncached memory and the qbuffer copy
pools still flushed.

Two things about that round's CONTROL, before its absolute numbers are quoted
anywhere: it runs `--profile debug`, so the live tools' per-frame `host:` polling
sits inside the `update` bracket and costs **4.79 ms of `update` / 6.44 ms of
`work`** (a `quiet-debug` control boot then matches the branch-tip table to
0.35 ms of `work`); and it is a **72-run fixture** — `ROADSTRIP ... packages 526`
against the branch tip's 470 — because the editor binary that regenerated it was
built two hours before the commit that raised the ceiling to 75. Every arm shares
both, so the deltas stand. **The garage-day capture hash matched anyway, which is
why a capture hash is a picture check and never a fixture check.**

See [the plan and what the probes changed in it](../../docs/ee-submission-rearchitecture.md)
and [the raw arms, patches, captures and reproduction recipe](authoring/ee-probes-2026-09-16/README.md).

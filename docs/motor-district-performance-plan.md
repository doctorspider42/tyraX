# Motor District performance work plan

Motor District's next optimization pass measures and reduces rendering work
while retaining all three vehicles, live scenery reflections, night lights and
the current driving model. This is an implementation and acceptance plan, not
a claim that its proposed gains have already been achieved.

## Baseline and measurement rules

Start from `1ce38d2b`, which includes bounded road chunks and the static-model
performance branch. The previous short PCSX2 software-renderer samples were
25.0 FPS by day and 22.1 FPS at night at the on-foot spawn camera, PAL 512x512,
debug configuration. A separate serialized day render capture reported
36.562 ms Total. These are different instruments; do not convert the serialized
capture directly into ordinary FPS or add its `_included` rows to parent costs.

The current roads contain 93,150 vertices in 90 chunks. Road drawing accounts
for roughly 7-8 ms in the sampled serialized captures. Environment mesh and
terrain LOD distances are zero. Live Link, Live Debug, Live Logic and Time
Machine are enabled. Wheels and the shared reflection capture currently lack
separate named phase rows.

The first agent audit confirmed that the shared reflection target already
updates every second frame. The next task must improve or validate that policy,
not claim savings against an incorrectly assumed every-frame baseline.

- Freeze the camera, input and test configuration in reproducible fixtures.
  Record traffic state or disable traffic identically in an isolated attribution
  fixture; also test normal traffic in the acceptance drive.
- Separate first-use allocation/loading from steady-state costs. Record cold
  starts separately. Reject stale telemetry by checking advancing frame IDs.
- Measure ordinary frame times/FPS outside one-shot serialized render captures.
  Record medians, slow frames and variability, not just the best reading.
- Alternate baseline and candidate boots when comparing small gains. Run no
  competing builds or benchmark emulators during a measurement window.
- Compare images for exact optimizations. For approximation, publish the error
  budget and inspect motion, silhouettes, seams and transitions, not only a
  parked screenshot.
- PAL targets 50 Hz (20 ms); a 60 Hz target needs an explicit NTSC fixture and
  a 16.67 ms budget. Changing video mode alone is not a speed optimization.
- Keep emulator results separate from real-console results. Low measured VU
  wait alone does not prove the GS is idle or the frame is solely EE-bound.

## Assignment and ownership

Three Terra subagents (`gpt-5.6-terra`) implement five tasks in isolated worktrees.

| Agent | Tasks | Branch |
|---|---|---|
| `terra_wheels` | 2: wheel preparation/rendering | `codex/district-wheel-performance` |
| `terra_roads_lod` | 3: adaptive roads; 4: environment LOD | `codex/district-road-performance` |
| `terra_measure_reflections` | 1: measurement/debug overhead; 5: reflection reuse | `codex/district-reflection-performance` |

All branches start at the same baseline. The coordinator owns integration,
version/format reconciliation, final combined validation and documentation
index/backlog updates. Agents may change separate sections of `templates.cpp`
in their own worktrees; no agent edits another agent's checkout. Heavy builds
and PCSX2 measurement slots are serialized by the coordinator. The measurement
agent has the first slot; others can inspect code and run lightweight host
checks meanwhile.

## Task 1: establish the remaining costs and debug overhead

Owner: `terra_measure_reflections`; priority: first, because other gains need a
trustworthy baseline.

1. Add a named shared-reflection capture phase. Coordinate with the wheel agent
   adding `Wheels`; keep counters mutually exclusive and label any included
   child counters explicitly.
2. Prepare fixed day/night spawn fixtures and a representative driving view.
   Provide a reusable procedure or script that restores temporary settings and
   stores configuration, frame IDs, timings and captures together.
3. Compare the current debug build, debug with optional live features disabled,
   and release. Preserve equivalent scene content and video settings. If release
   lacks live telemetry, use the ordinary FPS HUD or a minimal equivalent
   instrument; do not silently compare incompatible counters.
4. Attribute any difference before changing shipped settings. Identify whether
   periodic file polling/capture, diagnostics or rendering accounts for it.
5. Produce a table of per-phase costs, ordinary performance and measurement
   limitations. Report an inconclusive comparison as inconclusive.

Acceptance: reproducible fresh measurements; separate wheel/probe costs;
debug/release visual and gameplay parity; no permanent loss of development
features solely to improve a benchmark screenshot.

## Task 2: avoid unnecessary wheel work

Owner: `terra_wheels`; primary code: generated wheel storage and
`renderVehicleWheels()` in `src/templates.cpp`.

1. Reject invisible/off-screen vehicles before building their wheel arrays.
   Use a conservative bound that covers steering, suspension travel, roll,
   wheel radius and vehicle scale. Match the actual view being rendered.
2. Preserve existing distance and body-LOD rules: far body meshes already carry
   rest-pose wheels, so do not submit a duplicate set.
3. Cache invariant wheel UVs and modulation colors. Separate definitions and
   texture identities; changes to active instance count/order must not leave
   stale array ranges or mix the three cars' palettes.
4. Evaluate immutable wheel geometry with per-wheel matrices/VU transforms
   against the current CPU-transformed batching. Extra draw calls can outweigh
   saved arithmetic: implement the larger change only with supporting evidence.
5. Add the `Wheels` phase and measure parked, driving, steering, off-screen,
   near/far transition and multiple-definition cases.

Acceptance: correct body/wheel alignment, spin and suspension; no culling pops,
missing/duplicate wheels or wrong textures; exact optimizations preserve image
output; reduced measured wheel/whole-frame cost without memory instability.

## Task 3: adaptive road geometry

Owner: `terra_roads_lod`; primary code: `src/roadgen.cpp` and its generated
`buildRoads()` twin in `src/templates.cpp`.

1. Audit the actual longitudinal/crosswise sampling and flat-span reduction.
   Count retained geometry by road/shape to identify useful opportunities.
2. First try exact reductions on linear/planar sections. Do not assume equal
   shoulder heights imply a flat interior or that planar geometry guarantees
   equivalent interpolated texture coordinates.
3. If approximation is needed, define a world-space surface and UV error budget
   against the original dense reference. Retain samples around crowns, crests,
   saddles, bends, height discontinuities and segment endpoints.
4. Preserve winding, road width, texture arc length, junction continuity and
   the lift above terrain. Keep local chunk bounds/budgets; do not turn each
   entire road into one oversized culling box.
5. Keep editor drawing/picking and generated geometry consistent. Extend the
   existing road twin oracle with linear slopes, curved slopes, crowns,
   saddles, transitions, segment/chunk boundaries and scene revisits. Compare
   sampled surfaces/UVs to the dense reference as well as host/runtime twins.

Acceptance: measured vertex and road-time reduction; no floating/buried roads,
cracks, broken texture mapping or picking regressions; meaningful error checks
and slope/crest driving validation.

## Task 4: enable useful environment LOD

Owner: `terra_roads_lod`; primary files: the example manifest and deterministic
`authoring/build-district.py`.

1. Inventory model triangle/material counts, screen sizes and current per-object
   overrides. Preserve vehicle-specific LOD behavior.
2. Prepare isolated model-only, terrain-only and combined distance variants.
   Treat initial distances as experiments, not approved production values.
3. Keep detailed silhouettes near the camera. Verify memory use and first-use
   tier generation/allocation; more LOD levels are not automatically free.
4. Drive across thresholds in both directions. Inspect horizon movement,
   lighting, road/terrain intersections, light pools, shadows and reflection
   silhouettes. Coarsening terrain must not bury a road that follows the dense
   heightmap.
5. Select settings only after visual/performance validation and encode them in
   the authoring script so regeneration cannot undo the optimization.

Acceptance: reduced submitted geometry and repeatable frame-time improvement
without distracting transitions, changed collisions or geometry intersections.
Reject a setting that only improves the parked-camera benchmark.

## Task 5: reuse reflection captures safely

Owner: `terra_measure_reflections`; primary code: shared environment capture
and the corresponding dynamic-reflection sampling basis.

1. Measure capture and reflective-body costs separately before choosing policy.
   Keep the existing shared probe; do not multiply captures per car.
2. Detect conditions permitting reuse: unchanged capture pose and unchanged
   relevant scene/lighting, or a bounded update cadence with an explicit quality
   tradeoff. Unsupported dynamic conditions retain the every-frame path.
3. Preserve the original capture basis when reusing its texture. Sampling an old
   image using the current camera basis produces sliding reflections even if
   the capture itself is valid.
4. Invalidate on first use, scene reload, teleports, reflection mode changes,
   day/night changes and relevant reflected-object visibility/transform or
   lighting changes. Account for alternate cameras and split-screen semantics.
5. Test idle, straight driving, hard turns, camera orbit, reflected-object
   movement/hide-show, day/night toggles and re-entry after a scene change.
   Verify that all three car materials still reflect actual scenery.

Acceptance: measurable savings in supported cases, no stale day/night image or
reflection swimming, correct first frame and invalidation, bounded memory use.
If temporal reuse does not pass these gates, retain the measurement work and
report the experiment rather than enabling a visually broken shortcut.

## Integration and delivery

- Review each branch independently; do not add the claimed gains of separate
  experiments as if they were guaranteed to combine.
- Reconcile source changes, version/format additions and generated examples.
  Every implemented change carries English documentation and relevant skill
  twins; new serialized fields require normal format handling.
- Build the Release editor and native PS2 game, run vehicle properties and road
  oracles, then compare the integrated result with the same baseline fixtures.
- Exercise all three cars, AI traffic, acceleration/braking/turning, LOD
  transitions and day/night switching. Inspect captures and runtime errors.
- Publish accepted changes, rejected experiments, ordinary performance and
  visual tradeoffs. Keep real-hardware and Linux verification status explicit.
- Update this plan/backlog as tasks complete; do not mark unvalidated work done.

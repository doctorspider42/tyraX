# tyra-editor-dev — host-only subsystems (source-map rules)

Rules embedded in the per-file `## Source map` rows that have no heading of their
own. See the matching `docs/` page for each.

## sequence.hpp — cutscene data model and camera roll
**Cause/rule:** `Sequence` (object tracks + camera *shots* — free or bound to
Camera entities — plus widescreen bars, fades, skippable) and the shared math
`seqEase`/`seqSample`/`seqBarsFractions`/`seqShakeOffset`/`seqCameraForward` (each
mirrored in the generated PS2 player — keep in sync). **Camera roll** (the Dutch
angle) lives here too: `seqCameraUp(fwd, rollDeg)` is a THREE-way twin (host bake,
viewport `camView`, generated player's `upFor`) and `seqRollFromUp` inverts it;
roll 0 reproduces the engine's old hardcoded `(0,1,0)` exactly.
**Fix:** `SeqCameraKey::roll` is for FREE shots only — a bound shot takes its whole
basis from the entity's Euler via `seqCameraUpFromEuler`, because **`rotation.z`
is NOT a lens-axis roll** (`Rz*Ry*Rx` rotates about the world axis last: on a
camera pitched 40 deg, `rotation.z = 90` swings the aim 54 deg).
`seqEulerFromBasis` is the inverse used to bake a recorded roll into an entity's
rotation track. Project-wide (like presets), persisted but not in undo. Compiled
to `src/gen/sequences.gen.cpp` (a runtime player Script + the bars/fade
`renderOverlay`). Object renames remap track/shot name references —
`App::renameObjectRefs` (app.cpp) is the ONE remap for every by-name reference in
the project (here, mirror/scroller/portal/camera-feed lists, area lookups), called
by the Properties name field and by the AI Assistant's `set_object`;
`objRenameFrom_` only remembers what the name WAS. A new by-name reference joins
that function or it goes stale on the first rename.

## ambience.hpp/.cpp — ambience presets and the day/night cycle
**Cause/rule:** `AmbiencePreset` is the sky/lighting/AO/fog "mood" bundle a scene
picks; its fields mirror the matching `ProjectSettings` members exactly, and
`project::resolvedSettings` overlays them so all downstream codegen/viewport reads
ONE set of fields. `DayCycle` + `DayKey` add time: sun/moon arcs plus a cyclic
keyframe list, and **`ambience::evaluate` is the single answer** — `resolvedSettings`
applies it, codegen bakes `SCENE_SUN_*`/`SCENE_MOON_*` from it, the viewport places
its discs from it. Host-only, no GL, no `Project` (`Resolved` exists so
`applyCycle` need not include project.hpp).
**Fix:** two rules the math must keep, both paid for: the resolved light is
**clamped to +5 deg elevation** (a light at the horizon gives flat ground zero
diffuse; night gets dark from the key COLOURS), and the sun/moon handover
**switches** the direction to whichever body dominates while `Resolved::shadowFade`
takes the cast shadows to 0 across the swap — **never blend two near-opposite
directions** (a weighted sum cancels to zero at the crossover, a zenith term
`4*w*(1-w)` points the light straight up at the midpoint, and blending the AZIMUTH
sits on the ±180 seam). A direction cannot be crossfaded; the shadow it throws can.

## starfield.cpp — procedural night sky
**Cause/rule:** `generate(Params)` returns a list of stars (unit direction,
apparent size, RGB, magnitude tier); the editor viewport and the generated game's
`buildStarField` each turn one into a quad, so codegen ships ~400 ROWS rather than
~2400 baked vertices. The reason it is affordable is
`StaPipInfoBag::additiveBlendFix` — an additive 3D bag (`Cs*FIX + Cd`, the light
beams' own trick): per-star brightness/colour ride the Gouraud vertex colours, one
bag per `kTiers` magnitude tier makes the whole sky **three submits**, and both the
dusk fade (`DayKey::stars`) and the twinkle are that bag's FIX.
**Fix:** three things to keep — determinism through `mix32(seed, index, channel)`
(never a running counter), the Milky Way **PULLS** stars toward the band rather
than rejecting samples, and a star **must** be drawn through the soft corona sprite
(an untextured quad is a hard little SQUARE), so `projectStarCycle` forces the
`flare-corona.png` bake and `STAR_COUNT > 0` forces its load.

## facts.{hpp,cpp} + facts_ui.cpp — World Facts
**Cause/rule:** **`layoutOf` is the ONE slot assignment** — every stored fact gets
an index in `factNum[]` or `factPos[]`, and codegen AND the editor's blackboard
both build it from this function, so the index a graph was compiled against and
the index the debugger reads are the same number. **A condition is a TREE, not a
string**, because the same structure serves three consumers — codegen folds it
into one C++ expression, `evaluate` answers it host-side, and `Explain` says WHICH
leaf decided it. **A computed fact has no slot at all** — it IS a query, folded
into whoever reads it (free and unwritable). Host-only (project.hpp includes
THIS).
**Fix:** the enum/tier/scope/comparator tables are each ONE list read by the
serializer, the codegen, the widgets and their tooltips. `cyclicQueries` exists
because codegen must not recurse forever on a catalog the author has not fixed, so
such a query emits `false`. `facts_ui.cpp` keeps the **sectionJson guard** across
the whole body and renames through **`project::renameFactRefs`** (a fact is
referenced by name from graph nodes, query leaves, rule conditions, rule actions
and scenario rows); `factValueWidget` is the single answer to "what does this
fact's value look like".

## dronegen.cpp/.hpp — ambient / drone music generator
**Cause/rule:** host-only, no GL, no `Project` (the treegen/matbake pattern).
Output is an ORDINARY asset: a 16-bit PCM WAV in `res/audio/` plus a `.drone`
patch sidecar, so nothing downstream knows the generator exists — do not grow a
project field for a patch.
**Fix:** three things to respect. (1) **the audition IS the renderer** —
`Synth::render` is the only DSP, `LiveSynth` wraps it for the audio thread and the
offline `render()` runs the same loop then adds the mastering. (2) **`Params`
determinism** — every random stream derives from `seed` through `mix32`, so a
re-render is bit-identical. (3) **`visitParams` is the ONE field list** the
`.drone` writer and reader both walk. Seamless looping ADDS the rendered tail over
the head, but the fold MUST be windowed to zero at its end (unity then raised
cosine) — adding a tail and stopping leaves a step one loopTail into the file
(measured 0.37 vs a 0.11 p99 neighbour jump); the reverb is an 8-line FDN. The
timeline: `paramTable()` is built by walking `visitParams`, so every saveable
scalar is automatable by byte offset; `Params` carries fixed-capacity `AutoLane`s
(no heap — copied into the audio thread); `applyAutomation` runs once per control
block INSIDE the synth's own copy.

## treegen.cpp/.hpp — procedural low-poly tree generator
**Cause/rule:** host-only, no GL, no `Project` dependency — pure functions over a
`Params` struct. `Params::height` is the tree's SIZE: `thickness` and `leafSize`
are fractions of it, so the slider scales the tree instead of thinning it.
`Params::crown` picks the growth rule: 0 spread (the branching recursion) or 1
conical, where the trunk keeps its leader and `conicalWhorls()` hangs rings of
boughs off it with a length PROFILE along the trunk.
**Fix:** **fully deterministic in `Params`** — each branch derives its RNG stream
from (parent seed, child index), so editing one slider adjusts the tree instead of
reshuffling it. The tree reaches the scene through the ordinary `addModelObject()`,
so NOTHING downstream (serialization, codegen, runtime) knows trees exist — do not
grow a scene-object type for this. `bakeBarkTexture`/`bakeLeafTexture` bake
tileable 128² procedural textures; the leaf card uses **hard 0/255 alpha** with
opaque colors dilated into the margin.

## Smaller host-only helpers
**Cause/rule:**
- **`stochtile.cpp`** — stochastic tiling / texture bombing. `generate()` bakes a
  source tile into one larger, still-tileable "supertile" (≤512²). Single source of
  truth: texbake writes it into `.res-baked/stoch`, the viewport uploads the same
  pixels, codegen points the terrain texture table + tiling at it via
  `bakedBinPath`/`factorFor`. Deterministic (seeded from the source path).
- **`skytex.cpp`** — the ONE crop of an equirectangular panorama the sky draws.
- **`screenfx.cpp/.hpp`** — custom screen effects from
  `<project>/screen-effects/*.screenfx` into `customScreenEffects()`
  (`screenfx::loadForProject`, called by `project::load` before placements are
  read). A `Project` references one only by placement (`ScreenFxPlacement`: key +
  layer + enabled + params), placed/reordered in the *UI Editor* screen stack,
  codegen'd to `src/gen/screen_fx.gen.cpp`, run via `RendererCore::applyCustomPostFx`.
  Unknown-key placements are dropped on load. Header keys/`{pN}` placeholders are
  mirrored in the VS Code extension's `SPEC`.
- **`prefab.cpp/.hpp`** — reusable groups of scene objects with their flow graphs;
  `prefab::instantiate` is the one function Insert into scene and the runtime
  spawner use, so a preview cannot invent a placement.
- **`primmesh.cpp/.hpp`** — shared, GL-agnostic unit-primitive tessellation
  (box/sphere/cylinder/cone/plane → raw triangles).
- **`flownode.cpp`** — loads custom flow nodes from
  `<project>/flow-nodes/*.flownode` into `customFlowNodes()`
  (`flownode::loadForProject`, called by `project::load` *before* graphs are
  parsed). Header keys/`{placeholder}`s are mirrored in the VS Code extension's
  `SPEC` table and grammar — keep them in sync.

## Reference facts (upstream `scene-objects-and-gameplay.md` + `source-map.md` host subsystems)
- Areas: `project::areaContainsPoint`/`areaCaughtObjects`/`areaLiveCandidates`/`objectRuntimeMovable`/`runtimeRefNames`; `pointInArea` emitted into `scene_data.hpp`; live catch `catchAreaLive`, `CATCH_CANDIDATES`, `liveArea`/`firstCand`/`candCount` on `MirrorData`/`PortalData`/`CamFeedData`, `TerrainGame::collectLiveCaught`; reverb `REVERB_ZONES`/`TerrainGame::updateReverb`/`SceneObject::soundReverb`, `ScriptContext::reverbBusBase`, `reverbPresets()` (order IS wire format, `Tyra::AudioReverb::Preset`), `pickSoundSlots`/`flowPickSfxChannel`.
- Scroller: `scrollsim.cpp/.hpp` (~370+215); `wrapU`/`SCROLLERS`/`SCROLLER_CLONES`/`SCROLLER_HIDDEN`; `ScrollerDirector` (`scroller.gen.cpp`) with `sc_wrapU`/`sc_varyHash`/`cellAdjust`/`Placement::cell`/`memberVary`/`memberTemplateFlags`; clones use `wantsMatrixPath`.
- Physics: `updateObjectPhysics`, `TerrainGame::physBodies`, `PhysBody::lastPos/lastRot/lastSpin`, `spin[3]`, `physPushAt`, `GameModel::collider`, `resolveSphere`, `sweepSphere`; `physhull.cpp/.hpp` (`build(points)`, `kMaxVerts` 24, `physHullBlock`, `PHYS_HULLS`/`PHYS_HULL_VERTS`/`PHYS_HULL_PLANES`/`MODEL_PHYS_HULL`/`PHYS_PRIM_HULL`, `inc/model_data.gen.hpp`, `PhysBody::lv`/`lp`, `PHYS_MAX_HULL_*`).
- Placement `placement.cpp/.hpp`: `worldAabb`, `isSupport`, `restOffsetY`/`restOffsetYGroup`, `ceilingY` (`FLT_MAX` insert/paste vs own underside for `End` drop-to-floor); callers `App::snapInsertedObject`/`movePasteStaged`/`dropSelectionToFloor`; callbacks `Viewport::modelLocalBounds`/`Viewport::terrainHeight`; `App::placementHeight()`.
- Optional terrain `TerrainConfig::enabled`: `terrainHeightAtScene` returns `TERRAIN_VOID_Y` (-1e6 finite); explicit `TERRAIN_ENABLED` only where a site BUILDS (`resetTerrainChunks`, `setupLightPools`, shadow patch, rain length, player spawn Y); host bakes read `sc.terrain.enabled` (navmesh, decalproj, gibake, aobake, texbake; `TERRAIN_TEXTURES` -1); `Viewport::terrain_.enabled`.
- Cutscenes: `sequence.hpp` (~430): `Sequence`, `seqEase`/`seqSample`/`seqBarsFractions`/`seqShakeOffset`/`seqCameraForward`; `seqCameraUp` (three-way twin), `seqRollFromUp`, `SeqCameraKey::roll`, `seqCameraUpFromEuler`, `seqEulerFromBasis`; `src/gen/sequences.gen.cpp`. `cutscene_ui.cpp` (~2.3k): `ScriptContext::hudSuppressed` (a SECOND flag beside `hudVisible`); a skippable cutscene owns the `menu` action + `GameMenu::skipMenu` role. `camtake.cpp/.hpp` (~580+140): `CamTake`, `bakeCamTake`, `mapCamSample`, `CamTakeMapping`, `hasAnchor`/`anchor`, `keyRate`/`tolerance`, `SeqCameraKey`. `phonecam.cpp/.hpp` (~690+225): `phonecam::Link`, `drainEvents()`/`drainPoses()`/`App::phoneCamTick`, `wire::makeWebSocketTransport()`, `kMaxPendingPoses` (2048), `testClientPage()`, `phonecam::kProtoVersion`, `PROTOCOL.md`.

## Collision identifiers (upstream)
- `collidePlayer`, `sweepSphere` (camera boom) and `objectOutsideSplitBand` all go through `objectCollides`/`objectCollisionBox`; per-model smaller boxes flow through `Project::modelCollision` -> `placement::collisionBox`'s `boxes` argument and `MODEL_COLL_BOX` in generated `objectCollisionBox`. `modelYaw` turns the box by an animated model's yaw. Both overlays exist: editor and in-game `renderCollisionBoxes` (`ProjectSettings::showCollision`).

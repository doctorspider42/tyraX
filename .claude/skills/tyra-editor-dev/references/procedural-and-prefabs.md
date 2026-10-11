# Procedural volumes, prefabs, runtime-generated geometry

Read when you touch `procgraph`/`procgen`/`procbake`/`procrt`/`procui`, add a procedural node, change prefabs (`prefab.cpp`, `prefab_ui.cpp`), or the game's runtime-generated geometry (`TerrainGame::ProcChunk`).

## Data model (`procgraph.hpp/.cpp` ~330+990, docs/procedural-generation.md)

- `ProcNode` (keyed float/string params + a generic `rows` table for asset pools and curve points), `ProcLink` (typed pins), `ProcGraph` (nodes/links/seed/overrides/bakedHash/frozen/runtime), `ProcOverride` (manual per-instance edit bound to a point's stable key), `procNodeTypes()` (26 entries; `.desc` is the node's documentation: add-menu tooltip + hover), `validate`/`linkError` (type mismatch, cycles, missing inputs). Data only.
- `procObjectProps()`: properties the **Object Settings** node can put on every generated object (mesh LOD distance, baked lighting, reflections). Rows store their property by KEY -> append-only; twin = `applySettings` in procbake.cpp (offer in one, not the other, and the switch does nothing). NOT in it: the four fields Output owns (draw distance, cast shadow, collision, layer).
- The graph lives on a `Scatter` scene object (`SceneObject::procGraph`); **the UI calls it a "Procedural volume"** while the enum/serialized key stay `scatter` (file format). Per object -> undoable and collaboration-ready for free.
- Graph wire format = `project::procGraphJson`/`parseProcGraph` (the AI Assistant uses it too).

## Evaluator (`procgen.cpp/.hpp` ~2.4k+190)

Host-only, one deterministic function of (project, scene, volume, graph). Three properties every change preserves:
- **DETERMINISM**: `rand01(seed, nodeId, pointKey, channel)`, never a running counter (an unconnected node elsewhere cannot reshuffle results).
- **PREFIX STABILITY**: generators emit a fixed Halton sequence and density picks a PREFIX (honest progressive preview; overrides stay attached).
- **CACHING**: `Cache` = per-node memo keyed on params + input hashes + `Options::contextSerial`.
- `bakeHash` is the staleness key and **quantizes floats to the six significant digits the `.tyra` stores** (`%.6g`; raw bits made every bake stale after save/load). A node that reads the scene (objects, terrain, roads) must have its inputs covered by `bakeHash` (e.g. road points, width, sample spacing).
- Also `Mask`/`Curve`/`Instance`, `assetMesh` (cached .obj soup).
- **Repeat nodes** (Array / Radial Array) multiply their input; each copy's identity is `copyKey(node, sourceKey, i)`; they do NOT thin by `Options::fraction` (an exact count must not lie in preview); they stop at `kMaxRepeatOut` (200000) with a warning.

## Bake (`procbake.cpp/.hpp` ~520+70)

Instances -> ordinary static geometry: one asset's instances inside one world chunk merge into `res/models/<dir>/procgen-<vol8>-<asset>-x<i>z<j>.obj` (in the SOURCE asset's folder so `mtllib` resolves), and one Model scene object per chunk is reconciled (matched by NAME so ids / live-link identity survive re-bakes; a fresh chunk gets `project::newObjectId()` - an empty id is written to `objects/.json` and lost). `applySettings` applies Object Settings rows AFTER the fixed fields. `estimate` = live budget readout; `anyStale`/`bakeAll` = the build hook (`App::projectForBuild` + `bakeProcedural` in main.cpp - it MUTATES the model, so it cannot live in const `refreshGenerated`); `clearVolume` = cleanup on delete. Optional source decimation via meshlod (Output's Instance detail).

### Frozen bakes (1.156.0)

`ProcGraph::frozen` (optional JSON, format v84, default off; in equality/history/session sync). `procbake::setFrozen` bakes a stale layout before freezing; `bakeAll` skips frozen volumes even when forced; `anyStale` ignores them; `bakeVolume` refuses them. NOT in `bakeHash` (toggling changes no geometry). `updateProcPreview` skips evaluation and passes frozen ids to `Viewport::ScatterPreview::frozenSources`; the viewport draws their saved chunk objects through the ordinary model path (Show preview still controls visibility). Graph edits stage until unfreeze; explicit bake/clear, instance editing and runtime mode changes are disabled while frozen; `clearVolume` still supports deletion. Never implement freeze only in the UI (headless builds and reopening must keep the same geometry).

### Placement validation (1.154.0) and road modes (1.155.0)

`FilterPlacement` lives in the procgraph registry and host evaluator; its generic nums/strs serialize through the existing graph maps (format 83). Use actual mesh bounds transformed at all eight corners, and index every XZ cell a bound covers. Road rejection uses roadgen's full-width spline triangles (not the road object's unit box). Material containment bounds visible coverage across every intersected splat cell (centre/corners miss paint islands). Merge species before the final collision filter; exclude all baked chunks (avoids bake-order dependence). Keep it OUT of procrt's supported list. Road modes: `roads` key 0 = Ignore, 1 = Avoid, 2 = Only (old boolean graphs keep meaning); Only constrains the origin in XZ (not the footprint; no height/heading snap); `roadtarget` is an optional object name - never fall back to all roads when it is missing.

## Runtime volumes (`procrt.cpp/.hpp` ~1.3k+75, docs/procedural-runtime.md)

`ProcGraph::runtime` compiles the graph into the game (`src/gen/procedural.gen.cpp`); the EE evaluates it each boot (no geometry ships). Host-only, no templates.cpp dependency (livelogic.cpp arrangement).
- **`kRuntimeNodes` is ONE table read by `capability()` AND the emitter**: a graph with an unsupported node is named with its reason under the budget bar and codegen refuses it.
- The emitted evaluator is a numeric TWIN of procgen.cpp (same mix64/Halton/channels) - a change to either side is a change to both; the editor preview is the only way to author a runtime volume.
- Points live in ONE growing buffer; every node returns the `[begin, end)` range it produced, always ending at `count` (in-place filter compaction and Merge concatenation without allocation). A node feeding two consumers is EMITTED TWICE (dataflow meaning; hence `emitSeq` in variable names).
- `volumeIndexOf` is the only correct way to name a volume from codegen (skipped volumes are not in the emitted table).

## Adding a procedural node

`procNodeTypes()` entry (pins, params with UI ranges, `.rows` kind, mandatory `.desc`, **a `.tip` on every parameter**) -> a branch in `evalNode` (procgen.cpp) -> decide RUNTIME support: add to `kRuntimeNodes` AND write its `emit*` in procrt.cpp (listed without an emitter = a broken promise), or leave it out (reported honestly). Nothing else: the window renders params from the registry; the bake sees only Output; the AI Assistant's `procNodeCatalog` derives from the registry. Derive randomness from the point key; make a generator's count a PREFIX; a runtime emitter reproduces the numbers EXACTLY.

## The window (`procui.cpp` ~1.9k)

Tools > Procedural: the scatter-graph canvas (its OWN imnodes context `procEditorCtx_`, never `EditorContextSet(nullptr)`), live budget readout, per-instance overrides, `addScatterVolume`, `updateProcPreview`, bake verbs (`bakeProcVolume`/`bakeStaleProcVolumes`/`projectForBuild`), and the **seed simulator** (`runProcSeedSweep` + `procSeedPreview_`): N evaluations at N seeds show the SPREAD a "new world every run" volume hands players; the simulated seed rides `procgen::Options::seedOverride`, is a way of LOOKING, never an edit, and must stay out of `bakeHash`; the sweep uses a SCRATCH `procgen::Cache` per trial (cannot evict the preview's memo).
- A node's context menu remembers its target in `procCtxNode_`, NOT the hover tracker `procDescNode_` (reset to -1 every frame the cursor is off a node - an open popup is - so the menu closed the frame after opening).
- **A point carries an asset OR a prefab**; the prefab half has no mesh: `updateProcPreview` expands each through `prefab::instantiate` (same function as Insert into scene and the runtime spawner) into `ScatterPreview::prefabObjects`. A consumer looking only at `Instance::asset` shows/counts NOTHING for a prefab graph (the cube example previewed as empty ground at 0 triangles).
- The Pick Prefab combo lists prefabs then scene objects: every entry needs `##<prefix><index>` (capture makes duplicate names).
- `App::addScatterVolume(false)` is what `add_object` type `scatter` routes through (a plain `addObject` makes a 1x1x1 box with no graph that generates nothing).

## Prefabs (`prefab.cpp/.hpp` ~570+140, `prefab_ui.cpp` ~550, docs/prefabs.md)

- The `Prefab` struct lives in project.hpp (members are `SceneObject`s). prefab.cpp = the verbs + **`memberMerges`**: plain static geometry folds into the instance's shared bag (one submit); anything addressable takes a clone-pool slot. Computed HERE and baked into `PREFAB_MERGE`, so the cost readout and console agree.
- `capture` puts the origin at the selection footprint's centre at its LOWEST point (placement is a ground click) and clears group membership and `prefabSource`; `instantiate` = yaw + translation, ids left empty for `ensureObjectIds`, stamps `SceneObject::prefabSource` (prefab NAME; editor bookkeeping so the outliner folds an instance into one node); `rename` retargets it. `referencedBy` is the single source for the codegen asset scan and the "used by" list. Host-only.

## Runtime-generated geometry (`TerrainGame::ProcChunk`)

The third geometry path beside solo objects and static batches: world-space vertex bags the GAME built (runtime volume or prefab instance).
1. The merge IS the feature (a PS2 submit costs ~1 ms flat; neither "500 scattered cubes" nor "27 prefab rooms" can be objects). A prefab spawned BY A VOLUME merges into the volume's chunk grid (owner >= 0); a flow-node spawn keeps its own bags (Despawn Prefab removes one instance). Reserved owner `-3` = road chunks.
2. No objects behind merged geometry: collision from `procColliders` (one conservative world AABB per merged member with collision, tested in `collidePlayer` with a cheap distance reject - without it the cube example ran 47 instead of 50 FPS).
3. No lightmap region, no static-batch membership, no scene-table entry: lit from the probe grid. A new per-object visual feature is staged explicitly in `procAddMergedObject` (it states the `g_*` globals rather than inheriting the last rebuild's).
4. **Blocks** (Blocks Fill publishes a solid-cell field): `procBlockVertexAo` answers self-occlusion from 26 bit tests per block at generation and hands it to `pushVert` as `selfAo` (hence `g_aoOff = d.type == 5 && !blockAo`). **A per-vertex value is invisible under flat shading**: chunks were `TyraShadingFlat` (one corner paints the triangle) - two plateaus 42 levels apart with a diagonal seam (6964 hard steps vs 2783 AO-off control, 2868 after). Fix = `ProcChunk::smooth` + `procSmoothInfoBag`, a SECOND info bag (not a flag on `batchInfoBag`, which the static batcher shares and is flat by design). Anything else varying colour across a generated face owes the same pair.

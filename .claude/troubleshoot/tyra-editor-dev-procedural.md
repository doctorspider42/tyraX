# tyra-editor-dev — procedural placement, frozen bakes, seed sweep

From `## Procedural placement validation (1.154.0)` (+ Road modes 1.155.0),
`## Frozen procedural bakes (1.156.0)` and the `procui.cpp` source-map notes. See
`docs/procedural-generation.md`, `docs/procedural-runtime.md`.

## Procedural placement validation (1.154.0)
**Cause/rule:** `FilterPlacement` lives in the procgraph registry and host procgen
evaluator; its generic nums/strs serialize through the existing graph maps (format
83).
**Fix:** use actual mesh bounds transformed at all eight corners, and index every
XZ cell a bound covers. Road rejection uses roadgen's full-width spline triangles,
not the road object's unit box. Material containment bounds visible coverage
across every intersected splat cell; checking only the centre or corners misses
paint islands. Merge species before the final collision filter; exclude all baked
chunks to avoid bake-order dependence. Keep the node out of procrt's supported
list. `bakeHash` must include road points, width and sample spacing.

## Road modes (1.155.0)
**Cause/rule:** keep the roads key values 0=Ignore, 1=Avoid, 2=Only; old boolean
graphs retain their meaning. Only constrains the origin in XZ, not the model
footprint, and does not snap height/heading.
**Fix:** `roadtarget` is an optional object name; never fall back to all roads when
it is missing.

## Frozen procedural bakes (1.156.0)
**Cause/rule:** `ProcGraph::frozen` is optional JSON (format v84), defaults off and
participates in model equality/history/session sync. `procbake::setFrozen` bakes a
stale layout before freezing; `bakeAll` skips frozen volumes even when forced,
`anyStale` ignores them and `bakeVolume` refuses them. Unfreeze restores normal
hash-based updates.
**Fix:** the flag is deliberately absent from `bakeHash`: toggling it alone changes
no geometry. `updateProcPreview` skips evaluation and passes frozen volume ids to
`Viewport::ScatterPreview::frozenSources`; the viewport draws their saved chunk
objects through the ordinary model path. Show preview still controls visibility.
Graph edits are staged until unfreezing; explicit bake/clear, instance editing and
runtime mode changes are disabled while frozen. `clearVolume` still supports volume
deletion. Never implement freeze only in the UI: headless builds and reopening must
preserve the same saved geometry.

## The seed simulator
**Cause/rule:** the seed simulator for runtime volumes is `runProcSeedSweep` +
`procSeedPreview_` (docs/procedural-runtime.md) — N evaluations at N seeds, so the
author sees the SPREAD a "new world every run" volume will hand players. The
simulated seed rides `procgen::Options::seedOverride` and is a way of LOOKING at
the graph, never an edit.
**Fix:** it must stay out of `bakeHash`. Two traps `procui.cpp` has paid for: a
node's context menu must remember its target in `procCtxNode_` and NOT in the
hover tracker `procDescNode_` (that one is reset to -1 on every frame the cursor
is not over a node, which an open popup is — the menu closed the frame after it
opened); and the sweep uses a SCRATCH `procgen::Cache` per trial so it cannot
evict the live preview's memo. **A point carries an asset OR a prefab**, and the
prefab half has no mesh of its own — `updateProcPreview` expands each such instance
through `prefab::instantiate` (the same function Insert into scene and the runtime
spawner use) into `ScatterPreview::prefabObjects`; a consumer that only looks at
`Instance::asset` silently shows/counts NOTHING for a prefab-scattering graph
(which is how the cube example previewed as empty ground at 0 triangles).

## Reference facts (upstream `procedural-and-prefabs.md`)
- Data model `procgraph.hpp/.cpp` (~330+990): `ProcNode` (keyed float/string params + a generic `rows` table), `ProcLink` (typed pins), `ProcGraph` (nodes/links/seed/overrides/bakedHash/frozen/runtime), `ProcOverride` (bound to a point's stable key), `procNodeTypes()` (26 entries, `.desc` mandatory), `validate`/`linkError`. `procObjectProps()` = properties the Object Settings node can put on every generated object (twin `applySettings` in procbake.cpp). The graph lives on a `Scatter` scene object (`SceneObject::procGraph`); the UI calls it a "Procedural volume" while the serialized key stays `scatter`. Wire = `project::procGraphJson`/`parseProcGraph`.
- Evaluator `procgen.cpp/.hpp` (~2.4k+190): deterministic function of (project, scene, volume, graph). `rand01(seed, nodeId, pointKey, channel)`; fixed Halton sequence with density as a PREFIX; `Cache` keyed on params + input hashes + `Options::contextSerial`. `bakeHash` quantizes floats to `%.6g`. Also `Mask`/`Curve`/`Instance`, `assetMesh`. Repeat nodes (Array / Radial Array) use `copyKey(node, sourceKey, i)`, do NOT thin by `Options::fraction`, and stop at `kMaxRepeatOut` (200000).
- Bake `procbake.cpp/.hpp` (~520+70): merges an asset's instances per world chunk into `res/models/<dir>/procgen-<vol8>-<asset>-x<i>z<j>.obj`; reconciles one Model object per chunk matched by NAME. `applySettings`; `estimate`; `anyStale`/`bakeAll` (build hook `App::projectForBuild` + `bakeProcedural`, mutates the model so it cannot live in const `refreshGenerated`); `clearVolume`.
- Frozen bakes: `ProcGraph::frozen` (format v84, default off; in equality/history/session sync); `procbake::setFrozen`; `bakeAll` skips frozen volumes; `bakeVolume` refuses them; NOT in `bakeHash`; `Viewport::ScatterPreview::frozenSources`.
- Placement validation `FilterPlacement` (format 83); road rejection uses roadgen's full-width spline triangles; road modes `roads` key 0 = Ignore, 1 = Avoid, 2 = Only; `roadtarget` optional name. Keep it OUT of procrt's supported list.
- Runtime `procrt.cpp/.hpp` (~1.3k+75): `ProcGraph::runtime` -> `src/gen/procedural.gen.cpp`; `kRuntimeNodes` is ONE table read by `capability()` AND the emitter; numeric twin of procgen.cpp; points live in ONE buffer returning `[begin, end)`; a node feeding two consumers is emitted twice (`emitSeq`); `volumeIndexOf` is the only correct way to name a volume from codegen.
- Window `procui.cpp` (~1.9k): own imnodes context `procEditorCtx_`; seed simulator `runProcSeedSweep` + `procSeedPreview_` via `procgen::Options::seedOverride` (a way of LOOKING, out of `bakeHash`); context target `procCtxNode_` vs hover `procDescNode_`; Pick Prefab combo needs `##<prefix><index>`. `addScatterVolume(false)`.
- Prefabs `prefab.cpp/.hpp` (~570+140), `prefab_ui.cpp` (~550): `Prefab` in project.hpp (members are `SceneObject`s); `memberMerges` baked into `PREFAB_MERGE`; `capture`; `instantiate` stamps `SceneObject::prefabSource`; `referencedBy` is the single source for the codegen asset scan.
- Runtime geometry `TerrainGame::ProcChunk`: third geometry path beside solo objects and static batches; owner `>= 0` merges into the volume's chunk grid, reserved owner `-3` = road chunks; collision from `procColliders`; lights from the probe grid; `procAddMergedObject`; Blocks Fill self-occlusion via `procBlockVertexAo` (26 bit tests) -> `selfAo` (`g_aoOff = d.type == 5 && !blockAo`); `ProcChunk::smooth` + `procSmoothInfoBag` (a SECOND info bag, not `batchInfoBag`).

## Procedural window identifiers (upstream)
- UI bake verbs are `bakeProcVolume`/`bakeStaleProcVolumes` and `projectForBuild` (plus `updateProcPreview`).

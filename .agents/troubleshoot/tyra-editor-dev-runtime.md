# tyra-editor-dev — runtime, batching, physics

From `### 3` (runtime-generated geometry, static batching, matrix path, physics,
invisible walls), `docs/model-pipeline.md`, `docs/static-batching.md`,
`docs/physics.md`, `docs/collision-boxes.md`.

## A prefab-scattering procedural graph previews/counts as empty
**Cause:** a point carries an asset OR a prefab, and the prefab half has no mesh
of its own; a consumer that only looks at `Instance::asset` silently shows
NOTHING (the cube example previewed as empty ground at 0 triangles).
**Fix:** `updateProcPreview` expands each such instance through
`prefab::instantiate` (the same function Insert into scene and the runtime
spawner use) into `ScatterPreview::prefabObjects`.

## Generated (runtime) geometry mis-shades, collides wrong, or doubles a feature
**Cause/rule:** `TerrainGame::ProcChunk` is the third geometry path, next to solo
objects and static batches: world-space vertex bags the GAME built.
**Fix:** (1) The merge is the feature, not an optimization — a PS2 submit costs
~1 ms flat, so neither "500 scattered cubes" nor "27 prefab rooms" can exist as
objects; a prefab spawned BY A VOLUME merges into that volume's chunk grid
(owner >= 0) while a flow-node spawn keeps its own bags (Despawn Prefab must
remove one instance). (2) Merged geometry has no objects behind it, so collision
comes from `procColliders` — one conservative world AABB per merged member,
tested in `collidePlayer` with a cheap distance reject (without it the cube
example measured 47 FPS instead of 50). (3) Generated geometry gets NO lightmap
region, no static-batch membership and no scene-table entry; it is lit from the
probe grid. A new per-object visual feature must be staged explicitly in
`procAddMergedObject`, which states the `g_*` globals rather than inheriting
whatever the last rebuild left set. (4) **Blocks are the one exception**: a Blocks
Fill volume publishes a solid-cell field, so `procBlockVertexAo` answers
self-occlusion from 26 bit tests per block at generation time and hands the
result to `pushVert` as its `selfAo` byte — which is why `g_aoOff` is
`d.type == 5 && !blockAo` rather than the flat `type == 5`. **A per-vertex value
is invisible under flat shading**: generated chunks are `TyraShadingFlat` (one
corner paints the whole triangle), so the gradient came out as two flat plateaus
42 levels apart with a hard diagonal seam (measured: 6964 hard adjacent-pixel
steps against 2783 AO-off, 2868 once the shading followed). `ProcChunk::smooth` +
`procSmoothInfoBag` are the fix — a SECOND info bag, not a flag on
`batchInfoBag` (shared with the static batcher, flat-shaded by design).

## A batched member stops rendering after a runtime mutation
**Cause:** the generated game merges non-moving primitives and compact
imported-model parts into texture/cell bags (`staticBatchEligible`/
`batchBlockedNames` in templates.cpp, `buildStaticBatchList`/`rebuildStaticBatch`
in the game template). Any runtime path that mutates a rendered object property
must set `RuntimeObject::dirty` — that flag demotes a batched member back to its
own bag.
**Fix:** set `dirty` (visibility flips are the one exception, caught by a
snapshot). A new *reference kind* that can move/hide/re-submit objects at runtime
(like sequences and mirror target lists) must join `batchBlockedNames()` (flow
nodes are covered generically via `strKind == ObjectName`). A new
exclusion-worthy per-object property must join `staticBatchEligible()`.

## A new exclusion rule is named wrong by Tools > Static Batches
**Cause:** `staticbatch::Reason` (src/staticbatch.hpp) is the taxonomy the panel
and `--batch-report` print per object; add a rule to `staticBatchEligible()`
without its `Reason` and the panel names whichever older rule catches the object
next.
**Fix:** add both. The per-object opt-out `SceneObject::batchExclude` is
deliberately the FIRST test in both. Run
`examples/vehicle-playground/authoring/verify-batch-twins.py` after touching
either side (it lifts `buildStaticBatchList` verbatim and diffs the grouping);
the eligibility half is checked free by the `batchStatic` column of any
regenerated `inc/scene_data.hpp`.

## An exclusion quietly rejects most of the scene
**Cause:** `drawDistance != 0` alone held all 70 imported models solo on the
Motor District — the entire population compact model batching exists for.
**Fix:** before adding an exclusion, count what the existing ones already reject
(read `batchStatic` out of the generated `inc/scene_data.hpp`) in the same
commit. Often the cheaper fix is to move a per-object *value* into the group
**key**: `drawDistance` now does exactly that (same key as texture and cell,
tested once per batch against the box over its members in `renderStaticBatches`).
A batch-level test must never be routed through the `shown` snapshot — anything
that flips as the camera moves would re-bake the batch every frame.

## The grouping cell grows with the map and turns a win into a loss
**Cause:** the cell was `max(mapW / 4, 48)` — a fraction of the map. A 2048-unit
map got a 512-unit cell, merged 1,100 objects into FOUR batches, and turned a
0.46 ms win on the Motor District into a 3.20 ms loss on
`examples/large-terrain` (a batch's one draw-distance test, applied to the nearest
point of a 512-unit box, held props drawn that individually vanish at 60 units).
**Fix:** `cellFor` bounds the cell by the draw distance the group shares (already
a key, so every member agrees). Two lessons: **a per-group key is also a per-group
SCALE**, and **a ratio is not automatically scene-independent** (the district's
win comes from merging props sparse in their cell, 6.7% fill). See
docs/model-pipeline.md, "Why the cell is bounded by the draw distance".

## A near-miss when reading the batch census
**Cause:** the authored `SceneObject::dynamicLighting` flag (sets `bag->lighting`,
selects `cull_td`) is NOT the runtime per-bag light pick in `StaPipCore::render`
(`wantsLightPick = !bag->lighting && info->dynLightPick`, which feeds the colour
programs' single spot slot).
**Fix:** most triangles can have a light picked while not one object sets the
flag — do not confuse them.

## Batching throws away the baked triangle strip
**Cause:** `rebuildStaticBatch` re-emits every member into the combined array;
reading `GameModelPart::verts` (the list twin) instead of
`stripVerts`/`stripRun` silently undoes the strip bake. Measured on the Motor
District that alone made batching a net LOSS (+3.3% vertices, +1.7% bags, `strip`
packages down 3 900).
**Fix:** runs are self-contained and exactly `stripRun` vertices; members sharing
a run length concatenate and `pinPackageSize(bags, stripRun)` makes each package
exactly one run of one member — so `stripRun` is part of the group key, and the
strip/list choice is **all-or-nothing per batch**. When A/B-ing, read `verts`,
never `trianglesCull` (a strip counts `size - 2` primitives incl. degenerates; a
list counts `size / 3`).

## Merging two props shades both from one lamp
**Cause:** **a bag gets one dynamic light**; `StaPipCore::render` picks it from
the bag's world bounding sphere, and a batch is one bag. Widening eligibility put
7 of 49 Motor District batches in that state.
**Fix:** `lampOf` in `buildStaticBatchList` keys it away at one extra batch. Any
new key that merges more objects must be checked against this: group the
candidates and ask whether the members agree on which lamp reaches them.

## Cross-district model groups cost more than they save
**Cause:** measured on Aster, cross-district material groups destroy spatial
culling, and even per-object material groups widened bag bounds enough to cost
more fill than their saved submits.
**Fix:** group each part by its loaded `Texture*` plus a coarse world cell, reject
singleton groups and keep models whose horizontal footprint exceeds half a cell
solo. Mesh LOD, impostors, reflections, dynamic lighting and runtime reference
paths stay solo. A multi-part member may belong to several batches
(`objectBatchOf == -2`): every yes/no test uses `!= -1`; helpers needing one
concrete batch require a non-negative index. Dirty demotion must remove every
part before the normal object path draws it.

## An off-screen large/LOD model is still classified
**Cause:** every object with geometry gets `ObjectGeometry::coarseBox` since
1.124.1; the old three-part threshold was an emulator-era guess a physical PS2
refuted (StaPip classifies an off-screen bag in ~17 us against ~2 us for this
test, paid again per companion bag).
**Fix:** do not put the threshold back without a console A/B. Preserve precise
per-part/package culling for intersecting models, transform frustum planes for
matrix-mode local vertices, skip impostors (a billboard rewrites its vertices
every frame), and bypass the reject when a VU script moves geometry beyond the
baked box.

## Continuous motion through a flow node destroys frame time
**Cause:** setting `RuntimeObject::dirty` makes `renderScene` rebuild that
object's whole **world-space** vertex array on the EE — fine for a one-shot
(Move/Rotate/Set Object Position, a recolor), ruinous per frame per object.
**Fix:** continuous motion belongs in a game-loop pass with the **matrix fast
path**: `rebuildObjectGeometry(i, /*localSpace=*/true)` ONCE (gated on
`physFastPathEligible`) bakes local-space vertices, then `updateObjMat` refreshes
`objectGeometry[i].objMat` and VU1 applies the motion. Built for physics bodies
(PROGRESS 116), now also `updateSpinners()` (Spin Object, PROGRESS 222): the node
writes only a RATE and the loop integrates it. `buildStaticBatchList` must reject
`wantsMatrixPath` even when stale data says `batchStatic`; do that guard before
group construction. Ineligible objects (usable, reflective, animated models) fall
back to `dirty`. Trade-off: baked shading freezes at the promotion pose. **A
generated SCRIPT asks for the same path through the RuntimeObject** — it sets
`wantsMatrixPath` and reads `onMatrixPath` (the Script-visible mirror of
`ObjectGeometry::matrixMode`); `renderScene` does the promotion. Check promotion
BEFORE `dirty` (such an object dirties itself on the frame it asks; a world-space
rebuild would clear the flag and leave it asking forever). The only thing still
needing a `dirty` re-bake is a SCALE change (scale is baked into local vertices).
Worth 16 → 50 FPS on examples/endless-runner.

## Physics bodies behave as if a write came from outside
**Cause:** `updateObjectPhysics` predicts the pose, collects corner contacts
(terrain, `objectCollisionBox` boxes WITH rotation, collision meshes per corner
ray, hull vs hull) and solves with sequential impulses + a split impulse.
**Fix:** the truth for orientation is `TerrainGame::physBodies` (a quaternion per
physics object), re-derived from `data.rotation` whenever something ELSE wrote
position/rotation/spin — external writers need nothing, but code inside the
solver must update `PhysBody::lastPos/lastRot/lastSpin` when it writes.
`spin[3]` is world angular velocity in deg/frame, a mirror the sim re-reads when a
script changed it. A shove with a point of application goes through
`physPushAt` (player walk-into, the car bumper), never a bare velocity add.
`physBodies` may reallocate while slots are handed out — `updateObjectPhysics`
takes every slot BEFORE it holds a reference.

## An invisible wall blocks the player or eats a raycast
**Cause/rule:** invisible walls reuse Box with `collisionMode == 3`
(`collision: invisible`, format 32), not object visibility.
**Fix:** they remain collision/nav/physics obstacles, but `rebuildObjectGeometry`
emits no parts and `staticBatchEligible` rejects them; the viewport draws a cyan
wire box. AO shape/atlas collection, GI geometry/signature collection, projected
shadows and raytraced/reflected views must all exclude them. The field
participates in equality and live-link recipe hashing. See
`docs/collision-boxes.md`.

## A merged building ejects a body through the floor
**Cause:** a body INSIDE a merged building's AABB was "penetrating" it; mesh
(per-triangle) collision used to exist only in `collidePlayer`, so nothing thrown
into a cellar could survive.
**Fix:** since 1.83.0 the static-solid pass of `updateObjectPhysics` has its own
`collision == 1` branch over the same `GameModel::collider` (a vertical floor ray
from the previous underside to the current one, then a side-aware
`resolveSphere` for the walls, velocity reflected along the push). `sweepSphere`
(camera boom, carried object, carry whisker, the non-physics thrown arc) still
collides against the whole-mesh box (`objectCollisionBox`).

# Frame preparation audit: where the EE still works the PC way (2026-11-07)

This page is a read-only audit of **frame preparation** on the EE: everything
between the game update and the DMA kick. It covers visibility, matrices,
packet building, StaPip, terrain/road/batch submission, the physics-to-render
handoff, the game's object loop and per-frame allocations. Effect features
(flashlight volumes, paint fresnel, particles) are out of scope. The question:
where does a stage still work "the PC way" with a workaround on top, instead of
the way a PS2 title would?

How it was made:
- Code read at `claude/perf-integration` ddf6a4d25 (2.13.1). Nothing was built
  or run.
- The benchmark is the generated `examples/vehicle-playground`. Its generated
  sources are cited (`src/terrain_game.cpp`, `src/gen/*.cpp`, `inc/*.hpp`
  under `examples/vehicle-playground/`); they are emitted from
  `src/game_templates.inc`.
- Every number is quoted from an earlier physical-PS2 measurement and its
  source is named. A number marked **guess** is an estimate this audit made
  and has not been measured.

Read with docs/architecture-loop.md (the loop that met the headline
benchmark), docs/dod-submission.md, docs/ps2-hardware-utilisation.md,
docs/tyrax2-architecture-audit-2026-10-20.md (the previous "PC way" audit,
whose closed rows are not reopened here) and docs/record-renderer.md on
branch `claude/record-renderer-1412cf` (the record-based rewrite running in
parallel).

## Summary

- **Most of the steady-state draw path is PS2-native now.** Clipping is on
  VU1. Static geometry is a baked VIF stream sent by REF. A stamped draw is a
  replayed head plus a VU0-macro MVP. The frame is one recorded DMA chain.
  The stamp and crossing passes read 64-byte-aligned records. A stamp-route
  draw costs ~7-8 µs of frame at the margin (RRP2, docs/record-renderer.md on
  the record-renderer branch).
- **Physics does not use VU0.** It runs on the scalar FPU over 3-float
  structs. VU0 macro and VU0 micro solvers were both built and measured
  slower. Physics is not memory-bound (25 % misses), so it is not a candidate.
- **Clipping is VU1-only in the benchmark and in every project on the default
  `"clipping": "vu1"`.** The EE clipper is still compiled and reachable. It
  runs for `"precise"`/`"fast"` projects, `.tyra` files with no `clipping` key
  (they load as `"precise"`), `examples/vu-lab`, and a game that calls
  `vuprog::setVU1Clipping(false)`. The EE still does the per-package frustum
  **classification** of crossing bags, which is not clipping.
- **The PC habits left are on the game side and in how the world is
  produced, not in DMA plumbing:**
  - an immediate-mode "walk every entity, submit every visible part/pass"
    frame over a ~430-byte array-of-structs entity table, with changes found
    by polling keys;
  - terrain built as float4 world-space arrays on the EE and drawn as up to
    five separate bags per chunk;
  - world-space re-bakes and copies of geometry when something changes.
- **None of the candidates below is measured above the 0.5 ms bar on its
  own.** Two blocks of the benchmark frame have never been lapped, and they
  are larger than any single candidate (see "What is not attributed").

## Stage table

Order is the generated `TerrainGame::loop()`
(`examples/vehicle-playground/src/terrain_game.cpp:222-979`).

| stage | files | runs on today | PS2-native shape | layout / cache notes | measured cost (source) | estimated ceiling |
|---|---|---|---|---|---|---|
| Input, menus, devkit, scripts | `terrain_game.cpp:238-416` | EE scalar | same | Scripts get `RuntimeObject*` and write poses directly; nothing reports a change | `in` 0.011 ms devkit off; scripts small (docs/profiling.md, FTUPD) | none |
| Broad-phase refresh | `src/gen/game_collision.gen.cpp:166-316`; boundaries `terrain_game.cpp:518,532,539` | EE scalar | writers push moved ids to a dirty list; the grid updates only those | Polls a 12-float key per object out of the AoS table at three stage boundaries (`bpHotAt`/lane sampling is the crutch); `bpCache` is a separate array | walk 0.24-0.27 ms (memory frame-escape-hunt, rounds 7 and 9) | ~0.2 ms (**guess**) |
| Physics | `src/gen/game_physics.gen.cpp:226-1650`; `inc/game_runtime.gen.hpp:5095-5160` | EE scalar FPU (no VU0, no MMI) | same, with SoA islands; VU0 tried | `RbStep` ~800-880 B AoS (hull corners inline); contacts 212 B; fused island solve since 2.11.1; static vectors reused | 72 awake 7.76 → 5.54 ms (2.11.1-2.11.3), 25 % misses; benchmark window 0.65 ms (docs/architecture-loop.md R2-0, R2-B, R12) | meets (b); no candidate |
| Physics → render handoff | `game_physics.gen.cpp:248-290` (prebake scans); `game_collision.gen.cpp:2573` (`updateObjMat`) | EE scalar | bodies write a matrix slot the draw record points at | Prebake VIF scan walks up to every object each frame looking for 4 to do; `updateObjMat` compares a pose key per matrix-path body (a cached-trig crutch over Euler angles) | not lapped separately | small (**guess** < 0.1 ms) |
| Vehicles | `game_vehicles.gen.cpp:3458`; colliders `game_vehicles.gen.cpp:3104` | EE scalar | same | Static collider grid shipped (B4) | driven car ~0.5 ms (memory fixed-step-cost; docs/profiling.md FTUPD `veh` 0.51) | out of scope here |
| Per-frame entity scans | `game_scene.gen.cpp:287` (dyn-lit), `:397` (anim), `:1627` (sound emitters), `:2222` (use target); `game_physics.gen.cpp:1931` (probe content key) | EE scalar | per-type index lists built at load; dirty lists | Each walks the whole `runtimeObjects` table (135 objects in the example, 254 in the user's crate field) and filters by type or flag. `RuntimeObject` ≈ 430 B (`inc/scene_data.hpp:8`, `inc/scripts/script.hpp:21`); `active` sits after the ~360 B `data` block, so the first test of every walk touches a different line from `data.type` | not lapped | 0.1-0.3 ms together (**guess**: ~250 objects × 2-3 lines × ~16 cycles × 5-8 walks) |
| Settle before `beginFrame` | `terrain_game.cpp:794-795`; `game_collision.gen.cpp:3422` (batches); `game_physics.gen.cpp:5828` (terrain chunk build) | EE scalar, writes world-space float4 arrays, then bakes a VIF stream | VU1 expands terrain from a packed height grid; batches as member REF + matrix lists | Terrain: `push_back` of `Vec4` position + `Color` (4 floats) + `Vec4` ST per vertex per pass (`game_physics.gen.cpp:6306-6406`); ~2.8 µs a vertex | terrain chunk 1.0-3.3 ms before 2.12.1, now ≤ 0.7 ms a frame budget; batch settle 1.6 ms at crash frame f382 (docs/architecture-loop.md R9-A, R9-B) | steady 0-0.7 ms driving, spikes in crash frames |
| Camera, frustum, matrices | engine `renderer_core_3d.cpp`; `stapip_qbuffer_renderer.cpp:4350-4385` (`stampMvp`); `core_bbox.cpp:347-431` | MVP on VU0 macro; object-space planes and AABB tests on the scalar FPU | same | `frustumCheckAABB` is the branch-light centre/radius form | inside the stamp/crossing costs | none (already native) |
| Game scene code (`renderScene`) | `game_physics.gen.cpp:1650-3260`; object loop `:2749-3045`; terrain `:7132-7226` | EE scalar | draw records written on change; per-frame pass only over visible records | Immediate mode: every visible part and every companion pass (`aoBag`, `emisBag`, env) is a separate `stapip.core.render` call each frame | `G.scene` 2.5 ms district, 38-56 % misses (R0J); object loop game side 0.3-0.6 ms (frame-escape round 6) | see candidate 3 |
| Terrain draws | `game_physics.gen.cpp:7204-7224` | EE: up to 5 bags a chunk (base, layers, AO, ground shadow, emissive) | one upload per chunk, extra passes as extra XGKICKs from VU memory | The ground-shadow pass leaves CLAMP in force, so the next chunk cannot replay its stamp | 12.5 remainder draws = 1.21 ms of the benchmark (R12); ground-shadow pass 0.4-1.1 ms (docs/shadows.md) | see candidate 2 |
| StaPip recording | `stapip_core.cpp:589-650` | EE | append-only, O(1) | Duplicate-bag check scans the whole recorded list per call (O(n²)) | ~0.1 ms driving, fixed on the record-renderer branch (d3e741734) | 0.1 ms (measured there) |
| StaPip flush: stamp pass | `stapip_core.cpp:834`, `stapip_qbuffer_renderer.cpp:3540-3600` | EE; records 64 B aligned; one `pref` | native | `StampRecord` / `StampRevision` alignas(64), 512 slots each (64 KB, too big for SPR) | 2.33 ms at 105 stamps (R12), but the marginal draw is 7-8 µs, whole route ≤ 0.5 ms driving (RRP2) | ≤ 0.5 ms (measured ceiling) |
| StaPip flush: crossing pass | `stapip_core.cpp:864` (`crossClassify`) | EE classifies each package's box | VU1 rejects per package, or VU0 micro classifies a batch | Per-bag crossing record, contiguous package boxes | 0.83 ms, 10.6 draws at 79 µs, 41 % misses (R12) | see candidate 5 |
| StaPip `renderNow` remainder | `stapip_core.cpp:1267-2098` | EE | every draw from a record | Bag → info → model → texture → cacher → baked entry → stamp table; `renderNow` is 11.5 KB with 492 branches (R4-A) | 2.11 ms, 18.6 draws, 71 % misses: whole replay 0.62, partial 0.90, direct 0.45 (R12) | see candidate 1 |
| Clipping | `stapip_qbuffer_renderer.cpp:2336-2360` | **VU1** (`clip_*` family) | native | the EE clipper is bypassed when `vu1Clipping` | - | none |
| Packet build | `stapip_qbuffer_renderer.cpp:241-326` (`appendChainQwords`, `emitRef`), `stapip_vu1_program.cpp` | EE, cached memory, stock `packet2` for the unpack headers | write once into the frame's memory | Retained commands as REF, baked streams as REF; `packet2` back-patches bytes it already wrote | R3 "producers write into the frame bank": neutral (2026-10-20 audit) | closed |
| Frame recorder + Vif1Queue | `renderer/core/paths/path1/vif1_queue.cpp`, `frame_chain_arena.hpp:361-369` (lq/sq copy) | EE copies packets into the frame bank; polled queue; lazy `FlushCache` | one chain built in place | 128-bit lq/sq copies | recorder pays for itself: off = work +1.5-2 ms (docs/ps2-hardware-utilisation.md); `FlushCache` ~0.009 ms | closed |
| DMA kick | `vif1_queue.cpp` | DMAC ch1, one chain per frame, polled | native (an interrupt-driven start crashed a real PS2) | - | waits 0.007-0.03 ms driving (R0H, R13D) | none |
| Per-frame allocation | whole path | none in steady state | - | `static std::vector` reused (`steps`, `contacts`, `movedIdx`, ...); `costRows` allocates only during a capture | - | none |
| DynPip / skinning | `skel_instance.cpp` | VU0 macro skinning on the EE thread | VU1 skinning | - | ~0.9 ms per 1092-vertex character (docs/ps2-hardware-utilisation.md) | not in the benchmark |

## What is not attributed (lap these first)

Two blocks of the benchmark frame are bigger than any candidate below and
have no laps in a release build.

1. **The update before `beginFrame`: 2.48 ms at 40 % misses**, of which
   physics is 0.65 (R12, `BR12A`, window 250-450). That leaves **~1.8 ms**
   unlapped. It holds vehicles, the walker and chase camera, broad-phase
   walks, particles update, sound, the settles and the per-frame entity
   scans. The FTUPD laps exist (docs/profiling.md) but were never read on the
   release benchmark.
2. **The frame without present is 12.27 ms. The update (2.48) and the StaPip
   flush (5.30) leave ~4.5 ms.** That figure is a subtraction, and the
   project already learned not to trust a remainder (memory
   object-loop-cost: "never attribute a remainder; lap the code"). It holds
   at least: the game's `renderScene` code, which is `G.scene` 2.5 ms in the
   district; non-StaPip renderers (2D HUD, post-fx, DynPip if any); packet
   sends; and the recorder.

**Probe (cheapest, no new code paths):** one release arm of the benchmark
with `TYRA_FRAME_PROFILE` laps around `renderScene` minus the flush, the
FTUPD sections and `endFrame`'s pre-present part, read over the same
250-450 window. Without it, every ranking below is guesswork about which
block the next millisecond is in.

## Candidates: PC-style with a crutch, ranked

The ranking is by estimated prize on the benchmark, then by how directly a
probe can bound it. Remember the RRP2 lesson: a per-draw cost derived as
"pass time ÷ draws" overstated the marginal cost about 3x (22 µs against
7-8 µs). Every ceiling below should come from a skip-it probe, not from a
division.

### 1. The general `renderNow` path validates a mutable bag graph per draw

- **PC habit.** An immediate-mode draw call (`stapip.core.render(bag)`). It
  re-derives each draw's state every frame by chasing the bag → info → model
  → texture → bbox cacher → baked entry → stamp table. The crutch is a set of
  caches in front of it:
  - stamps and their revision classes;
  - the crossing record;
  - retained commands.

  Each cache covers one route, and every draw it declines falls back to the
  full path.
- **Evidence:**
  - `vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp:1267`
    (`renderNow`, ~830 lines) and `:589-650` (`render`, the recording front).
  - The bag-side reads per draw are counted in docs/dod-submission.md,
    "Phase 3: the hot record": texture 39, count 32, lighting 20,
    `info->model` 17, and more.
- **Cost (measured).** The remainder is 2.11 ms of the benchmark window at
  71 % misses: whole replay 8.1 draws 0.62 ms, partial declined 5.9 draws
  0.90 ms, direct 3.0 draws 0.45 ms (docs/architecture-loop.md R12). Terrain
  is 12.5 of the 18.6 draws (1.21 ms).
- **Ceiling (guess): 0.6-1.2 ms.** A draw still has to be sent, and the RRP2
  marginal cost suggests well under half of the attributed 2.11 ms is
  avoidable.
- **Already tried:** R12-A (whole replays from a record, loser), R13-A
  (terrain wrap state, -0.2..-0.3 ms, verify not clean), and the REPEAT entry
  (slower, record-renderer branch).
- **Cheapest probe.** A rig arm where `renderNow` returns at entry for every
  draw that neither pass took. The picture breaks; arm 2 of RRP2 is the
  template. It bounds input and output together. If it comes in under
  0.5 ms driving, the general path is done as an EE item.

### 2. Terrain is built on the EE as float4 world-space arrays and drawn as up to five bags a chunk

- **PC habit.** A chunk is built in the CPU, like a vertex buffer on a PC:
  - position, colour and ST are `push_back`ed per vertex per pass as
    `Vec4`, float `Color` and `Vec4`;
  - the arrays are baked into a V4-32 VIF stream;
  - each pass (base, splat layers, AO, ground shadow, emissive) is a
    separate bag through the whole draw path.

  The crutches:
  - a 0.7 ms-a-frame build budget (2.12.1);
  - LOD settles before `beginFrame`;
  - the ground-shadow fold option.

  The CLAMP the ground-shadow pass leaves in force is what keeps the next
  chunk's draw off its stamp (R13-A).
- **Evidence:**
  - `examples/vehicle-playground/src/gen/game_physics.gen.cpp:5828`
    (`buildTerrainChunk`), with the per-vertex `push_back` at `:6306-6406`;
    template `src/game_templates.inc:31418`, `:32050`;
  - the passes at `game_physics.gen.cpp:7204-7224`; template
    `src/game_templates.inc:33165-33260`;
  - every StaPip unpack is V4-32 (`packet2_utils_vu_add_unpack_data` in
    `programs/*/stapip_*_vu1_program.cpp`).
- **PS2-native shape:**
  - VU1 expands the chunk from a packed height grid (S-16/V4-16 heights,
    xz and ST from the loop counters, LOD as a stride);
  - shading comes from a per-node V4-8 colour;
  - the extra passes are further XGKICKs of the vertices already in VU
    memory, each with its own TEX0/CLAMP in its own head, so no state leaks
    into the next draw.
- **Cost (measured):**
  - the build budget is ≤ 0.7 ms a frame while driving; before it,
    1.0-3.3 ms a chunk (R9-A/B);
  - terrain draws are 1.21 ms of the remainder (R12);
  - the ground-shadow pass was 0.4-1.1 ms (docs/shadows.md);
  - in the user's crate-field project, layer passes were 0.46 ms
    (docs/stapip-bag-cost.md census, before 2.12).
- **Ceiling (guess): 0.5-1.0 ms driving.** The build part is bounded by the
  measured 0.7 ms budget. The draw part overlaps candidate 1.
- **Already closed.** For the district's day workload, identical-position
  fusion was rejected (docs/tyrax2-world-stream-2026-10-11.md, "Terrain
  Multipass Eligibility"): the ground-shadow pass draws a lifted subset, not
  the base positions. Splat layer passes do share `ch.vertices` exactly.
- **Cheapest probes:**
  - Freeze terrain LOD and streaming after the first second of the drive
    (`settleTerrainChunks` and `updateTerrainChunks` return early). The
    picture goes stale but the frame stays valid, and this bounds the build
    half.
  - Draw only `ch.bag` (skip the companion passes): this bounds the pass
    half. The ground-shadow half of it is already measured.

### 3. The game's entity table is a fat array of structs, walked and polled every frame

- **PC habit.** `RuntimeObject` is one ~430-byte struct per object:
  - authoring data (`SceneObjectData`, ~90 fields: emitters, sound, light,
    LOD, impostor) sits next to the runtime state;
  - scripts write its poses directly, so nothing reports a change;
  - each per-frame system scans the whole table and filters by type, and
    each consumer re-discovers motion by comparing float keys.

  The crutches:
  - `bpHotAt` / lane sampling in the broad phase;
  - pose keys in `updateObjMat` and `worldShape`;
  - the content FNV hash for the reflection probe;
  - per-car compact lists that were bolted on after measurements (memory
    object-loop-cost, 1.125.2).
- **Evidence:**
  - the structs, `examples/vehicle-playground/inc/scene_data.hpp:8` and
    `inc/scripts/script.hpp:21`;
  - the walks, at `game_physics.gen.cpp:2749` (object loop), `:1931` (probe
    content key), `:272` (prebake scan); `game_scene.gen.cpp:287`, `:397`,
    `:1627`, `:2222`;
  - the broad-phase poll `game_collision.gen.cpp:166-316`, reached after
    three invalidations a frame (`terrain_game.cpp:518`, `:532`, `:539`).
- **PS2-native shape:**
  - a hot SoA (position, flags, batch id, type: 16-32 bytes an object, so
    254 objects fit in 4-8 KB);
  - per-type index lists built at load;
  - setters that append to a dirty list the broad phase, matrices and draw
    records consume.
- **Cost (measured):**
  - `G.scene` 2.5 ms in the district at 38-56 % misses (R0J);
  - object loop game side 0.3-0.6 ms (frame-escape round 6);
  - broad-phase walk 0.24-0.27 ms;
  - earlier fixes of the same shape paid: physics body index list 0.51 →
    0.013 ms, AI car gather 0.47 → 0.17 ms (memory object-loop-cost).
- **Ceiling (guess): 0.3-0.6 ms.** The per-object rows are each small. Phase
  2 of the record-renderer plan (scene objects and cars) touches the same
  table.
- **Cheapest probe.** A PMU arm with laps around each of the walks above,
  one release run. If their sum is < 0.3 ms, close the item. A
  "skip it" arm is unsafe here, because the scans feed sound and USE.

### 4. Geometry that changes is re-baked in world space, in float4, on the EE

- **PC habit.** Changing geometry is treated like a dynamic vertex buffer.
  When a static batch loses or gains a member, a dirty object is rebuilt, or
  a car is dented, the EE rewrites world-space `Vec4`/float-`Color`/`Vec4`
  arrays. It then re-bakes or re-copies the VIF payload.

  The crutches:
  - batch patching by ranges;
  - prebake queues;
  - `kRefreshStreak`;
  - one pre-bake step a frame;
  - body batches apart (`STATIC_BATCH_BODY_MAX`).
- **Evidence:**
  - `game_collision.gen.cpp:2945` (`rebuildStaticBatch`; `push_back` tails
    at `:3138-3140`) and `:1556` (`rebuildObjectGeometry`);
  - `renderObjectBag` calls, one per pass, at `game_physics.gen.cpp:2983-2993`.
- **PS2-native shape:**
  - a batch is a list of (member stream REF, member matrix) on VU1, so a
    member leaving is one list edit;
  - vertex data is stored packed (V3-16/V2-16/V4-8: ~16-24 bytes a vertex
    instead of 48-64).
- **Cost (measured, spikes only):**
  - batch head copy 1 ms/1800 vertices before its fix;
  - re-bake 0.80 ms after it;
  - settle 1.6 ms at f382 (R9-A);
  - payload refresh copy 3.6 ms (~470 KB) on the hard crash, later spread
    out (memory physics-awake-cost and frame-escape-hunt).
  - **Steady frames pay nothing.** The benchmark already has 0 missed
    frames.
- **Ceiling (guess).** Halving to two-thirds of the bytes the EE writes or
  copies in change frames is worth ~1-2 ms in a crash frame and ~0 in a
  normal one.
- **Already measured.** Instanced submission was measured: VU1 coronas and
  plane pools as matrix geometry were at noise, and awake bodies through
  `renderInstances` left the Objects total unchanged. The packed-format half
  is unmeasured on the EE side; V4-8 colours were rejected for **VU1** time
  only.
- **Cheapest probe.** A counter of bytes baked plus bytes copied per frame
  (bake, refresh, patch), read over the benchmark. Under ~50 KB in the worst
  frame means no prize.

### 5. Crossing bags are classified package by package on the EE

- **PC habit.** CPU-side frustum culling at sub-mesh granularity, so the GPU
  only gets what it needs. A PS2 title lets VU1 reject per triangle or per
  package and keeps the EE at whole-object granularity. The crutch is the
  crossing pass: a per-bag record, and one pre-pass over contiguous package
  boxes per flush.
- **Evidence:**
  - `stapip_core.cpp:864` (`crossClassify`) and the routing at `:2409-2482`;
  - `core_bbox.cpp:383-431`.
- **Cost (measured):**
  - the crossing pass is 0.83 ms (10.6 draws at 79 µs, 41 % misses, R12);
  - the R1-A ceiling of a VU1 "clip only what needs it" family is ≤ 0.6 ms
    in the crate field (docs/ps2-hardware-utilisation.md), blocked by
    micro memory (2034 of 2048 words resident);
  - the whole partial bag into the VU1 clip program measured +0.6/+2.5 ms
    (2026-10-20 audit, R5).
- **Ceiling.** Measured ≤ 0.6 ms for a VU1 move. **Guess** ≤ 0.3 ms for
  batching the classification on VU0 micro: the VU0 micro physics solver lost
  to its pack/upload overhead, and the same risk applies here.
- **Cheapest probe.** Already run (R1-A arm 2048). Do not reopen unless micro
  memory frees up.

### Smaller items (each measured or bounded under 0.2 ms)

- **O(n²) duplicate check in `StaPipCore::render`.**
  `stapip_core.cpp:600-604` scans `recorded` per call. It is fixed on the
  record-renderer branch (d3e741734, about -0.1 ms driving). Port it with
  that branch.
- **Polling prebake scan.** `game_physics.gen.cpp:272` walks up to every
  object a frame to find four bags to prebake. A queue fed when a body joins
  a batch near the camera would make it O(work). **Guess** < 0.05 ms.
- **`updateObjMat` from Euler angles.** It rebuilds the matrix from
  `data.rotation` (degrees) through a trig cache
  (`game_collision.gen.cpp:2573-2605`), where a body that integrates a
  quaternion could write its matrix directly. The cache already removes most
  of the cost (memory physics-awake-cost: 18 `sinf`/`cosf` a body before
  `rotTrigCached`).

## PS2 tricks not used in frame preparation

| trick | used? | where / why not |
|---|---|---|
| 16 KB scratchpad (SPR) for hot working sets | **no** | Only mentioned in `vif1_chain_check.hpp` and `vif1_queue.cpp:201`. Direct SPR prefix construction was built and lost 0.18-0.39 ms (docs/tyrax2-spr-call-runtime-2026-10-04.md). Reading records instead of bags is worth only 0.06-0.13 ms in total (RRP2 arm 1), which caps any SPR staging of records. The stamp tables are 2 x 32 KB (512 slots x 64 B), larger than SPR. |
| SPR DMA (ch8/9), double-buffered streaming | no | dod-submission phase 4, unbuilt; ceiling as above |
| `pref` | once | `stapip_qbuffer_renderer.cpp:3544`, the stamp pass head lines. Prefetching the whole next entry was slower (docs/stapip-bag-cost.md) |
| Uncached-accelerated memory for write-only streams | probe only | `TYRA_STAPIP_PROBE_UNCACHED_CHAIN` (`stapip_probes.hpp`): uncached +7.55 ms; UCAB unsafe because stock `packet2` back-patches bytes it already wrote |
| Avoiding the D-cache write-back on large write-once buffers | partly | lazy `FlushCache` per chain (~0.009 ms); baked streams written once and REF'd; geometry re-bakes (candidate 4) still go through the cache |
| 128-bit lq/sq copies | yes, in places | `frame_chain_arena.hpp:361-369`; elsewhere `memcpy` (`appendChainQwords`, `stapip_qbuffer_renderer.cpp:241`) |
| DMA CALL/RET replay | **no** | never in production; the corrected CALL trial was +3.3-3.5 ms; chain-form stamps (R1) neutral |
| REF instead of copy | yes | `emitRef` (`stapip_qbuffer_renderer.cpp:319`), baked streams, retained commands, VU1 clipping reads source by pointer (`stapip_core.cpp:2474-2477`) |
| VIF unpack straight from source data | yes | baked VIF streams by REF |
| Packed unpack formats (V2-16, V3-16, V4-8) | **no** | every StaPip stream is V4-32; V4-8 was rejected on VU1 time, and the EE side was never priced (candidate 4) |
| VU0 macro for matrix/vector math | yes | `Vec4`/`M4x4` operators, `stampMvp`, bbox min/max (`core_bbox.cpp:27-50`), skinning. Not in physics (scalar `CollisionV3`) or the frustum tests (scalar FPU, already branch-light) |
| VU0 micro as an asynchronous co-processor | no | only the VU0 ray tracer; the physics solver on VU0 micro was slower (3.40 → 3.91 ms) |
| VIF0, MFIFO | no | not needed by the current design |
| Small I-cache-hot loops | partly | the stamp and crossing passes are stage-major loops. `renderNow` (11.5 KB, 492 branches) and the game's interleaved object loop are not; `-Os` and hot/cold ordering measured worse or neutral |
| 64 B line alignment of hot records | partly | `StampRecord`, `StampRevision` and the crossing records are alignas(64); `RuntimeObject`, `RbStep` and the bag graph are not |
| Interrupt-driven VIF1 | no | crashes a real PS2 (memory vif1-submission-queue) |

## Stale notes found and fixed in this audit

The notes below said or implied that the EE clips in today's default path.
The code says otherwise. `"clipping": "vu1"` is the default
(`src/project.cpp:1388-1392`, `:6420-6428`). It reaches `CLIP_VU1`
(`src/templates.cpp:11839`, `:12412`), and `StaPipQBufferRenderer::clip`
returns before the EE clipper when `vu1Clipping` is set
(`stapip_qbuffer_renderer.cpp:2345-2360`).

| file | was | now |
|---|---|---|
| `docs/reflective-materials.md` (scenery-box and ground-grid paragraphs) | boxes and the ground grid "go through the EE clipper" | the clip route: per-package classification on the EE, clipping on VU1 (the district is a `vu1` project) |
| `docs/procedural-runtime.md` (prefab merge paragraph) | a crossing chunk "is clipped triangle by triangle on the EE" | classified per package on the EE and clipped on VU1 |
| `docs/backlog.md` (scenery boxes) | "mostly EE clipping of big boxes" | the clip route (EE classification + VU1 clip) |
| `.claude/skills/tyra-engine-dev/references/static-pipeline.md` and `.agents` twin (History line) | real cost of partial geometry = classification + EE clipping | marked as the EE-clipper era; today it is classification + dispatch on the EE, clipping on VU1 |
| `.claude/skills/tyra-editor-dev/references/lighting-and-bakes.md` and `.agents` twin | scenery boxes cost "(EE clipping of near boxes)" | the clip route of near boxes |
| `.claude/skills/tyra-editor-dev/references/tyrax2-integration.md` and `.agents` twin (`reflscenery` row) | "mostly EE clipping of big near boxes" | the clip route |
| `.claude/skills/tyra-editor-dev/references/procedural-and-prefabs.md` and `.agents` twin | "(EE clipping)" | "(the clip route)" |

The memory notes corrected in the same pass are listed in the session report:
vu1-clipping-implementation, vu1-clipping-cost-measurement,
stapip-partial-cost, tyra-engine-clipping, dod-submission and the MEMORY.md
index.

Left as they are, because they are true:
- the rest of docs/vu1-clipping.md, docs/vu-authoring.md,
  docs/toolchain-image.md, docs/flashlight.md and the CHANGELOG. They
  describe the EE clipper as the legacy mode, or are dated history.
- docs/ps2-hardware-utilisation.md. Its unit map already lists
  classification, not clipping, as EE work.

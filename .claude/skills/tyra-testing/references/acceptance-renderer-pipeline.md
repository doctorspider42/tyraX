# Acceptance recipes — static pipeline, batching, culling, caches, frame presentation

Read when: you change StaPip (retained commands, baked VIF stream, bag content versions, bbox cache), static batching/culling, a skip-when-unchanged optimisation, visibility/LOD/impostor swaps, first-entry HUD, or buffer/presentation (hybrid triple buffer). Fixture hygiene: performance-and-profiling.md.

## General shapes

- **What is SUBMITTED is not what is SEEN — ask by REMOVAL.** Hide exactly one object at runtime (`ctx.objects[i].visible`), `--capture-frame`, diff against control. **0 px = contributes nothing by ANY path** (silhouette, projected shadow, baked AO, reflection) → free to cull. Non-zero = its on-screen contribution; rank by `triangles / visible pixels`. Drive the whole population from ONE boot (write index set to a command file, wait, capture). Before any verdict: within-probe repeatability; control drift (control first AND last in the boot); **a positive control for every zero** (photograph the suspected occluder removed too; compare pair against occluder, NOT against control — they change the same region). Worked example `world-visibility-2026-09-17/` was removed in 3e7b0f67a (git history).
- **"Collisions and picking unchanged" without a capture**: diff GENERATED SCENE DATA. Adding models inserts them in `model_data.gen.hpp` in place → indices shift (coupe `model` 11 → 13 = same body). Build `index -> name` for both tables, rewrite the control's model field to the candidate index for the same NAME, diff; residual must be only fields the change owns. Select object rows by FIELD COUNT (other tables share `{...}, // name`). Report consequences (assigning an impostor clears `batchStatic`) rather than allow-listing.
- **Judging a POP: the ratio, not the pixel count.** Park at STATIONS across the switch distance (not a moving camera — two arms' `--capture-frame` never land on the same frame). `move(ctl)` = adjacent-station change in control (the yardstick); `move(cand)` = same step with the swap. Ratio ≈ 1 invisible, ≫ 1 pop. Cover distance swaps (approach route) AND view-sector swaps (ORBIT at fixed radius — rotating in place doesn't change the object-to-camera-POSITION vector).
- **Skip-when-unchanged: three fixtures** (reflection reuse budget 1.106.0): parked = BEST case, label it; MOTION (idle / straight / gentle turn / hard turn) — saving fell to ZERO in a hard turn; INVALIDATION (camera parked, scene changes: hide/show, slide, day/night flip) with a no-change regime as its own control. Predict each regime's number first ("one capture per toggle, 120 of 120 for a thing moving every frame"). Record the quality cost in the change's unit from INSIDE the game (worst staleness in pixels of the 128-px reflection target). Parked traffic (benchmark fixture popped `vehicle.route`) scores 100% on any skip; quote parked + moving-traffic (pixels only from parked).
- **A skip must be a NO-OP**: hold the old code as an oracle, compare live buffer vs full rebuild EVERY frame including skipped ones, **inside the game** (e.g. `TYRA_WHEEL_REBUILD_VERIFY`, default 0, re-derives each drawn car's wheels with PRE-CHANGE arithmetic → `VERIFY checked=N STALE=M`). Don't share code between checker and hot path.
- **Hoisted code must be BIT-identical**: same operations, order, operands; prove over a wide random spread incl. degenerate branches with a native harness (no editor/engine link).
- **Two arms tell you something moved, not which is right**: add a batching-OFF (`staticBatching: false`) third arm as ground truth — showed the new cell byte-identical to unbatched and the OLD one drawing 400 px of props the unbatched scene culls.
- **A steady count is not a steady picture**: counts identical across four `FTCLIP` windows while captures differed 283–419 px within one arm. Report which gate a fixture passes.
- **Dominated case**: worse on every axis (more triangles AND flushes) needs no hardware to condemn; a trade (+23.5% triangles, −43% flushes) is a console question.

## Retained static command data (1.96)

One knob, one worktree, one project dir: `TYRA_STAPIP_RETAINED_COMMANDS` in `vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp`, 1 vs 0 (engine source → ordinary `--build` rebuilds it).
1. **Packet byte-identical → picture byte-identical**: frozen camera, `--capture-frame` x3 per arm; repeats identical within arm first. Any cross-arm difference = capture/replay bug.
2. **Counts must not move**: `FTCLIP` `verts`/`flush`, frame-cost submission count. New counters: `STAPIPRET retained=N rebuilt=M per frame, cache=K KB` (debug engine, every 300 frames) or `StaPipCore::takeRetainedCommandHits/Builds`.
3. Then time — PCSX2 has no EE data cache; delta = upper bound.
Stress invalidation: evictions with a batch pending, pipeline switch away/back, LOD crossings both ways (tier swaps vertex pointer AND count), scene reload, hundreds of moving frames. `rebuilt` ≈ 0 parked, spikes exactly where geometry changed.

## Content-version contract (docs/bag-content-version.md)

`BagArray<T>`: every array handed to a `StaPipBag`; `data()` const; every mutation stamps `contentVersion`. Negative check, no PS2 toolchain:
```bash
tools/bag-array-enforcement.sh <project>/inc/bag_array.gen.hpp
```
Seven cases on the REAL header (`TYRAX_BAG_ARRAY_NO_TYRA` drops the four `bind()` overloads): 2 positive (API compiles; stamp MOVES), 5 escapes refused. Falsify: non-const `data()` in a copy → `passed 5, failed 2`. The positive control matters (a non-compiling header "passes" every negative).
- `bind()` aims at `data()`; an EMPTY vector's `data()` may be null → *"Vertices are required in 3D render bag!"* at first frame. Size rings before binding AND before the first updater/builder (vehicle smoke clears dead slots, skids may spawn in physics, glow writes lit-lamp geometry before checking its bag) — zero-length spans called `Vec4::set` through null; PCSX2 hid it, hardware cause 3 `BadAddr 0` after loading.

## Baked VIF stream (`TYRA_STAPIP_BAKED_STREAM`)

Same header, 1 vs 0, one project dir; `TYRA_FRAME_PROFILE` and `TYRA_STAPIP_BAKED_REPORT` = 1 in BOTH arms (revert before commit).
- Gate = picture + counters: `FTCLIP` `cull`/`clip`/`guard`, `verts`, `flush`; `STAPIPBAKE` `chainQw` (DMA chain qw handed to VIF1 per frame; compiled into control too). No ms.
- Counter gate holds only while flush cadence is fixed (a run under one `REF` can't cross a packet flush, so pinning `packetFlushes` pins the saving). A cadence-moving change uses docs/baked-stream-acceptance-gate.md: canonical NOP-stripped hash of the word stream VIF1 receives (decoded at the packet tap, texture mutations interleaved) + byte-identical pixels over a pose sweep; `FTCLIP` routing counts become diagnostics.
- A gate arm is never a timing arm (hash reads ~2.7 MB/frame, 10–25 ms).
- Motor District: drive by pose file, never `--pad`; garage day is the only pixel pose.
- **A matching capture hash is not a fixture check**: a borrowed old editor produced `stripRun = 72u` (`cull=40175 ... verts=55332` vs `38750 ... 55836`) yet garage day still hashed `415f970f...73f1e`. Check `ROADSTRIP scene 0 ... packages 470`, `TERRAINSTRIP ... vertices 588 packages 8` in `bin/log.txt` and `grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp`.
- `STAPIPMISS` = why a bag rebuilt: `bbox`, `prim`, `streams`, `program`, `size`, `new`, `incomplete` + biggest bag (verts/packages). Garage day `bbox=1 prim=2`/frame, outer road zero. Since the content-version contract `bbox=` (moved) is split from `content=` (re-shaded): garage day `bbox=300 content=600` per 300 frames, naming the 2 280-vertex bag (two cars' paint pass).
- **Adversarial arm needs no control**: `TYRA_STAPIP_BAKED_VERIFY` rebuilds every block with ordinary writers and compares → run with traffic MOVING; `failed=0` = acceptance (2026-09-17: **169 843 blocks, failed=0**, ~12 600 frames). Enrich MISMATCH lines (stream pointers + differing qw's four floats named the caller; three equal RGB lanes drifting 0.012 = camera-dependent shade).

## Wheel-batch skip (docs/wheel-rebake-skip.md, 1.100)

`WHEELBAKE cars=N rebuilt=M wheels=W rebuilt=X batches=B stamped=S per 300 frames` behind `TYRA_WHEEL_REBUILD_REPORT` (default 0, timed `host:` write — never inside a sampling window). Lives in the GENERATED game: flip in `src/templates.cpp` or `-D` on the game compile, not an engine header. `stamped` (submits that bumped `bboxVersion`) should fall to zero on parked traffic once settled.

## Pricing a culling/grouping change in PCSX2 (1.102.1, docs/model-pipeline.md)

`TYRA_FRAME_PROFILE 1` (identical every arm, reverted) → `FTCLIP` every 50 frames; generated game prints `Static batching:` at load (`eligible 87, solo 22` / `65 objects in 48 batches` on vehicle-playground). No instrumented loop needed. Batching-OFF third arm as reference (above). Short paths, one PCSX2 per arm by hand, `frame.tga` deleted between captures, DAY pose.

## Static submission batch (1.93)

Complete baked resource hashes, four parked day/night poses, warmed unarmed frame-cost rows, a second baseline boot, GS captures. Stress residency separately (evict with batch pending, pipeline away/back; fresh frame progress, reuploads, intact textures). Night images twinkle/flicker — compare same-build repeats first. Lower DMA count alone isn't acceptance (bigger-bag prototype cut sends but raised pipeline waits). docs/static-submission-batching.md.

## Bbox counters and bbox cache (2026-09-28)

- `StaPipCore::takeTelemetry()` folds `cacher.stats` into `telemetry.attrib` and resets → peeking `telemetry.attrib.bboxCache*` around one draw gives zero deltas. Per-object probes must snapshot live `cacher.stats` plus live telemetry brackets (frame exports via `takeTelemetry()` are fine). Corrected arms found two fresh replacements per frame in each of Ravager and Pica. One ELF per A/B group with a boot-only switch; report instrumented times apart from the production 23.923 ms baseline.
- Count-variant acceptance on physical PS2: cached main boxes, every child box, merged min/max and coarse bounds vs freshly constructed `StaPipBagPackagesBBox`; repeated full/prefix requests, content-version bumps, hundreds of changing counts, different VU package sizes, recycled-address versions, full expiry, hash-index rebuild after partial expiry (3,470 lifecycle cases + 48,803 live requests). Correctness arm — no timings. Shipped engine keeps `TYRA_STAPIP_ATTRIB=0`, no oracle/boot-toggle/raster hooks. (Patches/README were in `night-entry-hardware-2026-09-28/`, removed in 3e7b0f67a.)

## Hull and reflection proxies (1.111.0)

Hull: bake a concave multi-material OBJ via Properties, resave, refresh; emitted OBJ has one `usemtl`, `4n-4` faces for its convex XZ ring, non-billboard far assignment. Boot near/far; collision/picking identity from scene data. Material consolidation: repeated `usemtl` groups resolving to identical state → inspect `.tmdl` part counts; a captured 4/8/16-view impostor is the negative control (ordered parts survive). Reflection box proxy: per-producer inventory + console self-capture with only `reflectionProxy` changed — env-probe row falls to one bag / one package, main-view rows and collision fields identical.

## Exact first-entry HUD (1.150.1)

Stationary night fixture, every frame through the driver transition; one ELF with a boot-only old/new HUD gate; repeat the control. Config reads guarded by the one-time init result (a helper whose `done` returns false later → per-frame host I/O → invalid timing control). Check single-init logs, identical ELF hashes, driver/speed/traffic state, aligned CSVs; keep first-entry work/HUD/uploads apart from warm medians. Lifecycle: revisit scenes, count atlas copies, sprite/texture identity; evict VRAM and reload. Plain/unknown-token strings must not request the icon sheet; a valid icon loads once and reuses. Run loading/render checks after timing, never in the window. Compile a non-vehicle orbit project too. HUD lifetime probe: mutate VRAM inside an active render frame (after scene submission, before frame end), then test resolved icons; the between-frame forced mutation stalled at the barrier in PCSX2 — don't count that or a blocked forced scene transition as lifecycle evidence. Font prep ≠ scene revisit. -O1 harness timings are never hardware evidence; physical PS2 is authoritative.

## Hybrid triple buffer (1.153.0)

Frozen-camera scratch project with solid geometry + HUD; hybrid double/triple in the software renderer; compare finished-scene captures; GS dumps: CT32 draw at slot 0, CT16 copies to slots 1/2, DTHE only during copies, PSMZ32 depth. Fresh frame progress, no alternating black frames. Limiter on/off, field mode, a BLSS layout rebuild, a mode with insufficient headroom. Mixed project: scratch global script calling `ctx.requestDisplayMode` to switch field/full PAL at the pre-frame boundary (also with BLSS scene-disabled) → 3/2 buffer grants, fresh captures after both. Regular 32-bit triple buffering still rotates all three targets. Preferences checkbox enables in Hybrid and persists. PCSX2 = correctness, not hardware frame pacing. Presentation changes also need the motion gate (motion-and-ab-rigs.md).

# TyraX2 architecture audit (2026-10-20)

A read-only, big-picture review of the whole TyraX2 frame flow, asking one
question: where does the renderer do things "the PC way" instead of the way a
PS2 title would, and what could be baked by the editor instead of computed on
the console every frame. Target: stable 60 fps on real hardware. No code was
changed and nothing was measured for this page; every number below is quoted
from an existing record (cited), and the key claims were checked against the
source.

**Read the numbers with care.** Several night prices (pools/beams ~1.9 ms,
live lights ~1.1 ms, dressing ~0.8 ms) come from the 2026-10-02..06 ELFs
([tyrax2.md](tyrax2.md), [night ablation](tyrax2-night-ablation.md)). The
latest lean district rig is far lower (night ~10.3 ms parked / ~10.7 ms
driving, [night EE notes](tyrax2-night-ee-2026-10-12.md)). Treat directions as
solid and the milliseconds as upper bounds until re-priced on the current ELF.

## Verdict

The VU1/VIF core is already PS2-native: geometry is double-buffered in VU
memory (`setDoubleBuffer(22, 461)`, `xtop`, TOPS-relative unpacks, MSCNT
continuation), static geometry is stripped, the whole program set is resident,
clipping runs on VU1 with a guard band, and a frame is one DMA chain with N/N-1
overlap. The remaining PC habits are (1) how much the EE re-derives for every
bag every frame, and (2) static lamps treated as fully dynamic lights.

The offline terrain / final-VIF track ([offline world](tyrax2-offline-world.md),
[offline VIF](tyrax2-offline-vif.md)) netted 0.04–0.08 ms for 2.8 MB because it
attacked replacement-time generation; the cost lives in per-frame per-bag
plumbing and in runtime lighting of static content.

## Red flags (architecture)

### R1. Static bags are re-derived every frame instead of being ready chains

- A stamp hit costs ~34 µs; 122 hits ≈ 4.1 ms/frame at the heavy night spot
  ([stapip-stamps.md](stapip-stamps.md), segment table: find 0.29, producer
  facts 0.35, frustum 0.36, light pick 0.74, close previous packet 0.70,
  copy+MVP+fog 0.59, send after stamp 0.87 ms). The light pick has since been
  trimmed (0.44 → ~0.40 ms on the driving fixture, same page).
- The bag census finds only **20 distinct model matrices** per frame — almost
  every static bag is baked in world space ([night EE notes](tyrax2-night-ee-2026-10-12.md),
  "Fewer draws"). A per-bag EE model×view×proj is mostly redundant.
- PS2 way: upload VP once per frame into VU1 memory; each static object is an
  immutable prebuilt chain; patch only what moves.
- CALL/RET is **not** ruled out by hardware. The freeze was an END patched into
  a zero-QWC NEXT joining two separately allocated packet2 chains
  (tyra-engine-dev skill, "Do not fuse already-finished ... chains"). A
  corrected CALL ran on the console ([SPR/CALL runtime](tyrax2-spr-call-runtime-2026-10-04.md));
  it was slower because it was layered on top of the snapshot path instead of
  replacing per-bag work. Viable once the baked stream is authored as real DMA
  subchains (today it is a tag-free REF payload, [SPR staging](tyrax2-spr-staging.md)).

### R2. One FLUSHE per bag

- `vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp`
  ~1218–1232: uniforms land at absolute VU1 addresses (MVP 0..3, OPTIONS 8,
  CLIP_CONSTS 20, ALPHA 21, planes 944..955), so every bag — and every replayed
  stamp, which copies `[FLUSHE..REF]` — waits for the microprogram to end.
- ~110–250 drains per frame (112.5 bags / 572.5 packages garage day; 197–213
  XGKICKs). Estimated bubble 2–4 µs each → **~0.3–0.8 ms of VU1 idle**, which
  matters where VU1 is on the critical path (the parked road-bands floor is
  VU1-bound). Unmeasured.
- The fix is designed but never built: **S2**, a second uniform bank at VU1
  22..43 plus a bank bit ([ee-submission-rearchitecture.md](ee-submission-rearchitecture.md),
  "Supporting changes"). Burnout 3 for reference issues ~600 FLUSHE per frame
  at 60 Hz, so the drain is only expensive where VU1 is the bottleneck.
- Cheaper variant to evaluate first: the cull programs load the MVP into
  registers at program start; if they also copied SINGLE_COLOR / LOD / TESTS /
  CLUT / ALPHA to private scratch (1020–1023 are free), FLUSHE would only be
  needed around clip-family bags and program overrides.
- **First step:** look for VU1 idle gaps at bag boundaries with the `--gputl`
  rig before building anything.

### R3. Packets are written twice

- Producers build a cached packet2; `Vif1Queue::submit` then copies every CNT
  payload and every mutable REF target into the frame bank and writes a native
  chain (`renderer/core/paths/path1/vif1_queue.cpp` `recordSinglePass`,
  ~341–400). ~53 KB chain + ~93 KB REF data per frame; submit ≈ 0.85 ms after
  1.176.1; zero-copy priced at ~0.4 ms ([night EE notes](tyrax2-night-ee-2026-10-12.md)).
- The deeper PC-ism is the builder: packet2 writes float by float and
  back-patches already-written bytes. That is why UCAB never booted and plain
  uncached cost +7.55 ms ([ee-submission-rearchitecture.md](ee-submission-rearchitecture.md)),
  and why SPR construction was slower.
- PS2 way: producers write once, sequentially, straight into the frame's
  buffer. Recorded as **H02** in the [EE performance audit](tyrax2-ee-performance-audit-2026-10-05.md),
  not built.
- `kNativeQwords = 8192` (128 KiB) forces a mid-frame flush+wait on overflow.

### R4. Dynamic effects rebuilt as geometry on the EE every frame

- Lamp coronas are camera-facing quads rebuilt on every moving frame; the car
  headlight pool is rebuilt in world space. Driving windows: pools/beams/decals
  ~2.3 ms, particles/glow ~1.5 ms ([night EE notes](tyrax2-night-ee-2026-10-12.md)).
- The wheels proved the pattern: matrix instead of a rebake took 1.02 →
  0.27–0.34 ms (same page). VU1 billboard programs already exist
  (`programs/billboard/stapip_billboard_{c,t}_vu1.vclpp`); the GS SPRITE
  corona variant was never promoted ([GS sprite coronas](tyrax2-gs-sprite-coronas.md)).

### R5. Partial bags classified and clipped per package on the EE

- 160–210 µs per partial bag, ~2.6 ms while driving ([night EE notes](tyrax2-night-ee-2026-10-12.md)).
- Coarser per-third classification is **refuted** (+4.59 ms, S3). The open
  lever is a whole partial bag sent as one REF into the streamed VU1 clip
  program (lead 6; 1.174.0 did part of it).

## Bake instead of compute

### B1. The night state of static lamps (largest single category)

- All 8 garage lamps are `lightDynamic` only so the day/night script can
  switch them; 7 of 8 do not flicker ([backlog](backlog.md), "Bring the night
  garage chase view back under the PAL frame budget";
  `examples/vehicle-playground/src/scripts/district_mood.cpp`).
- That flag pulls in, every frame: re-registration (`updateDynLights`), a
  `pickDynLight` per bag, per-bag spot preparation, pool patches
  (`setupLightPools` / pool render in `src/game_templates.inc`) and beams.
- `src/gibake.cpp` (~459) skips dynamic lights ("never baked"); there is no
  lighting-set / bake-variant path anywhere in `src/`.
- Proposal: the editor bakes **two lighting states** (day, night) and the game
  switches them on the mood change. Static objects: night vertex colours /
  lit textures through the existing routes. Terrain: a per-chunk additive
  night map drawn only on lit cells (the 4-bit ground-shadow-map mechanism in
  reverse), carrying the pools' gobo footprints. Only car headlights, the
  flashlight and player/driven-car receivers stay live. Beams and coronas
  stay per frame (camera-facing) but as one immutable batch.
- Prices this targets (older ELFs): pools/beams 1.87–2.07 ms, live lights
  1.06–1.26 ms ([tyrax2.md](tyrax2.md), night isolation); subgroup cuts pools
  0.88–0.97, beams/coronas 0.68–0.78 ms ([night ablation](tyrax2-night-ablation.md)).
  Also removes the static-lamp re-scoring, priced 0.33–0.36 ms pick + 0.19–0.24 ms
  spot build ([night EE notes](tyrax2-night-ee-2026-10-12.md)).
- Needs its own image and hardware gates; marking lamps static alone changes
  scene behaviour.

### B2. Night dressing

The 11 emissive window boxes are solo `reflected=True` bags toggled per mood
(0.74–0.94 ms in the older isolation). Bake them into a night variant of the
facade textures, or emit one static batch whose visibility follows the mood.

### B3. Reflection-probe background

Each capture re-renders the frozen sky, static buildings and ground
([reflective materials](reflective-materials.md)); roughly 1–1.7 ms/frame
while the camera moves. PS2 way: an editor-baked panorama per road cell and
mood (sky, buildings, ground) drawn as one textured dome in the probe, with
only cars and dynamic objects live. Trade: parallax on near buildings.

### B4. Vehicle colliders

`buildVehicleColliders` walks ~170 runtime objects every frame to rebuild an
almost static list, then each car / debris piece scans it linearly
(182 µs build + ~150 µs per-car gather, [night EE notes](tyrax2-night-ee-2026-10-12.md)).
An editor-emitted static cell grid with only dynamic bodies listed at runtime:
~0.25–0.3 ms, scaling with traffic.

### B5. Static spot shadow volumes (other projects)

Off in the district, but where used a static spot's volumes are rebuilt every
frame (caster pick, extrusion, count bracket) even with lamp and casters
static. Bake into that pool's gobo texture.

## Yellow (measure before acting)

- **Static vertex colours as float Vec4 + `FixColor` per vertex on VU1**
  (`programs/cull/stapip_cull_tc_vu1.vclpp` ~147/157/167; macro in `vcl_sml.i`).
  PS2 way: V4-8 unpack — ready integer RGBA from the VIF, 4 B instead of 16 B
  in RAM. 3 of ~13 upper ops per vertex; ~0.3–0.4 ms VU1 parked. Only the
  spot-lit loop needs the clamp. Cheap, canonical trim.
- **TEX0 always carries `CLUT_LOAD`** (`renderer_core_texture.cpp`
  `updateClutBuffer`; emitted per bag in `stapip_qbuffer_renderer.cpp` ~1616).
  The CLUT reloads even for the same texture. CLD 4/5 or sort by CLUT; < 0.2 ms.
- **Bloom** (0.12 in the main scene) is a chain of full-screen single-sprite
  blits, not 32/64-px strips like the hybrid copy, with ZTEST on. ~0.5 ms GS
  for a barely visible effect.
- **Hybrid with double buffering**: one 32-bit draw target, copy after vsync,
  so the GS cannot start N+1 early. Hybrid triple exists but ran 54 Hz with
  tails at night ([ready present](tyrax2-ready-present-2026-10-11.md)).
- **VRAM is an LRU cache with upload on first bind** behind an `align3D`
  barrier. Free today (28 uploads, 0 re-uploads over 4440 frames), but the
  heap has 0.119 MB free and the pause menu already evicts.
- **Terrain in several passes** (base, shadow cells +0.40 ms, emissive at
  night, AO when enabled). Part of it could live in terrain vertex colour.
- **Polled VIF1 queue** (`TYRA_VIF1_QUEUE_ISR=0`, `waitFor` spins on CHCR).
  Measured waits ~0.001 ms with the pipeline on; fine in practice.
- **Stamp lookup** is a hash keyed by bag pointer (0.29 ms); a slot stored in
  the bag would make it O(1). Moot if R1 lands.
- **The VU1 clip programs process every strip corner three times.** In
  `programs/clip/stapip_clip_tc_vu1.vclpp` a strip advances the input by one
  vertex per `triLoop` (`streamStepReady`, step 1) but loads and processes all
  three corners each iteration: MVP multiply, spot light, colour clamp, fog,
  STQ. A 72-input strip (70 triangles) does 210 corner passes for 72 vertices.
  The cull programs already process each strip vertex once; only the clip
  route repeats. The [critical-path page](tyrax2-critical-path.md), "VU power-move
  assessment", proposes a per-input preparation stage in VU1 memory; the
  resident image set has only **8 free instruction words** (2034 + 6 of 2048).
  - Ceiling: the clip programs were ~1 ms of VU1 in the parked frame before
    road bands ([night EE notes](tyrax2-night-ee-2026-10-12.md), "The parked
    floor was VU1"), probably less now; S-H, emit and fan remain. Realistic
    ~0.2–0.4 ms of VU1, and only in VU1-bound frames (parked).
  - Preferred design: a **sliding window**, the classic PS2 strip approach.
    Keep the last two processed corners (clip-space position, clip flags,
    final colour/STQ/fog) in registers or a two-entry scratch and process one
    new vertex per triangle. No 72-qword prep area in the tight XTOP halves,
    no extra loop, and one vertex block instead of three unrolled copies, so
    the program can shrink — addressing the 8-word budget instead of forcing
    other images out. Traps: the S-H path must preserve or reload the window;
    strip parity (corner order) stays as is, only the data source changes;
    list input (step 3) keeps the old path; keep the MVP operation order so
    output stays bit-exact.
  - Cheap check before writing a shader: measure the clip programs' VU1 share
    and clip-routed triangles per frame with the GPU sampler on the current
    ELF; upper bound = cycles per corner × 2/3 of corners × clip triangles.
    Below ~0.2 ms in a VU1-bound frame: skip. Otherwise start with the unlit
    strip route, exact raster + same-ELF physical gates. Same family as S2 and
    V4-8 (recovering parked-frame VU1 time); price all three with one rig.
- **Package size** is capped at ~72–75 vertices because input and output use
  separate VU areas; in-place output could roughly double it. Low priority
  (Burnout uses ~68 vertices per chunk).

## Already done right (do not reopen)

VU1 double buffering and strips; resident program set, no pipeline switches;
VU1 clipping with guard-band routing; prelit static programs without
directional lights, spot and fog gates; GS fog (XYZF2); no VRAM readback in a
frame; no heap allocation / `std::function` / heavy virtual dispatch in the
hot path; `FlushCache` now ~0.009 ms; paint fresnel on the EE only on turns
(VU1 variant is a dead end, +0.5 ms); occlusion/PVS measured and rejected
(2 of 142 objects occluded); instancing has nothing to amortise (20 matrices);
CLAMP sent only on change; P4 state diff; reflection probe split over three
frames with a reuse budget and ground stand-in.

## Goal: implement and verify every item on the console

**Finding that reorders the page (agent A, PS2, 2026-10-20):** on 1.176.3
the parked night frame is **EE-bound, not VU1-bound**. The EE waits for the
GPU ~8 µs per frame; VU1 is busy ~32–36% and idle ~44–51% of the period;
renderScene is ~9.9 of 11.3 ms parked work (StaPipCore::renderNow ~6.9 ms).
VU1-time trims cannot move the frame; moving EE work onto VU1 can.

Work through this whole page: implement each opportunity, verify it on the
physical PS2 (same-ELF A/B where timing is claimed, exact or reviewed images
where appearance can change), and tick it here with the result — shipped,
rejected with its measurement, or blocked with the reason. A measured "no
gain, reverted" counts as done; an unmeasured change does not. Appearance
trades ship opt-in until reviewed on a TV.

| Item | Status | Result |
| --- | --- | --- |
| B1 two-state lighting bake (static lamps) | shipped (on in example) | CZ17 (50 cycles): all 8 lamps baked parked −0.12, first drive −0.48, cruise −0.67, late −0.32 ms; 7 lamps only +0.11 parked / −0.22 cruise. Example: bakeStaticLamps on, lamp 7 flicker 0.025→0; PS2 shots <0.5 % px, max 30. |
| B2 night dressing batch / texture | rejected | Already 3 static batches since 1.175.5; hiding all 11 windows saves ~0.07 ms (CZ4). |
| B3 baked probe background | shipped (on in example) | Per-road-cell 128x32 panorama (v97, 1.177.0). PS2 DG10: probe EE per driving frame up to −0.08 ms, driving work p50 10.80→10.55 ms; only car paint differs (≤9 levels). Agent D branch 98348d980. |
| B4 static vehicle-collider grid | shipped (on) | CZ9 same ELF: vehicle update 0.52→0.36 parked, 0.58→0.42 ms driving; frame work −0.05 parked, −0.10..−0.16 ms driving; byte-compare oracles 0 mismatches. |
| B5 baked static spot shadow volumes | shipped (on) | CZ14 night-walk, PAL, 17 cycles: kept volumes −0.20..−0.22 ms in every window (e.g. 7.557→7.341, 7.727→7.515 ms; CIs clear); SPOTVOLKEEP oracle 0 mismatches. |
| R1 immutable static chains, VP in VU1 | rejected | BA2 (34 cycles/arm): ~80 chain-form replays/frame, parked 10.341 vs 10.323, cruise 10.907 vs 10.918 ms — the cost is what the replay reads, not writes. Removed. |
| R2 per-bag FLUSHE (measure, then S2) | rejected | PS2 same-ELF (vpnAR1, 21 cycles): no-FLUSHE ceiling arm 11.50 vs controls 11.49/11.51 ms parked; drains = ~0.1 ms VU1 idle off the critical path. Commit 560ef51c9 (agent A branch). |
| R3 producers write into the frame bank | implemented, kept off (measured) | BA4 (73 cycles): neutral driving, one parked window +0.18 ms; no gain on top of R5 mode 4. Switch `Vif1Queue::setOwnedPackets`. |
| R4 coronas / pools as matrix or sprite geometry | rejected (kept opt-in) | CZ17: VU1 coronas parked +0.03, cruise −0.06, late −0.06; plane pools parked 0, cruise −0.08, late −0.01 ms — at the noise floor. |
| R5 whole partial bag into the VU1 clip program | shipped: runs + masks (mode 4) default | Whole bag rejected (+0.6/+2.5 ms). BA4: mode 4 parked within noise, first drive −0.15, cruise −0.11, late drive −0.19 ms. |
| Y clip-program strip sliding window | rejected | Under the 0.2 ms rule: 8 strip packages / 479 tris parked, ceiling 0.05–0.17 ms of VU1, and VU1 is not critical. |
| Y V4-8 static vertex colours | rejected (VU1) | ≤0.3–0.4 ms of VU1 inside ~7 ms of VU1 idle per frame; EE side not priced. |
| Y CLUT load method | rejected | CLD=1 reload costs 0.08–0.17 µs; DG4/DG11 parked work 10.16 vs 10.18 ms night, 9.56 vs 9.56 day. Reverted (53a365640). |
| Y bloom strips / ZTEST off | strips rejected; bloom off in district | Strips 0.318 vs 0.317 ms GS. Bloom off: parked work 10.16→9.96 (night), 9.56→9.29 ms (day); ≤6 levels diff. Example bloom 0.12→0. |
| Y Hybrid presentation structure | rejected | Second 32-bit target does not fit; triple (DG5) 59.7/59.2 vs double 59.6/58.8 Hz, all misses from the rig's print stall; work unchanged. Stays double. |
| Y VRAM planned residency | not needed (measured) | 607 KB free with pause menu open, 4 uploads/0 evictions; zero uploads/evictions in every park/drive window. |
| Y terrain passes into vertex colour | shipped opt-in, PS2 image review pending | Day sun-shadow pass costs 9.33→9.57 parked, 9.46→10.59 ms driving; night passes free. Fold (bakedShadowGroundFold, v97) softens building shadows (4-unit grid vs 0.5-unit mask) — DG13 PS2 image pair still to run. |
| Y stamp slot in the bag | rejected (measured) | BA4: no window improves; switch stays off. |
| Y larger VU packages (in-place output) | not built (bound below bar); PS2 confirmation run EY2 owed | Only frustum-crossing bags pay per package (33 parked, 37–86 driving); doubling saves ≤0.1–0.2 ms driving, ~0.05 ms parked (≤19 µs/package, R5 runs arm −0.14/−0.27 ms). Clip programs cannot go in place; kRun + all strip emitters would re-bake. |

## Suggested order

1. B1 + B2: two-state lighting bake for static lamps and night dressing.
2. Measure the R2 bag-boundary bubble on `--gputl`; then S2 (or the
   scratch-copy variant).
3. R3: producers writing straight into the frame bank (H02), replacing
   packet2's back-patching builder on the hot path.
4. R1: static objects as immutable chains with VP resident in VU1, via
   CALL/RET into real subchains.
5. R4: coronas / pools as matrix-driven immutable geometry or VU1/GS sprites.
6. On the side: V4-8 colour unpack, CLUT load method, bloom strips.

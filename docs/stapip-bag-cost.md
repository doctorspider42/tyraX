# What one StaPip bag costs the EE on a PS2

This page is a measurement of the fixed EE (CPU) cost of submitting one bag
through the static pipeline (`StaPipCore::render`) on a physical PS2. It breaks
that cost down by route and stage, shows that it is almost entirely waiting for
memory, and lists the one cut that was kept and the ones that were not.

The frame is EE-bound on the console: in the crate-field drive of
`vehicle-playground` the GPU never made the EE wait over 1 000 frames (max
0.04 ms). The game's own object loop is 0.3-0.6 ms, and the rest of the scene
recording is StaPip work per bag.

## How it was measured

- Fixture: a copy of the user's `vehicle-playground` project (129 crates in
  rows, the Ravager), the scripted 1 000-frame drive, engine log muted, window
  of 900 frames (300..1199).
- COP0 count laps around `StaPipCore::renderNow`, recorded per route and per
  stage, plus laps inside the dispatch, a census of partial bags, and laps
  around `StaPipQBufferRenderer::sendPacket`.
- EE PMU: counter 0 on event 6 (I-cache miss), counter 1 on event 6 (D-cache
  miss), counting in kernel, supervisor and user mode. The game zeroed both
  counters every frame, because a counter overflow is an unmaskable exception.
- Arms toggled at boot in one ELF (the game read a mask from `host:arm.txt`).
  Two or more runs per arm.

The probes were in a private copy of the engine and are not in the repository.

## The cost per route

Per frame, baseline:

| Route | Bags a frame | µs a bag | I-cache misses a bag | D-cache misses a bag | ms a frame |
|---|---:|---:|---:|---:|---:|
| Stamp hit (docs/stapip-stamps.md) | 48-50 | 25.6 | 58 | 101 | 1.27-1.36 |
| Culled by its main box | 1.4-2.2 | 29-34 | ~100 | ~105 | 0.05 |
| Whole baked replay | 3.6-4.0 | 85-88 | 290 | 280 | 0.31-0.36 |
| Direct packages (not baked) | 2.6 | 149-153 | 460 | 700 | 0.38 |
| **Partial replay** (crosses the frustum) | 13.5-14.3 | **145-150** | 412 | 395 | **2.0-2.1** |
| `endSubmissionBatch` | 2 | 50 | - | - | 0.10 |

StaPip is 4.2-4.3 ms of the frame here, and half of that is bags that cross
the frustum: static batches, road chunks and the crates at the screen edge.

**It is memory, not instructions.** At ~50 EE cycles an I-cache miss and ~42 a
D-cache miss (calibrated earlier, docs/tyrax2-night-ee-2026-10-12.md), the
misses alone account for 86-100 % of every route's time. A bag always starts
cold: the game runs its own code between two bags and evicts StaPip from both
caches. The ordinary path touches ~20-26 KB of code per bag (290-412 lines of
the 16 KB I-cache) spread over about 25 functions.

## Where a partial bag's 147 µs go

| Stage | µs |
|---|---:|
| `tryStamp` attempt (fails) | 10.7 |
| Bounds: size, transform cache, bbox cache, object-space planes, main box | 18.4 |
| Prepare: MVP, clip planes, packager | 12.5 |
| Prepare: texture + wrap | 7.4 |
| Prepare: program set, light pick, fog/info | 4.5 |
| Prepare: `sendObjectData` | 13.9 |
| Dispatch: `beginBakedBag` (cull entry) | 11.6 |
| Dispatch: retained check + `beginPartialReplay` (clip entry) | 8.4 |
| Dispatch: `packager.create` (9.5 packages, `checkFrustum` 2.1 µs each) | 28.5 |
| Dispatch: routes loop | 1.7 |
| Dispatch: `emitPartialRuns` | 6.7 |
| Dispatch: its packet sends (0.75 a bag) | 22.4 |

The splits show where the first touch of each line lands rather than what each
stage computes: a stage that only does a hash probe still costs 5-6 µs when it
is the first to touch the bag.

A packet send (`sendPacket` -> `Vif1Queue` recording) is 22.6 µs with 47 I and
88 D misses; there are 31-34 a frame, ~0.75 ms.

## Kept: partial replay classifies without package records

The partial replay used `StaPipBagPackager::create()`, which fills a
`StaPipBagPackage` record per package (five stream pointers, sizes, 1/3
indices), and then read three fields of each to pick a route. It now calls
`StaPipBagPackager::classifyRoutes()`, which writes the routes and clip masks
directly, with the same coarse groups, merged part boxes and mask rule
(`TYRA_STAPIP_LEAN_CLASSIFY`, default 1). Telemetry runs and the fallback when
the runs do not fit the packet still build the records.
`TYRA_STAPIP_LEAN_CLASSIFY_VERIFY` (default 0) runs both and prints
`LEANVERIFY checked=.. bad=..`. On the PS2 drive, 137 943 packages matched in the probe build and 400 003
in the clean build (no probes, shipped code plus the verify switch), all
exactly.

Physical PS2, same ELF toggled at boot, two runs an arm:

| | Partial µs a bag | Dispatch | Classification | Routes loop | Partial ms a frame |
|---|---:|---:|---:|---:|---:|
| Before | 149.7 / 148.7 | 80.2 / 78.1 | 28.0 / 28.3 | 1.6 / 1.8 | 2.06 |
| After | 141.6 / 142.8 | 72.5 / 73.1 | 23.2 / 23.5 | 0.2 / 0.2 | 1.90 / 1.95 |

About 7-8 µs off every crossing bag, ~0.1 ms a frame in this scene. The win
scales with the number of crossing bags a frame, so any scene with many large
static batches or road chunks at the screen edge gains it.

## Measured and not kept

- **Static batches, roads and procedural chunks inside one submission batch
  scope** (they are drawn outside any scope when the interleave is off):
  packets 33 -> 25 a frame, send time 0.64 -> 0.58 ms, StaPip total unchanged
  (4.27 / 4.26 vs 4.26 / 4.16 ms). A send costs by the bytes it records, not by
  the call (docs/stapip-stamps.md said the same of `submit`).
- **`tryStamp` tests visibility first** when the model did not move since the
  capture (rejecting earlier is always safe): the partial bags' stamp attempt
  10.7 -> 8.9 µs, ~0.025 ms a frame. The attempt costs its first touch of the
  bag, not its checks; rejects a frame were few (no stamp 12, producer 4.6,
  view 7).
- **The StaPip directory at `-Os`** (`renderNow` 12.2 -> 5.8 KB,
  `sendObjectData` 11 -> 3.7 KB): I-cache misses a partial bag 413 -> 393 and
  every route 4-6 % slower. The hot path is the lines it touches, not the
  function sizes; the earlier whole-engine `-Os` result holds per file too.
- **Packed base array for `ImmutableSpanTable::lowerBound`** (a 4 KB `u32`
  search instead of 16-byte spans): not measured; its two runs ended in the
  console wedge after a ps2link reset (see below), and a last-hit cache for
  the same lookup measured nothing earlier (docs/stapip-stamps.md).

## Rig traps

- Leaving the PMU counting after the game stops looked like it made the next
  `ps2client reset` wedge the console: with the per-frame enable removed,
  resets went through again. Enable the PMU only for the runs that need miss
  counts.
- `freepad: DMA Busy` during scene load after a reset needs a power cycle and
  says nothing about the arm under test.

## What is left

The per-bag cost is the memory a bag touches. The remaining levers are
structural: fewer bags crossing the frustum (they cost 1.7x a whole replay),
fewer packet sends (31-34 a frame at ~22 µs), or a lean path for crossing bags
the way stamps are one for visible bags. The head stamp for crossing bags was
tried and removed (docs/stapip-stamps.md), because the replay read about as
much memory as the ordinary path.

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

## A bag does not fit the cache even on its own

Two probes, physical PS2, two runs an arm:

- **Warm floor.** Every bag's `renderNow` runs twice in a row and the second
  run is timed apart (it is drawn twice, so the frame itself is not a
  measurement). The warm bag still pays most of its cold cost:

  | route | normal µs/bag | warm (second run) µs/bag |
  |---|---:|---:|
  | stamp | 28.3 / 28.4 | 22.2 / 23.0 |
  | culled | 30.7 / 31.3 | 10.7 / 11.0 |
  | direct packages | 149.9 / 151.4 | 114.8 / 115.6 |
  | partial replay | 149.3 / 149.7 | 137.1 / 138.1 |

  The working set of one bag is larger than the EE's 16 KB instruction and
  8 KB data caches, so the misses are a bag evicting itself, not one bag
  evicting the next. Ordering bags by route so the path stays hot is capped
  below the warm column (at most ~12 µs a partial bag, ~0.17 ms a frame) and
  stamps are already replayed in runs (docs/stapip-stamps.md). Not worth
  building. Only the small culled route fits.
- **Crossing bags that clip nothing.** Of the partial-replay bags (13.4-13.9
  a frame), 22-25 % have no package to clip, only packages to draw and to drop.
  None has every package drawn, so none could take the whole replay as it is:
  that replay would draw the dropped packages unclipped. They cost 155-161 µs,
  more than the average crossing bag.

## Where the bags come from, and what fewer of them would take

A call-site census (every `renderWorldBag`/`renderObjectBag` tagged with its
line, crate-field drive, one frame's average):

| producer | bags a frame | ms a frame |
|---|---:|---:|
| static batches + roads fed into the object loop (`dripHeavy`) | 13 stamped + 5 other | 1.06 |
| scene objects drawn solo (mostly woken crates, 36-vertex cubes) | 20 stamped + 0.4 other | 0.64 |
| terrain chunks, base pass | 1.7 stamped + 5.3 other | 0.56 |
| terrain layer passes | 1.9 stamped + 2.6 other | 0.46 |
| static batches drawn directly (`submitHeavy`) | 3.4 stamped + 1.5 other | 0.38 |

A stamped bag spends its ~26 µs evenly: ten stages of 1-5 µs each (lookup,
producer facts, renderer facts, visibility, texture, light pick, batch and
wrap, texture use, copy, flush). No stage is worth a change of its own.
Each stage is the first touch of another few lines.

Measured and not kept:

- **Prefetching the whole stamp entry** (head, captured bytes, baked entry,
  texture, colour) for the next bag of a stamp run, and also for the bag after
  it: stamp 34.4 / 35.1 µs -> 35.1 / 35.8 (whole entry) and 35.8 / 36.5 (two
  ahead). The run already prefetches the head lines, which is all that lands
  in time.
- **Static batches grouped in cells twice as wide** (160 instead of 80 on the
  crate field): stamped bags unchanged (46 -> 43-47 a frame), crossing bags
  13.2 -> 17.0, StaPip 4.40 / 4.49 -> 4.80 / 4.96 ms a frame. The cell bound
  in docs/static-batching.md holds.

The one producer where fewer bags is plausible is the solo crates: a woken
body draws alone at ~29 µs for 12 triangles until it rejoins its batch. That
is a gameplay and batching decision (how soon bodies rejoin, or drawing awake
copies of one mesh through `renderInstances`, which has no dynamic light and
no stamps today), not a submission fix.

## One stage for every bag at once: the ceiling of a stage-major pipeline

The warm-floor probe repeats the whole path, so it cannot tell whether a
pipeline that runs one stage for every bag before the next stage would keep
its code in the cache. A probe measured that for the bounds stage:
`renderNow` recorded every bag it was handed (precise culling, MVP), and once
a frame a separate pass ran only that stage for all of them in one loop
(`getMaxVertCountByBag`, the transform reuse test, the bbox cache lookup, the
object-space planes and the main-box test, the same calls the path makes),
with the clock and both PMU counters around the loop. Physical PS2, two runs:

| | µs a bag | I-cache misses a bag | D-cache misses a bag |
|---|---:|---:|---:|
| Bounds stage inside the path (partial / whole / direct bags) | 17.6-18.2 / 20.4-21.3 / 29.8-30.4 | - | - |
| The same stage, one pass over all bags | **4.33 / 4.33** | 1.5 | 15.5 |
| The pass run again straight after | 4.19 / 4.19 | - | - |

63-65 bags a frame went through the pass in 0.27-0.28 ms. Run as a stage of
its own, the bounds work costs a quarter of what it costs inside the path. Its
instruction misses fall to 1.5 a bag, it no longer gets faster when repeated,
and the D-cache misses left are the bag, info, model and cache entry it really
reads. The other routes did not change in the arm that ran the pass (partial
bounds 17.6-17.8 µs), so the pass warmed nothing for them.

This first measurement shows that the per-bag overhead comes from the
structure of the path, not from the hardware: the same calls on the same data
cost 4 µs instead of 18-30 µs when their code stays in the cache.

**The prepare stage, measured the same way.** A second version of the probe
keeps what the bounds pass produces (the box, the frustum result and the
object-space planes of every bag, as a stage-major pipeline would have to), and
then runs prepare for every bag that reached it in the real path: MVP, clip
planes, packager, texture facts, wrap, light pick, fog and info, and
`sendObjectData` into a scratch packet. The qbuffer renderer is saved byte for
byte before that pass and restored after it, so nothing reaches the real
chain. Two things are left out: `useTexture`, because it uploads (the real
path's texture buffers are replayed instead), and `ensureProgramSet`, because
it may upload programs. Physical PS2, two runs, results identical to the
second digit:

| | bags a frame | µs a bag | I-cache misses a bag | D-cache misses a bag |
|---|---:|---:|---:|---:|
| Bounds, one pass, results stored | 64 | 6.00 / 6.01 | 6.9 | 26.2 |
| Prepare inside the path (partial / whole / direct) | 13.9 / 3.3 / 2.5 | 39.5-40.8 / 35.0-36.3 / 29.8-31.2 | - | - |
| Prepare, one pass | 19.4-19.6 | **15.64 / 15.67** | 28.5 | 48.3 |

- Prepare drops from ~38 µs a bag (the routes' weighted average) to 15.6,
  about 2.4 times rather than the bounds stage's 4. Its code does not fit
  the cache even on its own: `sendObjectData` alone is ~11 KB of the 16 KB
  I-cache, and 28 instruction misses a bag remain. The data misses (bag,
  info, texture, light, packet lines) are about half of what is left.
- The pass flatters the packet writes: it writes the same scratch lines every
  time, where the real path writes fresh packet memory.
- Storing the bounds results costs 1.7 µs a bag (4.33 -> 6.0). A real
  stage-major pipeline pays that too.

Together, for the ~20 bags a frame that are not stamped or culled, the two
stages would go from about 1.2 ms to about 0.5 ms a frame, out of the 4.3 ms
StaPip spends here. Dispatch (partial replay classification, runs, packet
sends: ~78 µs a crossing bag) is the largest stage and was not probed. It
reads and writes the most memory, so it is the least likely to shrink by the
same factor.

## Rig traps

- Leaving the PMU counting after the game stops looked like it made the next
  `ps2client reset` wedge the console: with the per-frame enable removed,
  resets went through again. Enable the PMU only for the runs that need miss
  counts.
- `freepad: DMA Busy` during scene load after a reset needs a power cycle and
  says nothing about the arm under test.

## What is left

The per-bag cost is the memory a bag touches, and one bag alone overflows the
caches, so reuse between bags cannot pay for much. What is left is touching
less memory per bag, hiding the data misses (prefetching the next bag's
headers and boxes while the current one is processed), or the structural
levers: fewer bags (49 stamped bags a frame are ~1.35 ms of mostly fixed
cost), fewer bags crossing the frustum (they cost 1.7x a whole replay),
fewer packet sends (31-34 a frame at ~22 µs), or a lean path for crossing bags
the way stamps are one for visible bags. The largest one measured is the
structure itself: run each stage over every bag before the next stage starts,
so each stage's code stays in the cache (bounds 18-30 -> 4.3-6.0 µs a bag,
prepare ~38 -> 15.6 µs a bag, see above; dispatch not probed). The head stamp for crossing bags was
tried and removed (docs/stapip-stamps.md), because the replay read about as
much memory as the ordinary path.

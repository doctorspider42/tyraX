# Data-oriented draw submission (design, in progress)

This page is the plan and the running record of a rebuild of how StaPip turns
the game's draws into VIF1 packets. The aim is to make the EE wait less for
memory. Branch `claude/dod-submission`, started 2026-11-05 after the
measurements in docs/stapip-bag-cost.md and docs/ps2-hardware-utilisation.md.
Each phase below is measured on a physical PS2 before the next one starts,
and a phase that does not pay is reverted and written up here.

## Why

The gameplay frame is EE-bound in every scene measured except the showcase.
VU1 is idle 44-51 % of the night district frame, and the crate field never
makes the EE wait for the GPU. Inside the EE, StaPip's per-bag cost is 86-100
% cache misses.

- **I-cache.** A bag runs ~20-26 KB of code (290-412 lines of the 16 KB
  I-cache) spread over ~25 functions.
- **D-cache.** It also touches its own scattered data. The bag, its info,
  model, colour and texture bags, the texture object and resident list, the
  bbox cache's buckets and boxes, the baked-stream table and entry, the stamp
  table and entry, and the packet lines it writes. That is 100-700 D-cache
  lines per bag for an 8 KB D-cache.
- **Neither fits.** One bag run twice back to back is still 92 % of its cold
  cost.

Measured per bag (crate-field drive, docs/stapip-bag-cost.md):

| route | µs | I-cache misses | D-cache misses |
|---|---:|---:|---:|
| stamp | 25.6 | 58 | 101 |
| culled | 29-34 | ~100 | ~105 |
| whole replay | 85-88 | 290 | 280 |
| direct | 149-153 | 460 | 700 |
| partial (crossing) | 142-150 | 412 | 395 |

StaPip is 4.4 ms of a ~6-7 ms crate-field frame, so the prize is real. Three
things are already measured:

- **Code reuse alone is a 4x win on some stages.** The same stage run over
  every bag of a frame in one loop costs bounds 4.3-6.0 µs instead of 18-30
  µs, prepare ~17 µs instead of ~38, and dispatch 42.5 µs instead of ~50. That
  is ~0.8 ms a frame on the crate field.
- **The deferral queue cannot get there.** Its runs are about one bag long,
  because the game interleaves its own code and non-queueable bags between
  submits (docs/ps2-hardware-utilisation.md).
- **Dispatch barely moves on code reuse.** Its misses are the bag's OWN data:
  baked entries, offsets and package boxes. Only touching fewer, contiguous
  lines helps there.

So the redesign has to do two things the current API cannot. It has to see
the whole frame's draws before it processes any, and it has to read each
draw's per-frame inputs from compact, contiguous memory.

## Target shape

1. **Recording.** `render(bag, reception)` appends a fixed-size **draw record**
   to a frame list instead of submitting. The record holds everything about the
   draw that may change between this call and the flush: the model matrix
   value, the light reception, the program parameters (`setVuParams`), the
   renderer state tokens. Nothing is submitted until a flush point: the end of
   a view, a render-target switch, a PATH3 upload outside StaPip, a non-StaPip
   draw, or the end of the frame.
2. **Hot records.** Each bag keeps a small persistent **hot record**, one or
   two cache lines in one contiguous array. It holds what the per-frame stages
   read: the main box, the package-box base pointer and count, the baked-entry
   pointers, the stamp slot, route flags, the texture id. It is rebuilt only
   when the bag's revision changes (bboxVersion, contentVersion, info
   revision), not every frame.
3. **Stage-major flush.** Each stage loops over the frame list before the next
   one starts:
   - visibility and route over hot records;
   - prepare (uniforms) for the drawn records;
   - classification for the crossing ones;
   - emission.

   Each loop's code stays in the I-cache. Each loop reads contiguous records,
   which can be streamed into the scratchpad by DMA (toSPR, chained) while the
   previous chunk is processed: the one asynchronous load the EE has.
4. **Emission from records.** The packet is written from the hot record and the
   draw record, the way a stamp replay writes from its captured bytes today.
   Stamps become the general case.

## The contract change, and how it is policed

Recording changes one rule for the game. Today a bag, its info and its model
only have to be valid during the `render` call. With recording they must stay
valid and unchanged until the flush, or the change must go through the
record. The generated game has places that break this. A shared info bag
(`batchInfoBag`) is one candidate. A scratch matrix reused between submits is
another, and so is anything a pass toggles between two draws of one bag.

Phase 1 therefore starts with a **census** rather than a speed-up. In a
verify build every record also stores a hash of its bag, info and model, and
the flush re-hashes them. The game logs `RECORDVERIFY checked=.. changed=..`
with the producers whose draws changed after recording. Every such producer
is fixed (snapshot the field into the record, or give the draw its own info)
before recording is turned on for it.

## Phases

| phase | what | measured result | status |
|---|---|---|---|
| 1a | Recording behind a flag, flushed bag by bag (same order, same code), plus RECORDVERIFY | 140 000 draws recorded, 0 changed; but `endSubmissionBatch` flushed ~13 times a frame (~11 bags a run) | done |
| 1b | Batch begin/end as markers in the list; foreign VIF1/GIF work (Vif1Queue entry points, `frameSendPacket`, frame begin/flush/end) flushes the list first | runs ~48 bags (max 141), ~5 flushes a frame. **Work p50 7.72 / 7.68 -> 7.20 / 7.29 ms, missed frames 44 / 28 -> 16 / 16 of 1000, StaPip 4.51 -> 4.29 ms** (crate drive, same ELF, 2 runs) | measured; producers to fix |
| 1c | Fix every producer RECORDVERIFY names; recording on by default | three fixes (below). Census then **0 of 300 000** changed. Crate drive with them: **work p50 7.70 / 7.75 -> 7.10 / 7.29 ms, missed 26 / 27 -> 16 / 21, StaPip 4.43 / 4.51 -> 4.17 / 4.02 ms**. Motor District (the shipped example, parked): census 0 of 160 000 changed; work p50 3.86 / 3.82 -> 3.75 / 3.75 ms, **p95 11.99 / 11.93 -> 11.54 / 11.52 ms**, StaPip 2.86 / 2.90 -> 2.66 / 2.64 ms, missed 10 / 6 -> 2 / 6 | **on by default (2.11.0)** |

The fixes RECORDVERIFY found, by component (a per-component variant of the
census; it names the bag, its info, texture, vertex count and position):
1. **Frustum planes** changed under an unchanged view-projection for four
   draws every ~25 frames: the env-probe capture. The engine's camera setters
   (`RendererCore3D::update`, `setProjection`, `pushEnvView`, `pushPortalView`,
   `popEnvView`) now submit the recorded list first, through the same
   `Vif1Queue::beforeForeignWork` barrier. The record path also compares the
   planes.
2. **The sky dome's info** changed after recording. The game sets
   `zTestType = AllPass`, draws, and restores it right away, so the recorded
   draw would have gone out with the restored test. The three sky draws now
   flush before the restore.
3. Found by reading the templates, not by the census (this scene has no
   mirror or probe-lit object): `renderObjectProbe` relied on
   `endSubmissionBatch` flushing, and the mirror pass points infos at shared
   mirror matrices it rewrites per object. Both now flush before they move
   on.

4. **mirror-room**: a target's normal draw was still queued when the mirror
   pass pointed its info at the mirror matrix, so it would have gone out
   reflected (a 36-vertex box, every frame). `renderMirroredObject` now
   flushes at its start, and the sky dome also flushes before it changes the
   state, not only before it restores it.

Census after the fixes, each scene on the PS2, `--build` with the verify
switch on: crate drive 0 / 300 000, Motor District 0 / 160 000, two-players
0 / 20 000 (title screen), portals 0 / 60 000, mirror-room 0 / 60 000,
showcase 0 / 300 000. None wedged or asserted.

Rule for game code from here: an info, model or bag that is changed AND
restored around a draw needs `stapip.core.flushDeferred()` before the
change (an earlier draw of the same bag may still be queued) and before the
restore. Phase 3's draw records will carry the per-draw values, which
removes this rule.
| 2a | Bounds stage as one pass over the recorded list | StaPip 4.29 -> 4.05 ms, but the pass sits outside that counter: work p50 7.36 vs 7.25 ms with recording alone (+0.1) | no gain, off |
| 2 | Stage-major prepare / dispatch passes; ceiling ~0.8 ms crate field | - | |

The first 1b run wedged the console (`freepad: DMA Busy`). Bags recorded outside
a submission batch could still be queued when `endRecordingFrame` kicked the
frame, because StaPip's own `onFrameEnd` flush comes after it. The barrier in
`beginRecordingFrame` / `flushRecording` / `endRecordingFrame` fixed it: every
later run completed.

Why recording alone pays: the game's object loop and StaPip no longer
alternate bag by bag. The game's code runs once through, then StaPip runs its
recorded draws in long runs, so neither evicts the other between two bags.
| 3a | Stamp record + stamp pass: a stamp's resolved state cached under input epochs, consumed by one loop per flush; per-class bag revision (below, "Phase 3, first slice") | Motor District: parked work -1.7 / -1.8 ms, driving -0.43 / -0.38 ms, p95 -0.6..-2.5 ms; verify clean over ~2.9 M stamps | **on by default (2.12.0)** |
| 3b | Crossing pass: crossing bags keep a record of their package boxes and entries in their stamp slot; one pre-pass per flush classifies all their packages, the stamp pass emits them in list order (below, "Phase 3, second slice") | district crossing bag 213 -> 125 µs parked, 201 -> 136 µs driving (control inflated, see there); headline benchmark 0 missed of 1 000 in 4 runs; verify clean over 226 719 crossing draws | **on by default (2.13.0)** |
| 3 | Hot records: per-frame stages read compact persistent records, not bag/info/model/cacher/baked tables | - | |
| 4 | Scratchpad streaming of hot and draw records by DMA, double-buffered | - | |
| 5 | Emission from records (stamps as the general form) | - | |

## After phase 1: why the next phases do not pay one by one

Phase 2a is the measured lesson. A bounds pass over the recorded list cut a
crossing bag's in-path bounds stage from 19.2 to 14.3 µs, but the frame got
0.1 ms slower. The pass touches each bag's data (bag, info, model, bbox
cache), and by the time `renderNow` reaches the same bag that data has been
evicted. So the misses are paid twice, once in the pass and once in the
path. A stage-major pass only wins when the later stages no longer read the
bag's scattered data. That is phases 3 and 5 together: a compact record the
passes read, and emission from that record, not from the bag.

Where the remaining StaPip time sits with recording on (crate drive,
~4.1 ms):

| route | bags a frame | µs a bag | ms a frame |
|---|---:|---:|---:|
| stamp | ~47 | ~31 | ~1.45 |
| partial (crossing) | ~14 | ~138 | ~1.93 |
| whole replay | ~3.7 | ~85 | ~0.31 |
| direct | ~2.5 | ~150 | ~0.37 |

Packet sends are ~0.66 ms, inside the rows above. A stamp's ~0.33 sends cost
about 7 µs of its 31.

The stamp is already an "emission from a record": its captured bytes are
copied and patched. What it still pays is validation against the live bag,
info, model, texture and renderer state. The ten measured stages are each 1-5
µs, and each is mostly the first touch of another structure. So the next
lever with a clear size looked like a **lean stamp** (priced below at
~0.14 ms, not built):
- per-bag revision counters replace the field-by-field comparison;
- the world box and the light pick are read from the record;
- the copy source is streamed into the scratchpad.

Ceiling estimate: 31 -> ~10 µs a stamp, ~1 ms a frame in the crate drive,
more in the district (~100 stamps a frame). Its cost is a contract the game
must keep: every write to a stamped bag's inputs bumps the bag's revision.
RECORDVERIFY's hashing is the tool that proves it, as it proved phase 1.

### The lean stamp, priced: ~0.14 ms

A rig probe (`C:\tx\qp`, arm 1048576) skipped the producer facts (the
bag/info/stream comparison) and the global renderer facts in `tryStamp`, but
kept the baked entry's validity check. That is the most a revision-counter
contract could save. Crate drive with recording on, two runs each:
- stamps 30.3 / 29.8 µs against 33.1 / 32.7, so ~3 µs a stamp, ~0.14 ms a frame;
- work p50 6.99 / 6.70 against 7.31 / 7.42 ms.

The work figure overstates the prize. With the producer checks gone, ~1.2
bags a frame that are rejected today now replay as stamps (direct 2.5 -> 1.3
a frame). Those bags' inputs really change every frame, so they would draw
stale geometry. A correct lean stamp would recover the ~0.14 ms of validation
only. That is not worth putting a revision contract on every writer of a bag
or an info in the game. Not built.

The first version of the probe also skipped the entry's validity check. The
game never reached its first frame, so the entry check is not optional.

## Phase 3, first slice: the stamp record and the stamp pass (2.12.0)

The first slice of phase 3 covers the stamp route only. It is on by default
since 2.12.0 (`TYRA_STAPIP_STAMP_PASS`, 1). The rig rounds that measured it
are R8-A to R8-E in docs/architecture-loop.md.

Measured on a PS2 in the Motor District, same ELF, two runs:

| state | work mean | p95 |
|---|---|---|
| parked | -1.7 / -1.8 ms | -2.5 / -1.9 ms |
| driving | -0.43 / -0.38 ms | -0.6 ms |

Verify was clean over ~2.9 M stamps: words, cached answers, the epoch and
revision contracts, list order and the in-pass culls.

### The stamp record

A stamp replay used to re-ask ~25 questions every frame, and most of their
inputs had not changed. Each stamp slot now has a 64-byte **record**
(`StaPipQBufferRenderer::StampRecord`, allocated with the stamp table). It
holds the answers and the input epochs they were derived under. The answers
form three groups:

| group | answers | re-derived when |
|---|---|---|
| C camera | visibility, MVP, fog words | the camera epoch moved, or the model moved |
| T texture | residency, wrap, batch candidacy, LRU slot, CLUT, TEX0 | the texture epoch or the texture function moved |
| L light | light pick, spot block | the light epoch moved, the receive flag changed, or the model moved |

A replay re-derives only the groups whose epochs moved. The cached words
(MVP, fog, TEX0, spot) live in the stamp's own head, so the head copy carries
them.

- On a parked camera the camera words are written back into the head. On a
  moving camera they are not, because they would never be reused.
- A glob or program epoch change, or the slow light pick, sends the stamp
  down the full `tryStamp`, which refreshes the record.
- `tryStamp` keeps this record path for stamps the pass does not see
  (`replayRecord`).

### The epochs

The epochs live in `renderer/core/draw_epochs.hpp` (`DrawEpochs::g`). Each is
bumped at its writer, in the engine:

| epoch | writers |
|---|---|
| `cam` | `RendererCore3D::update` (only when the view really changed), `setProjection` (only when the projection changed), `pushEnvView` / `pushPortalView` / `popEnvView`, `narrowFrustumToRect` |
| `light` | `setSpotLight`, `disableSpotLight`, `clearDynLights`, `addDynPointLight` (and through it `addDynSpotLight`), `markDynLightBaked`, every frame start. These only mark the set dirty; StaPip compares a snapshot of the set and moves the epoch when it differs. |
| `tex` | `RendererCoreTexture` eviction, `unregisterAllocation`, `evictAll`, `updateTextureInfo`, `Texture::setWrapSettings` / `setDefaultWrapSettings`, the env map and shadow map binding their VRAM-resident textures |
| `glob` | `setFog` / `disableFog` (only when the values changed), `RendererCoreDepth` (depth scale), the resident program classes, program overrides, VU1 clipping mode, `setVuCustomEnabled`, `setStampBatchAnySize` |
| `prog` | `setProgramsCache`, `uploadPrograms`, `reinitVU1` |

**The camera contract.** A camera writer calls
`Vif1Queue::beforeForeignWork()` FIRST and bumps the epoch AFTER it. The
recorded draws are flushed under the view that is still in force, and only
then does the view change. R8-A bumped before the flush, so draws were
resolved under one view and stored under the next. Verify found that gap
3 790 times.

`popEnvView` restores the pushed view exactly, and with it the view's epoch.
An env or portal pass therefore does not void every main-view record.

### The stamp pass

At a frame-recorder flush (`StaPipCore::flushDeferredRun`), the whole recorded
list goes to `StaPipQBufferRenderer::stampPass`. It is one tight loop over the
draws in list order. For each draw it:

1. finds the slot through the bag's hint, else last frame's plan entry, else
   the hash;
2. checks the producer facts, or the revision (below);
3. handles a moved model (copies the matrix, recomputes the world box);
4. tests visibility against the per-epoch plane set. A box that is outside a
   frustum plane by a margin is culled in the pass;
5. re-resolves the texture inline if needed. This covers resident textures
   only; an upload goes general;
6. re-picks the light if needed;
7. copies the head, writes the MVP straight into the packet (the same VU0
   sequence as `M4x4::cross`), patches the fog, TEX0 and spot words, and
   appends.

A draw the pass cannot take goes down `renderNow` from inside the loop
(`PassContext::general`): no stamp, no record, a straddling box, a
non-resident texture, a changed prim key. So the GS order is the list order,
and one flush is one pass. Batch markers are handled in the loop.

A per-frame **plan** (`StampPlan`, per list position) remembers the slot,
info, model and entry found at each position last frame. The pass prefetches
draw k+1's lines from it with no dependent load.

### The revision contract

The pass can skip the companion bags (info, colour, texture bag) for a bag
the game tagged with `StaPipBag::revisionClass`. Today the generated game tags
three classes:

| class | tagged at | bumped by |
|---|---|---|
| `kClassStaticBatch` | the static-batch submit | `rebuildStaticBatch`, `buildStaticBatchList`, `procFinishChunks`, `procGenerateVolume`, `spawnPrefabAt`, `applyBakedLampsToChunk`, `loadScene`, the spot receiver pass's shared `batchSkipInfoBag` (only when its slot or `spotLit` changes) |
| `kClassTerrainChunk` | the terrain chunk submit | `buildTerrainChunk`, `applyBakedLampsToChunk`, `loadScene` |
| `kClassRoadChunk` | the procedural chunk submit | `procFinishChunks`, `procInstGeoRange`, `procGenerateVolume`, `spawnPrefabAt`, `applyBakedLampsToChunk`, `loadScene` |

A whole writer function takes a `DrawEpochs::ClassWriter`. It bumps on entry
and again on exit, so a flush inside the function cannot verify a record
against half-written companions and keep it afterwards.

While the class epoch is the one the facts were last verified under, the
pass checks only these:
- the bag-line facts: vertices, count, `bboxVersion`, the info, colour and
  texture bag pointers, strip and billboard;
- the content stamps, through pointers cached at the last full check.

The model value is always compared live. **Untagged bags (class 0) keep the
full field-by-field facts**, so a producer that is not tagged can never be
wrong.

### The rule for new code

- **A new renderer-state writer** that a stamp's replay reads must bump its
  epoch at the write. This covers the camera, lights, texture residency or
  wrap, fog, depth, programs, overrides and anything `tryStamp` checks. A
  camera writer bumps after `Vif1Queue::beforeForeignWork()`.
- **A new game writer** of a tagged bag's info, colour or texture bag (or a
  new submit site that tags a bag) bumps that bag's class. Use
  `DrawEpochs::bumpClasses(mask)` at the write, or a
  `DrawEpochs::ClassWriter` for the whole function.
- **Prove it with `TYRA_STAPIP_STAMP_VERIFY` 1** in
  `stapip_qbuffer_renderer.hpp`. Nothing is then sent from a record: every
  draw is predicted dry and sent down the full path, and `STAMPVERIFY` lines
  in the log count each contract. Every `bug`, `bad`, `order`, `once` and
  `rev ... bug` counter must stay 0. A missing epoch bump shows as a
  `contract` bug; a missing class bump shows as `stage=rev`.
- `TYRA_STAPIP_STAMP_PASS` 0 is the kill switch: `tryStamp` and `renderNow`
  as in 2.11.

What is left after this slice, from R8-E: a pass stamp costs ~18 Âµs parked,
and its misses are spread evenly over the record, the head, the packet and
the bookkeeping. Crossing bags (partial and strip, ~2.5 ms in the district)
are now the largest StaPip cost, not stamps. The second slice (below) takes
the partial ones.

## Phase 3, second slice: the crossing pass (2.13.0)

The second slice covers crossing bags: bags whose box straddles the view,
sent by the lean partial replay (`classifyRoutes` + `emitPartialRuns`). It
is on by default since 2.13.0 (`TYRA_STAPIP_CROSSING_PASS`, 1). The rig
round that measured it is R11-A in docs/architecture-loop.md. The code is in
`stapip_crossing.hpp` (the record, the per-flush arena, verify) and in
`StaPipCore::crossClassify` / `crossCapture` and
`StaPipQBufferRenderer::crossEmit` / `crossSynth`.

Before, a crossing bag cost ~180-210 µs. Every frame it re-derived its planes,
read its package boxes cold from the bounding-box cacher, ran the baked-entry
lookup and rebuilt its uniform head. There were 14-22 of them a frame, and
they were the last missed frame of the headline benchmark.

### The crossing record

After a successful lean partial replay, the bag's stamp slot gets a
**crossing record** (`CrossPass::XRec`, one per slot, 64-byte aligned). It
holds:
- the merged package boxes and the coarse (8-package) boxes, in one SoA block
  (`pmin[n]`, `pmax[n]`, `cmin[g]`, `cmax[g]`), merged exactly as
  `classifyRoutes` merges them;
- the main box (object space);
- the cull entry, which must be the stamp's own entry (pointer and serial),
  and the whole-clip entry (pointer, serial, arena);
- where the clip block's REF goes in the stamp head: before the single
  colour, else before the options;
- the facts it was built under: vertices, count, `bboxVersion`, package size,
  the `glob` and `prog` epochs, band (tight / loose) and `fullClipChecks`.

The record is cleared when its cull or clip entry is destroyed
(`sweepStamps`), and with the whole table when the stamp table is reset.

**Crossing-only slots.** A bag that has never been wholly visible never
earns a real stamp: a chunk under the car, or everything straddling a parked
view. Its record would have nowhere to live. `crossSynth` builds a slot for
it from this draw's crossing head (`crossSnap`, taken just before
`emitPartialRuns`), the way `captureStamp` builds one from a whole replay:
- the clip REF is taken out and the whole-arena REF appended;
- the offsets behind the clip REF move back by one;
- the record gets this draw's T and L answers, and never the camera group.

`CrossPass::xonly` marks the slot. **A crossing-only slot is never a
stamp:** `tryStamp` rejects it, the stamp pass takes it only as a crossing
draw (otherwise it goes general, as if it had no stamp), and a real
`captureStamp` clears the mark. New code that reads stamp slots must keep
this fence.

**Only a bag that wants REPEAT gets a crossing-only slot (2.13.1).**
`crossEmit` emits only with REPEAT in force and never writes a wrap block.
A CLAMP bag captured right after another bag with the same CLAMP has no
block in its head. In 2.13.0 such a bag could get a slot and later be sent
under REPEAT with the wrong wrap. It needs two consecutive crossing bags
with the same non-REPEAT wrap, so the verify scenes never produced one; it
was found by reading the code. `crossSynth` now refuses that bag, and it
stays on the general path.

### The pre-pass

At each recorded-list flush, before the stamp pass, `crossClassify` runs
when the partial replay is in mode 4 and VU1 clips whole bags:
1. **Phase 1**, one walk over the list. For each draw whose slot holds a
   valid record (bag-line facts and epochs equal), it derives the planes as
   `renderNow` does: object-space frustum planes from the live model, the
   main-box test, the VU1 clip planes from `viewProj * model`, the
   guard-band test. One plane set serves consecutive draws with the same
   model value and band. The verdict per list position is crossing, wholly
   in, wholly out, guard-band only, stale, or full.
2. **Phase 2**, one loop over the packages of every crossing bag of the
   flush. It runs `classifyRoutes`'s arithmetic verbatim over the record's
   boxes and writes routes and masks into a per-flush arena (16 384
   packages, 256 crossing bags, 64 plane sets).

### Emission in list order

The stamp pass does the rest. A straddling stamp (vis 2) that the pre-pass
classified as crossing, for the same bag, slot and model value, keeps going
through every stamp check: the facts or the class revision, the model, the
epochs, and the T and L answers. `crossEmit` then sends it:
- the stamp head, with the camera group always re-derived (a crossing draw
  never claims `r.cam`);
- the clip block's REF at the record's insert point;
- `emitPartialRuns` over the record's cull and clip entries.

It refreshes the T and L halves of the stamp's record and keeps both entries
alive. A draw it cannot take goes general from its list position, as in
2.12.0, so the GS order is still the list order. Among these: a stale record,
a changed band, a wrap block, a wrap in force that is not REPEAT, or runs
that do not fit the packet.

### Verify

`TYRA_STAPIP_CROSSING_VERIFY` 1 (in `stapip_qbuffer_renderer.hpp`, default
0) sends nothing from a crossing record. `crossEmit` builds each crossing
draw's words dry, and the draw goes down the general path, which compares:
- the classification (routes and masks of every package, `clsBad`);
- the pre-pass verdicts: a draw called wholly in, wholly out or guard-band
  only must never reach the lean partial replay (`verdictBad`);
- the head and runs, walking the stream as the DMAC does. Tags are exact
  except the DMA tag PAD bits (`0x03FF0000` of word 0: `packet2` never
  writes them and the DMAC ignores them), CNT payloads are exact, REFs are
  exact (`headBad`, `runsBad`, `lenBad`; PAD-only differences are counted
  in `padOnly`, not as bugs);
- the list order and exactly-once (`order`, `once`, `noCmp`).

`CROSSVERIFY` lines in the log report every 20 000 predictions. Clean is
`cmp == preds` with every bad counter, `noCmp`, `once` and `order` at 0. The
first 16 bugs print a `CROSSVERIFY BUG` detail line. Verify needs the
crossing pass, and it cannot be combined with `TYRA_STAPIP_STAMP_VERIFY`:
the stamp pass is dry there, so no crossing draw is ever sent. Prove the
stamps first, then the crossing pass.

`TYRA_STAPIP_CROSSING_PASS` 0 is the kill switch: no records, no
crossing-only slots, crossing bags down `renderNow` as in 2.12.

### Measured (PS2, the R11-A rig)

Verify was clean over 226 719 compared crossing draws (district 158 180,
benchmark 68 539): heads, runs, lengths, classes, verdicts, order and once
all 0.

| | control | crossing pass |
|---|---:|---:|
| district, parked: crossing bag | 213 µs | **125 µs** |
| district, parked: work mean / p95 | 11.02 / 12.82 ms | 10.31 / 11.57 ms |
| district, driving: crossing bag | 201 µs | **136 µs** |
| district, driving: work mean / p95 | 10.98 / 13.15 ms | 10.00 / 12.04 ms |
| headline benchmark: missed of 1 000 | 3 / 3 / 3 / 4 | **0 / 0 / 0 / 0** |
| headline benchmark: work p50 | 6.49-6.60 ms | 5.53-5.83 ms |

**The A/B overstates the gain.** The rig's patch put its hook between
`if (lean) classifyRoutes(...)` and its `else`, so the control arm also ran
`packager.create` for every lean crossing bag. The control is therefore
slower than shipped 2.12. Against 2.12.1's own figures (R10-A: ~190 µs a
crossing bag; R9-B: benchmark p50 5.89-6.04 ms, 1 missed frame a run), the
pass saves ~55-65 µs a crossing bag, about three quarters of what the A/B
shows. Scaled the same way, the district gain is an estimated 0.5-0.75 ms a
frame, not the measured 0.7-1.0 ms. The pass arm itself is unaffected: 0 missed
frames in all 4 benchmark runs. The port leaves that `if / else` intact.

## Phase 3: the hot record (layout draft)

What `renderNow` reads from the bag side, by number of reads in
`stapip_core.cpp`:
- texture 39, count 32, lighting 20, info->model 17, colour 17, billboard 15,
  vertices 14;
- info->fullClipChecks 13, stripped 12, bboxVersion 10;
- info->transformationType 9, packageSize 5, info->frustumCulling 5,
  info->tightClipBand 4, immutableStreams 3;
- the light-pick fields: info->spotLit, dynamicLightReceive, dynLightPick,
  dynLightSkipSlot, dynLightSkipBaked, torchPerPixel, fogDisabled.

On top of that come the per-draw lookups that today chase pointers on every
draw:
- the bbox cache bucket and entry;
- the program per bag (`getCullProgramByBag`, `getMaxVertCount`);
- the texture object and its wrap settings, and the resident-texture hint;
- the baked-stream table and entry, and the stamp table and entry.

A hot record is 64 or 128 bytes (one or two lines), kept in one array in
bag-registration order:

| bytes | field | rebuilt when |
|---:|---|---|
| 32 | main box lo/hi, object space | bboxVersion changes |
| 4 | package-box base pointer (the cacher's part array) | bboxVersion / count changes |
| 2+2 | count, package size (maxVertCount resolved) | count / packageSize / program set changes |
| 4 | route flags: precise cull, full clip checks, tight band, stripped, billboard, lit, textured, env, fog off, light-pick flags | info revision |
| 4 | baked cull entry pointer | bake / rebake |
| 4 | baked clip entry pointer | bake / rebake |
| 4 | stamp slot | stamp capture / erase |
| 4 | texture id + wrap class | texture bind / wrap change |
| 4 | program (cull class) pointer | program set change |
| 4 | the bag's revision stamp the record was built from | - |

The revision stamp needs an "info revision" counter that the game bumps when
it edits an info bag after creation. RECORDVERIFY (phase 1) is the tool that
proves the bump is not missed: today's census shows no draw whose bag, info,
model, colour, texture or lighting changes between record and submit, which
is the per-frame half of the same contract.

What phase 3 is expected to take out:
- The bounds stage stops touching the bag, info, cacher and program
  (measured in-stage today at 4.3-6 µs with those reads).
- Prepare's light pick and texture facts read from the record.

The dispatch stage's misses are the baked entry's offsets and package boxes.
They stay, because they are per-package data. Phase 4 streams them, and the
records, into the scratchpad by DMA instead.

## Measured dead ends this design avoids

- Ordering bags by route.
- Prefetching the next stamp's whole entry.
- `-Os`.
- Code-layout ordering.
- A deferral queue that can only hold stamps and immutable bags.

The measurements are in docs/stapip-bag-cost.md and
docs/ps2-hardware-utilisation.md.

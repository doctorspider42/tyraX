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

A rig probe (`C:	x\qp`, arm 1048576) skipped the producer facts (the
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

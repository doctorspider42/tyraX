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
| 1a | Recording behind a flag, flushed bag by bag (same order, same code), plus RECORDVERIFY. No speed-up expected: it proves the contract | - | next |
| 1b | Fix every producer RECORDVERIFY names; recording on by default | - | |
| 2 | Stage-major flush of the frame list (bounds, prepare, dispatch as passes); ceiling ~0.8 ms crate field | - | |
| 3 | Hot records: per-frame stages read compact persistent records, not bag/info/model/cacher/baked tables | - | |
| 4 | Scratchpad streaming of hot and draw records by DMA, double-buffered | - | |
| 5 | Emission from records (stamps as the general form) | - | |

## Measured dead ends this design avoids

- Ordering bags by route.
- Prefetching the next stamp's whole entry.
- `-Os`.
- Code-layout ordering.
- A deferral queue that can only hold stamps and immutable bags.

The measurements are in docs/stapip-bag-cost.md and
docs/ps2-hardware-utilisation.md.

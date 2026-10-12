# troubleshoot/tyra-engine-dev-baked.md

The baked VIF stream (`TYRA_STAPIP_BAKED_STREAM`, default 1 since 1.117.1), its
content-version contract and acceptance gate, plus the stationary-night chase-view
repricing. Symptom first.

## The baked VIF stream (default 1 since 1.117.1)

**Cause:** a wholly visible static bag's whole per-frame VIF1 command stream emitted
once and replayed by ONE DMA `REF` tag. Format, arithmetic and what verified:
docs/baked-vif-stream.md. It ships **on**; 0 is the control arm.
**Fix — five things any edit must keep:**
- **A `REF` payload may contain NO DMA tags.** The DMAC does not interpret tags inside
  referenced data — it feeds every word of it to VIF1 as a VIFcode — so the baked block
  is a pure VIFcode stream: `STCYCL` + `UNPACK` followed **inline** by the data that
  unpack transfers, repeated, then `FLUSH` + `MSCAL`/`MSCNT`. That is not new for VIF1:
  with TTE a chain tag is 8 bytes of DMA tag plus 8 bytes of VIFcodes, so VIF1 already
  receives an unbroken WORD stream in which codes and data alternate with no relation to
  quadword boundaries — `packet2_utils_vu_open_unpack` + `packet2_add_float` (the
  `submissionBatchCandidate` branch of `sendObjectData`) is the shipping existence proof.
- **The block is TRANSCODED, never re-derived.** `bakeBlock` walks the chain fragment the
  ordinary writers just produced and turns each tag into ONE header quadword carrying
  that tag's own two VIFcodes **verbatim**, preceded by two VIF `NOP`s, followed by the
  quadwords the tag transferred. The NOPs go in FRONT (an `UNPACK` eats the words that
  immediately follow it, so trailing padding would be read as unpack data) and they keep
  every payload quadword aligned. Anything that is not `CNT` or `REF` refuses the bake.
- **THE PROGRAM KICK IS A BAKE-TIME FACT, and that is what makes one `REF` per MESH
  possible.** `addBuffersDataToPacket` picks `MSCAL` or `MSCNT` off `lastProgramName` —
  but `StaPipCore::render` calls `clearLastProgramName()` **once per bag**, so package 0
  always produced `MSCAL <program address>` and every later package `MSCNT`. The kick
  therefore lives inside the block and the program's micro-memory destination joins the
  key. If you ever stop clearing that per bag, this breaks silently.
- **A baked block IS live DMA memory**, unlike a retained one. It is written once, when
  built, and only read afterwards — never patched in place — and eviction may not free
  what the last submitted packet still names, so every free goes through a two-frame
  graveyard. The arena is hand-aligned to 16 bytes: a DMA tag's address field drops its
  low four bits, so a misaligned arena transfers from somewhere else, silently.
- **Only the DIRECT, wholly-visible cull route bakes**, and the run a single `REF` covers
  stops at the 16-group packet flush boundary (the direct route is the one whose packages
  are a fixed slice of the bag; changing the flush cadence would move `packetFlushes`,
  which the acceptance gate pins). Lifting that limit needs a gate not built on counters:
  docs/baked-stream-acceptance-gate.md. `StaPipQBuffer::bakeIndex` is reset by
  `getBuffer()` for the same reason `retainIndex` is — a half flush covers buffers of
  SEVERAL bags, so a stale index would be read against another bag's arena.

## The baked stream's measured win

**Cause:** it is worth **1.287 ms of garage-day `work` on hardware, against a 0.012 ms
floor** — a fifth of the 5.5-6.5 ms the plan budgeted, measured with NO instrument in
either arm. `total_ms` does not move in garage day at all (the saving goes into
`present`, because the vsync rung is 10.42 ms away) but garage night stops juddering
(43.9 ms averaged over a two/three-field alternation becomes a flat 39.959). Only half
the bags take the direct route (`dsDirectBags` 53.5 against `dsPartialBags` 59), and
`prepare` RISES 0.315 ms.
**Fix:** **THE EE PAYS PER PACKAGE**: packet construction is 1.898 ms over 803.5 packages
= **2.362 us a package**; replaying 360 of them predicts 0.850 ms and 0.808 was measured.
That is why badly packed geometry costs out of proportion — projected shadows and wheels
run 22-25 triangles a VU1 package against a strip's ~70, taking 16% of the frame's
packages for 7.6% of its triangles.

## The old key did not cover what the block contains; `BagArray` is the fix

**Cause:** the key held each array's POINTER plus `bboxVersion`, and `bboxVersion` is a
statement about the bounding box, i.e. about POSITIONS. A caller that re-shades
per-vertex COLOURS in place — which the Motor District does for its dynamic lights —
changes what the block must contain without touching anything the key can see, and the
baked stream then replays stale lighting. Measured: `TYRA_STAPIP_BAKED_VERIFY`
mismatches 1438 times, all of them the colour-only class, all with the first differing
quadword on the first COLOUR quadword. The RETAINED cache is immune because it stores
the chain and its `REF`s still name the bag's own arrays; the exposure belongs to the
baked stream BECAUSE it inlines the payload.
**Fix:** the generated game owns it structurally with `BagArray<T>`, whose `data()` is
const; generated mutable arrays now use it, its enforced mutation API stamps
`contentVersion` (`tools/bag-array-enforcement.sh`), and the baked key includes that stamp. The moving-route verifier checked
169 843 blocks with zero failures. See docs/bag-content-version.md and
docs/baked-stream-acceptance-gate.md.

## The two adversarial modes are how you find that class at all

**Cause:** they are not optional before turning this on.
**Fix:** `TYRA_STAPIP_BAKED_VERIFY` never replays — it rebuilds every block with the
ordinary writers and compares — so it needs no control arm and runs under `--keep-routes`
with the traffic MOVING. `TYRA_STAPIP_BAKED_POISON` overwrites an evicted arena
immediately instead of letting the two-frame graveyard hide it; paired with
`TYRA_STAPIP_BAKED_BUDGET_QW` cut far below what the scene wants, it thrashes eviction
and proves the DMA lifetime. Poison PASSES at 16 384 quadwords against 1 541 KB wanted,
with a byte-identical picture.

## The cost the plan page did not mention

**Cause:** inlining the payload stores every static vertex twice, ~49 bytes per vertex
for the textured per-vertex-colour class, and **nothing can free the originals** — the
bbox cacher, the clip route and the generated game all still read them.
**Fix:** `STAPIPBAKE` (behind `TYRA_STAPIP_BAKED_REPORT`, default 0) prints the arena
size and the per-frame DMA chain quadword count; that last counter is compiled into BOTH
arms, which is what makes the A/B readable. The shipping flip was measured on a physical
PAL PS2 on 2026-09-20, after the content-version contract existed: in an 83-road-chunk
carless district, three synchronized captures per arm measured 18.776 -> 17.012 ms
median total and 8.856 -> 7.769 ms procedural; EE memory rose 14.3 -> 15.7 MB, and
187 904 non-HUD capture pixels were identical.

## Current chase view versus the older TyraX2 overlap estimate

**Cause:** the older GPU-hold sweep's 2.5–3.5 ms overlap estimate does not bound the
stationary night entry chase view: its camera doubles StaPip calls and submits 46 096
primitives including strip joins.
**Fix:** reprice GPU-hold/segments in that view before promising a frame-pipeline gain.
Dynamic scene-light and raster suppression probes identify material costs without
isolating exact VU1/GS utilization. See `docs/ee-submission-rearchitecture.md`,
"Stationary night vehicle entry: what TyraX2 would and would not solve". The generated
vehicle HUD font prewarm uses existing normal evictable texture residency; it changes no
engine allocator or texture pinning rule.

# A baked VIF stream per mesh: the format, and what it costs

[ee-submission-rearchitecture.md](ee-submission-rearchitecture.md) names one
change as the thing that would stop the EE paying 147 cycles per triangle: emit
a wholly visible static mesh's whole per-frame VIF1 command stream **once** and
replay it with **one DMA `REF` tag**. This page is the spike that answers the
two questions that had to be answered before that change is worth building —
**what exactly is in the block**, and **what does it cost in EE RAM** — plus the
things the plan page got wrong.

Nothing here is a millisecond. The spike produces pixels and counts; the plan's
timing claims belong to a later console round and no PCSX2 number may stand in
for them.

**Switch:** `TYRA_STAPIP_BAKED_STREAM` in `stapip_qbuffer_renderer.hpp`,
**default 0**, in the style of `TYRA_STAPIP_RETAINED_COMMANDS`. 1 is the
candidate arm. `TYRA_STAPIP_BAKED_REPORT` (default 0) adds the periodic
`STAPIPBAKE` readout, which also prints the per-frame DMA chain quadword count
and is therefore built into **both** arms when a measurement is being taken.

## The format works, and the reason is that VIF1 has no idea what a DMA tag is

The design question was whether a block transferred by a `REF` tag may contain
the unpacks it needs. It may not contain **DMA tags** — the DMAC does not
interpret tags inside referenced data, it feeds every word of it to VIF1 — so
the block has to be a **pure VIFcode stream**: `UNPACK` followed inline by the
data that unpack transfers, repeated, then the program kick.

That is not a new thing to ask of VIF1, and the proof is already in the
pipeline. With `packet2`'s tag-transfer enable (every StaPip packet is
`packet2_create(..., P2_MODE_CHAIN, true)`) a chain tag occupies **8 bytes of
DMA tag plus 8 bytes of VIFcodes**, and the DMAC hands VIF1 only the second
half. So what VIF1 already receives is an unbroken **word stream** in which
VIFcodes and their data alternate with no relationship to quadword boundaries
or to where a tag was. `packet2_utils_vu_open_unpack` + `packet2_add_float` —
the shape the `submissionBatchCandidate` branch of
`StaPipQBufferRenderer::sendObjectData` uses for the MVP, the light quads and
the single colour — is exactly "UNPACK with its data inline", and it has been
shipping on hardware since 1.93. Moving those same words from a tag's upper half
into a referenced payload changes nothing VIF1 can observe.

### One package's block, quadword by quadword

Take the simplest colour class, `cull_tc` (textured, per-vertex colours, three
streams), 75 vertices. `sw[0]`..`sw[3]` are the quadword's four 32-bit words in
the order VIF1 consumes them.

| qw | contents |
| ---: | --- |
| 0 | `NOP`, `NOP`, `STCYCL cl=1 wl=1`, `UNPACK V4_32 num=2 dest=0 usetop` |
| 1 | `2048.0, 2048.0, RendererCoreDepth::scale, vertexCount` |
| 2 | the prim GIFtag (`NLOOP` = vertex count, reglist, prim type/shading/…) |
| 3 | `NOP`, `NOP`, `STCYCL`, `UNPACK V4_32 num=75 dest=VERT_DATA usetop` |
| 4..78 | the 75 position quadwords, **inline** |
| 79 | `NOP`, `NOP`, `STCYCL`, `UNPACK V4_32 num=75 dest=VERT_DATA+75 usetop` |
| 80..154 | the 75 ST quadwords, inline |
| 155 | `NOP`, `NOP`, `STCYCL`, `UNPACK V4_32 num=75 dest=VERT_DATA+150 usetop` |
| 156..230 | the 75 colour quadwords, inline |
| 231 | `NOP`, `NOP`, `FLUSH`, `MSCAL <program>` — or `MSCNT` |

**232 quadwords, 3 712 bytes**, replayed by one `REF` tag whose own two VIFcode
slots are `NOP`, `NOP`.

Three details of that layout are load bearing:

- **The two `NOP`s come FIRST, not last.** An `UNPACK` consumes the words that
  immediately follow it, so padding written *after* the pair would be eaten as
  unpack data. In front, the padding costs one word pair and keeps every
  payload quadword aligned — which is what makes the block a `memcpy` of the
  arrays rather than a shift of every vertex by eight bytes.
- **The block is TRANSCODED out of the chain the ordinary writers produced**,
  the way `StaPipRetainedCommands` captures rather than re-derives
  ([retained-static-commands.md](retained-static-commands.md)). Each tag becomes
  one header quadword carrying **that tag's own two VIFcodes, copied verbatim**,
  followed by the quadwords the tag transferred: the ones inline behind a `CNT`,
  the ones at the named address behind a `REF`. Anything that is not `CNT` or
  `REF` refuses the bake. So there is still no second description of the packet
  format, and a future edit to a program's `addProgramQBufferDataToPacket`
  cannot be silently missed here.
- **No `NEXT` and no `CALL`**, exactly as
  [retained-static-commands.md](retained-static-commands.md) requires. A `REF`
  to a tag-free payload is the whole point: control returns to the frame chain
  by itself and the DMAC's two-deep ASR stack is never involved.

### The per-class table

`blockQw = 3 + streams x (1 + n) + 1`, with `n` the package's vertex count.

| program class | streams | qw at n=75 | bytes | bytes per vertex |
| --- | ---: | ---: | ---: | ---: |
| `cull_tc` (texture + per-vertex colour) | 3 | 232 | 3 712 | 49.5 |
| `cull_tce` (matcap; normals ride the ST slot) | 3 | 232 | 3 712 | 49.5 |
| `cull_c` (colour) | 2 | 156 | 2 496 | 33.3 |
| `cull_tc`, single colour | 2 | 156 | 2 496 | 33.3 |
| `cull_td` (texture + directional lights) | 4 | 308 | 4 928 | 65.7 |

`cull_td` is in the table for completeness only: the Motor District does not
reach it. A generated game attaches a lighting bag only to dynamically lit
objects, so ordinary static geometry has `lighting == nullptr` and selects the
COLOUR programs — measured, not assumed
([vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md), where a probe aimed at
`cull_td` moved the frame by 0.000 ms).

## Two bake-time facts the plan page called "details to design"

The plan lists the GIFtag and `RendererCoreDepth::scale` as the two things to
resolve. Both resolve, and a third one it does not mention resolves too.

- **The GIFtag is entirely bake-time.** `NLOOP` is the package's vertex count,
  which is a fixed slice of the bag; the prim type, shading, mapping, fogging,
  blending, AA, mapping type and colour fix all come off `prim_t`, and they are
  already part of the retained cache's key (`primKey`), so a bag that changes
  any of them rebuilds. Nothing here needs patching.
- **`RendererCoreDepth::scale` is bake-time per display mode.** It sits in
  `sw[2]` of quadword 1 and a 16-bit framebuffer moves it. At run time the key
  carries its bit pattern and a display-mode switch simply rebuilds. For an
  **editor-side** bake it is one 32-bit word at a known offset in each block, so
  it is patched at load, not baked twice.
- **The program kick is bake-time as well, and that is the finding that makes
  one `REF` per mesh possible at all.** `addBuffersDataToPacket` chooses `MSCAL`
  or `MSCNT` from `lastProgramName`, which looks like a property of the frame —
  but `StaPipCore::render` calls `clearLastProgramName()` **once per bag**, so
  package 0 of every bag always produced `MSCAL <program address>` and every
  later package always produced `MSCNT`. The kick therefore goes inside the
  block, and the program's micro-memory destination address joins the key
  (the retained cache does not need it, because its blocks stop short of the
  kick).

## What one `REF` actually covers, and the one thing that stops it being a mesh

The blocks of one bag are laid out **contiguously in package order**, so a run
of consecutive packages is a contiguous range of the arena and one `REF` tag
covers all of it — unpacks, inline vertex data and every package's `MSCAL` /
`MSCNT`.

The run stops at a **packet flush boundary**. The pipeline flushes every 16
qbuffer groups, and a mesh whose packages straddle that boundary is submitted
as two runs in two packets. That is not a defect to fix here: `packetFlushes` is
one of the counters the acceptance gate pins, so a spike that changed the flush
cadence could not be checked against the control at all. A mesh that fits inside
one flush is **one tag**; a mesh that does not is two. `MSCNT` at the head of the
second packet is exactly what the current code emits there too, so the split
costs a tag and nothing else.

## Chain quadwords: the number the later console round will price

Per package, the DMA chain the EE writes:

| | today | baked |
| --- | ---: | ---: |
| scale/count + GIFtag (`CNT` + 2 data) | 3 | — |
| one `REF` per vertex stream | 3 | — |
| `FLUSH` + `MSCAL`/`MSCNT` (`CNT`, qwc 0) | 1 | — |
| one `REF` per RUN of packages | — | 1 (amortised) |
| **per package** | **7** | **1 / packages-in-the-run** |

What VIF1 receives is almost unchanged: today 4 x 8 bytes of tag-borne VIFcodes
plus 227 payload quadwords, baked 8 bytes plus 232 quadwords — **40 bytes more
per package**, because the `NOP` padding is real transfer. The saving is not
bus traffic, it is the chain the EE builds and the DMAC walks.

MEASURED, Motor District garage day, PCSX2, frozen parked pose, `STAPIPBAKE`'s
`chainQw` (the sum of `packet2_get_qw_count` over every packet the static
pipeline submits in a frame):

| garage day, held pose | chain quadwords per frame |
| --- | ---: |
| control, `TYRA_STAPIP_BAKED_STREAM` 0 | **9 146** |
| candidate, switch 1 | **6 705** |
| | **-2 441, -26.7 %** |

Both arms read the same number in four consecutive 300-frame windows, which is
what the frozen pose is for. 9 146 is about what the arithmetic predicts for the
control: 1 050 drawn packages at 7 quadwords, 120 end tags, and roughly a
hundred bags of uniform chain.

The candidate emits **59 `REF` tags a frame** where the control wrote those
packages one at a time, so one tag carries **6.4 packages** on average and the
saving works out at roughly **360 of the 775 cull-route packages replayed**.
Not all of them: see the churn below.

Raw windows, both arms, and everything else this page measures:
[`examples/vehicle-playground/authoring/baked-vif-stream-2026-09-16`](../examples/vehicle-playground/authoring/baked-vif-stream-2026-09-16/README.md).

## The memory, which is the cost this design has and the retained one does not

Inlining the payload stores every vertex **twice**: once in the bag's arrays,
which nothing can free, and once in the baked block.

Nothing can free the originals, and this is not an implementation gap. The
positions feed `StapipBagBBoxesCacher`, which recomputes per-package AABBs
whenever `bboxVersion` moves, and they feed the clip route whenever the same
mesh stops being wholly visible — the near plane and the guard band are
properties of the *camera*, so any bag can need them in the next frame. The
generated game reads them too (the shading bake, `renderAtFloor`'s y-clamp copy,
the flashlight receiver passes, the portal clipper). An editor-side bake changes
none of that.

So the rule is simple: **a baked static vertex costs about as much again as it
already cost.**

| | bytes per vertex |
| --- | ---: |
| the bag's arrays today, `cull_tc` (position + ST + colour) | 48.0 |
| its baked block | 49.5 |
| the retained command block, for comparison | 1.3 |

MEASURED on the same pose: the arena holds **1 431 KB**, for the part of the
garage-day cull route that the direct branch reaches. That is a fraction of the
scene - roughly half the submitted bags take the wholly-visible route at all
(`render-submission-attribution.md`, "Round two"), and this spike bakes only
those - so read 1.4 MB as a LOWER bound on what a whole-scene bake would hold,
not as the answer.

For scale, the whole Motor District's static geometry as the scene-load lines
report it: `ROADSTRIP scene 0 strips 1 packages 470 triangles 31050` — 470
packages of road alone, 1.74 MB baked — plus 25 terrain chunks at 8 packages
each (0.74 MB) and the district's models at 9 648 strip vertices. A PS2 has
32 MB, so it fits; it is a **three-to-four-megabyte line item** on a console
where the garage already runs its texture heap at 84% occupancy
([gs-vram.md](gs-vram.md)), and it is the reason the switch ships at 0.

The run-time cache is therefore a hard byte budget with bounded LRU eviction and
the same 250-frame expiry the retained cache and the bbox cacher use:
`StaPipBakedStreams::kMaxQwords` is 262 144 quadwords, **4 MB**.

### The lifetime rule the retained design did not need

`retained-static-commands.md` can say "the retained storage is EE-private and is
never referenced by DMA", and that is why it adds no DMA-lifetime exposure at
all. **A baked block is the opposite**: a `REF` tag names it, so it is live DMA
memory for as long as the packet that names it can still be read.

Two consequences, both implemented:

- A block is written exactly once, when it is built, and only read afterwards.
  There is no in-place patching of a live block — which is *why* the `MSCAL` /
  `MSCNT` finding above matters: patching one word per frame would have put a
  write into a window the previous frame's DMA can still be reading.
- **Eviction never frees.** An evicted arena goes into a two-frame graveyard.
  One frame boundary already proves the transfer finished — the pipeline waits
  for VIF1 before every one of its ~120 submissions a frame — but that count is
  a property of the scene, not of this class, so the second frame is the margin.

## Scope of the spike

Only the **direct, wholly-visible cull route**: the branch in
`StaPipCore::render` commented "The whole-bag bbox already proved every range
visible". That is the one route whose packages are a fixed slice of the bag.
Everything else falls through to the path it took before, byte for byte:

- partially visible bags (per-package classification, guard-band and clip
  routes), because a clip buffer's count word carries a camera-dependent plane
  mask and a copied or strip-expanded buffer points into the double-buffered
  slot pool, whose address is not a property of the bag;
- billboard bags, whose centres are expanded on VU1 from a per-frame camera
  basis and which swap the whole program set;
- any bag at all while a game-supplied program override is installed — a
  replacement writer has no packet ABI and is not required to be
  position-independent.

`stapip_bag_packager.cpp` is untouched.

## Verification

PCSX2 software renderer, `examples/vehicle-playground` through
`authoring/benchmark-district.py` (parked traffic, frozen four-pose camera),
**one project directory and one knob** - `TYRA_STAPIP_BAKED_STREAM` 0 against 1,
with `TYRA_FRAME_PROFILE` and `TYRA_STAPIP_BAKED_REPORT` at 1 in both arms. The
fixture was regenerated by the editor that built it (an example's committed
generated sources are routinely stale, and a stale fixture measures a different
game). Garage day held through `bin/district-benchmark-pose.txt`; the night
poses have authored lamp flicker and twinkling stars and never settle, so
nothing is read from them.

Raw output for everything below, both arms, is archived in
[`examples/vehicle-playground/authoring/baked-vif-stream-2026-09-16`](../examples/vehicle-playground/authoring/baked-vif-stream-2026-09-16/README.md),
with the harness that produced it and a host program that re-derives the
block-size table.

**Two builds, two ELFs.** The build log carries
`Engine sources changed - rebuilding libtyra...` and its `ar rcs bin/libtyra.a`
line in both arms, and the two ELFs hash differently:

```
ctrl  3b27a5faad043a0c59305d68679e61656e8527dc8c121ce97992086a7322c1c5
cand  23a7b09e1c7b535f379b0f0033d1fe521ed5f0620cd37b925757f2fd3610dad3
```

**And the fixture is the scene the rest of the branch measured**, which is a
SEPARATE check and the one this round initially failed. The scene-load producer
lines read `ROADSTRIP scene 0 strips 1 packages 470 triangles 31050` and
`TERRAINSTRIP scene 0 chunk 2,2 strips 1 vertices 588 packages 8 triangles 512`
in both arms, which is what
[the package-ceiling round](../examples/vehicle-playground/authoring/package-ceiling-75-2026-09-16/README.md)
recorded. Why that sentence is here at all is the last subsection below.

**The picture is identical, which is the result that matters.** Six
`--capture-frame` images - three per arm - hash to ONE value,
`415f970fe840f3880c48f4bab7119d8a6cc1d55aacdf533d23522dac61273f1e`. The VIF1
word stream really is the same stream.

**No counter moved.** `FTCLIP`, 50-frame windows, identical in both arms across
every window of the held pose:

```
cull=38750/2031525 clip=1825/16525 guard=11925/605925 out=46125
flush=6000 strip=24250 sexp=100 verts=55836
```

i.e. 775 cull packages, 36.5 clip, 238.5 guard-band, 120 packet flushes and
55 836 submitted vertices per frame, both ways - and, line for line, the
garage-day row the package-ceiling round recorded for this scene.

### The fixture check a matching capture hash does NOT give you

Worth a paragraph, because the first pass of this A/B got it wrong in a way that
looked right. It was run with the editor binary that happened to be sitting in
another checkout's `build/`, and its `FTCLIP` read `cull=40175 ... verts=55332`
against the 38 750 / 55 836 above. Its control capture still hashed to
`415f970f...73f1e`, **the same value the package-ceiling round published for
this pose**, so every obvious check said "same scene".

It was not. That binary predated the 72 -> 75 package-ceiling change, so the
fixture's generated `src/terrain_game.cpp` came out carrying `stripRun = 72u`
and the roads and the terrain were cut into 72-vertex runs -
`ROADSTRIP ... packages 526`, `TERRAINSTRIP ... vertices 591 packages 9`. That
is the stale-editor trap `tyra-testing` spells out, and nothing in any log names
it.

**The capture hash could not catch it, because that round's own evidence shows
BOTH of its arms hashing to that one value**: changing the strip run changes how
a surface is cut into runs, not which pixels it covers. So a matching capture
hash is a PICTURE check and never a FIXTURE check. `ROADSTRIP`, `TERRAINSTRIP`
and a grep for `stripRun` in the generated source are the fixture check.

Everything on this page was re-measured after rebuilding the editor from this
worktree and regenerating the fixture with it. The earlier arms' conclusions
survived unchanged - both of them were built by that one binary, so the knob was
still the only difference between them - but their absolute numbers described
the 72-run scene and are not quotable beside anything else on the branch.

### What did not converge, and which three bags it is

The candidate reports `built=64 per frame` at a completely frozen pose - 64
package blocks re-baked every frame, for ever - against a stable `refs=59` and a
stable 1 431 KB arena inside a 4 MB budget. So it is neither eviction nor
expiry, and "it churns" is not a finding. `STAPIPMISS` tallies one reason per
invalidation, and on the held garage-day pose it reads the same thing in three
consecutive windows:

```
STAPIPMISS new=0 bbox=300 prim=600 streams=0 program=0 size=0 incomplete=0
           over 300 frames; loudest bag count=2280 packages=31 reason=primState
```

Read per frame, that is the whole answer, and it is **three bag invalidations a
frame** rather than a systemic failure:

| reason | per frame | what it means |
| --- | ---: | --- |
| `bbox` | **1** | a caller bumped `bboxVersion` - a claim that the buffer's CONTENTS changed - on a scene that is not moving |
| `prim` | **2** | the same vertex array submitted again with a different prim state, i.e. a SECOND PASS, thrashing the one entry the cache holds per (array, package size) |
| `new`, `streams`, `program`, `size`, `incomplete` | **0** | nothing else moves at all: no array is reallocated, no program or package size changes, and every bag that starts finishes |

Both mechanisms were among the guesses; what the instrument adds is that they
are the **only** two, that they cost exactly three bags, and that those three
bags are **large** - the loudest carries 2 280 vertices in 31 packages, and an
earlier window names a 3 768-vertex, 51-package one. Three bags at about 21
packages each is the 64 rebuilds, to the package.

Two more things the same counter settles:

- **The cache converges completely when nothing lies to it.** At the outer-road
  day pose a whole window reads `new=0 bbox=0 prim=0 ... built=0` with
  `refs=19`: every drawn direct bag replayed, nothing rebuilt. So the churn is a
  property of what the garage view contains, not of the design.
- **`bboxVersion` is a defect this repo has already paid for once.**
  [wheel-rebake-skip.md](wheel-rebake-skip.md) records the vehicle wheel batch
  bumping it unconditionally and disabling two caches at once. The garage is
  where the parked cars are and the outer road is where they are not, which is
  consistent with another caller doing the same thing - but consistent is not
  measured, and naming it means following one bag through a submitter, which is
  a change to a CALLER's contract and not to this one. **Identified, not
  fixed**; the counter is in the tree so whoever takes it starts with a name
  instead of a theory.

It is not a correctness problem either way - a rebuilt package takes the
ordinary path, and the byte-identical picture proves it - but it is a third of
the direct route's packages paying for a cache that never serves them, and it
would contaminate a later round that tried to price this.

## What it would take to move the bake into the editor

Explicitly out of scope here, and this is what it needs:

1. **An emitter for the block, host side.** It is not a new description of the
   packet format if the host emits VIFcodes through the same constants the
   engine uses (`stapip_vu1_shared_defines.h` is already shared); it IS a second
   description if it hardcodes the layout. The safe shape is to keep the
   run-time transcoder as the reference and make `--vu-check`-style host
   verification compare an editor-emitted block against a transcoded one for
   the same bag.
2. **Two patch points at load.** `RendererCoreDepth::scale` (one word per block)
   and the program's micro-memory destination address inside each `MSCAL`
   (one word per block, and only for package 0 of each bag). Both are known
   offsets; the loader would walk the block's header quadwords once.
3. **A file format decision.** `.tmdl` already stores what the editor resolved
   (`src/tmdl.hpp`, twinned with `TmdlLoader`); the baked stream is the same
   bytes again in VIF order, so the honest options are to store the stream
   INSTEAD of the arrays and rebuild the arrays at load for the bbox/clip/game
   consumers, or to store both and pay the disc space. Storing the stream alone
   does not save RAM — the consumers listed above still need the arrays.
4. **The per-instance problem.** The district's models are shade-baked per
   placed object, so two instances of one model do not share a vertex array and
   would not share a baked stream either. Whatever the editor bakes has to be
   keyed the way the run-time cache is keyed, or it bakes one stream per
   instance and the memory number above is the floor rather than the estimate.

## What this page does not establish

No timing, on either machine. The spike's own EE saving over the **retained**
path is one `REF` tag per run instead of one 96-byte `memcpy` per package, which
is small; the prize the plan page is after is the per-package classification and
qbuffer bookkeeping that this spike deliberately leaves in place so the counters
stay comparable. Read the chain quadword count as the input to that later
question, not as an answer to it.

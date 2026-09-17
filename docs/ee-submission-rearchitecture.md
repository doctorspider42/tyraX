# The EE pays 147 cycles per triangle: a plan to stop it

Six rounds of renderer work have made this engine measurably faster and have
generalised across two maps. None of them changed the thing that decides the
Motor District's frame rate, because none of them was allowed to: the static
pipeline rebuilds, every frame, the command stream for geometry that has not
moved since the level loaded. Its EE cost therefore scales with **triangles**,
and a 60 Hz budget only affords a cost that scales with **objects**.

This page states that in numbers, names the change that would fix it, and — the
part that matters more — names the two probes that must be run *first*, because
either of them can cap the prize before a line of the redesign is written.

Nothing here is implemented except the two probes, which were run on hardware
on 2026-09-16 and are written up in their own section below. Every other number
in this page that is not attributed to a measured run is a prediction to be
falsified - and the probes falsified two of them, so read the supporting-changes
table through that section rather than on its own.

## The number

Branch tip, physical PS2, `package-ceiling-75-2026-09-16/console/frame-cost.csv`,
240 warmed rows per pose, four parked poses, PAL 512x512 native:

| pose | update | submit | finish | present | **work** | triangles | flushes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| garage day | 3.26 | 26.14 | 1.02 | 9.54 | **30.42** | 40 961 | 120 |
| garage night | 3.34 | 32.74 | 1.54 | 8.26 | **37.62** | 41 629 | 142.5 |
| outer day | 3.26 | 9.60 | 1.04 | 6.05 | 13.90 | 16 053 | 43 |
| outer night | 3.33 | 11.93 | 1.56 | 3.15 | 16.82 | 16 385 | 47 |

Inside `submit`, garage day / garage night:

| bucket | day | night |
| --- | ---: | ---: |
| `bounds` | 2.40 | 3.28 |
| `prepare` | 2.86 | 4.75 |
| `dispatch` | 14.92 | 16.66 |
| — packet construction | 1.85 | 2.01 |
| — `send_packet2` (`FlushCache` + chain start) | 2.10 | 2.49 |
| — **VIF1 wait** | **5.70** | **6.17** |
| the generated game's own EE work in `renderScene` | 5.96 | 8.05 |

`VIF1 wait` is the only bucket in which the EE is not executing instructions.
Subtract it and the rest is EE time:

```
day    26.14 - 5.70 = 20.44 ms x 294.912 MHz / 40 961 tri = 147 EE cycles per triangle
night  32.74 - 6.17 = 26.57 ms x 294.912 MHz / 41 629 tri = 188 EE cycles per triangle
```

A 60 Hz frame is 4 915 200 EE cycles. At 41 000 triangles the EE can afford
about **five cycles per triangle** and still leave room for gameplay. We are
thirty times over. That gap does not close by making any of the existing terms
smaller; it closes by making the per-triangle term **not exist**.

## Why the engine spends them

The static pipeline is immediate-mode. Per frame, for every VU1 package of 75
vertices, the EE merges part bounding boxes, classifies the package against
object-space frustum planes, fills a qbuffer descriptor, resolves a program,
runs the 32-slot flush bookkeeping and writes about seven quadwords of commands
through `packet2`, one float at a time.

The garage-day FTCLIP counters (`package-ceiling-75-2026-09-16/ftclip-arms.txt`,
line 5, 50-frame window) say how many times:

| per frame, garage day | packages |
| --- | ---: |
| cull route | 775 |
| guard-band-only | 238.5 |
| clip route | 36.5 |
| **classified and rejected** | **922.5** |
| **total package classifications** | **1 972.5** |

20.44 ms over 1 972.5 classifications is roughly **3 000 EE cycles per package
touched** (the figure includes per-bag and game-side work, so read it as an
order of magnitude, not an attribution).

**Those are ROUTED packages, not classifications, and the distinction was worth
knowing before designing against it.** `checkFrustum` actually runs **570.5**
times a garage-day frame and costs **1.79 ms** in total, measured — about half
the bags are wholly visible, take the direct route and never build a package
descriptor at all (`dsDirectBags` 53.5 against `dsPartialBags` 59). See the
probes section below.

Two more per-frame taxes sit on top, both visible in the source:

- **`FlushCache(0)` 120 times per frame.** `dma_channel_send_packet2(p, ch,
  true)` is syscall 100 — a whole-data-cache write-back invalidate — followed by
  the chain start. The `send_packet2` bracket is 2.10 / 2.49 ms, and
  [vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md) already established that
  the refill the flush forces onto `bounds`, `Package_create` and `Packet_build`
  is real and was never measured, because adding flushes cannot measure it.
- **A `FLUSHE` per mesh.** `StaPipQBufferRenderer::sendObjectData` opens every
  bag with a VIF `FLUSHE` because the MVP, light matrix, options and ALPHA land
  at *absolute* VU1 addresses 0..21, outside the double buffer
  (`stapip_vu1_shared_defines.h`). Every mesh boundary is therefore a full VU1
  drain. The comment above it is correct about why it is needed *given that
  memory map*; the memory map is the thing to change.

None of this is bad code. It is a general-purpose immediate-mode renderer doing
exactly what it says. It is the wrong shape for a level whose geometry is
constant.

## What the other half of the frame costs, so nobody is surprised later

VU1 is not idle and the redesign does not make it free. Using this repository's
own measured rate — `cull_tc` at 73 cycles per triangle on an unlit mesh and 133
on a lit one, and about 39% of garage-day triangles unlit
([vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md), "The spot light half is
done") — 40 630 cull-route triangles is roughly **15 ms of VU1 work** in garage
day, hidden today behind 20.44 ms of EE work and surfacing only as the 5.70 ms
wait.

**So the EE redesign alone lands at about 19-20 ms of work, and stops.** That is
the PAL 50 Hz threshold and not one step past it. Anything beyond needs fewer
triangles or cheaper ones. This is stated up front because a plan that promises
60 Hz from the EE half alone would be wrong, and would be found to be wrong only
after the work was done.

## Where the frame could go

| | now (garage day) | EE redesign | + triangle budget |
| --- | ---: | ---: | ---: |
| update | 3.26 | 3.26 | 3.26 |
| submit | 26.14 | ~16 | ~9 |
| finish | 1.02 | 1.02 | 1.02 |
| **work** | **30.42** | **~20** | **~13** |
| displayed (PAL) | 25 fps | 50 fps | 50 fps (60 on NTSC) |

The middle column is bounded by VU1, not by the EE. The right column needs the
triangle count down by roughly 40%.

## The vsync ladder makes the first step worth double

Garage day is locked at `total_ms` 39.96, exactly two PAL fields, with 9.54 ms
of it spent waiting in `present`. The next rung is 20 ms. **Work has to fall
10.42 ms for the displayed frame rate to go from 25 to 50** — and the supporting
changes below were expected to sum to that on their own, before the architecture
is touched. **That expectation is now partly measured and it does not hold.**
S3 is refuted outright and S1 is worth 1.09 ms rather than 2.10, so S1, S4 and
S5 together are nearer 4-5 ms than 10.42. The rung needs the architecture.

## The change: a baked VIF stream per mesh

**At bake time**, for each static mesh / LOD tier / material, the editor emits a
ready **VIF1 command stream** — unpack headers, the scale quadword, the GIF tag,
the vertex payload interleaved in VIF-ready order, `MSCAL` — as one contiguous
block, plus the per-package and per-cluster bounding boxes.

**At run time**, per visible object: one AABB test, the MVP and light uniforms,
and **one DMA `REF` tag** naming the block. The EE never sees a package again.

Five things make this a bounded change here rather than a rewrite:

1. **The baker already cuts geometry on package boundaries.** `meshstrip::kRun`
   is 75 and is asserted equal to `getMaxVertCount`; `roadgen::kStripRun` is the
   same constant. The split the runtime recomputes every frame is already a
   bake-time fact.
2. **The blocks are already known to be position-independent.** That is the
   whole thesis of `StaPipRetainedCommands` and
   [retained-static-commands.md](retained-static-commands.md). The retained cache
   is most of the machinery; what is missing is baking instead of capturing on
   frame 1, *referencing* instead of `memcpy`-ing into a packet, and dropping
   per-package classification for the wholly-visible case.
3. **It needs no `NEXT` or `CALL` tag.** If the block is a pure VIFcode stream —
   no DMA tags inside, vertex payload inline — then a single `REF` tag transfers
   it and control returns to the frame chain by itself. The prohibition
   `retained-static-commands.md` records stands untouched, and the DMAC's
   two-deep ASR stack is never involved.
4. **The slow path is unchanged.** A mesh whose box straddles the near plane or
   the guard band keeps today's classify-and-clip route. In garage day that is
   36.5 of 1 050 drawn packages.
5. **It is checkable by construction** — but **not by the gate this line
   originally named.** "The triangle, package and flush counters must not move"
   is the gate every earlier round used, and it is the reason the spike could
   not deliver: a run of packages under one `REF` cannot cross a packet flush
   boundary, so pinning `packetFlushes` pins the prize. The replacement is
   designed in
   [baked-stream-acceptance-gate.md](baked-stream-acceptance-gate.md) — a
   canonical hash of the word stream VIF1 actually receives, with texture
   mutations interleaved, plus byte-identical pixels over a pose sweep. The
   byte-identical picture half of the old gate survives intact; the counters do
   not.

Two details to design, not discover: the GIF tag in the block encodes `NLOOP`
and the primitive type (both bake-time facts), and the scale quadword carries
`RendererCoreDepth::scale`, which a 16-bit display mode moves — so either bake
per depth or patch one quadword at load.

**The format half of this has now been spiked and it works** — see
[baked-vif-stream.md](baked-vif-stream.md) for the exact quadword layout, the
per-class table, and the three corrections this section needs. In short: VIF1
accepts the block because a TTE chain already hands it an unbroken word stream
(the `submissionBatchCandidate` branch of `sendObjectData` is the existence
proof); the program kick is a **third** bake-time fact, not a per-frame one,
because `render()` clears `lastProgramName` per bag, which is what lets one
`REF` cover a whole mesh rather than one package; and the cost is the part this
page does not mention at all — inlining the payload stores every static vertex
twice, about **+49 bytes per vertex**, three to four megabytes for the Motor
District, because nothing can free the arrays (the bbox cacher, the clip route
and the generated game all still read them).

Predicted EE cost: about 300 visibility units at roughly 400 cycles each, near
**0.4 ms**, against 20.44. Even a three-fold error leaves the direction intact.

## Supporting changes, ranked by value over risk

| # | change | predicted (garage day / night) | risk |
| --- | --- | ---: | --- |
| S1 | Build the frame's chain in the EE scratchpad (16 KB at `0x70000000`, unused by this engine) or in uncached memory, and stop calling `FlushCache` | **−1.09 MEASURED**, not −2.10 | **medium** — dropping the flush corrupted the picture even with the packet uncached; see the probes |
| S2 | Remove the per-mesh `FLUSHE`: a second uniform bank at VU1 addresses 22..43 and a bank bit in the count word | via VIF1 wait; little until S1 and the redesign land | medium — touches 15 microprograms; sequence it last |
| ~~S3~~ | ~~Classify per 1/3-bbox part instead of per package~~ **REFUTED, do not build** | the whole bracket is **1.79 ms** and the arm that coarsens it measured **+4.59** | — |
| S4 | The shared reflection probe costs +26 flushes and +10 444 triangles on every second frame | ~1.5-2 averaged | low — fewer objects, coarser LOD, or a longer cadence |
| S5 | `vehicles_included_ms` is a flat 2.26 ms with the car parked | ~1.5 | low — per-wheel matrices instead of an EE vertex rebake |

## The triangle budget, which is the other cardinal half

- **The road surface alone is 31 050 triangles** in the whole map
  (`ROADSTRIP scene 0 strips 1 packages 470 triangles 31050`). A ribbon of
  constant cross-section does not need them; longitudinal LOD is the largest
  single content lever in this scene.
- **Authored LOD distances are zero.** The LOD trials were rejected because they
  repeated 25 / 25 / 50 / 50 — but that was measured while the EE was the
  limiter, where no reduction in VU1 work could show. They must be re-run after
  the EE half lands, not treated as settled.
- **There is no occlusion culling of any kind.** A city district is the textbook
  case for baked sectors/portals or a PVS. [portals.md](portals.md) and
  [impostors.md](impostors.md) exist as per-object features; what is missing is
  world-level visibility.

## What is NOT the lever, with the evidence

Recorded so nobody spends a week re-discovering it:

- **Vertex compression.** A probe adding 33% more payload per frame moved VIF1
  wait by 0.067 ms ([vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md)). The
  transfer hides completely. A V4-16 position also still unpacks to a full
  quadword in VU1 memory, so it would not enlarge a package either.
- **16-bit framebuffer.** No gain established
  ([hardware-profiler-results.md](hardware-profiler-results.md)).
- **GS pixel fill.** Masking the scissor to one pixel bought 1.66 / 2.62 / 0.68 /
  1.53 ms. The GS is not the limiter in these views.
- **Bigger VU1 packages.** 72 to 75 measured +0.009 ms in garage day: a larger
  package straddles a clip plane more often, and `clip_tc` is 184 cycles against
  `cull_tc`'s 73. The ceiling is self-limiting
  ([package-ceiling-75-2026-09-16](../examples/vehicle-playground/authoring/package-ceiling-75-2026-09-16/README.md)).
- **Forking ps2sdk.** Both of its cache primitives are wrong for a DMA packet
  and the right one is about ten instructions — but S1 removes the question
  entirely by never writing the chain into cached memory. The component actually
  worth owning is `packet2`, a float-at-a-time builder, and only if profiling
  after the redesign still shows it.

## The two probes that come first — RUN, on hardware, 2026-09-16

Both were run on the physical PS2. Raw evidence, arms, ELF hashes and the
reproduction recipe:
[ee-probes-2026-09-16](../examples/vehicle-playground/authoring/ee-probes-2026-09-16/README.md).
Seven ELFs, all hashed and distinct; two boots of the control give a
**repeatability floor of 0.135 ms of `work`**; every run collected all 960 rows
and reported `reuploads` 0.000 in all four poses.

**Do not read the probes' control against the four-pose table at the top of this
page without these two facts.** Both are established in the evidence directory,
not assumed.

- **The probes' control runs the live tools; that table does not.** The probe
  fixture is `--profile debug`, which leaves `liveDebug` and `remotePad` on, and
  their pollers `fopen` over `host:` *inside* the instrumenter's `update`
  bracket. An extra control boot with `--profile quiet-debug` prices them at
  **4.79 ms of `update` and 6.44 ms of `work`**, and with them off the two tables
  agree to 0.35 ms of `work` and 0.00 ms of `total` (30.071 against 30.42;
  39.957 against 39.96). The probes' arm tables should therefore be read through
  `submit`/`dispatch`, because `update` there carries host-I/O outliers of
  +3.5 to +4.4 ms that no arm can produce.
- **The probes' control is a 72-run fixture, not the branch-tip 75.** Its
  `ROADSTRIP` reports 526 packages against 470 and its `stripRun` bakes `72u`,
  because the editor binary that regenerated it was built two hours before
  `9a9d25e0` raised the ceiling — **an editor binary in `build/` is not "the
  editor at the tree's commit"**. Every arm shares it, so no delta is affected;
  the absolute level would be a few per cent lower at 75, in the same direction
  for all of them. And the garage-day capture still hashed to the expected
  value, which is why **a capture hash is a picture check and never a fixture
  check** — the producer lines and the baked constant are.

**They cap both directions they were aimed at, and one of them falsifies the
reason S1 was thought to be safe.** Garage day, frame `work`:

| question | answer |
| --- | ---: |
| what per-package frustum rejection BUYS | **2.60 ms** |
| what per-package classification COSTS | **1.79 ms** |
| what coarsening the classification costs (S3) | **+4.59 ms** |
| what `FlushCache` costs, refill included (S1) | **1.09 ms** |
| what the cheapest legal way to stop calling it costs | **+7.55 ms** |

**Probe A — rejection pays for itself, and S3 is refuted.** The arm that ran the
classification and discarded only its `OUTSIDE_FRUSTUM` verdict cost **+2.60 ms**
of `work` (+8 387 triangles, +1.09 ms of VIF1 wait), with a **byte-identical
picture**. The whole `checkFrustum` bracket, measured exclusively under
`TYRA_STAPIP_ATTRIB`, is **1.79 ms** — so the test buys more than it costs, and
**the redesign must keep an equivalent visibility test**. 1.79 ms is also the
hard ceiling on S3, and the arm that actually coarsens classification to the
existing eight-package group came out **+4.59 ms**: a coarse box crosses more
clip planes, so whole groups of eight take the clip route where one package
would have. **S3 does not ship. Strike it from the table.**

Three corrections to what this page said above:

- **"Force every package to `IN_FRUSTUM` and skip classification entirely" is a
  corrupted arm, not a probe.** It routes near-plane-crossing geometry to the
  cull programs, which do not clip. The arm has to keep `PARTIALLY_IN_FRUSTUM`
  intact and discard only the `OUTSIDE` verdict — and it has to cover the coarse
  eight-package test as well, or most of the frame's rejections survive.
- **A "1/3-bbox part" is one third of a PACKAGE**, not a group of them
  (`StaPipBagPackagesBBox` splits a bag into `maxVertCount / 3`-vertex parts and
  a full package spans exactly three). S3 as written would triple the number of
  tests, and parts are not shared between packages, so nothing amortises. A
  coarse level one box per **24 parts / 8 packages** already exists and already
  short-circuits wholly-in and wholly-out groups.
- **"1 972.5 total package classifications" is the routed-package count, not the
  classification count.** `checkFrustum` runs **570.5** times a garage-day frame;
  about half the bags are wholly visible and never call `packager.create` at all.

**Probe B — the prize is half what was predicted, and dropping the flush
corrupts the picture.** Two controls were needed, because allocating the chain
uncached changes two things at once: an arm that is uncached and **still calls
`FlushCache`** holds the write cost constant.

- `uncached + flush` − stock = **+7.55 ms**. That is what writing the chain
  through uncached memory costs; `packet2` writes floats one at a time and an
  uncached store does not gather.
- `uncached` − `uncached + flush` = **−0.84 ms of the `send_packet2` bracket and
  −1.09 ms of `work`**. That is `FlushCache`, refill included, with everything
  else equal. **The −2.10 / −2.49 predicted for S1 above is about 2x
  optimistic**, and the "unmeasured refill" is worth about 0.25 ms, not a hidden
  prize.
- **The uncached arm that still flushes is byte-identical to the control. The
  one that does not flush is CORRUPT** — 1 271 of 262 144 pixels, a 14-row band
  at the horizon where the road and the far buildings tear into slices — even
  though the qbuffer copy pools were explicitly kept on the flushing path and
  the uncached allocation was verified on the console before anything was read
  from it.

So **S1's justification on this page — "the packet is no longer in cached
memory, so there is nothing to write back" — is false.** Something else the EE
writes into cached memory and the DMA reads by `REF` is not covered, or
`FlushCache(0)` is also supplying an ordering barrier that its removal takes
away. This round did not separate those two, and whoever builds S1 owes that
answer first. Its risk rating in the supporting-changes table should read
**medium**, not low, and its predicted value **−1.1 ms**, not −2.10.

`P2_TYPE_UNCACHED_ACCL`, which this page names for the arm, was deliberately
**not** booted: the stock ps2sdk builder back-patches bytes it has already
written (`packet2_vif_close_unpack_auto` reads byte 3 and writes byte 2 of the
open unpack VIFcode), and under UCAB those go through the EE's 128-byte
write-gather buffer. `P2_TYPE_UNCACHED` has no such buffer and is what was run.
UCAB is the version S1 would want for speed, and it needs a packet builder that
never reads back — i.e. owning `packet2`, which "What is NOT the lever" lists as
not worth doing. That tension is now S1's problem to resolve, not a detail.

**What still has no measurement**: the flush-removal correctness question above,
and Probe B under `--keep-routes`. The parked fixture cannot see a defect in a
per-frame rebake that writes the same bytes every frame, so the corruption found
is a lower bound on the problem rather than an inventory of it.

## What is left of the prize, recomputed from the probes' own attribution

The probes did not only refute two supporting changes; the attribution arm they
needed produced the first per-bracket breakdown of the EE side of
`StaPipCore::render` this work has ever had. Garage day, measured, exclusive
brackets under `TYRA_STAPIP_ATTRIB`:

| bracket | ms | can the baked stream remove it? |
| --- | ---: | --- |
| `bounds` | 2.46 | no - the visibility test needs the boxes |
| `prepare` | 2.84 | partly - per-bag uniforms stay, the clip chain is already retained |
| `dsRetain` | 0.69 | yes |
| `dsDirect` (the wholly-visible loop) | 1.90 | mostly |
| `dsCreate` minus classify | 0.68 | yes |
| **`dsClassify`** | **1.79** | **no - Probe A says rejection buys 2.60** |
| `dsRender` (package routing) | 2.05 | partly |
| packet construction | 1.79 | yes |

**Nothing in that column is over 3 ms, and that is the finding.** A frame with
one expensive function has a fix; this one has fourteen cheap ones that each
scale with geometry. It is the clearest evidence yet that the shape is the
problem - and it is also why the earlier "about 0.4 ms" estimate for the
redesign was too optimistic.

**Probe A moved the floor.** The redesign was described above as "the EE never
sees a package again". It cannot be: per-package rejection buys 2.60 ms against
a 1.79 ms test, so a baked stream has to carry per-package boxes and the runtime
has to keep rejecting. `bounds`, `prepare` and `dsClassify` all survive.

**Which control these belong to matters, and the first version of this section
got it wrong.** The exclusive `ds*` brackets were measured on the arm with the
live tools running; those pollers `fopen` over `host:` inside `update`, not
inside `render`, so the brackets carry over - but the frame they sit in does
not. On the quiet-debug control that matches this page's headline table
(`update` 2.804, `work` 30.071, `total` 39.957), the EE side of `render` is
`bounds` 2.35 + `prepare` 2.92 + (`dispatch` 15.04 - `VIF1 wait` 5.56) =
**14.76 ms**.

Splitting that by the column above: about **5.5-6.5 ms is removable** (the
retain lookup, package-record creation, packet construction, most of the
wholly-visible loop and part of the package routing), and about **7.1 ms
survives** - `bounds` and `prepare` because the visibility test needs them, and
`dsClassify` because Probe A says so.

So the projection, and it is a projection: **work 30.07 -> about 24**, and with
S4 and S5 on top, **about 21**. That is still **above** the 20 ms rung.

**THAT PROJECTION IS NOW MEASURED AND IT WAS ABOUT FIVE TIMES TOO OPTIMISTIC.**
The architecture was built and run on the physical PS2 — a complete baked bag
replayed by ONE DMA `REF`, skipping the qbuffer ring entirely — and garage-day
`work` falls **1.287 ms**, not 5.5–6.5, against a repeatability floor of
**0.012 ms**
([ee-rearchitecture-2026-09-16](../examples/vehicle-playground/authoring/ee-rearchitecture-2026-09-16/README.md)).

The column above was not wrong about *which* brackets are removable. It was
wrong about how much of each one this architecture can reach:

| bracket | budgeted removable | measured |
| --- | ---: | ---: |
| packet construction | 1.79 | **−0.808** |
| `dsDirect`, `dsRetain`, `dsCreate`−classify, part of `dsRender` | ~4.3 | inside `dispatch`'s **−1.426** |
| `bounds` | none | −0.173 |
| `prepare` | none | **+0.315** |
| **`work`** | **5.5–6.5** | **−1.287** |

Two reasons, both worth carrying into the next estimate on this page. **Only
half the bags take the direct route at all** — `dsDirectBags` 53.5 against
`dsPartialBags` 59 — so a change confined to that route can never reach a whole
bracket: the 360 packages it replays are 45% of the frame's 803.5. And
**`prepare` RISES**, by 0.315 ms, which is the same term that refuted mesh
LOD 64 (+0.272 ms there).

The part to trust is that the packet arithmetic closes exactly: 1.898 ms over
803.5 packages is **2.362 µs a package**, 360 packages replayed predicts
**0.850 ms**, and **0.808** was measured. **The EE pays per PACKAGE**, now
measured twice over — which is also why the frame's worst-packed geometry costs
out of proportion to its triangles: projected shadows and wheels run 22–25
triangles a package against a strip's ~70, taking 16% of the frame's packages
for 7.6% of its triangles.

**And `total_ms` did not move in garage day at all**: 39.960 in every arm, the
whole saving absorbed by `present`. The rung needs 10.42 ms and this is an
eighth of it. Garage night is the exception — the control averages 43.9 ms
(frames alternating between the two-field rung and a three-field spill, i.e.
judder) while the candidate reads a flat 39.959. **Stutter removed,
milliseconds not.**

**Read that as the plan's central correction.** The first version of this page
expected the supporting changes to reach the rung by themselves; the probes
refuted that. This recomputation says the architecture plus the supporting
changes do not reach it either. The triangle budget is not a later chapter, it
is the third of three things that all have to happen - which also means the
redesign should be built so that fewer triangles make it cheaper rather than
merely quieter.

## Order of work

1. ~~Probe A and Probe B.~~ **DONE, on hardware, 2026-09-16.** S3 does not ship;
   the recoverable part of the `send_packet2` bracket is 0.84 ms.
2. **Find out what else `FlushCache` was writing back** - dropping it corrupted
   the picture with the packet already uncached. S1 cannot be built until that
   is answered, and it is worth 1.09 ms when it is.
3. ~~The baked VIF stream, behind a compile-time switch.~~ **BUILT AND MEASURED
   ON HARDWARE: worth 1.287 ms of garage-day `work`, and BLOCKED on a contract.**
   A complete baked bag is replayed by one DMA `REF` and skips the qbuffer ring
   entirely; the flush cadence moved 120 → 109, which the old counter gate could
   never have accepted, and
   [the redesigned gate](baked-stream-acceptance-gate.md) accepted it on a
   bit-identical VIFcode stream and a byte-identical picture. The per-package
   visibility test is untouched — the direct route never had one to lose, since
   Probe A's finding is about the partial branch.

   **The blocker is not an oversight, it is the direction's own trade**:
   inlining the payload is what buys the tag count, and inlining the payload is
   what creates a copy that can go stale invisibly. The key holds each array's
   pointer plus `bboxVersion`, and `bboxVersion` is about positions, so a caller
   re-shading per-vertex colours in place changes what the block must contain
   without changing anything the key can see — measured, 1438 times, by the
   adversarial verify mode. The retained cache is immune precisely because it
   stores the chain and lets the DMA follow the pointer at transfer time.

   So the open question is not "fix the key" but **"does 1.287 ms earn a
   contract worth roughly 110 unenforced obligations in the generated game, whose
   failure mode is stale lighting visible only while the camera moves?"** It
   ships at 0 until that is answered.
4. S4 and S5 — the two cheap non-pipeline wins. They are needed to reach the
   20 ms rung even with the architecture, so they are no longer optional.
5. The triangle budget, promoted out of last place: road LOD, the authored LOD
   distances the earlier trials could not evaluate while the EE was the limiter,
   and world visibility. The recomputation above says the EE half does not clear
   the rung on its own.
6. S2 — the uniform bank, once VU1 is the limiter and the drain costs something.

## What this page does not establish

Every millisecond attributed to a future state is a prediction. The 15 ms VU1
estimate is derived from a measured per-cycle rate and a routing mix taken from
a different build, not measured directly. The 0.4 ms redesign figure is an
arithmetic sketch. The measured numbers here are the four-pose table at the top,
the FTCLIP package counts, **the two probes' section and everything it links**,
and the prior results this page cites by link. Everything else remains a
prediction.

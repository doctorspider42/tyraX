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
| S4 | The shared reflection probe costs +26 flushes and +10 444 triangles on every second frame | **2.07 / 2.56 MEASURED**. **SHIPPED as a reuse budget** (1.106.0); what it recovers depends on motion, see below | low — it can only reduce captures, and the cadence stays the ceiling |
| S5 | `vehicles_included_ms` is a flat 2.26 ms with the car parked | ~1.5 | low — per-wheel matrices instead of an EE vertex rebake |

## The triangle budget, which is the other cardinal half — MEASURED, 2026-09-16

All three of the attacks below were run on hardware. Raw evidence, arms, ELF
hashes and the reproduction recipe:
[road-lod-2026-09-16](../examples/vehicle-playground/authoring/road-lod-2026-09-16/README.md).
Nine boots, `--profile quiet-debug`, four parked poses, 240 recorded rows each,
**repeatability floor 0.016 ms of `work`** — eight times tighter than the
probes' round, because those ran `debug`.

**The finding that reorders this section: the garage barely contains any road.**
The road reduction that shipped takes the district from 31 050 road triangles to
21 252 and from 470 packages to 337 — and the garage-day frame loses **614** of
them, 1.5% of its 40 961. The outer-road poses lose 2 817, 17.6%. The bullet
below was true about the MAP and wrong about the garage, which is the pose this
whole page is about.

| attack | garage day | garage night | outer day | outer night | verdict |
| --- | ---: | ---: | ---: | ---: | --- |
| road lateral budget | **−0.358** | −0.367 | **−0.591** | −0.547 | **shipped** |
| + terrain LOD 160 | −0.738 | −0.821 | −0.607 | −0.609 | recommended, one check outstanding |
| + mesh LOD 64 | −0.166 | −0.235 | −0.444 | −0.362 | **REFUTED, and now for a reason** |
| + reflection probe every 4th frame | **−1.391** | **−1.645** | −1.133 | −1.087 | a bounding probe for S4 |

- **The road surface was 31 050 triangles** in the whole map
  (`ROADSTRIP scene 0 strips 1 packages 470 triangles 31050`). A ribbon of
  near-constant cross-section does not need them — but the attack that worked
  was **lateral**, not longitudinal, and the reason is in
  [roads.md](roads.md), "The lateral budget": the reduction was all-or-nothing,
  and a road is a decal sampled eight times more finely across than the 4-unit
  terrain cell it is projected onto, so the full width was almost never one
  plane and nothing ever merged. Merging maximal coplanar RUNS instead is
  −31.6% of the triangles with the surface and every seam exactly unchanged.
- **Authored LOD distances are zero, and the two halves of the rejected pair
  have OPPOSITE SIGNS.** Re-run against `work_ms` rather than displayed FPS,
  mesh LOD 64 removes 592 triangles, takes 0.32 ms out of `dispatch` and 0.31 ms
  out of the VU1 wait — and makes the frame **0.19 ms slower**, because the
  per-object tier selection costs more than the geometry it saves at this object
  count. That is no longer a null result. Terrain LOD is the useful half, worth
  **−0.44 ms of garage day at 96** — but 96 lets the second detail band (every
  4th sample, beyond 2.2x the distance) reach the map, and there the coarse
  ground rises up to 0.38 units above a road that floats 0.12 above the dense
  heightfield. At **160** the second band starts at 352 units and cannot fire,
  the worst rise is 0.0175 against the 0.12 lift, and −0.38 / −0.45 ms survives.
- **The shared reflection probe is worth more in the garage than both of those
  together.** Halving its cadence buys 1.03 / 1.28 ms, so the whole probe costs
  **2.07 ms of garage day and 2.56 ms of garage night** — S4's "roughly 2 ms
  averaged" was a prediction from counts and it was right. Anyone picking up the
  triangle budget should start there, not at the road. **Taken, 2026-09-16** —
  as a reuse budget rather than as a cadence; see item 3 of the order of work.
- **There is no occlusion culling of any kind.** A city district is the textbook
  case for baked sectors/portals or a PVS. [portals.md](portals.md) and
  [impostors.md](impostors.md) exist as per-object features; what is missing is
  world-level visibility. Nothing in this round touched it, and after this round
  it is the largest untried lever on the garage.

**A rule worth carrying forward, because two independent reductions now agree on
it.** A triangle REMOVED from this frame is worth about **0.4** of its VU1 cycle
count, while a triangle ADDED costs about **0.9** of it. The road arm's 614 and
2 817 removed triangles predict 0.152 and 0.697 ms of unlit VU1 and moved the
VIF1 wait by 0.055 and 0.274 — 36-39% — which is the same fraction the
spot-light gate reached when it removed 60 cycles (41%), and the mirror of the
87% that the +33% payload probe measured when it ADDED work. Budget accordingly.

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
for 7.6% of its triangles. (Attacked in 1.107.0 — see the bullet below for what
that turned out to be, and for the 23 packages it was worth. **2.362 µs is the
floor of what a package costs, not the price of one**: removing 23 of them was
measured at 19.5 µs each — see "What one VU1 package costs" below.)

**And `total_ms` did not move in garage day at all**: 39.960 in every arm, the
whole saving absorbed by `present`. The rung needs 10.42 ms and this is an
eighth of it. Garage night is the exception — the control averages 43.9 ms
(frames alternating between the two-field rung and a three-field spill, i.e.
judder) while the candidate reads a flat 39.959. **Stutter removed,
milliseconds not.**

**One correction from S4's own round, and it makes the 21 an optimistic
reading.** S4 is now built, and what it recovers depends on how the camera is
moving: the full 2.07 ms when nothing moves, about half of it driving straight
or turning gently, and **nothing at all in a hard turn**. So "with S4 on top"
is a range, not a number, and the pessimistic end of it is the one a player
sees while driving. Every arithmetic in this section that adds S4 as a constant
is quoting its best case.

**Read that as the plan's central correction.** The first version of this page
expected the supporting changes to reach the rung by themselves; the probes
refuted that. This recomputation says the architecture plus the supporting
changes do not reach it either. The triangle budget is not a later chapter, it
is the third of three things that all have to happen - which also means the
redesign should be built so that fewer triangles make it cheaper rather than
merely quieter.

## What the garage-day frame is MADE OF — MEASURED, 2026-09-16

The section below says this page reasoned from the wrong inventory. There now
is a right one. Every producer in `renderScene` was bracketed with a telemetry
drain (`takeTelemetry()` clears as it reads, so the split is exclusive and
needs no new engine counter), giving triangles, VU1 packages, bags and packet
flushes per producer in each of the fixture's four parked poses. Counts only —
PCSX2, no console, and none of it is a millisecond. Raw evidence, the
instrument and the identity checks:
[reflection-probe-2026-09-16](../examples/vehicle-playground/authoring/reflection-probe-2026-09-16/README.md).

**The rows total 40 347 / 41 015 / 13 236.5 / 13 568.5, which is this page's
own four-pose table minus the road round's measured reduction, exactly, in all
four poses.** That is the check that the split is complete.

Garage day, 240 recorded frames, per frame:

| producer | triangles | % | packages | bags | flushes | tri/pkg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| solo static objects | **18 087** | **44.8%** | 354.0 | 62.0 | 38.0 | 51.1 |
| terrain | 5 416 | 13.4% | 76.0 | 25.0 | 12.0 | 71.3 |
| **the shared reflection probe** | **5 286** | **13.1%** | 92.5 | 18.5 | 13.0 | 57.1 |
| static batches | 5 159 | 12.8% | 89.0 | 45.0 | 18.0 | 58.0 |
| roads | 3 276 | 8.1% | 48.0 | 59.0 | 22.0 | 68.3 |
| projected shadows | 1 544 | 3.8% | 69.0 | 6.0 | 7.0 | **22.4** |
| wheels | 1 506 | 3.7% | 61.0 | 3.0 | 6.0 | **24.7** |
| sky (dome, stars, discs) | 67 | 0.2% | 12.0 | 2.0 | 1.0 | 5.6 |
| particles, blob shadows | 6 | 0.0% | 2.0 | 4.0 | 2.0 | |
| **total** | **40 347** | | **803.5** | **224.5** | **119.0** | |

Light pools, light beams, shadow decals, mirrors, portal surfaces, animated
models, the camera feed, the highlight pass and every non-road procedural chunk
submit **nothing at all** in this pose.

Four things this says that no earlier number could:

- **The garage is a pile of solo static models, and three instances of one
  model are a quarter of the frame.** `district-tower` x3 is **10 491
  triangles, 26.0%**; `district-loft` x2 is 2 748; the three car bodies are
  3 446. Nothing else reaches 600. The front this page still has open — world
  visibility, item 5 — is aimed at exactly that population.
- **The reflection probe is 13.1% of garage day and 13.5% of outer day.** It is
  very nearly pose-independent, because it is a 110-degree level-forward view
  of the same buildings whatever the camera does. S4's predicted "+26 flushes
  and +10 444 triangles on every second frame" reads **26 flushes and 10 572
  triangles** per hit, attributed.
- **Roads are 8.1% of garage day and 47.4% of outer day.** The previous round
  measured that as a delta; this is the inventory behind it.
- **Projected shadows and the wheels are the frame's worst PACKING** — 130
  packages for 3 050 triangles, i.e. 16% of the frame's VU1 packages for 7.5%
  of its triangles, because both are triangle LISTS (75/3 = 25 per package)
  where terrain, roads and the probe are strips (up to 73). This page's whole
  thesis is that the EE pays per package, so those two producers are worth
  about twice their triangle share — and neither has ever been attacked.

  **ATTACKED IN 1.107.0, and the diagnosis above was right about the symptom
  and wrong about the cause.** Neither producer "was generated at runtime and
  never got a strip". Both are lists because **`vehbake` never called
  `meshstrip` at all** — no vehicle model in the district has ever carried one —
  and when you do call it, it REFUSES them: an imported car is flat-shaded, so
  2 242 of a body's 2 280 corners are unique and the strip is 1.65x the list.
  Splitting the shadow bracket three ways says the rest:

  | producer | VU1 packages, garage day | what it is |
  | --- | ---: | --- |
  | `proj_shadows` → caster silhouette | **60** | the CASTER's own bags, re-submitted |
  | `proj_shadows` → receiver patches | 9 | the only array the feature builds |
  | `proj_shadows` → torch wall copy | 0 | no sunlit pose reaches it |
  | `wheels` | 61 drawn + 18 rejected | the wheel batch |

  So 87% of the "projected shadows" row is the car bodies, priced a second time
  from the light's point of view; the feature's own geometry is 9 packages.
  Two things followed. The **wheel** batch can strip, because its bag is unlit
  and flat-coloured so its vertex is position and UV only
  (`meshstrip::Weld::kNoNormal`, [vehicles.md](vehicles.md)): **79 packages →
  60**. The **receiver patch** is a grid and strips trivially: **9 → 2**, more
  than the vertex halving suggests, because a stripped package is never
  sub-split into thirds by the partial-frustum route. Together **−23 of the
  frame's 711 packages (−3.2%)**, additively. The **body** is lit and is NOT stripped — `meshstrip`'s refusal of it is the
  right answer, and reaching those 60 packages needs something else.

  Note the denominator: 711, not the 803.5 in the inventory above. The shipped
  reflection reuse budget zeroes both `env_probe_*` rows at a parked pose.

## WHAT ONE VU1 PACKAGE COSTS, measured end to end

This page's thesis is that the EE pays per package, and until 2026-09-17 the
only *price* for one was **2.362 us**, which is packet construction alone. The
worst-packed-producer round could do better, because it removes a counted number
of packages and changes nothing else about the scene - so its millisecond
divided by its packages is the whole submission path's marginal cost rather than
one bracket's. Physical PS2, control alternated with candidate, control booted
twice (`examples/vehicle-playground/authoring/wheel-strip-2026-09-17/`,
`console/`).

| pose | control `work` | candidate | delta | packages removed | **us/package** |
| --- | ---: | ---: | ---: | ---: | ---: |
| garage day | 27.731 | 27.281 | **-0.449** | 23 | **19.5** |
| garage night | 34.686 | 33.987 | **-0.699** | 22 | **31.8** |
| outer day | 11.808 | 11.872 | +0.064 | 0 | - |
| outer night | 14.699 | 14.700 | +0.000 | 0 | - |

**Read it against two floors**: 0.020 ms for two boots of ONE ELF, and
**0.064 ms** for two DIFFERENT ELFs in a pose where the change cannot act (the
outer poses, where neither producer submits anything). The second is the honest
one for a code change, and it is three times the first; garage day clears it
sevenfold and garage night elevenfold.

**The prediction that was right was the generous one.** 23 packages priced at
packet construction alone gives 0.054 ms; priced at the whole `dispatch`
bracket's per-package average (19.0 us) it gives 0.43 ms. Measured **0.449**.
**A package removed from this frame is worth about EIGHT TIMES the
packet-construction figure**, and roughly half of an average package's total
`work` share (39.0 us).

**But do not carry 19.5 us as a constant**, and this caveat matters more than
the headline. The bracket split does not support a pure per-package model:

- **packet construction did not move at all** (-0.004 ms, inside its own 0.007
  floor). The one term with a published per-package price is absent from the
  saving, where a pure package model predicts -0.054.
- **`bounds` ROSE** by 0.087 ms, four times its floor - the same direction
  `prepare` moved in the baked-stream round, and the term that refuted mesh
  LOD 64.
- The three StaPipCore brackets explain only **-0.072 ms of the -0.434** in
  `submit`. About 0.36 ms sits in the unbracketed region that
  `instrument-frame-cost.py --attribute` exists to split, and which this page
  already flagged as 7.5 ms nobody had looked inside.
- `vif_wait` fell 0.163 ms because the candidate submits **2.5% fewer
  vertices** - a VU1 effect, not an EE per-package one.

So the defensible general claim is narrower than the headline and still useful:
**stripping a producer is worth roughly the whole-`dispatch` per-package
average, ~8x the packet-construction figure** - for a change that removes
packages AND vertices together, which stripping always does. A change that
re-packs an array without removing vertices should be budgeted lower, and
nothing here says how much lower. The cheap next probe is these same two arms
under `--attribute`.

`total_ms` is unchanged, 39.961 -> 39.960: two PAL fields either way, the whole
saving absorbed by `present`, exactly as the baked stream's 1.287 ms was.

And half of all classified packages are thrown away: **797.5 rejected against
803.5 drawn** in garage day. Probe A already established that the rejection
pays for itself, so that is not a defect; it is the population world visibility
would work on, stated as a number for the first time.

## The map is not the view, and this page promoted a front on the wrong inventory

The triangle budget was promoted here on the strength of "the road surface alone
is 31 050 triangles in the map". That sentence is true, and it was the wrong
number to reason from. Cutting the district's road triangles by **31.6%
map-wide** removes **614 from the garage-day frame, 1.5% of its 40 961**. The
outer poses lose 17.6%, which is where that front's value turned out to be.

**Inventory the view, never the map.** Nothing in the map-wide counter is wrong;
it simply does not say what the camera draws, and every pose in this scene
disagrees with it. The error was this page's, not the measurement's.

Two numbers from that round worth carrying into every later estimate:

- **A removed triangle is worth about 0.4 of its cycle count; an added one costs
  about 0.9.** Two independent reductions now agree with the spot-light gate's
  41%, against the payload probe's 87%. Do not budget a reduction at the full
  rate.
- **Per-bag cost is the term that does not shrink with the triangle count.**
  Mesh LOD 64 removes 592 triangles and takes 0.315 ms more off `dispatch` than
  the road arm, then adds 0.272 ms of `prepare` and nets +0.192. Anything that
  multiplies distinct bags pays this, which is why LOD tiers keep failing here
  and why making `prepare` scale with something other than the bag count would
  unblock more than itself.

## The VIF1 submission queue - MEASURED on hardware, 2026-09-23

`VIF1 wait` was the one bucket in this page's first table in which the EE
executes nothing (5.70 ms in garage day, in the round that measured it), and no
earlier round went after it: `StaPipQBufferRenderer::sendPacket` sent from two
packet buffers and waited for the previous transfer before every send. The EE
therefore ran in lock-step with VU1. **`Vif1Queue`**
(`renderer/core/paths/path1/vif1_queue.{hpp,cpp}`, `TYRA_VIF1_QUEUE`, on by
default) lets up to four packets be in flight. Each packet remains its own
complete chain ending in its own END tag, and the EE starts the next queued one
whenever it submits or waits and finds the channel idle. Nothing joins chains
with `NEXT`; that was tried and froze a console, see the engine skill.

Physical PS2, `--profile quiet-debug` Motor District fixture, the timing
instrument, control booted twice (floor **0.019 ms** of `work`), four parked
poses:

| pose | control `work` | queue | delta | `dma` bracket | `vif_wait` |
| --- | ---: | ---: | ---: | ---: | ---: |
| garage day | 14.761 | 14.043 | **-0.718** | -0.413 | -0.262 |
| garage night | 18.003 | 17.372 | **-0.631** | -0.528 | -0.144 |
| outer day | 11.014 | 10.463 | **-0.552** | -0.253 | -0.393 |
| outer night | 12.555 | 12.055 | **-0.500** | -0.279 | -0.358 |

Triangles and packet flushes are identical to the digit in every pose.
`total_ms` stays at 19.96 in all eight rows: this branch's fixture already sits
on the 20 ms rung, so the saving is headroom inside it, not a new rung. The
night poses pay **+0.23 ms of `prepare`** for one of the changes the queue
forced (the inline uniforms below); the net is still -0.63 / -0.50.

The picture is equivalent in PCSX2's software renderer. Every differing pixel
between the arms lies in one band (y 61-147) that also differs between two
captures of the SAME control ELF, and the rest of the frame is byte-identical.

Three things the queue could not be built without. Each was found the hard way.

- **Every loop over "the copy-pool sides" had to lose its 2.**
  `StaPipQBuffer::deallocateDynamicData` asked only sides 0 and 1 whether a
  slot's arrays were pooled. With four sides, a slot filled from side 2 or 3 was
  `delete[]`d while the pool still owned it, and the next copy landed in whatever
  the heap had handed out since. On this fixture that was the retained command
  cache, so VIF1 met vertex floats where DMA tags belonged (PCSX2:
  `Unknown VifCmd 3f`). A chain validator walked in `sendPacket` found it: the
  control's 6-quadword retained block (CNT header plus three REFs) had become
  six vertices.
- **Uniforms go inline.** For a non-batched bag, `sendObjectData` REF'd the
  MVP, the light matrix and directions, and the single colour. That was safe
  only while the chain was read before the next bag rewrote that storage. With a
  queue it is not, so they are copied into the packet exactly as bounded batching
  already did. That copy is the +0.23 ms of `prepare` at night.
- **The interrupt-driven variant crashes a real PS2.** Starting the next chain
  from a DMAC VIF1 completion handler (`TYRA_VIF1_QUEUE_ISR 1`) ran clean in
  PCSX2 and took an EE exception on the console twice, both times at the first
  instruction after `EIntr()`. The first time it had `ExitHandler()` (`ei`
  inside the handler, which let a nested interrupt corrupt the interrupted
  thread's `s0`). The second time, without it, it was an instruction fetch at
  address 0 with `Status.EXL` set, i.e. inside the kernel's interrupt path. It is
  off. What it would add over the shipped form is only the gap between one
  chain's end and the EE's next visit.

**Round two, same day: one write-back for several chains, and a deeper queue
refuted.** A chain only has to see memory as it was when the DMAC starts reading
it, so `TYRA_VIF1_QUEUE_LAZY_FLUSH` (on) moves the `FlushCache(0)` from every
submit into `Vif1Queue::start`. It runs only when the chain being started was
submitted after the last write-back, and that one write-back then covers every
chain queued behind it. Physical PS2, same fixture, base booted twice (floor
**0.003 ms**), delta against the depth-4 queue above:

| arm | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| lazy flush | **-0.471** | **-0.507** | **-0.259** | **-0.277** |
| depth 8 | +0.375 | +0.187 | +0.326 | +0.407 |
| depth 8 + lazy flush | -0.113 | -0.188 | +0.249 | +0.202 |

With the lazy flush the `dma` bracket falls 0.13-0.35 ms and `vif_wait` gives
back 0.13-0.31 ms of it. VU1 is now behind more often, which is the expected
direction. Garage night's `total_ms` goes 20.33 -> 20.21, the pose sitting on
the rung. **Depth 8 is worse**, and the whole cost is in `prepare`
(+0.26..+0.50 ms): twice the packet buffers and copy-pool sides means twice the
memory the EE touches per frame, i.e. D-cache and TLB pressure, not DMA. Four
stays.

Every other VIF1 user now calls `Vif1Queue::drain()` before touching the
channel. That includes the generated game's projected-shadow `projClamp`
barrier, which is template code, so a committed example's `terrain_game.cpp` is
current only after its next build regenerates it. A plain `dma_channel_wait`
can return between two queued chains.

## The HUD on VIF1 - MEASURED on hardware, 2026-09-24

The attribution arm put the 2D region at 2.6-2.8 ms in every pose. It opened
with `Renderer2D`'s once-a-frame `sync.align3D()`: with the VIF1 queue that is
the EE waiting for VU1 and the GS to finish the WHOLE 3D frame before the first
sprite. After it, every sprite was its own PATH3 send. The commercial 60 Hz
reference (docs/emulator-captures.md) ends its VIF1 traffic with the HUD as one
`DIRECT` block instead: in order after the 3D, and nothing waits for it.

**`TYRA_2D_VIF1_DIRECT`** (`renderer_core_2d.hpp`, on) does the same once VU1
is up. `RendererCore2D` still builds each sprite's GIF packet exactly as
before, but appends it to a VIF1 chain as a CNT tag carrying `NOP` +
`DIRECT(qwc)`, and the chain goes to `Vif1Queue` behind the 3D chains. The
chain opens with `FLUSHA`, so VIF1 itself holds the sprites until the VU1
program before them has ended and PATH1/2/3 are idle. That is the ordering the
drain bought: a sprite stamps z = max across its rect, so a late scene
triangle behind it would z-fail. `FLUSHA` rather than `FLUSH` also covers a
texture upload a sprite needed. The CLAMP-to-REPEAT restore the drain also
did now rides the chain's head. The 0 arm is the stock path, unchanged.

Two rules came with it, because sprites are now undrawn for a while after
`Renderer2D::render` returns:

- **Every GIF-channel (PATH3) send calls `path3Fence()` first**
  (`renderer/core/paths/path3/path3_fence.hpp`). While a 2D chain is pending
  it submits the chain, waits for it in the queue, then waits for VIF1's FIFO
  to empty. Without that, a texture upload could overwrite a texture a queued
  sprite still samples, a CLAMP or ALPHA write would change their state, and a
  capture or flip would see the frame without them. When nothing is pending it
  is one load and a branch. `RendererCore::endFrame` fences first thing: with
  no DMAC interrupt nothing starts a queued chain during the vsync wait.
- **An open chain is submitted by whoever touches VIF1 next.**
  `Vif1Queue::setOpenChainCloser` makes the next `submit()` or `drain()`
  submit it first, so 3D drawn after 2D in the same frame still lands after it.

Physical PS2, `--profile quiet-debug` Motor District timing fixture (hybrid
colour depth), control booted twice (floor **0.016 ms** of `work`), candidate
booted twice:

| pose | control `work` | HUD on VIF1 (boot 1 / 2) | delta |
| --- | ---: | ---: | ---: |
| garage day | 14.492 | 14.039 / 14.042 | **-0.45** |
| garage night | 18.686 | 18.321 / 18.285 | **-0.37 / -0.40** |
| outer day | 10.254 | 9.770 / 9.755 | **-0.48 / -0.50** |
| outer night | 11.745 | 11.261 / 11.254 | **-0.48 / -0.49** |

`submit` falls 0.42-0.67 ms and `finish` rises 0.06-0.18 ms. The wait did not
vanish, it moved to the `endFrame` fence, after the EE has built the whole 2D
pass while the GS was still drawing the 3D. Triangles and flushes are identical
(garage night twinkles). `total_ms` stays on the 20 ms rung, so this is
headroom, not a new rung. PCSX2 captures of poses 0 and 2 differ from the
control only inside the debug text block, by as many pixels as two captures of
the control differ from each other.

## The GPU-only frame - MEASURED on hardware, 2026-09-24

How long do VU1 and the GS need for a frame when nothing makes them wait for
the EE? That number caps what any EE/GPU overlap can win, including a
frame-pipelined engine (next section).

**The probe:** `TYRA_VIF1_QUEUE_HOLD` (`vif1_queue.hpp`, 0 by default, never
shipped) makes `Vif1Queue::submit` only queue. No chain starts until the EE
first waits, which in a frame without a mid-frame barrier is the `endFrame`
fence, so the frame's whole VIF1 load runs back to back with the EE idle.
`endFrame` then times it to the GS FINISH and logs a 30-frame mean
(`GPUHOLD us ...`).

- A frame with a barrier in the middle (VU1 program-set swap, texture upload)
  releases more than once. Each release opens a segment that closes when the
  wait that released it sees the queue idle; `align3D` adds its GS tail to
  FINISH. The segments are summed.
- The probe needs `TYRA_VIF1_QUEUE_DEPTH` above the frame's chain count.
  **80, not 128**: 128 packet buffers and copy-pool sides threw
  `std::bad_alloc` on the physical PS2 when the fixture entered outer night.
- It serialises the frame on purpose, so the arm's `work` means nothing.

Physical PS2, `--profile quiet-debug` Motor District timing fixture (hybrid
colour depth, HEAD 5dc4220a plus the probe), two boots agreeing to 5 us. The EE
column is the HUD-on-VIF1 arm of the round above:

| pose | GPU only (VU1 + GS) | chains | releases | EE `work` | GPU / EE |
| --- | ---: | ---: | ---: | ---: | ---: |
| garage day | **5.96 ms** | 57 | 1 | 14.04 | 42% |
| garage night | **7.73 ms** | 75 | 4 | 18.30 | 42% |
| outer day | **3.94 ms** | 33 | 1 | 9.76 | 40% |
| outer night | **4.22 ms** | 37 | 2 | 11.26 | 37% |

What it does NOT count:
- GS work sent over PATH3 outside the held chains: the clear, the hybrid
  blit, post fx, and whatever a light pass draws over PATH3 between barriers.
- The GS tail behind a mid-frame segment that no `align3D` closed.

Both omissions can only make the GPU column bigger, so the night rows are
floors. Garage day also has a late stretch of 8.2 ms blocks (60-63 chains, 2
releases) once the recording window is over; the median ignores it.

**The frame is EE-bound about two to one.** The GPU needs 4-8 ms and the EE
10-18. A perfect overlap - one chain, frame N built while the GPU draws N-1 -
therefore removes only the EE's waiting (`vif_wait` 0.8-2.4 ms plus the
`endFrame` fence), not its work: roughly 2.5-3.5 ms in the garage, less
outside. The larger prize is the EE's own computation (`prepare`, `bounds`,
`dispatch`, the game's object loop). The reference title's "one chain" matters
less than what it implies there: about 16 qwords of uniforms per object plus a
`CALL` into a prebaked block, i.e. almost no EE work per draw.

## A frame-pipelined engine ("TyraX2") - considered 2026-09-24, not now

The reference title builds its whole 3D frame as one chain while the GPU draws
the previous one. Building one chain is easy. Making it pay is not.

**What one chain requires of this engine:**
- **Everything a chain REFs lives until the DMA reads it.** With the queue that
  is about three packets; with a pipelined frame it is two frames. Uniforms
  (MVP, lights, colour) are rewritten per bag, the qbuffer copy pools rotate
  with the packets, and arrays are rewritten in place (`contentVersion`,
  `BagArray`). All of it needs frame-lifetime storage. Going from 2 to 4 packet
  buffers already exposed one such bug (the pool-side test).
- **VU1 must not wait for the build.** Building the whole frame and then sending
  it serialises EE and GPU, which is worse than today. Extending a running chain
  (END rewritten to NEXT) froze a console before. So a pipeline is the only
  form that pays.
- **Mid-frame PATH3 has to join the chain or leave the frame:**
  - texture uploads between bags (`beforeTextureMutation`);
  - CLAMP/ALPHA writes;
  - env map, shadow map, alpha mask and BLSS redirects;
  - mid-frame post fx;
  - dynpip and mcpip's direct sends.

  Tyra is immediate-mode, so this is an architecture change, not a function.
- **Build it where the reference does:** in the scratchpad, moved to RAM by
  DMA, which needs no `FlushCache`. A bigger per-frame buffer in cached RAM
  pays in `prepare`, as depth 8 did.
- **A chain validator from day one.** One bad tag in a 4 000-tag chain is a
  silent freeze.

**Risks that stay even if it is implemented well:**
1. The prize is bounded by the table above: max(EE, GPU) with the EE on top
   does not move the EE.
2. +1 frame of input latency: +16-20 ms at 50/60 Hz, +33-40 at 25/30 Hz.
   Sampling the pad as late as possible reduces it, not to zero.
3. VRAM: a texture must stay resident until the GPU has drawn the frame BEFORE
   the one that evicts it, and uploads must ride the chain in order. With 4 MB
   and paging that means less room or more uploads.
4. A new class of silent bug: data changed after it was submitted. Every user
   script, custom node, custom VU program or post fx that edits an array after
   `render()` gets console-only garbage. It has to be impossible by
   construction (const views, generation counters, debug-build checks), not
   documented.
5. Anything that reads a GPU result in the same frame (captures, VRAM reads)
   drains the pipeline and loses that frame's win.
6. A worst-case frame must not overflow a fixed chain or data arena without a
   fallback. Streaming, the spawn pool and particles vary it. Memory per frame
   doubles.
7. PCSX2 emulates no data cache, so the write-back and SPR-to-RAM half is only
   testable on a console, and faults show up a frame after their cause.
8. Every pipeline has to move: StaPip's program families, dynpip, mcpip,
   splitview, BLSS, the alpha mask, shadow volumes and the frame warp. Two
   engines have to be maintained until then.

**Universality.** This is how commercial PS2 engines of every genre worked,
open world and first-person included. A racing game is its easiest case: a
known track and a known set of objects. An open world adds streaming unloads
that must wait two frames (the baked arenas' graveyard, generalised), more VRAM
paging (risk 3), and a chain size that swings with the scene (risk 6). A
first-person game feels risk 2 more when aiming. Game-side ray casts, physics
and AI are EE work and do not change. What gets harder is this editor's API:
scripts, debug draw, custom post fx and VU programs assume immediate mode.

**What happens to user-authored code.** What decides it is whether the code
RECORDS commands, CHANGES data the GPU reads later, or READS a GPU result.
Most of it survives with its API intact:

- **VU programs** (`vu::Program`, docs/vu-authoring.md) do not change. A body
  is a pure function of the vertex and the object's `c.params`/`c.time`. In a
  frame chain it is just another `MSCAL` address beside the bag, and its
  parameters go into the per-object uniform block the EE builds anyway, with
  their values taken at build time. `activeAtBoot` / `vuscript::activate`,
  `shellPass` (one more chain entry) and `movesGeometry` behave as now. A
  program-set swap becomes an `MPG` upload in the chain instead of today's
  drain, which is a gain.
- **VU0 kernels** do not change. `run()` blocks on the EE and sits outside
  the GPU pipeline; a result meant for rendering goes into the frame arena
  before submission like any other data.
- **Custom screen effects** (`.screenfx`, docs/custom-screen-effects.md) can
  keep the exact API. An effect already appends GS primitives to a buffer (`q`,
  `fx.blit`, `fx.flatQuad`), i.e. it records. The same code would append to the
  frame chain as `DIRECT` data, as the HUD does since 1.126.1. It names only
  VRAM addresses and bakes its parameters into the qwords, so running a frame
  later is correct.
- **Object scripts and custom flow nodes** (docs/object-scripts.md,
  docs/custom-flow-nodes.md) keep their game logic: pad, objects, teleport,
  saves, menus and audio are EE work. Per-object fields such as
  `self->data.color` are gathered into uniforms at build time and are fine.
  What breaks is **editing vertex arrays in place**. The GPU reads them a frame
  later, so the replacement is a copy-on-write handle (for example
  `mesh.writeVertices()` returning a fresh frame-arena buffer, swapped in at the
  next submission): the same function, one line different. `BagArray`'s const
  `data()` and `contentVersion` are already half of that model.
- **Reading a GPU result in the same frame** (captures, VRAM reads, EE
  decisions from what the GS drew) gets one of two answers: the result a frame
  later, or an explicit pipeline sync that costs that frame's win.
- **A compatibility mode**, so nothing old stops working. Immediate-mode calls
  (raw `ctx.engine->renderer` use, debug draw, the old mesh API) sync the
  pipeline and run as before. The frame profiler names the script and the
  milliseconds that sync cost, so old code runs on day one, slower, and the
  author can see what to port.
- **Detection instead of documentation.** In a debug build the engine
  checksums each buffer at submission and checks it again before the arena
  side is reused. A mismatch is a write after submit, logged with the buffer's
  owner. The console cannot write-protect it, but every debug run can catch
  it.

Net: VU programs, VU0 kernels and screen effects stay as they are. Script
logic stays. Geometry-editing scripts need a small port. Same-frame GPU reads
become asynchronous or pay for a sync. Raw renderer access keeps working
through the compatibility mode at a visible cost.

**If it is ever done, in this order**, each step measurable on its own:
1. A frame arena for packets and copy pools, with chains still submitted as
   now. This removes the buffer-reuse `vif_wait`.
2. PATH3 mutations into the chain (as the HUD already is) or out of the frame.
3. The N / N-1 pipeline.

## The EE's own work, round one: the sprite texture lookup - MEASURED, 2026-09-24

The GPU-only measurement above moved the focus to the EE's computation. A fresh
attribution arm on HEAD (after the HUD moved to VIF1) still put `dmHud` at
1.79-1.86 ms, and none of that was waiting any more. A temporary split inside
`Renderer2D::render` (85 sprites a frame, physical PS2) named it:

| part of a sprite draw | ms per frame |
| --- | ---: |
| `TextureRepository::getBySpriteId` | **0.97** |
| building the GIF packet and appending it to the chain | 0.53 |
| `useTexture` | 0.14 |
| `note2dRect` | 0.05 |
| total | 1.73 |

`getBySpriteId` means "the first texture whose link list contains this id",
found by walking every texture in the repository and each one's link vector.
Motor District's repository also holds every scene material, so each sprite
paid about 11 us for that walk.

**Fix (1.126.2):** `TextureRepository::findLinked` puts a direct-mapped
256-entry cache in front of the walk, shared by `getBySpriteId` and
`getByMeshMaterialId`. An entry is valid for one `Texture::linkGeneration`.
Every `addLink`/`removeLink*`, texture destruction, repository add/remove and
`getAll()` (which hands the list out for editing) bumps that counter, so the
cached answer is always the one the walk would give. The generation stayed at
70 for the whole run: links are made at load time, not per frame.

Physical PS2, same timing fixture, control booted twice (floor **0.010 ms**),
candidate booted twice:

| pose | control `work` | cache (boot 1 / 2) | delta |
| --- | ---: | ---: | ---: |
| garage day | 14.025 | 12.976 / 12.958 | **-1.05 / -1.07** |
| garage night | 18.318 | 17.505 / 17.512 | **-0.81** |
| outer day | 9.744 | 8.795 / 8.770 | **-0.95 / -0.97** |
| outer night | 11.240 | 10.296 / 10.271 | **-0.94 / -0.97** |

The split with the cache on: lookup 0.02 ms, whole 2D pass 0.60-0.63 ms.
`useTexture` and `note2dRect` also got cheaper (0.14 to 0.12, 0.05 to 0.03):
the walk had been evicting the data cache under them. PCSX2 captures of poses
0 and 2 are byte-identical to the control whenever the FPS text matches.

### Round two: the per-bag texture work - MEASURED, 2026-09-24

`spTex` (the texture part of StaPip's `prepare`) was 0.45 ms in the day poses
and 1.50 ms in garage night. It held two unrelated costs.

**Two resident-list scans per textured bag.** The batching test
(`getAllocatedBuffersByTextureId`) and `useTexture` each walked the whole
resident list for the bag's texture. `Texture::residentHint` now remembers the
entry's index and is trusted only when that entry still carries the texture's
id. Allocation ids are unique in the list, so a hit IS the scan's answer.
`RendererCoreTexture::isResident` replaces the batching test's scan. Physical
PS2, against the lookup-cache arm (floor 0.005 ms): work -0.09 / -0.09 /
-0.03 / -0.05 ms.

**The EE waiting at a wrap switch.** A probe inside the lazy CLAMP bracket put
**3 switches and 1.28 ms of `align3D` a frame** in garage night (lamp pools
and projected shadows sample clamped render targets), 1 switch and 0.23 ms in
outer night, and nothing in the day poses. Each switch made the EE wait for
the whole 3D frame so far. The write now rides the bag's own chain, like the
HUD's. `StaPipCore` names every bag's wrap (`setBagWrap`), and
`sendObjectData` emits `FLUSH` (VU1 done and PATH1 idle, so the previous bag's
last primitives are drawn with the old wrap) plus `DIRECT` A+D `CLAMP_1` ahead
of the bag's uniforms, when the wrap differs from the one in force. The GS
wrap cache moves when the packet carrying the write is submitted. A packet
thrown away unsent (a wholly culled bag) takes its write with it, so the cache
never claims a wrap the GS will not get. Every PATH3 consumer that relies on
the wrap already drains first (alpha mask, shadow map, env map, BLSS, post
fx), so a write still queued lands before them.

Physical PS2, against the resident-hint arm (floor 0.001 ms), two boots each:

| pose | control `work` | in-chain wrap | delta | `vif_wait` |
| --- | ---: | ---: | ---: | ---: |
| garage day | 12.902 | 12.847 / 12.858 | -0.05 / -0.04 | +0.10 |
| garage night | 17.417 | 17.136 / 17.121 | **-0.28 / -0.30** | +0.75 |
| outer day | 8.737 | 8.788 / 8.782 | +0.05 / +0.05 | 0.00 |
| outer night | 10.220 | 10.126 / 10.123 | **-0.09 / -0.10** | 0.00 |

Garage night got back 0.3 of the 1.28 ms. The rest became `vif_wait`: without
the drain the EE runs ahead into a full queue and waits there for VU1 instead.
That wait is the GPU-bound stretch of that pose, and it is only an EE loss
because nothing else is queued for the EE to do. The day poses have no switch
at all, so their +-0.05 is code layout between two builds, not this change.
PCSX2 night captures (poses 1 and 3) differ from the control only in the
debug HUD's frame-time digits.

### Round three: object data without packet2's per-call cost - MEASURED, 2026-09-24

`spObjData` (`sendObjectData`, the per-bag uniforms) cost 1.17 ms for 75 bags
in garage day: ~14 us a bag for ~40-60 qwords. A section split on the console
(temporary tick marks) found that the data was not the cost. The FLUSHE tag
section, one qword, took ~1.5 us a bag, and so did the one-qword ALPHA unpack.
Each packet2 open/close does the DMA-tag bitfield writes, a back-patched
VIFcode and a handful of asserts, and every `packet2_add_float` reloads
`packet->next` through memory.

**Cache misses were measured and are the smaller half.** 16 qwords into cold
packet lines took 1.0 us; into a hot static buffer, 0.17 us. That is ~60
cycles a line, about 20% of the bag's cost. `pref` 1 KB ahead of the packet
cursor won nothing and cost 60 us a frame: the EE keeps one miss outstanding,
so prefetch only moves the stall.

**The fix (1.127.2).** Each uniform block is now its header qword plus
whole-qword copies. That covers FLUSHE, MVP, the light matrix, directions and
colours, the spot quads, custom VU params, single colour, billboard and env
bases, and ALPHA. The header (CNT tag, STCYCL, UNPACK V4_32) for each (VU
address, length) is produced ONCE by packet2 itself into a scratch packet and
remembered, so the bytes are packet2's by construction. The VIF-hash gate
confirmed it: 24 of 24 frames with identical VIFcode, uniform-payload, pool,
`chainQw` and word hashes against the previous HEAD. The options block
(texture, CLUT, LOD, TEST) and the retained clip block keep their old path.

Physical PS2, same timing fixture, control booted twice (floor 0.006 ms),
candidate booted twice:

| pose | `work` | `prepare` | `bounds` (untouched) |
| --- | ---: | ---: | ---: |
| garage day | -0.22 / -0.24 | -0.13 | +0.16 |
| garage night | -0.30 / -0.31 | -0.17 | |
| outer day | -0.32 / -0.32 | -0.06 | +0.04 |
| outer night | -0.35 / -0.36 | -0.08 | |

**Read the per-bracket numbers against the layout noise, not against the
floor.** Two builds differing only in code the fixture never runs have moved
`bounds` and `prepare` by up to ~0.17 ms on this console (docs/roads.md,
"Holes in the road"). The repeatability floor is between two boots of ONE
ELF, and it says nothing about that. This round's saving that can be pinned
on the change is `prepare`'s 0.06-0.17 ms. The rest of `work`'s drop is in
brackets the change does not touch.

**The options block followed (1.127.3):** TEX1, TEST and TEX0 are the same
`GS_SET_*` words built as qwords, one cached header plus a copy. The VIF-hash
gate was 24/24 identical again. On the PS2, against 1.127.2, two boots each:
`work` -0.08 / -0.09 / -0.09 / -0.09 ms (garage day, garage night, outer day,
outer night), `prepare` -0.03 in the garage and `bounds` flat, so the layout
did not move this time.

**The geometry side was NOT redone, on purpose.** Retained command blocks
already replay a package's commands as a memcpy (packet construction is 0.21
ms of the garage-day frame), so the per-buffer packet2 cost that this round
removed from the uniforms was taken out of the geometry path earlier.

### Round four: one submission for every light beam - MEASURED, 2026-09-25

Splitting `rsLightFx` on the console (garage night, 2.1 ms after the in-chain
wrap) gave light pools 1.24 ms, beams 0.86 ms, and projected and blob shadows
around 0.01 ms. A beam was two StaPip submissions, a 6-vertex corona and a
24-vertex cone, and each paid the whole per-bag path for a quad.

**The change (generated game, 1.127.4).** Every visible corona goes into one
bag and every cone into another, per call of `updateAndRenderLightBeams`. The
per-lamp brightness that rode each bag's additive FIX now scales the vertex
colours, and the batch draws at FIX 128: Cs*k*128/128 + Cd is the same light.
Each call in a frame (the main view, every portal view) gets its own slot,
because the first call's chain REFs its arrays and the next may not refill them
before VIF1 has read them. `loop()` restarts the slots each frame, and
`endFrame` has drained VIF1 by then.

**The first version was worse outdoors.** One bag for every lamp has one box.
With lamps behind the camera inside it, the bag is always partial, and those
lamps' packages went to the clip route instead of dropping out on their own
box. So a lamp now joins the batch only if a sphere around it (0.8 R, which
covers the corona's pull toward the camera and the shaft) is inside the
current view's frustum planes. That test is EE-side, 6 dot products a lamp.

**Measured with ONE ELF, the path chosen at boot by a host file.** The
earlier two-ELF A/B of the same change read +0.37 ms in garage day, where the
change draws nothing: that was code layout. Physical PS2, `work`, legacy path
booted twice (drift 0.003 ms), batch path booted twice:

| pose | batch, no cull | batch + frustum test |
| --- | ---: | ---: |
| garage day | +0.006 / +0.002 | +0.006 / +0.002 |
| garage night | -0.15 / -0.14 | **-0.27 / -0.26** |
| outer day | +0.05 / +0.04 | +0.009 / +0.008 |
| outer night | +0.20 / +0.20 | **-0.21 / -0.20** |

PCSX2 night captures show the same coronas, shafts and pools. Night frames
flicker, so the comparison is by eye and by self-noise, not byte for byte.

### How to A/B a change to GAME code: one ELF, toggled at boot

A two-ELF comparison of generated-game code moves more than the change. Code
layout between two builds of the whole game moved `work` by ~0.4 ms here
(and ~0.17 ms in single engine brackets, "Round three"). The repeatability
floor, two boots of one ELF, cannot see that. For game-side changes:
- keep the old function beside the new one in a copy of the fixture;
- read a flag once at scene load from a `bin/` file (`fopen` over `host:`);
- boot the same ELF with and without the file, and log which path ran;
- compare the boots. The recipe lives in the tyra-vq rig as
  `beam_toggle.py` / `beamT-series.sh`.

### Round five: the shared clip block by REF - MEASURED, 2026-09-25

A fresh attribution put `sendObjectData` at ~12 us a bag even after round
three. A second section split showed that sections doing no work at all cost
~1 us a bag: comparing the wrap, resetting the packet. That is data-cache
misses on scattered per-bag state (bag, info, texture, lights, GS), because
the EE's 8 KB D-cache is flushed by the rest of the frame. Writing packet
lines is the other half (16 cold qwords: 1.0 us). So what is left is not
arithmetic. Only NOT building, and NOT writing, the block helps.

The first piece is the 15-qword VU1 clip block (near/far constants and the
six guard-band planes), identical for every bag and copied into every
packet: 193 us of garage day's 75 bags. It is now kept once more as a plain
VIF stream, with its CNT tags' DMA halves turned into VIF NOPs, and each bag
REFs that one copy (`emitRef`, `chainToVifStream`). The copy is rewritten
only after a `Vif1Queue::drain()`, because chains in flight REF it.

VIF-hash gate: `ctrl` and `uni` identical in 24/24 frames (the decoder drops
NOPs, so the added ones do not count). Physical PS2, **one ELF toggled at
boot**, drift <= 0.009 ms:

| pose | `work` | `prepare` | `vif_wait` |
| --- | ---: | ---: | ---: |
| garage day | -0.08 / -0.09 | -0.11 | +0.05 |
| garage night | -0.13 / -0.13 | -0.13 | +0.05 |
| outer day | -0.04 / -0.03 | -0.05 | +0.01 |
| outer night | -0.04 / -0.03 | -0.06 | +0.03 |

**`vif_wait` rising is the finding worth keeping.** In the garage, part of
every EE saving now turns into the EE waiting in a full queue for VU1. EE-side
savings there are worth less than they measure in `prepare` until the queue
or the submission order changes.

### Where the EE waits, pass by pass - MEASURED, 2026-09-25

Is it worth making any EE work cheaper? An EE saving counts in full only
where the EE is not about to wait for VU1 anyway. So the probe below measures
WHERE `vif_wait` happens. `Vif1Queue::waitFor`, `drain` and the back-pressure
loop in `submit` add their spin ticks to one global counter, whoever the
caller is. The fixture's render-pass brackets each sum how much it grew. Rig:
`waitsplit_probe.py`, `attr5-series.sh` in the working notes.

Physical PS2, the attribution fixture (hybrid colour depth), ms per frame:

| pass | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| roads | **0.89** | **0.90** | **0.66** | **0.64** |
| static batches | 0.66 | 0.70 | 0.17 | 0.15 |
| terrain | 0.43 | 0.63 | 0.01 | 0.16 |
| `endFrame` | 0.21 | 0.19 | 0.17 | 0.09 |
| objects | 0.00 | 0.01 | 0.01 | 0.01 |
| **the whole frame** | 2.19 | 2.43 | 1.02 | 1.04 |

**The GPU-heavy passes and the EE-heavy one run one after another.**
Terrain, batches and roads are EE-cheap and GPU-heavy: whole-bag baked
replays, one REF each. The EE fills the four-chain queue and then waits.
Objects are the opposite (2.3 ms of EE in the garage, next to no wait), so
EE savings in the object loop count in full, including `prepare`. This also
explains why round five's `vif_wait` rose: those saved microseconds reached
the next GPU-heavy stretch sooner.

**Interleaving them, prototyped.** The terrain, batch and road bags of the
main pass are deferred into a list and fed into the object loop, spread evenly
over the drawn objects and flushed after it. Game code only, one ELF toggled
at boot (`drip_probe.py`). `work`, two control boots:

| arm | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| inside the object submission batch | **-0.29 / -0.32** | **-0.35 / -0.41** | +0.40 / +0.33 | +0.22 / +0.23 |
| batch closed around every drip | +0.25 | +0.22 | +0.31 | +0.14 |

- It works where the table above says it should: in the garage, `vif_wait`
  falls 0.19-0.31 ms and `dispatch` 0.8-0.9 ms.
- It costs `prepare` +0.42..+0.49 ms in every pose, and in the outer poses
  more than it saves. There, terrain is EE-heavy too (1.97 ms of EE and
  0.01 ms of wait), so deferring it only moves EE work around.
- Closing the object batch around each drip costs more than it saves.
- The +0.42 ms of `prepare` is not extra instructions. The same arm under the
  attribution build in PCSX2, which emulates no data cache, moves `prepare` by
  +0.03 ms (`spTex`: the batch-candidate residency and wrap checks the deferred
  bags now take) and `dsDirect` by -0.10 ms. On the console the rest is
  therefore D-cache (or I-cache) cost from alternating two working sets, and
  only a console arm can price it.

**Not shipped.** The next arm to run defers only the roads and batches, and
prices the `prepare` increase with the attribution build. Order is also a
correctness question: an alpha-blended object drawn before the terrain behind
it blends with the sky instead, so a shipped version must flush the deferred
list before the first object that may be translucent.

**A hazard found on the way.** An adaptive variant asked the queue how many
chains were pending (a probe-only `Vif1Queue::pending()` that advances the
queue, called from the object loop inside an open submission batch). It hung
the physical PS2 on its first boot: the IOP spammed `freepad: DMA Busy`, and
`ps2client reset` could not recover it. The cause was not isolated. Game code
must not drive the queue.

## Order of work

1. ~~Probe A and Probe B.~~ **DONE, on hardware, 2026-09-16.** S3 does not ship;
   the recoverable part of the `send_packet2` bracket is 0.84 ms.
2. ~~The triangle budget.~~ **RUN, on hardware, 2026-09-16.** The road lateral
   budget shipped (−0.358 / −0.591 ms; −31.6% of the district's road
   triangles with the surface and every seam exactly unchanged). Mesh LOD is
   refuted with a mechanism. Terrain LOD 160 is a measured −0.38 / −0.45 ms
   with its two quality risks bounded in world units, and owes one drive across
   the band before it is authored.
3. ~~**S4, the shared reflection probe.**~~ **BUILT, 2026-09-16, as a reuse
   budget** ([reflective-materials.md](reflective-materials.md), "The reuse
   budget";
   [evidence](../examples/vehicle-playground/authoring/reflection-probe-2026-09-16/README.md)).
   The probe skips its cadence beat while nothing that feeds the capture has
   moved, with the staleness bounded in **pixels of its own 128-pixel target**
   rather than in frames. Measured in PCSX2 as a capture rate and converted
   through the road round's 4.14 ms per capture: **−2.07 ms parked, −1.04 ms
   driving straight or turning at 20 deg/s, and NOTHING in a 90 deg/s turn** —
   which is the right shape, because a hard turn is when a stale reflection
   would be seen. The capture-basis work is used rather than undone: the
   retained basis is the thing the gate compares against. **It owes a hardware
   A/B**; the counts settled the design, the console owes the milliseconds.
   Of the other two options, "fewer objects" is worth ~0 triangles in the pose
   that is slow, and "a coarser LOD for the probe pass" needs the models
   re-baked with tiers and a second resident bag set per reflected part — it is
   on the backlog with the mechanism.
4. **The baked VIF stream.** **Built and measured in PCSX2, 2026-09-16:**
   one DMA `REF` per bag takes the chain from 8 221 to 5 784 quadwords a
   garage-day frame (−29.6%) and packet flushes from 120 to 109, with the
   VIFcode and absolute-uniform hashes bit-for-bit identical and the captures
   unchanged. It keeps the per-package visibility test Probe A says it cannot
   drop, and it is gated by [the acceptance gate](baked-stream-acceptance-gate.md)
   rather than by the counters — which is what stopped the spike, and what the
   moved flush count proves. **Hardware milliseconds outstanding**, and they
   **MEASURED ON HARDWARE, 2026-09-17: garage-day `work` falls 1.287 ms,
   about a fifth of what this page budgeted**, against a 0.012 ms floor,
   and garage-day `total_ms` does not move at all — 39.960 in every arm,
   the whole saving absorbed by `present`. Garage night is the exception and
   it is real: the control alternates 43.877 / 43.044, off the rung and
   juddering, where the candidate is a flat 39.959. **Stutter removed,
   milliseconds not.** Then the adversarial verify mode blocked it: the key
   holds each array's
   So the open question is not "fix the key" but **"does 1.287 ms earn a
   contract worth roughly 110 unenforced obligations in the generated game, whose
   failure mode is stale lighting visible only while the camera moves?"**

   **ANSWERED, AND THE PREMISE WAS WRONG — it ships at 1**
   ([bag-content-version.md](bag-content-version.md)). The obligations are not
   unenforced and there are not 110 to remember: the exact census is **280 write
   sites in 42 generated functions behind 108 array declarations**, and **every
   one is generator-emitted** — fixed template text in `src/templates.cpp`, zero
   in other editor sources, zero in checked-in example game sources, and zero
   reachable from user-authored code (`ScriptContext` carries no geometry
   pointer and `objectGeometry` is private). A closed population in one file is
   a **type** problem, not a discipline problem, so the arrays became
   `BagArray<T>`: `data()` is const, every mutation stamps, and a raw write does
   not compile. The 280 sites did not change — they keep their syntax and gain
   the obligation.

   Acceptance, on the arm that found the defect: Motor District under
   `--keep-routes` with the traffic **moving**, **169 843 blocks checked,
   `failed=0`** over ~12 600 frames. Against the control on a parked fixture,
   two real ELFs: every count that describes what is drawn identical to the
   digit (`cull`, `clip`, `guard`, `out`, `strip`, `sexp`, `verts`), the
   captures **byte-identical**, and only the two numbers the change exists to
   move — packet flushes −300 per 50 frames and `chainQw` **−26.0%**.

   Two things the round added beyond the fix. The arm found a **second** caller
   of a different shape (the vehicle paint pass, recomputing a per-vertex
   fresnel from the camera every frame through a `const_cast` past the array),
   which is the argument for running it rather than reasoning about the
   contract. And `STAPIPMISS` now splits `bbox=` from `content=`, so "a mesh
   moved" and "a mesh was re-shaded" stop reading as the same thing — which
   makes the `prepare` +0.315 ms hypothesis above testable for the first time.

   **Still owed: the hardware re-measure.** The −1.287 ms was taken before this
   contract existed; the contract adds one global RMW per mutating call and one
   `u32` to the key, and PCSX2 can price neither.
5. ~~**World visibility** — baked sectors, portals or a PVS.~~ **MEASURED FIRST,
   2026-09-17, and it redirects the front.** "There is still no occlusion
   culling of any kind in a scene made of buildings" is true and it is not a
   reason to build one. The garage-day frame was probed object by object by
   REMOVAL — hide one object, photograph the frame, diff it against the control
   — and **the garage is not overdrawing: it is genuinely visible.** Twelve of
   the fourteen solo objects that submit triangles paint pixels. **Strictly
   occluded: 3 509 triangles, 51 packages, 6 bags — one tower and the pavement
   slab under it**, 10.0% of this commit's 35 061-triangle frame and 7.2% of its
   711 packages. The population a pure occlusion scheme could work on is **two
   objects out of 142**.
   [Evidence](../examples/vehicle-playground/authoring/world-visibility-2026-09-17/README.md).

   **The number that ranks this population is triangles per visible pixel, and
   it did not exist before this round.** The two near towers pay ~0.10; four
   distant objects pay between 2.4 and infinity. **6 532 triangles — 18.6% of
   the frame — buy 327 pixels between them, 0.12% of the screen.** That is a
   representation problem, not a culling one, and it points at item 5's own
   fallback: impostors already ship
   ([impostors.md](impostors.md)), are distance-driven so they work in every
   pose and while driving, and **do not multiply bags** — the card is six
   vertices updated in place, which is the exact failure mode that killed mesh
   LOD twice. A threshold between 48 and 115 units captures every object in that
   bottom row **including both strictly-occluded ones**, because they are the
   most distant things in it. The impostor lever therefore CONTAINS the
   occlusion lever here, at a fraction of the machinery.

   Two things this does NOT establish, and they bound the claim. **Not one
   millisecond** — 3 509 triangles and 51 packages are counts, and the plan's
   own constants (0.4 of the cycle count for a removed triangle; per-bag cost
   that does not shrink) cut against a naive conversion. And **one parked
   pose**, which is the most favourable viewpoint in the scene for occlusion —
   nothing yet says how much of that tower stays hidden while the player
   **drives**, and the same probe set under the motion regimes is the next
   check. Until then the 3 509 triangles are an upper bound for a parked camera.

   **THE THRESHOLD IS BUILT AND PRICED IN PACKAGES, 2026-09-17**
   ([evidence](../examples/vehicle-playground/authoring/impostor-threshold-2026-09-17/README.md)).
   Eight-view impostors for the two building models, assigned to all ten
   instances, switching at **100 units** — a threshold placed in the gap the
   visibility round measured between the furthest building the frame is SEEN to
   draw (82 units, 610 px) and the nearest one it is not (117 units, 0 px).

   | pose | packages | delta | triangles | converted |
   | --- | ---: | ---: | ---: | ---: |
   | garage day | 750.0 → 670.0 | **−80.0 (−10.7%)** | −5 730 | −1.560 ms |
   | garage night | 803.0 → 723.0 | **−80.0 (−10.0%)** | −5 730 | −2.544 ms |
   | outer day | 175.0 → 175.0 | 0 | 0 | 0 |
   | outer night | 192.0 → 192.0 | 0 | 0 | 0 |

   **The millisecond column is a CONVERSION** through this page's own
   19.5 / 31.8 us per package, not a measurement; no console was available for
   this round. Two objects move, in both garage poses: Tower block 06 goes
   50 packages → 1 and Loft block 07 32 → 1. **The strictly-occluded object is
   captured by the distance rule with no occlusion machinery**, which was the
   whole argument for preferring this lever.

   Two corrections this round owes its own predecessor. The claim that a
   threshold captures **both** strictly-occluded objects is **wrong**: the other
   is a box primitive and impostors apply only to static OBJ models, so there is
   no asset to bake. It is 12 triangles and 1 package, so the error is
   numerically trivial and a PVS would still have taken it. And **the outer
   poses gain nothing at all** — the same buildings are 32 and 44 units away
   there and correctly stay full models, so this lever is worth exactly zero
   outside the garage. That is a property of the scene, not a defect of the
   threshold.

   **Driven, not just parked**, at 20 camera stations — 12 along the garage
   approach and 8 orbiting the switched object. **At the garage pose the whole
   80-package saving costs 123 pixels**, 0.05% of the screen, because the object
   it removes is the one measured to contribute nothing. Hysteresis holds the
   card from 117 units down to 90 and the full model returns at 88, so **the pop
   is 5 297 px (2.02% of screen)** in one step — against 173 137 px that the
   same 2 units of camera motion changes anyway, a ratio of 1.00. That ratio
   flatters it: the stations are coarser than one frame of driving, and scaled
   to a frame the swap is nearer **18%** of the change rather than 3%. That
   scaling is an extrapolation.
   **The orbit is the worse of the two pops**: at a constant 110 units the
   8-view card's error swings from 0 to **7 150 px (2.73%)** with azimuth,
   because a card is up to 22.5 degrees off its captured view. 16 views halves
   that angular error and is the obvious move if it is judged too visible.
   Collisions and picking are proved unchanged across all 142 objects in the
   generated scene data. **Nothing appears late** — the card is present wherever
   the model was; what changes is fidelity, not presence.

   **This owes a hardware arm and nothing else.** No console was available; the
   millisecond columns above are conversions through this page's own
   19.5 / 31.8 us, whose caveat (the bracket split does not support a pure
   per-package model) applies in full.
6. S5, the wheel rebake; and terrain LOD 160 once somebody drives the band.
7. **The `FlushCache` hunt (S1).** Dropping it corrupted the picture with the
   packet already uncached, and it is worth 1.09 ms when the cause is found.
   That is the worst value-for-risk here; it stays on the list only because the
   corruption itself is worth understanding.
8. S2 — the uniform bank, once VU1 is the limiter and the drain costs something.

## What this page does not establish

Every millisecond attributed to a future state is a prediction. The 15 ms VU1
estimate is derived from a measured per-cycle rate and a routing mix taken from
a different build, not measured directly. The 0.4 ms redesign figure is an
arithmetic sketch. The measured numbers here are the four-pose table at the top,
the FTCLIP package counts, **the two probes' section and everything it links**,
the **per-object visible-pixel table** behind item 5 (counts and pixels, PCSX2,
with its own repeatability, control-drift and positive controls run before any
zero was read), and the prior results this page cites by link. Everything else
remains a prediction.

One methodological rule this page earned twice over and should not need a third
time: **inventory the view, never the map — and then ask what the view is SEEN
to draw, because those are two different questions as well.** The map-wide road
count promoted the wrong front; the submission inventory that replaced it
promoted item 5 on a population that turned out to be almost entirely visible.
Each step was a real measurement of the wrong quantity.

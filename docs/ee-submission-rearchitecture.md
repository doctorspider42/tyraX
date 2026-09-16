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

Nothing here is implemented. This is a plan, and every number in it that is not
attributed to a measured run is a prediction to be falsified.

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
changes below (S1, S3, S4, S5) plausibly sum to that on their own, before the
architecture is touched. Do them first: they double the day pose, and they clear
the measurement noise (120 cache flushes a frame) that the architectural A/B
will have to be read through.

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
5. **It is checkable by construction.** The picture must be byte-identical and
   the triangle, package and flush counters must not move. That is the same
   acceptance gate the strip and batching work already used.

Two details to design, not discover: the GIF tag in the block encodes `NLOOP`
and the primitive type (both bake-time facts), and the scale quadword carries
`RendererCoreDepth::scale`, which a 16-bit display mode moves — so either bake
per depth or patch one quadword at load.

Predicted EE cost: about 300 visibility units at roughly 400 cycles each, near
**0.4 ms**, against 20.44. Even a three-fold error leaves the direction intact.

## Supporting changes, ranked by value over risk

| # | change | predicted (garage day / night) | risk |
| --- | --- | ---: | --- |
| S1 | Build the frame's chain in the EE scratchpad (16 KB at `0x70000000`, unused by this engine) or in `P2_TYPE_UNCACHED_ACCL` memory, and stop calling `FlushCache` | −2.10 / −2.49 plus the unmeasured refill | low; `packet2_create` already accepts the type |
| S2 | Remove the per-mesh `FLUSHE`: a second uniform bank at VU1 addresses 22..43 and a bank bit in the count word | via VIF1 wait; little until S1 and the redesign land | medium — touches 15 microprograms; sequence it last |
| S3 | Classify per 1/3-bbox part instead of per package | part of the 3.3 ms `Package_classify` measured in the emulator | low, fully reversible |
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

## The two probes that come first

Neither is shippable. Both are cheap, and either can cap the plan.

**Probe A — what is per-package classification actually worth?** Force every
package of a partially visible bag to `IN_FRUSTUM` and skip classification
entirely. This removes EE work and adds VU1 work (922.5 rejected packages a
frame stop being rejected). If the frame does not improve, per-package culling
is already paying for itself and the redesign must keep an equivalent, which
changes its shape. If the frame improves, the rejection is costing more than it
saves and S3 becomes a shipping change rather than a cleanup.

**Probe B — what does the cache flush really cost, including the refill?**
Allocate the static pipeline's two packets as `P2_TYPE_UNCACHED_ACCL` and pass
`flush_cache = false`. Unlike the earlier arm that wedged the console, this one
is legal: the packet is no longer in cached memory, so there is nothing to write
back. The measured delta is the *whole* bill — the 2.10 ms bracket plus the
refill tax that adding flushes could not see.

Both arms must follow this repository's existing rules: fixture regenerated by
the editor that built it, ELF hashes distinct and recorded, fresh boot over
ps2link, 120 warm-up frames then 240 recorded per pose, all four poses, and the
garage-day capture compared for a byte-identical picture (Probe A will *not* be
byte-identical and must be inspected instead).

## Order of work

1. Probe A and Probe B. Decide from evidence whether S3 ships and how much of
   the `send_packet2` bracket is recoverable.
2. S1 — the frame chain out of cached memory. This is also what makes every
   later measurement quieter.
3. S4 and S5 — the two cheap non-pipeline wins, aimed at the 20 ms rung.
4. The baked VIF stream, behind a compile-time switch, with the existing path as
   the A/B fallback and the counters as the correctness gate.
5. S2 — the uniform bank, once VU1 is the limiter and the drain costs something.
6. Re-open the triangle budget: road LOD, authored LOD distances, world
   visibility.

## What this page does not establish

Every millisecond attributed to a future state is a prediction. The 15 ms VU1
estimate is derived from a measured per-cycle rate and a routing mix taken from
a different build, not measured directly. The 0.4 ms redesign figure is an
arithmetic sketch. No arm of this plan has been run. The only measured numbers
here are the four-pose table at the top, the FTCLIP package counts, and the
prior results this page cites by link.

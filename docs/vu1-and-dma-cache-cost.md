# Where the Motor District frame really goes: VU1 arithmetic and DMA cache flushes

Two controlled physical-PS2 experiments price the next two candidate directions for
the vehicle example's frame time: removing the per-submission `FlushCache(0)` that
ps2sdk performs on every DMA send, and reducing the VU1 arithmetic each triangle
costs. The DMA cache direction is bounded at about 2.4 ms of a 50 ms frame and
disappears on its own once submissions are batched further. VU1 arithmetic is
measured, on hardware, at **0.0768 ms of frame time per VU1 cycle per triangle**
in the garage view, and it converts almost entirely into VIF1 wait, so reducing
it pays immediately.

Neither experiment is an integrated change. Both probes were reverted; this page
and [its raw evidence](../examples/vehicle-playground/authoring/vu-cost-dma-cache-2026-09-15/)
are the result.

## Method

An isolated fixture was built from `examples/vehicle-playground` with
`authoring/benchmark-district.py`, its `.res-baked` tree and `bin/aoatlas`,
`bin/aomap` copied in, and `authoring/instrument-frame-cost.py` applied. All 105
PNG/TMDL/MTL files match the September 14 reference manifest byte for byte; the
two AO images are outside that manifest and are additional. Compilation used
`tools/toolchain/native-build.ps1` directly, so no editor build could regenerate
the instrumentation away.

Every arm is a fresh boot over ps2link on physical hardware, native PAL 512x512
32-bit, parked traffic, four 360-frame poses with 120 warm-up frames each and 240
recorded rows per pose. Work means update + submission + finish; included
telemetry buckets overlap and none of these numbers may be inverted into FPS.

**Two repeated controls on one ELF differ by 0.010 ms of render submission**
(40.905 / 40.915 ms, garage day). That is the noise floor every delta below is
read against. A third control on the second ELF reads 40.830 ms, so a rebuild
alone moves the number by under 0.09 ms.

## The DMA cache flush is worth about 2.4 ms, and it is not a fork

`dma_channel_send_packet2(packet, channel, flush_cache)` disassembles to
`FlushCache(0)` — **syscall 100**, a whole-data-cache write-back invalidate —
followed by `dma_channel_send_chain`. The static pipeline issues 131.65 of them
per garage-day frame.

Adding redundant flushes is correctness-neutral by construction, so the arms
below carry no risk and measure the cost directly:

| arm | render submission ms | `send_packet2` bracket ms | work ms |
| --- | ---: | ---: | ---: |
| control (two boots) | 40.905 / 40.915 | 2.357 / 2.359 | 50.437 / 50.188 |
| control, second ELF | 40.830 | 2.326 | 50.654 |
| +1 `FlushCache` per submission | 41.522 | 1.940 | 50.906 |
| +3 `FlushCache` per submission | 42.396 | 1.930 | 51.880 |

The bracket around `dma_channel_send_packet2` itself is **2.358 ms per frame**,
17.9 microseconds per call. That is the ceiling on what removing the flush can
save, plus an unmeasured tax: the cache the flush invalidates has to be refilled
by the bag preparation that follows, and that cost lands in `Bounds`,
`Package_create` and `Packet_build` rather than in the submit bracket. Adding
flushes cannot measure that half — the cache at those sites is already cold — so
the honest statement is *at least 0.61 ms and at most 2.36 ms plus the refill*.

The bracket **falls** to 1.940 ms in the arms with an extra flush, because the
call inside `send_packet2` then finds a clean cache with nothing to write back.
The difference, 3.2 microseconds per call, is the write-back half; each further
flush on an already-clean cache costs 3.3 microseconds (0.437 ms per frame).

### What suppressing the flush proved

An arm passing `flush_cache = false` **hung the console during boot** and wedged
ps2link until a physical Reset. That is a result, not an accident: the memory the
DMA must see coherently is **the packet itself**, written by the EE into cached
memory microseconds before the send. Static model vertex arrays are referenced by
DMA `REF` tags but were written at load time and are long evicted by the warm
frames; the packet never is.

So an explicit-ownership redesign cannot simply drop the flush. It has to give
the packet buffers an owner — allocate them uncached or uncached-accelerated
(`packet2_create` already accepts `P2_TYPE_UNCACHED` and `P2_TYPE_UNCACHED_ACCL`),
or write back their lines by address — and the qbuffer copy pools, which are
written *between* sends, need the same treatment.

### Why ps2sdk offers nothing better, and why that still is not a reason to fork it

ps2sdk is licensed under the **Academic Free License 2.0**: copying, modifying and
redistributing derivative works is permitted, and this repository already vendors
AFL-2.0 PS2SDK code (`tools/toolchain/bin2s`). Forking is allowed.

The two primitives it offers are both wrong for this job:

- `FlushCache(0)` is a kernel syscall that invalidates the **entire** 8 KiB data
  cache, whatever the transfer touched.
- `SyncDCache(start, end)` is not a cheap range operation. `_SyncDCache`
  disassembles to a loop over **all 128 cache indices in both ways**, issuing
  `sync` before and after every `cache` instruction and comparing each tag
  against the requested range. This is why the earlier experiment that
  synchronised each DMA `REF` range separately came out slower than the baseline:
  every call walked the whole cache.

What the pipeline actually wants — a hit-based write-back of the twenty or so
64-byte lines a packet occupies — is about ten instructions and exists in neither
function. That is a real argument for owning the code.

It is still not the lever. The whole bill is bounded by 2.36 ms of a 50 ms frame,
and it is **proportional to the number of submissions**: the
[submission batching work](static-submission-batching.md) already moved garage
day from 156 to 129 sends, and a retained-command redesign that submits ten
chains instead of 131 removes over 90% of this cost without touching the SDK at
all. Fix the submission count, and the flush question answers itself.

## VU1 arithmetic costs 0.0768 ms per cycle per triangle

The shipped assembler is `openvcl`. Its main-loop lengths, counted from the loop
label to the loop branch in the generated `.vsm`, are **cycles per triangle** —
the static pipeline's programs process three vertices per iteration because every
mesh is a triangle list:

| VU1 program | cycles per triangle |
| --- | ---: |
| `cull_tc` (texture, per-vertex colour, dynamic light attenuation) | 133 |
| `cull_c` (colour, dynamic light attenuation) | 130 |
| `cull_tce` (matcap/env) | 109 |
| `cull_td` (texture, directional lights) | 107 |
| `clip_tc` (VU1 clipper) | 269 |

A probe added twelve measured cycles per triangle to a program — an extra
`MatrixMultiplyVertex` per vertex whose result is stored into free VU1 data
memory at quadwords 1016-1018, so the assembler cannot eliminate it and the GIF
packet stays bit-identical. Two arms, each against its own control:

| probe target | garage-day submission | VIF1 wait | predicted at 100% routing |
| --- | ---: | ---: | ---: |
| `cull_td` only | 40.830 → 40.793 ms (**+0.000**) | 6.794 → 6.794 ms | 1.058 ms |
| `cull_tc` + `cull_c` | 40.830 → 41.752 ms (**+0.922**) | 6.794 → 7.688 ms | 1.058 ms |

Three things fall out of that pair.

**The scene's static geometry runs the colour programs, not the lit ones.** The
generated game attaches a lighting bag only to dynamically lit objects
(`part.bag->lighting = part.litBag.get()`); everything else submits with
`lighting = nullptr` and therefore selects `cull_tc` / `cull_c`. Instrumenting
`cull_td` moved nothing at all, which is the cleanest possible confirmation and
also a warning: **a VU1 experiment aimed at the wrong program measures zero and
looks like a null result about VU1.**

**The cycle model is right on hardware.** 26,015 triangles times twelve cycles at
294.912 MHz predicts 1.058 ms; 0.922 ms was measured, 87% of it. The missing 13%
is the geometry that routes through the other programs. The outer-night pose
agrees independently: 12,156 triangles predict 0.495 ms and the wait bucket moved
0.513 ms.

**VU1 is on the critical path, and the cost surfaces as VIF1 wait.** Of the
0.922 ms added, 0.894 ms appeared in `vif_wait_included_ms`. There is no slack to
absorb it, so the converse holds too: arithmetic removed from these programs
comes straight off the frame at the same rate.

### What that rate implies

At 0.922 ms per 12 cycles per triangle, one cycle per triangle is worth
**0.0768 ms per garage-day frame**. `cull_tc`'s 133 cycles are therefore about
**10.2 ms of the 40.8 ms render submission**, leaving roughly 30.6 ms of EE-side
preparation and dispatch.

Two reductions are visible in the source at that rate, and neither needs an EE
redesign first:

- **Triangle lists.** Nothing in `renderer/3d/pipeline` emits a triangle strip;
  every mesh is an unindexed triangle list, so each shared vertex is transformed
  once per triangle that uses it. A strip program transforms roughly one vertex
  per triangle instead of three and emits one output vertex instead of three.
- **Per-vertex dynamic light attenuation.** `CalculateTyraSpotLight` runs per
  vertex inside `cull_tc` and `cull_c`, and is most of the 26-cycle gap between
  them and `cull_td`. Meshes that no dynamic light reaches pay it anyway.

The triangle-strip half is **implemented now** for baked static models - see
[model-pipeline.md](model-pipeline.md), "Triangle strips". It cost zero VU1
instructions (the cull programs' per-vertex ADC judgement is already the right
one for a strip) and it took the district's own models from 13 176 list
vertices to 9 648. In the garage view that is 11.3% of the frame's submitted
vertices and 10.8% of its VU1 packages; it has NOT been measured on hardware,
so there is no millisecond number here yet. The roads and the terrain are grids
and strip much better still, and neither is stripped - that is where the rest
of this paragraph's opportunity remains.

### The spot light half is done (1.94.0), and it was measured here

The colour programs skip `CalculateTyraSpotLight` when the mesh's light is inert
— selected on the sign of a lane the EE already had spare. The full account is in
[flashlight.md](flashlight.md), "The cone costs nothing when nothing is lit".
Rows a path actually executes, same `.o.vsm` method and same assembler (the clip
rows are the COLOUR path, since a colour bag never runs the peer half of a shared
image):

| program | before | unlit mesh | lit mesh |
| --- | ---: | ---: | ---: |
| `cull_c` | 130 | **72** | 130 |
| `cull_tc` | 133 | **73** | 133 |
| `clip_c` | 230 | **173** | 232 |
| `clip_tc` | 241 | **184** | 243 |

Micro memory 1684 → 1862 of 2042.

**This page's rate predicted the first attempt's console result, including its
regression, which is the best evidence the model is right.** That attempt gated
the spot with a branch INSIDE the loop, which cost a *lit* mesh 11 cycles a
triangle of lost instruction pairing, and on hardware it read −1.059 / +1.264 /
−0.620 / −0.701 ms over the four parked poses: garage night, the heaviest pose,
got slower because at night nearly every mesh picks a lamp. 25,090 triangles at
11 cycles predicts 0.93 ms against 1.26 measured. **Two lessons.** The rate
converts a cycle count into frame time well enough to design against — and a
cycle count is only a prediction once you know *which path* the scene takes, so
solve for the routing mix (about 39% unlit in garage day, ~0% in garage night)
before quoting anything. The shipped shape is two whole loops with the lit one
byte-for-byte the original, and it has not been on a console.

## An unrelated finding, RETRACTED — and why

The garage poses recorded **12.17 texture re-uploads per frame** (day) and 5.75
(night) against zero everywhere in the September 14 runs, with `VRAMSTAT`
reporting `freeMB=0.048`, `largestKB=33` and four evictions per frame. That was
read here as "GS VRAM is full in the garage and the scene thrashes it
permanently". **It is not. The fixture was stale.**

`benchmark-district.py` copies the example's **committed** generated sources,
and examples' generated files in this repo drift. Every fixture since
regenerated with the editor under test records **0.000 re-uploads and 0
evictions in all four poses** on the console — and the stale build also carried
**3.6 ms more render submission** than the regenerated one, which is larger than
several of the deltas this page reports as findings.

**A performance fixture built from committed generated sources is measuring a
different game, and nothing in any log says so.** That is the rule to take from
this section; the script's own docstring carries it now, as does the
`tyra-testing` skill. Re-read any number on this page that was taken from a
fixture whose generated sources were not refreshed first.

What survives is not a thrash but a **cliff**: the texture heap at `Pal576i`
32-bit is 196 608 words and the garage holds 84% of it, so opening the pause
menu is enough to reach `freeMB=0.048` and start evicting. The inventory, the
residency census that names what is resident, and the levers are in
[gs-vram.md](gs-vram.md).

Acting on the cliff is **not** a free win either. Quantizing that car body -
the obvious fix, and a real bug, since a project set to 4-bit was silently
shipping a 32-bit car - measured **0.51-0.74 ms SLOWER on every pose**, all of
it in the `finish` bucket. The obvious explanation does not survive its own
evidence: the outer-road poses are **byte-identical** between the two arms yet
show the largest finish delta, so a per-texel CLUT sampling cost cannot be the
whole story and VRAM address layout is the better hypothesis - the body shrank
by 57,280 words and every allocation after it moved. If that holds it applies to
any texture-size change, so treat none of the remaining VRAM levers as free
until a padded arm settles it. The change therefore stays behind the project's
own texture settings rather than shipping unconditionally.


## What the first VU1 reduction actually bought, and where it moved the limiter

The spot-light gate that came out of this page was measured against two
bracketing baseline boots (repeatability 0.007-0.165 ms of render submission):

| pose | baseline | with the gate | delta | VIF1 wait delta |
| --- | ---: | ---: | ---: | ---: |
| garage day | 40.974 | 40.220 | **-0.754** | -1.009 |
| garage night | 44.417 | 44.210 | **-0.207** | -0.502 |
| outer day | 18.449 | 18.040 | **-0.409** | -0.574 |
| outer night | 21.446 | 20.620 | **-0.826** | -0.669 |

**The saving is well under the cycle ceiling, and that is the interesting part.**
`cull_tc` lost 60 cycles per triangle on the unlit path, which the measured rate
prices at 1.81 ms for garage day; 0.75 ms arrived. The first shape of the change
gives the other half of the picture: it removed 52 cycles unlit but *added* 11
lit, and garage night — where the lamps are on and nearly every mesh picks a
light — regressed by 1.264 ms, almost exactly the 0.93 ms those 11 cycles
predict. So the model is right in both directions; what is not linear is how much
of a **reduction** reaches the frame.

That asymmetry locates the next limiter. A packet's cost is the larger of the
EE's work on it and the VU1 side's, and the two overlap across the packet double
buffer. Adding arithmetic pushes more packets over the VU1 side, which is why
additions measured 87% of theory. Removing arithmetic only helps a packet until
VU1 drops below the EE's preparation, after which further cycles are free and
invisible — which is why removing 60 bought 41% of theory.

**It is not the DMA transfer.** That was the obvious suspect: the colour path
uploads three quadwords per vertex — position, ST and colour, 48 bytes — by DMA
reference, and garage day submits 26,010 triangles as an unindexed triangle
list, so 78,030 vertices and 3.75 MB cross VIF1 every frame. A probe re-sent the
position stream to the address it already occupied, adding 16 bytes per vertex
(1.25 MB per frame, a third more payload) while leaving VU1 memory, the
microprogram, the GIF packet and the picture untouched. Garage-day **VIF1 wait
moved 5.828 → 5.895 ms, 0.067 ms**, while EE-side packet construction rose 0.171
and render submission 0.472. The transfer hides completely behind the work on
either side of it; bytes are not what a packet is waiting for, and a
vertex-compression pass would buy nothing on its own.

**The strip half of that has since landed for static models and is measured in
counts, not milliseconds** (model-pipeline.md, "Triangle strips"): the same
garage view submits 76 951 vertices per frame as a list and 68 235 as strips,
in 56 625 VU1 packages against 50 525 per fifty frames. Those are the numbers
this page's owner converts into hardware time; PCSX2 put the median frame work
at 20.3 -> 19.5 ms and render submission at 19.43 -> 18.85 ms, which is
directional and nothing more.

What is left is the EE. Of garage day's 40.2 ms of render submission, 5.8 ms is
VIF1 wait and the rest is preparation: bounds 4.8, per-bag preparation 4.6,
packet construction 3.0, the `send_packet2` bracket 2.4, and about 8 ms of
package creation and classification inside dispatch. **Every one of those scales
with the number of VU1 packages, which scales with the vertex count** — so the
reduction that helps is fewer vertices, and it helps on the EE side more than on
the VU1 side. The static pipeline emits **no triangle strips at all**, so every
shared vertex is packaged, transferred and transformed once per triangle that
uses it; the district's baked models hold only 27.4% unique vertices, and roads
and terrain are grids that strip perfectly.

## The fog gate (1.134.3)

`cull_tc`, the program most of a scene runs through, computed the GS fog
coefficient for every vertex: `CalculateTyraFog`, 6 upper-pipe operations out of
~20. Most geometry never shows fog, though. Everything nearer than the fog start
gets F = 255, so a car, the street it is on and most of a district were paying
for arithmetic whose answer was a constant.

**The gate, and why its output is bit-identical.** `StaPipQBufferRenderer::sendObjectData`
decides per bag whether F is 255 at every vertex. That is true when GS fog is
off for the bag. It is also true when the bag's box lies wholly inside the fog
start: F = w * scale + offset is linear in w, and the box's maximum clip w is
w at the centre plus |dw/daxis| times the half-extent, three multiplies. When
it holds, the options qword carries fog scale 0 and offset 255.
- Every program computes F = 255 from those numbers, so the picture cannot
  change.
- `cull_tc` reads the scale's low 16 bits (`ilw.z`) and, on zero, takes a third
  copy of its unlit loop that stores the constant 0xFF0 (ftoi4 of 255) instead
  of computing it. The EE sets the lowest mantissa bit of a real scale whose
  low half happens to be zero, so a real scale is never mistaken for the
  signal.
- The spot-lit loop is untouched.
- The decision is taken only for bags that can use it: textured, unlit,
  non-env, no billboard, no spot reaching them.
- `--vu-check` stages a quarter of its trials with scale 0 / offset 255 and
  still reads IDENTICAL for `cull_tc`.

**Cost.** The fog-free loop is 61 cycles per three vertices against 73: the
loop is partly bound by the lower pipe, so the gain is less than 6 of 20. The
program grew by 66 words, and the VU1-clipping resident set is now ~1950 of
2042.

**Measured on a physical PS2**, district fixture, one ELF (engine toggle read
at boot, control = the file holding 0), two rounds per arm, repeatability
floor 0.007 ms. `work` change:

| pose | `work` | `vif_wait` | `prepare` |
| --- | ---: | ---: | ---: |
| garage day | **-0.24** | -0.42..-0.43 | +0.04 |
| garage night | +0.02 | -0.13..-0.14 | +0.09..+0.11 |
| outer day | -0.04..-0.05 | -0.10 | +0.04..+0.05 |
| outer night | -0.04..-0.05 | -0.11..-0.12 | +0.06 |

The first version tested the box with eight full matrix transforms per bag and
for every bag. It cost the EE more than VU1 saved at night: +0.08 ms in garage
night, where most meshes take the spot-lit loop anyway. What is left of
`prepare`'s increase is most likely D-cache misses on the boxes.
**The same loop for `cull_c` was measured and not kept.** It cost 64 words and
bought ~0.03 ms more in garage day (-0.27 against -0.24) and nothing
elsewhere. Untextured geometry is a small share of the vertices, and ~90 free
words are worth more than that. The clip family's fog lives in its emitter,
and there is no room for a second copy of it.

## Limits

These are four parked views of one scene on one console. Work excludes
presentation and the engine's outer pad/info services. The included buckets
overlap, `vif_wait_included_ms` brackets `dma_channel_wait(VIF1)` and is not a
measurement of VU1 execution, and none of these numbers convert into displayed
FPS. The VU1 rate is measured for the colour programs in the garage view; a scene
with a different program mix or a different clip ratio will have a different
rate. No image comparison was run for the arms — the probes are arithmetic-only
and the GIF packets are bit-identical by construction, and the triangle means
agree to 0.1%, but that is a weaker check than a GS capture. Linux was not
involved, and no change to the shipped engine came out of this work.

## Progress against the 60 fps budget

Every row is the physical console, release profile, four parked poses, 240
warmed rows per pose, with each arm's fixture regenerated by the editor that
built it. `work` is update + submission + finish; 60 fps needs the whole frame
inside **16.67 ms**.

| arm | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| VU1 spot-light gate only | 38.492 | 45.542 | 18.529 | 21.769 |
| + static models as strips | 37.325 | 44.541 | 18.257 | 21.438 |
| + roads and terrain as strips | 34.129 | 41.493 | 14.284 | 17.471 |
| + retained command data | **33.449** | **40.994** | **13.930** | **16.992** |

Render submission over the same arms, garage day: 34.665 → 33.434 → 30.284 →
29.634 ms.

**The outer-road views are now inside the 60 fps budget for measured work.** The
garage is not, and the reason is no longer geometry: 25,650 surface triangles
still cost 118 packet flushes and 29.6 ms of submission, of which VIF1 wait is
5.1, bounds 4.0, per-bag preparation 3.3, package creation and classification
about 5.5, packet construction 2.0, the send bracket 2.1, and roughly 7.6 ms is
outside those brackets. **Everything left scales with the number of bags and
packages rather than with triangles**, which is what the next round has to
attack.

**The `bounds` 4.0 in that list has since been attributed and cut.** Its five
parts are measured in
[render-submission-attribution.md](render-submission-attribution.md), "Round
two", and the largest of them was not any of the three things this page's own
rounds attacked: `StaPipQBufferRenderer::setMaxVertCount` fanning one `u32` out
to all 32 qbuffers once per bag. Removing that redundant fan-out is worth
**−0.34 ms of `bounds`** in the emulator (17.6% of the bucket), against 0.081 ms
for the branchless AABB test and the compacted `partBounds` stride combined. The
bbox cacher, which this page and its successor both nominated as the next
suspect, is **exonerated**: every lookup in the frame costs 0.118 ms and nothing
allocates. Read the 4.0 above as pre-fix, and note the lesson it cost: two of
the three attacks on this bucket were aimed by a plausible story.

**That last clause used to read "the generated game's own object loop and
per-bag engine overhead", and it was a guess. It has now been measured, and the
object loop is not in it** — see
[render-submission-attribution.md](render-submission-attribution.md). Two things
to carry back to every row above. First, `submit` is the whole
`beginFrame()`..`endFrame()` block, post-process passes and 2D HUD included,
while `bounds`/`prepare`/`dispatch` only ever covered `StaPipCore::render`, so
subtracting one from the other was never measuring "pipeline overhead" — it was
measuring everything else the frame does. Second, of the emulator's equivalent
gap the per-object visibility, distance, LOD and split-band tests are **1.0%**,
the thirteen live `TYRA_ASSERT`s are **0.5%**, and the largest single term is
`renderVehicleWheels` rebuilding every wheel vertex on the EE, which is not
submission at all.

Two notes on reading these arms. The retained-command round measured −0.605 ms
on hardware against −3.36 ms in PCSX2, and its author predicted exactly that:
the emulator models no EE data cache, and the change trades computing bytes for
reading them out of a cold 128 KB arena. And the fixture matters — the first
measurements on this page used `benchmark-district.py`'s copy of the example's
**committed** generated sources, which drift; regenerating with the editor under
test moved garage-day submission by 3.6 ms and restored the triangle count to
the September 14 reference exactly.

### Where the garage frame stands after the second day (2026-09-16)

Garage day, release profile, physical console: **work 29.910 ms**, from 38.492
when this work started. What is left, largest first — the three at the top are
within a millisecond of each other, so there is no single dominant term any more:

| | ms | what it is |
| --- | ---: | --- |
| the game's own `renderScene` phases | ~5.91 | outside `StaPipCore::render` entirely: the shared reflection probe, sky, terrain streaming, batches, procedural, animation, light effects, particles, HUD, post-fx |
| package creation and classification | ~5.32 | inside `dispatch`, now split six ways and **bounded** — see below |
| **VIF1 wait** | **5.47** | the only bucket that is genuinely *waiting* rather than working |
| update | 2.96 | the game's simulation |
| per-bag preparation | 2.90 | |
| bounds | 2.30 | after three attacks; the cacher lookup inside it is 0.118 ms |
| the `send_packet2` bracket | 2.10 | almost all of it `FlushCache(0)`, and proportional to the 120 submissions |
| packet construction | 1.92 | |
| finish | 1.03 | |

Presentation reads 10.05 ms, which is not a cost: 29.9 ms of work lands the frame
on the second PAL field and that is the slack to the boundary. One field is
20 ms, so **10 ms more has to come out before the display rate changes at all**,
and 13.2 ms before a 60 Hz frame is possible.

**And the obvious way to attack the package row is closed.** Almost every term
inside it is per-package, the frame is cut into 572.5 packages, and static
geometry ships as 72-vertex strip runs that ARE the packages — so "make the
package bigger" is where anyone would go next. It does not exist: the class this
frame runs derives **75** before `getMaxVertCount`'s multiple-of-9 rounding, and
the whole of VU1 data memory caps a six-quadword-per-vertex package at **81**
even with the clipping scratch deleted. A 144-vertex package wants 1 770 of
1 024 quadwords. The derivation, the per-class table, the `DBUFFER_END` sweep
and the two costed ways to reach 75 (−4.0% of packages) and 81 (−11.1%) are in
[render-submission-attribution.md](render-submission-attribution.md), "Round
three". **The single lever left on the package count is the six quadwords per
vertex, and this page's own probe says what that would and would not buy**: the
16-bytes-per-vertex arm moved VIF1 wait by 0.067 ms, so a smaller per-vertex
footprint pays by fitting more vertices per package, never by moving fewer
bytes.

## GIF state-tag compaction spike (2026-09-21, rejected)

The four state writes at the head of `cull_tc` looked like an easy packet win:
TEST, TEX1, TEX0 and ALPHA were each emitted as a one-loop PACKED A+D tag plus
one A+D payload qword. The safe-looking prototype kept the four payload qwords
unchanged but put them behind one four-loop PACKED A+D tag. That reduced this
header from **9 to 6 quadwords** (−3 QW / −48 bytes per textured-colour
package) and reduced the VU program from **345 to 342 instructions**.

It is not shippable. The generated and handwritten VU programs matched, the
native game built, PCSX2 booted and rendered the four benchmark poses correctly,
but the physical console stopped making progress on the first live gameplay
frame and never produced a benchmark CSV after more than four minutes. The
control build completed normally on the same console:

| pose | control median FPS |
| --- | ---: |
| garage day | 25.00 |
| garage night | 16.67 |
| outer-road day | 49.04 |
| outer-road night | 25.00 |

The prototype was fully reverted. **PCSX2 accepting a GIF packet is not evidence
that the real GIF path accepts it.** Any retry must start as a minimal
hardware-first packet harness, not as another whole-renderer arm.

The more aggressive REGLIST version is invalid for this state block for a
separate, architectural reason: a REGLIST selector is four bits wide and can
name PRIM, RGBAQ, ST, UV, XYZF2, XYZ2, TEX0, CLAMP, FOG, XYZF3, XYZ3, A+D or
NOP. TEST, TEX1 and ALPHA are not directly selectable. A future REGLIST study
therefore belongs on regular vertex output such as RGBAQ/XYZF2, and needs a
real native-64-bit VU packing design; it cannot be applied mechanically to
arbitrary A+D state writes.

One implementation trap also surfaced before the hardware result: adding a
second static upload changed the static DMA packet from one CNT/data pair to
two, so its capacity had to grow from 3 to 5 quadwords including END. An
undersized `packet2_t` can corrupt the chain before the GIF semantics are even
under test.

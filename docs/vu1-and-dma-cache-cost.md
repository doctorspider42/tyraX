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

## An unrelated finding worth its own work

The garage poses record **12.17 texture re-uploads per frame** (day) and 5.75
(night); the outer road records zero. The September 14 runs recorded zero
everywhere. The game's own `VRAMSTAT` line explains it: `freeMB=0.048`,
`largestKB=33`, and four evictions plus four re-uploads every frame. **GS VRAM is
full in the garage and the scene thrashes it permanently.** Each re-upload is a
PATH3 transfer plus the `dma_channel_wait(GIF)` the static pipeline performs
before every send. This is not part of either budget above and was not chased
here; see [gs-vram.md](gs-vram.md) for the instrument.

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
the generated game's own object loop and per-bag engine overhead outside those
brackets. **Everything left scales with the number of bags and packages rather
than with triangles**, which is what the next round has to attack.

Two notes on reading these arms. The retained-command round measured −0.605 ms
on hardware against −3.36 ms in PCSX2, and its author predicted exactly that:
the emulator models no EE data cache, and the change trades computing bytes for
reading them out of a cold 128 KB arena. And the fixture matters — the first
measurements on this page used `benchmark-district.py`'s copy of the example's
**committed** generated sources, which drift; regenerating with the editor under
test moved garage-day submission by 3.6 ms and restored the triangle count to
the September 14 reference exactly.

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

Neither is implemented or measured yet, and the arithmetic above is an
opportunity model, not a result. Quote the measured rate, not a predicted saving.

## An unrelated finding worth its own work

The garage poses record **12.17 texture re-uploads per frame** (day) and 5.75
(night); the outer road records zero. The September 14 runs recorded zero
everywhere. The game's own `VRAMSTAT` line explains it: `freeMB=0.048`,
`largestKB=33`, and four evictions plus four re-uploads every frame. **GS VRAM is
full in the garage and the scene thrashes it permanently.** Each re-upload is a
PATH3 transfer plus the `dma_channel_wait(GIF)` the static pipeline performs
before every send. This is not part of either budget above and was not chased
here; see [gs-vram.md](gs-vram.md) for the instrument.

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

# Retained static geometry command data

Retained command data stops the static pipeline from rebuilding, every frame,
the DMA and VIF commands that did not change. A wholly visible static bag hands
VU1 the same command block every frame — a CNT tag carrying the scale quadword
and the prim GIFtag, then one DMA `REF` tag per vertex stream — and the same
fifteen quadwords of VU1 clipping constants. Only the MVP matrix, the picked
dynamic light and the frustum classification are genuinely per-frame, and those
stay per-frame. Everything else is **captured once and replayed with a memcpy**.

It is an EE change and only an EE change: the packet that reaches VIF1 is
byte-for-byte what it was, so the picture, the VU1 microprograms, the GIF stream
and the submission count are all unchanged by construction.

## Why capture rather than re-derive

The obvious shape is to describe the block in a struct and write it out from
that description each time. This does the opposite: the ordinary builders
(`StaPipVU1Program::addBufferDataToPacket` and the per-program
`addProgramQBufferDataToPacket`) run exactly as before on the first frame, and
the bytes they produced are copied **out of the packet they just wrote them
into**. The replay is then byte-identical by construction rather than by
inspection, for every program class, for the strip and list representations,
and for a game-supplied VU1 program the engine knows nothing about.

That property is what makes the change cheap to trust. There is no second
description of the packet format to keep in sync, and no way for a future edit
to one of those writers to be silently missed here.

## Why copying is safe, and what it does NOT change

**Every DMA tag this pipeline writes is position-independent.** A `CNT` tag
counts the quadwords that follow it; a `REF` tag names an absolute address
*outside* the packet; with tag-transfer enable the VIF codes ride in the tag's
own upper half. So a finished, quadword-aligned run of them means the same thing
at any quadword-aligned position in any packet. Nothing here may be used for a
tag that refers to its own position — `NEXT` and `CALL`. The pipeline writes
none, and [static-submission-batching.md](static-submission-batching.md) records
why chaining separately allocated packets with one is not an option.

**The retained storage is EE-private and is never referenced by DMA.** Its
`REF` tags name the bag's own vertex, ST, colour and normal arrays exactly as
before, so the packet remains as self-contained as it always was and this adds
**no new DMA-lifetime exposure at all**. In particular the double-buffered
qbuffer copy pool — the thing that showed up on a real PS2 as a lamp corona
sliver in 4–19 frames of 30, see [vu1-clipping.md](vu1-clipping.md) — is
untouched: a copied qbuffer never gets a retained block (see the routing rules
below).

**It changes no count the console measures.** VU1 packages, submitted vertices,
packet flushes and `dma_channel_send_packet2` calls are identical, because the
same bags produce the same packets. What changes is only how many EE
instructions it took to write them.

## What is retained

Two independent pieces.

**The per-package geometry command block.** Only the CULL route is ever
retained. A clip-routed buffer packs a six-bit plane mask into its count word
and that mask moves with the camera; a copied, merged or strip-expanded buffer
points into the double-buffered slot pool, whose address is not a property of
the bag. Both keep their old path. What is left — the wholly-visible bag's
direct submission, a partially visible bag's packages that classify
`IN_FRUSTUM` or guard-band-only, and a stripped bag's runs — is the common case
and is exactly the case the task is about.

A strip run is a package, and the runs are baked
([model-pipeline.md](model-pipeline.md), "Triangle strips"), so a stripped bag's
blocks are as stable as a list bag's. Only the expansion branch, which cuts a
genuinely clipped run back into a triangle list on the EE, stays per-frame.

**The VU1 clipping uniform chain.** `addClipChain` writes one quadword of
clip-space constants and the six clip planes as `(A,B,C,D)+(E,0,0,0)` pairs —
fifteen quadwords, 52 float stores, **per mesh**, whose only inputs are the
renderer's near/far pair and the guard-band constant. It is captured once and
replayed for every mesh after that. `init()` and `setVU1Clipping()` are the only
places those inputs can move and both drop the capture.

## Invalidation is the whole correctness problem

The key is **every input the block encodes**, compared in full before the block
is replayed:

| keyed | catches |
| --- | --- |
| `vertices`, `sts`, `colors`, `normals` pointers | a reallocated or swapped stream, an LOD tier change, a texture-coordinate swap |
| `count` | a resized mesh (it moves the last package's vertex count and the package count) |
| package size (`maxVertCount`) | a program-class change, a `packageSize` pin |
| `bboxVersion` | a rewritten vertex buffer |
| the resolved VU1 program pointer | a material/program-class change, a resident-class change, an override |
| the prim state (type, shading, mapping, fogging, blending, AA, mapping type, colour fix) | fog on/off, a blending or shading change, a bag gaining or losing a texture |
| `RendererCoreDepth::scale` | a display-mode switch, which moves the Z scale in the block's first data quadword |
| single-colour and `stripped` flags | a colour-mode or topology change |

A mismatch does not allocate a second entry: the same storage is reused and all
of its captured blocks are thrown away, so the next frame rebuilds and
re-captures. A changed package COUNT re-sizes the entry in place.

Two invalidations are unconditional rather than keyed, because a pointer compare
cannot see them:

- **A pipeline teardown or scene unload** (`deallocateOnUse`) clears the whole
  cache and the clip capture. Every stream the `REF` tags name is being freed,
  and the key would only notice a reallocation that came back at a *different*
  address.
- **A VU1 clipping-mode switch** clears both, since the clip chain is either
  present or absent and every bag's cull program changes with it.

Everything else is keyed, and note what the key does **not** need to catch: a
recycled heap address that comes back with the same layout produces the same
block, because the block carries addresses and counts and no vertex data at all.

Two bag shapes are refused outright, for the same reasons the submission-batch
scope refuses them: a **billboard** bag (its program set is swapped in on
demand and its centres are expanded from a per-frame camera basis), and any bag
at all while a **game-supplied program override** is installed (a replacement
writer has no packet ABI and is not required to be position-independent).

## Cost and bounds

An entry holds `packages x 7` quadwords — 112 bytes per VU1 package, a slot
reserved before the first capture measures the block, so the reserve is the
fattest built-in class (3 quadwords for the scale/GIFtag group plus one DMA REF
per stream, 6 in the worst case) with one stream of headroom. The budget is
**8192 quadwords, 128 KB**, about 1170 packages.

**The cap is a hard budget, so it is spent on the bags actually being drawn.**
When it binds, the least recently used entries are dropped to make room, at most
eight per frame. Both halves of that are load-bearing. Without eviction the
first bags to ask filled the cache and every later one was refused for its whole
250-frame lifetime — measured on the Motor District garage as **25-40% of
packages rebuilding every frame** while the cache sat pinned at the cap holding
geometry from a pose the camera had left. Without the per-frame bound, a scene
whose working set genuinely does not fit would evict and re-capture the same
bags forever, which is strictly worse than not retaining them; with it, such a
scene settles on the subset that fits.

Entries also expire after 250 unused frames, the same retention the
package-bbox cache uses. Both eviction and expiry compact the storage and
rebuild the 256-bucket index — indices, not pointers, so vector relocation stays
safe.

## Reading it back

`StaPipCore::takeRetainedCommandHits()` / `takeRetainedCommandBuilds()` /
`getRetainedCommandBytes()` report how many package command blocks were replayed
against how many were built, and how much EE RAM the cache holds. The two
counters reset on read. They are always compiled — three loads — and always
zero when the feature is compiled out, so a game's HUD can print them in either
arm.

A **debug** build of the engine also logs them itself, once every 300 frames:

```
STAPIPRET retained=812 rebuilt=27 per frame, cache=104 KB
```

A debug build is therefore the only in-engine consumer of those counters; a
release game gets every one of them.

## The A/B switch

`TYRA_STAPIP_RETAINED_COMMANDS` in
`stapip_qbuffer_renderer.hpp` defaults to 1. Setting it to 0 restores exactly
the previous packet construction — that is the control arm, one knob, same
project directory, same assets, same generated game. Both arms must produce
byte-identical `--capture-frame` images.

## What has been measured

PCSX2, software renderer, the Motor District benchmark fixture
(`examples/vehicle-playground/authoring/benchmark-district.py`, debug profile,
parked traffic, frozen four-pose camera), **one project directory and one
knob** — `TYRA_STAPIP_RETAINED_COMMANDS` 0 against 1. Three boots per arm, and
the fixture's own 32 rolling engine-FPS samples per boot.

**The picture is identical, which is the result that matters.** Eighteen
`--capture-frame` images — three per boot, six boots, both arms — hash to one
value. That is the byte-identical-packet claim demonstrated rather than argued.

| run | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| control (switch 0) | 27.778 | 25.000 | 49.038 | 49.038 |
| control, 2nd boot | 27.889 | 25.000 | 50.000 | 50.000 |
| control, 3rd boot | 28.000 | 25.000 | 50.000 | 50.000 |
| retained, first shape | 29.423 | 25.000 | 50.000 | 49.999 |
| **retained (shipped)** | **30.768** | 25.000 | 50.000 | 50.000 |
| **retained, 2nd boot** | **30.769** | 25.000 | 50.000 | 50.000 |

**Garage day is the only pose that can answer anything**: the other three sit on
a vsync division (25 or 50 of PAL's 50 Hz) in both arms and are saturated, not
neutral. There, 27.889 → 30.769 median FPS is **35.86 ms → 32.50 ms, −3.36 ms
(−9.4 %)**, against a control spread of 0.222 FPS (0.29 ms) and a **same-build
repeatability of 0.001 FPS** — the two boots of one ELF differ in the fourth
digit, which is what makes a single-digit-FPS delta readable at all.

The "first shape" row is worth keeping: it is the same feature with an 8-quadword
reserve and no eviction, and it is **1.35 FPS slower** than the shipped one. The
cap was binding and the cache held geometry the camera had left, so a quarter to
a two-fifths of the frame's packages were rebuilding for no reason. Tightening
the reserve to 7 and adding the bounded LRU is the whole difference.

At the held garage-day pose the shipped build reports
`STAPIPRET retained=772 rebuilt=246 per frame, cache=106 KB` — **76 % of the
frame's VU1 package command blocks replayed**, using 106 KB of the 128 KB
budget. The 246 rebuilds are not a cache miss to chase: they are the routes that
deliberately keep their old path (clip-routed and copied buffers) plus the bags
whose geometry genuinely changes every frame, which is what `bboxVersion` is for.

## What has not been measured

See [vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md) for where the frame's
remaining EE time sits. Two limits are worth stating up front.

**PCSX2 emulates no EE data cache.** This change trades computing bytes for
reading them out of a 128 KB arena that is cold every frame, and the emulator
cannot price that trade: it sees the removed work and none of the added misses.
So a PCSX2 delta is an upper bound on the hardware saving, not an estimate of
it, and no millisecond figure from the emulator belongs in a hardware claim.

**A DMA lifetime bug is a hardware-only failure.** The design's answer to that
class is structural — the retained block is copied, never referenced, so the
packet's lifetime contract is literally unchanged — but structural arguments are
what the slot-pool race also had before a console disproved it. The stress
harness that the submission-batching work used (forced texture evictions while a
batch is pending, pipeline switches, LOD crossings, a scene reload) is the right
gate, and PCSX2 will not reproduce a lifetime bug reliably whatever it reports.

# troubleshoot/tyra-engine-dev-measurement.md

Profiling, counters, the free-RAM probe, the hardware timeline and the A/B method.
The frame-rate counters are in `tyra-engine-dev-display.md`. Symptom first.

## The free-RAM probe is a measurement, not a sensor — and it used to lie

**Cause:** `Info::getFreeRAMSize` is the only way to ask this allocator what is left:
claim every free block until malloc refuses, sum, free the chain. The block search used
to start at the TOP BIT of `size_t` (malloc(2 GB) on the EE: eight refusals before the
first success), free the block that worked, refine the size upward and then allocate the
refined size AGAIN — and when that last allocation failed, its recovery cleared the
LOWEST SET BIT of the size, which for a single-bit size is ZERO. The probe then reported
"no block", the sum stopped at the first one, and the game's HUD printed
`MEM 32.0/32 MB` on a console with 13.6 MB free while the Debugger's *Measure now*
silently showed nothing. It starts at the console's 32 MB now and keeps the block that worked instead of
re-allocating it (1.85.1).
**Fix:** a probe built out of failing allocations is sensitive to the allocator's mood,
so PROVE it against a plain `malloc`/`free` ladder logged at the same instant before
believing either number — and a measurement that comes back 0 must be reported as a
failure, or it reads as "the button does nothing".

## The StaPip telemetry timing brackets do not cover the whole of `render`

**Cause:** the head (the fog decision and the thirteen `TYRA_ASSERT`s, which a release
game really does execute, because no game build defines NDEBUG) and the tail sit outside
all of them. `TYRA_STAPIP_ATTRIB` in `stapip_attrib.hpp` (default **0**, and it must
stay 0 in anything shipped) adds a `renderTicks` bracket around the whole function plus
a six-way split of `prepare`, a five-way split of `bounds` (with the bbox cacher's own
hit/recalculate/fresh counters, and the per-frame expiry scan, which lives OUTSIDE
render() and therefore lands in `finish`) and a split of the package-creation box inside
`dispatch`. Unlike the first round's, THESE hooks are measurable: +0.122 ms on `bounds`
and +0.062 on `dispatch`, so the children over-report by 6.3% and 1.1% and one COP0 read
prices at ~11 cycles.
**Fix:** what the split found — the bbox cacher is NOT the cost (226.5 lookups = 0.118
ms, 1.44 probes each, zero allocations) — 0.618 ms is 10.5 `recalculate()` calls forced
by a caller that bumps `bboxVersion` every frame; and 22% of `bounds` was
`StaPipQBufferRenderer::setMaxVertCount` fanning one u32 out to all 32 qbuffers once per
bag, which now returns early when the value has not moved (−0.34 ms, picture
byte-identical). Docs: docs/render-submission-attribution.md.

## `StaPipTelemetry` finally has a reader: the `FTCLIP` line

**Cause:** the counter block existed for a year with nothing calling `takeTelemetry()`.
**Fix:** under `TYRA_FRAME_PROFILE` the generated game enables it and prints
`cull=P/T clip=P/T guard=P/T out=... flush=... vuwait=...` once a second beside
`FRAMETIME` (docs/profiling.md). Drain it EVERY frame — `takeTelemetry` clears as it
reads, so a skipped frame is a lost frame. `StaPipTelemetry` records the transition
count (`programSetSwaps`, also the frame-cost CSV's `Program_swaps_count` row) and full
wait/upload ticks, plus cull/clip/outside routes, active-plane population, qbuffer
flushes, and VIF1/VU1 wait ticks; disabled telemetry keeps the AABB early-out and
performs no COP0 reads.

## The static packet structure walker

**Cause:** it is the safe first step before changing the packet language.
`TYRA_FRAME_PROFILE` enables `TYRA_STAPIP_PACKET_PROFILE`, which walks the completed
DMA/VIF chain in `sendPacket` and prints per-producer `FTPKT` totals every 50 frames.
**Fix:** a row is admissible only with `bad=0`; GIF/GS totals are derived from the
selected VU1 program and package count. Both counters and parser compile out by default.
The 2026-09-22 physical capture found 765–826 A+D writes and 197–213 XGKICKs per garage
frame. Aligning only each `BagArray` base to 128 bytes is a measured dead end: the
1,200-byte package stride loses alignment again and hardware work regressed 0.09–0.18
ms. Do not restore it without a package-owned packed stream. See docs/profiling.md.

## Code layout moves the brackets you are reading

**Cause:** code layout between two builds moves `bounds`/`prepare` by up to ~0.17 ms on
the console, and the repeatability floor (two boots of one ELF) does not cover it.
**Fix:** read a change's delta in the brackets it touches. **A/B a game-code change with
ONE ELF toggled at boot**, not two builds — code layout moved `work` by ~0.4 ms between
two builds of the generated game (docs/ee-submission-rearchitecture.md, "How to A/B a
change to GAME code").

## How to measure a rendering change

**Cause:** PCSX2's HW renderer and its GS timing are not honest.
**Fix:** judge correctness on PCSX2's **software renderer** (see `tyra-testing`), measure
with PCSX2's FPS display on the software renderer, 3+ samples, before and after;
pixel-compare screenshots to prove output is unchanged. PCSX2 under-reports GS fill by
**76×** and over-weights libm, so attribute per-function numbers on hardware. The
fixture's own repeatability floor matters: 0.135 ms on the six-hashed-ELF EE probe set,
0.012 ms for the baked-stream garage-day A/B, 0.001 FPS same-build. A parked fixture
cannot see a whole class of bug (see the flush-removal note in
`tyra-engine-dev-submission.md`) — re-run with `--keep-routes` and moving traffic.

## The classification rebuild (PROGRESS entry 100)

**Cause:** per-package corner transforms and merged 8-corner boxes.
**Fix:** the frustum planes are transformed into the bag's object space once per bag
(`CoreBBox::computeObjectSpacePlanes`) and every package/subpackage is classified
with the p-vertex/n-vertex AABB test on min/max corners
(`CoreBBox::frustumCheckAABB`) — no per-package corner transforms, no merged 8-corner
boxes, and `clipFrustumCheck`'s duplicate zero-guard-band re-check is gone. Plus a
VU0 `vmini`/`vmax` min/max scan in `CoreBBox(const Vec4*, count)` and `memcpy` fills
in `StaPipQBuffer::fillByCopy*`. Net: 47 → 73 FPS on the vsync-off 98k benchmark,
pixel-identical; the hidden `"clipping": "vu1"` mode hits 120 FPS now. When touching
classification, mind the AABB invariant: every CoreBBox the packager sees is
axis-aligned with `vertices[0]`/`vertices[7]` as min/max — only the matrix-transform
constructor breaks that, and it must never feed the AABB test.

## Performance context

**Cause:** the 98k-vertex benchmark scene went 12 → 50 FPS through: outcode early-out in
the EE clipper, static pools (no per-call heap), and finally exact per-package frustum
classification. The StaPip packager's per-submit `new[]`/`delete[]` package arrays are
pooled too (grow-only vectors in `StaPipBagPackager`; callers must not free the result)
— measured worth only ~2% of the partial branch in PCSX2: the real cost of
PARTIALLY_IN_FRUSTUM geometry is the per-package bbox classification + EE clipping,
which scale with vertex count (PROGRESS entry 79: a single detail-16 box near the camera
= 9.2k verts = ~36 ms of EE; the fix was authoring-side detail, not the allocator).
**Fix:** the classification rebuild (PROGRESS entry 100) net 47 → 73 FPS on the vsync-off
98k benchmark, pixel-identical; the hidden `"clipping": "vu1"` mode hits 120 FPS (see
the classification rebuild entry above).

## Hardware timeline capture

**Cause:** bounded RAM events need a start/stop.
**Fix:** use `tools/hardware-trace.py arm PROJECT` before a boot, then export the
complete `bin/hardware-trace.csv` to HTML/Perfetto. The engine captures bounded RAM
events without new drains and writes after sampling. Scope totals overlap; VIF1 DMA wait
is not VU1 execution, and VIF/GIF snapshots are not utilization. Compare unarmed/armed
controls and reject dropped or stale events. See docs/hardware-profiler.md for
start-frame semantics and capture limits.

## Native hardware timeline (1.92)

**Cause:** the offline exporter needed a browser/Python step.
**Fix:** `src/hardware_timeline.cpp` reads the same bounded CSV as the offline exporter.
Debugger > Hardware timeline arms the next boot and loads completed captures on demand,
with frame selection, zoom, raw marker tooltips and inclusive totals. No browser, Python
or extra debugger polling is required. Engine detail scopes separate package
creation/classification, qbuffer copies and packet construction. See
`docs/hardware-profiler.md`; use unarmed controls to rank performance.

## Triangle counts are not comparable across a strip change

**Cause:** telemetry gained `packagesStrip`, `packagesStripExpanded` and
`verticesSubmitted` (counted where a buffer is packetised). A strip run of 72 reports
70 triangles including its degenerate joins and padding, against a list package's 24.
**Fix:** compare `verticesSubmitted`. The `triangles*` fields are **GS primitives** —
one surface reports 25 650 as a list and 38 427 as strips on the garage-day pose (a
**measured 1.498x**, not a miscount). The PRODUCERS log theirs instead —
`ROADSTRIP`/`TERRAINSTRIP` in `bin/log.txt`, degenerates dropped. Both strip-counting
bugs are now FIXED in `stapip_core.cpp`: `recordGuardBandPackage` uses
`recordPackage`'s rule (the guard route had averaged 21.5 tri/package against cull's
37.9 from `size / 3` vs `size − 2`), and `recordOutsideBag` charges
`packages * (runLength - 2)` (it was `count - 2`, over-counting by `2 * (packages - 1)`).

## The frame-local last-transform cache

**Cause:** `StaPipCore` has a frame-local last-transform cache for consecutive TyraMVP
material bags. It reuses the MVP and object-space frustum planes only when the model
pointer, model-matrix contents and view-projection contents all match; this content key
is required because physics mutates matrices in place and portal/split-screen passes
switch cameras.
**Fix:** `onFrameEnd()` invalidates it and TyraMP bypasses it. The portal/mirror-free
Aster PCSX2 fixture measured median serialized Total 21.322 -> 19.172 ms and Objects
14.102 -> 12.425 ms over six alternating boots, but the directly attributed Prepare
counter moved only 1.375 -> 1.341 ms: treat the aggregate emulator delta as directional
until the same ELF pair is measured on a physical PS2.

## The frame-cost instrument

**Cause:** `FRAMETIME` needs a zero-cost gate.
**Fix:** the instrument is `inc/debug/frame_profile.hpp` (`TYRA_FRAME_PROFILE`, default
0, so a shipped `libtyra.a` carries none of it) plus the `FRAMETIME` line in the
generated `drawDebugHud`. The rig's window mean summed ticks in a **u32**, which
overflows above ~290 ms/frame and printed a 500 ms frame as 37 ms (now u64 — the median
was always right, which is how it was caught). And `RendererCore::getStallTotal()` is a
never-reset running total of the renderer's stall time beside `takeStallTicks()`, which
resets.

## The counter families and where each reads

**Cause:** every cache and probe has its own log line and all of them compile out.
**Fix:** the `STAPIP*` family — `STAPIPRET` (retained-command hits/builds),
`STAPIPBAKE` (baked arena size + per-frame chain qwords), `STAPIPMISS` (WHY a bag
rebuilt — `bbox`, `prim`, `streams`, `program`, `size`, `new`, `incomplete`) — plus
`VRAMSTAT`/`VRAMRES`/`VRAMEVICT`, the `BLSS*`
set (`BLSSGRID`/`BLSSWORST`/`BLSSFEAT`/`BLSSOUT`/`BLSSFILL`) and `GPUHOLD`. See
`tyra-engine-dev-submission.md`, `tyra-engine-dev-vram.md`, `tyra-engine-dev-blss.md`.
Old `PROGRESS 123` citations name an entry of the retired `PROGRESS.md` (in git history:
`git log --diff-filter=D -- PROGRESS.md`; recipe in docs/backlog.md).

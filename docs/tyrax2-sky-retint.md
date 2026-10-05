# Runtime sky retint

The physical cold-reuse census isolated repeated sky work: two bounding-box
recalculations cover 1,158 vertices, consisting of a 1,152-vertex painted dome
and a six-vertex scene bag. All three 1,200-vertex wheel batches report zero
normal recalculation and zero changed output in those cold snapshots. The
emulator's earlier 1,206-vertex total has a different owner; it is not the PS2
sky witness.

The runtime RGB caller used the complete sky builder, regenerating positions,
UVs, colors, bags and bounds. The candidate retains geometry and updates the
same colors through one stamped span. Initial and scene builds remain full.
The candidate retains the original gradient/tint arithmetic and triangle order.

## Physical result

One ELF, full night, stationary driving view, ordinary sampler, no extra scope
clocks or removed effects. Each order completed 5,400 entered loops, 15 chunks
and 384 raw samples. Sparse checks at offsets 750 and 1155 compare every array
against an independent original full build and verify geometry/color stamps.
The original equal output remains live on those cold frames; timed frames are
separate and use the selected path normally.

| Order | First phase (ms) | Middle (ms) | Last (ms) | Candidate minus control | Outer drift |
|---|---:|---:|---:|---:|---:|
| On / Off / On | 18.574942 | 19.384158 | 18.592105 | -0.800634 ms | 0.017163 ms |
| Off / On / Off | 19.394448 | 18.553030 | 19.417788 | -0.853088 ms | 0.023340 ms |

All physical cold checks activated, retained 1,152 vertices, had zero mismatches
and zero fallback. The saving is about 4.3% of non-pacing elapsed work in this
view. It does not establish a scalar CPU-only saving or full-quality 60 FPS.
The physical rendered cadence remains approximately 30 Hz.

## Validation boundaries

Common sampler/candidate code footprint is not separately priced. Cold vector
allocations and full-reference calls are outside the timed window, but their
residual state effects are not declared zero. No uniform observer subtraction
or cross-version subtraction is used. Production removes this private harness.

The same-ELF emulator trial completed its loops but all six cold retint counters
were zero. Strict analysis rejected it as inactive. Its raster pictures do not
qualify the active candidate and its HUD FPS is not a console performance result.
A separate generator/motion qualification forces real RGB changes and exercises
painted/plain sky transitions; that qualification is not a pricing run.

The additional physical trigger witness rejects the staged-zenith sentinel
hypothesis in this view: the cycle is active, all twelve inputs are positive
finite values, and RGB/zenith changes are typically only 1-3 binary32 ULP.
DistrictMood resets the hour to 0 or 12 each update; the later cycle tick adds
real dt times 24/7200 before evaluation. Near midnight, the evaluated hour is
therefore dt/300, while the next frame resets it again. Variable dt can drive
small real color changes. The recorded shader clock being zero does not prove
a frozen cycle hour. Actual hour-before/after correlation remains unmeasured;
no emulator/FPU defect is inferred. A separately qualified paused-clock API is
a follow-up, not part of this geometry-retention change.

## Generator and warm lifecycle qualification

The editor built successfully and freshly generated the vehicle project. The
private qualification forces RGB after all normal writers, drives steering,
cameras and player movement, changes day/night, crosses three scenes and
returns to the first. It swaps painted/plain skies and changes panorama yaw.
All four scene generations have owned warm captures, taken 60 retints after an
original-reference rebuild. All 38 array comparisons match, covering 1,152- and
504-vertex domes. The FPP collision and physics outputs pass the actual PS2
compiler syntax check. Runtime PS2 warm appearance has not received a new human
confirmation; physical cold data parity and emulator warm visuals are separate
claims. Portal/split and CALL-specific retint scenarios are not newly qualified.

The first generator qualification was rejected because the ordinary cycle
writer overwrote the early RGB forcing. Its incomplete coverage and the inactive
same-ELF emulator trial remain rejected evidence, not successful validation.
Production retains no forced colors, counters, vector snapshots or oracle calls.

## Evidence

[Payload manifest](tyrax2-sky-retint-2026-10-05/payload-manifest.json) pins the
source blobs, native/ABI audits, completed physical records, rejected attempts,
strict controls and warm qualification. Source blobs are stored by SHA-256;
each fixture manifest maps its relative source path to that digest. Full game
ELFs remain in the LAB with their hashes archived here. No full-quality 60 FPS
or precise EE-only attribution is claimed.

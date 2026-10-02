# Physical PS2 hardware timeline

The hardware profiler records bounded EE scopes and pipeline-state observations
in RAM, then exports an interactive timeline and Perfetto-compatible JSON.
It measures the ordinary asynchronous renderer without adding per-draw drains.

## Hierarchy and observer controls (1.170)

Coarse capture records the main loop, pad/game/info, refreshed generated
Update/Scene scopes, frame submission, existing completion waits and presentation
pacing. Detailed capture adds package, snapshot/preflight/fixup/mutable-copy,
native sizing/emission and other fine scopes. Register snapshots are a separate
option. Refresh generated game sources when upgrading: Update now reserves its
parent scope before nested work and closes explicitly before rendering.

The editor retains its five-column `hardware-trace.csv`. The separate
`hardware-trace-v2.csv` and `hardware-trace-v2-<first>.csv` provide explicit scope
parents, typed intervals and actual recording/presentation source jobs:

```sh
python tools/hardware-trace.py arm PROJECT --start 1100 --frames 4 --no-states
python tools/hardware-trace.py arm PROJECT --start 1100 --frames 2 --detailed --no-states
python tools/hardware-trace.py export PROJECT/bin/hardware-trace-v2.csv -o REPORT
```

`--capacity` selects 128..32768 slots (default8192). Event density depends on
scene and detail mode. The first Motor District emulator calibration overflowed
8192 slots during a32-frame detailed window; its result is rejected. The revised
private fixture captures8 frames with32768 retained slots. Capacity and timing
still require physical acceptance; shorten the window on overflow. Export
occurs after capture, but its host I/O can delay the enclosing/following loop.

V2 analysis partitions elapsed frame intervals once into authoritative scope
self-time, existing waits, pacing and uncovered time. Inclusive totals remain
separate. Raw compatibility spans have unknown parents and remain inclusive-only;
they cannot silently enter the exclusive ledger. COP0 spans include preemption
and any unclassified waiting: self-time is not useful CPU instructions or VU/GS
utilization. `present_job` identifies the CPU presentation source, not an ISR
display latch or TV photons. Pending jobs from before a capture are explicitly
unknown. Requested mode differs from effective recording after fallback.

Define `TYRA_HARDWARE_TRACE=0` consistently in engine and game to compile out
trace APIs: inline no-ops and constant false gates remove trace clocks, file
access, allocation, state reads and implementation calls. The default is1 to
retain the existing opt-in boot feature; a missing config still leaves compiled
guards. Runtime-off and compiled-out controls therefore answer different
questions. Never label the default build zero-overhead.

The private calibration compares Off/coarse/Off/detail/Off/detail+states/Off
with the same ELF and a ring reserved before all phases. Common independent
clock samples measure the full engine loop plus game/render work. Of8 samples,
the last includes automatic export and is excluded in every arm, leaving7.
Small single-boot windows are exploratory; repeat boots and compare both adjacent
controls. The independent sampler/FrameProfile also cost work. A separate
compiled-out ELF changes code layout and held heap, so its difference is not
pure dispatch cost. Wall time includes pacing; unchanged wall time does not
establish free instrumentation. Do not subtract one universal overhead constant.

`hardware-trace.py controls MANIFEST.json -o REPORT.json` checks repeated explicit
A/B/A controls with matching scene/camera/replay/clock/order/window identities,
one instrumented ELF and declared separate compiled-out layout. See
[host analysis protocol](../tools/hardware-trace-analysis.md) and the
[validation record](hardware-timeline-v2-2026-10-02.json). Physical observer cost
requires repeated valid matched controls on PS2.

The first instrumented physical calibration completes 15,400 frames and all
seven arms. Coarse/detail/detail-with-states captures pass strict export with
104/19,685/23,064 events, each 8 frames, no drops, invalid entries or ambiguities.
Independent FrameProfile work means are 17.697577/17.913900/17.702240/20.898795/
17.775104/21.412268/17.798146 ms for Off/Coarse/Off/Detailed/Off/States/Off.
Against both adjacent controls, observed work increases are 0.211660–0.216324 ms
coarse, 3.123690–3.196555 ms detailed and 3.614122–3.637163 ms detailed with states.
The whole-engine mean remains around 33.36 ms: added work mostly consumes pacing
headroom, and detail activation changes individual frame pacing. Unchanged FPS
therefore does not establish zero observer cost. Seven accepted samples per arm
in one boot remain exploratory; compiled-out physical comparison and repeated
matched runs are open. States-minus-detail is not an isolated register-read
cost, and none of these ranges is a constant correction for production timing.

[Seven physical PS2 controls and measured overhead](hardware-profiler-results.md)
record the first full-asset Motor District diagnosis.

## In the editor (1.92+)

![Native hardware timeline](img/hardware-timeline.png)

Open **Debugger > Hardware timeline** (F9). Set the first engine frame, capture
length (1–32) and optional register snapshots, then **Arm next boot**. This writes
`bin/hardware-trace.cfg`; it does not reset or interrupt the running console.
Build the game before arming. After the capture finishes, **Load hardware capture**
reads `bin/hardware-trace.csv` on demand. The previous file stays available until
the game replaces it; arming is not evidence of a new capture.

Select a frame and zoom horizontally (1–16x). Hover blue EE scopes, red waits or
orange register markers for duration, relative start and raw values. Scroll the
chart in either direction; expand or undock the Debugger for a wider view. The
inclusive table contains overlapping scopes, not additive hardware utilization.
The native viewer requires neither Python nor a browser, adds no polling and
rejects incomplete, overflowing or inconsistent captures. HTML/Perfetto export
still uses the same CSV and remains available with the command below.

Detailed engine lanes include `Package_create` (metadata and classification),
`Package_classify` (nested inside creation), `QBuffer_copy` (including pooled
allocation bookkeeping), and `Packet_build` (commands, excluding send/wait).
Generated debug games also expose `Live_debug`, with nested
`Live_debug_poll` and `Live_debug_flush` scopes when those synchronous host-file
operations actually run. These are the first place to look when an otherwise
fast physical-console frame periodically doubles.
Vehicle projects split `Vehicles_update`, `Vehicle_smoke_update` and
`Vehicle_skids_update`; `Vehicle_sleep` is nested once for every parked instance
that skipped its static physics work in that frame.
These extra hooks have measurable capture overhead; do not rank optimizations
using their short armed captures alone.

## Capture

Build a game with this engine and refreshed generated sources. Deploy all baked
resources, keep one ps2client host server alive, and use the usual ps2link marker.
Before the next boot:

```sh
python tools/hardware-trace.py arm /path/to/project --start 120 --frames 4
```

The engine reads `bin/hardware-trace.cfg` once after game initialization. Start
is a zero-based engine-loop index, including loading/splash loops, not a scene
frame number. Choose a warmed window after loading. Capture length is 1–32
frames. The default 8,192 event slots occupy approximately 480 KiB on EE;
capacity is configurable from 128 to 32,768. Boot capture allocates before
sampling and frees after export. A missing or invalid configuration disables capture. There
is no continuous host-file polling. The game keeps playing after capture.

The completed file is `bin/hardware-trace.csv`. Archive old files before a new
boot and verify fresh output; an earlier CSV is not evidence of a new capture.
Export only after the final END footer arrives:

```sh
python tools/hardware-trace.py export /path/to/project/bin/hardware-trace.csv -o /path/to/report
python tools/hardware-trace.py disarm /path/to/project
```

Export produces `report.html`, `report.json` (Perfetto/Chrome trace format), and
`report.summary.json`. Open HTML locally to select frames and hover over events.
The exporter rejects incomplete, overflowed and inconsistent captures. On
overflow reduce the requested frames. Disarm affects the next boot.

## Read the graph correctly

- Frame encloses the main EE loop, including pad, game and info. Game/Scene,
  static bags, dispatch and wait scopes nest: never add their inclusive totals.
- Scene phases and object IDs reuse the render-cost labels, but do not insert
  that tool's GS barriers. Do not request a serialized render-cost capture,
  screenshot or VU memory capture during hardware sampling.
- `Roads` is the per-frame cull/submission of road and junction bags that were
  generated once at scene load. `Procedural` excludes those reserved road
  chunks and covers only procedural volumes and prefab geometry. Generation is
  therefore not recurring, but submitting visible ready bags still is.
- VIF1_DMA_wait measures EE waiting for DMA consumption, not VU1 arithmetic.
  VIF1 snapshots show VPS and VEW at observed boundaries only. GIF_STATE shows
  active path and FIFO occupancy at those instants; it is not GS utilization.
- GS_FINISH_wait measures existing completion handshakes and can include queued
  upstream work. No new FINISH is inserted. Present includes buffer-flip waits.
- Packet_qwords is a submitted static-chain size, not total vertex/texture bytes
  referenced through DMA tags. No unmeasured transfer-byte total is inferred.
- COP0 Count timestamps use 294,912 ticks/ms. Scopes measure elapsed EE time,
  including preemption; they are not retired CPU instruction counts.

Record an unarmed control, an armed capture and a repeat. `--no-states` disables
register snapshots while retaining scopes for overhead diagnosis. Active scope
recording and register reads perturb timing; export itself occurs after the
window and must be excluded from FPS comparisons. Short traces locate work,
while longer warmed controls establish performance.

## Engine API

`debug/hardware_trace.hpp` exposes `Scope`, `record` and `state`. Labels must have
static lifetime; storage retains their pointers until export. The recorder is
for the main EE thread only. Scope destructors handle early returns. When
unarmed, hooks check the active flag without reading clocks or hardware state.
The engine owns configure/beginFrame/endFrame, so early game-loop returns still
close a complete frame. The editor uses boot capture configuration; calibration
code can reserve retained storage and arm future windows between phases.
This is not a hardware utilization percentage display.

Pair traces with isolated raster, shader, extra-pass and debug-I/O controls.
Changes to visible content are diagnostic substitutions, not proposed quality
settings. Verify actual GS captures and geometry counters before interpreting
their sensitivity as a bottleneck diagnosis.

For two reproducible engine probes, use an already-built fixed-workload project:

```sh
python tools/hardware-probe.py FIXTURE vendor/tyra NEW_DIRECTORY --kind one-pixel-scissor
python tools/hardware-probe.py FIXTURE vendor/tyra ANOTHER_DIRECTORY --kind unlit
```

Each command copies the game and engine, records resource hashes and patched
files in `probe.json`, and refuses an existing destination. Build its `game`
with native-build and its `tyra` as the engine source. The one-pixel probe masks
engine scissor writes, including render-target restores, while retaining the
original geometry, projection and VU programs. It suppresses raster area; it
does not remove primitive setup or all GS work. Verify its nearly empty GS
capture and matching geometry counts. Unlit removes lighting and normal data
from static bags, restoring the caller's pointer afterward; this changes
program class, data transfer and possibly package capacity, so it cannot
isolate VU arithmetic alone. Both are destructive to image quality by design
and must remain isolated diagnostics, never production engine patches.

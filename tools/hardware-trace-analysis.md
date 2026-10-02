# Host timeline analysis

`hardware-trace.py` accepts the unchanged five-column `hardware-trace.csv`
and the separate fifteen-column `hardware-trace-v2.csv` sidecar. The editor
continues to consume the legacy file. Use the v2 sidecar for source-reserved
scope parents, recording job IDs and CPU presentation source IDs:

```powershell
python tools/hardware-trace.py arm PROJECT --start 120 --frames 4
python tools/hardware-trace.py arm PROJECT --start 120 --frames 2 --detailed --no-states
python tools/hardware-trace.py export PROJECT/bin/hardware-trace-v2.csv -o LAB/timeline
python tools/test_hardware_trace_analysis.py
```

The default arm requests coarse capture. The optional fourth configuration
integer selects detailed capture. `--no-states` disables instantaneous register
snapshots. Arming does not restart the console; an existing CSV does not prove a
new capture. Export produces HTML, Perfetto JSON and a structured summary JSON.
The fifth integer sets `--capacity` (128..32768, default8192). Detailed Motor
District captures exceeded8192 slots over32 frames in the first emulator trial;
overflow is rejected, not truncated. Shorten the window or reserve more slots.
The revised private calibration uses8 frames/32768 slots; its overhead still
requires physical measurement and repeated controls.

The selected-frame ledger partitions elapsed EE frame time exactly once into
CPU scopes, waits, presentation pacing, uncovered frame time and ambiguous
overlap. Scope tables retain inclusive durations separately. Job totals use
only exclusive intervals with an actual source job ID. This describes observed
EE execution and waits; it does not establish useful arithmetic time, VU/GS
utilization or a GPU dependency graph. Recording job and CPU presentation source IDs
describe source identity; adjacent IDs do not establish pipeline age. Triple
buffering can queue a source for a later uninstrumented ISR display. These IDs
do not measure display-latch or TV latency. Frame/Scene scopes opened before
recording can have unknown job0 while their later children have actual job IDs.

Legacy hierarchy is inferred from strict interval containment. Equal or crossing
intervals remain ambiguous. V2 uses reserved event/parent IDs. Detailed raw
intervals marked `kind=legacy,parent_id=4294967295` have unknown hierarchy:
they appear as inclusive observations and are excluded from exclusive ownership.
Their summary IDs and HTML warning make this limitation explicit. Typed enclosing
scopes can still describe that elapsed EE interval at a coarser level.

Both schemas reject malformed rows, missing END, unexpected frame ranges and
dropped events. V2 additionally rejects invalid hierarchy flags, missing/duplicate
IDs, cycles, children outside parents, overlapping authoritative siblings and
invalid renderer metadata. Starts use explicit v2 wrap epochs; legacy uses
frame-relative modular recovery. Durations at least half a 32-bit wrap are
rejected. Hidden multiple wraps inside an interval cannot be inferred.

## Observer controls

`hardware-trace.py controls MANIFEST.json -o REPORT.json` validates an explicit
control manifest with `matched_inputs` containing camera, replay, clock, scene,
pass_order and sample_window identities. Each chronological `runs` entry carries
`run_id`, `arm`, `elf_sha256`, identical `matched_inputs`, and equal-length arrays
`work_ms` / `period_ms` with at least two samples. Arms are `compiled_out`,
`runtime_off`, `coarse`, `detailed`; each needs at least two runs. The three
instrumented arms must share one exact ELF. `layout_notes` must explain the
separate compiled-out code layout. `brackets` lists chronological A/B/A run-ID
triples, covering runtime-off, coarse and detailed as middle arms.

The report retains both middle-versus-control differences and the control spread.
This is a measurement protocol, not evidence that a real target run occurred.
Match stable camera/replay/clock inputs and warmed windows on hardware. Code
layout, temporal state, pacing and observer scheduling can still change results.
Do not subtract a universal overhead constant or promise zero observer impact.

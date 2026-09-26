# Static submission batching

Static submission batching combines up to four consecutive small StaPip bags into one
native VIF1 DMA chain. It targets the fixed submission and wait cost measured
in the Motor District garage while preserving the existing render order and
image quality.

Batching is explicit. A caller opens `StaPipCore::beginSubmissionBatch()` only
around bags whose vertex, texture-coordinate, colour and normal streams are
owned and immutable through the renderer's next VIF1 synchronization after the matching
`endSubmissionBatch()`. The end call submits asynchronously; it is a submission
boundary, not a completion fence. Frame-owned static streams satisfy this rule
naturally. Generic `StaPipCore::render()` calls retain their existing behaviour
outside that scope. The scope is an ownership promise, not an opaque sorting
request.

The scope must end before external GS operations, view changes or pipeline
switches. Those operations are not implicit submission-batch boundaries; the
caller closes the scope first so pending PATH1 work is submitted in order.

Within a scope, only wholly direct, non-billboard bags using built-in programs
and at most 15 VU packages are retained. Installing any game-supplied program
override disables the scope globally: replacement writers have no enforced
packet-size ABI, and an override object's self-reported program name is not a
safe way to identify the slot it replaced. Textures qualify only when their VRAM allocation is
already resident and both wrap axes use REPEAT. Partial-frustum packaging, VU1
or EE clipping, qbuffer copy pools, billboard program switches, packet capacity
and the explicit scope end flush pending work. A texture miss or any other
allocation/content mutation invokes the registered cold-path barrier: it sends
pending geometry and drains PATH1 before eviction or PATH3 upload. Resident
binds remain drain-free. Thus an upload cannot reuse VRAM while queued geometry
still samples it, and copied qbuffer slots are never reused by an unsent packet.

Each bag keeps its own `FLUSHE`, uniform unpacks and geometry commands in the
original order. The chain is built contiguously in one double-buffered packet;
it does not join separately allocated packets with DMA `NEXT` or `CALL` tags.
MVP, light matrix, light directions and single-colour values are stored inline
because packet2 unpack helpers otherwise emit DMA references to caller or stack
memory. Geometry remains referenced only under the caller's explicit lifetime
promise.

The API is dormant until a caller opens a scope. Since editor 1.93, generated
games open it around the main Objects loop, closing it around reflected-object
probes and serialized per-object timing drains and before later passes.
Removing those scope calls provides the A/B fallback. `StaPipTelemetry` reports eligible bags, bags emitted through batch
packets and batch packet count so unsupported or zero-hit captures are clear.

An experiment that retained larger direct bags was rejected on physical PS2.
Although it reduced submissions further, garage render-submission time regressed
from 37.286/43.546 ms with the bounded resident-texture variant to
38.264/44.580 ms. Packet construction added about 0.2 ms and the garage VIF1 DMA
wait increased about 0.6 ms; no capacity split occurred in that capture. The
implementation therefore keeps the four-bag, 15-package limit.

Hardware acceptance needs the complete 105-asset fixture and identical parked
views. Compare unarmed render-submission time, packet submissions, triangle
counts, resource hashes and GS captures. Also capture a detailed timeline to
confirm fewer `VIF1_submit` events without increased waits, then run long enough
to expose intermittent DMA lifetime corruption. PCSX2 compilation and images
alone are insufficient because earlier packet-chain and qbuffer lifetime bugs
only failed on a physical PS2.

## Physical PS2 result: retained bounded implementation

The final `gated` variant is retained. It keeps four bags / at most 15 VU
packages per bag and removes avoidable eligibility overhead: reject structurally
ineligible bags before the extra texture lookup, and use a cached global
custom-program flag. A replacement program's own name is not a safe way to
identify the repository slot that was overridden. Installing any custom writer
therefore disables batching until all overrides are cleared.

Two fresh baseline boots reproduced submission time within 0.007–0.025 ms.
The table uses their midpoint and 240 warmed rows per pose for the candidate.
All variants used identical deployed resources and geometry on a physical PS2;
PAL 512x512 32-bit raster, parked traffic, existing host/debug cadence and
additional passes remained enabled. No detailed trace or GS capture ran inside
the 960-row timing window.

| Pose | Baseline submission ms | Retained submission ms | Saved ms | VIF submits/frame before → after |
| --- | ---: | ---: | ---: | ---: |
| Garage day | 37.989 | 37.040 | 0.949 | 156 → 129 |
| Garage night | 44.397 | 43.252 | 1.145 | 184.5 → 151.5 |
| Outer day | 16.331 | 15.961 | 0.370 | 65 → 57 |
| Outer night | 18.972 | 18.628 | 0.344 | 69 → 61 |

This is a 1.8–2.6% render-submission improvement, not a 50 FPS result. Measured
update + submission + finish work remains 46.59 / 52.92 / 24.90 / 28.74 ms.
These buckets exclude presentation and some outer-loop services; they must not
be inverted into claimed FPS. Sparse HUD samples remained roughly 17 / 16 /
25 / 23 FPS, with host/presentation variation large enough that this experiment
does not establish a whole-game FPS improvement. The night garage does show
about 1 ms less measured work, but other views are mixed.

The first resident-texture prototype saved 0.7–0.9 ms in the garage, but the
first safety-guard implementation reduced that to 0.15–0.22 ms. A separate
non-stress build reproduced the smaller gain. The retained cached flag and
early eligibility gate recover the saving without weakening safety. The
larger-bag variant is rejected; fewer DMA submissions alone were insufficient.

## Correctness and evidence

- Every timing arm has 960 unique finite/nonnegative rows and 240 samples per
  pose. All 105 PNG/TMDL/MTL SHA-256 values match; triangle means remain
  25650.5 / 26324.5 / 12154.5 / 12488.5 and steady-state reuploads remain zero.
- Both day captures are pixel-identical to the baseline. Night captures are
  checked against repeated baseline and same-build captures because stars
  twinkle and District night lamp 7 has authored flicker 0.025. The retained
  night garage differs in 989 pixels by at most one channel level; outer night
  differs in 121 pixels by at most two. The baseline
  repeat alone changes 9485 garage-night pixels by at most three channel levels.
- The separate post-measurement stress harness completed 240 forced texture
  evictions, including 220 that flushed pending batches, 12 pipeline switches,
  and 4588 total reuploads. Subsequent captures retained the textures and fresh
  frame progress. The final gate correction leaves this tested packet/barrier
  implementation unchanged.
- The retained detailed capture contains 4327 events across complete frames
  1500–1501 with no dropped events. Scopes overlap: VIF1 DMA wait is not a direct
  measurement of VU1 execution or hardware utilization.
- The Windows editor builds as 1.93.0, format 54 unchanged. Fresh code generation
  produces an Objects block identical to the checked-in generated example.
  The measured fixture compiles natively and boots on physical PS2. Linux and
  the full traffic-driving/custom-program scene matrix were not run here.

Packet storage is now two 784-qword buffers (24.5 KiB total), 18.7 KiB more
than the previous two 185-qword buffers. Generic callers keep the immediate
submission path unless they explicitly open a scope, but share this allocation.
Pipeline teardown drains outstanding texture readers before removing the
mutation callback. External render-target writes/view changes still require
the caller's explicit scope boundary.

[Raw CSVs, source snapshots, hashes, captures and reproduction recipe](../examples/vehicle-playground/authoring/submission-batching-2026-09-14/README.md)
record every variant, including the rejected ones. The large variant's detailed
trace was captured in outer-night pose 3, so only its four-phase timing CSV is
directly comparable; its trace must not be compared with the garage-day traces.

The next independent experiment is persistent static geometry preparation and
command data: stop reconstructing the same package metadata and geometry
commands every frame, with explicit asset/LOD/material invalidation and a
conservative clipping fallback. It needs its own A/B gate. Roads remain deferred.

The comparable garage-day traces show 156 → 129 VIF submits per frame,
mean inclusive VIF submit 3.000 → 2.247 ms and VIF wait 7.860 → 7.133 ms.
Packet-build event count remains 156 per frame: the change combines submission,
not the underlying per-bag geometry commands. Packet-build time is essentially
unchanged (2.929 → 2.936 ms), which is why persistent command preparation is
a separate next experiment.

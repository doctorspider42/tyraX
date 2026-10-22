# Producer attribution experiment

Private diagnostic, 2026-10-08. No production rendering defaults change.

The previous static-plan experiment selected only static batches and could not
explain the whole moving-frame cost. This experiment attributes actual StaPip
Core execution and VU input chunks to terrain, roads, batches, objects, vehicles,
effects, sky, procedural geometry, and an explicit other category. Main,
reflection and secondary views are separate. Deferred heavy submissions retain
their producer/view tags until execution. Mixed snapshot copies remain unassigned.

One ELF runs twenty phases: ten vehicle-free camera phases, then ten ordinary
three-vehicle full-throttle phases. Each workload uses off, census, off, six
selected-producer timing phases, off. Census has no scoped clocks. Timing phases
have no census counters. Both production geometry caches remain enabled.

Rotation samples 120 parked, 360 one-degree rotation, 120 parked frames. Driving
samples 120 parked frames, then full throttle to z >= 108. Drive warmup is 1200
frames; rotation warmup is 1200 initially and 360 after reload. Vehicle-free
phases deactivate vehicle objects and simulation at runtime; assets remain in
memory. Rotation also suppresses environment captures triggered by those resident
vehicle materials (the previous truly vehicle-free fixture had zero environment
users); it restores the material-user count after rendering. Drive arms retain
normal dynamic reflections. This is an explicit workload distinction, not a fix.
All arms retain normal synchronization.

Core elapsed includes stalls. Nested explicit wait leaves are reported separately;
subtracting them does not prove pure EE cost. Geometry outside StaPip remains in
the whole-frame residual. Census bookkeeping is included in measured frame time.
Observer-off controls must be checked before interpreting scoped measurements.
Category sums from different timing runs are not a simultaneous frame breakdown.

Tools: `tools/tyrax2-producer-probe.py`, its private header,
`tyrax2-producer-analyze.py`, and `verify-producer-probe.py`.

## Completed evidence

All twenty emulator phases and twenty physical PS2 phases passed. Hardware
recorded 11,770 rows; emulator recorded 12,540. The same performance ELF was used:
`f450aa6e54c63ef95721485836f73985968f12fe12bf4d92aafb7c640e35db55`.
Native mirror/source identity covers 466 files. Host tests exercised the actual
ledger with only its EE clock substituted, and rejected nine malformed evidence
inputs against both full runs. No production optimization was applied.

Raw data, analysis, source snapshots and launch authority are in
[the evidence archive](tyrax2-producer-2026-10-08/README.md).

## Physical attribution

Milliseconds below are actual Core invocation elapsed time averaged over all
moving frames, including frames where the producer did not run. Each producer
was timed in a separate run; do not sum this into an exact simultaneous budget.

| Producer | Rotation, main | Drive, main | Drive, reflection |
| --- | ---: | ---: | ---: |
| Terrain | 3.496 | 2.146 | 0.158 |
| Roads | 1.602 | 1.593 | 0.101 |
| Solo objects | 0.908 | 0.805 | 0.043 |
| Vehicles | 0 | 1.139 | 0 |
| Static batches | 0.645 | 0.798 | 0 |
| Sky | 0.811 | 0.559 | 0.273 |
| Effects | 0 | 0.454 | 0 |
| Other | 0.682 | 0.675 | 0 |

No instrumented explicit wait leaf was entered inside these selected Core
scopes. This narrows the location of the measured work; it does not measure
memory stalls, preemption, VU/GS execution or waits outside Core. Reflection
columns are geometry preparation only, not total capture cost/barriers.

Terrain grows from 2.634 ms before rotation to 3.496 ms during rotation;
30-degree bins range from 1.992 to 4.518 ms. However, the whole rotated view
does **not** have a higher mean cost than the starting view: many directions
contain less geometry. The issue is angle-dependent peaks, not a uniform
per-frame rotation penalty.

The census gives a more specific mechanism:

- Terrain generates 136.28 clip chunks/frame during rotation, about **70.0%**
  of all clip chunks. Each contains only **11.46 input vertices** on average.
- Terrain rotation submits 2,327 cull vertices and 1,561 clip vertices/frame.
  Only 33.47 vertices/frame are freshly strip-expanded; 1,441.23 come from the
  expansion cache. That is **97.73% cache reuse among expanded strip vertices**,
  not a cache-hit percentage for all terrain geometry.
- During driving, vehicles submit 8,048 cull vertices/frame and no clip chunks
  in this route. Main-view terrain submits only 2,361 total vertices/frame,
  yet costs roughly twice as much in Core. Vertex count alone is a poor proxy
  for this preparation cost.
- Main-view drive terrain produces 40.01 clip chunks/frame, plus 5.28 in
  reflection; roads produce 24.94 plus 3.62. The reflection workload is active,
  not frozen or removed from the driving comparison.

## Observer controls and limits

Rotation observer-off work means are 10.712 / 10.841 / 10.758 ms; the census is
10.969 ms. Census overhead relative to the adjacent controls is about
0.13-0.26 ms. The initial off rotation reaches 19.049 ms, with p95 15.188 ms,
despite its low mean. Its loop rate is 58.80 Hz versus 59.94 Hz parked.

Drive observer-off work means are 13.993 / 14.051 / 14.066 ms. Their loop rates
are 51.65 / 52.32 / 51.65 Hz, with p95 work 18.602 / 19.089 / 18.782 ms.
Thus this is still **not stable 60 FPS**. Census work is 14.301 ms, roughly
0.25-0.31 ms above adjacent controls. Whole-work means subtract the renderer's
recorded pacing interval, not every possible wait.

Selected-timing moving means are near those controls (rotation 10.837-10.984 ms;
drive 14.083-14.185 ms), but the vehicle-timing parked phase is an outlier:
14.941 ms versus roughly 14.0 ms in off controls. Do not interpret scope timings
as unbiased optimization deltas. Drive phases have different frame counts;
compare the stored 20-unit position bins, not frame indices. Only one census
pass per workload was taken, so repeated semantic-count equality is not proven.

This is a day scene, a specified fixed-eye sweep, and one straight drive. It
does not establish night, arbitrary-camera, multiple moving traffic or exact
framebuffer equivalence. The other category remains explicit. DynPip and work
outside StaPip Core are not covered by producer timings. Runtime-hidden cars
retain allocations; rotation is not memory-identical to removing them in the
project. No Burnout debugger comparison was performed in this experiment.

## Next intervention

Target `StaPipCore::renderStrippedPkgs`, especially terrain, before another
general batch cache. A partial strip package currently goes straight into
`clipPackageSize()/3` triangle chunks; each chunk is sent to `clip` without
a smaller spatial classification. Reusing the expanded arrays avoids rebuilding
vertices but leaves this repeated dispatch work intact. This is a plausible
optimization target supported by attribution, **not yet a demonstrated fix**.

Prototype versioned, local-space bounds for small strip subranges. For a partial
parent, conservatively reject outside subranges; route wholly guard-band-safe
subranges to pointer/cull submission and combine only compatible adjacent ranges;
retain the existing clip path at actual boundaries. Preserve triangle order,
strip parity, material passes and data lifetime. Do not simply enlarge clipping
chunks: their capacity includes worst-case VU output expansion.

Acceptance: unchanged geometry/image correctness at near-plane and guard-band
edges, actual reduction in terrain clip dispatch, then a positive same-route
observer-off PS2 A/B/A full-frame result. Do not promote on Core clocks alone or
promise stable 60 from the 3.5 ms inclusive terrain figure.

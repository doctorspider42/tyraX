# Night producer activation and observer trial

A private same-ELF hardware trial isolates five preparation scopes from three
EE audit leads: projected flashlight receiver output, flashlight terrain hull,
beam scratch generation and the separate corona/cone output commits. It keeps
all effects and submission paths, measuring observation rather than an
optimization.

## Scope boundaries

| Stage | Included work | Sparse units |
|---|---|---|
| 0 projectedReceiver | Receiver geometry preparation, triangle facing/budget walk, copied positions, projective STQ and reach colors | triangle visits, output vertices |
| 1 terrainHull | Ground sampling plus bilinear-sheet error/max fold and hull heights | ground samples, cells, comparisons |
| 2 beamScratch | Scratch clearing, lamp selection and corona/cone scratch construction | corona vertices, cone vertices |
| 3 coronaCommit | Existing write-on-change comparison/resize/write of corona positions, ST and colors | requested bytes, changed call flag |
| 4 coneCommit | Existing write-on-change comparison/resize/write of cone positions and colors | requested bytes, changed call flag |

Scopes are disjoint. Draw submission, stream binding, bbox stamping and consumer
completion are outside the beam commit scopes. The receiver scope would include
first-use geometry rebuild if activated; the terrain scope includes irregular
road queries. These are elapsed scopes, not purely scalar EE arithmetic bills.
The two flashlight scopes do not cover every projected vehicle/headlight or
scene-light path.

## Observer and limits

Kind13 uses the ordinary full-night sampler, native 24-bit configuration and
unchanged pool-table mode. Off/On/Off and On/Off/On toggle only scope clocks.
On reads Count twice per activated scope during loops 800..1119, accumulating
calls/read counts and uint64 elapsed ticks. Off adds no scope clock reads.
Sparse workload counters run at 750/1155, outside that interval; the 1155 record
exports the completed 320-loop scoped totals. No per-object fences, GPU holds
or host I/O are added inside the priced interval.

The phase's inherited countReads field describes ordinary sampler clocks only;
NIGHTPROD reads separately accounts for additional clocks. The two diagnostics
must not be conflated. Timed scope calls provide whole-window activation;
sparse vertex/byte/change counters remain snapshots, not whole-window totals.

Same-ELF loop contrasts measure enabled observation's net effect, including
induced scheduling and consumer overlap. Common disabled branches, cold-counter
guards, additional storage, code layout and stack footprint are retained in
both arms and unpriced. Scope timestamps include the clock bracket and possible
preemption. Do not uniformly subtract whole-loop observer contrasts from each
scope or compare this compiled Off arm against another ELF as a pure overhead
measurement.

The instrumented native build binds 501 source inputs, linked ELF/symbol text,
298 assets and unchanged resident microprogram images. Only four postimages
differ from the ordinary corona24 pricing baseline: one generated lighting
source and three debug headers. Production engine and generator code are
unchanged. The target ELF SHA-256 is
`1d7d1b6d8bba4d63f1986063b82133b678c383a1ef90f5f01aed4412f2bc05d9`.

## Physical result and next priority

Both physical boots complete 5400 loops, 384 raw samples and 15 chunks. Across
three On windows, the receiver and terrain-hull scopes have zero timed calls
in all 960 priced loops. They are flashlight routes disabled while driving;
their source complexity cannot explain this fixed car/night frame time.
This does not qualify their cost when walking or exclude vehicle/headlight
or other scene-light producers.

Each active beam scope executes exactly once per priced loop, with 320 calls
and 640 additional Count reads per On window. Scoped means per loop are:

| Scope | Order 0 On mean (ms) | Order 1 two On mean (ms) |
|---|---:|---:|
| Beam scratch | 0.133404 | 0.134481 |
| Corona commit | 0.030441 | 0.029952 |
| Cone commit | 0.071268 | 0.070947 |
| Disjoint scope sum | 0.235113 | 0.235379 |

The three individual On-window sums are 0.235113, 0.235177 and 0.235582 ms.
Twelve sparse witnesses each report 48 corona and 192 cone vertices, 2304
corona bytes and 6144 cone bytes requested for comparison/commit. Both commit
changed-call flags are zero at every witness. Those flags do not prove zero
writes across the entire priced window. All rendered flip-return periods
remain approximately 33.367 ms.

Observer On minus its own mean Off controls is -0.079212 ms in order 0 and
+0.001250 ms in order 1. Corresponding outer-arm spreads are 0.153405 and
0.014845 ms. The enabled tax is not resolved as a stable positive quantity;
the forward negative contrast is not a speedup and does not imply zero-cost
instrumentation. Both source/ELF orders and all adjacent contrasts are retained.
Do not subtract these noisy net contrasts from the scoped times. The scopes
repeat closely, but remain instrumented elapsed observations, not guaranteed
recoverable frame-time savings.

This deprioritizes these three leads for the missing multi-millisecond budget
in the fixed night view. A bounded static-cone producer change may still be
useful, but first revisit actual model preparation and additional night passes.
The previous [Core prefix/submit scopes](tyrax2-prefix-submit-scopes-2026-10-04.json)
retain larger inclusive regions; their earlier ELF times must not be subtracted
from this trial or relabelled as pure EE execution. Moving/walking/showcase
fixtures are needed to activate the two flashlight routes and other conditional
audit findings.

Both physical orders and derived numbers are archived in the
[source/evidence checkpoint](tyrax2-night-producers-2026-10-05/README.md).
No emulator timing, raster equivalence or optimized-renderer promotion is
claimed by this observer experiment.

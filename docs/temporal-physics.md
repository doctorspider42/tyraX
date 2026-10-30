# Temporal physics experiment

Temporal physics adds conservative collision caches, persistent contact impulses
and independent dynamic islands without reducing collision coverage or solver passes.

## Asynchronous VU0 SAT/CCD experiment

The final physical same-ELF timing completed **16,800 measured frames** across
28 phases. A separate run of that exact ELF shadow-checked **101,641 jobs** with
zero hard errors or missed reference hits; 13,695 small accepted differences
remained in the simulation. Timing disabled reference comparisons and recorded
258,201 jobs plus 27,280 observations of EE query work while VU0 was busy.

Driving-phase FPS, pooling both reversed-order repeats:

| Added cubes | Layout | Scalar | VU0 micro |
|---|---|---:|---:|
| 32 | Spread | 59.94 | 59.94 |
| 64 | Spread | 59.94 | 59.94 |
| 128 | Spread | 59.20 | 58.88 |
| 32 | Pile | 49.04 | 50.77 |
| 64 | Pile | 33.25 | 37.14 |
| 128 | Pile | 19.92 | 19.72 |

This does **not** establish a general kernel speedup: variable dt and accepted
numerical differences produce different trajectories and contact populations.
For 128 piles, micro repeats were 18.67/20.91 FPS versus scalar 19.30/20.57;
the outcome is mixed. Dense 60 FPS remains unqualified and the backend stays
**default-off**. All generated contacts and vehicle candidates passed capacity
accounting; reference geometry was checked in shadow mode, not during timing.
The [timing report](../tools/ps2rig/results/vu0-micro-2026-10-29.json),
[timing frames](../tools/ps2rig/results/vu0-micro-2026-10-29.csv.gz) and
[qualified shadow](../tools/ps2rig/results/vu0-shadow-2026-10-29.json) retain the
source/ELF/assets and per-phase raw hashes, scopes and both repeats.

The independent stationary 64-cube gate completed **5,040 measured frames**
with scalar/micro order reversed on the second repeat. All four arms had zero
awake bodies and zero squared-velocity sum in the final 120 park and recovery
frames. The known base impulse woke bodies and both implementations recovered.
The 26,300 micro jobs passed full scalar shadow checks with no missed hits or
capacity omissions. This is a bounded settling test, not universal stability or
performance evidence; see the [settle report](../tools/ps2rig/results/vu0-settle-2026-10-29.json).

Final default-path qualification used a freshly generated Release furniture
scene with production Pad code and `TYRA_PHYSICS_MICRO=0`: native build plus a
private PCSX2 software-renderer run, 46 finite-state samples over 37 bodies and
three captures. The unstripped symbol audit found no SAT backend functions or
batch storage; its stripped relink matched the tested ELF byte for byte.
See the [smoke metadata](../tools/ps2rig/results/vu0-default-smoke-2026-10-29.json)
and [capture](../tools/ps2rig/results/vu0-default-smoke-2026-10-29.png).

The interpenetration diagnostic completed on physical PS2: **720 physics calls**,
including the first call after spawning 64/128 spread, partially overlapping and
coincident-centre cubes, with scalar and micro-shadow arms. No EE/VU0 division-by-zero,
invalid-operation or overflow flags were observed. Intentional EE `1/0` and `0/0`
first verified the exception monitor. Every generated contact was retained and
all runtime shadow comparisons passed the practical tolerances below.

The scalar first call for 128 coincident cubes generated **8,156 contacts** and
took **474 ms**; partial overlap produced 6,424 contacts and 220 ms. The run
completed, reproducing an apparent freeze through contact-density cost rather
than an arithmetic exception. The 128 cubes alone have 8,128 possible unordered
pairs. This evidence does not exclude unrelated hangs in other scenarios.
Per-call diagnostic logging changes subsequent real dt, and micro-shadow also
executes the reference; those timings must not be used as an FPS/backend A/B.
See the [report](../tools/ps2rig/results/collision-overlap-2026-10-29.json),
[raw frames](../tools/ps2rig/results/collision-overlap-2026-10-29.csv.gz) and
[full log](../tools/ps2rig/results/collision-overlap-2026-10-29.log.gz).

`TYRA_PHYSICS_MICRO` is an experimental, default-off backend for dynamic OBB
pairs that reach SAT after the existing corner tests. Seven jobs fit VU0's
4 KB data memory. VU0 computes every existing face/edge axis projection,
minimum penetration and linear time interval. EE reconstructs the winning
support feature with the original scalar normalization, preserving feature
selection when rounding would otherwise select a different corner. Ambiguous
normal signs fall back to the complete scalar algorithm. Other shapes and
world contacts retain their algorithms. Solver passes, colliders, candidate
coverage and contact order remain unchanged.

While one batch runs, EE builds the next using scalar RbV and broad-phase
code. Deferred contacts reserve their original positions; stable compaction
removes misses after the final completion fence. VU0 memory accesses happen
while idle. Macro-mode transforms disable the asynchronous route. Built-in
ray-tracing and generated compute wrappers share a micro-image owner token
and reload their programs after an owner switch.

The next scheduling iteration starts an already prepared packet immediately
after harvesting the previous results, before EE reconstructs those previous
contacts. The two input buffers remain immutable until their result consumption
finishes; the outer pair loop can reuse a buffer only afterwards. Reconstruction
and scalar positive-CCD confirmations use no COP2 instructions while the next
packet runs. Contact insertion/compaction and solver order remain unchanged.
The chained schedule and near-minimum guard passed a subsequent physical
1,200-frame shadow run with 95,798 jobs, no missed hits, hard errors or capacity
omissions. The whole-scene timing table above still describes the preceding
implementation; shadow-mode frame times are not a performance comparison.

The fixed-input `--micro-stream` probe separates scalar SAT/CCD, the preceding
micro schedule, chained micro scheduling, packing alone and chained prepacked
input. It uses mixed and coincident-centre corpora with eight reversed-order
repeats. Geometry validation runs outside timing, and micro schedules must
produce identical contact hashes. Neither solver work nor host I/O is included.

The follow-up scopes numerical uncertainty to the current winning or competing
minimum axes. A clearly worse axis cannot mark the contact ambiguous; a clearly
better minimum clears superseded uncertainty while retaining the independent
near-parallel input classification. A controlled aligned-box case must keep its
unambiguous Z contact without scalar confirmation, while the distinct near-minimum
case must still request canonical selection. The preceding kernel fails the new
unambiguous-winner check. The follow-up passed its own physical 1,200-frame shadow gate with 84,712
jobs and zero hard/missed/accounting errors; the preceding table remains a
separate implementation measurement.

`--micro-kernel-baseline <commit>` installs a compatible reference micro image
only in the private benchmark engine. Both assembled kernels validate the same
input arrays before measurement; mode 5 times the reference with chained
scheduling. Micro-image upload happens outside timed samples. The fixture now
includes ordinary offset overlaps as a third corpus and captures/replays all
input bytes in `vu0-stream-input-*.bin`, with per-corpus hashes in every CSV row.
This isolates kernel changes without relying on repeatable pile trajectories.

The first semantic gate runs the actual VCL source through the host VU0
simulator against `convexMotionContact`: 10,000 rotated/swept pairs, including
stationary and coincident-centre cases. Classifications agree; measured depth,
normal and point errors are zero after winning-feature reconstruction. The
first openvcl image is 2,048 bytes. These gates do not qualify assembled-code
execution or establish a console speedup; console validation and same-ELF
timing remain required.

The extended gate adds 2,048 cases in batches of up to seven, with six face normals,
touching faces, near-parallel orientations, large coordinates and separated
CCD endpoints. The data-memory sentinel survives. A deliberately inverted
axis gate fails immediately, proving the oracle observes the tested path.
Near-parallel jobs use the original scalar tie policy; the runtime does not
queue those jobs. Rejected micro jobs also restore separating-axis history.
The default-off runtime compiles out batch storage and VU0 calls.

`collision_stress.py --micro-comparison --baseline 0647f5596` prepares the
same-ELF 28-phase console comparison. It runs an assembled-kernel 2,048-case
oracle before measurements. A boot-time `bin/sat-verify.cfg` enables shadow
comparison of runtime micro jobs with the full scalar result; remove the file
for timing the same ELF. Per-phase `vu0-phase-*.csv` records actual jobs,
batches, busy observations during EE queries, completion waits and validation
errors. Busy observations prove overlap at those sample points, not overall
VU utilization. Shadow-enabled runs are correctness evidence, not performance.
Shadow mode runs the +128 pile pair (phases 12 and 13); timing runs all 28 phases.

The initial assembled image was rejected in PCSX2: openvcl paired an upper
`addx.x` with lower `mfir.y` to the same VF register, and the overlap flag
retained old bits despite the disjoint component masks. The source-level
oracle cannot model this scheduling fault. An explicit scheduling barrier
fixes it: the next assembled startup oracle passes all 2,048 cases with zero
depth/normal/point error. Explicit CLIP initialization and exit directives
also let Sony VCL assemble the kernel (2,144 bytes, within 4 KB).

The first physical startup gate rejected four positive CCD cases out of
2,048: normal differences below 9e-8 still changed strict support-vertex
selection by up to 1.94 units. The corrected backend confirms positive
linear sweeps with the original EE algorithm. VU0 still evaluates the full
SAT/CCD rejection; it never drops an axis or replaces a collider. These
confirmations are included in the backend's eventual performance cost.
Physical startup and full-scene shadow validation must pass before timing.

### Physical equivalence, rather than bit identity

The practical shadow gate permits depth error and additional shallow touches
up to **0.01% of the smaller body's radius**, contact-point displacement up
to **1% of that radius**, and normal-vector error up to **0.01** (about 0.57
degrees for unit normals). Every lost reference hit remains a hard failure.
For a 0.9-unit cube these are roughly 0.00008 units of depth and 0.008 units
of point displacement. The existing solver's penetration slop is 0.005 units.
These are diagnostic acceptance thresholds; they do not change colliders,
SAT axes, solver slop, iteration counts or the collision algorithm.

Small accepted differences remain in the simulation, and are counted separately
from hard failures. This avoids testing a scalar-repaired trajectory while
timing another one. Large CCD support-feature changes still use the original
scalar confirmation. Near-equal discrete minimum axes may differ within the
physical tolerances. A subsequent chained-schedule shadow run exposed a rare
contact-point difference of 1.54% of radius, exceeding the 1% gate despite a
normal difference of only 0.00142. Distinct near-minimum axes now request the
canonical scalar feature selection. This guard preserves the existing tolerances
instead of widening them; its dedicated host case fails without the guard.
The sidecar records numerical differences, extra shallow touches, missed
reference hits and maximum normalized errors. `vu0_sat_analyze.py` validates
those bounds along with every-contact accounting, complete phases, finite
state, parent/child scopes and sampled EE/VU0 overlap. A ray-tracer/SAT/ray-tracer
micro-image round trip also checks ownership reloads at startup.

Scalar confirmations and the diagnostic reference use one compiled EE routine.
Object inspection found that the compiler inlined a separate copy into the
confirmation path while the game also emitted an out-of-line copy. FPU
contraction/scheduling differences between those EE copies could exchange
strict CCD support vertices even without a VU arithmetic error. Calling the
same compiled scalar body avoids that misleading comparison and stabilizes
the confirmed feature; ordinary scalar physics keeps its existing algorithm.

Aligned packet uploads and result readback use the EE integer `LQ/SQ` path,
moving 128 bits at a time without COP2 instructions. The single-word control
write and any unaligned endpoint retain a scalar copy. These accesses remain
inside the VU0-idle ownership fences; packet storage is explicitly 16-byte
aligned. Transfer, packing, launch, completion and scalar confirmations all
belong to the backend cost measured in the same-ELF comparison.


## Selected defaults and final PS2 result

The refined geometry/admission cache is enabled by default
(`TYRA_PHYSICS_COHERENCE=1`). Warm starting and coordinated island sleep/wake
remain opt-in (`TYRA_PHYSICS_WARM_START=0`, `TYRA_PHYSICS_ISLANDS=0`): their
fixed-iteration dense-pile results are worse. The prepared-solver experiment
also remains disabled by default. No collider, contact or iteration is removed.
Cache storage is allocated when active physics needs it; empty/sleeping scenes
do not initialize the scene-sized collider cache, and disabled islands do not
allocate a scene-sized membership map.

The final same-ELF cache-only comparison from `3fa710314` completed 28 phases
and 16,800 measured frames. Pooled drive FPS (720 frames per cell):

| Additional piled boxes | Prior | Refined cache |
| --- | ---: | ---: |
| 32 | 50.89 | 49.89 |
| 64 | 35.90 | 37.56 |
| 128 | 18.32 | 20.58 |

At +128 this is about **12.3% higher FPS**, and mean physics time falls from
32.17 to 27.48 ms. Body-grid candidate visits average 1,623.8 versus 774.1 per
frame, about **52% fewer**, with every generated contact retained. At +64 visits
fall from 476.8 to 268.7. These are candidate visits, including the grid's own
and duplicate-direction entries, rather than discarded collisions.

The gain is workload-dependent: +32 regresses slightly. +128 repeat FPS is
18.55 -> 19.30 and 18.09 -> 22.04. Frame timing changes the engine's variable
timestep and subsequent contact trajectories, so this is a whole-game test,
not identical-input kernel timing. Both repeats and stage/candidate counters
are retained in the [final summary](../tools/ps2rig/results/temporal-refined-2026-10-29.json)
and [raw frames](../tools/ps2rig/results/temporal-refined-2026-10-29.csv.gz).
The ELF SHA-256 is
`c5417acd86fba2cc1b5a446c91712d62dfe4748ce5bd0e5f55d1e7aacc9a3141`.

The 56-phase exploration, eight-phase stationary gate and 28-phase follow-up
cover **60,480 measured physical-console frames**. All arms retain every
generated contact and test every vehicle candidate, with zero omission/overflow
counters and passing timing/finite-state gates. Assets are complete and shared;
the private fixture disables physical padman polling/rumble in every arm while
injecting the same controls. Production Pad code is unchanged.

This does **not** reach 60 FPS in dense active piles: +128 physics alone still
exceeds the 16.68 ms whole-frame budget. These results do not establish the
console's hardware limit or a universal speedup across scenes.

The previous scalar integration is the reference. A same-ELF four-arm rig
compares reference, geometry/axis caches alone, caches plus warm starting, and
caches plus warm starting and islands, with reversed arm order in a second repeat. Stage timings include
prediction, world contacts, body-grid construction, body contacts, solver and
write-back; nested query/SAT scopes must not be added twice to their parents.
Host gates cover cached-axis CCD conservatism, contact identity/invalidation,
unbounded history storage and stable island partitioning. Full-game and
physical-console qualification are recorded below.

## Implementation and gates

`TYRA_PHYSICS_COHERENCE` controls persistent world-box transforms and per-step
hull normal/edge transforms, plus a previous-frame separating-axis hint.
`TYRA_PHYSICS_WARM_START` controls impulse history, and
`TYRA_PHYSICS_ISLANDS` controls stable component grouping and coordinated
sleep/wake. The selected defaults are 1/0/0 after console comparison.
The prepared scalar/VU0 experiment remains separate and disabled in this rig.

Host gates passed 50,000 swept/rotated box pairs, including arbitrary cached
axes and separated-endpoint crossings. History gates passed one-to-one
matching, shape/state/scene/timestep invalidation, 4,096 simultaneous records
and stable island order. The first native integration build exposed an orbit
header dependency and a timing-scope visibility issue; both were corrected.
External-change revisions now live in always-regenerated side state, preserving
owned header layouts. The subsequent full native PS2 build passed.

The benchmark adds squared-velocity and maximum contact-depth diagnostics.
Squared velocity is not mass-weighted kinetic energy, and its units depend on
the frame timestep. Driving adds physical impulses, so settling and wake
behaviour must be assessed alongside FPS, rather than requiring monotonic
energy during a car impact.

The isolated PCSX2 software-renderer run completed six 600-row phases,
including both full-feature drive trials. Contact/candidate accounting and
disjoint-stage timing checks passed; history recorded 2,636 and 2,300 matches
in the two full-feature phases. No assert, exception or missing-resource
diagnostic occurred. The later physical qualification is recorded below.

## Stationary settle and wake gate

`collision_stress.py --settle-check` uses the same four arms and reversed
repeat order, with 64 piled boxes and no driving. After 150 warm frames it
records 600 park frames, applies one horizontal 0.12 per-frame velocity impulse
to a base box, records a 60-frame wake interval and 600 recovery frames.
All eight phases must have 1,260 rows. The test checks late-park/late-recovery
motion and the first frames after the impulse, in addition to complete contacts
and finite disjoint/nested timing scopes. It does not replace representative
non-box or moving-support qualification.

Fixture qualification caught a missing reflection-panorama binary in a manual
asset copy; that attempt is rejected. Baked binary resources must be copied
alongside textures/models, and the complete 283-file cache is hash-qualified
before hardware timing. A premature manual launch without the resident-IOP
marker required a console power cycle; no result from that attempt is used.

The six-phase stationary software-emulator gate completed 7,560 rows with full
contact/candidate accounting and finite, consistent timings. Both full-feature
repeats had zero awake bodies and zero squared-velocity sum in their final
120 park and recovery frames. Their first wake frames activated 64 and 60
bodies, versus 2 and 1 in the prior-solver repeats: whole-component waking is
functionally conservative but can increase transient workload substantially.
This motivates measuring warm starting separately from coordinated islands.
Emulator timings are not physical PS2 performance evidence.

## Separate warm starting from island wake cost

The initial three-arm stationary probe is preserved as
[stability summary](../tools/ps2rig/results/temporal-settle-emulator-2026-10-29.json)
and [raw rows](../tools/ps2rig/results/temporal-settle-emulator-2026-10-29.csv.gz).
It is explicitly emulator-only, not a performance qualification. The revised
rig uses four arms by default: reference, cache, cache + warm starting, and
cache + warm starting + islands. The full driving matrix is 56 phases and
33,600 measured frames; the stationary matrix is eight phases and 10,080 rows.
Use `--temporal-arms 3` only to replay/analyze the initial prototype.

## Physical-pad transport isolation

The first qualified four-arm hardware attempt completed only two control
phases, then stopped in repeated `freepad: DMA Busy` output during scene
reload. It is incomplete and provides no performance qualification. Temporal
fixtures now initialize neutral synthetic pad state and skip physical pad
polling and rumble RPCs in their private engine copy, identically in every arm.
Driving already comes from the rig's direct vehicle inputs. Production Pad
code remains unchanged. This isolates the physics experiment from resident-IOP
pad transport; it is not a fix or a qualification of that transport.

## Profile-guided pair admission follow-up

The first four-arm console trial attributes a large share of dense-pile cost
to world/body contact work and nested broad-phase queries. The follow-up uses
the existing swept hull/sphere envelope as the awake body's grid box and query
area. A sleeping hull uses a cached local AABB transformed conservatively at
its current pose; local bounds invalidate on shape/external-change revisions.
Outward rounding and query padding preserve the later envelope test's tolerance.
Only candidates that would fail that existing test can be removed early.

The cached separating-axis test also uses analytic OBB projection for known
boxes. Successful overlap axes are discarded rather than retested every frame.
These changes retain every face/edge SAT axis and every solver pass.

The host pair-grid oracle runs 200 scenes of 128 off-centre hulls/spheres,
with awake motion and sleeping rotations. All 39,126 exact-envelope pairs
survive 17,171 awake queries in the same order. Candidate visits fall from
247,114 to 84,568 in this host dataset; this is not a PS2 timing claim. The
50,000-pair cached-axis SAT/CCD oracle and contact-history gates also pass.
A focused `--temporal-comparison --temporal-arms 2` run compares prior against
the refined cache path with warm starting and islands compiled out, across
28 phases / 16,800 measured frames. It records body-grid candidate and later
bounds-rejection counts alongside the stage timings. Results are recorded above.

## Completed four-arm physical PS2 trial

The implementation from `407585452` completed all 56 phases and 33,600 frames
with the four arms in reversed repeat order, complete 283-file baked assets,
and synthetic physical-pad isolation. The ELF hash is
`aafd0828e259a1a581f740d3ad3e25b6ad6297538290af5d2a7e84632603d24f`.
Every generated contact was retained and every vehicle candidate was tested;
all omission counters were zero. Disjoint and nested timing gates passed.

Pooled drive FPS (720 frames per cell):

| Piled boxes | Prior | Cache only | Cache + warm | Cache + warm + islands |
| --- | ---: | ---: | ---: | ---: |
| 32 | 48.88 | 49.49 | 47.01 | 49.95 |
| 64 | 34.50 | 34.53 | 33.51 | 27.68 |
| 128 | 20.75 | 20.40 | 18.69 | 14.18 |

The first cache implementation does not establish a general frame-rate gain.
Warm starting and coordinated islands stay opt-in: with the fixed 6+3 passes,
bookkeeping adds cost, and waking complete components substantially increases
active work. At +128, average drive awake bodies are 58.6/51.8/63.8/114.7 and
contacts 449.9/413.9/458.6/872.5, respectively. Inclusive solver cost per contact
is 18.86/19.14/22.73/23.28 microseconds. These are workload ratios, not isolated
identical-input arithmetic benchmarks.

Mean +128 physics time is 26.58 ms prior and 48.22 ms with the full experiment.
The prior solver uses 8.49 ms; world/body contact stages use about 5.5/10.1 ms,
with roughly 6 ms of nested query time across those stages. Grid build itself
is about 0.26 ms. This identifies pair admission and queries as useful next
targets, rather than assuming the grid construction or impulse arithmetic is
the dominant cost. Timestep/contact trajectories vary, so both repeats remain
in the [complete summary](../tools/ps2rig/results/temporal-four-2026-10-29.json)
and [raw frames](../tools/ps2rig/results/temporal-four-2026-10-29.csv.gz).

A separate software-renderer non-box smoke run exercised 37 authored bouncy
bodies/furniture with all features enabled and normal production Pad code.
Across 35 telemetry samples all positions, rotations and velocities were
finite. Squared-velocity sum ranged up to 0.743 and ended at 0.0094; the authored
periodic ball impulse remained enabled, so monotonic energy decay is not
expected. Three captures rendered correctly. This is functional coverage, not
a physical-console performance or complete non-box settling qualification.

![Physical PS2, full experiment with 128 boxes before the drive](img/temporal-physics-console.png)

## Physical stationary settle and wake qualification

The eight-phase four-arm stationary gate completed 10,080 measured rows on
physical PS2 with the same `407585452` implementation and synthetic-pad setting.
Every generated contact and vehicle candidate was retained/tested, and timing
and finite-state checks passed. Both full-island repeats had zero awake bodies
and zero squared-velocity sum in their final 120 park and recovery frames.
The first three frames after the known base impulse activated 60/60/60 and
63/63/63 bodies, versus 1/4/5 and 1/6/6 in the prior repeats. Warm-only activated
1/5/6 and 2/6/6; one warm-only repeat retained one slowly moving original prop
with late energy about 7.3e-7. Thus convergence/recovery works, but complete
component waking has a substantial transient cost and is not the default.
The [stationary summary](../tools/ps2rig/results/temporal-settle-2026-10-29.json)
and [raw frames](../tools/ps2rig/results/temporal-settle-2026-10-29.csv.gz)
retain both repeats. This gate does not establish universal sleep stability.

The refined admission implementation passed a native PS2 build and isolated
software-renderer startup; both comparison arms produced complete 600-row CSVs
including candidate/rejection counters, without assert/exception/missing-resource
diagnostics. The conservative-bound gate also covers cancellation at large
off-centre coordinates using a double-precision eight-corner oracle.

The refined bound implementation was also exercised with all three feature
flags enabled in the 37-body software-renderer furniture scene. All 34 samples
were finite; sampled awake counts and squared-velocity sums matched the prior
implementation exactly at the same frame numbers. See the
[refined non-box smoke evidence](../tools/ps2rig/results/temporal-refined-nonbox-emulator-2026-10-29.json).
This does not prove full-state identity or qualify physical-console non-box
performance. The final lazy-allocation integration preserves these admission
and solver rules.

Final integration gates also passed: a Release editor build, refreshed vehicle
and physics example snapshots matching the exact template function, and a
native build/software-renderer smoke run of the selected 1/0/0 defaults with
production Pad code. All 18 samples over 37 authored bodies were finite and
the capture rendered correctly. The [default smoke metadata](../tools/ps2rig/results/temporal-default-smoke-2026-10-29.json)
records working-source/function hashes and the ELF; this gate includes the final
lazy-allocation control flow, rather than claiming that an unmeasured stock ELF
has exactly the benchmark's frame rate. The console was returned to ps2link.

The first fixed-input scheduling probe at `704637a51` measured mixed-pair
medians of 28.755 us (preceding micro) versus 19.197 us (chained), and coincident
centres of 56.461 versus 34.076 us. Scalar was 16.744/27.390 us. The associated
full-scene shadow had one point-tolerance failure and is rejected; these backend
costs alone do not qualify the new runtime. Profiling now distinguishes scalar
CCD, tied-axis/sign and near-parallel confirmations so duplicated EE work can
be attributed before another kernel redesign.

The qualified fixed-input follow-up uses the same ELF for each schedule, with
the near-minimum guard and confirmation counters enabled:

| Corpus | Scalar us/pair | Previous schedule | Chained | Chained, prepacked |
|---|---:|---:|---:|---:|
| Mixed | 16.805 | 32.500 | 21.547 | 20.604 |
| Coincident centres | 27.456 | 60.186 | 34.846 | 33.280 |

These are medians across eight alternating-order repeats, including contact
reconstruction and integer result hashing. Packing-only costs 3.42 us/pair;
prepacking saves much less on the chained critical path, so packet preparation
alone does not explain the gap. The chained schedule is 34%/42% faster than the
previous micro schedule in these corpora, but still slower than scalar.

Confirmation counters reveal duplicated work: mixed inputs confirm 12.95% of
jobs for positive CCD and 32.14% for ambiguous/tied axes; coincident centres
confirm **100%** for ambiguity. The latter intentionally stresses an adverse
case and must not be generalized to all pile contacts. Further work should avoid
micro dispatch when scalar confirmation is inevitable, track uncertainty only
for the final winning/competing axes, and measure ordinary offset overlaps.
The backend remains default-off. The [stream report](../tools/ps2rig/results/vu0-chain-2026-10-29.json)
and [shadow report](../tools/ps2rig/results/vu0-chain-shadow-2026-10-29.json)
retain exact source/ELF hashes. The [initial report](../tools/ps2rig/results/vu0-chain-initial-2026-10-29.json)
explicitly records the rejected runtime gate.

The same-ELF winning-axis follow-up compares both assembled kernels on the
captured input arrays, keeping chained scheduling identical:

| Corpus | Scalar us/pair | Previous kernel | Winning-axis kernel |
|---|---:|---:|---:|
| Mixed | 16.761 | 21.407 | 20.086 |
| Coincident centres | 27.408 | 34.317 | 33.674 |
| Offset overlaps | 27.514 | 29.918 | 28.883 |

Mixed discrete confirmations fall from 32.14% to 22.32%; the offset corpus
falls from 0.45% to zero. The near-minimum safeguard remains enabled. The new
kernel is 6.2%/1.9%/3.5% faster in these corpora but still loses to scalar;
this does not establish a whole-scene FPS improvement. See the
[backend report](../tools/ps2rig/results/vu0-minimum-2026-10-29.json),
[full-scene shadow](../tools/ps2rig/results/vu0-minimum-shadow-2026-10-29.json)
and [replay inputs and logs](../tools/ps2rig/results/vu0-minimum-2026-10-29.inputs.zip).

The next experimental kernel recognizes **exactly equal** linear displacements.
At the existing fixed orientation their relative motion is zero, so linear CCD
cannot add a contact beyond discrete SAT. All relevant SAT face/edge tests and
minimum-depth selection remain enabled; only interval arithmetic is bypassed.
A separating axis still proves no hit. No approximate velocity threshold or new
collision simplification is introduced. Four controlled overlap/separation/
touching/common-translation cases compare the shortcut against full interval
arithmetic, and the host oracle requires fewer executed instructions. The prior
kernel fails that work-reduction check. The first physical run passed its
measured window but later lost a 1.90735e-6-depth reference hit; the complete
run is rejected. The qualification analyzer now rejects any hard runtime
diagnostic, including after the measured window.

The reference-image fixture also checks the live VU0 instruction memory against
both selected images outside timing, and reports the fraction of input pairs
with exactly equal motion. Captured Case data is replayed without regeneration.

A targeted near-touching corpus reproduced an EE/VU0 disagreement at case3455.
The actual hardware result and geometry were captured after the micro fence;
the host source simulator reports a hit, so it cannot serve as the sole oracle
for this arithmetic boundary. Applying the preceding result-consumption logic
to the **recorded hardware result** loses the hit; the corrected logic recovers it.
See the [capture manifest](../tools/ps2rig/results/vu0-precision-miss-2026-10-30.json)
and [binary packet](../tools/ps2rig/results/vu0-precision-miss-2026-10-30.bin).

A VU negative now requires the existing conservative EE full-sweep separating-axis
proof on the returned axis. If it cannot establish separation, the canonical
scalar routine decides. This reuses the geometry-cache rejection rule and does
not widen the collision tolerances or discard shallow hits. Profiling separates
proved negatives from full negative confirmations. Deferred diagnostic capture
copies into fixed RAM while micro runs and writes only after all packets drain.
The 50,000-case precision corpus and saved-hardware replay run before timing.
Physical requalification passed:50,000 precision cases, both saved-image replays,
1200 measured scene frames and103,623 micro jobs with zero hard geometry or
accounting failures, including the post-window log. The fixed-input offset-overlap
stream measured24.086us/pair against27.568us scalar (12.6% lower time); mixed
20.344us versus16.804us and coincident34.118us versus27.459us still favor EE.
These are backend timings, not whole-frame FPS. The scene classified49,481
conservative negative proofs and29,156 full negative confirmations; this remaining
duplication motivates further dispatch/work-selection measurements. Replay the
[qualified input package](../tools/ps2rig/results/vu0-negative-proof-2026-10-30.inputs.zip)
with the stream and full-scene analyzers. The
[initial common-motion report](../tools/ps2rig/results/vu0-common-initial-2026-10-30.json)
is explicitly unqualified despite its promising isolated throughput.

The body-grid screening experiment compares minimum cells of8/4/2/1m on
actual captured body/query bounds. The engine parameter defaults to8m;
production callers are unchanged. Four physical snapshots preserved eligibility
and sorted order against an independent full walk, with1136 query/cell
combinations replayed on the host. A2m cell reduced fresh-build plus query-pass
time from3.575 to1.872ms in the initial138-body snapshot and1.512 to0.929ms
in a later55-query snapshot. This excludes SAT and solver work, includes fresh
allocation, and perturbs the live scene; it only selects a candidate for a
separate complete frame benchmark. See the
[screening report](../tools/ps2rig/results/body-grid-screen-2026-10-30.json)
and its captured input package.

The complete normal-clock body-grid follow-up passed16,800 frame/accounting
rows with micro dispatch off in both arms. It does **not** qualify a2m default:
128-pile drive FPS changed20.07/22.57 to18.98/21.16 across the two reversing
repeats;64-pile33.15/37.08 to39.45/37.01;32-pile50.65/50.30 to50.18/50.30.
Different real frame intervals produce different contact-density trajectories
(the first128-pile comparison averaged roughly451 versus549 contacts), so
fixed-input query savings cannot be substituted for live FPS. Preserve this
[normal-clock result](../tools/ps2rig/results/body-grid-normal-2026-10-30.json);
a separately labeled nominal-step private benchmark is being measured to
isolate algorithm cost without changing the production clock or collisions.
The next world-index prototype remains private: it excludes physics bodies
from world queries because those candidates are immediately rejected there,
while retaining scripted moving nonphysics solids. Its host oracle passes
20,000 category/movement/full-walk comparisons and generated PS2 sources pass
syntax checks; physical admission and frame qualification are pending.

The first nominal-step run retained all16,800 frame/accounting rows, but its
overall controlled-workload gate is rejected: the initial zero-added baseline
had300 extra warmup frames, leaving75 paired rows with different settling
work. All13 other workload pairs match logged contact/awake/candidate counters
and trajectory fields at their printed resolution. The rejected
[initial fixed-step report](../tools/ps2rig/results/body-grid-fixed-initial-2026-10-30.json)
and raw package remain available. Even at equal heavy workload, query savings
did not yet become a total-physics win: first128-pile query5.067 to4.665ms,
physics21.711 to21.853ms. Separately compiled baseline/candidate functions can
also change instruction/data placement, so a focused control now uses one
physics/query implementation, varying only the grid cell at phase boundaries.
It measures64/128 piles in both repeat orders and balances initial warmup.
Production grid and clock defaults remain unchanged.

The [shared-function fixed-step control](../tools/ps2rig/results/body-grid-shared-2026-10-30.json)
passes4800 measured frames: four workload pairs and2400 paired rows match all
logged workload/trajectory fields, with complete accounting and no micro
dispatch.128-pile total physics changes21.866/21.811 to21.540/21.552ms
across reversing repeats (roughly1.2-1.5% lower);64-pile10.988/10.960 to
10.917/10.849ms. SAT timing is now essentially equal between arms. This is
controlled-workload throughput, not a production60FPS qualification. The
production8m grid remains unchanged. The world-only admission experiment uses
the shared implementation and8m body grids in both arms to isolate its effect;
physical full-walk admission verification passed before frame timing.

The world-only admission gate passed1200 frame/accounting rows and48,606
actual world queries checked against independent filtered full walks, with zero
missing candidates, ordering errors or logged-workload parity failures. Both
arms use the shared scalar implementation and8m body grids. The same ELF is
now measured with the admission oracle disabled; no world-index timing claim
is made from the verification run. See the
[physical admission report](../tools/ps2rig/results/world-grid-verify-2026-10-30.json).

The [world-index fixed-step timing](../tools/ps2rig/results/world-grid-fixed-2026-10-30.json)
passes4800 measured rows,2400 paired telemetry comparisons and full contact/
candidate accounting with oracle work and micro dispatch disabled.64-pile
physics11.146/11.075 becomes10.149/10.082ms (about9% less);128-pile
22.147/22.167 becomes19.325/19.322ms (about13% less). The unchanged8m body
index and shared scalar solver isolate world admission. Fixed-step frame
throughput improves more modestly:128-pile22.50/22.52 to23.43/23.74FPS.
This does not qualify production60FPS. A new private build restores the
original variable clock and all28 workload phases; its admission gate is
repeated before measuring the same new ELF without verification overhead.

Normal-clock admission requalification also passed:1200 frame/accounting rows
and45,328 actual world queries checked against independent full walks, with
zero missing candidates/order errors. Per-frame trajectory parity is not a
requirement here because simulation uses the original variable clock. The
[normal-clock admission package](../tools/ps2rig/results/world-grid-normal-verify-2026-10-30.json)
records the new ELF; its complete28-phase same-binary timing run follows with
verification disabled. Production codegen still awaits those measurements.

The full production-clock eager world-index experiment measured16,800 frames:
the128-box pile repeats changed18.32/18.49 to22.52/22.06FPS, while lightweight
scenes gained roughly0.1ms physics overhead. These use live variable-step
trajectories, so contact density can differ between arms. A minimum16-awake
prototype reduced sparse overhead but had mixed128-pile results; both raw
comparisons are retained in tools/ps2rig/results/world-grid-{normal,adaptive}-2026-10-30.*.
Neither establishes a universal speedup or60FPS for dense piles.

The selected implementation caches the static world index until geometry
or membership changes and handles current moving solids separately, with no
awake-body threshold. The physical admission gate checked48,502 world queries
against independent full walks with zero errors; only five index builds were
needed. The subsequent16,800-frame normal-clock comparison passes all contact
and candidate accounting checks, with no micro dispatch or admission oracle
in measured frames. TYRA_PHYSICS_WORLD_INDEX now defaults1; set0 to compare
the original combined query. All exact contact tests, contact ordering and6+3
solver passes are preserved.

| Piled props | Baseline FPS, repeat1/2 | Cached world FPS, repeat1/2 | World stage ms, baseline -> cached |
|---|---|---|---|
|32|51.38 /49.61|51.13 /50.65|0.82 /0.90 ->0.67 /0.68|
|64|36.76 /37.14|36.95 /37.27|2.26 /2.14 ->1.15 /1.14|
|128|19.05 /20.04|20.19 /22.67|6.69 /6.23 ->2.73 /2.29|

Scattered32/64 workloads remain about59.9FPS and scattered128 about59.1-59.3.
The added sparse physics overhead seen in the eager prototype is gone. The
32-pile first repeat is slightly slower in FPS despite lower world-query time;
keep both repeats. This is a useful reduction of EE query work, not dense-pile
60FPS or a hardware-limit proof. Variable-step contact densities differ between
arms, so whole-physics savings include trajectory feedback. The complete
[normal-clock report](../tools/ps2rig/results/world-cache-normal-2026-10-30.json)
and its inputs ZIP/log retain all28 phases.


The selected default also passed a separate physical non-box smoke:37 rigid
bodies,79 finite-state samples through frame7900, zero reported invalid values
or load/exception diagnostics. The fixture was freshly regenerated and its
native physics object references broadPhaseWorldNear. Pad I/O was privately
stubbed to avoid the unrelated freepad DMA wedge; this is not a production-pad
check or an exact full-state oracle. See
[smoke metadata](../tools/ps2rig/results/world-default-smoke-2026-10-30.json).

A separate calibrated EE hardware-counter profile sampled1200 dense frames.
On the cached-world arm the drive solver averaged about23,732 D-cache misses
per frame and486 I-cache misses; body contact generation averaged32,827 and
10,813 respectively. These event counts motivate investigating data locality
and instruction footprint. They do not measure hardware-unit utilization or
prove a console limit. Three event pairs rotate across different live frames;
outer scopes overlap, and the instrumented profile is not a production FPS
comparison. The counter calibration measured identical CP0/PCR cycle counts,
zero warm4KiB scan misses and8386 misses for four128KiB scans. See the complete
[PMU report](../tools/ps2rig/results/physics-pmu-2026-10-30.json).


The first follow-up screens compact solver storage without changing the live
solver. It replays four prepared physical inputs with880-byte full bodies or
96-byte velocity/mass/inertia bodies, including gather/scatter. Savings are
only about0-4% in these isolated samples. Both replay layouts agree bitwise,
including10,000 host contact graphs and four saved physical snapshots, but the
separate function differs from the caller-inline solver by up to2.98e-8.
The [expanded layout report](../tools/ps2rig/results/solver-layout-delta-2026-10-30.json)
is therefore explicitly unqualified. This is not a collision simplification,
a production solver change, or evidence of a whole-frame speedup. A separate
private scratchpad replay follows before deciding whether locality merits a
larger integration change.


The [private scratchpad screen](../tools/ps2rig/results/solver-scratchpad-2026-10-30.json)
uses the same compact scalar function on heap and SPR. All16 SPR repeats restore
all16KiB without errors; first-repeat outputs match heap bitwise on all four
snapshots. The two denser snapshots
save0.27/0.47ms (4.36/6.68%) versus full-stride replay; the lighter snapshots
lose0.25/0.31ms to maintenance. Mode2 includes gather/scatter and scratchpad
save/restore/verification, with stack/DMA eligibility outside the bracket.
This is a small, conditional kernel opportunity, not a production FPS result;
caller-inline differences reach5.96e-8 and numerical parity remains unqualified.
The shipped solver is
unchanged. A common-kernel integration and whole-frame comparison would be
needed before enabling any heap/SPR solver path.

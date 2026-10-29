# Temporal physics experiment

This experiment measures geometry coherence, persistent contact impulses and
independent dynamic islands without reducing collision coverage or solver passes.

The previous scalar integration is the reference. A same-ELF four-arm rig
compares reference, geometry/axis caches alone, caches plus warm starting, and
caches plus warm starting and islands, with reversed arm order in a second repeat. Stage timings include
prediction, world contacts, body-grid construction, body contacts, solver and
write-back; nested query/SAT scopes must not be added twice to their parents.
Host gates cover cached-axis CCD conservatism, contact identity/invalidation,
unbounded history storage and stable island partitioning. Full-game and
physical-console qualification are recorded after the experiment.

## Implementation and gates

`TYRA_PHYSICS_COHERENCE` controls persistent world-box transforms and per-step
hull normal/edge transforms, plus a previous-frame separating-axis hint.
`TYRA_PHYSICS_WARM_START` controls impulse history, and
`TYRA_PHYSICS_ISLANDS` controls stable component grouping and coordinated
sleep/wake. The current provisional defaults are 1/0/0 pending console timing.
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
diagnostic occurred. Physical-console qualification follows separately.

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
bounds-rejection counts alongside the stage timings. Hardware results follow.

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

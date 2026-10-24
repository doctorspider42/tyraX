# Vehicle Playground full-throttle trial

Private follow-up to the [vehicle-free rotation census](tyrax2-motion-census.md). This test restores the authored vehicles and drives the Ravager straight up Garage Boulevard from its authored spawn at approximately (0, -74.3035) to z >= 108, just before the northern end at z=112. Main scene contains Ravager, Pica Turbo and Strix V12. The authored OnStart seats the player in the Ravager. Vehicle effects, physics, real frame dt and the ordinary chase camera remain active. Noon is paused, matching the preceding diagnostic; this is not the night workload.

Prepare with `tools/tyrax2-motion-probe.py <new-folder> --drive`. Engine instrumentation and the private baked lifetime selector are shared with the preceding test; the drive-specific header is `tools/tyrax2-drive-probe.hpp`. The original lifetime trial changes only private copies. The later combined qualification below promotes two engine defaults, without changing the authored map. `tyrax2-drive-analyze.py <bin>` validates the output.

## Protocol

### Combined geometry reuse qualification (2026-10-08)

`--geometry-reuse` enables partial baked-body replay and immutable strip
expansion together, retaining normal captures, TTL250 and original geometry.
It uses six full routes: Off/On/Off with counters, then On/Off/On without
counters. The matching analyzer requires activation of both paths, rejects
counter leakage, and checks restored parked controls. `--verify-partial
--verify-expansion` additionally compares both paths against the actual
original writers in a separate correctness build, never a timing build.
The combined result is not the sum of the historical individual savings.
The physical comparison completed all six routes. Since 1.173.1 both geometry
reuse macros default on; verification macros and env barrier coalescing remain
off. The final default-route camera/lifecycle build passed the additional gates.

| Phase | Both caches | Counters | Drive work, ms | Mean loop Hz | Work P95, ms |
| --- | --- | --- | ---: | ---: | ---: |
| 0 | Off | On | 14.590 | 49.86 | 19.92 |
| 1 | On | On | 14.039 | 51.31 | 18.68 |
| 2 | Off | On | 14.672 | 48.40 | 20.22 |
| 3 | On | Off | 13.988 | 52.43 | 18.84 |
| 4 | Off | Off | 14.524 | 49.18 | 19.43 |
| 5 | On | Off | 14.010 | 52.32 | 18.64 |

The disabled-counter contrast saves 0.536/0.514 ms, not the sum of the two
historical candidates. Observed snapshot traffic falls from about198 KB to97 KB
per moving loop; ordinary expansion falls from804-810 to54 vertices while the
cached path supplies about801 expanded vertices. Partial replay averages20.31
packages per moving loop. Peak residency is2,806,688 bytes/171 entries, with
zero eviction or reserve failures in the observed candidate. Restored parked
controls match. The central z[-30,-10) bin improves from33.78 Hz to38.14/37.05 Hz
in the disabled-counter triplet, still below60. P95 remains above the field
budget and44-47 ms candidate outliers remain. Loop Hz is not a full scanout
census; elapsed-minus-pacing is not pure EE instruction time.

The same performance ELF completed six emulator routes before PS2 deployment:
`5a320cea001195f26b81c1818201a18a619c847201633b6d6fbb72083bf98cdc`.
The separate combined byte oracle completed six routes with at least319488
expansion and131072 partial-body comparisons, zero failures. Its ELF is
`b9cee060483f3fec92182c6aec1ab38d4ab99a585e36141505779a81a462b4f6`.
Both native bindings match466 source files. One startup SIF guarded-completion
warning occurred on PS2; all routes completed valid with no trap/assert.
Counters-off retains shared diagnostic footprint and disabled hook branches;
an uninstrumented production binary's absolute saving remains unpriced.
Evidence is under geometry-reuse.

`--geometry-sweep` is a separate correctness-only, vehicle-free camera sweep
with both candidates always enabled. It uses the ordinary six-phase motion
protocol and `tyrax2-motion-run.py analyze`; it is not an On/Off timing contrast.
Both target byte oracles may be enabled. The host lifecycle control
`tools/verify-stapip-cache-lifecycle.py` extracts the actual cache declarations
and methods, widens only pointer hashing for the host ABI, and uses mocked DMA
readers. Its 59 ASan/UBSan checks cover key fields, the disjoint expansion key,
in-flight retirement, reload/clear, expiry, budget refusal/eviction and churn
fallback. It does not replace target packet or raster tests.
The sweep also visits dense/procedural/main after frame7200 and requires its
final lifecycle witness at9000. `--production-defaults` leaves both actual
compile-time route predicates untouched; historical private comparisons instead
pin their copied defaults off before inserting runtime gates.

The default-route ELF
`c5c7b1395cb590721ffdc450b9b69dd9beee0d9cbf77ca88b08e47c03615fbb6`
matches 466 native source inputs and completes all 7200 camera loops plus scene
witnesses at 7500/8100/8700 and GEOMETRYSCENEDONE at 9000. Through the stopped
run, 1,507,328 expansion and 139,264 partial comparisons report zero failures.
The camera parser accepts all six phases; restored counters match. Screenshots
show normal road/scene geometry without obvious stretched triangles, not exact
framebuffer equivalence. This is emulator lifecycle coverage, not a physical
all-scenes or full-night qualification. The default-route binary is an oracle
build, not a priced uninstrumented production build. Two intermediate sweep
fixtures were prepared but superseded before execution; only this default-route
run qualifies the final rotation/scene gate.

### Next structural boundary: prepared static clusters

The `--prepared-static` experiment now implements the first bounded stage:
prepare ordinary static-batch packet bodies and strip expansions before the
measured drive, pin their immutable entries for the scene, and release pins
before the next reload. `TYRA_STAPIP_PREPARED_STATIC` defaults off and has no
production caller. Both previously promoted geometry caches stay on in every
arm. Preparation runs at offset1000, outside sampled loops, with its own cold
duration/attempt/readiness record; the route still starts after1200 warmup loops.
The existing4 MiB cap, version invalidation and reader retirement remain active.
This is not an offline format. The completed comparison below rejects its
promotion. Pins refuse eviction but do not suppress content invalidation.
The host lifecycle harness now includes pin expiry/budget/release controls.

#### Prepared/pinned batch result (2026-10-08)

All six routes complete on PS2 with matching restored parked controls. Both
promoted caches stay on; only preparation/pinning alternates. Counters are
enabled for phases0-2 and disabled for phases3-5.

| Phase | Prepared | Drive work, ms | Mean loop Hz | Work P95, ms |
| --- | --- | ---: | ---: | ---: |
| 0 | Off | 13.776 | 53.22 | 17.98 |
| 1 | On | 14.039 | 51.65 | 18.74 |
| 2 | Off | 13.916 | 51.98 | 18.61 |
| 3 | On | 13.977 | 52.55 | 18.57 |
| 4 | Off | 13.922 | 52.55 | 18.52 |
| 5 | On | 13.957 | 52.21 | 18.96 |

The counters-off candidates cost another0.055/0.035 ms rather than saving work;
this small contrast is not a statistically established regression, but provides
no reason to promote. Preparation reports28/28 ordinary bodies ready in each
candidate and costs31.32/28.63/28.59 ms outside the sampled window. Observed
peak residency rises from2,762,320-2,804,672 bytes/169-171 entries to4,106,016
bytes/296 entries, still within4 MiB, with zero eviction/reserve failures.
Snapshot copies remain about97 KB/loop. Bake misses total316/316/322 in
phases0/1/2: this policy does not remove the measured runtime construction.
Counters-off outliers remain43.8-44.1 ms. Work remains elapsed-minus-pacing,
not pure EE time, and loop Hz is not a scanout census.

**Decision: keep the experiment default off, with no production caller.**
Do not add its result to the earlier combined savings or compare absolute
times across separate ELFs. This trial covers warmup preparation of current
batch bodies, not a new static-cluster format. Readiness counts ordinary bodies,
not successful preparation of every optional expansion or a census of later
pinned-entry hits. Byte comparisons establish consumed payload correctness,
not that every prepared body was subsequently reused. Therefore this rejects
the tested full-scene pinning policy, not load-time ownership in general.

The exact performance ELF completed six emulator routes before PS2 deployment:
`0d7c52b71708e2aa6845e0be7b0603d8246eae2bcc48f6127e5ba4973fe08170`.
The separate oracle ELF is
`44922999f30c06f6e34bffac82a226e3a473a0647e2c0e6c969353b458957fa0`.
Both native bindings match466 source inputs. Through the stopped oracle run,
1,032,192 expansion and282,624 partial comparisons report zero failures;
all six routes complete. In-route emulator screenshots are smoke checks only.
The host cache harness passes62 ASan/UBSan checks with mocked DMA readers;
the analyzer accepts its reference and rejects16 malformed inputs, including
missing/empty/reordered preparation and preparation leaking into a control.
The ordinary host suite passes too. No physical trap/assert was logged.
Evidence and frozen candidate sources are in
prepared-static.

The next useful structural change must reduce live classification/submission
work for static clusters, rather than merely retain more existing cache entries.
Keep fresh per-view state and microprogram entry rules; do not increase clip
package capacity without a new worst-case VU-memory proof. Stable60 remains open.

The combined candidate moves immutable geometry preparation out of repeated
visible-frame work, but still builds its arenas on demand. In particular,
`preparePartialBakedBag` constructs all fixed slices on a cold partial-bag miss;
`renderStrippedPkgs` still submits small clip chunks, and the non-strip partial
route still classifies and may subdivide packages. Neither cache removes VU
clipping or reduces its input geometry.

The next larger implementation should start with generated static batches,
not a universal replacement of dynamic mesh submission:

1. Give each fixed render cluster a stable geometry/version owner and bounds.
2. Prepare its ordinary VIF body and optional expanded clip representation at
   load/build time, with a bounded resident working set and existing fallback.
3. Keep per-view classification, clip mask, uniforms, fresh entry header and
   MSCAL/MSCNT selection live. Never replay an arbitrary old whole block after
   switching programs or secondary views.
4. Register owned immutable storage before publishing it and preserve bank
   readers through rebuild, scene change, eviction and texture mutation.
5. Compare this one producer against the qualified combined renderer, including
   cold-start memory and load cost. Do not infer a gain from fewer EE statements.

This is the next implementation boundary, not a completed asset-format change.
The current trial deliberately leaves the asset format and VU memory layout
unchanged. Wider clip chunks need a separate worst-case output-capacity proof;
raising the existing divisor-based limit is not a safe shortcut.

Six passes use refresh lifetimes 250/1000/250, repeated with operation counters disabled. Each pass starts with a normal scene reload (except the boot pass). Samples begin at loop offset 1200, exceeding the longer lifetime so unused entries from the previous scene can expire. Offsets 1200..1319 measure the parked car; at 1320 the harness supplies full throttle, no steering, no brake and no nitrous, until the car crosses z=108. Normal pedal-derived physics, substeps, suspension, wheel updates, paint, reflections, smoke, audio, camera and rendering continue. Pad state is neutralized to prevent external input from perturbing the path. Scripted pedal values are applied at the ordinary vehicle-input boundary, not by moving the car or overriding dt. Handbrake holds the car before launch and brakes it after the measured endpoint; post-finish coasting is not sampled.

The 1200-row cap fails closed if the car does not reach the endpoint. CSV output occurs in the following unmeasured loop, then the usual script scene-switch request reloads scene 0. Timed windows contain no diagnostic file I/O. The sampler stores frame time, pacing, update/render elapsed splits, existing presentation callbacks, position, speed, yaw, damage, real dt, camera pose, vehicle/driver counts and operation counters. Elapsed non-pacing time includes existing rendering waits; it is not pure EE instruction time. Scene loading/restart and startup are excluded from the samples.

## Comparison boundaries

Real dt is preserved: different frame rates can produce different substep histories, positions and effect states at the same frame index. The parser therefore validates lane bounds, forward progress, parked start and the final endpoint, then summarizes the route in 20-unit spatial bins as well as whole-pass timings. Do not assert bitwise-identical trajectories or subtract mismatched frame indices. The same-ELF counter-disabled triplet prices net diagnostic perturbation, not its common header/storage/hook overhead. Longer TTL may retain more memory; the 4 MB budget and DMA ownership rules are unchanged.

The immediate question is whether ordinary driving reproduces sustained missed refreshes, and whether the TTL change affects that symptom. It is not a blanket optimization benchmark or a claim that the rotation fix solves driving.

## Pilot: reproduction, not a qualified TTL contrast

The initial `F:/Projects/tyrax2-full-throttle-20261007-v1` fixture used only 180 warmup loops. Its native build, source binding, emulator and physical six-pass runs completed, and all route validation checks passed. The physical run reproduced sustained slowdown: counter-disabled full-route mean periods correspond to approximately 48.6/49.4/46.8 loops/s for 250/1000/250, with the z=-30..-10 segment around 31-34 loops/s. The final northern segments reach approximately 60. These are loop-cadence figures, not host emulator FPS.

**Reject its TTL benefit estimate:** scene reload does not fully erase previous cache occupancy. The return-to-250 parked control has 8 new misses and 8 budget evictions per frame, 3 failed reserves, approximately 4.18 MB occupied and 329,136 mutable snapshot bytes, versus zero misses/evictions and 126,528 bytes in the first parked control. That is a different starting cache state. It also happens in the counter-disabled return control (30 Hz parked versus approximately 60 Hz before). The valid reproduction remains useful, but these A/B differences must not be sold as an optimization result. The v2 fixture extends warmup beyond the 1000-frame lifetime without forcing a cache clear or changing engine ownership.

Pilot evidence is preserved in pilot/physical with CSV, logs, launch inputs and native binding; emulator images validate the driven car and road. ELF SHA-256: `355c2f17487d8f60c21341ddecebf5c3cb62f4605d097a149b4229af41ec0f64`. An initial misleading-indentation diagnostic warning was fixed and rebuilt before qualification. The post-build binding, not the earlier preparation manifest, identifies shipped sources.

## Qualified warmup result: physical PS2

The v2 trial completed all six passes (`DRIVEDONE valid=1`). All 3315 rows pass route/accounting validation. Each pass takes approximately 8.92 simulated seconds from full throttle to the endpoint, with x=0/yaw=0 and no damage in the samples. The before/after counted parked controls match all checked route, replay, copy, miss and eviction counters. All six parked controls are approximately 59.94 Hz. Longer warmup removes the pilot's return-control cache-pressure artifact.

| Counters | TTL | Driving loops/s | Mean non-pacing ms | P95 ms | Long periods / driving rows |
| --- | ---: | ---: | ---: | ---: | ---: |
| On | 250 | 48.62 | 14.703 | 20.168 | 99 / 434 |
| On | 1000 | 47.39 | 14.785 | 20.427 | 111 / 423 |
| On | 250 | 46.38 | 14.836 | 20.271 | 119 / 414 |
| Off | 250 | 49.18 | 14.720 | 20.061 | 94 / 439 |
| Off | 1000 | 49.74 | 14.652 | 20.041 | 89 / 444 |
| Off | 250 | 49.41 | 14.683 | 19.784 | 93 / 441 |

"Loops/s" is 1000 divided by mean loop-entry period; "long" is an entry period above 25 ms. These are physical clock observations in this diagnostic ELF, not emulator speed. Even the counter-disabled arms retain common sampling/storage/hooks, including a 244,800-byte row buffer; absolute observer footprint is unpriced. The counted arms show additional variability, so no fixed observer subtraction is justified.

The cache lifetime change **does not solve sustained driving slowdown**. Counter-disabled mean non-pacing contrast is only 0.068 ms versus the preceding control and 0.031 ms versus the following one; this single series is not evidence for a general driving optimization. In the z=-30..-10 segment, rates are **31.08 / 33.30 / 33.30 loops/s** for 250/1000/250. The next segment is approximately 37-42 loops/s. The northern z=70..108 tail approaches 60 in all three arms: the issue is sustained in the central road section, not every point on the map.

## What changes during driving

The pre-main-render elapsed slice averages approximately **1.30 ms** in the disabled-counter runs. This split does not isolate every producer or every EE instruction, and the remaining non-pacing interval still includes rendering waits.

The counted runs show these concrete differences from the stable parked pose:

- Shared reflection captures are zero parked and occur in approximately **19%** of driving samples.
- Capture-bearing loops average **18.4-18.5 ms** non-pacing, versus **13.8-14.0 ms** without captures. These groups also differ in position, visible geometry and cache/effect history; their difference is an association, **not the isolated cost of a capture**. Entry periods belong to the preceding loop, so group period means must not be used as same-loop capture attribution.
- Baked churn skips rise from zero to approximately **2.8 per driving loop**, indicating that changing payloads route around baked reuse.
- Mutable snapshot traffic rises from **126,528 bytes parked** to approximately **197-198 KB per driving loop** on average.
- The measured paint-update branch and `rebuildObjectGeometry` counter remain zero; this does not exclude wheel/smoke or other stream updates outside that rebuild function.
- There are isolated non-pacing outliers up to approximately **45-58 ms** even with counters disabled. Neither the lifetime change nor this aggregate census attributes those outliers completely.

The next useful causal trial is to freeze only the shared reflection capture while keeping the same driven path, main-view rendering, vehicle simulation and camera. That is a diagnostic image-quality change, not a production optimization. A second distinct lead is the changing-stream fallback/preparation path; do not label either whole span pure EE work without separating its waits. No production renderer, cache policy or authored project was changed here.

## Evidence and verification

### Shared-capture freeze follow-up

`tyrax2-motion-probe.py <new-folder> --freeze-capture` keeps the same six full-throttle runs and 1200-loop warmup, but fixes baked lifetime at 250 in every phase. Phases 1 and 4 freeze only the shared environment capture after throttle starts, and only while a valid retained capture already exists. Warmup and parked samples capture normally. The reuse/content scan remains active. Main-view vehicle rendering and reflection sampling remain active, with the old texture and its corresponding basis retained; image correctness is deliberately sacrificed during the frozen drive. This is a causal diagnostic, not a shippable reflection optimization.

`captureFrozen` records the actual valid-basis gate on every sampled loop, even when counters are disabled. `captureBlocked` counts ordinary eligible captures rejected by that gate in counted phases. Analyze with `tyrax2-drive-analyze.py <bin> --freeze-capture`; the parser requires normal capture activity in both controls, zero captures and positive rejected eligibility in the counted frozen run, exact phase/gas/freeze flags, and the original path/parked-state checks. No added completion fence or per-capture clock is introduced. Timing contrast includes downstream cache/scheduling effects and is not pure EE preparation time.

The physical run completed all six passes (`DRIVEDONE valid=1`); all 3494 rows validate and parked controls match (`startingStateQualified=true`, no mismatches). Every pass took approximately 8.92 simulated seconds. The counted controls performed 80 and 79 captures; the frozen arm performed zero and rejected 233 eligible attempts. Rejected eligibility is not a count of captures that a normal run would perform: retaining stale state can leave more attempts eligible.

| Counters | Capture | Driving loops/s | Mean non-pacing ms | P95 ms | Long periods / driving rows |
| --- | --- | ---: | ---: | ---: | ---: |
| On | Normal | 50.42 | 14.472 | 19.417 | 83 / 450 |
| On | Frozen | 54.67 | 13.880 | 17.138 | 46 / 488 |
| On | Normal | 49.30 | 14.513 | 19.603 | 94 / 440 |
| Off | Normal | 50.53 | 14.481 | 19.565 | 83 / 451 |
| Off | Frozen | 55.12 | 13.842 | 17.063 | 42 / 492 |
| Off | Normal | 50.75 | 14.502 | 19.764 | 80 / 453 |

Freezing capture improves cadence in both triplets, but **does not restore sustained 60 Hz**. In the counter-disabled z=-30..-10 bin, cadence is 39.23 / 44.40 / 41.07 loops/s; in z=-10..10 it is 45.55 / 53.95 / 42.81. The disabled-counter mean non-pacing reduction is 0.638-0.660 ms, with long periods roughly halved; a 45.46 ms non-pacing outlier remains in the frozen arm. This is evidence that shared capture contributes to the driving slowdown, not that it explains all of it or that its cost is pure EE work. Partial-visibility packet preparation and changing-stream fallback remain unisolated leads. Earlier runs have different control cadences: use within-ELF bracketing controls, not cross-build subtraction.

Private fixture: `F:/Projects/tyrax2-drive-reflection-20261007-v1`. ELF SHA-256: `db59908b822ed2f397b364b814e0d5b8f7acd14fb3645b532e2ac5a1f9ea20a8`. Native binding verifies 465 source mirrors and the shipped ELF; the same ELF completes emulator and console runs. The sampler now retains a 254,400-byte row buffer, including common unpriced overhead in all phases. The freeze parser accepts the reference and rejects 13 malformed controls; the original parser still rejects its ten controls. Emulator imagery confirms the vehicle/road/chase view, not reflection correctness; emulator time is not a hardware measurement. The owned emulator/Xvfb were stopped, while the physical host server remains alive.

Physical analysis, CSV and console log are archived with launch identity, asset hashes, native binding, parser controls and emulator evidence. No production renderer change was made.

### Partial-packet preparation and existing waits

Prepare with `tyrax2-motion-probe.py <new-folder> --packet-scopes` and analyze
with `tyrax2-drive-analyze.py <bin> --packet-scopes`. All six runs retain normal
reflections and TTL 250. Timed scopes are Off/On/Off, then On/Off/On; ordinary
operation counters are enabled only in the first triplet. The 1200-loop warmup,
parked control and full-throttle route are unchanged. No work is bypassed and no
wait, drain or fence is added.

Two nested elapsed scopes measure `StaPipCore::render` and its partial route.
The partial scope begins after route selection, before retained lookup, and
ends after package creation/classification, strip expansion or subpackage
merging, submission/flush and cache closure. It excludes the earlier bounds,
MVP, texture, light and object-data preparation. It includes all producers and
views reaching that route, not only the main camera. The parent includes early
culled calls. Partial time is a subset of Core time, never an additive term.

Wait scopes wrap the original DMA/GS/VIF polling leaves and presentation waits
reachable from a Core call, not whole `waitFor`/`drain` functions (which also
prepare/submit work). The private `wait-sites.json` pins every replacement.
Nested waits are unioned to prevent double subtraction. Queue polls still
include their existing `advanceIfIdle` servicing work. Explicit wait time is
reported inside Core and inside Partial separately; subtraction is **elapsed
time outside these measured wait leaves**, not pure EE instructions or a
complete accounting of cache stalls, interrupts, SDK internals and scheduler
preemption. Waits outside Core are intentionally not measured by this trial.

The sampler has a 292,800-byte row buffer. Clock-disabled controls retain its
storage and inactive hook branches, so their absolute overhead is unpriced.
The parser checks containment, balanced scope depth, zero clocks in disabled
phases, and matching Core/Partial call counts against the independent counted
bag routes. Its synthetic reference passes and 18 malformed controls fail.
`tyrax2-motion-run.py scope-selftest <fixture>` additionally compiles the actual
scope header with a deterministic host clock: disabled gates, waits outside
Core, nested-wait union, nested scope containment, unsigned clock wrap, rejected
recursive Core calls and balanced depth all pass. This checks ledger arithmetic,
not physical clock accuracy or exhaustiveness of the selected wait sites.

#### Physical result

All six passes finish with `DRIVEDONE valid=1`; all 3346 rows pass validation,
and the before/after counted parked controls match. No capture is frozen.
Every pass covers the route in approximately 8.92 simulated seconds.

| Phase | Counters | Scopes | Driving loops/s | Non-pacing ms | Core ms | Partial ms |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 0 | On | Off | 49.30 | 14.519 | - | - |
| 1 | On | On | 48.74 | 14.598 | 9.424 | 4.092 |
| 2 | On | Off | 48.74 | 14.546 | - | - |
| 3 | Off | On | 49.07 | 14.529 | 9.359 | 4.043 |
| 4 | Off | Off | 49.18 | 14.479 | - | - |
| 5 | Off | On | 49.18 | 14.525 | 9.383 | 4.038 |

Values are per-loop means. Scope-enabled minus adjacent scope-disabled driving
means are approximately 0.046-0.079 ms. Parked contrasts are larger,
approximately 0.095-0.164 ms, so do not apply a fixed observer subtraction.
This bounds the net effect of activating the extra clocks in this series, not
the common inactive hooks or storage footprint.

Measured wait calls and ticks **inside Core are zero in every measured row**,
including the Partial subset. This is consistent with frame recording deferring
execution; it does not mean the entire renderer or frame never waits. The
selected preparation scopes therefore do not conceal an entered explicit
DMA/GS wait at these audited sites. Their elapsed cost is host-side preparation,
including memory/cache and interrupt effects, not a pure retired-instruction
count or the subsequent VU/GS execution cost.

The Partial route rises from 8 calls and 3.31-3.35 ms parked to about 12.7 calls
and 4.04-4.09 ms averaged over driving. In the central bins z=-30..30 it costs
approximately **4.73-5.33 ms** (counter-disabled measured passes). This is a
material preparation cost worth decomposing into package classification,
expansion/copy and command/snapshot construction. It is not yet proof that one
of those operations is redundant or can safely be bypassed. Total Core time
actually falls from approximately 11.3 ms parked to 9.4 ms averaged over the
route as visibility changes; this is not evidence that every rendering cost
increases under movement.

**The largest hitch is elsewhere.** In all three scope-enabled passes, the
largest non-pacing sample occurs at z approximately -53.38: 50.79 / 45.37 /
44.97 ms, while Core remains 12.01 / 11.93 / 11.91 ms and Partial remains
4.03 / 4.09 / 3.95 ms. Update is only 1.44-1.46 ms. Most of this hitch is in the
render-side interval outside Core; these scopes do not identify its producer or
wait. Do not blame the partial packet path for that distinct spike. Its location
is a sharper next target than another whole-render aggregate.

Private fixture: `F:/Projects/tyrax2-drive-scopes-20261007-v1`; ELF SHA-256
`50a1b1af6d329a2f252aea102bdd1dade0066c0ad27d3f61d3ae3d26fa6eede4`.
Native build succeeds, 465 source mirrors and the shipped ELF match; the shared
scope state has one symbol definition per variable. The same ELF completes all
six emulator passes with valid ledger/path checks. The endpoint screenshot shows
the car and chase view, not a pixel-equivalence proof. Emulator timing is not
used for physical cost. No production renderer or authored project is changed.

Physical analysis,
CSV,
console log
and wait-site inventory
are retained with native binding, build log, parser/ledger controls and emulator
evidence. The owned emulator/Xvfb were stopped; physical ps2client stays alive.

### Exclusive partial-path breakdown

`--packet-breakdown` on the fixture generator/analyzer extends the preceding
scope protocol, preserving the six runs, normal captures, TTL 250 and both
clock activation orders. It splits work executed **inside Partial** into six
exclusive buckets:

- `create`: both packager `create` overloads, excluding nested classification.
- `classify`: `checkFrustum`, including merged bounds and clip-plane masks.
  Coarse group tests remain in `create`.
- `fill`: qbuffer package-pointer fill and the four copy/strip-expand functions,
  including their pool bookkeeping.
- `packet`: `addBuffersDataToPacket`, including retained command handling.
- `submit`: `Vif1Queue::submit`, excluding nested payload copies.
- `payload`: arena `copyQwords`, for both chain and referenced payload copying.

A nested bucket charges its full elapsed interval to the parent's child ledger;
the parent reports elapsed minus children. Each bucket also records call count.
These exclusive values may be added; their sum must not exceed Partial. The
unassigned remainder is explicit, not relabeled classification: it includes
retained lookup, route loops, clip/cull setup, flush bookkeeping and observer
bookkeeping outside selected function bodies. Measured waits, if any, remain
inside these elapsed buckets and are separately reported by the parent scopes.
The 350,400-byte row buffer and inactive hooks are common unpriced overhead.

The deterministic host ledger test covers nested bucket exclusion and the
outside-Partial gate. The parser accepts its reference and rejects 22 malformed
controls, including bucket overflow, disabled-phase leakage and mismatched
call/tick presence. Earlier scope, freeze and lifetime parser controls also pass.
Native build succeeds and 465 source mirrors match; the ELF SHA-256 is
`93f5be20031d0385581b3cd34d86e753fe90150981df5f925f6080a949732c97`.
The fixture is `F:/Projects/tyrax2-drive-breakdown-20261007-v1`.
All six emulator passes complete (`DRIVEDONE valid=1`), all 3924 rows validate,
and the inspected image shows the three vehicles, roadway and chase view.
This establishes apparatus/path checks, not PS2 timing. The owned emulator and
Xvfb are stopped. Emulator evidence and analysis
are archived with source binding and parser/ledger controls.

#### Physical breakdown result

All six physical passes complete (`DRIVEDONE valid=1`), with all 3264 rows
valid and matching before/after counted parked controls. Every pass takes
approximately 8.92 simulated seconds. The same native-verified ELF is used.

| Phase | Counters | Clocks | Driving loops/s | Mean non-pacing ms |
| --- | --- | --- | ---: | ---: |
| 0 | On | Off | 47.95 | 14.640 |
| 1 | On | On | 46.72 | 14.861 |
| 2 | On | Off | 48.18 | 14.657 |
| 3 | Off | On | 46.94 | 14.805 |
| 4 | Off | Off | 48.51 | 14.611 |
| 5 | Off | On | 46.72 | 14.862 |

Enabling clocks adds approximately 0.194-0.251 ms to whole-drive means against
adjacent controls; parked contrasts are 0.141-0.191 ms. The finer observer is
not free. Do not subtract a uniform amount from individual buckets or compare
absolute FPS across the earlier ELFs. Common inactive hooks, row storage and
code layout remain unpriced.

Exclusive per-loop mean costs within Partial, in milliseconds:

| Category | Phase 1, counted | Phase 3, counters off | Phase 5, counters off |
| --- | ---: | ---: | ---: |
| Package descriptors/coarse tests | 0.233 | 0.230 | 0.231 |
| Detailed classification | 0.486 | 0.485 | 0.485 |
| Geometry fill/copy/strip expansion | 0.534 | 0.518 | 0.528 |
| Packet command construction | 0.469 | 0.461 | 0.466 |
| Queue submit excluding payload copies | 1.169 | 1.151 | 1.153 |
| Arena chain/payload copying | 0.860 | 0.859 | 0.857 |
| Unassigned remainder | 0.587 | 0.592 | 0.596 |
| **Partial total** | **4.338** | **4.297** | **4.317** |

All measured wait calls/ticks within Core remain zero. Queue submission plus
arena copies is approximately **2.01-2.03 ms**, about 47% of the selected
Partial interval. Descriptor creation plus classification is about 0.72 ms,
not the dominant measured component. In the central z=-30..30 bins of the
counter-disabled measured passes, queue submission plus copies is approximately
2.56-2.70 ms, within a Partial total of 5.31-5.78 ms. Those are instrumented
elapsed costs, not an estimate of recoverable speedup.

The source explains why this is more than a hardware-send call:
`StaPipQBufferRenderer::addBuffersDataToPacket` first builds packet commands;
`Vif1Queue::submit` calls `copyForNativeRecording`, whose arena preflight/fixup
owns mutable references; it then emits a native VIF chain from the snapshot.
The first-16-reference memo and counted snapshot already exist, so proposing
those again would repeat earlier work. The measured Submit bucket includes
validation, ownership lookup, fixup, native emission and other queue overhead;
it does not isolate which is dominant. The Payload bucket includes both chain
and referenced data, not only vertex arrays.

The next code target is this conversion/ownership path, with preservation of
DMA source lifetime and ordering. These results do **not** justify borrowing
arbitrary mutable buffers, dropping safety checks or claiming a 2 ms saving.
The separate z=-53 hitch from the preceding scope trial is not explained by
this breakdown. No production renderer or authored project has changed.

Physical analysis,
CSV
and console log
are archived with launch identity and asset hashes. Physical ps2client remains
alive after the completed test.

## Submission fixes, 2026-10-07

The tested candidate fuses the arena snapshot fixup with native VIF emission.
It preserves the original snapshot layout, mutable copies, immutable bank
leases and submission order, but removes the separate native-emission walk.
Writer capacity is checked before writes; failure rolls the writer back and
uses the existing path. Busy writers and diagnostic HOLD retain their old path.
A second independent candidate makes successful immutable borrows perform one
span lookup instead of calling `contains` and repeating the same binary search.

`tyrax2-motion-probe.py <fixture> --submit-fix` creates an 18-pass private
experiment with all counters on, detail clocks off, normal captures and TTL250.
Phases 0-5 compare fused submission alone, 6-11 single lookup alone, and 12-17
both. Each group uses Off/On/Off followed by On/Off/On. The appended
`fusedSubmit` counter verifies actual successful entry. Source selectors and
native binding establish the lookup variant. Common hooks/storage/layout are
not priced. The matching analyzer requires all 18 endpoints, stable parked
workloads, no clock/freeze leakage, and actual fused entry only in its arms.

Host verification compares exact arena and finished writer bytes for 2,000
randomized chains including CNT/END payloads, REF/REFE, immutable/mutable data,
dirty tag padding and existing arena/writer prefixes. Capacity/malformed failures
preserve cursors. The complete count harness passes 84,635 cases plus 10,000
malformed streams. This establishes byte equivalence, not hardware speedup or
complete raster coverage. Fixture: `F:/Projects/tyrax2-submit-fix-20261007-v1`.
Native source agreement covers 465 files; ELF SHA-256:
`93f2246e37568165361493ae6db7c4bebb51abe2e41a247655f0d56bae511114`.
The optimized host build also passes AddressSanitizer and UndefinedBehaviorSanitizer.

**Neither candidate is enabled in production.** Their engine and host-test
patches live in the experiment archive.
The fixture generator applies the engine patch only to its private copy, with
a fail-closed patch check. Reproduction matches all 492 manifest entries except
the expected directory-derived Docker Compose project/volume names.
`verify-tyrax2-host.py --only count --submission-candidate` applies both patches
to temporary sources and runs the actual-source tests without editing the repo.
The ordinary harness remains at 16,210 checks plus 10,000 malformed streams.

### Physical result: rejected / incomplete

PCSX2 completed all 18 passes. PS2 completed phases 0-8, 4,867 rows, before
stalling during the next scene reload. All nine completed endpoints, parked
controls, normal-capture flags and expected fused-entry counters qualify.
The strict 18-pass analyzer does not accept this as a complete trial;
`--submit-fix --completed-prefix=9` explicitly validates only these three
completed triplets and labels the remaining arms unqualified.

| Phase | Variant | Mean non-pacing ms | Driving loops/s |
| --- | --- | ---: | ---: |
| 0 | Baseline | 14.688 | 47.84 |
| 1 | Fused | 14.973 | 46.38 |
| 2 | Baseline | 14.792 | 47.28 |
| 3 | Fused | 14.925 | 46.83 |
| 4 | Baseline | 14.794 | 47.62 |
| 5 | Fused | 14.956 | 46.05 |
| 6 | Baseline | 14.765 | 47.84 |
| 7 | Single lookup | 14.803 | 47.06 |
| 8 | Baseline | 14.830 | 47.39 |

Fused submission loses by 0.131-0.285 ms against adjacent controls in both
orders. Single lookup is mixed (+0.038/-0.027 ms) with no reverse triplet.
The combination has no physical result. Common apparatus and the fused-success
counter's extra candidate-side increments are unpriced; no pure per-traversal
cost is inferred. These results do not justify promoting either candidate.
All default renderer changes from this attempt were removed.

At the failed phase9 reload, the IOP repeatedly prints
`freepad: DMA Busy ID = 9caf0002 ret = 1`, `SB_STAT = 00f00160` after asset loads.
No EE exception identifies the cause. One bounded network reset plus a bare
execee into a separate recovery directory produced no `loadelf` or new game
start, only the continuing freepad messages. Both attempts and their raw logs
are preserved. This does not establish whether the renderer, repeated reloads,
host I/O or another subsystem caused the stall. Recovery requires a manual
reset; the owned host clients and invisible emulators were stopped.

The next implementation target is **reusable partial-route geometry**, not
another timing layer. Only baked arenas currently register immutable ranges.
Ordinary partial-route stream references therefore remain snapshot-copy inputs,
even when their source geometry happens to be unchanged. Safe reuse needs an
explicit ownership/version/retirement contract. Existing whole-bag baked blocks
cannot simply be enabled for arbitrary visible subsets: package0 encodes MSCAL,
later packages MSCNT, and clip interleaving changes the current program and GS
state. A package/run design must restore those entry conditions while preserving
classification, strip boundaries and required clipping.

### Partial baked geometry candidate (2026-10-07)

`TYRA_STAPIP_PARTIAL_BAKED` defaults to zero. The private `--partial-baked`
fixture selects it at runtime in six Off/On/Off then On/Off/On full-throttle
passes, with normal captures, TTL250 and no new scope clocks. Counters are on
only in the first triplet. Classification, clipping and strip boundaries stay
unchanged. Cold partial entries are built with the actual ordinary packet
writers into the existing versioned, leased baked arenas. Only fixed original
slices replay; copied, subdivided and expanded buffers keep the ordinary path.
Replay regenerates the standard header and MSCAL/MSCNT using the current
program state, borrowing only the immutable geometry body. No DMA CALL/RET
support or mutable-reference borrowing is introduced.

`--verify-partial` creates a separate correctness build: each replay compares
its geometry bytes and VIF commands against freshly generated actual-writer
packets, ignoring only the regenerated emit-state bit and final kick. Require
nonzero `PARTIALCHECK` counts with zero failures, target screenshots and all
six endpoints before pricing the non-verifying ELF. Host oracle testing passed
50,000 checks; the analyzer accepts its reference and rejects fourteen malformed
controls. These are not hardware performance evidence. The first native build
caught a PS2 `u32`/`uint32_t` pointer type mismatch in the verifier; corrected
casts retain the fixed-width oracle interface. Cache pressure, cold build cost
and default-off footprint remain explicit risks outside this private trial.

All six passes completed on PS2 and PCSX2 with the performance ELF. Both
analyzers accepted the endpoint/lane/driver/accounting checks and parked
semantic controls. The separate oracle completed six passes, with its last
periodic report at 131,072 checks and zero failures; no failure report occurred.
The PS2 boot used one bare `execee` from the user's manually reset ps2link,
without an extra network reset. No reload stall occurred in these six passes.

| Phase | Reuse | Counters | Drive work mean (ms) | Mean loop rate (Hz) |
| --- | --- | --- | --- | --- |
| 0 | Off | On | 14.600 | 49.30 |
| 1 | On | On | 14.550 | 50.08 |
| 2 | Off | On | 14.678 | 48.51 |
| 3 | On | Off | 14.388 | 50.98 |
| 4 | Off | Off | 14.647 | 49.07 |
| 5 | On | Off | 14.361 | 50.30 |

The counters-off candidate saves 0.259/0.286 ms against the intervening control.
The observed order saves only 0.050/0.128 ms; do not subtract a uniform observer
price. Mean snapshot bytes fall from 196,903-197,723 to 136,114 (about 31%),
with 20.24 partial package replays and 62,072 reused body bytes per moving loop.
Candidate cold partial builds average 0.273 per loop. Peak live baked occupancy
is 2,555,344 bytes versus 2,345,152-2,389,520; no eviction or reserve failure.

In the counters-off z=[-30,-10) bin, work falls from 17.996 to 17.047/17.249 ms
and mean loop rate rises from 34.41 to 39.24/37.74 Hz. Improvement is not uniform:
the next bin remains around 16.6-16.7 ms work and its cadence can worsen. These
are spatial bins, not identical trajectories or a guarantee of visible FPS.
Normal capture cadence remains active. This is a small positive candidate,
**not stable 60 Hz and not a production promotion**. Default remains off pending
broader scene/mutation/cold-start coverage and pricing of the compiled-off
footprint against an unmodified build. Unchanged clip/expanded/copied routes
still account for work the candidate cannot remove.

Evidence and provenance,
physical analysis
and raw CSV
retain this result separately from the rejected submission experiments.

### Clip expansion and reflection ordering follow-up

Two further default-off candidates are under qualification:

- `TYRA_STAPIP_EXPANSION_CACHE`: cache original strip-to-list expansion arrays
  per original strip/count and existing bbox/content stamps. Tagged package
  sizes separate array entries from VIF entries in the same 4 MiB baked cache.
  Existing retirement leases protect in-flight readers. The clip-plane mask,
  program header and kick remain freshly generated. Custom programs, billboards
  and the EE clipper fall back. Chunks borrow slices of the expanded run, keeping
  the original expander's winding. Reject failed immutable registration rather
  than publishing a permanently copy-only entry; abandon an unpublished span
  without a real DMA drain if budget reservation fails. Each expansion arena is
  bounded to 64 KiB within the shared budget. `TYRA_STAPIP_EXPANSION_VERIFY` compares all cached
  attribute bytes against the actual original expansion before rebinding.
- `TYRA_ENVMAP_COALESCE_BARRIER`: while recording only, omit the early env-map
  `align3D`, since `frameSendPacket -> recordGif` supplies a barrier before the
  raster redirect/restore. Legacy synchronous rendering keeps its real fence.
  No texture flush, raster restore, final FINISH or presentation wait is removed.

The synchronization audit found that `align3D` already records FLUSHA during
TyraX2 frame recording, while `frameWaitGif` and `frameWaitFinish` do not wait.
Calling these functions is therefore not evidence of an EE stall. The candidate
only removes duplicate ordering commands, not a supposed full GS wait. Capacity
overflow/unsupported transfers can still drain: the normal recording route and
absence of env-map fallbacks must be checked on the actual fixture. The v3
fixture's EnvFallback counter includes synchronous env calls, failed recording
of any GIF packet and multi-batch frame splits; a zero excludes all three,
not every possible wait elsewhere in the engine.

`--clip-followup` selects eight passes: Off, expansion, barrier, both, both,
barrier, expansion, Off. Counters remain enabled throughout; no additional
scope clocks, normal reflection captures, TTL250 and 1,200 warmup loops.
This protocol does not calibrate its own counter observer. `--verify-expansion`
is a separate correctness build and is never priced. Previous partial baked-body
reuse stays off throughout, so this tests the new changes rather than stacking
an unpriced third intervention.

The first per-clip-chunk cache passed at least 425,984 actual expansion checks,
but its comparison was rejected: parked snapshot bytes rose from 184,128 to
375,008 after the first cache arm, and as high as 1,726,192 in a later cache arm.
The cache reached 666 entries against a 512-span immutable registry; registration
failure is ignored by the older baked publisher, leaving complete entries on
copy fallback after registry pressure subsides. This is a residency/control
problem, not geometric corruption or a valid timing result. The revised cache
uses one registry slot per original strip and refuses failed registration.
Its initial parked controls return to 184,128 bytes; only three extra parked
entries are needed instead of 54.

The revised oracle and performance ELF each completed all eight emulator
passes, and the same performance ELF completed all eight physical PS2 passes
(4,500 sampled loops). Both strict analyzers accept the parked controls, full
routes, actual reuse and variant gates. The final oracle report records at
least 425,984 comparisons with zero failures. One moving oracle screenshot
and a parked performance screenshot were inspected, not a pixel-identical
raster proof. Emulator timings are not used below.

| Phase | Variant | PS2 drive work mean (ms) | Mean loop rate (Hz) |
| --- | --- | --- | --- |
| 0 | Off | 14.694 | 47.95 |
| 1 | Expansion cache | 14.213 | 51.54 |
| 2 | Barrier only | 14.752 | 47.39 |
| 3 | Both | 14.302 | 50.86 |
| 4 | Both | 14.191 | 51.76 |
| 5 | Barrier only | 14.787 | 48.17 |
| 6 | Expansion cache | 14.278 | 51.43 |
| 7 | Off | 14.741 | 47.50 |

Expansion-only saves 0.481/0.463 ms against its adjacent outer controls. Across
the two repeats, its mean is about 0.472 ms below the two Off means. Ordinary
expansion drops from 807-813 to 54-56 vertices/loop (about 93%), and snapshot
copies from 197,696-198,212 to 159,346-159,364 bytes/loop (about 19.5%). Cold
whole-strip builds average 0.25-0.26 per loop; cache reuse still feeds about
806-811 expanded vertices to VU, rather than removing visible geometry.
In z=[-30,-10), expansion-only is 36.63/38.14 Hz versus 34.87/33.30 Hz in
the outer controls. Its P95 work is still about 19 ms and maximum about
45-49 ms: stable 60 Hz and the isolated hitch remain unresolved.

Barrier-only does not show a useful gain. Combining it with expansion has
essentially the same mean work as expansion alone, with nonuniform per-bin
cadence. All measured env operations used recording, with **zero synchronous
env calls, GIF-recording failures and multi-batch frame splits**. These zeros
do not exclude waits elsewhere. The retained recordGif barrier, TEXFLUSH and
final presentation fence are necessary; deleting them is not the next fix.

Maximum live cache occupancy was 2,606,592 bytes and 161 entries, with no
evictions or reserve failures. The changed code remains default-off: this is
a positive experimental expansion cache, not broad production qualification.
The always-on observer is unpriced, and unrelated scenes, mutable legacy
callers, cold starts and combinations with the earlier partial baked-body
candidate still need coverage. Do not add the two experiments' savings.

Archived provenance and results
include raw CSVs, native source/ELF hashes, console logs, screenshots, malformed
analyzer controls and host frame-arena/writer checks. No authored example edits
or commits were made for this test. The user requested shutdown after this
report; ps2link poweroff is fire-and-forget, not remotely verified standby.

### Burnout reference, bounded conclusions

A separate PCSX2 v2.9.93 session on owned Xvfb98 loaded the user's slot6 without
changing the running Windows session, its settings, or original saves. A write
breakpoint on VIF1 CHCR (`0x10009000`) stopped at `0x001f1a3c`, immediately before
starting a chain with `0x145`. Two consecutive stops exposed roots `0x8fcf80`
and `0x7fd680`, each with 2,645 visited tags: 1,742 CNT, 38 REF, 432 CALL, 432 RET,
one END. The walker follows control flow with bounds and cycle checks.

Each root calls 355 distinct targets; 354 targets are shared and byte-identical
across the stops, including referenced payloads, totalling 825,360 unique bytes.
Both complete decoded VIF streams contain V4-32, V2-32, V2-16, V4-16, V4-8 and
V3-8 UNPACK commands. Vector counts are not unique vertex counts, and formats
alone do not identify attribute meaning. This is concrete evidence of reusable
DMA subchains and compact inputs, not a measurement of EE preparation time or
a complete frame census. Original saves show idle DMAs; their terminal TADR
must not be treated as a frame root. Stock PINE RAM reads of MMIO returned zero
here; use serialized `eeHwRegs.bin`, not those zeroes, for the captured registers.
SaveState drains consumers and these pauses perturb cadence. No Burnout or
TyraX hardware timing is inferred from the debugger.

The architectural lead is immutable reusable command/geometry packages with
small changing parameters. TyraX already has a direct replay path; extending
reuse to partial visibility is distinct from blindly borrowing mutable data or
assuming CALL/RET can be enabled without validator, lifetime and ordering work.
Full proprietary RAM/states stay outside the repo in the private fixture.

### Original driving trial provenance

Private fixture: `F:/Projects/tyrax2-full-throttle-20261007-v2`. Native build succeeded; 465 source files match their native build mirrors, and the shipped ELF matches the native output. ELF SHA-256: `4ac3066d441d3c28c02e3f80690ed8c7705c8d7a103dfa0fa2b015c27d87dce8`. The same ELF completed PCSX2 and PS2 runs. Emulator images were inspected for correct vehicle, chase view and roadway, not pixel-identical raster proof. Emulator timing is not used for hardware performance claims.

Physical analysis, raw CSV and console log are retained with asset hashes, launch identity, build output, source binding and emulator evidence. The analyzer accepts the reference and rejects ten malformed controls (missing pass, wrong lane, insufficient endpoint, wrong pedal, wrong driver, broken time accounting, counter leakage, inconsistent miss categories, wrong TTL and moving parked control). `git diff --check` passes. The owned emulator was stopped; physical ps2client remains alive. User-owned generated example changes and unrelated files were left intact.

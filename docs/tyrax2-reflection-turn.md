# Parked vehicle reflection turn

Stationary-car camera sweeps isolate reflection costs and qualify a private
VU1 paint-colour prototype on physical PS2.

Latest status: A13 physical results
complete six integrated EE/VU/EE road trips. Mean work saves 1.040-1.195 ms day
and 1.027-1.103 ms night; equal-distance estimates are 0.926/1.017 ms. Candidate
entry/present rates are 38.317/34.235 Hz, not sustained 60. Production remains
OFF. The user requested a pause after this series and revoked power-off
permission. Reverse is native-bound but only partially emulator-tested and
not deployed. Hardware recovery succeeded; the old A06 stall is not current.
Original overnight archives remain unchanged, including A13's A12 final-log
reanalysis. See the physical archive for the complete overnight summary.

## A11 generated game integration

A12 extends this to full-throttle day/night/day road travel using the integrated
path: each trip covers over 182 units with live capture, all three cars, camera
sweeps and EE/VU/legacy/override transitions. The corrected V2 completes 8,981
paint checks (1,138 at speed >5) with zero errors, both authored LODs and all
three reloads. Ten lifecycle and five travel negative controls reject malformed
evidence. V1's light counter incorrectly scanned inactive array capacity;
V2 counts the live prefix and confirms day/night/day lights 0/8/0. This is no
new renderer bug or speed gain. A12 evidence
preserves both attempts. A13 subsequently removes the heavy every-vertex oracle
and forced fallback/LOD and completes the first physical order as linked above.

`TYRA_VU1_EXP_PAINT` remains OFF by default. Both GeoPart templates now track
EE-colour readiness separately from accepted paint basis in the texture bag.
The paint loop queries the public engine API after setting HIGHLIGHT2, clears
stale VU requests for non-paint draws, and invalidates acceptance/distinct-normal
maps on source/count/content-stamp changes. A fallback materializes colours
from the retained basis without changing the existing camera hysteresis.

The actual loop passes 6,000 sanitizer transitions and three mutation controls;
flag-OFF preprocessed paint code/declarations match the before snapshot. Fresh
FPP/orbit generated physics TUs/headers pass the target compiler with flag 0/1.
On the native emulator night-road fixture, three reload/sweep phases exercise
12,124 paint visits, all six EE/VU/legacy/override modes and both authored LODs,
including 62 held-basis EE refreshes, with zero colour/state errors. Every phase
confirms midnight, 66 road chunks and eight active lights; final completion and
nine negative parser controls pass. No timings are inferred from this intrusive
oracle. Full-throttle driving and physical requalification remain outstanding.
See A11 evidence and limitations.

## VU paint trial (private, not promoted)

`tools/tyrax2-vu-paint.py <new-fixture> <source-project>` creates a private
EE/VU/EE (0/6/0) sweep. It retains the paint acceptance gate and stores the
accepted forward/half vectors separately from the capture sampling basis.
A VU input prepass computes the original Fresnel/specular formula before
clipping interpolation, overwriting only VU-local input colours. EE colour
arrays and their versions remain unchanged in the candidate. Upload layout,
package sizes and reflection captures remain unchanged. This deliberately
still uploads the existing colour stream; it tests removal of EE generation
and mutable-stream invalidation before changing the packet ABI.

The trial asserts ordinary VU clipping, per-vertex colours and no custom
program overrides. Legacy EE clipping and AsIs are not qualified, and no
production engine/codegen path is changed. Analyze with
`tyrax2-reflection-turn-analyze.py <csv-dir> <log> --vu-paint --selftest`.
`--reverse` on generator and analyzer selects 6/0/6. The middle-minus-controls
contrast consequently changes sign; a negative saving means the middle EE
control is slower. Promotion still requires production codegen/VU-generator
integration, safe legacy EE/custom-program fallback with invalidation on mode
changes, and actual matched GS raster/lifecycle qualification. Do not treat
the sweep screenshots or the host simulator as a hardware raster oracle.

### First physical A/B/A

Fixture `F:/Projects/tyrax2-vu-paint-20261008-v2`, ELF
`3463d3a4c4226071ea6102e75c7550556c5dd3d9771ff4e4967fe21de40c8cf1`;
467 source/native files match. PS2 and emulator complete all 2520 sampled
loops, with five malformed-parser controls rejected. Physical sweep results:

| Mode | Mean non-pacing ms | Loop Hz | p95 ms | Max ms | Entry periods >25 ms |
|---|---:|---:|---:|---:|---:|
| EE before | 13.870 | 53.54 | 17.317 | 20.802 | 86 |
| VU paint | 11.933 | 59.86 | 15.302 | 16.662 | 1 |
| EE after | 13.829 | 53.48 | 17.309 | 20.868 | 87 |

Savings are 1.896-1.938 ms against bracketing controls. All three arms retain
568 accepted paint updates and 1440 body submissions; the candidate records
1440 VU-enabled submissions. Captures are 174/179/174, so the elapsed delta
includes scheduling/cache consequences and is not isolated EE arithmetic.
The candidate has no work intervals over one field but one long entry period:
presentation deadlines and the measured work interval are not identical.
Emulator savings are 0.929-0.938 ms, NOT physical prices; all its arms are
59.94 Hz. Three moving-view screenshots show no obvious corruption but are
not frame-aligned pixel comparisons.

### Reversed physical order

Fixture `F:/Projects/tyrax2-vu-paint-reverse-20261008-v1`, ELF
`44fdb53d4cda6ab59db5c907ad44198ca250f9b453e1cf1e77f38e45cbfc0047`;
467 files bound and all 2520 sampled loops complete. Five parser negative
controls fail as required. VU/EE/VU gives:

| Mode | Mean non-pacing ms | Loop Hz | p95 ms | Max ms | Entry periods >25 ms |
|---|---:|---:|---:|---:|---:|
| VU before | 11.952 | 59.94 | 15.192 | 16.192 | 0 |
| EE middle | 14.017 | 54.08 | 17.450 | 21.254 | 78 |
| VU after | 11.956 | 59.94 | 15.230 | 16.056 | 0 |

VU saves 2.061-2.065 ms here. Paint acceptances remain 568 in every arm,
body submissions 1440, captures 174/179/174: the middle-phase capture-cadence
difference reverses sides, while the gain remains. Both physical orders
support a useful private candidate; neither proves a universal 60 Hz result
for roads, driving, night scenes or other vehicles. All 303 staged physical
inputs per fixture still match after execution, and the user's original
manifest hash is unchanged. Evidence is in
`tyrax2-reflection-turn-2026-10-08`.

### Numerical checks

`tools/verify-vu-paint.cpp` parses the actual injected VCL prepass using the
repo's `vuasm`/`vusim`. Per program, 73,500 components differ from independent
scalar paint arithmetic by at most 0.000396729 (limit 0.001, versus one whole
GS colour step). Disabled modes preserve every memory word. Zeroing the
actual specular coefficient is detected. Whole-program comparisons use 300
randomized draws per shader, comparing EE-prepared colours with VU-prepared
colours through transformation, clipping, interpolation and GIF output.
All five variants pass: cull TCE, unit-normal cull, shared clip TC, folded
shared clip TC, reference clip TCE. All 300 draws per variant emit geometry;
295/300 clip draws change the vertex count. All tested GIF output words match
exactly (colour tolerance allowed one step, observed zero). This is source
simulation, not proof of the target assembler's register allocation.
Existing `--vu-check` passes too, but exercises the disabled new branch.

The first fixture-generation attempt (`...-v1`) is rejected: an insertion
anchor matched both `xtop` instructions in the shared clip program. The
generator now anchors `begin: xtop`; the failed fixture was not deployed.

## A07 target-assembled raster qualification (2026-10-09)

The overnight follow-up tests actual compiled C16 VU shaders through PCSX2 and
GS readback, rather than another timing series. `tools/tyrax2-paint-raster.py`
creates 27 isolated EE/VU/EE cases: three fixed paint bases and inside,
near-crossing and view-edge geometry. Texture, camera, positions and normals
are deterministic. Candidate uploaded colours are deliberately incorrect, so
a bypassed paint prepass cannot pass by reusing EE results. Four draws per case
exercise warm reuse; source stamps change only between completed cases.

V2 fixture: `F:/Projects/tyrax2-paint-raster-20261009-v2`.
468 native-bound files; ELF SHA256
`579c24f6c1b08eaaa3f676dd15335476a76a242fd9a3538396bf2e5feefbbb00`.
All 27 raw 448x448 CT32 images are nontrivial. All nine EE/VU comparisons and
nine EE/EE controls have **zero changed RGBA pixels**. Near-crossing cases
record four actual clip calls each; inside/view-edge cases record one cull
call each followed by reuse. Six exported images were generated from raw
readback; the near-crossing and view-edge candidates were visually inspected.
Parser controls reject off/off pixel corruption and measure one candidate pixel
mutation exactly. Image, log, source and launch ELF hashes are recorded.

V1 completed all cases but is rejected: it omitted TerrainGame's clipMargin
initialization before setRenderer, so the inherited -10 default removed every
clipped triangle. Nonblank validation rejected those images. Its three inside
groups did match exactly, but that is not full clipping qualification. The first
native compile also required the actual SDK `screenshot.h` include, corrected
before launch. Both attempts remain preserved in the evidence archive.

This passes ONE bounded shader-output gate. It does not cover actual vehicle
LOD/reload lifecycle, EE/custom-program fallback, mode changes, physical GS
output, night/fog or a new PS2 performance result. A06 left physical hardware
stalled during an unrelated scene reload; no reset loop or A07 deploy was issued.
The private paint path remains private. Next audit representation readiness:
switching EE/VU with an unchanged accepted key must not retain stale colour data
or uninitialized VU basis. Preserve the prior accepted basis/hysteresis.

Evidence: A07 snapshot.
All 1034 archived files verify against their manifest. Both 468-file source
bindings and both ELFs were independently verified before export. No emulator
from this test remains running; physical owned server 13224 remains alive.

## A08 paint representation lifecycle (2026-10-09)

`tools/tyrax2-paint-lifecycle.py` privately applies a lifecycle correction to
the C16 prototype. Visual acceptance and representation readiness are separate:
the accepted forward/half basis is stored on EVERY acceptance, regardless of
EE/VU mode. EE colours are materialized only when needed and from that stored
basis. Switching representation does not accept the current camera direction
or relax paint hysteresis. Source/count/normal content-stamp changes invalidate
the geometry-dependent state; the distinct-normal table also checks the stamp.

The private engine capability query conservatively rejects legacy EE clipping,
any installed program override, non-normal coordinates, single colour, lighting,
billboards and non-MVP transformations. The game falls back to EE colour data
before submission. The fixture switches EE/VU/legacy/VU/override/VU every 30
frames through the REAL clipping/override APIs. Its custom program is an instance
of the same built-in TCE image installed as an override, not an arbitrary user's
shader. Three ordinary scene loads and forced authored LOD changes are retained.

Host ASan/UBSan: 6000 transitions, source/count/stamp changes, fresh state and
stale-colour corruption detection pass. V1 generation failed before build/deploy
on an include anchor; V2 uses the actual game_runtime.gen.hpp include.
V2 fixture: `F:/Projects/tyrax2-paint-lifecycle-20261009-v2`.
468 bound native source files, ELF SHA256
`f1419187da7901ce5cb67c20f462ac3fa44797fd8c5b3777c12270f659622e7f`.

| Phase | Paint visits | Accepted bases | EE refreshes | Refresh with held basis | Errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 4000 | 582 | 312 | 22 | 0 |
| 1 | 4062 | 582 | 314 | 20 | 0 |
| 2 | 4062 | 582 | 314 | 20 | 0 |

Every phase records 662 legacy fallback and 660 override fallback submissions,
all six modes, both available LOD tiers, 66 road chunks, 18 shadow pins, three
vehicles and the paused noon scene. Raw TMDL inspection ties LOD coverage to
the three authored vehicle IDs: each has ONE far tier (mask 3), even though
unused cars left in bin have two. Requested tier 2 clamps to tier 1; it is not
a third tested tier. The analyzer rejects six malformed witness variants.

Every submitted EE paint colour is checked against scalar evaluation of the
accepted basis with a 0.001 tolerance; VU uniform basis fields are bit-checked
against that state. This is intrusive correctness work, NOT timing evidence.
All three stationary screenshots were captured; phase 0 was visually inspected.
No matched whole-scene raster, arbitrary-custom-shader, night/fog, physical PS2
or production-default qualification is claimed. Same-pointer stamp invalidation
is exercised on host, not injected into live geometry in this run. The actual
scene rebuild and LOD tests exercise fresh/rebound data instead.

Evidence: A08 snapshot.
All 500 archived files verify against the manifest; the 468 bound native sources,
ELF and three authored vehicle body assets were also checked before export.
The owned emulator and Xvfb have stopped; no A08 physical deployment occurred.

## A09 generator integration (2026-10-09)

`vugen::buildPaintExperiment` owns the accepted-basis paint loop as a private,
explicit generator entry point. It emits independently named images for Cull
TCE, shared Clip TC and the reference Clip TCE. It rejects other built-in
families and custom/staged programs, and does not change the ordinary catalogue,
engine defaults, authored maps or generated game paint loop.

Host qualification: all 45 ordinary emitted files are byte-identical to the
pre-change snapshot; rebuilt editor `--vu-check` passes. The focused harness
compares 8,400 draws against C16, including ordinary TC and TD shared paths,
and rejects three specular-zero mutations. The numeric oracle runs the actual
generated prepass, checks 73,500 components per image, all 64 empty-input plane
masks, and 300 whole-shader draws. Maximum scalar difference is 0.000396729;
tested GIF words are identical. This is not a hardware timing result.

`tools/tyrax2-paint-generator.py` prepares the separate target qualification:
it transplants only the generated prepass into the existing FOLD/ENVN bodies,
emits separately named whole images, and switches all three TC/TD/TCE wrappers
to the same shared clip image. It does not replace the folded/normalized body
with a different algorithm. The raster harness has 54 EE/VU/EE cases across
fog disabled/enabled, and must prove fog actually changes pixels. This remains
synthetic output qualification, not night gameplay or production readiness.

The first staging invocation used WSL Python with a Windows-only generator
chain and was stopped while copying, before build or launch. V2 uses Windows
Python and the same authored full-road project as A08. No console reset/deploy.

V2 native/emulator completes, but its strong 12-near fog range leaves two
view-edge bases with only 110/115 unique RGB values and fails the unchanged
>128 nontriviality gate. Inspected images contain geometry, not a blank frame;
this is an over-compressed test palette, not an observed EE/VU mismatch. Retain
the rejected inputs/results. V3 uses 24-near fog end, preserving every oracle.

V3 ELF: `4a2bddae33e119ff8f06c4adc93c0c428a8e56c8291429970c6683e928f46f76`.
473 native sources and the launch ELF agree. All 54 readbacks pass: 18 exact
EE/VU RGBA groups, 18 exact EE/EE controls, observable fog in all 27 matching
pairs. The first 27 also match the old A07 raw pixels exactly. Three analyzer
negative controls cover control-pixel corruption, one candidate pixel and a
fog no-op. The fixed texture was hash-bound before the V3 launch and is archived.
The actual linked PAINT images are shared Clip TC FOLD (404 slots) and Cull TCE
ENVN (166 slots); no duplicate TC/TD/TCE clip images are linked. Fog clipping
and view-edge candidate images were inspected. Final generator ASan/UBSan and
rebuilt `--vu-check` pass; ordinary 45-file byte identity still holds.

Evidence: A09 snapshot.
All 1,336 archived files verify against the SHA256 manifest; both 473-file
native bindings and their ELFs were independently checked before export.
The generator-owned loop is qualified in this bounded synthetic test, not in
night gameplay or on physical PS2. Engine capability/uniform and generated
game lifecycle integration remain next, with defaults OFF. No new timing claim.

## A10 opt-in engine contract (2026-10-09)

The engine has a default-OFF `TYRA_VU1_EXP_PAINT` whole-image switch and public
`StaPipCore::supportsPaintVu(bag)` capability. With the flag off, paint fields
and submission checks/uniforms compile out and ordinary VCL images are unchanged.
Four derived images preserve baseline raw/FOLD/ENVN bodies; reproduce/check them
with `tools/emit-paint-images.py <generated-output> <engine> [--check]`.
All TC/TD/TCE wrappers share the same selected clip image; no extra independent
clip image is added for TD or TCE.

Eligibility requires VU clipping, a resident env class, no program overrides,
MVP, HIGHLIGHT2, real texture/normal coordinates, many colours, and no lighting,
billboard or single colour. The caller promises unit normals and an accepted
paint basis, and must compute EE fallback BEFORE submission. Invalid requested
paint asserts in debug; the public render entry skips it under NDEBUG rather
than submitting stale colours or an incompatible stream. This is fail-closed
misuse handling, not a substitute for correct generated-game fallback.

`tools/tyrax2-paint-engine.py <new-fixture> <A09-V3> [--enabled]` uses the real
engine wrappers/capability/uniforms, not private replacements. It checks runtime
eligibility changes and 54 synthetic fog-off/on readbacks; the disabled build
must use EE for every group. Unused TerrainGame TUs retain EE paint so they
do not accidentally depend on C16 private fields. Generated-game accepted-basis
integration and actual night/roads qualification remain the next step.

Set the compile-time flag in the shared engine header and rebuild BOTH engine
and game: conditional texture-bag fields make inconsistent per-TU flags an ABI
error, not a runtime toggle. The capability reports availability; it does not
enable or switch a bag. Readiness must remain the generated game's responsibility.

Host `verify-paint-engine.py` compiles the actual capability method and render
guard extracted from the source under ASan/UBSan. It checks both flag values,
all rejection conditions and NDEBUG-style assertion removal; deleting the
release guard's return must fail the test. It also preprocesses all 32 wrapper
combinations (paint/fold/normalized flags and four wrappers). This is a bounded
source-fragment test, not a full release game run. Native verification accepts
`--symbol paintRasterCull` for this non-motion harness; the old default remains
`MotionProbe::rows`, and a deliberately missing symbol is rejected.

Both final builds pass 17 runtime capability checks plus null rejection and
54 nontrivial fog-off/on GS readbacks. Every enabled EE/VU/EE comparison is
exact; the disabled build actually uses EE for all cases and is also exact.
All 54 pixels sets in each build match A09 V3. Bindings cover 471 native source
files each. Disabled ELF has no PAINT image symbols; enabled links only one
shared Clip TC FOLD PAINT (404 slots) and Cull TCE ENVN PAINT (166 slots).
The original 15 cull/clip VCL sources remain byte-identical; all four derived
image scalar/GIF oracles, image regeneration check and rebuilt --vu-check pass.

Enabled ELF: `e40a3cfa06aff16b5ae7c28b14fa5e4ea2ae74ff7748893d44e132358bfa7989`.
Disabled ELF: `923b7c1f4f3d8f61d340bc3b3bfbd8a05f14a0d8427218bdb9f71cc158517bdd`.
The initial enabled build missed the private motion header still included by
unused TerrainGame TUs; it failed compilation, was fixed and rebuilt before any
launch. No engine motion hooks were imported. The first verifier also correctly
rejected the absent MotionProbe::rows symbol before the explicit raster-witness
option was added. Both emulator runs are stopped. No physical deployment.

Evidence: A10 snapshot.
All 1,173 archived files verify against the SHA256 manifest; native sources and
ELFs were independently checked before export. The enabled near-clip fog image
was visually inspected. Authored full/no-roads manifests retain their original hashes.
This completes engine eligibility/uniform/output integration, not generated-game
lifecycle or night gameplay. A11 must integrate both GeoPart templates and the
accepted-basis state; no production default is enabled.

## Night with roads follow-up

Historical C16 procedure below targets the pre-A10 source snapshot. Its exact
injection anchors deliberately reject a changed engine. Reproduce on the bound
historical sources, not by loosening those assertions in the current tree.

Pass `--night-roads` to `tyrax2-vu-paint.py` and the turn analyzer (alongside
`--vu-paint --selftest`) to use the full authored vehicle-playground copy.
The source main scene must contain seven roads. All three vehicles retain
the same shiny bake; the project's ordinary `district-night` value is set to
one in the scratch manifest, activating its fixed midnight mood and lamps.
No roads or light effects are removed. The measurement remains the same
stationary handbraked-car sweep, not a driving test. Devkit/profilers remain
disabled in both arms. Before each measured phase, `NIGHTROADS` requires
hour zero, paused time, resident nonempty road chunks and active dynamic
lights. The analyzer rejects missing/daytime witnesses and differing context
counts across phases. Historical road-free/default commands remain unchanged.

The source is `F:/Tyra-Projects/vehicles-test/vehicle-playground`; its original
manifest SHA-256 before preparation is
`3c20bfeac1de6e74c923b6474069f3adbb36947ad3cfca35ac458c9bd9aca69c`.
Fixture `F:/Projects/tyrax2-vu-paint-night-roads-20261008-v1`, ELF
`a70830e7bb9ebb5a04b2d8f707371b37b50396b133fb8ef8d2463467abba65c2`.
467 source/native files match. PS2 and PCSX2 each complete 2520 sampled loops;
all nine analyzer negative controls are rejected. Every phase reports hour 0,
paused time, 66 nonempty road chunks, eight active dynamic lights and 18
ground-mask pins. Source settings retain three shiny vehicles (strength .45),
dynamic reflection maps and the normal frame pipeline; devkit is disabled.

| Physical mode | Mean non-pacing ms | Loop Hz | p95 ms | Max ms | Entry periods >25 ms |
|---|---:|---:|---:|---:|---:|
| EE before | 20.982 | 30.48 | 26.100 | 28.413 | 696 |
| VU paint | 19.130 | 31.92 | 23.778 | 25.145 | 632 |
| EE after | 21.029 | 30.48 | 26.153 | 28.341 | 696 |

The candidate saves 1.853-1.899 ms (about nine percent non-pacing elapsed),
consistent with the road-free daytime benefit. The heavier scene remains
above a 16.68 ms field budget in 629/720 candidate work intervals; most
presentations therefore still land every second field. Mean work alone is
2.45 ms over budget, p95 is 7.09 ms over and maximum is 8.46 ms over: reducing
only the mean to budget would not establish stable 60 Hz.

All arms perform 568 accepted paint updates and 1440 body draws. Captures
are 174/179/174, and the candidate witnesses 1440 VU-enabled body submissions.
The same scheduling caveat applies: elapsed savings are not pure EE arithmetic.
Compared with the earlier road-free daytime VU arm, combined roads/night add
about 7.20 ms mean elapsed, but this is a cross-fixture observation with
different vehicle metadata, NOT an isolated road or night cost. The completed
same-source follow-up below supersedes that context comparison (6.902 ms).

PCSX2 stays at 59.94 Hz in all arms (9.884/8.984/9.916 ms non-pacing), so it
again does not reproduce the hardware deadline limit. Screenshots witness
nighttime roads and the candidate/control sweep without obvious corruption;
the capture helper missed phase 0 and captured phases 1 and 2 only. No paired
GS raster equivalence is claimed. `--vu-check` passes. All 303 physical launch
inputs and the original authored manifest hash match after the run. The
private candidate is still not promoted to production.

## Roads/day versus night/no-roads controls

The follow-up uses `--roads-day`, `--night-no-roads` and `--day-no-roads`
on the VU generator and matching analyzer. All three MUST use the full
seven-road source project. The two no-road modes skip main-scene `buildRoads`
after its normal old-road cleanup, retaining the authored manifest and bake
identity. This removes road chunks, their drawing and chunk-based queries;
road definitions remain compiled. Other scenes, objects, vehicles and baked
shadows remain unchanged. The day baseline is refreshed
to complete a same-source factorial comparison with the preceding roads/night
run. Each cell keeps EE/VU/EE and the same parked angular sweep.

Why not reuse the older no-road project directly: its 128 common main objects
match the full map, but its vehicle metadata does not. It has different
`pieces` and `envLimits`, and Strix glass opacity .3 rather than .45. The
initial `tyrax2-vu-paint-night-no-roads-20261008-v1` build from that source is
excluded and was never deployed. V2 derives from the full map but removing
road references before generation invalidates its shadow bake: it reports
zero pins and `DRIVEDONE valid=0` on both targets. Reject that entire run;
do not relax the 18-pin acceptance gate. V3 retains the manifest and skips
runtime road construction instead. The corresponding day/no-road v1 was
built but not deployed; day/no-road v2 uses the corrected intervention.
Those differences do not invalidate within-fixture EE/VU contrasts, but they
prevent treating the old between-map delta as a clean road-only intervention.

The historical `NIGHTROADS` log label is retained for compatibility, now also
recording noon (12), zero road chunks or daytime inactive lights as applicable.
Parser controls validate the requested hour and road presence/absence instead
of assuming every new context is night with roads. Daytime requires zero
active lights; all contexts reject nine malformed controls. The generator,
host paint oracle and factorial analyzer are explicitly allowlisted in
`.gitignore`, so the diagnostic tools are not silently omitted from Git.

`tyrax2-factorial-analyze.py DAY_NO_ROADS DAY_ROADS NIGHT_NO_ROADS NIGHT_ROADS
OUTPUT_JSON` checks normalized authored settings, all 135 main objects, 302
identical non-ELF launch assets, shadow tables and paint-program invariants,
then checks sampled camera/vehicle poses before contrasting mean non-pacing
elapsed time. Strict world-pose equality FAILED: removing road geometry also
lowers the car and camera by .12 units, the authored road contact-surface lift.
The explicit `--allow-road-height-shift` report requires that exact rigid shift
in y/eyeY/lookY and no other pose difference (residual tolerance .00002).
Day/night pairs have identical world poses. Road contrasts include changed
ride/view height and must NOT be called pure road drawing costs. These are
separate qualified fixtures, not a
same-ELF randomized experiment or separate EE/VU/GS clocks. The night factor
is the complete project midnight mood, not just the eight light calculations.

### Physical results

Each cell completes 2520 sampled loops in EE/VU/EE order. Table entries show
the middle VU arm; controls retain ordinary EE paint preparation.

| Context | EE before/after ms | VU ms | VU loop Hz | VU p95 ms | VU long entries |
|---|---:|---:|---:|---:|---:|
| Day, no road geometry | 14.172 / 14.170 | 12.228 | 59.77 | 15.669 | 2 |
| Day, roads | 17.495 / 17.588 | 15.565 | 47.85 | 19.381 | 182 |
| Night, no road geometry | 17.115 / 17.114 | 15.177 | 47.95 | 18.980 | 180 |
| Night, roads | 20.982 / 21.029 | 19.130 | 31.92 | 23.778 | 632 |

The VU candidate saves 1.85-2.02 ms across these cells. Every arm keeps 568
paint acceptances and 1440 body submissions; captures remain 174/179/174.
The fresh day baseline is near, not perfectly at, 60 Hz: two long entries
and maximum work 17.013 ms. It replaces the old different-project baseline
for the context comparison, without invalidating prior within-fixture gains.

With VU paint, the complete night mood adds 2.949 ms without road geometry
and 3.564 ms with roads. The natural road intervention adds 3.338 ms by day
and 3.953 ms by night, including the .12-unit height change above. The joint
increment is 6.902 ms; the interaction contrast is .615 ms. EE controls show
the same qualitative split (interaction .498-.544 ms). Most of the combined
cost is already present in the two separate factors; this is not evidence
that one exceptional road-light interaction explains all slowdown.

Next: inspect/qualify the road submission path in daylight separately from
night receiver/light passes. Keep the VU paint integration and its production
fallback/raster obligations separate. No production optimization is promoted
by these diagnostic fixtures, and this is still parked camera turning, NOT
full-throttle driving or a proof that either component is exclusively EE-bound.

Accepted new fixtures and SHA-256 ELFs (467 source/native entries each):

- `F:/Projects/tyrax2-vu-paint-roads-day-20261008-v1`:
  `cb65f0007f92437661a23ded78ad7166039fcfee0f3265deeb9dc6ba51f30e28`.
- `F:/Projects/tyrax2-vu-paint-night-no-roads-20261008-v3`:
  `f09f30062985845bf1e79ad3197cad231bfb9e4b440b2b656b4ab0d4507da0a0`.
- `F:/Projects/tyrax2-vu-paint-day-no-roads-20261008-v2`:
  `22fa2aa5a0d4dc0492c0767c81f4380f785329930d760865d5341bd3a61f9131`.

All three also complete on PCSX2 at 59.94 Hz and reject nine malformed parser
controls per target. Their three-phase screenshots show no obvious corruption;
they are not pixel-aligned GS comparisons. All 303 staged files per cell and
both original user manifests were rechecked unchanged. Owned emulator/host
processes were stopped and the final network reset returned zero after settle;
the physical ps2link screen was not independently inspected. Complete results,
rejected zero-pin evidence and source/tool snapshots are in the evidence folder.

## Road-only strip refinement trial

`tools/tyrax2-road-refine.py <new-fixture> <full-source>` reuses the daytime
seven-road sweep and holds private VU paint ON in every arm. Modes 0/6/0 now
mean road subrange refinement off/on/off, not paint off/on/off. Only road
bags in `renderRoadChunks` opt into the existing conservative `refineStrips`
path; terrain defaults, geometry, contact height and capture policy stay intact.
Use the turn analyzer with `--vu-paint --roads-day --road-refine --selftest`.
Cold offsets 1000-1099 witness actual road partial-package entry and successful
subrange-cache entry via an exact bag registry; the registry is not traversed
during sampling. These cold counters are activation evidence, not timed work
counts or an EE/VU cost partition. `--verify` enables the existing per-range
vertex/plane oracle in a separate correctness fixture, never for pricing.
`--reverse` on generator and analyzer selects on/off/on, still with VU paint
enabled throughout. This trial is private and not a production opt-in.

The first physical off/on/off result is 15.455 / 15.197 / 15.503 ms,
a 0.258-0.306 ms saving. All three arms present at 48.166 Hz with 176 long
entry periods each: the gain does not cross enough presentation deadlines.
Each keeps 568 paint acceptances and 1440 VU body submissions, with captures
174/179/174. This is a useful bounded candidate, not the main road slowdown.
The reverse on/off/on physical order gives 15.001 / 15.355 / 14.967 ms,
saving 0.354-0.388 ms; again all arms remain 48.166 Hz with 176 long entries.
The middle-phase capture-count difference changes sides but the gain retains
its direction. Do not subtract the two ELFs' absolute means as a new contrast.
PCSX2 completes both orders at 59.94 Hz; its gains are not hardware prices.
All three fixtures (forward, reverse, oracle) complete 2520 rows on each
target and reject eleven malformed analyzer controls per run.

The separate verification ELF completes all 2520 samples on both PS2 and
PCSX2 with the range oracle enabled, positive cold road activation and no
witness assertion. Read-only emulator PINE after completion reports 508689
accumulated range checks and zero failures (includes terrain and warmup,
not a road-only timed count). Host ASan/UBSan independently passes 190000
range witnesses and malformed-route tests. These are geometry classification
checks, not an exact framebuffer oracle; the verification build is unpriced.

Source audit: roads are tessellated at scene load, not regenerated on every
camera turn; height queries already use a spatial index. In the preceding
day/no-road versus day/road VU fixtures, mean update is 1.066 versus 1.174 ms,
while render minus pacing is 11.161 versus 14.392 ms. The natural road delta
is therefore mostly render-side, with the same .12-height caveat as above.
Do not infer a pure EE bill from the render label.

A subsequent candidate was constant-colour road submission: `buildRoads`
emits identical grey RGBA entries for ordinary road/junction vertices, and
`procFinishChunks` binds them as `many` even though `StaPipColorBag` supports
`single`. Any trial must preserve varying-alpha spill/edge colours, baked
cache keys, deferred-bag lifetime, fog/light behaviour and package topology.
The completed trials below find only small gains, not the main road cost.

Evidence lives in `tyrax2-reflection-turn-2026-10-08/road-refine`. Bound ELFs:

- Forward `F:/Projects/tyrax2-road-refine-20261008-v1`:
  `5bf384e6652ca51762c7a1109ee8f969c637ba80e40702ce7e06c1df6b3e2a03`.
- Reverse `F:/Projects/tyrax2-road-refine-reverse-20261008-v1`:
  `677619fe399a2ba273752bd362e564e9875b7d64f878c3aa48b9dfbf311e57d2`.
- Oracle `F:/Projects/tyrax2-road-refine-oracle-20261008-v1`:
  `76b19ff32e1ac7406dad05200df90f01e471eb4c6bce9142180d5a7c36613f15`.

Each binds 467 source/native files; 303 physical launch files were rechecked.
Original authored projects are unchanged. No production switch was added.
Driving, night, scene lifecycle and matched GS raster qualification remain
open before production road opt-in, independently of paint promotion.

## Constant-colour road trial

`tools/tyrax2-road-single.py <new-fixture> <full-source>` compares ordinary
per-vertex road colours with the existing single-colour path in off/on/off
order (`--reverse`: on/off/on). VU paint stays on, road refinement stays off.
Only ordinary road/junction chunks are eligible; spill and soft-edge alpha
streams remain untouched. At scene preparation, assert every eligible RGBA
equals (128,128,128,128), then bind a static-lifetime single colour instead of
the many-colour pointer. Retain geometry, strip runs and original colour arrays
for ownership and contact data; this does not claim resident-memory savings.
Cold `ROADCOLORS` witnesses require positive eligibility, complete selection
and actual road submissions in candidate arms, none in controls. Analyze with
`--vu-paint --roads-day --road-single --selftest` and matching `--reverse`.
No production path is changed. The colour-only physical A/B/A is
15.297 / 15.300 / 15.340 ms, all 48.166 Hz with 176 long entries. Its
-0.003/+0.040 ms contrast does not establish useful savings; do not promote.
PCSX2 completes at 59.94 Hz with 0.044-0.045 ms savings, not a physical price.
Each target completes 2520 samples and rejects twelve malformed controls.
Cold witnesses show 61 eligible chunks, 23466 eligible vertices and 2900
candidate submissions in 100 warm loops; five varying-alpha chunks stay many.
ELF `378ef22be83409e66e89fcb538ce76e2b38384eb96a2cd1ad66f6a75b6ccc19d`
is bound to 467 source/native files in
`F:/Projects/tyrax2-road-single-20261008-v1`.

### Repacking follow-up

`--repack` also repacks each existing eligible strip chunk into the actual
single-colour textured class capacity obtained from the engine. It does NOT
retessellate roads, change chunk bounds/UV rebasing or remove input surfaces.
Join degenerates and two-vertex overlaps preserve topology. Before publishing
new storage, compare every nondegenerate triangle's position and ST bytes,
in order, against the original runs; road contact indices are invalidated.
The engine's normal clip divisor/capacity formula remains in force, not an
arbitrary larger clip-output budget. Analyze with additional `--road-pack`;
require fewer packets and positive triangle-verification witnesses in On arms.
`verify-road-repack.py <fixture>` compiles the actual injected block under
ASan/UBSan: 2000 random arrays at capacities 90/150, control immutability,
tails/joins and a corrupted-position negative control pass. Native v1 failed
on an escaped logging newline and was never launched; corrected v2 is separate.
V2 physical off/on/off is 15.265 / 15.194 / 15.284 ms (0.071-0.090 ms saved),
all 48.166 Hz. It changes 307 whole-scene strip packets to 272, verifies 19320
triangles, but adds join degenerates (eligible vertices 23466 -> 24459).
It completes both targets and fourteen malformed parser controls are rejected.
This does not establish a useful FPS improvement. Bound ELF:
`2fdd597d5743a5ad98cbf04e6f796315a63de896d892741654429ea95fcef356`.

V3 recognizes byte-identical two-vertex/ST overlap at old package boundaries
and continues that strip instead of adding another degenerate join. Unrelated
runs keep the safe join path. Host randomized checks cover both cases. It
remains the same per-triangle byte oracle before publication. Physical off/on/off
is 15.028 / 14.869 / 15.061 ms, saving 0.159-0.192 ms; rates are
48.220 / 48.328 / 48.166 Hz, with 175 / 173 / 176 long entry periods.
Whole-scene stored strip packets fall from 307 to 258; this is not a count of
timed dispatches. Eligible vertices fall from 23466 to 23382 and 19320 triangles
are verified. Both targets complete 2520 samples; fourteen malformed parser
controls are rejected. PCSX2 remains 59.94 Hz, with a drifting final control
(7.480 / 7.364 / 7.829 ms), so its timing is not physical evidence.
Bound ELF: `a8a43054b0692e8096b7980719fb2f450ad7bbd359ffe9a8d890c55119a61637`.

Close this narrow packing investigation without production promotion: the
gain is small and does not restore 60 Hz. Do not sum these savings with road
refinement without a combined test. Night, driving, lifecycle and paired GS
raster qualification are still absent. Triangle byte equality and emulator
screenshots are not a complete raster oracle, and elapsed work is not pure EE
time. Evidence is in `tyrax2-reflection-turn-2026-10-08/road-single`.

## Road submission isolation

`tyrax2-road-submit.py` preserves all authored roads, generated storage,
contact indices and light receivers. After ordinary distance/frustum rejection,
the middle arm omits `submitHeavy` only for accepted road bags. This includes
reflection views using the same entry point. Constant VU paint, unchanged
ground pins, normal captures and strict same-pose checks remain required.
Cold ROADSUBMIT witnesses count accepted bags/input vertices and omitted bags;
the analyzer requires equal eligible workload and complete middle-arm omission.
This is a destructive rendering diagnostic, not an optimization: any price
includes downstream VU/GS work, scheduling and changed overdraw/reflection
contents. It removes the earlier road-removal ride-height confound but does
not isolate EE. Day PS2 A/B/A completes at 15.425 / 12.463 / 15.405 ms,
saving 2.943-2.963 ms. Rates are 48.166 / 59.609 / 48.166 Hz; long entry
periods fall from 176 in each control to four. Cold accepted workload is
3200 bags / 1213500 input vertices over 100 loops, identical in all arms;
the middle arm omits all 3200. These are accepted whole-bag inputs, not
post-clipping vertices or a timed-window dispatch count. All 66 road chunks
and 18 ground-mask pins remain resident. Same-pose checks pass without the
earlier .12-unit exemption. This identifies a substantial net drawing cost,
not a fix: the diagnostic deliberately removes visible asphalt.

Day bound ELF: `499d0f2b6f013c3b7b5556b0bfa4c624ce8aa571d8275ad66fa7d7a9ae4be90d`.
Night bound ELF: `e0aa50644e71160c01e13caab68ea0c8cac083cc1dc7164b88e66eab661732fa`.
Night PS2 A/B/A is 18.752 / 15.502 / 18.809 ms, saving 3.250-3.307 ms;
32.695 / 47.899 / 32.449 Hz, with 600 / 181 / 610 long entry periods.
Cold road inputs remain identical to day, with eight enabled night lights.
Day/night sampled poses match exactly and all 302 non-ELF deployment inputs
match. With road submission omitted, the full night mood still adds 3.039 ms.
This is not an isolated light-pool price: night changes other rendering too.
Both targets complete all 2520 samples per fixture and each analyzer run
rejects twelve malformed controls. Each ELF binds 467 source/native files;
all 303 physical inputs and the original authored manifests were rechecked.
PCSX2 completes all arms at 59.94 Hz; its prices are not PS2 evidence.
Emulator screenshots witness omission; they are not intended to match control
pixels. No production rendering changes. Evidence lives in the `road-submit`
archive subdirectory. Owned hosts/emulators are stopped; final network reset
succeeded with ten-second settle, without independent TV confirmation.

Next discriminate accepted road clipping/dispatch cost from raster work and
separately attribute night receiver/light passes. Do not repeat packing or
assume the road tessellator lacks planar reduction: it already collapses flat
and affine coplanar lateral spans. No main-view/reflection split or individual
night-producer cost is claimed by these two completed interventions.

## Road primitive-kick isolation

`tyrax2-road-submit.py ... --no-kick` keeps the accepted road submissions and
all road storage. It marks only road bags during scene preparation, and the
actual standard packet writer substitutes GIF XYZF3 for XYZF2 in their
primitive register list. VU input streams, shader arithmetic, clipping and
output lengths are unchanged. Both retained and baked cache primitive keys
include the flag in bit 12; no per-draw synchronization or scissor change is
introduced. The middle arm suppresses primitive kicks, not the road CPU call.
ROADRASTER cold witnesses retain the old `skipped` field name, meaning selected
no-kick bags here. Analyze using `--road-no-kick`, not `--road-submit-off`.

The extracted actual substitution passes a host undefined-behaviour sanitizer
check across all register positions, unchanged controls and invalid-input
rejection; both cache-key sites are checked. This is not a complete packet or
physical raster oracle. The experiment removes primitive setup/raster work,
including reflection views, and changes downstream occlusion and overlap.
Its timing must not be labelled pure fill-rate, or subtracted from an older
different ELF to produce an exact EE time.

PS2 A/B/A completes at 15.496 / 15.487 / 15.434 ms and
48.166 / 48.005 / 48.166 Hz. Contrasts are +0.008/-0.053 ms saved: no useful
improvement. Long entry periods are 176 / 179 / 176. All cold arms accept
3200 road bags / 1213500 input vertices; the middle selects all 3200 for
no-kick while still submitting them. Camera, vehicle, pins and reflection
activity gates pass. Both targets complete 2520 rows and reject twelve bad
controls. PCSX2 is 59.94 Hz throughout (7.487 / 7.535 / 7.839 ms), with final
control drift; not a hardware price. Screenshots show the expected absent
asphalt while other geometry remains, not target GS packet capture proof.

This contrasts with about 3 ms saved by skipping road submissions entirely:
the evidence directs the next investigation upstream of primitive raster,
toward submission/transfer/VU clipping, not road texture bandwidth first.
It does NOT distinguish EE versus VU, prove zero GS work, or establish additive
stage costs. No production change or night no-kick qualification is claimed.
Bound ELF `267c7d626bf2f86f311f6029b85fd23f7ce51a222f4df90278073c176aa83150`
in `F:/Projects/tyrax2-road-nokick-day-20261008-v1` binds 467 source/native
files. All 303 physical launch inputs and original authored manifest hashes
remain unchanged. Evidence is archived under `road-nokick/day`.

## Road VU/output bypass

`tyrax2-road-vu.py` holds XYZF3 no-kick on for roads in ALL arms. The middle
arm selects private uniform mode 3; ordinary texture-colour cull/clip programs
emit the normal TEST/TEX1/TEX0/ALPHA state and a zero-loop primitive tag,
then reach their ordinary continuation point without transforming/clipping
vertices. EE road selection, packaging, cache handling, input uploads and
dispatch commands remain active. Uniform mode is not a baked-body key: body
bytes are identical in both arms, while the retained no-kick flag remains in
both primitive keys. Program restrictions are asserted at uniform submission.

This measures removal of VU vertex work AND most GIF output, not pure VU
arithmetic or an exact EE-only remainder. Both images omit asphalt, unlike
the previous visible/no-kick contrast. Cold ROADVU witnesses require equal
positive actual uniform counts, mode 3 only in the candidate, in addition
to road acceptance, pose and ground-pin gates. The source-level VU simulator
checks 240 unchanged control executions, 120 bounded state-only outputs and
120 corrupted-EOP rejections per patched shader (cull TC, clip TC, clip TC
fold). It does not model native register allocation or hardware timing.
Native symbol ranges total 1972 even-rounded resident microinstructions,
including billboards, below the 2042-word ceiling. The three host shaders
pass all stated checks.

Physical no-kick / VU-bypass / no-kick completes at
15.428 / 15.403 / 15.363 ms, 48.166 / 48.112 / 48.166 Hz, and
176 / 177 / 176 long entry periods. Contrasts are +0.025/-0.040 ms saved:
no useful improvement. Cold actual uniform uploads are 3200 per arm, bypass
uploads 0 / 3200 / 0; accepted road inputs remain 3200 bags / 1213500 vertices.
Both PS2 and PCSX2 finish 2520 samples and reject fourteen malformed controls.
PCSX2 is 59.94 Hz throughout, at 7.507 / 7.540 / 7.509 ms; not a PS2 price.
Screenshots show omitted asphalt in both kinds of arm, without obvious damage
to other geometry. No paired-pixel or physical VU instruction capture is claimed.

Together with no benefit from suppressing primitive kicks and roughly 3 ms
from omitting road submissions, this strongly prioritizes EE-side preparation
and delivery over VU vertex execution or GS raster work in this daylight sweep.
Input DMA/VIF transfer and scheduling costs remain, so do not call the residual
an exact pure-EE instruction bill. Next inspect selected-road Core preparation,
package classification and chain construction with deferred ownership preserved;
do not restart whole-scene profiling or another VU arithmetic rewrite first.
No road fix is promoted, and the conclusion does not qualify night or driving.

Bound ELF `b8af0f4372fa8f3fd14f142acd75c8667085e4c2c24cce0309c308ab29799c54`
in `F:/Projects/tyrax2-road-vu-day-20261008-v1` binds 467 source/native files.
All 303 physical launch inputs and original authored manifests remain unchanged.
Evidence is under `road-vu/day`; owned emulator/host processes were stopped,
and final network reset succeeded with ten-second settle (TV not inspected).

## Original attribution protocol

Follow-up to the road-free straight-drive tests and the 1.173.3 ground-mask fix.
`tools/tyrax2-reflection-turn.py <new-fixture> <source-project>` preserves all
production cache defaults and ground-mask pins, uses the same shiny vehicle
bake, disables devkit profiling and records sparse intervention counters.

Five phases: normal / frozen paint colours / body env pass skipped / frozen
shared capture / normal. Each phase reloads the scene, warms for 1200 loops,
samples 120 parked loops then 720 angular-sweep loops. The driven car remains
on the handbrake, throttle zero. Camera orbit follows three deterministic
0/+60/-60/0 cycles of 240 loops, overriding only the ordinary glance angle
after stick integration. Camera placement/collision and real simulation dt
remain active. This is a frame-indexed workload, not a replay of human input
or a guarantee of equal angular velocity per wall-clock second under slowdown.

CSV `gas` denotes the sweep interval, not vehicle acceleration. Sparse counters
witness actual capture, body draw/skip, colour rebuild and blocked colour
rebuild operations. Frozen capture retains its matching sampling basis;
frozen paint intentionally retains old colours, while texture basis updates
and the body draw remain active. These are attribution interventions, not
proposed visual-quality compromises. Per-frame clocks/fences are not added
inside reflection code. Elapsed minus pacing is not pure EE time.

Require completed phases, identical camera paths within tolerance, a stationary
undamaged car, active paint rebuilds in normal arms, and verified intervention
gates before drawing conclusions. Emulator timings are not PS2 prices.

Optional `--scatter-copy` (generator and analyzer) uses three phases 0/5/0:
ordinary colour assignment / same colour bytes via memcpy / ordinary assignment.
It changes only the final palette-to-vertex scatter, not the paint formula,
normal deduplication, version stamps or update gate. This private candidate
must earn a repeatable physical benefit before any production change.

## Physical attribution result

Fixture `F:/Projects/tyrax2-reflection-turn-20261008-v1`, bound ELF SHA-256
`19231ed62f02a6a32a130d5114bd6c3599c122c84abb51146a0dfdbdbe96d86e`.
467 native files agree with sources. Emulator and PS2 complete all five phases
(4200 sampled loops each). Six parser negative controls are rejected.
Camera positions/look targets match across phases within 0.02 world units;
the car stays stationary and undamaged. All phases retain 18 ground masks.

| Mode | Mean non-pacing ms | Loop Hz | p95 ms | Max ms | Entry periods >25 ms |
|---|---:|---:|---:|---:|---:|
| Normal before | 13.835 | 53.48 | 17.328 | 20.801 | 87 |
| Frozen paint colours | 11.891 | 59.94 | 15.241 | 16.646 | 0 |
| Body env pass off | 11.615 | 59.94 | 14.949 | 15.756 | 0 |
| Frozen shared capture | 12.923 | 59.77 | 13.903 | 17.202 | 2 |
| Normal after | 13.878 | 53.15 | 17.385 | 20.853 | 92 |

Normal and capture-frozen arms perform 568 paint rebuilds; the frozen-paint
arm performs zero and blocks 1404 attempts. Body-on arms submit 1440 passes;
body-off submits zero and records 1440 skips. Normal captures are 174,
frozen-paint 179, body-off 174, capture-frozen zero (342 blocked attempts).
Thus capture counts are not identical across every intervention; contrasts
include scheduling/cache consequences, not just isolated function time.

Frozen paint saves 1.944-1.987 ms versus bracketing controls. Removing the whole
body pass saves 2.220-2.262 ms; freezing capture saves 0.911-0.954 ms. Do not
sum these as independent EE/VU/GS costs. Frozen paint intentionally changes
appearance and is NOT a production fix. This narrows the next optimization to
colour generation/scatter plus downstream mutable-stream consequences.

In the first normal phase, 86 of 87 over-budget work intervals also capture a
reflection (61 also rebuild paint, 25 do not). The two costs can jointly cross
the frame budget; this is not evidence that capture is free or that all stalls
come directly from EE paint arithmetic. The emulator stays at 59.94 Hz in all
arms, so it cannot reproduce the physical performance limit.

## Scatter candidate rejected

Fixture `F:/Projects/tyrax2-reflection-scatter-20261008-v1`, bound ELF
`501dc0de93dd148f6acfe86b71ff2e1ad0755116059c1da92c7809099129ece6`.
Both targets complete 2520 rows, native binding covers 467 files, and three
malformed-input controls fail as required. Normal / memcpy / normal on PS2:

| Metric | Before | Candidate | After |
|---|---:|---:|---:|
| Mean non-pacing ms | 13.898 | 13.879 | 13.861 |
| Loop Hz | 52.95 | 55.12 | 53.21 |
| Max non-pacing ms | 20.868 | 21.118 | 20.908 |
| Captures | 174 | 179 | 174 |
| Paint rebuilds | 568 | 568 | 568 |

Mean savings are +0.019 / -0.017 ms against the bracketing controls: no repeatable
mean benefit. Presentation cadence improves, but capture scheduling differs and
the maximum does not improve. Do not call that a proven copy optimization.
Emulator mean regresses 0.015-0.030 ms. The candidate remains private, with no
production colour-copy or quality change.

The architectural candidate selected after scatter was VU1 evaluation of the
existing paint formula from normals and view/half-vector parameters (the
private implementation and both physical orders are reported above). It avoids EE colour
rewrites and mutable payload invalidations, but must cover cull/strip/clip
paths, compute vertex colours before clipping interpolation, preserve ordinary
reflective-material behavior, and qualify numerical/image equivalence and
physical cost. The scatter run alone did not establish a VU speedup.
Preserve the existing update gate by retaining the accepted paint basis per
part/LOD on EE; evaluating against a fresh camera basis every frame would
silently change the current hysteresis behavior. The reflection sampling basis
and the accepted paint basis are distinct, especially for a retained capture.

Evidence archive includes the
attribution, scatter and VU trials, logs/CSVs, source/ELF identities, parser controls and owned
emulator captures. No user project files or production renderer paths changed.

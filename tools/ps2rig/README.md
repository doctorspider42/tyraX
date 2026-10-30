# PS2 timing rig

The physical-PS2 A/B rig every measurement of the 2026-10 TyraX2 work was made
with (night EE notes, the [architecture audit](../../docs/tyrax2-architecture-audit-2026-10-20.md)).
It copies a generated project and the engine to a scratch root, injects a
frame probe, builds natively, deploys over ps2link and summarises the console
log. One ELF carries several **arms** (variants switched at runtime, one per
cycle), so A and B are interleaved on the same binary and boot.

PCSX2 proves correctness only; timing claims come from the console.

## Collision stress fixture

`collision_stress.py` creates the same-ELF collision algorithm comparison used
in the [physical PS2 results](../../docs/collision-stress-results.md).
`collision_bench.hpp` records COP0 frame periods, EE work, physics time, awake
bodies, contact truncation, car-push candidate overflow and actual car motion.
`collision_analyze.py --complete` validates all 28 phases and reports pooled
and separate-repeat statistics. The baseline is restored from the recorded
Git commit into a shared candidate runtime; it is not a stock-main binary.

Use a fresh task directory under the **primary checkout's ignored `.tmp/`**.
For example, from the collision worktree in PowerShell:

```powershell
$collisionRoot = 'F:/Projects/tyra-editor/.tmp/tests/collision-stress'
$collisionBuild = 'F:/Projects/tyra-editor/.tmp/builds/collision-stress'
$env:TYRAX_NATIVE_BUILD_ROOT = "$collisionBuild/native"
python tools/ps2rig/collision_stress.py prepare --scratch $collisionRoot
build/tyrax-editor.exe --build "$collisionRoot/district"
python tools/ps2rig/collision_stress.py instrument --scratch $collisionRoot
tools/toolchain/native-build.ps1 -Project "$collisionRoot/district" `
  -Engine "$collisionRoot/engine" -Cache "$collisionBuild/cache" `
  -Toolchain "$env:LOCALAPPDATA/tyra-editor/toolchain/ps2dev"
```

The editor must come from the candidate source head. The full first build
materializes assets; the second build compiles the instrumented engine and
game. **Do not regenerate after instrumentation**: it overwrites the hooks.
Before deploying, verify every deployed baked PNG/TMDL against `.res-baked`,
record the ELF hash, and inspect an isolated PCSX2 smoke run. On Linux use the
paired `tools/toolchain/native-build.sh PROJECT ENGINE_SOURCE CACHE_ROOT PS2DEV_ROOT 0`
instead of the PowerShell bridge (all four paths absolute).

Follow the real-PS2 ownership and first-boot rules in `tyra-testing`. Manual
deployment must write and verify `ps2link` in the absolute `district/bin/ps2link.run`
before `execee`. Run the primary checkout's already firewall-qualified
`tools/ps2client/bin/ps2client.exe -h <ip> execee host:vehicle-playground.elf -ps2link`
from that `bin/`, serving `host:` until `CB DONE phases=28`. Logs belong under
the primary `.tmp/artifacts/`. Never interrupt a run with another ps2client.
Then stop only that owned server, reset once, and summarize:

```powershell
python tools/ps2rig/collision_analyze.py "$collisionRoot/district/bin" `
  --complete --out 'F:/Projects/tyra-editor/.tmp/artifacts/collision-stress.json'
```

Retain the report/raw evidence needed for review, then remove scratch projects,
builds and captures when no longer used. `results/collision-2026-10-29.csv.gz`
contains the measured frame CSV; the adjacent JSON includes source/ELF hashes,
pooled statistics and individual repeats. The zero-box first phase has extra
boot settling time; dense trajectories differ across arms. No mesh-mode BVH
performance is measured by this fixture.

## Files

| File | What it is |
| --- | --- |
| `cld_probe.hpp` | The probe, copied into the engine as `inc/debug/cld_probe.hpp`. Drives the cycle (boot, 600 parked frames, a full-throttle drive, scene reset), sets `Cld::arm = cycle % CLD_ARMS`, and prints per window `CLD win arm= cyc= st= ... work= p50= p95= late= period=` plus COP0 scopes (`CLDS`), PMU counters and route counts. |
| `cld_shot.hpp` | One VRAM frame grab per early cycle to `host:shot_c<cycle>_a<arm>.tga`. |
| `make_arm.py` | Instruments a copied project + engine (anchored edits; quiet `TYRA_LOG`, probe scopes). `--lean` = loop statistics only (headline numbers); `--arms=N`; `--day`; many diagnostic flags (`--gputl`, `--bagcensus`, `--shot`, `...ab` arm sets) implemented by the sibling modules. |
| `make_engine_probe.py` | The engine half of `make_arm.py` only (probe in `Engine::realLoop`, quiet log, scopes, flags), so any generated project can be measured as authored: `MAKE_ARM=make_engine_probe.py bash tools/ps2rig/arm.sh S1 --lean`. The cycle states are then just frame counters; the windows sample whatever the project does from boot. |
| `gputl.py` and the other `*.py` modules | Optional probe extensions imported by `make_arm.py` flags; `gputl` is the whole-frame VU1/GIF/VIF1 sampler (`CLDGT`). |
| `arm.sh` | `arm.sh <name> [make_arm flags]`: copy, instrument, native-build one arm into `$RIG_ROOT/vpn<name>` / `tyra<name>`. |
| `deploy.sh` | `deploy.sh <projectdir> <log> [noreset]`: deploy to the PS2 and serve `host:`; run as a background task. |
| `emu.sh` | Headless PCSX2 under WSL/Xvfb for counts and correctness (`PCSX2_APPRUN` required). |
| `cldse.py` | Median work per (state, window, arm) with 95% bootstrap CIs. |
| `cldwin.py`, `cld_sum.py` | Window medians / means incl. p50, late and named scopes. |
| `imgdiff.py`, `tga2png.py` | Compare probe shots; convert TGA. |

## Running an A/B

1. Generate a base project once (`tyrax-editor --build <example>`), copy it to
   `$RIG_ROOT/vpn` (default `RIG_ROOT=/c/tx`), set its `buildProfile` to
   release and force the scene state you want (e.g. night in
   `district_mood.cpp`).
2. Put the variants behind `Cld::arm` in the engine/game copy (an arm patch, or a
   `make_arm.py` flag) and build: `bash tools/ps2rig/arm.sh N1 --lean --arms=3`.
3. Take the console (`mkdir C:\tx\ps2-console.lock.d` when several sessions
   share it), power-cycle, then `bash tools/ps2rig/deploy.sh /c/tx/vpnN1 /c/tx/runN1.log noreset`
   as a background task; stop it after enough cycles (~10 per arm).
4. `python tools/ps2rig/cldse.py /c/tx/runN1.log 1` (skip cycle 0, the warm-up).
   Decide on whole-frame work, never on a producer scope.

## Traps

- **Wedges:** a ps2link reset after a long run regularly ends in
  `freepad: DMA Busy`; plan one power cycle per long run and deploy `noreset`.
- **Never kill `ps2client` by name** - other sessions may own one; stop the task.
- **Never reuse an arm name**; a stale log fools the summaries.
- `make_arm.py` edits by exact anchors in generated and engine code. When an
  anchor moved, it stops with `anchor count 0!=1` - update the anchor, never
  loosen it.
- Instrumented builds (scopes + PMU) add ~2.5 ms a frame; quote `--lean` numbers.
- `--pgo-*` assumes the WSL native-build cache prefix in `PGO_PREFIX`.


For an equal-completeness optimization comparison, use `--baseline cc1f8895b
--uncapped-baseline` when instrumenting. This retains the earlier branch's
SAT/CCD and broad-phase algorithms but removes its contact cap. Both arms
share the current unbounded car-prop candidate list. New CSV columns compare
`generated_contacts` with solver `contacts` and `push_candidates` with
`push_tested`; `--complete` rejects candidate omissions. Historical CSVs without
these columns remain readable. Do not present this as a stock-old-branch A/B.

When analyzing a `cc1f8895b --uncapped-baseline` run, pass
`--baseline-label prior-uncapped` so the summary does not call that arm main.

The diagnostic, first-bounds and final-projection 2026-10-29 follow-ups are in
`results/collision-{diagnosis,bounds,projection}-2026-10-29.{json,csv.gz}`.
The final run validates all 16,800 frames in both arms for complete contact/prop
accounting. It improves pile FPS by 6-25% against uncapped prior algorithms;
it does not qualify dense 60 FPS. See the linked results page for frame misses,
per-repeat spread, shared-layout and trajectory limitations.

## Prepared contact solver A/B/C

Pass `--solver-comparison --baseline 2ed7cb6f7 --uncapped-baseline` to prepare
and instrument. This selects 42 phases: the previous solver, prepared scalar
and prepared VU0, seven workloads and reversed order on the second repeat.
The startup `contact_solver_probe.hpp` compares the three implementations on
64 coupled contact graphs, writes `solver-check.csv` and aborts timing on an
error. The fixture includes no warm starting or reduced physics fidelity.

Analyze with `--solver-comparison --complete --baseline-label pre-prepared`.
The solver arm is stored in every CSV row; each of the 42 phases must have
600 measured frames. Prepared contacts must retain all generated points and
all vehicle candidates. The prior solver shares scene/step layout and assets;
its integration order and 6+3 passes are preserved.

The lean comparison additionally records `solver_ticks`: contact preparation,
packing, all 6+3 passes and copying solved velocities back, excluding detection.
The analyzer reports solver mean/p95 and aggregate microseconds per contact;
contact populations/trajectories can still differ between arms. Malformed CSV
rows or solver scopes exceeding whole physics time are rejected.

The prepared solver is opt-in in stock code generation. The comparison rig
forces `TYRA_PHYSICS_PREPARED=1` only for its candidate translation unit and
selects scalar/VU0 per arm; it must not silently benchmark the prior default
three times. The final lazy/direct response report is linked from
[the solver experiment](../../docs/prepared-contact-solver.md).

## Temporal physics comparison

Use --temporal-comparison --baseline ef9597bda --uncapped-baseline instead of
the prepared-solver comparison. This keeps the previous scalar solver and
compares prior / geometry cache / cache + warm starting / added islands, 56 phases
with reversed order on repeat. Analyze with --temporal-comparison --complete.
The CSV adds prediction/world/body/write-back stages, nested grid/query/SAT,
warm match/island counts, summed squared velocities and maximum contact depth.
Energy and penetration are stability diagnostics; driven scenes inject energy,
and per-frame velocity units change with measured dt. Do not call them a
mass-weighted energy conservation proof or add nested scopes to their parents.

The additional --settle-check mode records eight 64-box stationary phases
(four arms in both orders), 1,260 rows each: park 600, known base impulse,
wake 60, recover 600 after 150 warm frames. Analyze with --settle-check
--complete. Copy all baked resources including reflpano/*.bin; a generic
*.bin exclusion silently replaces reflections and invalidates performance.

Use --temporal-arms 3 to analyze/replay the first three-arm prototype.

Temporal fixtures use synthetic input only: their private Pad init/update/
rumble paths avoid physical padman DMA/RPC. All arms share this setting. The
production engine Pad remains unchanged; do not compare these whole-frame
numbers directly with a differently configured physical-input fixture.

For a focused refined-cache comparison use --temporal-comparison
--temporal-arms 2 on prepare/instrument, and --temporal-comparison
--temporal-arms 2 --complete on analyze. This has 28 phases and compiles the
candidate's cache on and warm/islands off, matching its default code path.
CSV body_candidates/body_bounds_rejects count broad visits and subsequent
conservative envelope rejection, never contact omissions.


## VU0 SAT/CCD comparison

Use collision_stress.py with --micro-comparison --baseline 0647f5596.
This selects two arms and 28 phases, comparing the current scalar pipeline
with asynchronous dynamic OBB SAT/CCD, retaining cache defaults and synthetic
pad isolation. This is an experimental backend, disabled in production.

The micro fixture runs tools/ps2rig/vu0_sat_probe.hpp before sampling.
Create bin/sat-verify.cfg for shadow validation; remove it for timing the
SAME ELF. Preserve vu0-phase-*.csv with collision-phase-*.csv. Busy query
samples are overlap witnesses, not VU utilization percentages.

### Interpenetration / apparent hang diagnostic

After instrumenting a fresh private `--micro-comparison` fixture, run
`python tools/ps2rig/overlap_diagnostic.py --scratch <fixture>` before the native
build. This selects 12 phases: 64/128 cubes, spread / 0.4-unit grid spacing /
identical centres, scalar and micro shadow, 60 physics calls each starting at
frame zero. All original contacts and solver passes remain enabled.

The diagnostic clears/samples EE FCR31 and VU0 STATUS around each physics call.
Intentional EE `1/0` and `0/0` must prove the monitor before interpreting results.
EE SI/SD/SO bits 6/5/4 and VU0 IS/DS/OS bits 10/11/9 detect invalid operations,
division by zero and overflow. Underflow of tiny velocities is recorded separately.
Finite positions alone cannot exclude a PS2 arithmetic error: EE division by
zero saturates to a finite value. See the [EE manual, chapter 8](https://docs.alexrp.com/mips/ee.pdf)
and [VU manual](https://www.manuallib.com/download/VU-USER-MANUAL.PDF).

Validate with `overlap_analyze.py <fixture>/district/bin --log <hardware.log>
--output <report.json>`. Per-call logging affects subsequent measured dt and the
micro arm includes scalar shadow checks; **these FPS/micro costs are not timing
comparisons**. Preserve both CSV families and the log. The
[physical report](results/collision-overlap-2026-10-29.json) and
[full log](results/collision-overlap-2026-10-29.log.gz) cover all 720 calls.

The completed default-off backend qualification retains
[16,800 timing frames](results/vu0-micro-2026-10-29.json) and a separate
[101,641-job shadow run](results/vu0-shadow-2026-10-29.json) of the same ELF.
No hard shadow failures or capacity omissions occurred. The driving-phase
64-cube pile improves from 33.25 to 37.14 FPS, while the 128-cube pile is
19.92 versus 19.72 FPS. Different contact populations/variable dt prevent
attributing all whole-scene changes to kernel speed. Keep micro default-off;
the full table and limitations are in docs/temporal-physics.md.

For the stationary qualification use `--micro-comparison --settle-check` when
preparing/instrumenting, retain sat-verify.cfg, and pass `--mode verify
--settle-check` to vu0_sat_analyze.py. All four phases must have 1260 measured
frames; completion-wait denominators include 1410 simulated frames per phase
plus the initial 300-frame warm-up. The
[physical settle report](results/vu0-settle-2026-10-29.json) retains all 5040 rows.

For an isolated fixed-input backend comparison add `--micro-stream` to prepare
and instrument. Startup validates mixed and coincident-centre corpora outside
measurement, then writes `vu0-stream.csv`: mode0 scalar,1 previous micro schedule,
2 chained schedule,3 packing-only,4 chained with prepacked jobs. Each timed
sample processes 1792 pairs, eight repeats in alternating order. Micro modes
1/2/4 must produce identical result hashes; scalar uses the same geometry
oracle but need not have bit-identical contacts. No file I/O, solver or reference
comparison runs inside timed samples. This isolates backend costs; it does not
predict whole-scene FPS or replace the full-scene shadow qualification.

Stream CSV confirmation counters identify canonical EE work repeated after VU0:
confirmed_ccd, confirmed_tie and confirmed_parallel. The distinct near-minimum
startup case must print VUSATTIE failures=0; its guard-disabled host falsifier
must fail. Both assemblers fit the guarded kernel in 4KB (openvcl2416B/Sony2480B).
See results/vu0-chain-2026-10-29.json and its separate full-scene shadow report.
The initial scheduling run has an explicitly rejected point-tolerance gate;
never silently relax tolerances or count repaired results as a passing trial.

For a same-ELF kernel comparison add `--micro-kernel-baseline <commit>` to
prepare/instrument (implies --micro-stream), or install it into an existing
private stream fixture with vu0_stream_reference.py. The helper checks the
32-qword/seven-job layout and installs the reference image/selection only into
the private engine. Mode5 is reference-kernel/chained. Program upload precedes
timing. Both kernels validate all three corpora, including offset overlaps.

The expanded probe writes/replays vu0-stream-input-{0,1,2}.bin with a versioned
header and all Case bytes. A partial corpus set is rejected. Preserve these
files beside vu0-stream.csv: the analyzer checks each payload hash against all
rows and records SHA-256. Without a reference image there are120 rows; with it,
144. Existing80-row reports remain supported. Measurement CSVs are pinned to LF
so their SHA-256 stays stable across checkouts.

The qualified winning-axis experiment is archived in
results/vu0-minimum-2026-10-29.inputs.zip. Extract under the primary .tmp/ and
run vu0_stream_analyze.py on vu0-stream.csv with --log hardware.log. The package
also contains both full-scene phase CSV families and metadata for
vu0_sat_analyze.py --mode verify. A changed input byte must be rejected by the
payload-hash gate. This kernel assembles to2496B/openvcl and2560B/Sony VCL.

Common-motion qualification additionally requires VUSATCOMMON cases=4 failures=0
and the old-kernel work-reduction falsifier. The stream CSV reports
common_motion_jobs; mode5 reference probes compare live instruction memory to
the selected image before validating each corpus. These checks and image uploads
remain outside timings. Replay the captured Case files to preserve exact inputs.

All private micro fixtures now record the first hard failure into fixed RAM and
flush vu0-sat-failure.bin only after the micro queue drains. --micro-precision
adds a50000-case near-touching corpus and replay of a saved packet; install into
an existing fixture with vu0_capture_fixture.py --precision. The captured
1232-byte Dump includes exact Case/Job/VU0 output and the EE reference. Run the
host oracle with a third argument naming this packet: it tests both simulated
output and the actual recorded GPU result against the saved EE contact.

The2026-10-30 capture demonstrates a hardware/source-simulator disagreement;
recorded-result regression must fail on the old consumer and pass the certified
negative bridge. Vu0SatCapture::flush must stay outside active micro execution.
Qualification rejects VUSATDIFF anywhere in the complete log, even after CB DONE.
Additional vu0-class-<phase>.csv files count conservative EE negative proofs,
full negative confirmations and the original confirmation categories.

The certified-negative qualification is archived in results/vu0-negative-proof-2026-10-30.inputs.zip.
It contains the saved hardware packet, unchanged Case inputs, stream CSV, both
scene phase families, classification CSVs, metadata and the complete log. With
metadata.certified_negatives the scene analyzer validates classification totals
against jobs and hashes the additional sidecars. The precision gate is50,000
cases plus accepted replay of the recorded hardware failure.

The body-grid experiment uses body_grid_fixture.py --scratch <existing-micro-fixture>.
It captures actual body boxes and awake-body query boxes at phase13 frames0,150,300,600,
after all micro packets drain. Each fixed snapshot compares8/4/2/1m cells in
eight reversing-order samples, with an independent sorted full-walk eligibility
oracle outside timing. body_grid_analyze.py validates binary input hashes and
reports fresh-build plus query-pass times. Fresh allocation differs from the live
reused-grid rebuild; this is a screening benchmark, not whole-scene FPS. The probes
perturb the live simulation; never qualify that run's scene timing.

BroadPhase accepts an optional minimum grid-cell size (default8m, unchanged);
its moving index inherits it. Invalid/sub-millimetre values fall back to8m.
The64-side limit still increases cells for large scenes, wide-object fallback
and the original inclusive float tests remain intact. No production caller has
changed its cell size on the basis of the screening test alone.

Replay the captured snapshot files with tools/verify-body-grid-snapshot.cpp
linked with engine/src/physics/broad_phase.cpp. The first physical screening
package is results/body-grid-screen-2026-10-30.inputs.zip (1136 query/cell
combinations also replayed on the host). Use --timing-cell 2 on the same fixture
to remove probes and compare8m/tuned body grids with canonical EE physics in
both arms. It removes only that private fixture's sat-verify.cfg to enable28
phases. Rebuild before launch, preserve screening files first, and validate the
full16,800 rows with body_grid_timing_analyze.py plus matching log/metadata.
No VU0 dispatch or snapshot work may appear in the measured scene run.

When variable frame times produce different contact-density trajectories, keep
the normal-clock result and add a separate --fixed-step body_grid_fixture.py
run. It pins g_frameDt/g_frameScale only in the private benchmark after benchTick;
CP0 timing stays real and production codegen is unchanged. Metadata and the
FIXEDSTEP marker distinguish controlled-workload throughput from real-clock FPS.
Do not discard an unfavorable normal-clock repeat because fixed-step looks better.

The next private experiment, world_grid_fixture.py --verify, requires the2m
scalar body-grid fixture. It builds an additional world index from current
BroadPhaseCache boxes, excluding physics bodies while retaining nonphysics moving
solids. Both arms use the same body-grid size (--body-cell 8 by default, retaining production policy). Each world query is checked against an
independent filtered full walk; world-grid-<phase>.csv records queries, verified
queries and errors. The same scalar timing analyzer handles its metadata and
12/13 verification phases or all28 timing phases. --verify selects the fixture's
sat-verify.cfg admission gate; absent --verify switches the same ELF to timing.
The host category-transition oracle is tools/verify-world-grid.cpp.

The fixed-step analyzer compares per-frame contact/awake/candidate counters and
logged trajectory fields at printed resolution across paired arms. A mismatch
writes qualified=false with the first differing frames, then exits with failure;
it must not silently bless a changed workload. The first run catches the300-frame
initial warmup asymmetry in75 zero-added pairs and is archived as rejected.

--shared-physics --dense-only on an existing nominal-step body_grid_fixture.py
rig removes separately compiled physics/query paths from both arms. One function
selects8/2m cells only at phase boundaries. It balances initial warmup and measures
phases10/11/12/13/24/25/26/27 (4800 rows): freeze only those expected phases from
the live folder, which can still contain older CSVs. The analyzer checks the
metadata.expected_phases subset and retains strict workload parity. The world
prototype supports this shared-function rig and fixes body grids at the selected identical size in both
arms while switching only world admission. Physical qualification is pending.

The shared-function fixed-step control is qualified in
results/body-grid-shared-2026-10-30.inputs.zip:4800 frames, four workload pairs,
2400 matched frame pairs at printed telemetry resolution, zero parity/accounting
errors and zero micro dispatch. The2m grid saves roughly0.26-0.33ms total
physics in the128-pile controlled workload; this does not replace normal-clock
FPS qualification. world_grid_fixture.py defaults to --body-cell 8 in both arms
to measure world admission independently of this small grid effect.

The world-index admission package is results/world-grid-verify-2026-10-30.inputs.zip:
1200 frame rows,48,606 independently checked world queries, zero admission/parity
errors and no micro dispatch. It includes both phase12/13 frame/sidecar families,
world metrics, metadata and the full log. Re-run body_grid_timing_analyze.py on
the extracted package. Remove the private cfg with world_grid_fixture.py without
--verify to time the exact same ELF; preserve its hash and disable oracle work.

The qualified world-index fixed-step timing package is
results/world-grid-fixed-2026-10-30.inputs.zip (4800 frame rows, strict paired
telemetry parity, no oracle work/micro dispatch in timings). To follow up with
production-clock workloads, use world_grid_fixture.py --normal-clock --full-suite
--verify, rebuild, and repeat admission on that new ELF before removing the cfg
and measuring all28 phases on the same binary. These options restore the
original clock and full schedule only in the private fixture; keep initial
warmup balanced and record the new ELF/source hashes.

The normal-clock world-index gate is archived in
results/world-grid-normal-verify-2026-10-30.inputs.zip:1200 frame rows and45,328
independently checked world queries, no admission/accounting failures. Its ELF
hash is reused for the separate full28-phase normal-clock timing run.

The complete eager world-index production-clock comparison is archived in
results/world-grid-normal-2026-10-30.inputs.zip (16,800 measured frames). Dense
128-box repeats improved from18.32/18.49 to22.52/22.06FPS; lightweight scenes
paid roughly0.1ms extra physics. Variable-step trajectories differ, so these
whole-frame gains do not isolate query cost. A minimum16-awake private prototype
removed most sparse overhead but produced mixed128-pile results; retain
results/world-grid-adaptive-2026-10-30.inputs.zip and its admission package.
Neither result qualifies a universal default or dense-pile60FPS.

world_cache_fixture.py installs the canonical cached-static/compact-moving
WorldBroadPhase in the existing shared-function world rig. --verify selects
phase12/13 full-walk admission; omit it only after qualification, then time the
same ELF with the original28-phase production-clock schedule. Its first gate,
results/world-cache-verify-2026-10-30.inputs.zip, contains48,502 independent
query checks with zero errors and only five index builds. Those instrumented
frame times are not performance evidence. The production switch
TYRA_PHYSICS_WORLD_INDEX is now1 after the following timing qualification.
Build the host oracle with both broad_phase.cpp and world_broad_phase.cpp;
it exercises cached statics, moving/category transitions, asset-bound changes,
more than eight movers, clear, global ordering and grid/octree backends.
codegen_member.py provides the shared source-method extractor without importing
the fixture CLI. World-grid sidecars retain build counts as well as admission
checks; original combined-index fallback can contain physics candidates before
the existing exact filter, which the independent oracle handles explicitly.


results/world-cache-normal-2026-10-30.inputs.zip retains the complete same-ELF
16,800-frame normal-clock run after the cached admission gate. Both128-pile
repeats improve19.05->20.19 and20.04->22.67FPS;64-pile world work roughly halves.
Sparse overhead is absent, while32-pile FPS is mixed within the small observed
delta. Keep all repeats and the variable-step trajectory caveat.

physics_pmu_fixture.py --scratch <cached-world-rig> installs a separate private
12/13 dense profile. It rotates PCR event pairs each frame:6/6 I$/D$ misses,
2/2 single/dual issue,12/14 completed/COP1 instructions. Only outer physics,
solver and stage scopes read PMU counters; frequent query/SAT scopes remain
CP0-only. The probe refuses an enabled external PMU owner, preserves disabled
control/count state, checks overflow, calibrates CPU-cycle counters against CP0
Count, and compares warm4KiB to128KiB cache scans before any profile frames.
Selectors and timer cadence follow the [SCE EE Core User's Manual v6](https://docs.alexrp.com/mips/ee.pdf),
sections3.2 and7. Counter events are diagnostic counts, not unit utilization.
The profiled ELF changes instrumentation and cannot qualify production FPS.


physics_pmu_analyze.py validates the physical calibration,600 frames per arm,
three balanced event modes, positive outer scopes, complete collision/push
accounting and zero micro dispatch. The first retained profile is
results/physics-pmu-2026-10-30.inputs.zip. It is diagnostic evidence only;
performance_qualified staysfalse even when all counter checks pass.
The selected-default non-box smoke is results/world-default-smoke-2026-10-30.*:
37 bodies,79 finite aggregate samples through frame7900, private Pad stub.


solver_layout_fixture.py --scratch <pmu-rig> adds a fixed-input storage screen
at phase12/13 frames300/600. It extracts the canonical scalar velocity/split
loops, retaining six/three passes, and instantiates them for the original
880-byte RbStep or96-byte compact velocity/mass/inertia fields. Mode1 includes
gather/scatter; input resets are outside timing and mode order alternates.
The live solver remains unchanged. Raw prepared inputs are saved before replay;
its output is checked against both replay results after the live solve.
--refresh-probe updates only this screen header before the separate SPR stage.

The initial and expanded-delta packages are
results/solver-layout-{initial,delta}-2026-10-30.inputs.zip. Compact heap replay
saves roughly0-4% in these snapshots, but both are deliberately unqualified:
separate full/compact calls agree bitwise while the caller-inline solver differs
by up to2.98e-8 in the expanded gate. Do not infer a production regression or
promote the candidate from a matching pair of replay functions. The initial
package also contains the generated host replay source and10,000-graph/four
physical-snapshot bitwise verification metadata. Solver math is extracted from
current canonical source; it is not the prepared-coefficient solver experiment.
solver_layout_analyze.py retains costs with qualified=false and exits1 on
integration differences, while preserving all contact/push accounting checks.
These replay frames are perturbed and cannot qualify whole-scene FPS.

solver_scratchpad_fixture.py adds mode2 to the private screen. It calls the
same CompactBody solver instantiation on EE SPR, checks stack and both SPR DMA
channels, falls back to heap when busy or above170 bodies, and saves/restores
and verifies all16KiB before returning to I/O/rendering. Saving, restoring, verifying, gathering and scattering are
inside mode2 timing; eligibility checks precede the bracket. No contacts or bodies are dropped. In-tree packet owners
use normal RAM; this private experiment is not a production SPR ownership API.
The mapping and access model follow the
[SCE EE Core User's Manual](https://docs.alexrp.com/mips/ee.pdf) section6.4 and
[PS2SDK packet implementation](https://github.com/ps2dev/ps2sdk/blob/master/ee/packet/src/packet.c).


The first SPR package is results/solver-scratchpad-2026-10-30.inputs.zip.
All16 mode2 repeats actually used SPR and restored its contents without errors.
The four first-repeat snapshot output checks match compact-heap results bitwise. In the
two denser snapshots SPR saves0.27/0.47ms (4.36/6.68%) against the extracted
full-stride replay including maintenance. The two lighter snapshots regress
by0.25/0.31ms; do not pool these into a universal speedup. Caller-inline differences reach5.96e-8 in this screen, so
qualified/performance_qualified remainfalse and production
solver code is unchanged. Any integration needs a common scalar kernel,
amortization/fallback qualification, explicit SPR ownership and full scene A/B.


plane_sparse_fixture.py follows the completed private solver replay rig. It
removes replay/PMU instrumentation, restores the full28-phase production-clock
schedule, and compares generic plane dot products with exact signed-axis
arithmetic. Both arms retain the selected cached-world admission and8m body
grid. A fourth RbStep bool uses existing padding (880-byte size retained).
Classification requires exactly the generated box normals/order; zeros,
subnormals and nonfinite point coordinates retain the full generic path.
All six stored plane offsets and strict face/depth tie rules are preserved.

--verify shadows every eligible point, compares face/depth bits, and always
returns the original answer. The host tools/verify-plane-sparse.cpp checks
10,000,545 cases (9,884,203 eligible), including boundaries and exceptional
fallback; a broken first-plane sign is rejected. The physical same-ELF gate is
results/plane-sparse-verify-2026-10-30.inputs.zip:1,050,837 point comparisons and
56,057 independent world-query checks with zero errors. Omit --verify only
after this gate, then measure the same ELF with no plane/world oracle work.
plane_sparse_analyze.py checks all sidecars and collision/push accounting;
plane-timing counters must stayzero. The common cached-world baseline can
legitimately build its index, so the general timing analyzer permits baseline
builds only for explicitly tagged plane experiments. Production retains generic planes after the subsequent controlled-workload
comparison fails to establish a direct compute benefit.


The complete normal-clock plane package is plane-sparse-normal-2026-10-30.*,
with16,800 rows. It improves dense128 FPS while changing contact-density
trajectories. For a separate controlled run, apply body_grid_fixture.py
--fixed-step, then plane_sparse_fixture.py --verify --dense-only, rebuild,
qualify that new ELF, and toggle only its cfg to time phases10/11/12/13/24/25/26/27.
The physical nominal-step gate checks724,457 points without errors. The fixed
package has4800 rows and2400 matched logged frame pairs; mean drive physics
differs by at most0.018ms across four workload pairs. Both timing reports mark
optimization_selected=false. This rejects production integration, not the
correctness of the exact arithmetic. The original generic plane code remains.


body_sweep_fixture.py follows the rejected sparse-plane experiment. It restores
generic planes in both arms and switches only the transient body index:8m grid
versus a sorted32-byte row list with prefix maxima. Two binary searches narrow
each inclusive query; exact full AABB checks and original ascending IDs remain.
Invalid bounds use conservative fallback. Storage is linear; no all-pairs table
or pair/contact cap is added. World admission/solver/clock remain unchanged.
The host tools/verify-body-sweep.cpp passes120,000 independent full-walk queries.
The physical gate results/body-sweep-verify-2026-10-30.inputs.zip passes48,606
queries plus two independently replayed EE input snapshots, with exact logged
workload parity and complete contact accounting. body_sweep_analyze.py validates
those snapshots independently from the index. The same-ELF nominal-step timing
package is results/body-sweep-fixed-2026-10-30.*:4800 rows,2400 matched logged
frame pairs. Mean drive physics changes9.680->9.380 and9.695->9.397ms for64
props;18.508->17.755 and18.534->17.742ms for128. Both directions save query
time. This is controlled throughput, not production-clock FPS; promotion is
pending a normal-clock scene comparison.

ee_pipeline_audit.py inventories raw EE COP1/COP2 instructions, direct calls,
cross-function tail branches and unresolved indirect calls in a symbol-bearing
linked ELF. --wsl runs the specified --objdump under WSL; --tool-elf supplies
its Linux path. --match is repeatable and selects closure seeds. The parser
was falsified with injected COP2 words and checked against direct/tail/indirect
edges. Static results do not prove runtime ownership or utilization. The
results/ee-world-audit-2026-10-30.json inventory uses a relink whose stripped
bytes exactly match the timed ELF. Selected world helpers, including mesh
queries, geometry/SAT helpers and world admission, have no direct/transitive
COP2 in the inspected closure. Portal bodies have no authored portal workload
in this fixture. The whole physics closure does reach COP2 through later
object-matrix/geometry updates; preserve the completion fence before those
calls. Library indirect calls, exceptions and interrupts require separate
review. This evidence supports a private world/body interleaving experiment,
not enabling asynchronous physics in production.

world_overlap_fixture.py installs a private schedule comparison over the
fixed-step body-index fixture. Both arms restore the same8m body grid and use
the same chained micro SAT/CCD with canonical confirmations. The candidate
defers world-contact work into whole-awake-body chunks after packet launch;
the control completes world work first. Both stage contacts into separate
vectors and merge world before body, preserving solver order and6+3 passes.
The prototype changes no predicted pose during contact generation; sleepers
woken by pairs remain immovable until the next frame. Geometry updates stay
after the micro completion fence. Native compilation of the uninstrumented
schedule passed; production remains unchanged.

world_overlap_probe.hpp counts packet-window world chunks and samples actual
VPU busy status at chunk boundaries. This witnesses overlap, not utilization.
Admission mode also fingerprints ordered contact fields with two32-bit hashes
before the solver; this is not an exhaustive saved full-body state proof.
world_overlap_analyze.py requires complete phases, full accounting, nonzero
micro work in both schedules, witnessed candidate overlap and matching printed
workload/contact fingerprints. A synthetic valid package passes and changing
one candidate frame's contact fingerprint is rejected. Physical qualification
of the instrumented schedule is pending; its oracle timings cannot be used as
performance evidence. Use --add-probe only to upgrade an already installed
uninstrumented prototype. Inputs are backed up under the scratch root.

The physical128-prop admission package is now
results/world-overlap-verify-2026-10-30.* (ELF2b5ba076...). Both schedules
pass canonical micro validation without misses/errors;600 matched logged
frames have identical workload fields and ordered-contact fingerprints.
The candidate independently verifies47,972 world queries.47,006 whole-body
world chunks run in a software packet window,11,526 enter while VPU status
is busy and4728 leave while still busy. These boundary observations prove
some overlap, not a utilization fraction. The control has zero packet-window
world chunks. Micro job counts differ78,936/78,595 despite matching contacts;
this is a full schedule comparison including admission/history effects, not
a fixed micro-job corpus. Admission includes reference work and cannot price
the change. Unshadowed same-ELF nominal-step timing follows.

The first complete diagnostic timing screen is
results/world-overlap-probed-timing-2026-10-30.*.4800 rows/2400 matched logged
frame pairs pass accounting/parity. Interleaving adds0.167/0.173ms to64-prop
drive physics and0.262/0.271ms to128. This screen still samples VPU status and
updates world-window diagnostics in the candidate interval. Its
performance_qualified is therefore false despite the consistent regression.
The probe now disables all world-window counters/status/clock reads when the
shadow oracle is off. That changes the ELF and requires a fresh same-ELF
admission gate before timing. For the new timing format the world-overlap CSV
must have no data rows; supply --admission with its qualified same-ELF report.
Older diagnostic packages retain the strict nonzero busy-witness checks.
No production optimization is selected from the diagnostic screen.

The diagnostic control's finish waitIdle interval totals11.48/11.62ms per64-prop
phase and15.65/15.60ms per128-prop phase, each covering750 frames. That is only
0.015-0.021ms per frame on average. Even assigning all phase waits to the360
drive frames bounds their mean at0.032-0.0435ms. This limits savings from hiding
that particular fence; it does not bound packet packing/upload/readback or EE
confirmation work, prove unit utilization, or establish the console maximum.
The analyzer records this distinction explicitly.

The revised ELF815b12db... passes its independent physical gate in
results/world-overlap-unprobed-verify-2026-10-30.*:zero lost micro reference
hits/errors,600 matching logged workload/contact fingerprints,47,972 checked
world queries and witnessed world/VU0 overlap. Diagnostic callbacks now return
without status/timer/counter work when verification is disabled. The revised
analyzer's synthetic unprobed package passes, and a mismatched admission ELF
is rejected. Its same-ELF unprobed timing run follows; it remains a private
candidate rather than a selected production schedule.

The complete unprobed timing package is
results/world-overlap-unprobed-timing-2026-10-30.*:4800 rows/2400 matched logged
frame pairs, same-ELF admission bound, no world-window diagnostic rows. Drive
physics changes8.957->9.100 and8.956->9.137ms for64 props;15.328->15.568 and
15.327->15.535ms for128. This policy is rejected; production stays unchanged.

micro_cost_fixture.py follows that screen and profiles the world-first chained
schedule in both arms. It preserves every contact path and6+3 passes. Scopes
measure corner/sphere contacts, transformed views and axis hint, packet packing,
upload/start, finish including readback, reconstruction and contact emission.
Consume includes reconstruction/emission, so those values overlap and must not
be summed. CP0 timing and extra branches perturb the run: attribution is not a
production FPS ranking. A micro-cost.cfg boot flag selects instrumentation;
sat-verify.cfg independently selects the canonical admission oracle. Both modes
run only phases12/13. File writes occur after the completion fence. Inputs are
backed up under the scratch root, actual profile sources have their own hashes,
and predecessor hashes are retained as lineage. Native qualification is pending.

The cost fixture now compiles warning-clean natively. A host harness verifies
disabled scopes perform no timer reads, nested scopes retain inclusive timing,
and typed/void wrappers preserve return values and counts. micro_cost_analyze.py
checks complete phase/contact accounting, duplicate-schedule workload/contact
fingerprints, source ELF binding and exact wrapper calls versus job/batch counts.
All cost buckets cover750 frames including warmup, whereas scene CSVs contain600;
the report explicitly prohibits treating these different-window means as cost
fractions. Canonical admission and unshadowed attribution must use the same ELF.
The profiler does not qualify production FPS, even after admission passes.

The physical admission report is results/micro-cost-verify-2026-10-30.*,
ELF3386355e...:both phases dispatch78,936 jobs/11,583 batches, with no canonical
micro misses/errors.600 duplicate-schedule frames match printed workload and
ordered-contact fingerprints. All eight scope counts match the corresponding
job/batch relationships. A synthetic valid report passes, and one extra
reconstruction is rejected. The same-ELF run with only the shadow oracle disabled
follows; admission timings cannot establish the EE cost ranking.

results/micro-cost-attribution-2026-10-30.* completes the same-ELF unshadowed
screen with both duplicate schedules matching600 logged frames. All750-frame
means agree closely between repeats:corners2.005-2.009ms, preparation1.062-1.069,
packing0.512-0.513, upload/start0.176-0.178, finish/readback0.0747-0.0749,
consume1.234-1.240, reconstruction0.740-0.743 and emission0.379-0.381ms.
Consume overlaps reconstruction and some emission. The profile is attribution,
not a production FPS comparison; extra timers and changed code layout perturb
execution. It establishes that EE preparation/packing/result handling merits
attention even though the finish wait itself is small. Before moving a larger
kernel, inspect stage-specific data layout and duplicated EE work across the
whole pipeline, including the effect of faster EE work on VU0 overlap.

step_pool_fixture.py is the next private layout screen. It restores the
uninstrumented world-first chained source and compiles the original880-byte
step function alongside a312-byte EE step with two pose pointers. Both retain
the same8m grid, scalar contact arithmetic, micro packets/confirmations and6+3
solver. The candidate allocates one contiguous pair of pose spans using every
body's actual vertex count; zero-vertex spheres use null spans. No vertices,
pairs or contacts are removed. Invalid counts fall back to the original wide
function. All spans are assigned before prediction and remain stable until the
next frame, after the micro fence. The intent is to reduce EE working sets in
contact preparation and the solver; faster EE traversal could expose more VU0
wait, so only whole-frame measurements may select it.

The exact shared allocator is step_vertex_pool.hpp. Its host oracle
tools/verify-step-vertex-pool.cpp passes33,526,752 bitwise pose comparisons under
ASan/UBSan, including0/3/8/12/23/24 vertices, record-vector relocation, pool
growth/reuse and invalid-count rejection without truncation. This is storage
correctness only. Same-ELF physical admission and timing of the layout remain
pending. Runtime/header changes are private; no production layout is selected.

The pooled/wide native build now passes, including the312-byte EE ABI assertion.
The fixture retains the original full timing schedule:admission phases12/13,
then unshadowed64/128 comparisons in both phase orderings on the same ELF.
world_overlap_analyze.py also accepts explicitly tagged step-pool experiments:
both world-first layouts require zero packet-window world chunks, while old
interleaving experiments retain their busy-witness gate. A valid synthetic
pooled gate passes and a changed contact fingerprint fails; the prior unprobed
schedule report still passes its original checks. Admission is running on PS2.

The first physical layout admission is rejected and retained in
results/step-pool-verify-2026-10-30.*. Canonical micro validation reports no lost
hits/errors, but all600 measured frames differ in workload/contact fingerprints.
The first warm frame already has different ordered fingerprints despite92
contacts in both arms. Do not qualify timing or assume an arithmetic/compiler
cause from the storage oracle alone. --diagnose adds raw snapshots of every
initialized pre-solver step field, all actual vertices and ordered contact
geometry at frames0/1. Only the phase flush writes them. step_pool_diff.py
decodes exact words independently; a host writer/decoder check locates injected
one-ULP R1/depth changes. The diagnostic build compiles warning-clean. No gate
was relaxed and production retains the original layout.

The first raw diagnostic is results/step-pool-diagnostic-2026-10-30.*. At frame0
all captured core pose/velocity/matrix fields match, but1016 previous-vertex,
768 predicted-vertex,192 bounds and128 OBB words differ, as do48 contact words.
The first previous y is0 versus5.96046448e-8. This localizes the discrepancy to
geometry generation or its inputs; v1 did not capture local hull coordinates,
so it does not yet prove an instruction-contraction cause. V2 (--local-inputs
for an installed diagnostic) also stores those local vertices before both world
poses. The decoder retains v1 support. The actual host writer/decoder passes
the v2 format with injected one-ULP matrix/depth changes correctly located.
No performance claim or promotion is made from the divergent trajectories.

A separate late capture arrived through the reset client's file server after
the diagnostic had printed CB DONE and its owned game client was stopped.
results/vu0-late-contact-2026-10-30.bin/json preserve the1232-byte case and
provenance. Both paths report a hit, but their point distance is about40.8% of
the smaller radius, beyond the existing1% tolerance. Phase/frame are unavailable;
this is not a measured-window failure or a demonstrated missed hit. It does
prevent qualifying broader asynchronous use without replay and a correctness
fix. Do not relax the tolerance. The private diagnostic now stops physics after
CB DONE so unobserved continuation cannot create unlabelled post-window cases;
active benchmark frames and production gameplay are unchanged.

A fresh host build of tools/verify-vu0-sat.cpp reproduces the late capture's
rejection both from simulated results and the stored hardware result
(accepted=0, recoveryAccepted=0). The JSON records source hashes and expected
failure; it is not a passing qualification or proof of host/EE bit equivalence.
The next local-input diagnostic uses snapshot v2 and retains all existing
collision checks. The late positive-contact discrepancy must be replayed and
fixed before broader asynchronous selection, independently of the pose-layout
experiment.

Correctness fix under qualification:finishVu0Sat now accepts a micro negative
only after its existing conservative EE proof; every remaining result calls
scalarMotionContactReference. Approximate untied-positive centroid recovery is
removed. confirmed_positive is separate from the historical tie/CCD counters,
and new classification files must account for every job. The saved late case
passes fresh host replay after the fix;10,000 semantic pairs and2048 mixed-batch
cases pass, with bitwise canonical geometry asserted for all hits. Physical
replay/precision qualification and any new performance measurements are pending.
Earlier micro timings predate this extra EE confirmation work. Default micro
remains off, and the pose-layout gate is still unqualified.

The corrected backend now passes the saved contact on physical PS2 for both
kernel images,2048 startup pairs with zero geometry error,50,000 precision cases
and both dense phase micro oracles without misses/errors. Evidence is in
results/step-pool-local-positive-2026-10-30.* and the late-case JSON. The layout
gate remains rejected. V2 snapshots show identical local hull coordinates and
all captured core inputs at frame0, while derived world poses differ. This
supports using one compiled transform routine before ranking the two layouts;
no trajectory gate is relaxed. Fixture source readers now explicitly use UTF-8.

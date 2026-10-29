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
accounting. It improves pile FPS by 6–25% against uncapped prior algorithms;
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

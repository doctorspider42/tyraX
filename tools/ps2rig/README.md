# PS2 timing rig

The physical-PS2 A/B rig every measurement of the 2026-10 TyraX2 work was made
with (night EE notes, the [architecture audit](../../docs/tyrax2-architecture-audit-2026-10-20.md)).
It copies a generated project and the engine to a scratch root, injects a
frame probe, builds natively, deploys over ps2link and summarises the console
log. One ELF carries several **arms** (variants switched at runtime, one per
cycle), so A and B are interleaved on the same binary and boot.

PCSX2 proves correctness only; timing claims come from the console.

For per-frame questions (which frames of a crash drive drop, and what they
spent), use the sibling rig in [physics/](physics/README.md): one arm per
ELF, a 1000-frame ring, a stress-scene generator.

## Files

The benchmark scenes (crate field, physics pile), the architecture-loop
scripts and the physics result summaries are in [`bench/`](bench/README.md).

| File | What it is |
| --- | --- |
| `cld_probe.hpp` | The probe, copied into the engine as `inc/debug/cld_probe.hpp`. Drives the cycle (boot, 600 parked frames, a full-throttle drive, scene reset), sets `Cld::arm = cycle % CLD_ARMS`, and prints per window `CLD win arm= cyc= st= ... work= p50= p95= late= period=` plus COP0 scopes (`CLDS`), PMU counters and route counts. |
| `cld_shot.hpp` | One VRAM frame grab per early cycle to `host:shot_c<cycle>_a<arm>.tga`. |
| `make_arm.py` | Instruments a copied project + engine (anchored edits; quiet `TYRA_LOG`, probe scopes). `--lean` = loop statistics only (headline numbers); `--pmu` = PMU miss counts per scope without the fine StaPip scopes; `--arms=N`; `--day`; many diagnostic flags (`--gputl`, `--bagcensus`, `--shot`, `...ab` arm sets) implemented by the sibling modules. |
| `run_for.sh`, `scene_sum.py` | `run_for.sh <projectdir> <log> <seconds> [noreset]`: deploy for a fixed time, then stop that run's own client by verified PID (a Windows `timeout` leaves it holding the console). `scene_sum.py <log>`: the architecture loop's scene-table row (docs/architecture-loop.md). |
| `miss_fit.py` | `miss_fit.py <imiss_log> <instr_log>`: the price of an I and a D miss fitted per scope from a `--pmu` and a `--pmu --count-instr` run of the same scene, and each scope's miss share at those prices (architecture loop R2-0). |
| `make_engine_probe.py` | The engine half of `make_arm.py` only (probe in `Engine::realLoop`, quiet log, scopes, flags), so any generated project can be measured as authored: `MAKE_ARM=make_engine_probe.py bash tools/ps2rig/arm.sh S1 --lean`. The cycle states are then just frame counters; the windows sample whatever the project does from boot. `--gpuwait` adds the EE's waits on the GPU (`q.waitFor`, `q.drain`, `q.ringfull`), `--gputl` the GPU timeline, `--twowalk` puts two-players in gameplay (`twowalk.py`). |
| `bench_patch.py`, `rn_patch.py` | Round 14 of the architecture loop (docs/architecture-loop.md). `bench_patch.py <project> 0|1|2` (after `make_engine_probe.py` on the crate-field project): the scripted crash drive at probe frame 2100, the FTUPD update laps and `renderScene`'s cost rows as `Cld` scopes with PMU misses; 1 / 2 = the `renderNow` ceiling probe from the drive start on every / every odd frame. `rn_patch.py <engine> bench|district|district-alt`: StaPip flush scopes (`sp.flush`, `sp.pass`, `sp.cross`) and the probe's return at `renderNow` entry (district: arm 1 drive windows, `-alt` odd frames only); `bench` and `-alt` turn on `CLD_SERIES` (per-frame work lines). |
| `cen_patch.py` | Engine-copy patch: the census of draws reaching `renderNow` (architecture loop round 15): every stamp-pass `goto general` tagged with its source line, counts by revision class (+ lit / billboard / env bits), reason and repeat (`CENS` / `CENR` lines every 150 frames). Map the `rsn` lines with `grep -n 'cenRsn = __LINE__'` in the patched copy. |
| `lapsum.py`, `altsum.py` | Summaries for those arms: the drive windows' blocks with ms, I/D misses and miss share; even vs odd frame work from the `CLDW` series. |
| `rp_patch.py` | Engine-copy patch: the record renderer's ceiling probes (docs/record-renderer.md). With `--arms=3`, arm 1 sends a world stamp whose record list called it inside from the stamp alone, arm 2 does not send it at all (`CLDRP` / `CLDSK` counts). Unsafe by design. |
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

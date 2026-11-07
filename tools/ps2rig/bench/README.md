# The headline benchmark and the architecture-loop rig

These files used to live only in the scratch rig (`C:\tx\qp`) on the
measuring machine. They were moved here when that rig was deleted
(2026-11-07), so the benchmark can be rebuilt on any machine.

## `crate-field/`: the headline benchmark scene

Motor District with ~128 physics crates on the road, and a driven Ravager that
ploughs through them. It is scene 1 of `docs/architecture-loop.md` ("The
headline benchmark"). The shipped `examples/vehicle-playground` does **not**
contain the crate field. This folder is the overlay that turns it into the
benchmark:

- `vehicle-playground.tyra`: the project file of the benchmark (format 127,
  editor 2.10.0), with `liveDebug` and `remotePad` off;
- `objects/`: 119 crates the example does not have, plus `ravagerpark00001.json`
  (the car parked at the far end, facing the field).

To rebuild it:

```bash
cp -r examples/vehicle-playground /c/tx/bench/U5src
cp -r tools/ps2rig/bench/crate-field/* /c/tx/bench/U5src/
build/tyrax-editor.exe --refresh-gen C:/tx/bench/U5src
```

Use a short path: long `host:` ELF paths crash `loadelf`.

## `physics-pile/`: scene 3, the physics scene

The same overlay for the physics scene (`C:\tx\qp\P1`). It has 72 crates
lifted and left to rest **awake** for the whole run, and the car parked.
Use it with `mk_bench1.sh NAME P1 awake`, which removes the scripted drive
and turns holding off. The folder has the `.tyra` and the 84 object files
that differ from the example.

## `scripts/`: the rig

| file | what it does |
|---|---|
| `mk_bench1.sh` | Makes one arm: copies a source project, regenerates it, applies the probes and builds it with `tools/toolchain/native-build.ps1`. `MODE=pile` is the scripted drive into the crate field. |
| `patch_kf.py` | Adds the scripted drive (frames 250..750, full throttle and nitrous) and the per-frame timing ring (`KF` lines, frames 200..1199). |
| `patch_laps.py` | Adds per-stage laps (`PHYSLAP`). |
| `kf_slow.py`, `kf9_slow.py`, `kf10_slow.py` | List the frames whose period went over one 60 Hz frame, with their breakdown. |
| `kf_sum.py`, `kf12_sum.py` | Summarise work p50/p95 and per-component ms and misses. `kf12_sum.py` is the R12 attribution, using the fitted miss prices: I 51 cycles, D 16 cycles at 294.912 MHz. |
| `ab_sum.py`, `pmu_sum.py` | Per-log misses, StaPip route ms and sends per frame (`KFRSUM`, `KFRPMU`). |
| `test_engine_probes.py`, `test_game_probes.py` | Check that the probe patches still apply. Edit the absolute paths at the top first. |

The patch scripts anchor on generated code as it was at 2.10-2.14. After a
codegen change an anchor may no longer match. When it does not, the script
says so instead of patching blindly. Deploy and run the arms with
`tools/ps2rig/deploy.sh`.

Measure in a **release** build. A debug build logs over ps2link every 50
frames, and that logging alone misses vsync.

## `results/`: physics measurements from the qualified-physics work

The final summaries of the collision and solver rounds (PR #310 and #311,
both folded into the performance PR):

- `hardware-final.json`: real-PS2 runs;
- `emulator-summary.json`: PCSX2 runs;
- `STATE.md`: the round notes.

`architecture-loop-task.md` is the original task brief of the architecture
loop (in Polish, as the user wrote it). The loop's log is
`docs/architecture-loop.md`.

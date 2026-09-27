# Moving-camera emulator recheck, 2026-09-27

An isolated Motor District fixture from `vehicles` at `4695fcd8` was built with `benchmark-district.py --profile quiet-debug`, modified with `fps-sweep-2026-09-27.py`, regenerated and built by the editor, then instrumented with `instrument-frame-cost.py --attribute` and rebuilt natively. PCSX2 used its software renderer. Each run captured 240 warmed frames in each of four 360-frame day/night camera sweeps, plus 32 rolling FPS samples. Remote Pad, Live Debugger and AI routes were disabled; the camera moved while the player car stayed parked. These are **PCSX2 timings, not PS2 hardware estimates**.

| Phase | FPS median | Active median | Active p95 | Present wait | Scene | Largest scene phase |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Garage day | 50.000 | 3.024 ms | 5.268 ms | 16.871 ms | 2.039 ms | Objects 0.923 ms |
| Garage night | 50.000 | 3.482 ms | 5.541 ms | 16.443 ms | 2.454 ms | Objects 0.986 ms |
| Outer road day | 50.000 | 3.170 ms | 6.269 ms | 16.703 ms | 2.195 ms | Terrain draw 1.179 ms |
| Outer road night | 50.000 | 3.331 ms | 6.484 ms | 16.567 ms | 2.300 ms | Terrain draw 1.180 ms |

Active time is update + render submission + finish, excluding present/vsync. The longest active frame was 8.926 ms; rolling FPS stayed within 49.969–50.030 in both runs. The roughly 20 ms `total_ms` is chiefly the PAL presentation interval and must not be interpreted as EE work. Pipeline `bounds`, `prepare` and `dispatch` counters overlap the render phases; do not add them to the frame total.

## What the Objects label actually contains

The game interleaves road and static-batch submissions with objects via `dripHeavy()`. The original `rsObjSubmit_ms` bracket surrounds the object's own part draws, but not those interleaved submissions. Consequently `rsObjects_ms - rsObjSubmit_ms` is **not** the cost of visibility and LOD tests.

For a second run, two bare COP0 brackets were placed around the `dripHeavy()` call inside the object loop and its final drain in this fixture's generated `src/terrain_game.cpp`. Their ticks were added to `districtMeasure::arsObjSubmit`; the CSV column name stayed the same for direct frame-by-frame comparison. No render behavior was changed. The rest of the source and engine were byte-identical. Median results:

| Garage pose | Objects total | Object parts only, control | Interleaved roads/batches, second − control | Remaining loop |
| --- | ---: | ---: | ---: | ---: |
| Day | 0.923 ms | 0.077 ms | 0.621 ms | 0.226 ms |
| Night | 0.986 ms | 0.086 ms | 0.666 ms | 0.234 ms |

At the outer-road poses the interleaved contribution was approximately zero, while terrain draw took 1.179/1.180 ms. Garage-night light effects took 0.278 ms versus 0.017 ms by day. Vehicle update took 0.183 ms and parked-wheel rendering about 0.004 ms in all four phases. There were **zero texture uploads and zero reuploads** in every sampled frame. The second run's active medians differed from the first by at most 0.011 ms.

`control-frame-cost.csv`, `control-frame-attrib.csv`, `control-fps.csv` contain the stock brackets. `interleave-frame-cost.csv`, `interleave-frame-attrib.csv`, `interleave-fps.csv` contain the two extra `dripHeavy()` brackets. The latter's `rsObjSubmit_ms` deliberately includes the interleaved work as described above.

## Limit

PCSX2 demonstrates stable 50 FPS on this route and locates relative **emulated** work. It does not establish whether the current physical PS2 is EE- or GS-bound: PCSX2 underprices GS fill and does not model every VU scheduling cost. Earlier hardware measurements found an EE-over-budget night pose and debug `host:` polling overhead, but the current code needs a fresh hardware trace when the console is available. This fixture also does not reproduce the player's own driving or AI traffic.

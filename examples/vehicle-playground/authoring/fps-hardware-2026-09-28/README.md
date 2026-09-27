# Physical PS2 moving-camera recheck, 2026-09-28

This is the September 27 camera sweep repeated on a reset physical PAL PS2
(ps2link at `192.168.100.150`) from editor commit `0d5fdd23`. A fresh
`quiet-debug` Motor District fixture disabled Remote Pad, Live Debugger, Live
Link, Live Logic, Time Machine and Input Recorder. Traffic was parked. The
camera crossed garage Z −74…88 and outer-road X 4…99 by day and night.
Each phase warmed for 120 frames, then recorded 240 frames and eight rolling
FPS samples. Both boots show `loadelf:` in the archived ps2client logs.

The first boot had only the district FPS sampler. The second added
`instrument-frame-cost.py --attribute` to the generated game and rebuilt it
natively. Neither sampler wrote to `host:` during the measured window. The
profile exported 960 aligned cost and scene-attribution rows after sampling.
Engine-level `TYRA_STAPIP_ATTRIB` was disabled, so its `sp*`, `bd*` and `ds*`
columns in `frame-attrib.csv` are intentionally zero.

| View | Baseline median FPS | Profile median active work | Profile p95 / max work | Profile median present wait |
|---|---:|---:|---:|---:|
| Garage day | 50.000 | 7.142 ms | 11.742 / 13.619 ms | 12.830 ms |
| Garage night | 50.000 | 8.725 ms | 13.926 / 15.847 ms | 11.238 ms |
| Outer day | 49.999 | 6.892 ms | 12.258 / 15.040 ms | 13.045 ms |
| Outer night | 50.000 | 7.810 ms | 13.083 / 17.050 ms | 12.149 ms |

The 32 baseline FPS samples span 49.941–50.059. The profiled boot also held
50 FPS at all 32 samples (49.989–50.018). Active work is the measured sum of
update, render submission and finish; present wait is separate. The median
whole loop was 19.960 ms in every phase, the PAL period. The present wait is
mostly idle time to the next field, not EE work. Even the largest observed
profiled active-work frame stayed below 20 ms. This route therefore did not
reproduce the reported moving-view slowdown.

The largest active bucket was render submission: medians 5.474 / 6.481 /
5.244 / 5.595 ms. Update was 0.820–0.874 ms, including about 0.45 ms of
vehicle update; finish was 0.657–1.194 ms. VIF1 DMA wait inside submission
was 0.074–0.564 ms. Near the garage, the broad `Objects` phase was
2.284/2.459 ms, but it includes interleaved road/static-batch work; direct
object-part submission was only 0.224/0.258 ms. On the outer road, terrain
draw was the larger named scene phase at 2.173/2.144 ms. These phase and
pipeline counters are inclusive and overlap: do not sum them as a frame.
All 960 sampled frames recorded zero texture uploads and reuploads.

The route uses a moving camera with parked traffic, not a driven car with
normal AI or every view in the map. These measurements establish EE headroom
for this repeatable route; they do not rule out a different worst view or GS
limit elsewhere. No renderer change follows from this result alone.

## Reproduce and inspect

Create a new short-path fixture with `benchmark-district.py --profile
quiet-debug`, apply `fps-sweep-2026-09-27.py`, then run the current editor's
`--build` to regenerate sources and complete assets. Deploy its ELF over
ps2link with the resident-IOP marker, following
[the testing guide](../../../../.agents/skills/tyra-testing/SKILL.md).
For the second boot, apply `instrument-frame-cost.py --attribute` **after**
generation and run `tools/toolchain/native-build.ps1` directly; another
editor build would overwrite the instrumentation. The archived `fixture.json`
records the camera and debug settings. `baseline-fps.csv` and
`profile-fps.csv` are the rolling samples; `frame-cost.csv` and
`frame-attrib.csv` hold every profiled frame. The two ps2client logs preserve
boot and resource-loading evidence.

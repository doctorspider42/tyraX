# Collision stress results on PS2

Physical PS2 measurements show that the collision pipeline keeps 60 FPS with scattered props in Motor District, but regresses against the earlier algorithms when many bodies interact in a pile.

These are measurements from 2026-10-29, not a prediction from emulator speed.
They qualify the collision branch, not a general 60 FPS guarantee.

## Driving results

Each row combines two 360-frame full-throttle driving windows, with the arm
order reversed for the second repeat. FPS is 1 / mean observed frame period;
physics is the 95th percentile of `updateObjectPhysics`, in milliseconds.
Counts are **additional** boxes, alongside the example's ten authored physics
props. Scattered objects mostly sleep; only up to 11 bodies were awake together
in the candidate's +64 scattered test. Total object count is not active-body count.

| Additional boxes | Earlier algorithms FPS | Candidate FPS | Physics p95, earlier → candidate | Candidate missed refresh slots / 720 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 59.9 | 59.9 | 0.04 → 0.07 ms | 0 |
| 32 scattered | 59.9 | 59.9 | 0.92 → 1.07 ms | 0 |
| 64 scattered | 59.9 | 59.9 | 2.15 → 2.19 ms | 0 |
| 128 scattered | 58.5 | 59.0 | 4.12 → 3.32 ms | 12 |
| 32 piled | 55.9 | 50.8 | 6.28 → 9.57 ms | 130 |
| 64 piled | 44.2 | 39.4 | 17.97 → 25.36 ms | 297 |
| 128 piled | 34.4 | 13.4 | 45.61 → 74.17 ms | 720 |

The +128 scattered physics scope improves by about 19%, but whole-frame FPS
improves by only about 1%. At +64 scattered, the physics difference is negligible.
There is no measured overall speedup: the dense cases are slower. The two
candidate repeats gave 50.77/50.77 FPS for +32 piled, 39.30/39.45 for +64 piled,
and 13.02/13.78 for +128 piled. The large regression repeats in both arm orders.
Different contacts change the subsequent motion and awake population, so these
are workload outcomes under equal input, not instruction-cost ratios for equal
trajectories. The probe does not isolate the narrow-phase or solver contribution.

At +64 scattered, candidate EE work p95 is 12.49 ms; at +128 scattered it is
15.27 ms, leaving little room against the NTSC 16.68 ms cadence. At +32 piled
it reaches 20.48 ms. EE work subtracts the renderer's recorded stall delta from
the frame period; it is not a standalone measurement of GS execution. The
observed frame periods, rather than this subtraction, establish the FPS result.

## Collision limits matter too

Both arms retain a 384-contact solver budget. During the driving windows,
candidate +64 piled omitted 700 contact emissions and +128 piled omitted
821,752. The earlier algorithms also saturated: 886 and 241,927 respectively.
These are omitted contact points over many frames, not unique object pairs.
A bounded frame time after saturation cannot certify complete collision handling.

The car also collects at most eight nearby prop candidates per substep. The
candidate exceeded this list from +32 scattered onward (52 additional candidates
over the two drive windows), and much more often in piles. This counter applies
to a loose horizontal reach test, so it does **not** count proven missed exact
collisions. It identifies a completeness limit that still needs qualification.

The practical conclusion is to budget simultaneous awake bodies and contact
density, not promise 60 FPS from the number of props placed in a level. The
current branch does not meet 60 FPS for the tested piles. Dense contact work,
sleep stability and both contact/candidate caps need further work before such
a workload can be accepted.

## Fixture and scope

- Release build; physical PS2; NTSC progressive, vsync enabled, fixed daytime.
  Live debugger/link/logic/time machine/remote pad/input recorder and keyboard
  polling disabled. Ordinary logs suppressed; shared renderer telemetry remains.
- One ELF switches at scene boundaries between the exact physics and scene
  broad-phase algorithms from main `e4acb7379ef3bebc90362e2884719a4d2a8a9df5`
  and candidate `a5141c767fa600b05f06f532f46bbc62c17cf25d`.
  Both share the candidate's `PhysBody`/`RbStep` layout, renderer, assets and
  surrounding runtime. This is an **algorithm A/B in a common runtime**, not
  two stock main/candidate builds. Shared data layout can affect the comparison
  against stock main. Scalar transforms are used in both arms.
- A fixed pool of 128 boxes stays allocated in both arms; surplus boxes are
  inactive and invisible. Each box is 0.9 m per side, mass 0.6, bounce 0.1,
  friction 0.6, tumbling enabled, sleep 1.5, no shadow. Scattered placement is
  eight columns at 2 m spacing and rows at 7 m; piles are 4×4 per layer.
  The original level, its authored props and 32 procedural extras remain.
- Scene reload before every phase; 150 warm-up, 120 parked, 360 driving and
  120 settling frames. The first phase has 300 extra warm-up frames for boot
  I/O. Treat the zero-box physics comparison cautiously because of that extra
  settling time; both arms still hold 59.94 FPS in both repeats.
- The car receives full throttle and zero steering during the driving window,
  with the handbrake outside it. Pose and speed are recorded; collisions change
  the distance travelled. Windows are frame-count based, so a slower phase also
  covers more wall-clock time. No claim of identical trajectories is made.
- All 28 phase files contain exactly 600 measured frames: **16,800 total**.
  COP0 ticks use 294,912,000 Hz. A period over 25 ms counts as a missed refresh
  slot; CSV writes and four VRAM grabs happen outside the measured windows.
  The captures confirmed the car and props; no resource/assert failures occurred.
  All 238 deployed baked PNG/TMDL assets matched the baked reference.
- There are no mesh-mode colliders in this fixture. These results do not
  measure triangle-BVH speed, night rendering, complex hulls or rotational CCD.
  PCSX2 was used for a boot/input smoke check only.

![Physical PS2: candidate 128-box pile before the drive](img/collision-district-stress.png)

The image is a warm-up capture, not the peak contact load.

The measured ELF SHA-256 is
`c0a00f8c97f8d86cc16b84716455e068636041a43a8f01054a459e73980e2dea`.
The [summary and per-repeat statistics](../tools/ps2rig/results/collision-2026-10-29.json)
and [compressed raw frame CSV](../tools/ps2rig/results/collision-2026-10-29.csv.gz)
are retained with the repository. The [rig instructions](../tools/ps2rig/README.md)
describe how to reproduce the fixture.

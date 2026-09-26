# Burnout 3 GS capture observations

Analysis date: 2026-09-20. Local reference capture:
`Burnout 3 - Takedown_SLES-52584_20260920201357.gs.zst`.
Serial: SLES-52584; CRC: 0x75becc18. The capture, extracted GS data and
screen-space wire preview remain outside the repository.

## Scope

The parser follows PCSX2's GSDump framing and GS register definitions. The
capture contains four alternating VSync fields. Counts below describe the first
field only. GS state indicates NTSC interlaced output; the main viewport uses a
640x448 scissor. The embedded screenshot is 640x480, which is not the viewport
resolution.

These are observations from one stationary chase-camera view, not a complete
profile of Burnout 3. A GS dump cannot establish EE execution time, VU workload,
original model topology or the engine's CPU-side package count.

## Observed rendering

* The first field contains 2,049 recorded transfers, 3,640 GIF tags and 32,530
  triangle-strip primitive kicks across all passes. These are different metrics
  from Tyra's render packages. Hidden and degenerate triangles are not removed.
* The identified car sequence contains 4,585 base-pass strip triangles, including
  its wheels. This is submitted geometry, not a count of unique model faces.
* The car's base texture state is 256x256 PSMT8 (8-bit indexed color).
* Ninety adjacent base/effect state-group pairs have exactly matching projected
  triangles, accounting for 2,794 triangles in each of the paired passes. State
  groups are parser groupings, not engine draw-call counts.
* The second pass samples a 256x256 PSMCT32 texture from memory previously used
  as a render target in the same field. Address matching uses the different
  FRAME and TEX0 address units. Its blend reads destination alpha. A reflection
  or paint effect is the likely interpretation; its precise artistic purpose
  has not been proven from game code.
* VRAM addresses are reused within the field. Counting texture addresses as
  unique assets, or collecting every occurrence of the car's TEX0 value, would
  produce misleading results.

## Implication for the remodeling experiment

The reference does not support a roughly 550-triangle target as the route to
Burnout-like car quality. Our lean-body experiment reduced package counts but
did not improve the garage view's measured 29.97 FPS on PS2. It also sacrificed
body shape, so it is not the active vehicle variant. The active scene retains
the better original bodies with the earlier efficient wheels.

The instrumented candidate's garage-day update, submit and finish medians total
approximately 20.6 ms, above the 16.68 ms NTSC field budget. Submission includes
pipeline waits and is not a pure CPU timer. Instrumentation itself changes
performance. See the adjacent raw CSV files and results.json; 60 FPS has not
been achieved throughout this scene.

PCSX2 software rendering performs GS work on the host CPU. Its pixel, blending,
texture-memory and synchronization costs differ from native PS2 costs. A game
being slower in that mode does not imply that it is slower on the console.
Emulator video rate (VPS) must also be distinguished from internal game FPS.

## Primary references

* [PCSX2 GS dump capture](https://pcsx2.net/docs/troubleshooting/identify/)
* [PCSX2 performance counters](https://pcsx2.net/docs/troubleshooting/performance/)
* [PCSX2 software-renderer synchronization discussion](https://pcsx2.net/blog/2022/q4-2021-progress-report/)
* [GSDump framing](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSDump.cpp)
* [GS register definitions](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSRegs.h)

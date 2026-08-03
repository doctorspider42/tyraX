# iop-compute — the PS2's other CPU shaping the world

Sixteen pillars stand in a row in front of the spawn. Their heights are the
**escape counts of a row of the Mandelbrot set**, and every one of those numbers
is computed on the **IOP** — the R3000A that exists so a PlayStation 2 can be a
PlayStation 1, and that Sony's documentation reserves for I/O. The imaginary
coordinate sweeps, so the row slides through the fractal and the skyline
reshapes itself continuously.

The point is that the IOP is not printing a diagnostic here. It is computing the
world, every few frames, while the EE draws. See
[docs/iop-compute.md](../../docs/iop-compute.md) for the feature; this project is
what it looks like in use.

## What is where

| File | |
| --- | --- |
| [`iop/user_jobs.c`](iop/user_jobs.c) | **the code running on the IOP.** Three jobs; job 2 (`jobMandelRow`) is the one this scene uses. This project has **taken ownership** of the file (the generated marker line is gone), which is why it can carry a job the template does not |
| [`src/scripts/iop_fractal.cpp`](src/scripts/iop_fractal.cpp) | the EE side: submits a row, picks it up some frames later, drives the pillars — and carries the identical loop in C++ as the fallback |
| `iop/txiop.c`, `iop/txiop.h`, `iop/imports.lst` | the module around the jobs. Generated |
| `inc/txiop.gen.hpp` | the EE API: `available()`, `submit()`/`poll()`, `call()`, `info()` |

## Three things the script is arranged to show

**It is asynchronous, which is the whole point.** `txiop::submit()` hands the row
over and returns; the frame carries on drawing; `txiop::poll()` collects the
answer some frames later. One row is several milliseconds of work on a 33.8 MHz
CPU with no FPU — far too much to sit inside a frame, which is exactly why the
blocking `txiop::call()` (and the **Run IOP Job** flow node) is the wrong tool
here and is demonstrated in the docs instead.

**The fallback is real, not a stub.** `solveOnEe()` is the same fixed-point loop
in C++. Turn *Project > Preferences > Build > IOP compute* **off** and rebuild:
the pillars move exactly the same way, the EE just pays for it, and the ELF is
about 9.5 KB smaller. Nothing in the scene or the script changes, because
`txiop::available()` is a compile-time `false` in that build and every entry
point folds away.

**Colour says who did the work.** Green pillars = the IOP solved this row, amber
= the EE did. You can see which CPU is working from across the room.

## What to look for in `bin/log.txt`

Build and run (**F5**, or `tyrax-editor --build . --run`). A debug build logs the
availability gate as data, then a row every 24 solves:

```
LOG: IOP compute: ready. 3 job(s), 1467904 bytes free on the IOP,
     round trip 163us, calibration 7988us vs 1276us on the EE.
LOG: IOP is usable - the pillars are being shaped by the IOP
LOG: IOP fractal: first row solved on the IOP (16 columns, 64 iterations each).
LOG: IOP fractal row   1 (iop) im=-59/100: 3  3  3  3  4  5  5  7
LOG: IOP fractal row  49 (iop) im= 19/100: 5  6  9 64 64 64 64 64
LOG: IOP fractal row  73 (iop) im=  0/100: 8 10 64 64 64 64 64 64
LOG: IOP fractal row 121 (iop) im=-41/100: 3  3  4  6  5  6  7 37
```

Every number in the first line is worth reading:

- **3 job(s)** — the table in `iop/user_jobs.c`.
- **1467904 bytes free** — about 1.4 MB of the IOP's 2 MB. Note it is *less* than
  a bare IOP has free (1.68 MB measured with nothing else loaded): audsrv,
  padman, sio2man and the `host:` filesystem live there too. The IOP is not
  yours alone, and this number is the proof.
- **round trip 163us** — one RPC call with no work in it (146–197 µs measured
  across runs; it moves because the IOP is busy with other things). A 50 Hz frame
  is 20 ms, so a job shorter than ~200 µs is *slower* here than on the EE.
- **calibration 7988us vs 1276us** — the same integer loop on both CPUs. The IOP
  is about **6.3× slower**. It is not a second fast core; it is a slow core that
  is otherwise idle.

The row lines are the interesting ones, and they are deliberately loggable rather
than only visible: a skyline is hard to assert on from a screenshot, but
consecutive rows must differ, and they must differ *in the shape a Mandelbrot row
does*. They do — and they match an independent reference implementation of the
same fixed-point loop:

| | columns 0, 2, 4, 6, 8, 10, 12, 14 |
| --- | --- |
| the console, `im = -0.59` | `3 3 3 3 4 5 5 7` |
| reference, `im = -0.60` | `3 3 3 3 4 5 5 7` |
| the console, `im = 0.00` | `8 10 64 64 64 64 64 64` |
| reference, `im = -0.05` | `7 9 64 64 64 64 64 64` |

That is the claim this example exists to support: the IOP ran our code and got
the right answer, continuously, for hundreds of rows.

## Why the graph does not run a job

The `player-1` flow graph only *reports* availability (**IOP Available** into a
Branch, into two Log nodes). It used to also carry a **Run IOP Job** node, and
that exposed a real constraint worth knowing: **the module serves one request at
a time.** The script submits a row every few frames, so the blocking node kept
finding the IOP busy and taking its `no IOP` output — which looks exactly like
absent hardware and is not.

So: one IOP caller per scene, or gate them against each other with
`txiop::pending()`. The job-running node is demonstrated in
[docs/iop-compute.md](../../docs/iop-compute.md), where nothing competes with it.

## Chosen deliberately: the window

`RE_MIN`/`RE_MAX` in the script straddle the set's western boundary (−1.6 to
−0.6) with `MAX_ITER` 64. A wider window looks obvious but is a worse
demonstration: most of the sweep comes out flat and then a solid wall of
maxed-out columns appears. This window keeps the graded escape-time staircase on
screen for most of the sweep. It was picked by running the same fixed-point loop
against several windows and comparing how much the profile actually moves.

## The honest limits

Everything above was verified in PCSX2, which emulates a full IOP. Three claims
it cannot settle:

1. **The real speed ratio.** PCSX2's IOP timing is not cycle-accurate. On
   hardware the clocks are 294.912 MHz (EE) against 36.864 MHz (IOP).
2. **Audio contention.** On a console the IOP really is streaming audio through
   audsrv. Whether a row job makes it crackle is a hardware question — the
   module's worker thread runs at a low priority precisely because of it.
3. **Deckard slims.** PCSX2 emulates a pre-Deckard IOP, so "this is a fat" is
   checkable here and "this is a slim's emulated R3000A" is not.

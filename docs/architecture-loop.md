# Architecture loop: TyraX limited only by the PS2's hardware

This page is the running log of a loop that started on 2026-11-05 on branch
`claude/perf-integration`. Its goal is to rebuild the parts of TyraX (the
StaPip draw path, physics, frame preparation) whose architecture, rather than
the console, limits the frame.

## The goal, as a measurable condition

For every reference scene, measured on a physical PS2, one of these holds:

- **(a) GPU-bound.** VU1 or the GS limits the frame: the EE waits for the GPU,
  not the other way round.
- **(b) EE-bound on useful work.** The EE is the bottleneck, but:
  - in the subsystems that dominate its time, cache misses (EE PMU) are under
    about 30 % of that time;
  - no identified architecture defect has a measured or estimated ceiling
    above 0.5 ms a frame.

A miss is priced at ~50 EE cycles (I-cache) and ~42 (D-cache), as calibrated
in docs/tyrax2-night-ee-2026-10-12.md.

Reference scenes:

1. **Crate-field drive.** The user's `vehicle-playground-5` project (copy
   `C:\tx\qp\U5src`), the recorded drive.
2. **Motor District.** `examples/vehicle-playground`, heavy spot
   (-119.3, 0.3, 0.3), night and day.
3. **Physics.** The pile and the stack after a crash, and 82 awake boxes
   (`C:\tx\qp\P1`).
4. **two-players**, split screen, in gameplay (not the title screen).
5. **showcase**.

The loop: pick at most two architecture-defect hypotheses, each with a
ceiling and a decisive measurement; one subagent each builds the probe or the
prototype; the console runs (one at a time, same ELF, at least two runs an
arm) happen in the parent session; winners land on `claude/perf-integration`
with their docs, losers are reverted and written up here.

## Scene table

What limits each scene's frame, and how much of the dominant subsystem's time
is spent waiting for memory. Updated after every round.

| # | scene | work p50 / p95 | limited by | dominant EE subsystem | misses in it | meets the goal |
|---|---|---|---|---|---|---|
| 1 | crate drive | 6.70-6.99 / 12.0 ms (2.11.0, BC44) | EE; the GPU never makes the EE wait | StaPip 4.0-4.1 ms | 86-100 % per route (docs/stapip-bag-cost.md) | no |
| 2 | Motor District heavy, night / day | to measure (1.174: heavy 12.3 ms) | EE (VU1 idle 44-51 %, 1.176.3) | StaPip, crossing bags 316-378 µs | to measure | no |
| 3 | physics: 82 awake / pile after a crash | awake resting 0.3 ms physics (held); really awake ~7.5 ms | EE | physics, six stages of similar size, IPC ~0.5 | to measure | no |
| 4 | two-players, gameplay | 3.16 ms average (TP2) | vsync (work << 20 ms) | - | to measure | to confirm |
| 5 | showcase | to measure (was 20 ms GPU-bound; 50 fps after 1.177.14) | GPU? | - | to measure | to confirm |

## Round 0: baseline

The table above is assembled from earlier measurements on older builds. Round
0 re-measures every scene on the current branch: whole-frame work, the
`--gputl` GPU timeline (VU1 / GIF / VIF1 busy), the EE's waits on the GPU,
and PMU misses per dominant subsystem.

## Round 1 (2026-11-05)

### R1-A: VU1 clips only what needs it

**Hypothesis.** A crossing bag (its box crosses the frustum) costs the EE
138-150 µs in the crate field and 316-378 µs in the night district, against
85-105 µs for the whole-replay route. Most of the difference is the EE
deciding per package whether to drop, cull or clip, and building the partial
runs. The split of work is wrong: VU1 is idle 44-51 % of the district frame,
and it already sees every vertex. A VU1 program that culls and clips in one
pass, with an accept path at cull-program speed and a per-package trivial
reject, lets every crossing bag take the whole-replay route.

**Ceiling.** Crate field 13.5 bags x (142 - 99) µs ≈ 0.6 ms; night district
~13 bags x (316..378 - 105) µs ≈ 2.5-3 ms (estimate). It also moves EE work
onto an idle unit, towards condition (a).

**Decisive measurement first.** Re-measure the district's crossing bags on
the current branch (count, µs, PMU), and size the program against resident
micro memory (1904-1950 of 2042 words in production sets). R5 in
docs/tyrax2-arch-audit-ee-submission-2026-10-20.md is the warning: today's
clip program on the same route cost +0.6 / +2.5 ms, because an accepted
triangle runs the staged fan emit at ~184 cycles against 73.

**Pays when** the crate drive's work p50 drops by at least 0.3 ms and the
district's heavy spot by at least 1 ms, with no new GPU wait above what the
EE saves.

### R1-B: physics data layout for awake bodies

**Hypothesis.** Awake bodies cost ~110 µs each over six stages of similar
size (prep, world, pairs, solve, write, broad-phase refresh), at an IPC of
~0.5. Rearranging the solver's arithmetic (two lanes, VU0 macro and micro)
did not pay, so the open question is whether the stages wait on memory:
array-of-structs bodies, contacts that point back to bodies, and per-stage
passes that each re-read every body.

**Ceiling.** 82 really awake boxes ≈ 7.5 ms of physics (30 FPS); a pile
after a crash runs the same path for the frames it is awake.

**Decisive measurement first.** EE PMU (I and D misses) around each physics
stage, all-awake scene and the crash pile. If misses are over ~30 % of the
stages that dominate, rebuild the awake-body data as structure-of-arrays
with contacts carrying copies of the body data they read (setup gathers,
the passes run over contiguous rows, write-back scatters once). If they are
under, physics meets condition (b) as far as memory goes and the remaining
work is arithmetic.

**Pays when** physics in the 82-awake scene drops by at least 1 ms with the
byte-identical or tolerance check passing, or the PMU shows physics is not
memory-bound (then it is written up and closed).

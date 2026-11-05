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

All rows below are physical-PS2 runs of a PMU build (the probe adds ~2.5 ms
of work, so read the ratios, not the absolute milliseconds). "Misses" is the
share of the subsystem's time that its I and D misses would cost, given as a
range: 20 cycles a miss (lower bound) to 50 / 42 cycles (upper bound, the
earlier calibration). The upper bound overcounts. Misses overlap in real code,
and every uncached access counts as a D miss: the vsync wait's register poll
alone reads as 100 %+.

| # | scene | work mean / p95 | limited by | GPU timeline | EE waits on GPU | dominant EE subsystem | misses in it | meets the goal |
|---|---|---|---|---|---|---|---|---|
| 1 | crate drive | 6.70-6.99 / 12.0 ms (lean, BC44) | EE | - | ~0 (max 0.04 ms) | StaPip 4.0-4.1 ms | 86-100 % per route (docs/stapip-bag-cost.md) | no |
| 2 | Motor District heavy spot, night / day | 11.6 / 13.8 ms parked, 11.0-11.3 / 13.5-13.7 driving (PMU build, R0F) | EE (60 fps held, ~5 ms of vsync slack) | VU1 30-34 %, GIF 13 %, idle 50-52 % parked; idle 66-67 % driving | 0.005 ms | StaPip `core` 4.4-6.3 ms; game scene code 1.9-2.3 ms | core 40-97 %; scene code 33-77 %; whole frame ~80 % (upper) | no |
| 3 | physics: 82 awake / pile after a crash | (R1-B arms queued) | EE | | | physics | | no |
| 4 | two-players, split screen, walking (R0U) | 5.6 / 7.3 ms of a 20 ms PAL frame | vsync: neither the EE (28 % busy) nor the GPU (78-83 % idle) | VU1 16-18 %, idle 78-83 % | 0.01-0.02 ms | StaPip `core` 3.2 ms | 39-89 % | the hardware's refresh limits it; see the note |
| 5 | showcase (R0X) | 19.1 / 19.1 ms | **GPU** | VU1 55 % + kick 8 %, GIF 34 %, idle 1.5 % | **5.5-5.9 ms** (q.waitFor) | - | - | **yes, (a)** |

Note on two-players: the frame is paced by the display (50 Hz). Neither (a)
nor (b) describes it, because nothing in the PS2 is the bottleneck. Its EE
work is 5.6 ms, and 3.2 ms of that is StaPip at the same miss share as
everywhere else, so the StaPip rebuild applies to it too.

## Round 0: baseline (2026-11-05/06)

Rig: `tools/ps2rig` (`arm.sh`; `make_arm.py --pmu --gputl --daynightab` for the
district, `make_engine_probe.py --gpuwait --gputl [--twowalk]` for the other
examples), summary `tools/ps2rig/scene_sum.py`. Same branch, after the PR #311
merge. Arms `C:	xpnR0F` (district), `vpnR0U` (two-players), `vpnR0X`
(showcase).

What it shows:
- **Only the showcase is GPU-bound.** Everywhere else the EE never waits for
  the GPU (0.005-0.02 ms a frame) and the GPU is idle half the frame or more.
- **The EE's time is StaPip, and StaPip waits for memory.** In every scene
  the `core` scope (`StaPipCore::renderNow`) is the largest EE subsystem, at
  40 % of its time in misses by the lowest price and ~90 % by the
  calibrated one. That is the same finding as docs/stapip-bag-cost.md, now on
  three more scenes.
- **The calibration probe (`CLDCAL`) prices an isolated miss higher, not
  lower:** I ~83 cycles, D load ~74, D store ~105 (cold 128 KB walks). So the
  >100 % upper bounds come from overlap and uncached accesses, not from cheap
  misses.

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

#### R1-A feasibility (2026-11-05)

**The ceiling, recomputed.** The district's 316-378 µs a crossing bag was
measured on the sliced route before 1.174.0. After whole-package streaming the
R5 baseline put a crossing bag at 175-184 µs against 84 µs for a whole replay.
Run mode 4 (runs + masks) and the lean classification have since taken part
of that difference. The district ceiling is therefore about 13 x 95 µs ≈
1.2 ms before those cuts, or ~1.0 ms now. **The district bar (1 ms) cannot be
met even with zero added VU1 cost.** In the crate field (rig BC44: partial
138 µs x 14.9 bags, whole replay 77 µs) the ceiling is ~0.9 ms, against a bar
of 0.3 ms.

**The program.** The prototype fuses the trivial reject and a fast accept
into the existing clip images. `C:\tx\qp\fuse_clip.py` rewrites
`stapip_clip_{c,tc}[_fold]_vu1.vclpp`:

- **Trivial reject.** It reads the three per-vertex judgements with FCGET
  and ANDs them. A triangle wholly outside one tested half-space (x/y
  against w, near, far: the bits the program's own crossing test reads)
  skips Sutherland-Hodgman.
- **Fast accept.** A triangle that crosses nothing is emitted by the fan
  emitter's exact operations, unrolled three times and reloaded from
  polygon A, instead of going through the generic fan loop.

Both paths are gated on `VU1_CLIP_CONSTS_ADDR.z` (`0x3CA` = on, 0 = the
original behaviour).

**Proof on the host.** The editor's `--vu-check` gained two knobs
([vu-framework.md](vu-framework.md)). With `TYRAX_VU_CLIP_CONSISTENT=1` and
the gate on, every clip section is IDENTICAL to the generated reference:
description parity, the whole-package stream with lists and strips, and the
C/D, TC/TCE and TC/TD peer paths. Planted bugs were all caught:

- a misplaced fog word in the fast accept;
- rejecting on OR instead of AND;
- drawing rejected triangles unclipped.

The fast-accept bug passes with the gate off, which shows the gate selects
the path.

**Cost (openvcl schedule, rows, TC unlit multi-colour no-spot):**

| path | cycles a triangle |
|---|---:|
| cull_tc | 73 |
| clip_tc accept, before | 149 |
| clip_tc accept, fused | 128 (head 22, judgement 41, emit 56, loop 9) |
| triangle trivially rejected | 72 |

The fast accept saves only 21 cycles. The per-triangle colour-mode branches
and the latency-bound judgement are shared by both paths, and they are what
separates the clip program from cull speed. Getting near 73 needs a cull-shaped
loop: one variant chosen per batch, no branches inside the body, and the
judgement folded into the transform chain.

**Micro memory.** Clip TC fold 406 -> 482 words (reject + accept), clip C
fold 374 -> 418 (reject only). The all-class set with billboards is
2038/2042. Adding the fast accept to C as well (2088) would push the
billboards out of residency, so the C image has no fast accept and the
whole-route arm takes textured bags only.

**What the VU1 side predicts.** Package census for the district cruise
window (rig EY2), per frame:

| packages | cull | clip | out |
|---|---:|---:|---:|
| list | 6 | 11 | 24 |
| strip | 5 | 10 | 29 |

Today they cost ~216k VU1 cycles a frame. Through the fused program as one
run they cost ~377k: +0.55 ms of VU1, two thirds of it in out packages that
the EE drops today and the program now rejects one triangle at a time. R5's
"whole" arm (+0.6 / +2.5 ms for ~+540k cycles) showed that this VU1 time is
not hidden by the 44-51 % idle. A per-package box reject (object-space box
in the double-buffered input, eight corners judged at the batch head, ~60
cycles) would bring the run to ~188k, below today's cost. It is the missing
piece for any scene with many out packages.

**Rig (not in the repository).** Engine copy `C:\tx\qp\tyraV`
(`engine_r1a.py` + `fuse_clip.py`, cache `engine-cacheV`), arms built by
`C:\tx\qp\mk_r1a.sh`. probeArm bits on top of the BC44 base 1572868:

- 4194304: gate on, routes unchanged.
- 8388608: whole route. A textured crossing bag skips classification and
  goes as one REF of its baked six-plane clip blocks.

The game logs `R1A whole <bags> pkgs <packages> refused <n>` at frame 1300.
`patch_r1a.py --cap` builds the correctness ELF, which grabs frames 250/600/900
to `host:r1acap-<frame>-a<arm>.tga`; `r1a_capdiff.py` compares them.
Physical-PS2 results: owed.

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

## Round 1 results so far (2026-11-06)

### R1-B, the PMU per physics stage (PS2, rig `C:\tx\qp\R1B_*`)

72 really awake boxes (`R1B_A1`, holding off, 287 contacts):

| stage | ms a frame | I misses | D misses | miss share (20 cyc .. 50/42 cyc) |
|---|---:|---:|---:|---:|
| prep | 1.07 | 192 | 5 131 | 33 .. 71 % |
| world (terrain 0.82 inside) | 1.98 | 2 424 | 4 647 | 24 .. 54 % |
| pairs | 1.25 | 414 | 3 354 | 20 .. 44 % |
| solve setup | 1.07 | 153 | 5 083 | 33 .. 70 % |
| solve passes | 1.96 | 184 | 8 206 | 29 .. 61 % |
| write | 0.55 | 134 | 2 089 | 27 .. 58 % |
| broad-phase refresh (outside) | 0.71 | 871 | 1 810 | 18 .. 57 % |
| total | 7.89 | 3 504 | 28 517 | 27 .. 59 % |

The pile after a crash in the crate drive (`R1B_U1`, windows with 9-19
bodies awake): 1.0-1.9 ms of physics, 27-67 %, prep / world / pairs /
broad-phase dominate; the solver is small there.

Reading:
- **Physics sits on the 30 % line** and the price of a miss decides which
  side. Round 2 calibrates the price (below) instead of arguing it.
- **The solver passes miss far more than their data explains.** They run
  island by island (all 6 + 3 passes per island), and a resting crate's
  island is ~4 contacts x 212 bytes + one 88-byte body, so one frame should
  read ~1 050 lines. It reads 8 206. Either the scope holds more than the
  passes, or the layout defeats the cache (212-byte contacts straddle lines,
  the body array is indexed out of island order). That is a defect with a
  bound: the passes' misses are ~0.6-1.2 ms.
- **prep and setup build big records.** `RbStep` is 880 bytes a body (hull
  corners inside) and setup reads it field by field into 212-byte contacts.

## Round 2 (2026-11-06)

### R2-0: the price of a miss, measured

The (b) condition needs the share of time spent in misses, and the two
prices in use (20 cycles, 50 / 42) put physics on either side of 30 %. The
EE PMU can count completed instructions instead of I misses (`CLD_EV0=12`,
`make_arm.py --count-instr`). With a cycles / instructions / I-miss / D-miss
row for every scope (two builds, same scene), a least-squares fit of
`cycles = c0 * instructions + pI * I + pD * D` over all scopes gives the
effective prices in this code, and the miss share follows. Owned by R2-B for
physics and by the parent for StaPip.

### R2-A: draw records the EE replays without touching the bag (StaPip)

**Hypothesis.** In every EE-bound scene StaPip's `core` is the largest
subsystem and 40-97 % misses. The stamp route already replays captured bytes,
but it still touches the bag, info, model, texture, colour bag, stamp table
and baked entry to validate (~31 µs a stamp; ~47 stamps a frame in the crate
drive, ~100 in the district). The defect is that the per-frame path reads
the scene's scattered objects at all. A draw that did not change since its
last frame should cost one contiguous record read plus its captured bytes.

**Ceiling first.** An unsafe probe arm: a stamped draw whose recorded
inputs did not change replays from a compact record without touching the
bag (the GPU work is identical, so the EE number is honest). Expected: ~31 ->
~8-10 µs a stamp = ~1 ms crate drive, ~2 ms district.

**Pays when** the ceiling is ≥ 0.5 ms in the crate drive and the district,
and then a correct version (a per-bag revision the producers bump, policed by
RECORDVERIFY) keeps most of it.

### R2-B: the physics passes' misses and the big step records

**Hypothesis.** See R1-B above: the passes read ~8x the lines their island
data needs, and prep / setup move 880-byte `RbStep` records. Find what the
passes' misses are (scope contents, layout, index order), shrink
`RbSolveContact` to what a pass reads (the rest in a cold array), and build
slim step records. Plus the R2-0 calibration for physics.

**Pays when** physics in `R1B_A1` drops by ≥ 0.5 ms with a tolerance check
passing, or the calibration shows physics under 30 % (closed).

### R1-A on the PS2 (crate drive, rig `C:\tx\qp\RV1`, same ELF)

| arm | runs | work p50 | p95 | StaPip ms a frame | crossing bags (r5) ms / bags a frame |
|---|---|---:|---:|---:|---:|
| control (1572868) | 2 | 6.83 / 6.81 | 11.97 / 11.95 | 3.97 / 3.96 | 1.89 / 1.91 |
| fast accept + reject on VU1 only (5767172) | 1 + 1 short | 6.90 / (7.19, 600 frames) | 11.93 / 12.20 | 3.95 / 3.98 | 1.90 / 1.91 |
| whole: textured crossing bags sent whole (14155780) | 1 | 6.56 | 11.65 | 3.70 | 1.57 / 13.3 |

`R1A whole 11132 pkgs 110964 refused 0` in the whole arm, 0 in the others.
The whole route takes ~0.27 ms of StaPip and ~0.26 ms of work p50 off, just
under the 0.3 ms bar and on one run only. Its second run wedged the console
(`freepad: DMA Busy` after 8 reset deploys; not attributed to the arm).
Owed before a verdict: the second run, the GPU-wait arm (`RV1G`) and the
frame-capture check (`RV1C`).

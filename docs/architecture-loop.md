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

## The headline benchmark

The worst frame drops seen in this project are in the **Motor District with
~128 physics cubes scattered on the road, with the car driving into them**.
That is the crate field of `vehicle-playground` (rig copy `C:\tx\qp\U5src`,
scene 1 below, recorded drive; the changelog's "car driving through 129
crates"). Every cost of this loop meets in that frame:
- StaPip, for the district and the crates;
- physics, for the bodies the hit wakes;
- the batch rebuilds when a body leaves or rejoins a static batch;
- the car's crash work.

**The loop is done when this benchmark either holds a stable 60 fps on a
physical PS2 (no missed frames over the recorded drive) or is measured
against the console's ceiling.** Against the ceiling means the frame is
GPU-bound, or the EE does useful work under the (b) condition with no
architecture defect above 0.5 ms. Every round reports this scene's work
p50 / p95 and missed frames.

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
| 1 | crate drive (R2A_1 control, 2 runs) | 7.31-7.35 / 12.3 ms | EE | - | ~0 (RV1G: 0.00) | StaPip 4.3-4.4 ms | **stamp 61 %, crossing 66 %** at pI 51 / pD 16 (stamp 32.4 µs, 69 I + 147 D; crossing 143 µs, 423 I + 408 D a bag) | no: (b) fails on misses |
| 2 | Motor District heavy spot, night / day | 11.7 / 13.7 ms parked, 11.1 / 13.3 driving (R0H/R0J) | EE (60 fps held) | VU1 31 %, GIF 13 %, idle 50 % parked; idle 67 % driving | 0.007-0.029 ms | StaPip `core` 4.5-5.9 ms; scene code 2.5 ms | **core 61-73 % (I-cache 46-51 %), IPC 0.28** (measured R2-0, R0J + R0I); scene code 38-56 % | no: (b) fails on misses; R4-A lean path -0.22..-0.24 ms |
| 3 | physics: 72 awake crates (R2B_A1 / R3Q_L) | physics 7.76 -> **5.54 ms** (2.11.1-2.11.3, verified exact) | EE | - | - | physics: passes 2.4, prep 1.1, world 0.97 | 25 % fitted before the ports (pI 51, pD 16) | (b) on misses yes; defects found (world re-query, pair rebuild, step layout) fixed; pile after a crash not re-measured |
| 4 | two-players, split screen, walking, NTSC (R0W) | 5.5 / 7.3 ms of a 16.7 ms frame | vsync: EE 33 % busy, GPU 77 % idle | VU1 19 %, idle 77 % | 0.03 ms | StaPip `core` 3.1 ms | 41-89 % (20/50 cycle range) | display-paced; waiting for the user's call |
| 5 | showcase, NTSC (R0Z) | 19.06 / 19.12 ms | **GPU** | VU1 55 % + kick 8 %, GIF 34 %, idle 1.3 % | **5.9 ms** (q.waitFor 5.7) | - | - | **yes, (a)** |

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

## Round 2 results (PS2, 2026-11-06)

The console ran 8 deploys after a power cycle, then wedged again (an empty
log on the 9th; a reset deploy afterwards got no answer either).

### R2-0 for physics: a D miss costs ~16 cycles, not 42

`r2b_fit.py` over an I-miss run and an instruction run of `R2B_A1` (72 awake
crates, the live solve; 23 + 23 windows, 15 scopes, 690 rows), fitting
`cycles = c0 * instructions + pI * I + pD * D`:

    c0 1.39 cyc/instr, pI 50.9 cyc/I-miss, pD 15.7 cyc/D-miss
    rms relative residual 6.6 %

The I-miss price matches CLDCAL (a refill stalls the pipeline). A D miss
costs a third of the calibrated load: the EE keeps going past a miss until
the value is used, and store misses go through the write-back buffer. Priced
that way, physics spends **25 %** of its time in misses (stages: prep 28 %,
world 27 %, pairs 19 %, setup 28 %, passes 23 %, write 25 %). That is under
the 30 % line. The 50 / 42 pricing said 57 %.

### R2-B: the fused island solve - winner, shipped as 2.11.1

The passes scope held five things: pack the bodies, apply the warm start,
the island passes, unpack, store the warm impulses. Only the island passes
were compute (IPC 0.67, 9 % misses). Building the contacts, the warm apply
and the warm store each streamed the whole 287 x 212 B = 61 KB contact array
through the 8 KB D-cache, and the passes streamed it a fourth time.

The fused arm handles each island end to end in a buffer the size of the
largest island. Same ELF, I-miss runs, 23 windows each:

| arm | setup | passes | six stages | D misses a frame |
|---|---:|---:|---:|---:|
| live (0) | 1.097 | 1.992 | 7.762 ms | 28 264 |
| fused (4) | 0.261 | 2.321 | 7.242 ms | 20 759 |

That is -0.52 ms. The check arm (8) ran the live and fused paths on the same
inputs every frame. Over 1 250 frames it found no difference at all:
velocities, pseudo velocities, grounded flags and warm impulses were
bitwise equal, and the warm sizes and order matched. Ported into
`src/game_templates.inc` (2.11.1). `RbSolveBody` also lost its unused
inertia matrix (88 -> 52 B). The second timing run was the one lost to the
wedge.

What is left in physics: `world` (1.81 ms, terrain 0.78 inside),
`prep` / `setup` building contacts from 880 B `RbStep`s, `pairs` 1.20, and
`bpRefresh` 0.71. With misses at ~25 % they are mostly instructions.
Physics now meets (b) on the miss share. The defect question for physics is
now about the instruction count: 1.23 M instructions a frame for 72 bodies
is ~17 k per body.

### R2-A: draw records - loser

Same ELF (`R2A_1`, engine copy `tyraR2A`), 2 runs per arm, 1 000 frames:

| arm | work p50 | p95 | missed | StaPip | stamps a frame, us each |
|---|---:|---:|---:|---:|---:|
| control (524292) | 7.31 / 7.35 | 12.32 / 12.31 | 9 / 10 | 4.26 / 4.44 | 46.6 / 49.3 at 32.4 |
| records (17301508) | 7.32 / 7.34 | 13.79 / 13.84 | 89 / 91 | 4.79 / 4.76 | 43.5 / 42.0 at 47.7-48.3 |

Two things kill it:
- **Hits are rare.** Of ~38 000 eligible draws, 1 714 replayed from a
  record. 35 400 were turned away because their lighting inputs changed:
  the car's lights move every frame, so lit props never keep a record. The
  turned-away tries (route 0, 64 a frame at 7.9 us) cost 0.5 ms on their
  own.
- **A hit costs what a stamp does.** A record replay took 31.8 us against
  32.4 us for a stamp. The time is in writing the captured bytes into the
  packet and the DMA chain, not in validating the bag. So the 8-12 us
  ceiling was wrong, and even a 100 % hit rate saves nothing.

The arm also missed 9x more frames (the record build stalls). Reverted:
the arm lived only in the rig engine copy, and nothing landed in the repo.
Dead end: a stamp's cost is its output, not its inputs, so making the
validation cheaper cannot pay.

## Round 3 (2026-11-06)

### R3-A: physics instructions per awake body

**Hypothesis.** With misses at ~25 % (R2-0), physics on 72 awake crates is
still 7.24 ms: 1.23 M instructions a frame, ~17 k per body. Contact
generation per crate is a SAT box test plus a clipped face patch against a
few neighbours and the terrain. A rough count says that needs ~3-5 k
instructions. The gap would come from work done per frame that only
changes when the pose changes: `world` (1.81 ms, terrain 0.78 inside)
re-collides resting crates with the static world every frame, `prep` /
`setup` rebuild 880 B `RbStep` records, and `bpRefresh` (0.71 ms) re-sorts
bodies that did not move.

**Ceiling first.** A probe arm skips the static-world query (`world`) for a
body whose pose moved less than a tolerance since last frame and reuses
last frame's world contacts. Same ELF, live vs probe. The pass is the time
of the `world` + `bpRefresh` stages that goes away.

**Pays when** physics in `R2B_L1`'s scene drops ≥ 0.5 ms and the contacts
reused stay within tolerance of the recomputed ones (check arm).

### R3-B: the body-pair stage with no body touching another

**Hypothesis.** In `R2B_A1`, `pairs` costs 1.20 ms (~2 730 instructions per
awake body) while every body finds only itself (`pairCand=72`, zero
body-body contacts). Every frame it rebuilds the swept body grid, queries
it, runs `broadPhaseNear` for sleepers and merges the lists. All of that
depends only on the poses. The defect is rebuilding a spatial structure for
bodies whose swept boxes did not change cells.

**Ceiling first.** A probe arm keeps the body grid and the sleeper-neighbour
lists while every awake body's swept box stays in the same cells, and only
re-queries a body whose cells changed. A check arm compares the candidate
pair lists with the rebuilt ones.

**Pays when** `pairs` drops ≥ 0.5 ms with identical pair lists.

## Round 3 results so far (PS2, 2026-11-06)

The queue wedged on its second deploy: the reset that followed a stopped
Motor District run (`run_for.sh`) answered `freepad: DMA Busy` 70 times. Only
`vpnR0H` ran (district, I-miss PMU, `--gpuwait`, 2 400 + 1 800 frames per
state):

| state | work mean / p95 | GPU idle | EE waits on GPU | core ms | core I / D misses a frame |
|---|---|---:|---:|---:|---|
| parked night | 11.73 / 13.68 | 50 % | 0.029 ms | 5.70 | 14 261 / 19 109 |
| parked day | 11.86 / 13.89 | 52 % | 0.029 ms | 6.44 | 16 386 / 21 217 |
| drive night | 11.10 / 13.34 | 67 % | 0.007 ms | 4.49 | 12 384 / 15 781 |
| drive day | 11.40 / 13.84 | 67 % | 0.007 ms | 4.98 | 14 108 / 16 868 |

The present's waits are split now: `r.vsync` 5.1-6.5 ms, the GS tail
0.001-0.002 ms. The frame is EE-bound with the GPU half idle.

The instruction run (`vpnR0G`) did not happen, so StaPip has no fit of its
own yet. At the physics fit's prices (pI 51, pD 16 cycles; the I price
matches CLDCAL, so it is not physics-specific), StaPip `core` spends
**43-49 % of its time in I-cache misses** and 18-19 % in D misses: 61-67 %
in all. Physics was 25 %. StaPip fails (b) on the I-cache alone, whatever
the D price turns out to be.

### R4-A: StaPip's hot code does not fit the I-cache

**Hypothesis.** 14-16 k I misses a frame for ~100-200 bags is ~70-150 lines
(2.2-4.8 KB) refetched per bag. A bag's path through `renderNow`, the route
it takes, the packet writers and the Vif1Queue is longer than the 16 KB
2-way I-cache can keep across the game code that runs between flushes.
Measured dead ends to respect: `-Os` on the StaPip directory (half the code,
4-6 % slower), hot/cold link ordering (no gain), and running one bag twice
back to back (92 % of its cold cost: one bag already overflows).

**Ceiling first, no console.** From the ELF: the static footprint of each
route's path (functions and lines reachable per route, which of them are
rare branches), and an estimate of the I misses a lean per-route path would
take. Then a rig arm that splits the hottest route's cold branches out of
line (or a route-specialised `renderNow`) and measures I misses per bag on
the PS2.

**Pays when** StaPip `core` I misses fall enough to save ≥ 0.5 ms in the
district or the crate drive.

The instruction-count arm wedged the console twice: `vpnR0G` hung while
loading the district (the first `freepad: DMA Busy` comes right after the
road texture loads, in a run with no reset before it). `cld_probe` re-armed
the PMU once a frame, and the loading frame is ~10 s long. Instructions
(event 12) pass 2^31 in that time, and a counter overflow is an unmaskable
exception. I misses never get there. Fixed in `cld_probe.hpp`: the PMU counts
only in the measured states (`pmuStop` otherwise). Rebuilt as `vpnR0I`
(instructions) and `vpnR0J` (I misses), both `--pmu --gpuwait --gputl` from
`R0vp`, so the fit pairs two builds of one probe.

### R4-A analysis: one bag's path, by route (from the ELF, no console)

A crossing bag walks ~25 functions and 2 500+ basic blocks. A block averages
15-26 bytes, and every block the bag jumps to is a new 64-byte line.
`renderNow` alone has 492 branches in 11.5 KB.

| route | reachable code | path one bag runs | I misses a bag (measured) |
|---|---:|---:|---:|
| stamp | ~20 KB | 3.5-4.5 KB | 58 |
| culled | ~26 KB | 6-7 KB | ~100 |
| whole replay | ~55 KB | 17-19 KB | 290 |
| partial | ~65 KB | ~25 KB | 412 |
| direct | ~60 KB | ~28 KB | 460 |

The partial, whole and direct paths are each larger than the 16 KB I-cache
on their own. That is why `-Os` did not help: half the bytes but the same
blocks, 413 -> 393 misses. The stamp path fits, but other routes in the same
run evict it. The fattest hot functions are:
- `renderNow` 11.5 KB;
- `sendObjectData` 11.0 KB: every material variant around ~10 inlined
  unpacks;
- `tryStamp` 6.1 KB;
- `StaPipBakedStreams::acquire` 5.7 KB: the hit path is mixed with arena
  growth;
- the `Vif1Queue` send chain, ~11 KB for 0.75 sends a bag;
- `emitPartialRuns` 2.4 KB.

On top of those, 15-20 one-line accessors live in other source files, and
with no LTO each is a call and a line.

**The fix to measure.** A lean path per route, chosen once per bag from a
flags word. For partial: cached bounds, the MVP, a fixed-layout unlit
uniform writer, a cached baked-entry pointer, then `classifyRoutes` and
`emitPartialRuns` as they are. Everything else goes out of line
(`[[gnu::cold, gnu::noinline]]`). Also:
- a per-bag "no stamp" hint, so a crossing bag does not try and fail a
  stamp (10.7 µs);
- a lean append into the recording for `sendPacket`.

The target is ~9.5 KB of dense code, ~180 lines a partial bag instead of
412.

**Ceiling.** At 51 cycles a miss, 1 000 misses are 0.17 ms:
- crate drive: partial 0.56 + whole 0.11 + direct 0.11 + stamp 0.15 = ~0.9 ms;
- district: ~1.0 ms.

Expect roughly half of either. The D misses (the bag's own data) are not
touched by this fix.

### R2-0 for StaPip: the district, measured (`vpnR0J` I misses, `vpnR0I` instructions)

Both arms ran 45 windows without a wedge. `core` per frame:

| state | ms | instructions | IPC | I misses | D misses |
|---|---:|---:|---:|---:|---:|
| parked | 5.85 | 485 k | 0.28 | 15 677 | 19 290 |
| drive | 4.52 | 366 k | 0.27 | 13 379 | 15 547 |

Physics ran at IPC 0.54. The three-price fit is ill-conditioned here. Only
three scopes have PMU rows (core, q.submit, G.scene), and they nest, so the
fit returns an impossible c0 of 0.15 cycles an instruction. A bound does the
job instead. At 1.0-1.39 cycles an instruction (physics fitted 1.39) and 51
cycles an I miss, `core` spends:
- 61-72 % of its time in misses parked;
- 62-73 % driving.

I misses alone are 46-51 %, and the implied D price is 9-23 cycles,
consistent with physics' 16. `G.scene` (game scene code) is 38-56 %.

**StaPip fails (b) on misses in the district, by a wide margin, and the I-cache
is most of it.** R4-A is the lever.

`miss_fit.py` now drops scopes without PMU rows and `G.endFrame` (it holds
the vsync poll).

### NTSC for every reference scene

Two-players and showcase were `videoSystem: "auto"`, so they ran at 50 Hz on
the user's PAL console; the vehicle-playground scenes were already NTSC. Both
examples now build NTSC (commit a4e82b614). New arms `vpnR0W` (two-players,
`--twowalk`) and `vpnR0Z` (showcase), both `--gputl --gpuwait`, are queued.
The 50 Hz rows above (R0U, R0X) are superseded once they run.

## Round 4 results (PS2, 2026-11-06, queue r6)

Seven deploys ran, and the eighth wedged (an empty log, the usual
reset-count wedge).

### R4-A: lean partial path (`vpnR4A3`), verify + 2 interleaved runs

**Verify** (mask 0x1BC00). The routes, uniforms and classification were
compared on 208 297 bags, with 11.4 M words and 0 mismatches. The cached
baked entry was cross-checked 1 600 times with 0 bad.

**Timing.** Mask 0x1DC00, ALT: control and lean alternate by cycle in one
boot, 2 runs. Motor District, night:

| state | arm | core ms | I / D misses a frame | partial bag µs | I misses a partial bag |
|---|---|---:|---|---:|---:|
| parked | control | 6.07 / 6.06 | 14 988 / 21 097 | 172 (part), 186 (strip) | 488 / 453 |
| parked | lean | 5.83 / 5.84 | 13 741 / 21 204 | 154 | 375 |
| drive | control | 4.59 | 12 850 / 16 658 | 188 / 161 | 558 / 453 |
| drive | lean | 4.37 | 11 850 / 16 674 | 145 | 406 |

The lean arm took all partial bags (eligible share 100 %), and its cached
entry hit 60-99 % of the time. The lean send never engaged: every send went
to `qbFallback`, so its precondition is wrong for this game.

**Result: -0.22..-0.24 ms of `core` and of work, both runs.** An I-miss
saving of ~1 000-1 250 a frame, at 0.17 µs each, accounts for all of it.
The D misses did not move. The partial bag fell from ~470 to ~390 lines,
not to the 180 targeted.

**Verdict: below the 0.5 ms bar.** Not ported yet. Fixing the lean send
could add up to ~0.1-0.3 ms (estimate). The whole route path still runs
~375 lines a bag, because the baked lookups, `classifyRoutes`,
`emitPartialRuns` and the flush are still in the shared code.

### Scenes 4 and 5 at NTSC

- **Two-players (`vpnR0W`):** work 5.5 / 7.3 ms (mean / p95) of a 16.7 ms
  frame, 1 late frame in 5 700. GPU idle 77 %, EE waits on the GPU
  0.03 ms. Still paced by the display: vsync takes 11.1 ms a frame.
- **Showcase (`vpnR0Z`):** work 19.06 ms, every frame late at 60 Hz. GPU
  idle 1.3 % (VU1 55 % + kick 8 %, GIF 34 %), and the EE waits 5.9 ms a
  frame on the GPU. **GPU-bound: (a) holds at NTSC too.**

### R3 check run (`R3B_1`, mask 72)

- **R3-B (pair cache):** hit rate 1.00 and 0 mismatches in 88 472 body
  checks, over 1 226 frames.
- **R3-A (world-contact reuse):** hit rate 0.98, but the contact *count*
  differed on 648 of 85 320 hits (0.8 %). The carried points and depths of
  the contacts that match are within 4e-6. A reused body sometimes keeps a
  contact the fresh query drops, or misses one it adds (a terrain corner
  crossing the rbReduce threshold).

R3-A needs a count-stable reuse rule before its timing means anything. The
timing runs (masks 0 / 38) were lost to the wedge.

## Round 5 (2026-11-06)

### R5-A: stamps sent by reference, not copied

**Hypothesis.** In the district the stamp route is the largest single route:
89 stamps a frame at ~30 µs, 2.6-2.7 ms of `core` parked. A stamp averages
64 I and 116 D misses: ~7 KB of cold lines per stamp. R2-A showed the time
is spent in the stamp's output (copying the captured bytes into the packet
and patching them), not in validating it. The architecture defect is copying
static, already-built DMA data every frame. The DMA controller can send it
from where it lies: a REF tag to the captured block plus a small packet of
the words that change (matrices, colour, the per-frame header).

**Ceiling first.** A probe arm emits each stamp as {patched header packet,
REF to the captured payload}, with `SyncDCache` on the captured block only
at capture. A VERIFY arm compares the GIF/VIF stream the GPU receives with
the copied version.
- Expected: 30 -> ~10 µs a stamp, ~1.7 ms parked and ~0.9 ms driving in
  the district;
- crate drive: 47 stamps, ~0.9 ms.

**Pays when** core drops ≥ 0.5 ms in the district and the stream is
identical.

### R5-B: finishing what rounds 3-4 left

Two pieces:
- **R4-A lean send.** It never engaged (every send went to `qbFallback`).
  Fix its precondition and extend the lean path to the whole-replay and
  direct routes. Expected: +0.1-0.3 ms on top of R4-A's 0.22-0.24.
- **R3-A count-stable world-contact reuse.** Reuse a body's world contacts
  only when its pose is within tolerance AND no corner sits within a margin
  of the `rbReduce` / contact thresholds. The check arm must show
  countMismatch 0.

### R5-A verdict: dead end from the code, no console run

The premise was wrong. A stamp copies at most 24 qwords (`kStampMaxQw`), ~14
typically, and its geometry already goes by one REF to the baked arena.
Most of the copied head is rewritten every replay:
- the MVP (4 qw);
- fog in options;
- TEX0, because VRAM and CLUT can move;
- spot (3 qw);
- single colour;
- env basis (3 qw).

Only FLUSHE, wrap and ALPHA never change, so the static share a REF could
carry is 2-4 qwords. The arm was built (`vpnR5A1`, bits 0x20000/0x40000/0x80000),
and its estimate is 0.02-0.05 ms a frame, possibly negative: each REF
costs a lookup in the immutable-span table. The same idea was measured on
2026-10-20 ("chain stamps", `docs/tyrax2-arch-audit-ee-submission-2026-10-20.md`)
at +0.02 / -0.01 ms. Not run.

What this, R2-A and R4-A say together: a stamp's ~30 µs and its 116 D
misses are the memory the replay reads, not its code or its copy. That is
the per-draw state it gathers to patch: the camera, the light, the texture
residency, the arena entry. That memory is scattered across the engine. The
next lever is the layout of that state, phase 3 of `docs/dod-submission.md`
(hot records), and not another pass over the stamp's code.

## Round 5 results: physics R3 timing, the 2.11.1 port, R1-A (PS2, queue r7)

All 8 deploys ran. Physics in `R3B_1` (72 awake crates, I-miss PMU, 23
windows a run):

| arm | runs | world | pairs | bpRef | physics total |
|---|---|---:|---:|---:|---:|
| control (0) | 2 | 1.87 / 1.87 | 1.21 / 1.21 | 0.69 / 0.69 | **7.26 / 7.26** |
| R3-B pair cache (32) | 1 | 1.87 | 0.26 | 0.69 | **6.31** (-0.94) |
| R3-A + R3-B (38) | 2 | 0.35 / 0.35 | 0.54 / 0.54 | 0.27 / 0.27 | **5.15 / 5.15** (-2.11) |

- **R3-B: winner.** It saves -0.94 ms and was exact in the check run
  (88 472 body checks, 0 mismatches). It goes to the port.
- **R3-A: ceiling about -1.2 ms on top of R3-B**, but its contact count
  differed on 0.8 % of hits. It waits for the count-stable rule (R5-B part
  2) before it can ship.

With both, the 72 awake crates cost 5.15 ms instead of 7.76 ms at the start
of the loop.

**2.11.1 on the console (`R2B_L1`, generated from the template):** physics
7.246 ms (set-up 0.126, passes 2.398). This matches the fused probe arm's
7.242, so the port is confirmed.

**R1-A verdict: loser.** In `RV1G` the whole route took StaPip from 3.96 to
3.82 ms (-0.14) and work p50 from 6.79 to 6.72. The first run had shown
-0.26. EE waits on the GPU were 0 in both arms, so VU1 absorbing the clip
work cost no EE time, and saved little. That is under the bar, so it is not
ported. The `TYRAX_VU_CLIP_*` knobs stay off.

## Round 6 results (PS2, queue r8)

### R3-A with the count-stable rule (`R3B_2`): winner

- **Check (mask 72):** `countMismatch=0` over 76 771 reuse hits in 1 250
  frames. The hit rate stays 0.98, `missMargin` is 2 bodies a frame, and
  carried points are within 4e-6.
- **R3-B:** still 0 mismatches over 88 472 body checks.
- **Timing, same ELF:** physics control 7.57 ms, R3-A + R3-B 5.44 ms
  (-2.12):

  | stage | control | R3-A + R3-B |
  |---|---:|---:|
  | world | 1.83 | 0.84 |
  | pairs | 1.46 | 0.28 |
  | bpRefresh | 0.68 | 0.25 |

  The control is 0.3 ms slower than `R3B_1`'s because the rule's code is
  in this ELF.
- **Status:** handed to a port (2.11.3). The R3-B port (2.11.2, commit
  f76a916b9) is on the branch, and its console check runs next.

### R4A4: lean send fixed, lean whole / direct

- **Why the lean send never ran:** the Live Debugger's VU1 tap
  (`g_vuPacketHook`) stays installed, and the R4A3 lean send refused every
  send while it was set. Now the lean send calls the tap itself. Every send
  went lean (`send.lean` = `hooked`).
- **Verify:** 0 mismatches over 232 760 bags (13.3 M words), the whole/direct
  checks and 4 619 sends.

Mask 0x3DC00, ALT, 2 runs, same ELF:

| state | control core | lean core | control work | lean work |
|---|---:|---:|---:|---:|
| parked | 6.31 / 6.31 | 6.01 / 6.03 | 12.12 / 12.11 | 11.83 / 11.84 |
| drive | 4.75 / 4.82 | 4.44 / 4.40 | 11.17 / 11.23 | 10.85 / 10.77 |

That is -0.28..-0.42 ms of `core` and -0.29..-0.46 ms of work, ~0.33 on
average.

This ELF's control is 0.24 ms slower than R4A3's control, with no code on
its path changed. Adding code alone moves `core` by about the size of the
gain, through the I-cache layout. **Verdict: under the 0.5 ms bar, and inside
the layout noise between ELFs. Not ported.** R4-A reached ~0.3 ms by trimming
code. Getting StaPip under 30 % misses needs the data side (hot records,
phase 3 of `docs/dod-submission.md`), not more code trimming.

### The 2.11.2 port (R3-B) on the console

- **Exactness:** a verify build (`R3P_V`, `TYRA_PHYS_PAIR_CACHE_VERIFY 1`)
  found `PAIRCACHE mismatch=0` over 81 849 body checks.
- **Cache use:** 1 120 hits, 6 partial and 9 full rebuilds, 24 fallbacks
  (load and settle).
- **Timing:** the live build (`R3P_L`, 2 runs, generated from the template)
  gave pairs 0.256 / 0.256 ms and physics 6.426 / 6.426 ms. 2.11.1
  (`R2B_L1`) was 7.246, so -0.82 ms in the shipped code.

## Round 7 (2026-11-06)

### R7-A: where a stamp's misses come from

**Fresh look.** The stamp is the most frequent route: 89 a frame in the
district, ~47 in the crate drive. It costs ~30 µs with 64-75 I and 116-147
D misses. Three things have been ruled out by measurement:
- validation (the lean-stamp probe: ~3 µs);
- the copy (R5-A: ~14 qwords);
- code trimming (R4-A).

None of them explains the ~120 D misses (~7.5 KB of distinct lines a
stamp). The goal needs StaPip under 30 % misses, so the misses have to be
found before any rebuild is chosen.

**Measurement, no code change to the path.** An arm with PMU sub-scopes
inside `tryStamp` and its callees, one per stage: lookup, validation, light
pick, spot rebuild, template patch, packet write, queue append. It reports
I and D misses and cycles per stage per stamp, district parked. Decides the
next rebuild: hot records, scratchpad staging, or a different packet
destination.

### The 2.11.3 port (R3-A) on the console

A verify build (`R3Q_V`, `TYRA_PHYS_WORLD_REUSE_VERIFY 1`, with the 2.11.2
pair cache) found `WORLDREUSE countMismatch=0` over 71 120 reuse checks out
of 83 672 tries. The reuse rate is 85 % in this build: the port invalidates
more than the probe did. Max carried point diff 4e-6, max depth diff 2.4e-7.

The timing build (`R3Q_L`) wedged on its first deploy. It hung while
loading, the console's usual wedge after ~19 resets since the last power
cycle. Timing is owed.

### R7-A result: a stamp is 30 small costs, none dominant (`vpnR7A1`)

Motor District parked, stages on and off alternating in one boot, 2 400
frames each, 89 stamp hits a frame.
- **Control (stages off):** a stamp costs 29.9 µs with 70 I + 107 D misses,
  2.66 ms a frame.
- **Probe overhead:** +7.8 µs a stamp at 76 ticks a lap. The shares below
  are of the instrumented 37.8 µs.

The largest stages:

| stage | share of cycles | share of D misses | what it is |
|---|---:|---:|---|
| `S.frustum` | 11.0 % | 7.4 % | frustum check + guard-band mask |
| `S.head` | 7.4 % | 9.1 % | renderNow's plane cache / band choice |
| `S.f.submit` | 7.3 % | 7.2 % | one send per ~5 stamps (13.8 µs each) |
| `S.copy` | 6.5 % | **16.7 %** | the ~14-qword head copy: 20 D misses, the cold packet destination |
| `S.texres` | 6.3 % | 4.6 % | texture residency + wrap |
| `S.flush` | 5.5 % | 3.3 % | flush decision |
| `S.texuse` | 4.3 % | 5.6 % | useTexture + CLUT |
| `S.light` / `S.spot` | 4.9 / 3.8 % | 3.9 / 3.4 % | light pick, spot rebuild |

The other 22 stages are 1-3.3 % each. The producer facts (pointers,
content, streams, info) are 9.4 % in all, which matches the earlier lean
stamp's ~3 µs.

**What this says.** No stage is worth a rebuild of its own. A stamp costs
what it does because it asks ~30 independent questions of ~30 structures
every frame: the frustum, residency, light, spot, program, texture, CLUT,
fog and MVP. For a parked or slowly moving camera, most of the answers are
the same as last frame. The defect is architectural: the per-draw state is
recomputed per frame, per draw, from scattered sources, not kept as a draw
record that is updated when an input changes.

That is phases 3 and 5 of `docs/dod-submission.md` together. Each draw
keeps a record of its resolved state (texture slot and wrap, light and spot
pick, program, fog class, the visibility class with a frustum-epoch stamp),
plus the inputs it was resolved from. A frame first runs a pass of
input-change events (camera moved, light moved, texture evicted), and then
emits from records.

**Ceiling, estimated from this table.** A stamp whose record is valid needs
the frustum test (with a cheaper epoch test when the camera did not move),
the MVP, the copy and its share of the send: ~8-12 µs instead of 30. That
is ~1.6-2.0 ms a frame in the district and ~0.9 ms in the crate drive.
This is the only lever measured so far that is large enough to bring
StaPip's miss share down materially. It is a multi-week rewrite of the
submission path, not a rig arm.

### The 2.11.3 port, timed (`R3Q_L`, generated, 2 runs)

| stage | ms a frame, run 1 / run 2 |
|---|---|
| world | 0.975 / 0.973 |
| pairs | 0.283 / 0.282 |
| bpRefresh | 0.379 / 0.381 |
| physics | **5.543 / 5.539** |

72 awake crates in shipped code, across this loop:

| version | physics |
|---|---:|
| 2.11.0 | 7.76 ms |
| 2.11.1 | 7.25 ms |
| 2.11.2 | 6.43 ms |
| 2.11.3 | **5.54 ms** |

That is -2.22 ms (-29 %) in all, with every step verified exact on the
console.

## Round 8 (2026-11-06): the draw-state record, stamp route first

### R8-A: a stamp keeps its resolved state and re-asks only on change

**Hypothesis (from R7-A).** ~25 of a stamp's 30 stages answer questions
whose inputs did not change since the stamp's last replay:
- texture residency and wrap, `useTexture` and CLUT;
- light pick and spot;
- program set;
- fog class;
- batch candidacy;
- the producer facts.

Give each stamp a record of those answers plus the input epochs they came
from. Every input source gets an epoch counter:
- the camera (view-projection), for the frustum, MVP and fog;
- the dynamic-light set;
- the texture manager's residency/eviction;
- the program set;
- the renderer globals (fog, overrides);
- the bag's own revision (pointer and content facts).

A replay whose epochs all match runs the frustum test (or nothing, when
the camera epoch matches too), patches the MVP when needed, copies and
sends.

**Ceiling first, as a rig arm.**
- **Bit R:** use the record when the epochs match.
- **Bit V:** verify. Compute both and compare the emitted words and every
  cached answer, including the case where an epoch did NOT move but an
  answer changed (a missing epoch bump = a bug in the contract).
- Measured parked (camera epoch constant: the upper bound) and driving.
- Expected: 30 -> 8-15 µs a stamp, ~1.3-2.0 ms parked and ~0.7-1.0 ms
  driving in the district.

**Pays when** core drops ≥ 0.5 ms driving with V clean. Then it is ported
as the first slice of `docs/dod-submission.md` phase 3, and the other
routes follow with the same record.

### Scene 3, the pile after a crash (`R3PILE` = 2.11.3, `R1B_U1` = 2.11.0)

The crate field with the scripted drive ploughing into it, I-miss PMU, the
windows after the hit:

| build | runs | awake | contacts | physics | per awake body |
|---|---|---:|---:|---:|---:|
| 2.11.3 | 2 | 9.8 / 13.1 | 4.7 / 5.1 | 1.24 / 1.56 ms | 127 / 119 µs |
| 2.11.0 | 1 | 9.9 | 11.7 | 1.39 ms | 141 µs |

The bodies here move, so neither cache can pay, and the per-body cost is
about unchanged (-10 %). At the fitted prices (pI 51, pD 16) physics spends
~39 % of its time in misses. The 50/42 table above says 67-70 %. But
physics is 1.2-1.6 ms of this frame. The frame's dominant subsystem is
StaPip, the same as scene 1, so the pile does not open a physics defect of
its own above 0.5 ms.

## Round 8 results: R8-A stamp record (`vpnR8A1`, PS2)

**Verify** (mask 0x800000): 1 037 429 compared stamps, `verdict=0`,
`recomp=0`, but `bug=3 790` (0.37 %). All of them are camera-contract
misses: the MVP words (2 568) and the head's plane cache (1 222) changed
while the camera epoch did not. Some view writer does not bump the epoch.
This must be fixed before a port.

**Timing**, ALT (0x1400000), 2 runs, same ELF, Motor District:

| state | arm | core | work mean / p95 | stamp µs | stamp I / D misses |
|---|---|---:|---|---:|---|
| parked | control | 6.15 / 6.16 | 11.96 / 14.10 | 30.4 | 71 / 116 |
| parked | records | 5.21 / 5.22 | 11.02 / 12.97 | **20.0** | 37 / 95 |
| drive | control | 4.64 / 4.63 | 10.94 / 13.35 | 31.9 | 83 / 119 |
| drive | records | 4.60 / 4.63 | 10.93 / 13.25 | 32.3-32.8 | 81-83 / 125 |

- **Parked: -0.94 ms of core and of work, -1.1 ms p95, both runs.** 86.9 of
  89 stamps a frame took the fast path. That is the largest single StaPip
  saving in this loop.
- **Driving: nothing.** The camera epoch moves 2.6 times a frame (the main
  view plus the env/aux passes). 41 stamps a frame took the camera-only
  path, 8 the full path and 6.7 were rejected. A camera-only replay costs
  32 µs, as much as the full path. So it re-does or re-touches much more
  than the frustum, MVP and fog, or the write-back of recomputed words into
  the head costs what the skipped stages saved.

**Verdict.** The benchmark is a drive, so R8-A does not pay yet. Next
(R8-B):
- fix the camera contract;
- find what a camera-only replay still pays;
- key the visibility test so a moving camera stays cheap: a bag box against
  the planes, without the head's plane rebuild per stamp.

### R8-B: camera contract fixed, camera-only path made "cheap" (`vpnR8B1`)

**Cause of the contract gap.** R8-A moved the camera epoch in
`pushEnvView`, `pushPortalView` and `popEnvView` before
`Vif1Queue::beforeForeignWork()`. That call flushes the recorded draws,
which therefore resolved under one view and stored under the next. The
epoch now moves after the flush. Verify: `bug=0 verdict=0 recomp=0` over
1 060 338 stamps.

Timing, ALT, 2 runs:

| state | control core | records core | control stamp | records stamp |
|---|---:|---:|---:|---:|
| parked | 6.18 / 6.20 | 5.44 / 5.43 | 30.4 µs | 21.7 µs |
| drive | 4.67 / 4.66 | **4.87 / 4.87** | 32.1 µs | **34.2 µs** (96 I / 132 D) |

- Parked: -0.76 ms. Driving: **+0.20 ms, worse than the control**.
- The camera moves 2.56 epochs a frame: 1.6 `update()` calls, 0.2
  projection changes and 0.38 probe push/pop pairs.

The stage laps (S run, instrumented):

| mode | µs a stamp |
|---|---:|
| fast | 35.5 |
| camera | 42.3 |
| moved model | 80.7 |

The fixed part of every record replay is ~28 µs instrumented (~20 real):
`P.pre` 8, `S.chk` 6.4, `S.state` 5.3, `S.copy` 4, `S.tail` 6. The camera
part adds `S.vis` 7.7 µs with 24 I misses. That is new code, cold every
time. The record path is added on top of `tryStamp`, so it grew the stamp's
code. While the camera moves it runs the cold parts on every stamp, and
the I misses rise from 82 to 96 a stamp.

**Lesson (the one `docs/dod-submission.md` predicted after phase 2a).**
Records alone do not pay while the camera moves, because each stamp still
walks a long path on its own, and that path's code is not hot. Records
have to be consumed by a short loop that runs over all of a frame's stamps
at once. The loop's code then stays in the I-cache, and the records are
contiguous, so they can be prefetched or staged in the scratchpad.

### R8-C: the stamp pass

At each flush point, the recorded draws whose stamp record is valid run
through one tight loop:
- visibility against the epoch's planes;
- MVP and fog into the packet;
- head copy;
- queue append.

It is one function of ~1-2 KB, over records stored contiguously in the
order of the frame list. Everything else (no record, a model that moved,
an epoch beyond the camera's) goes through today's path.

- **Ceiling:** the R8-B laps without the per-stamp path overhead
  (`P.pre`, `S.chk`, `S.tail` ≈ 20 µs instrumented), ~8-12 µs a stamp
  while driving. That would be ~0.9 ms driving and ~1.5 ms parked in the
  district.
- **Pays when** driving core drops ≥ 0.5 ms with V clean.

## Round 8 results: R8-C stamp pass (`vpnR8C1`, PS2)

**Verify** (V|P): 956 252 pass predictions compared with the general path,
0 bad, 0 order errors, 0 exactly-once errors. R8-B's own check stayed
`bug=0` over 1.0 M.

**Timing**, ALT, 2 runs, same ELF. This ELF's control is slower than R8A1's
(stamp 36.5 vs 30.4 µs) from code growth alone, so compare within the
ELF:

| state | arm | core | work mean / p95 | all stamps µs |
|---|---|---:|---|---:|
| parked | control | 6.74 / 6.77 | 12.55 / 14.72 | 36.5 |
| parked | pass | **5.15 / 5.12** | **10.67 / 12.43** | **20.6** |
| drive | control | 4.96 / 4.92 | 11.33 / 13.68 | 37.5 |
| drive | pass | 5.00 / 4.96 | 11.15 / 13.46 | 34.9 |

**Parked: -1.6 ms core, -1.9 ms work, -2.3 ms p95.** 86.9 stamps a frame
went through the pass at 20.3 µs (35 I / 97 D).

**Driving: -0.18 ms work, core flat.** What still stops it:
- **Short runs.** 31.8 runs a frame, 25 of them empty. They are ended by
  bags with no stamp (12.9), moved models (8.0, the car and traffic),
  texture epochs (6.0) and visibility rejects (2.3). Each run pays its
  fixed setup, and each ending draw pays the full path.
- **A pass stamp while the camera moves** costs 29 µs with 62 I / 128 D
  misses. The 128 D misses are mostly the producer facts and the
  bag/info/model/entry lines every check reads.

### R8-D: keep runs long, stop reading the bag

- **A run does not end** at a visibility reject (skip it), a moved model
  (MVP from the model in the pass), a texture-epoch draw (re-resolve
  inline) or a draw with no stamp (send it through the general path
  without closing the run's state). Only order-sensitive state forces a
  close.
- **A per-bag revision** replaces the producer facts. It is bumped by every
  writer of a bag, info, model or colour stream. RECORDVERIFY and V police
  it. The pass then reads only the record, the stamp head and the arena
  REF.
- **Pays when** driving core drops ≥ 0.5 ms with V clean.

### R8-D: one run per flush, per-class revision (`vpnR8D1`, PS2)

**Verify:** all clean.
- R8V: 959 767 compared, bug 0.
- R8CV: bad 0, order 0, exactly-once 0.
- R8DV: 6 208 in-pass culls, all confirmed (cullBad 0). 558 798 revision
  claims, qBug 0.

The pass now runs once per flush (1.0-1.4 runs a frame, max 89-91 draws).

Timing, ALT, 2 runs, same ELF:

| state | arm | core | work mean / p95 |
|---|---|---:|---|
| parked | control | 6.49 / 6.48 | 12.82 / 15.55, 12.61 / 14.72 |
| parked | R8-D | 5.13 / 5.05 | 11.13 / 13.07, 10.85 / 12.79 |
| drive | control | 4.74 / 4.80 | 11.58 / 14.55, 11.46 / 14.04 |
| drive | R8-D | 4.57 / 4.64 | 11.15 / 13.93, 11.08 / 13.46 |

- **Parked:** work -1.7 / -1.8 ms, p95 -2.5 / -1.9.
- **Driving:** core -0.16 / -0.17, work **-0.43 / -0.38**, p95 -0.6.
  Work falls more than core: the code outside StaPip runs with a less
  polluted I-cache.

The cost left is the stamp's D side.
- **Parked:** a pass stamp costs 20.9 µs, 36 I / 99 D.
- **Driving:** 29.6 µs, 65 I / 125 D.

99 D misses is far more than the lines the fast path should touch: the bag
line, the record, the stamp head (4 lines), the model line, the entry, the
texture slot and the packet destination. That is ~15-20. The next question
(R8-E) is what the other ~80 are: uncached packet stores counted as misses,
the queue/arena bookkeeping, or the head copy source spread over lines.

### R8-E: where a pass stamp's misses go (`vpnR8E1`, PS2)

**Verify:** clean (R8V / R8CV / R8DV 0). XS span memo `spanBug=0`.

**X (narrower prefetch, span memo, lq/sq head copy): no gain.** Same ELF:
core 5.14 vs 5.15 parked, 4.59 vs 4.62 driving. Not ported.

**Laps** (parked, un-lapped R8-D pass stamp = 17.6 µs, 31 I / 83 D):

| stage | D a stamp | share | | stage | D a stamp | share |
|---|---:|---:|---|---|---:|---:|
| facts | 12.1 | 15 % | | book | 4.5 | 6 % |
| state | 10.6 | 13 % | | model | 4.5 | 6 % |
| dst (packet) | 7.6 | 10 % | | slot | 3.7 | 5 % |
| src (head) | 5.2 | 7 % | | batch | 3.4 | 4 % |
| rec | 4.9 | 6 % | | flush chain | ~11 | 14 % |

The rest are ≤ 3 % each.

What this says:
- The packet is not written uncached.
- `pref` costs more than it saves on the EE (CLDCAL `copy_lqsq_pref` 15.4 k cycles vs 12.5 k without).
- The remaining misses are again spread evenly. **The stamp is at ~18 µs
  parked, and the next ~20 % would need the producer facts and the
  renderer state out of the path entirely.** That is a per-bag revision
  covering object parts too, and renderer state resolved per flush.

**The district's StaPip after R8-D, parked:**

| route | bags a frame | µs a bag | ms a frame |
|---|---:|---:|---:|
| stamps | 89 | 17.6 | 1.57 |
| partial | 8 | 178 | 1.42 |
| strip | 6 | 188 | 1.13 |
| whole replay | 5 | 99 | 0.49 |
| direct | 1 | 142 | 0.14 |

**Crossing bags (partial + strip, 2.5 ms) are now the largest StaPip cost,
not stamps.**

### R8-D is a winner: port it

R8-D took parked work down 1.7-1.8 ms and driving work 0.38-0.43 ms, with
p95 down 0.6-2.5 ms. Verify was clean in three runs: 2.9 M stamps, the
order checks and the revision contract.

**Ported to the engine (next commit):**
- the epochs;
- the stamp record;
- the stamp pass in the frame recorder;
- the per-class revision for static batches, terrain and road chunks.

The verify switch stays as a compile-time engine define.

## The 2.12.0 port on the headline benchmark (PS2)

The crate-field crash (U5src, scripted drive into ~128 crates), all three
arms built from `vendor/tyra` copies with `TYRA_FRAME_PROFILE 2` and the
`patch_kf.py` frame ring:
- `BOLD`: 2.11.3;
- `BNEW`: 2.12.0;
- `BVER`: 2.12.0 with `TYRA_STAPIP_STAMP_VERIFY 1`.

**Verify (`BVER`):** `STAMPVERIFY` shows 0 on every counter over 164 300
stamps:
- rec bug 0, verdict 0;
- pass bad 0, order 0, once 0;
- cull 309 / 309;
- rev 95 332 checks, bug 0.

| arm | runs | work p50 | p95 | missed of 1 000 |
|---|---|---|---|---|
| 2.12.0 | 2 | 6.57 / 6.77 | 11.11 / 11.09 | 15 / 14 |
| 2.11.3 | 2 | 6.62 / 6.78 | 11.25 / 11.22 | 15 / 14 |

The stamp pass changes little here (p95 -0.14 ms). The drive moves the
camera, and the crates and the car are untagged object parts.

**The missed frames are not the EE's work.** The slow frames (`kf_slow.py`)
fall every 50 frames: 291, 341, 391, ... 791. In several of them pre + work
is 8-13 ms and the frame still takes two vsyncs. Examples: f1141 at 7.9 ms,
f741 at 12.0 ms. U5src is a `debug` build. Every 50 frames it logs ~9
lines over ps2link (FRAMETIME, FTVEH, FTUPD, FTPHYS, FTOCC, FTSPLIT,
PROFILE FRAME, ...), and every ~25 frames a VEH line. Those blocking host
writes are what misses the vsync. Only the crash frames 377-382 and 455
(pre 5.6-6.3 + work 12.4-13.1 ms) are over budget on their own.

**The benchmark has to be judged in a `release` build.** The arm `BRNEW`
(2.12.0, buildProfile release) is built. Its first deploy found the console
wedged, after ~35 deploys since the last power cycle.

### The headline benchmark in a release build, without host logging

`BRNEW` is 2.12.0 with buildProfile `release`. It still missed 9 frames in
1 000, 2 runs. Most of them fell on the 50-frame cadence of the frame
profile's own FRAMETIME line: the generated `drawDebugHud` prints it over
ps2link whenever `TYRA_FRAME_PROFILE` is on, and the rig needs that switch
for the KF ring. `BRNEW2` is the same build with that print held back
until frame 1 250, after the ring.

| run | missed of 1 000 | work p50 | p95 |
|---|---:|---:|---:|
| `BRNEW2` r1 | **4** | 6.36 | 10.65 |
| `BRNEW2` r2 | **3** | 6.04 | 10.57 |

**Every remaining miss is a crash frame:**

| frame | event | pre (ms) | work (ms) |
|---|---|---:|---:|
| f307 | first contact | 6.1 | 10.6 |
| f381 / f382 / f384 | the plough into the field | 5.7-6.9 | 11.2-13.9 |

Normal frames run pre ~1.9 ms. In those four frames physics, vehicles
and fx explain only ~1-2 ms of pre. Something else, ~3-4 ms, lands in pre:
bodies leaving their static batches, pre-bakes, batch rebuilds or the
settle before `beginFrame` (see the frame-escape notes,
`docs/static-batching.md`).

**Status against the user's bar.** The headline benchmark runs at 60 fps
for 996-997 of 1 000 frames. The 3-4 that miss do so by 0.3-4 ms, all in
the frames of impact. That is the next target: R9-A, the crash frame's pre.

### R9-A: what the impact frames' pre is (`BR9A`, release, 2 runs, identical)

`kf9_slow.py`, the laps between the loop's entry and `beginFrame`:

| frame | pre | settleTerrainChunks (update + prebake) | settleStaticBatches (rebuild + prebake) | physics |
|---|---:|---:|---:|---:|
| f307 | 5.6 | **3.28** (2.25 + 1.00) | - | 0.34 |
| f381 | 6.1 | **3.77** (2.4 + 1.3) | 0.05 | 0.83 |
| f382 | 7.6 | **3.1-3.2** (2.2 + 0.9) | **1.6** (1.15 + 0.37) | 0.96 |
| f384 | 5.6 | **1.9** (1.4-1.5 + 0.3-0.4) | 0.65 | 1.5 |

Normal frames have pre 2.2 ms. **The impact frames miss because a terrain
chunk is built that same frame, not because of the crates.** At ~22 u/s
the drive crosses into a new chunk often enough that `settleTerrainChunks`
builds one chunk a frame for long stretches. f304-f310 each build one, at
1.0-3.3 ms. When that coincides with the crash, the frame goes over. The
crates add the batch rebuild (1.6 ms at f382) and a heavier physics
(0.8-1.5 ms).

Also seen: the present is inside `endFrame` in every frame (`pp` ~0.1). So
no previous-frame tail leaks into pre, except f215 and f293 (pp 2.8-3.1).

### R9-B: terrain chunks built to a time budget, ahead of need

Two changes:
- **Look ahead and start early.** Build a chunk before the camera needs it,
  by a larger look-ahead ring in the direction of travel.
- **Cap each frame's share.** Split the work so a frame spends at most a
  budget (e.g. 0.7 ms) on terrain chunk geometry plus its prebake. Either
  the build itself is resumable (rows of cells), or the prebake is staged
  over frames the way `docs/impostors.md`'s near-bake staging was
  (1 024 vertices a frame).

The decisive measurement is the same benchmark in release: missed frames
0 / 1 000, and no frame with `settleTerr` above the budget.

### R9-B on the headline benchmark (2.12.1, release, PS2)

`BR9B`: budget 700 µs. `BR9B0`: the same generated game with
`TERRAIN_CHUNK_BUDGET_US 0`, the old settle. Both with no host log inside
the ring. Runs alternated, 2 each:

| arm | missed of 1 000 | work p50 | p95 |
|---|---|---|---|
| budget 700 µs | **1 / 1** | 6.04 / 5.89 | 10.53 / 10.52 |
| budget off | 2 / 4 | 6.08 / 6.02 | 10.43 / 10.43 |

f382's pre fell from 6.4 to 3.1 ms. The two misses left:
- **f382 (r1):** pre 3.1 + work 13.7 = 16.8 ms, 0.1 ms over. The work is
  the render frame of the plough into the field.
- **f755 (r2):** the end-of-drive hit on the parked car. veh 3.9 ms
  (crash damage and the dent bake) and scene 4.25 ms.

The terrain cause is gone. **The benchmark now runs 999 frames in 1 000 at
60 fps.** The last two are an impact frame's render work and the car-on-car
crash, each 0.1-1 ms over.

### R10-A: the last missed frame's render work (`BR10`, release, PS2)

Run 1 missed f382 only. The render split (`kf10_slow.py`), normal frames
first:

| | StaPip flush | stamp pass (stamps) | crossing (`partialReplay`) |
|---|---:|---:|---:|
| normal frames (field drive) | 4.0-6.7 ms | ~51-105 | 14-18 bags, 2.0-2.8 ms |
| f381 | 7.74 ms | 104 | 20 bags, **4.10 ms** |
| f382 (missed) | **8.18 ms** | 102 (3.07 ms of stamps) | 22 bags, **4.13 ms** |

In f381-382 the road chunks (1.74 ms) and static batches (1.26 ms) cross
the view edges.

There were no GPU waits, drains, prebakes or patch rebakes in f382. The
miss is the crossing route on the frame where the car ploughs past the
field's batches and road chunks at the screen edge: 22 crossing bags at
~190 µs.

**What is left of the headline benchmark is the crossing (partial) route.**
It is the same item R8-E named as StaPip's largest cost after the stamp
pass. R4-A's lean partial path measured ~20 % per crossing bag. A
data-side rebuild of that route (its baked classification per package,
which the camera invalidates every frame) is the next architecture item.

## Round 11 (2026-11-06): the crossing route

### R11-A: crossing bags classify per package from a per-bag record, in one pass

**Facts.** A crossing bag costs ~180-190 µs: 500-600 I and 500-630 D
misses. There are 14-22 a frame, 2.5-4.1 ms, and they are the last missed
benchmark frame.

For each crossing bag the general path runs, per frame:
- the bag's own bounds and plane setup;
- the baked-entry lookup;
- `classifyRoutes`, a test of every package box against the planes;
- `emitPartialRuns`;
- the per-package sends.

R4-A trimmed the code (~20 %), but the data side was not touched: package
boxes, baked offsets and the per-route state are read cold for every bag.

**Hypothesis.** The crossing route is the stamp story again. Each crossing
bag keeps a compact record:
- its package boxes (SoA, in one contiguous block);
- the baked entry pointer;
- the uniform head.

A frame then classifies all crossing bags' packages in one tight loop
against the frame's planes, and emits the runs from the record. This is the
stage-major form of the partial route, R8-C's lesson applied to crossing
bags.

**Ceiling first.** A rig arm (same ELF, ALT, verify against the general
path's emitted words) on the district and on the benchmark.
- Expected: 180 -> 80-110 µs a crossing bag, -1.2..-1.8 ms on f381/382.

**Pays when** crossing-bag time drops ≥ 0.5 ms a frame with verify clean.

### R11-A results (PS2), pending a verify question

**Timing, same ELF.**

District (ALT, 2 runs):

| state | control crossing µs a bag | pass crossing µs a bag | work mean | p95 |
|---|---:|---:|---|---|
| parked | 214 | 127 | 11.00 -> 10.32 | 12.91 -> 11.64 |
| drive | 201 | 137 | 11.00 -> 10.00 | 13.25 -> 12.18 |

Benchmark (release crate-field drive, alternating deploys, 2 runs each):

| arm | missed of 1 000 | work p50 | p95 |
|---|---|---|---|
| R11 pass | **0 / 0** | 5.60 / 5.83 | 10.22 / 10.06 |
| control | 3 / 4 | 6.60 / 6.59 | 11.24 / 11.26 |

**Verify is NOT clean yet.**
- Clean: classification (`clsBad 0`), heads (`headBad 0`), lengths
  (`lenBad 0`), verdicts, order and exactly-once.
- Not clean: `runsBad` 115 267 of 157 123 compared draws (district) and
  26 051 of 52 817 (benchmark). The words of the emitted runs differ.

Until it is known whether that is the GPU receiving different data or the
dry copy producing address-dependent words, R11-A is not a winner. Under
diagnosis.

### R11-A verify resolved: a winner (PS2)

**Cause of the mismatches.** Every first-mismatch dump was the program-kick
CNT tag after a clip block. Words 1-3 were equal, and word 0 had the same
ID and QWC. Only bits 16..25 differed: the `PAD:10` field of ps2sdk's
`dma_tag_t`, which `packet2_chain_set_dma_tag` leaves holding old buffer
bytes and the DMAC ignores. The compare now walks the stream like the DMAC:
- tags exact except that PAD mask;
- CNT payloads exact;
- REFs checked as qwords.

Re-run (`R11A2`, `BR11A2`, timing path unchanged):

| run | compared | bad | PAD-only |
|---|---:|---|---:|
| district | 158 180 | `wordBad 0` (heads, runs, lengths, classes, verdicts, order, once all 0) | 116 521 |
| benchmark | 68 539 | all 0 | 28 286 |

**Timing.** District (ALT):

| state | crossing bag, control | crossing bag, pass | work |
|---|---:|---:|---|
| parked | 213 µs | **125 µs** | 11.02 -> 10.31 ms, p95 12.82 -> 11.57 |
| drive | 201 µs | **136 µs** | 10.98 -> 10.00 ms, p95 13.15 -> 12.04 |

**The headline benchmark** (release, alternating deploys, 2 + 2 runs, 4
with the earlier `BR11A`):

| arm | missed of 1 000 | work p50 | p95 |
|---|---|---|---|
| R11 pass | **0 / 0 / 0 / 0** | 5.53-5.83 | 10.04-10.31 |
| control | 3 / 3 / 3 / 4 | 6.49-6.60 | 11.24-11.31 |

**The benchmark holds 60 fps through the whole recorded drive, with every
draw's DMA stream verified equal.** R11-A goes to the port (2.13.0).

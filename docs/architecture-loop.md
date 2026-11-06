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
| 1 | crate drive (R2A_1 control, 2 runs) | 7.31-7.35 / 12.3 ms | EE | - | ~0 (RV1G: 0.00) | StaPip 4.3-4.4 ms | **stamp 61 %, crossing 66 %** at pI 51 / pD 16 (stamp 32.4 µs, 69 I + 147 D; crossing 143 µs, 423 I + 408 D a bag) | no: (b) fails on misses |
| 2 | Motor District heavy spot, night / day | 11.7 / 13.7 ms parked, 11.1 / 13.3 driving (R0H/R0J) | EE (60 fps held) | VU1 31 %, GIF 13 %, idle 50 % parked; idle 67 % driving | 0.007-0.029 ms | StaPip `core` 4.5-5.9 ms; scene code 2.5 ms | **core 61-73 % (I-cache 46-51 %), IPC 0.28** (measured R2-0, R0J + R0I); scene code 38-56 % | no: (b) fails on misses; R4-A lean path -0.22..-0.24 ms |
| 3 | physics: 72 awake crates (R2B_A1) | physics 7.76 -> 7.24 ms (2.11.1) | EE | - | - | physics: passes, world, pairs | **25 %** with the fitted prices (pI 51, pD 16 cycles; R2-0) | misses (b) yes; open: is ~17 k instructions per body per frame a defect? pile after a crash not re-measured |
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

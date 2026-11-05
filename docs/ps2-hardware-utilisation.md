# How TyraX uses the PS2's hardware, and what a rebuild could still buy

This page records a research pass of 2026-11-05. It asked where the engine
could be rebuilt to use the PS2 better, rather than tuned. It maps what each
unit does in a gameplay frame today, the three directions that pass measured
on a physical PS2, and the larger projects that remain, each with the
measured or bounded size of its prize. Earlier work is cited, not repeated:
docs/ee-submission-rearchitecture.md, docs/tyrax2-architecture-audit-2026-10-20.md,
docs/tyrax2-night-ee-2026-10-12.md, docs/stapip-stamps.md,
docs/stapip-bag-cost.md and docs/tyrax2-spr-staging.md hold the experiments
behind most rows here.

## What each unit does in a frame today

| Unit | Use in a gameplay frame | Idle or wasted |
|---|---|---|
| EE core | Game logic, physics, vehicles, particles, pose evaluation, scene recording, every StaPip bag (bounds, prepare, package classification, packet build), the frame recorder | The bottleneck. A third to a half of its StaPip time is cache misses (docs/stapip-bag-cost.md). Every wait is a busy spin. |
| VU0 | Macro mode only: `Vec4`/`M4x4`, bbox math, skinning, all synchronous on the EE thread | Micro mode is used only by the ray-tracer kernel (start and wait). No asynchronous VU0 job exists. |
| VU1 | Every StaPip program (cull, clip, as-is, billboard), double-buffered package halves | 44-51 % idle in the night district (1.176.3, `--gputl`). The crate field never makes the EE wait. A FLUSHE per bag drains it, but the bubbles were priced at ~0.1 ms. |
| GS | Draws, post-fx blits, the hybrid present copy (a 32 -> 16-bit pass per present) | Fill is not the limiter in the EE-bound scenes: a 1-pixel scissor saved 0.68-2.62 ms in September. |
| VIF1 / DMA ch1 | The whole frame as recorded chains (frame pipeline), polled queue | |
| GIF / PATH3 | Uploads only outside recording. In the pipelined frame, texture uploads ride VIF1 as DIRECT, so they no longer overlap VU1 | |
| VIF0, SPR DMA (ch8/9), IPU, MFIFO | Unused | The 16 KB scratchpad at 0x70000000 is unused too. |
| IOP | audsrv, pads, host I/O | Idle-polling audio was already fixed (+0.5 ms). |

The frame is EE-bound in every gameplay scene measured except the showcase
(GPU-bound at 20 ms) and the parked district before road bands (VU1-bound).
The EE is itself memory-bound.

## Measured in this pass

All runs on a physical PS2, same ELF toggled at boot, two runs an arm, rig
`C:\tx\qp`.

### The frame recorder pays for itself

The pipelined frame copies every VIF1 submit into a frame bank and
re-encodes it as REF records. That is EE work no other path does, so it was
priced by turning the pipeline off at boot (crate-field drive, arm 1028 vs 4):

| | work p50 (KF) | FRAMETIME mean | packet sends, ms a frame | missed frames |
|---|---:|---:|---:|---:|
| Pipeline on | 6.63 / 6.08 ms | 5.69 / 5.49 ms | 0.65 / 0.66 | 8 / 1 |
| Pipeline off | 8.43 / 8.51 ms | 7.47 / 6.67 ms | 2.51 / 2.44 | 60 / 51 |

Without the recorder every send waits on the DMA, which costs four times as
much. The copy and re-encode are cheaper than the overlap they buy, so there
is no prize in removing them.

### VU1 clipping instead of EE classification has a small ceiling here

A crossing bag (one whose box crosses the frustum) costs 142-150 µs on the EE,
most of it in deciding per package whether to drop, cull or clip
(docs/stapip-bag-cost.md). A VU1 program that clips only the triangles that
need it would let every such bag take the whole-replay route instead (~86-99
µs). The ceiling probe (arm 2048, `C:\tx\qp\engine_gbwide.py`) routed every
crossing bag whose box is inside the near and far planes straight to the
direct route. It accepts holes where triangles cross the lateral guard band.

- Only 2.9-3.3 of the 13.4-13.8 crossing bags a frame qualified (partial
  13.4-13.7 -> 10.4-10.9 a frame, whole replay 3.6 -> 6.9-7.0). The rest
  cross the near or far plane, which needs real clipping.
- StaPip a frame: 4.42 / 4.28 ms against 4.40 / 4.43 ms, within noise.
- Work moved within noise: KF p50 +0.1..+0.6 ms, FRAMETIME mean -0.1..-0.5 ms.

The full version, which also routes the near-plane crossers, is bounded by
13.5 bags x (142 - 99) µs ≈ 0.6 ms a frame in the crate field. The night
district's crossing bags are larger (about 13 bags at 316-378 µs against a
105 µs whole replay, docs/tyrax2-night-ee-2026-10-12.md), so the same rebuild
is bounded there by ~2.5-3 ms. That figure is estimated, not measured.

What the rebuild needs:
- A clip program whose accepted triangles run at cull-program speed. Today an
  accepted triangle still goes through the staged fan emit at ~184 cycles
  against 73.
- A per-package trivial reject.
- Strip windows that do not transform each corner three times.

R5 in docs/tyrax2-arch-audit-ee-submission-2026-10-20.md is the measured
warning: the same route through today's clip program cost +0.6 / +2.5 ms. The
resident micro memory is also nearly full (1904-1950 of 2042 words in
production sets), so only the most-used variants could carry the new code.

### Skinning is not a cost in any shipped example

VU1 skinning, with the bind pose immutable on the EE and only a matrix palette
a frame, is the textbook PS2 split. The two-players example was driven with
both cats walking in circles and the title menu bypassed (`C:\tx\qp\TP2`).
Its whole frame work was 3.16 ms on average (2.0-3.5 ms windows, PAL 50 Hz).
Its parked title screen costs 9.3 ms. The earlier spider bench priced EE
skinning at ~0.9 ms per 1092-vertex instance
(docs/dynpip-guard-band-cost.md), so the rebuild only pays in a game with
several animated characters on screen. No example has that scene.

### Fewer bags: who draws solo, and two batching knobs

A census at the object loop's solo draw (`C:\tx\qp\patch_solocen.py`)
counted the bags drawn solo in the crate-field drive. Per frame:
- ~9.5 are **non-physics** objects that static batching leaves solo. The
  game's own log says eligible 218, solo 30, and 11 multi-part models kept
  solo because one of their parts had no partner.
- ~5.4 are awake bodies demoted from their batch.
- None are sleeping bodies waiting to rejoin.

So the earlier reading that the ~20 stamped solo bags were "woken crates"
was wrong.

- **Rejoin after 0.5 s instead of 3 s** (arm 4096, `patch_rejoin.py`): solo
  stamped bags over the window 20 757 / 20 853 -> 20 288 / 20 216, work p50
  7.27 / 7.28 -> 7.44 / 7.36 ms. No change. Asleep bodies are not what draws
  solo.
- **A smaller base grouping cell** (`patch_cell2.py`):

  | cell | objects batched | crossing bags a frame | StaPip ms a frame | misses / 1000 |
  |---|---:|---:|---:|---:|
  | 80 (default) | 188 in 49 batches | 13.3-13.8 | 4.40 / 4.48 | 27 / 31 |
  | 47 | 158 in 36 | 11.1-11.4 | 4.27 / 4.25 (and 4.25 in a second build) | 17 / 18 |
  | 31 | 145 in 25 | 11.2 | 4.20 / 4.26 | 19 / 23 |

  A smaller cell trades batched members (30 -> 60-73 solo) for fewer
  batches crossing the frustum, and comes out ~0.15-0.2 ms ahead in this
  scene. One 47-cell run took a different drive (69.6 stamps a frame, 5.13
  ms) and is left out. Shipped in 2.10.0 as a project setting (*Batch
  grouping cell*, docs/static-batching.md). The automatic default is
  unchanged: measured on the shipped Motor District, cell 47 is neutral to
  slightly worse (StaPip -0.05 ms, p95 +0.2-0.5 ms, missed frames 4/5 ->
  6/13 of 1000).
- **Batching the parts of a multi-part model that do have partners** (the 11
  models kept solo) was not built. Batch membership is per object in eight
  places (physics demotion, shadows, reflections, the solo path), and the
  prize is under ~0.15 ms here.

### Physics: two solver lanes on the FPU do not pay

With body holding off (`PHYS_HOLD_AFTER` huge), 72 awake boxes resting in the
crate field cost about 8 ms of physics a frame on the console (rig `PL1`, 288
contacts):
- prep 1.09 ms;
- world 1.91 ms, of which terrain samples 0.77;
- pairs 1.30 ms;
- solve 3.05 ms, of which setup 1.06;
- write 0.60 ms;
- broad-phase refresh 0.80 ms.

That is about 110 µs per awake body. The velocity and position passes take
about 2.0 ms for 1 728 contact passes, ~340 cycles each, for ~165 instructions:
an IPC of about 0.5.

The guess was FPU latency: one contact is one dependency chain on an in-order
FPU. So a two-lane solve was built (branch `claude/physics-solver-lanes`,
local only). It advances two islands in lockstep, with both lanes' reads and
arithmetic before either lane's writes. Same ELF, toggled at boot, two runs
each: solve 3.027 / 3.027 ms against 3.042 / 3.043 ms, -0.5 %. The latency
guess is wrong.

The byte-for-byte check (`PHYS_SOLVE_LANES_VERIFY`) also failed in ~99 % of
frames. The compiler contracts multiply-adds differently in the two
instruction streams, so a rearranged solver is not bit-identical even when
its arithmetic is. Not kept.

With the two VU0 solvers (macro and micro mode, both slower,
docs/physics.md), this closes the easy rearrangements of the passes. What a
pile of awake bodies costs is spread over six stages of comparable size, not
concentrated in the solver.

## What is left, by size of prize

| Project | Measured or bounded prize | Status, blockers |
|---|---|---|
| Physics of awake bodies | 82 awake boxes = 8.6 ms (solve 4.1) in the crate field; 10 awake = 1.7 ms | The largest single EE cost measured in vehicle-playground. Owned by the physics work (docs/physics.md). A VU0 micro solver was slower overall. bpRefresh is the next lever. |
| VU1 clips only what needs it (above) | ≤0.6 ms crate field, ~2.5-3 ms night district (estimate) | Needs a new clip family with a fast accept path, per-package reject and strip windows. Micro memory is the blocker. |
| Stage-major StaPip | ~0.8 ms a frame crate field (docs/stapip-bag-cost.md) | A rewrite of the core path for ~5 % of the frame. |
| Fewer bags (batching coverage) | the per-project cell shipped (-0.15..-0.2 ms crate field) | The automatic default stays (the district is neutral to worse at 47); woken bodies are only ~5 bags a frame. |
| Hiding D-misses with the scratchpad (DMA the next bags' heads into SPR) | ≤ about half of the stamp path's ~14 µs of D-miss a bag, ~0.3-0.7 ms | Pointer chasing makes the gather list data-dependent. The earlier SPR prefix was layered on the snapshot path and lost 0.18-0.39 ms (docs/tyrax2-spr-call-runtime-2026-10-04.md). |
| VU1 skinning | ~0.9 ms per 1092-vertex character | Pays only in a game with several animated characters. |
| Asynchronous VU0 jobs | Unmeasured | Needs an ownership protocol for VU0 (docs/tyrax2-ee-vu0.md); no large independent vector job is waiting for it. |

Not worth rebuilding, by measurement:
- the frame recorder (above);
- FLUSHE bubbles (~0.1 ms);
- interrupt-driven VIF1 (crashes a real PS2);
- uncached or UCAB packets (+7.55 ms, or no boot);
- code layout and -Os;
- GS fill in the EE-bound scenes.

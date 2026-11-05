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

## What is left, by size of prize

| Project | Measured or bounded prize | Status, blockers |
|---|---|---|
| Physics of awake bodies | 82 awake boxes = 8.6 ms (solve 4.1) in the crate field; 10 awake = 1.7 ms | The largest single EE cost measured in vehicle-playground. Owned by the physics work (docs/physics.md). A VU0 micro solver was slower overall. bpRefresh is the next lever. |
| VU1 clips only what needs it (above) | ≤0.6 ms crate field, ~2.5-3 ms night district (estimate) | Needs a new clip family with a fast accept path, per-package reject and strip windows. Micro memory is the blocker. |
| Stage-major StaPip | ~0.8 ms a frame crate field (docs/stapip-bag-cost.md) | A rewrite of the core path for ~5 % of the frame. |
| Fewer bags (batching, instancing of woken bodies) | 20 woken crates ≈ 0.64 ms | Gameplay and batching decision (docs/stapip-bag-cost.md). |
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

# A16 EE preparation corrections

This checkpoint turns A14/A15 source findings into isolated correctness gates
and a matched scene measurement, without changing authored content or claiming
that all preparation work should disappear from the EE.

## Scope and current status

The user approved correcting the observations and measuring again. The revised
objective is in the investigation register. The app goal API rejected objective
replacement because the old paused goal is unfinished; it cannot edit/resume
that goal. No false completion or shutdown is authorized.

Production change (1.173.4): `StaPipCore::render` applies current-bag `setInfo` before
`sendObjectData` serializes TEX1. The reflection-target gate is private only;
guard-band routing, ownership, synchronization and clip budgets are unchanged.
Visible-consumer gating and terrain companion-pass removal are NOT implemented:
their semantics still require separate evidence, not blanket suppression.

## Filter order

`packet/` preserves a source-bound EE ELF and 18 hidden-emulator cases: three
shapes (inside, near crossing, side crossing), batch scopes off/on, and
fixed/legacy/fixed ordering. Six submissions per frame alternate and repeat
nearest/linear; four frames per case exercise cold writers and warm replay.
The observer reads the copied OPTIONS qwords in the actual packet, not only the
input settings. There are 432 checks: 288 corrected checks with zero errors,
and 144 legacy-control checks containing 96 wrong TEX1 filters. Actual clip
and whole-baked-replay witnesses are positive; six before/after raster pairs
are exact and nonblank. Batch scopes are exercised; no separate physical
filter-only raster run or performance benefit is claimed.

ELF: `f5f1fd841ed04014429d9e385358ec242b3740b7d6d7cd7372ac54d2a3713346`.
The native binding verifies 398 source files. Preparation V1 was stopped while
unnecessarily copying vendor assets; V2 failed an ambiguous enabled/disabled
function anchor. Neither was built or launched. V3 is the completed fixture.

## Conservative reflection-target rejection

The private gate transforms four expanded target side half-spaces using the
actual MVP, fixed VU raster scale 2048 and target size. It retains one pixel
beyond the scissor plus a floating-point margin; invalid/nonfinite bounds or
matrix values conservatively remain accepted. It operates only on precise
bounded, ordinary MVP, non-billboard, built-in VU-clipped submissions during an
env view. It runs after MVP/bounds and before texture/light/uniform work.
Accepted bags keep their original classification and guard-band routing.
Pop and portal transitions disable the private target gate.

Host ASan/UBSan checks 200,000 random matrices/boxes against double-precision
eight-corner half-space evaluation, plus four edge/invalid-input cases:
157,062 rejects and 42,938 accepts in the random population. This is geometric
safety evidence, not a scene price or a proof for arbitrary custom shaders.

`target/` preserves 18 off/on/off raster cases covering four target edges,
near crossing, behind-eye/far input, a large crossing surface, changed model
and rotated capture camera. All 36 env/main readbacks are nonblank and equal
within their 12 off/on/off groups on BOTH hidden PCSX2 and physical PS2.
The candidate rejects four of ten fixture bags per frame (16 over four frames),
so the equality check does exercise actual omission. Main-view restore is
checked separately. Display-mode variants, textured strips and full-scene
moving raster equivalence remain outside this small raster fixture.

ELF: `406f6b5109136aaa3b3cff2d882b050a640c42294b57f0100e86bf422845076b`.
The native binding verifies 399 source files. Physical launch used the editor
protocol, successfully replacing the owned completed A13 server.

## Scene measurement

All six full-throttle day/night off/on/off trips complete on both emulator and
physical PS2. The A13 workload retains 66 road chunks, three cars, 18 texture
pins, live captures, normal LOD/clipping and approximately +/-60 degree camera
sweeps. Each trip exceeds 182 units. EE paint stays fixed in every arm and both
arms include the TEX1 fix. The 472-source binding and all 303 launch inputs
verify; ten malformed-data controls are rejected. The physical run contains
2,581 rows including 1,861 driving frames.

| Mood / gate | Mean work ms | p95 work ms | Entry/present Hz |
| --- | ---: | ---: | ---: |
| Day off before | 18.373 | 24.885 | 36.412 |
| Day on | 18.541 | 24.473 | 36.031 |
| Day off after | 18.584 | 25.014 | 35.470 |
| Night off before | 20.546 | 27.342 | 33.898 |
| Night on | 20.560 | 27.974 | 33.436 |
| Night off after | 20.624 | 27.548 | 33.448 |

Against the average bracketing controls, the candidate is 0.062769 ms slower
by day and 0.024484 ms faster at night. Equal-weight common 20-unit distance
bins give -0.020738/+0.009404 ms savings. The opposite signs and control spread
do not establish a useful gain; night p95/max also worsen. Candidate gate
counts are 180/1,367 rejected/tested by day and 357/1,989 at night, including
warm-up. Those are not per-sampled-frame counts or isolated EE costs.

Decision: DO NOT PROMOTE this target gate. Wider classification demonstrably
admits unnecessary submissions, but this bounded implementation does not
recover meaningful frame time on this workload. Do not conflate fewer bags
with a measured improvement or repeat arbitrary epsilon/guard-band tuning.
Any earlier producer-level rejection needs a new mechanism and ownership proof.

This prices only additional target rejection including its classifier cost,
not VU paint or the material-state fix. Both arms still link PAINT-capable
images inherited from A13; whole-image flag0 cost and common observers are
not isolated. Work is elapsed non-pacing time, not pure EE arithmetic. Neither
arm sustains 60 Hz. Do not compare the rates directly with A13's VU candidate.
Every sampled driving frame attempts at least one vehicle environment pass;
there is no observed zero-consumer-submission window here. This does not prove
visible pixels, but it gives no current justification for blanket capture
suppression in this chase workload.

ELF: `f897174fc1d053baccc59538f9068a133a2c2b4a1f4f203669337a36634d888d`.
The editor deployment protocol completed both physical launches without a
manual reset. The final owned file server (PID 16444) is intentionally retained;
the emulator/Xvfb processes were stopped. Authored full/no-roads map hashes
remain unchanged. No shutdown, public gate activation or goal-completion claim.

The larger objective remains open: qualify necessary capture consumers/passes
and upstream producer preparation before another structural change. Do not
remove shadow/material companion passes or protected copies merely because
they repeat geometry. The accepted change in this batch is filter correctness.

The archive preserves the completed live reports unchanged. Final reanalysis
reports at this directory's top level validate the frozen snapshots; the drive
console snapshot includes later idle log lines, so its final log hash differs
from the first live analysis without changing any measurements. Each fixture
has its own archive hash manifest. `tools/` preserves the actual staging and
analysis scripts. No editor executable was rebuilt for the metadata-only version
bump; the engine/game native builds and target runs above verify runtime code.

## Reproduction

Use repository `tools/tyrax2-packet-audit.py` with the completed A10 disabled
fixture; build with the native backend and bind using `tyrax2-motion-run.py
verify ... --symbol PacketAudit::checks`. The target equivalent is
`tyrax2-target-audit.py`, with `--symbol TargetAudit::rejected`; `--drive` uses
the completed A13 forward fixture. The packet/target analyzer validates actual
outputs; the drive analyzer additionally requires complete travel, live
captures, authored roads/lights/pins, fixed EE paint and matched launch identity.
Only the physical drive analyzer reports timing. Use fresh destinations and
the documented hidden emulator/editor-equivalent PS2 launch protocol.

# TyraX2 EE preparation and VU0 audit

This audit separates current submission costs from consumer completion and identifies which remaining EE work could benefit from fewer passes or VU0 arithmetic.

## Physical night diagnostic, 2026-10-02

One private instrumented build used the corrected 1.169.1 stationary night vehicle fixture. Existing wait, cache, snapshot and conversion brackets were timed with COP0. A read-only FINISH observer sampled every eighth submission and before the existing completion fence; it introduced no new wait, bit clear, interrupt or GPU hold. Native build and PCSX2 reached frame 1700 before a fresh physical PS2 boot.

Ten neighboring 50-frame summaries (frames 1100 through 1550) each contain 50 completion samples and zero missed samples:

| Bracket | Mean ms/frame |
| --- | ---: |
| Existing VIF completion wait | 0.001234 |
| Existing FINISH wait | 0.001583 |
| Existing queue cache flush | 0.008730 |
| Source-chain snapshot | 2.723882 |
| Native-chain conversion | 0.742786 |
| Entire VIF1 submission | 3.665426 |
| Combined consumer completion observation, upper bound | 12.779332 |

Snapshot and conversion are **inside** submission; do not add them to its total. The exact 512 raw renderer-work samples, frames 1100–1611, average 18.305820 ms (p95 18.981954 ms), with all samples above 16.667 ms. Renderer work excludes presentation pacing; this diagnostic is not a new ordinary performance benchmark or a delivered-FPS result.

Nearly zero explicit waits point toward EE preparation on the observed critical path. They do not establish idle VU1/GS: the consumer runs concurrently, and the 12.779332 ms first-observation upper bound covers DMA/VIF/VU1/GIF/GS together. It is neither VU-only time nor utilization. Current cache timing also means the historical approximately 1.09 ms flush-removal estimate cannot be reused for this runtime and pose.

The [machine record](tyrax2-pipeline-attribution-2026-10-02.json) preserves exact windows, source/ELF/frozen-log hashes, preparation/analysis scripts, build/emulator evidence hashes and the full read-only VU0 audit. The owned ps2client remains serving the healthy night game; freezing evidence does not stop host-file service.

## VU0 already has owners

The local engine uses VU0 macro instructions for Vec4 arithmetic, matrix/camera transforms, plane helpers, some bounding-box reductions and skeletal skinning. Static source use does not yield a utilization percentage, but rules out treating VU0 as an unclaimed second CPU. This fixture has no authored custom VU0 kernels or mirrors; those optional micro-mode entry paths are absent, while macro use remains.

Skinning retains vector registers across assembly blocks and explicitly forbids helper calls between them. Existing raytracer and generated compute drivers execute synchronously. Their instance-local uploaded flags are not a shared micro-image ownership token: a new dispatcher must reload the correct program when switching owners and prohibit overlapping macro use. Asynchronous micro work therefore needs an explicit register/program/data/channel ownership contract before it can be considered safe.

## Ranked follow-up

1. Split the 2.724 ms snapshot into preflight, chain copy, mutable REF copy and fixups; split native conversion into decoding and writer stores. Count bytes, tags and resolver queries before choosing SPR or direct producer recording. The aligned copy already uses EE 128-bit LQ/SQ; pointer, tag and lease work is not vector floating-point arithmetic.
2. Price the narrow duplicate lookup in ImmutableSpanTable::borrow. A successful borrow currently searches the same address twice; two snapshot passes imply four searches per successful immutable REF. Removing one search per borrow preserves both validation passes and leases. Fewer searches are source evidence; hardware frame gain is still unmeasured.
3. Measure exclusive scalar object-space plane transforms, AABB planes visited and dynamic-light selection, including cache misses and candidate counts. Existing Vec4/matrix helpers already use macro mode; avoid moving work that is already there or skipped by caches.
4. If a math block is material, first compare a synchronous macro implementation with exact boundary/classification controls. Only then price micro-mode identity transfer/setup and a shared owner dispatcher. Include packing, launch, waits and readback in its cost.

The retained beam-state shortcut remains a separate private candidate. Preserve moving cameras/lights, visibility, levels and portal views and compare outputs before quiet physical timing. No VU0 rewrite, production night optimization or additional FPS gain is accepted by this audit.

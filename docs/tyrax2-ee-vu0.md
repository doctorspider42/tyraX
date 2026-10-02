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

## One-search borrowing trial, physical PS2

A narrow private variant removes the second lowerBound inside a successful ImmutableSpanTable::borrow. Public contains, both snapshot passes, lookup selection, retirement and reader-bank mutation remain unchanged. A differential oracle extracted the actual baseline and candidate classes: 55,163 operations / 38,018,254 assertions passed, including 50,000 randomized operations and range/mask/full-table/two-bank boundaries. A host count control gives 2,000 versus 1,000 searches for 1,000 successful borrows; this is not timing evidence.

The integrated candidate passed the existing 1,074 arena cases plus 10,000 malformed streams and 43 writer cases. Native build and PCSX2 2.9.93 passed all three phases, two scene reloads and 6,600 gameplay frames. A fresh physical boot ran baseline/candidate/baseline in the same ELF, with a shared runtime branch and no new profiling scopes. Each 2,200-frame phase uses 1,100 warmup frames and an exact 512-frame raw work window; neighboring period summaries remain separate.

| Variant | Mean renderer work (ms) | Delivered rate from period (Hz) |
| --- | ---: | ---: |
| Baseline before | 18.468173 | 29.94 |
| One-search borrowing | 18.435447 | 29.94 |
| Baseline restored | 18.556225 | 29.94 |

Apparent savings are 0.032727–0.120778 ms, while the controls themselves differ by 0.088052 ms. One sequential boot does not distinguish a stable small gain from control variation; no production integration or FPS improvement is accepted. The candidate remains private. Prioritize the bounded snapshot/native breakdown rather than calling this enough to restore the night frame budget. The [trial record](tyrax2-immutable-borrow-2026-10-02.json) preserves exact windows, patch, tests, preparation/analysis scripts and hashes. Its frozen log does not stop the owned client serving the restored baseline.

## Bounded snapshot/native breakdown, physical PS2

A private three-phase same-ELF diagnostic toggles aggregate COP0 clocks/counters off/on/off. Both host macro variants pass 10,047 actual-class callback/state differential cases, including partial writes on failure and unstable second-pass resolution. The actual fixture headers also pass the existing arena/writer harnesses; native compilation, PCSX2 and physical PS2 complete all 6,600 gameplay frames and both scene reloads. No new DMA wait, allocation, FINISH read/clear or GPU hold is introduced by the probe.

Warm exact 512-frame renderer-work windows give **19.164984 / 19.596451 / 19.344394 ms**, off/on/off. Enabling the observer changes work by **+0.252057 to +0.431467 ms** against these controls, whose spread is 0.179410 ms. These are one-boot ranges, not confidence intervals. Runtime-disabled probes may still have compiled scaffolding overhead; pristine and compiled-out physical controls remain separate gates. This instrumented result is not a new production benchmark or accepted optimization.

Ten neighboring 50-frame enabled summaries give the following diagnostic brackets:

| Bracket | Mean ms/frame |
| --- | ---: |
| Entire snapshot | 3.312289 |
| Preflight tag/range/callback checks | 1.104756 |
| Full source-chain copy | 0.265598 |
| Second-pass fixups, excluding mutable copy | 0.776857 |
| Mutable REF copying | 1.056222 |
| Native sizing pass | 0.158458 |
| Native decode / writer emission | 0.515674 |

Entire snapshot includes preflight, chain copy, second-pass fixups and mutable copying. Fixups above subtract the nested mutable-copy child; never add the inclusive second pass and its child. Native sizing and emission are sibling adapter brackets, still inside submission. Timings include counter/clock overhead and are not a pure instruction or bandwidth measurement.

Per warm frame, the observed source path visits 1606.33 tags in each pass, calls the readable resolver 964.20 times and the immutable resolver 1086.19 times. It copies 56.16 KiB of original chains and 167.93 KiB of mutable REF payload, while borrowing 2100.61 KiB. Published copied qwords equal full-chain plus mutable-copy qwords; borrowed bytes are excluded rather than subtracted a second time. Enabled windows have matching preflight/second-pass tags and successful snapshot attempts; native sizing and emission visit equal tag counts. Disabled windows retain zero metrics.

The [machine record](tyrax2-snapshot-breakdown-2026-10-02.json) preserves exact raw/profile/count/period windows, observer overhead, source/ELF/frozen-log hashes, preparation/analysis scripts and probe/test evidence. No production behavior changed. Further source changes need quiet physical controls and existing rollback, capacity, callback and bank-lease gates.

### What this changes next

The measured preflight plus fixups total about 1.882 ms; mutable copying adds 1.056 ms, versus only 0.266 ms for full-chain copying. These are instrumented costs, not additive gain estimates. Runtime-disabled work also differs from earlier pristine ELF measurements; cross-boot/layout differences cannot all be assigned to the runtime gate. Price pristine/compiled-out controls before deriving a production saving from these brackets.

The source audit identifies two additional scalar-math seams to census: a partial guard-band bag can transform the same eight clip planes twice in one render call; local spot preparation performs affine inverse and normalization inside sendObjectData, beyond the existing prepLight bracket. Measure activation/cache misses and exclusive cost before reusing results or writing a VU0 kernel. Existing object-cost export changes batching with per-object fences, and some TYRA_STAPIP_ATTRIB counters ignore the runtime telemetry gate, so those modes cannot silently stand in for an ordinary pipeline control. The full read-only census plan and source hashes are retained in the machine record.

# V10 batch / texture / program measurement

Completed V10 evidence isolates the inclusive range immediately before `attribTextureStart` through immediately before `attribLightStart` in `StaPipCore::render`. Earlier empty/outside-frustum returns do not enter this range. It includes existing batch/texture/program preparation, dispatch and wait seams; it is not pure EE cost.

| Environment / order / On phase | B mean (ms) | B median (ms) | B p95 (ms) | Scene mean (ms) | Work calls/frame |
|---|---:|---:|---:|---:|---:|
| emulator / 0 / 1 | 0.136877 | 0.124644 | 0.172319 | 7.422684 | 134 |
| emulator / 1 / 0 | 0.137406 | 0.124644 | 0.171875 | 7.417938 | 134 |
| emulator / 1 / 2 | 0.135257 | 0.124644 | 0.159203 | 7.422793 | 134 |
| ps2 / 0 / 1 | 0.788060 | 0.775308 | 0.918488 | 17.929322 | 134 |
| ps2 / 1 / 0 | 0.751168 | 0.738625 | 0.881805 | 17.988213 | 134 |
| ps2 / 1 / 2 | 0.785856 | 0.793048 | 0.904626 | 17.842801 | 134 |

The first physical On window measured B at 0.788060 ms. Every accepted run completed 5400 loops, 384 common scope records, 15 chunks, 135 Core calls/frame and 49794 vertices/frame. Actual Work counts are reported from raw records; 134 is an observation, not a configured assertion. Three On windows per environment exclude warm frames and each contain 128 samples.

| Environment / order | Own sampler On-minus-Off contrasts (ms per tax loop) | Outer spread (ms) |
|---|---|---:|
| emulator / 0 | +0.019533, +0.004615 | 0.014917 |
| emulator / 1 | -0.012308, +0.002265 | 0.014573 |
| ps2 / 0 | +0.064740, +0.035904 | 0.028836 |
| ps2 / 1 | +0.301064, +0.153126 | 0.147939 |

These contrasts use the same V10 ELF/source and 320-loop chunk windows. They include observer overhead and induced timing/selector response. No per-sample correction or uniform subtraction is applied. V8 prefix and V9 submit measurements are not subtracted or combined with B; their inclusive wait/dispatch scopes and separate executions do not establish an additive partition. Emulator timings remain emulator observations and are not physical PS2 prices.

Accepted stems: `minimal-scope-v10-batch-emulator0-attempt1`, `minimal-scope-v10-batch-emulator1-attempt2`, `minimal-scope-v10-batch-ps2-0-attempt3`, `minimal-scope-v10-batch-ps2-1`. Original concurrent emulator/PS2 attempt and no-network attempt are rejected and excluded; their evidence remains preserved. Root reported viewing normal rasters; this report pins available raster files without claiming an independent visual review.


The [compact machine record](tyrax2-batch-texture-scopes-2026-10-04.json) pins completed source, ELF, parsers and evidence closures. Existing pacing is a whole-loop counter; Scene minus B is an inclusive elapsed remainder, not an attribution to GPU or EE. Subsequent prototype runtime results are qualified separately in the [SPR/CALL report](tyrax2-spr-call-runtime-2026-10-04.md).

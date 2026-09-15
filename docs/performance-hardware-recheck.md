# Full-asset PS2 performance recheck

This independent recheck corrects incomplete test deployments used by the
transform-cache and DMA clip-table experiments on September 14, 2026.

## Invalid earlier fixtures

The agent fixtures had only `sfx` and `vehicles` directories in `bin`. Against
the complete baked reference they lacked 66 PNG files, 11 TMDL models and 18
MTL files, among other resources. Missing PNGs produce placeholder textures;
missing models can be skipped. Identical geometry counts between two equally
incomplete fixtures do not establish equivalence to the intended scene.

The earlier PCSX2 conclusions are therefore not accepted as performance
evidence for the full Motor District scene. Their source experiments remain
available for this corrected comparison; neither experimental patch is in the
production engine.

## Controlled setup

The coordinator rebuilt baseline, transform-cache and DMA candidates from the
same instrumented generated game and the same complete `.res-baked` tree.
The three deployments match all 105 PNG/TMDL/MTL files byte-for-byte. Engine
source comparison permits only the two `stapip_core` files for the transform
candidate and the two `stapip_qbuffer_renderer` files for the DMA candidate.
The baseline includes the retained indexed bounds cache.

All runs use physical PS2, native PAL 512x512 32bpp, no BLSS, parked traffic,
automatic devkit cadence and four identical garage/outer-road day/night views.
Each run records 240 warmed frames per view and eight rolling FPS samples.
Builds finish before timing. Fresh output files and advancing snapshot frame
ids establish a new run. Captures are requested only after sampling ends.

Work means update + submission + finish, excluding presentation wait and the
engine's outer pad/info work. Included pipeline buckets overlap; neither their
sum nor a serialized render capture is a whole-frame FPS estimate.

## Results

Each arm has 960 raw frame rows (240 per phase); baseline was repeated with the identical ELF.

| View | Baseline work ms | Repeat ms | Transform ms | DMA ms |
|---|---:|---:|---:|---:|
| garage-day | 46.35 | 46.09 | 46.40 | 46.72 |
| garage-night | 53.54 | 53.41 | 53.47 | 54.00 |
| outer-day | 25.16 | 24.74 | 25.17 | 25.65 |
| outer-night | 28.64 | 28.30 | 28.47 | 28.97 |

Neither candidate demonstrates a useful net saving. The transform submission bracket is 0.10–0.19 ms slower than the first baseline in every view. DMA total work is 0.33–0.49 ms slower. Baseline repeat differs by 0.13–0.42 ms, so tiny apparent improvements cannot establish a win. Neither patch is integrated.

Baseline rolling FPS medians are 17.65 / 16.37 / 24.50 / 23.15; repeat medians are 16.96 / 16.04 / 28.31 / 23.15. In particular, outer-day rolling FPS varies despite only 0.42 ms difference in mean measured work. Do not use those sparse rolling medians to rank candidates.

Garage-day baseline: update 8.25 ms, render submission 37.08 ms, finish excluding presentation 1.02 ms, presentation wait 12.45 ms. Garage-night: 8.62 / 43.37 / 1.55 / 10.50 ms. VIF1 DMA wait inside submission is 7.79 / 7.62 ms. The counter named vu1WaitTicks actually brackets dma_channel_wait(VIF1); it does not measure VU1 arithmetic time. GIF waits and downstream backpressure need separate attribution. A small finish tail does not exonerate GS, which runs concurrently with submission.

All six candidate/repeat day-view screenshots are RGB-identical to the matching baseline. Garage and outer-road GS captures show complete building, vehicle, road and terrain textures. This is fixed-view evidence, not an exhaustive map traversal. Warm sampled frames record zero texture uploads/reuploads; that rules out warm texture churn in these views, not startup loading failures elsewhere.

[Raw CSVs, asset hashes, source-difference audit, summaries and screenshots](../examples/vehicle-playground/authoring/hardware-recheck-2026-09-14/) preserve the evidence. The reset console was left running the complete baseline-repeat deployment.

## Next diagnostic step (proposed, not implemented)

Capture a bounded RAM trace on physical hardware and export it after sampling: EE scopes, separate VIF1/GIF DMA waits, sampled VIF/GIF state, texture bytes, packet sizes, and the existing end-of-frame GS completion boundary. Display parallel timeline lanes, with measured waits distinguished from inferred downstream activity. Do not label DMA completion as VU1/GS completion or introduce per-draw FINISH barriers in normal timing. Measure instrumentation overhead with trace disabled/enabled.

Run isolated controls for raster size, cheapest VU shader with unchanged geometry, fixed-material texture work, expensive additional passes, and debugger host I/O. These are diagnostic substitutions, not proposed visual downgrades. Combine sensitivity with the trace before selecting a renderer rewrite.

# Physical PS2 static submission batching experiment

This archive records bounded native-chain batching trials in the complete Motor District fixture on September 14, 2026. See [the report](../../../../docs/static-submission-batching.md) for the final decision and measurements.

## Arms and controls

- `control`, `repeat`: identical baseline ELF, two fresh boots.
- `candidate`: four small untextured direct bags only; rejected as too few eligible draws.
- `textured`: resident REPEAT textures admitted; initial speedup before final safety review.
- `large`: large direct bags appended by 16-group halves; rejected after increased construction/wait time.
- `final`: bounded small bags with pipeline teardown drain and first override guard; includes a stress harness that starts after normal sampling.
- `production`: same engine as `final`, without the stress harness; isolates harness effects.
- `gated`: final conservative override flag and cheap eligibility before texture residency lookup.

Names are immutable experimental labels: `final` is the stress arm, not a declaration of the shipping decision.

Each timing CSV has 960 unique frames, 240 per pose, after 120 warm-up frames in each 360-frame phase. Pose order is garage day, garage night, outer day, outer night. Native PAL raster is 512x512, 32-bit; traffic is parked. All 105 deployed PNG/TMDL/MTL hashes must match `asset-manifest.json`. Geometry, material resources and additional passes are unchanged between arms. `total_ms` does not include every outer-loop service; the report uses update + submission + finish as measured work and does not convert that number into displayed FPS. The 32 HUD FPS samples per boot are retained separately and include presentation/host-service variability.

## Reproduction

The original working root was `D:/tyra-batching-0914`; scripts preserve that provenance. Adapt paths before using the archive on another machine. Build an isolated full-resource fixture using `tools/toolchain/native-build.ps1` and the same native PS2DEV cache/toolchain. Do not use `--build` on instrumented fixtures: it regenerates the measurement edits. `control/fixture` and `gated/fixture` contain the measured game source and parked-pose script. `final/fixture` additionally contains the post-measurement eviction/pipeline stress harness. `engine-baseline-to-final.patch` is relative to the saved dirty baseline, not git HEAD. `final-source` contains the candidate engine files; earlier source snapshots preserve rejected designs.

Use a single ps2client host process. `run-arm.ps1` verifies and rewrites `bin/ps2link.run` after build, resets the console, and starts execee from the exact fixture bin directory. Keep other builds, captures, emulators and bulk transfers off during the timing window. Only fresh frame progress and newly written CSVs prove a run happened. Each `run.json` records PID, boot time and ELF SHA-256.

Trace configuration is frame 1500, two frames, no state snapshots. Traces run after the timing window and must pass END/count/frame bounds and zero-dropped checks in `tools/hardware-trace.py`. The large arm retained outer-night pose 3 after frame 1440, so its trace is not comparable to the garage-day control/textured/repeat/gated traces. Its four-phase timing CSV is comparable. Capture poses only after measurement by writing 0..3 to `district-benchmark-pose.txt` and allowing the 30-frame polling interval to pass.

The stress arm evicts textures during the open Objects scope on frames 1600..1839, records whether eviction flushed pending bags, and switches through an empty pipeline every 20 frames. `batch-stress.txt` proves 240 evictions, 220 pending-batch flushes, 12 pipeline switches and 4588 total reuploads; subsequent captures and live frame progress verify survival. The final gate correction changes only eligibility/override lookup, not the tested packet or mutation-barrier implementation.

Night images are time-varying: one authored lamp (`District night lamp 7`) has flicker 0.025, and stars twinkle. The repeated baseline alone changes 9485 garage-night pixels by at most 3 channel levels. Day images should be pixel-identical. Compare repeated same-build images and localized deltas rather than requiring time-varying night images to match byte-for-byte.

Windows editor build and fresh code generation were verified; `codegen-check.txt` verifies the generated Objects block matches the checked-in example exactly. Linux, a full traffic-driving matrix and all custom-program scenes were not run in this experiment.

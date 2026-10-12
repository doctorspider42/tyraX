# troubleshoot/tyra-testing-perf.md

Render-cost capture, the full-asset gate, and the hardware timeline. Symptom first.

## Render-cost capture

**Cause:** a render-cost CSV must be comparable and the pipeline must be drained.
**Fix:** use `--profile-frame PROJECT -o report.csv` against a debug game with Live Debugger on and a live host server. Verify matching sequence/footer, finite phase/object values, repeated captures and CSV output. For UI changes drive Render cost / Measure render cost / Keep as baseline / Copy render cost CSV, and click the column headers: each sorts, and a third click on the same header clears the sort back to the original grouping. Object rows are nested costs inside Objects. Measurements drain the pipeline; verify ordinary FPS separately and capture the same camera on physical PS2. Row `Roads` is road/junction cull + submission built once at load (NOT generation), and `endFrame` is mostly vsync idle — do not read either as render work.

## An isolated native build is not an asset bake

**Cause:** a copy excluding bin and `.res-baked` must first materialize the complete baked resources, then add instrumentation and use `native-build` directly.
**Fix:** before timing, compare deployed PNG/TMDL/MTL files with the intended baked reference, reject missing/different resources, verify fresh frame IDs and inspect actual GS captures after the sampling window. A September 14 fixture with only sfx/vehicles directories lacked 66 PNGs and 11 TMDLs: placeholders and skipped models made matching baseline/candidate triangle counts falsely reassuring. See docs/performance-hardware-recheck.md. Empty redirected host logs or a failed ping alone do not prove a failed boot; check new telemetry.

## The GENERATED SOURCES are half of that gate

**Cause:** a fixture script that copies an example copies its **committed** `inc/*.gen.hpp`, `src/gen/` and `src/terrain_game.cpp`, and examples' generated files in this repo drift silently. Compiling that with `native-build` directly means measuring **a different game from the one the editor under test produces**, with nothing in any log saying so.
**Fix:** `--build` (or at least `--refresh-gen`) the fixture once before instrumenting it, and **check a counter against the previous known-good run before concluding anything from a change in it**. Measured on `examples/vehicle-playground`, 2026-09-15: a stale fixture reported **12.17 texture re-uploads per frame** in garage day and **3.6 ms more render submission** than the same scene regenerated with the editor being compared, which records 0.000 re-uploads and 0 evictions in all four poses.

## `--refresh-gen` IS NOT ENOUGH ON ITS OWN

**Cause:** the EDITOR BINARY can be older than HEAD — `build/tyrax-editor.exe` is a build artifact, not a property of the checkout, and `--refresh-gen` faithfully regenerates the fixture with the OLD baker while every log line says "regenerated". Measured 2026-09-16: the binary was built at 15:17 and the commit that raised the VU1 package ceiling landed at 17:07, so seven arms were baked with `stripRun = 72u` against a tree whose `meshstrip::kRun` reads 75.
**Fix:** check the things that are actually functions of the baker:

```bash
grep -E "ROADSTRIP|TERRAINSTRIP" <results>/host.log   # packages 470 / 588 verts, 8 pkgs at 75
grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp  # the baked constant itself
```

Compare the binary's mtime against the commit that last touched what you are measuring (`git log -1 --format=%ad <path>`), and rebuild the editor when in doubt. **The capture hash does not catch it** — the strip run changes how a surface is cut into runs, not which pixels it covers. A round whose arms all share one wrong fixture keeps its DELTAS but loses the right to be quoted against anyone else's absolute numbers.

## `--profile debug` leaves the LIVE TOOLS ON

**Cause:** two of their pollers run inside the instrumenter's `update` bracket — `livepad::tick` and `livedbg::tickFromLoop` `fopen` `livepad.bin` and `livedbg.cmd` over `host:` every frame.
**Fix:** measured on the same fixture, garage day: **4.79 ms of `update` and 6.44 ms of `work`**, which is why that control read 36.51 ms against a published 30.42. `benchmark-district.py --profile quiet-debug` is the same build profile with `liveLink`, `liveDebug`, `liveLogic`, `timeMachine`, `remotePad` and `inputRecorder` off; with them off the two tables agreed to 0.35 ms of `work` and 0.00 ms of `total`. **Say which profile a frame-time table was taken on** — and treat sporadic multi-millisecond `update` outliers as host-I/O contention rather than as a finding.

## Hardware timeline capture

**Cause:** scope totals overlap; VIF1 DMA wait is not VU1 execution, and VIF/GIF snapshots are not utilization.
**Fix:** use `tools/hardware-trace.py arm PROJECT` before a boot, then export the complete `bin/hardware-trace.csv` to HTML/Perfetto. The engine captures bounded RAM events without new drains and writes after sampling. Compare unarmed/armed controls and reject dropped or stale events. See docs/hardware-profiler.md for start-frame semantics and capture limits. For debug builds, inspect per-frame `Live_debug_poll` and `Live_debug_flush` before blaming gameplay update spikes. An unattached debugger should write its one boot marker but no periodic flushes; a valid command attaches it and resumes the configured report cadence. Vehicle projects expose `Vehicles_update`, `Vehicle_smoke_update` and `Vehicle_skids_update`; nested `Vehicle_sleep` events count parked instances that skipped the expensive ground/collider/suspension path.

## Native hardware timeline (1.92)

**Cause:** the offline exporter and the in-editor view share the same CSV.
**Fix:** `src/hardware_timeline.cpp` reads the same bounded CSV. Debugger > Hardware timeline arms the next boot and loads completed captures on demand, with frame selection, zoom, raw marker tooltips and inclusive totals. No browser, Python or extra debugger polling is required. Engine detail scopes separate package creation/classification, qbuffer copies and packet construction. See `docs/hardware-profiler.md`; use unarmed controls to rank performance.

## Hash the two ELFs, and the tooling that is gone

**Cause:** a null A/B is only evidence if the two arms were two builds; swapping one game source can leave identical ELFs behind a no-op `make`. Quality criteria can be turned into seconds-long scripts.
**Fix:** stamp the file and delete the matching `.o` in the project AND the `-Cache` tree, then `sha256sum` both ELFs (they MUST differ); `tools/instrument-frame-cost.py` rewrites the file itself, so that flow does not hit the stale-mtime trap. Removed benchmark tooling (recover with `git show 3e7b0f67a^:<path>`): `examples/vehicle-playground/authoring/benchmark-district.py` (sampler over `bin/district-benchmark-pose.txt` -> `bin/district-benchmark.csv`), `probe-visibility.ps1`, `motion-sampler.py`, `content-sampler.py`, `verify_scene_identity.py`, `stripcheck-wheels.cpp`, `build-arm.ps1`, and `road-lod-2026-09-16/terrain-lod-burial.py` / `terrain-lod-step.py` (worst coarse-ground rise, worst mesh disagreement in pixels).

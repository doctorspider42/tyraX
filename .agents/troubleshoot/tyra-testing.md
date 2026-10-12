# troubleshoot/tyra-testing.md

INDEX for the `tyra-testing` skill. Symptom first. Per-topic files hold the full entries.

| Symptom shape | Read |
|---|---|
| The editor build fails, a bake reads as a hang, a build times compare wrongly, the native incremental build regresses | `tyra-testing-build.md` |
| A headless verb (`--new`/`--build`/`--bake-*`/`--resave`/`--refresh-gen`) does nothing, refuses, or a migration is a no-op | `tyra-testing-cli.md` |
| A BLSS/upscaler number or decibel table cannot be reproduced, or a `--vu-check`/`--vu-replay` result is suspect | `tyra-testing-blss.md` |
| A codegen/template change needs checking without Docker; a menu stylesheet or `App` method needs a harness | `tyra-testing-codegen.md` |
| A PCSX2 boot/launch is wrong (black screen, wrong scene, missing assets), a gym fails, the toolchain image is suspect | `tyra-testing-e2e.md` |
| A pad script runs and the game ignores it, a hold does nothing, the arg is mangled, or you need the unattended recipe / input replay | `tyra-testing-pad.md` |
| A screenshot is black, captures the wrong window, or you must watch the game over time / on Wayland / via savestates | `tyra-testing-capture.md` |
| You must drive the editor's UI, reach a widget, or capture the editor's own framebuffer/viewport | `tyra-testing-ui-script.md` |
| A pixel/visual/timing comparison proves nothing, or a frame time is suspect | `tyra-testing-measure.md` |
| You need to find what the owner is debugging, or the console/devkit/ps2client channel is dead | `tyra-testing-debug-state.md` |
| You need a scripted probe for flow-graph, VU1, crash, release audit, log panels, audio or multiplayer | `tyra-testing-probes.md` |
| A fault exists only while the picture is MOVING | `tyra-testing-motion-gate.md` |
| A corruption appears only on real PS2 hardware and only in some frames | `tyra-testing-flicker.md` |
| A dynamic-shadow switch needs an A/B or a walk | `tyra-testing-shadow-ab.md` |
| An AI Assistant layer (docs search, prompt, parser, whole loop) needs checking | `tyra-testing-ai.md` |
| A package, installer or the update check needs verifying | `tyra-testing-packages.md` |
| Physics energy/hull, or a particle texture/emitter needs checking | `tyra-testing-simulation.md` |
| Animated directional GI or RGB SH needs verifying | `tyra-testing-gi.md` |
| An impostor data path, eight-view capture, picking or GPU bake needs verifying | `tyra-testing-impostors.md` |
| Vehicle damage/contact, vehicle defaults, chase camera or sound import needs verifying | `tyra-testing-vehicles.md` |
| Roads, junctions or hull/reflection proxies need verifying | `tyra-testing-roads.md` |
| Triangle strips, the road/terrain twins, or the road-index/static-batch oracles | `tyra-testing-strips.md` |
| A codegen/CONTENT change where the arm is the editor | `tyra-testing-content.md` |
| Render-cost capture, the full-asset gate, or the hardware timeline | `tyra-testing-perf.md` |
| Retained static commands, content version, baked VIF stream, static submission batching | `tyra-testing-stapip.md` |
| Pricing a culling/batching change, bbox counters, or proving collisions unchanged | `tyra-testing-culling.md` |
| Judging a POP, or a skip-when-unchanged change | `tyra-testing-skip.md` |
| Exact first-entry HUD or hybrid triple-buffer acceptance | `tyra-testing-hud.md` |
| Procedural placement, a frozen procedural bake, or scene object data iteration | `tyra-testing-procedural.md` |
| The console frame is wrong because a debug build is not the player's frame; ps2link launch/cadence | `tyra-testing-ps2link.md` |
| Submitted geometry may not be reaching the screen | `tyra-testing-visibility.md` |

---

## Highest-hit entries

### A boot produces a black window and `bin/log.txt` never appears

**Cause:** the ELF path is over ~145 characters (PCSX2's host: loader gives up) or the `-elf` path is relative and PCSX2 rebased it below the ELF directory.
**Fix:** keep the fixture path short (`%TEMP%\tyra-editor-test\<name>` on Windows, `~/tyra-projects/<name>` on Linux) and use an absolute `-elf`/`-logfile` (`[IO.Path]::GetFullPath()`). See `tyra-testing-e2e.md`.

### A capture/verification run "proves nothing"

**Cause:** the camera was frozen and the fault lives in motion; or a GDI grab read the screen; or the fixture is stale; or an A/B compared two runs whose phases differ.
**Fix:** add the motion gate; capture with `-PrintWindow`; regenerate the fixture with a freshly built editor; compare DISTRIBUTIONS per leg, not capture k against capture k. See `tyra-testing-measure.md` and `tyra-testing-motion-gate.md`.

### A pad script runs clean and the game ignores it

**Cause:** the poller was not compiled in, the wrong `bin/livepad.bin` is being written, a `hold` had no `wait`, or `Start-Process -ArgumentList` mangled the script.
**Fix:** check `src/gen/live_pad.gen.cpp`, rebuild after a preference change, confirm the `-elf` path, and read the driver's stderr/exit code. See `tyra-testing-pad.md`.

### A build fails on a missing `vendor/` header

**Cause:** on Windows `build` runs `build.cmd` (PATHEXT), and the dependency-fetch guard is in `build.ps1`; or a dependency is not pinned/fetched.
**Fix:** check which script ran; add dependencies to BOTH `deps.ps1` and `deps.sh`; re-run setup. See `tyra-testing-build.md`.

### My migration does nothing

**Cause:** a step whose `kFormatVersion` bump you forgot produces NO steps to run.
**Fix:** read the `BROKEN MIGRATION REGISTRY` stderr line; use `--migrate`. See `tyra-testing-cli.md`.

### A green `--vu-check` that cannot be made red

**Cause:** the check stages VU1 memory itself, so a term evaluating to zero is compared by comparing nothing.
**Fix:** falsify it first — invert the branch or zero the term in the HANDWRITTEN program and confirm `DIFFERENT`. See `tyra-testing-blss.md`.

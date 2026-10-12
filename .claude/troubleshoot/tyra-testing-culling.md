# troubleshoot/tyra-testing-culling.md

Pricing a culling/batching change in PCSX2, live bbox counters, and proving collisions are unchanged. Symptom first.

## Pricing a CULLING change in PCSX2: counts, plus a batching-OFF third arm

**Cause:** PCSX2 emulates no EE data cache, so its times mislead, while its **counts and its pixels are exact**.
**Fix:** the recipe that priced the static-batch cell (1.102.1, docs/model-pipeline.md):
- **The counters are two log lines, not an instrumented loop.** Set `TYRA_FRAME_PROFILE` to 1 in `vendor/tyra/engine/inc/debug/frame_profile.hpp` (**identical in every arm, and reverted before committing**); the game prints `FTCLIP` every 50 frames: package and triangle counts per clipper route, `flush`, `verts` and `out`. Divide by 50 for per-frame; total VU1 packages is `cull + clip + guard + out`. The generated game prints `Static batching:` at scene load for the batched/solo split.
- **RUN A BATCHING-OFF THIRD ARM, and make it the reference.** Two arms tell you a change moved; they cannot tell you which one is *right*. `staticBatching: false` in the `.tyra` renders the ground truth.
- **A steady count is not a steady picture.** Check both gates separately. The adversarial fixture had counts identical to the digit across four consecutive `FTCLIP` windows in both arms while its *captures* differed by 283-419 px **within one arm**.
- **Watch for the DOMINATED case.** Batching on a big map was worse on both axes at once (more triangles *and* more packet flushes). With the cell fixed the same feature became a pure win on one map and a genuine trade (+23.5% triangles, -43% flushes) on another.
- Usual hygiene: short paths outside the repo, one PCSX2 launched per arm by hand, `frame.tga` deleted between `--capture-frame` calls, and a DAY pose on the district because the night ones flicker.

## A perfect null result from a codegen A/B

**Cause:** `Copy-Item` (and `cp -p`, and `git checkout` of a file) carries the SOURCE's timestamp across, so the swapped-in file can be OLDER than the object `make` already built from the other arm, `make` does nothing, and both arms run the same ELF. Measured here: two arms whose sources differed by 332 lines produced **byte-identical ELFs**, and every routing counter read a delta of exactly 0.0.
**Fix:** after swapping, stamp the file (`(Get-Item $f).LastWriteTime = Get-Date`), delete the matching `.o` in BOTH the project and the `-Cache` tree, and then **hash the two ELFs and check they differ**. A null A/B is only evidence if the two arms were two builds. (An `instrument-frame-cost.py` pass rewrites the file and bumps its mtime, which is why that flow does not hit this.)

## Two correctness checks this class of change needs

**Cause:** a timing A/B does not catch them.
**Fix:**
- **A skip must be a NO-OP, not a cheaper approximation.** Hold the old code as an oracle and compare the live buffer against a full rebuild *every* frame, including the frames that skipped. **Run that oracle INSIDE the game, not only in a host harness** — the wheel round's is `TYRA_WHEEL_REBUILD_VERIFY` (default 0; it costs more than the work it checks): it re-derives every drawn car's wheels with the PRE-CHANGE arithmetic and byte-compares, printing `VERIFY checked=N STALE=M`. Do not reshape the hot path to share code with the checker.
- **Anything hoisted out of a loop must be BIT-identical, not equivalent.** A reassociated rotation differs in the last place and that is a moved pixel. Do the trigonometry once but keep the same operations in the same order on the same operands, and prove it over a wide random spread including the degenerate branches.

## Read live bbox counters for per-object attribution

**Cause:** `StaPipCore::takeTelemetry()` folds `cacher.stats` into `telemetry.attrib` and resets it, so peeking `telemetry.attrib.bboxCache*` around an individual draw yields zero deltas even when that draw replaces cached boxes.
**Fix:** instrumented per-object probes must snapshot the live `cacher.stats` as well as the live telemetry brackets. Frame exports taken via `takeTelemetry()` remain correct. The first three 2026-09-28 causal arms retain those deferred per-object zeros; the cache control/candidate/repeat arms fix that reader and identify two fresh replacements per frame in each of Ravager and Pica. Keep each A/B group on one ELF with a boot-only switch; report instrumented times separately from the production 23.923 ms baseline. The archived probe patches are in the example's `night-entry-hardware-2026-09-28/causal/`.

## Accept a bbox-cache change against fresh canonical bounds

**Cause:** the 2026-09-28 count-variant acceptance compares cached main boxes, every child box, merged min/max ranges and coarse bounds to freshly constructed `StaPipBagPackagesBBox` instances on physical PS2.
**Fix:** it also checks repeated full/prefix requests, content-version bumps, hundreds of changing counts, different VU package sizes, recycled-address versions, full expiry and hash index reconstruction after partial expiry. The oracle is a correctness arm; its timings cannot be used as performance data. The shipped engine keeps `TYRA_STAPIP_ATTRIB=0` and carries none of the oracle/boot-toggle/raster hooks. See the example's `night-entry-hardware-2026-09-28/fix/README.md` for the 3,470 lifecycle cases plus 48,803 live requests. Scenario overrides must run after the initial scene creates vehicle instances, at the simulation boundary; a first-loop override can see an empty array and be replaced by `bootFirstScene()`. Export traffic positions/speeds and reject a nominal parked control if another car moves. Continuous driving windows are selected by actual position, not a presumed frame count. Quality-reduction probes may change FPS and thus physics substep cost; compare render/finish as well as total work.

## Proving "collisions and picking are unchanged" WITHOUT a capture

**Cause:** a capture can only show the poses you thought to photograph, and adding models to a project inserts them into `model_data.gen.hpp` **in place**, so every model index above the insertion shifts and a plain diff of `scene_data.hpp` reports a change on most rows (the coupe's `model` going 11 -> 13 is the same vehicle body).
**Fix:** diff the GENERATED SCENE DATA instead — it covers every object at once and it is exact. (1) read both `model_data.gen.hpp` tables and build `index -> name` for each; (2) rewrite the control's model field to the candidate's index for the SAME NAME; (3) diff, and require the residual to be only the fields the change owns. `examples/vehicle-playground/authoring/impostor-threshold-2026-09-17/verify_scene_identity.py` is the worked example. Select the object rows by their FIELD COUNT (other tables in the same header share the `{...}, // name` shape), and expect **consequences** as well as the change — assigning an impostor also clears `batchStatic`. Report those rather than allow-listing them silently.

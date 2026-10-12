# tyra-editor-dev — the Neural Upscaler (BLSS)

From the `blss_ui.cpp/.hpp` + `blss_window.cpp` source-map row (which has no
heading of its own). See `docs/neural-upscaler.md`, `docs/profiling.md`.

## The window must not re-implement the drivers
**Cause/rule:** the window RUNS THE EDITOR'S OWN BINARY and parses its stdout,
because every driver in `blss.cpp` (`buildCorpus`, `labelCorpus`, `gatherSamples`,
`evalRecurrent`, `crossValidateOnce`, `featureReport`, `netField`) is in an
ANONYMOUS namespace — a window calling "the public API" would have to re-implement
them, and a re-implemented driver is a second answer to "what does `--blss-eval`
measure". `blssui::Job` spawns `tyrax-editor --blss-<verb>` on a worker thread
(`App::blssPoll` runs every frame from `drawUI`, not from the window body, so a
run that ends while the tab is shut still lands — the `giBakerPoll` rule) and
`parseEval`/`parseCv`/`parseFeatures` turn the printed tables back into numbers.
**Fix:** `blss_ui.cpp` has no ImGui and no `App` — the parsers are pure functions
of a string, so a captured run can be fed to them from a harness (link
`blss_ui.cpp.o` + `platform.cpp.o`, stub `kFeatureNames`). Two bugs found that
way: `%-11s` OVERRUNS on the two long method names, so the columns must be found
by "first token that is wholly a number" and never by position; and the per-shot
header of `--features` is FIXED WIDTH because a shot name truncated to eight
characters contains a space (`poles pa`, `flat slo`), which whitespace tokenising
turned into three extra columns. The raw output is always on screen under the
tables, which is what makes a parse falsifiable instead of trusted.

## The settings block is two mirrors of an interlock that must not drift
**Cause/rule:** `blss_window.cpp` owns the **seven project settings and the
build-interlock warning**, drawn from there by BOTH this window and *Project >
Preferences* (`drawBlssSettings`/`blssClashesFor`/`drawBlssClashWarning`):
`blssClashes()` in templates.cpp is the source of truth and this is its ONE mirror
(it was inlined in the Preferences dialog, and a second window would have made it
two mirrors).
**Fix:** the block is TWO LAYERS from one definition (`drawBlssSettings(s,
BlssDetail)`, the `drawBlssVerdict(compact)` idiom): `Essentials` — what
Preferences draws — is *Use the upscaler*, *Mode*, *Render scale*, the VRAM line,
ONE LINE OF VERDICT from `blss::measureCoverage`, the per-scene / mixing notes,
the clash block and an `Advanced...` button; `Everything` adds the four knobs that
only mean something to somebody FITTING a network (sharpen, temporal, jitter,
debug view) and the long half of each tooltip — a tooltip is one string whose long
half is APPENDED at the deeper level, never a second string, so the two forms
cannot disagree about a number. **Nothing was deleted from the window**, because
every number this feature has published came out of that instrumentation. Three
rules the split must keep. **A SIMPLER UI MUST NOT BECOME A MORE CONFIDENT ONE** —
the Preferences verdict is `blssui::recommend(EvalSummary{}, false, speed, true,
mode)`, the same pure function the window's own answer goes through, so "TOO CLOSE
TO CALL" still quotes no multiplier and the picture half is still named as
UNMEASURED. **The price is re-derived EVERY FRAME** (`blssSpeedFor(s)` from
`blssPoll`, and from Preferences against the STAGED settings) — the two modes'
break-evens are 13.1 and 2.6, so a verdict frozen at the mode the count finished
under is wrong by a factor of five. And **`blssClashesFor` takes
`assumeProjectDefaultOn`**: it skips scenes that resolve the upscaler off,
correctly, which made the whole INFORMATIONAL branch unreachable; the flag walks
the scenes that INHERIT the default as though it were on (a scene that EXPLICITLY
overrides to off is still skipped) and is set exactly when the caller labels the
block informational. `project::blssUse(p, defaults)` answers "does this project
mix" for a default the modal has not committed yet. The per-scene override in
*Scene > Scene Preferences* draws the SAME two controls with the SAME wording as
Essentials.

## Provenance and corpus
**Cause/rule:** a `blss.net` records nothing about how it was made, so provenance
is a `blss.net.args` sidecar the editor writes next to a net it trained.
**Fix:** a net newer than its sidecar reports "unknown" rather than a stale answer.
**The corpus is ONE switch in the header, not one per tab** (`blssCorpusProject_` +
`blssCommonArgs()`, which puts the project directory in as the positional argument
every verb already accepts, ABSOLUTE because the job runs with cwd = the project
dir and because that string is what the `blss.net.args` sidecar records); it
DEFAULTS to the project with `--all-shots` on. The Evaluate tab states a VERDICT
above the table in three branches (no headroom / the net is worse than not using
it / the margin and its fraction of the ceiling), and the arithmetic is
`blssui::summarise()` and NOT a calculation inside the draw call. The
Cross-validate tab carries a caveat on a project corpus. Window key `"blss"` — in
`showFlagForKey` AND `kLayoutWindowKeys`.

## The shot plan
**Cause/rule:** `Project::blssShots` (a `BlssShotPlan`, its own
`Section::BlssShots`) is the author's statement of which of the six automatic
camera moves the corpus shoots, with how many frames each, plus their own vantages
(typed, grabbed from the viewport, or bound to a placed Camera object).
**Fix:** three rules hold it together. **A default plan writes NOTHING to the
.tyra**, which keeps every existing project's corpus byte-identical. **The
resolution is `project::blssResolveShot`**, in project.cpp, called by BOTH the
window's preview and (by design) blssscene's walk — the editor drawing a frame the
corpus never renders is the exact class of bug this feature spent eleven commits
on. And **the panel checks that the trainer obeyed it**:
`blssui::parseCorpusScenes` reads the `[blss] scene ...N shot(s)` lines back and
compares them against the plan. `blssui::corpusHealth()` turns the `--features`
columns into named findings and refuses to read as a pass when nothing has been
measured; `blssui::parseProbe`/`probeVerdict` place a console `BLSSFEAT` line in
the corpus, taking the flags from the TOOL'S OWN verdict words. **The game writes
no `bin/log.txt` under ps2link** (`templates.cpp` sets `writeLogsToFile =
!ps2link`), so the panel falls back to the runner's `[ps2]` stream and names the
source it used.

## Plain mode
**Cause/rule:** `ProjectSettings::blssNetwork` is the sixth setting and the one
that changes who this feature is for (docs/neural-upscaler.md, "Plain mode"):
Plain keeps the reduced raster, the VRAM saving and the build interlock and
deletes the network (no bag proxies, no reprojection, no feature grid, no MLP,
four grid vertices instead of 476), taking the EE bill from 4.60 ms to 0.52 and
the break-even from 13.1 full-screen coverages to 2.6 at 512x448.
**Fix:** its own bool rather than a third value of `blssEnabled` or `blssScale`,
because it is a third question and because an additive bool defaulting to true
regenerates every existing project byte for byte. Two consequences: the four knobs
below it are the NETWORK's and go grey in plain mode, and
`blssui::speedFrom`/`breakEven`/`recommend` all take the mode. The engine forces
the sub-pixel jitter off there (one place, `configure()`), and codegen emits no
`#include` of the net and no `setNet()` call.

## The speed model is hardware-fitted, never assumed
**Cause/rule:** the `fill::` block in `blss_ui.hpp` is the speed model
(docs/profiling.md, "Calibrating the speed model against hardware"):
`saved(ms) = 0.7548 x fill - 5.10` over five load points, RMS 0.093, so
`kSavedFraction` 0.7548, `kEeCostMs` 4.60, `kCompositeGsMs` 0.50 — joined by
`kEeCostPlainMs` 0.52 (arithmetic over hardware terms, not a measurement of plain
mode). `breakEven(rasterPx, network)` is **DERIVED from those, never written
down**.
**Fix:** `kPassMs` is per 512x512 while a coverage is per the project's own raster,
so it is expressed per PIXEL (`fill::kPassMsPerMpx` 2.2524 + `fill::passMs(rasterPx)`,
both rasters measured on one console: 0.5896 at 512x512, 0.5174 at 512x448, perMpx
agreeing to 0.3 %) — `breakEven(rasterPx)` and `speedFrom(coverages, rasterPx)`
take a raster, and `CoverageReport` echoes back the `outW`/`outH` it counted at so
the window and the CLI cannot price one scene two ways (13.1 coverages at 512x448
against 11.4 at 512x512). **QUOTE A BREAK-EVEN WITH ITS RASTER.** The counter is
still deliberately left alone (`kAnchorCoverages` stays 58.7). **`--blss-coverage
<projectDir>` is the headless twin of the window's "Will the frame get faster?"
button** (`blssCoverageFromCli` in main.cpp, calling the same
`blss::measureCoverage` + `blssui::speedFrom` in-process, printing per-shot and
overall mean/p95 and machine-readable `[blss] coverage ...` lines). The window now
calls the figure an overdraw INDEX, names the cameras it averaged and their
spread. **The window's three bands are bounded** — the output pane at 35 % of the
body and the header capped and scrolling — because the tab child measured 111 px
at 1080p once an answer rendered and the Evaluate tab's controls were being
submitted BELOW its bottom edge, where `--ui-script` reports a rect, clicks
'successfully' and hits something else.

## Reference facts (upstream `blss.md`, host half)
- Host files: `blss.{hpp,cpp}` (~1.3k+4.1k) feature vector + 6-12-3 MLP (forward + Adam) + oracle + trainer + emitter; `blsscorpus.{hpp,cpp}` (~340+3k) software rasteriser; `blssscene.{hpp,cpp}` (~260+1.07k) walks a project into world triangles (primitives via `primmesh`, static `.obj` via `objparser`, terrain 16-cell chunks, plus animated `.glb`; `updateAndRenderAnimObjects` submits through `stapip.core.render()`); `blss_ui.{hpp,cpp}` (~750+1.56k); `blss_window.cpp` (~4.5k). Corpus describes a frame through `BagProxy` (screen bbox, w range, one material scalar).
- Window runs the editor's own binary: `buildCorpus`/`labelCorpus`/`gatherSamples`/`evalRecurrent`/`crossValidateOnce`/`featureReport`/`netField` are in an anonymous namespace, so `blssui::Job` spawns `tyrax-editor --blss-<verb>` and parses stdout (`App::blssPoll`). Parser traps: `%-11s` overruns the two long method names; the `--features` per-shot header is FIXED WIDTH (a truncated shot name contains a space, `poles pa`); find columns by "first wholly numeric token", never position. `blssui::summarise()`; `corpusHealth()`; `parseCorpusScenes`; `parseProbe`/`probeVerdict`; `recommend`; `speedFrom`/`breakEven`.
- Settings (8 on `ProjectSettings`): `blssEnabled`, `blssScale` (0 = 2x2, 1 = 1x2), `blssNetwork` (true = network, false = Plain), `blssAdaptive`, `blssSharpen`, `blssTemporal`, `blssJitter`, `blssDebugView`. `drawBlssSettings(s, BlssDetail)` is TWO LAYERS (Essentials/Everything); the long tooltip half is APPENDED, never a second string.
- Per scene: `SceneOverrides::upscaler` (`blssEnabled` + `blssNetwork`, format v13, additive/inheriting). Read `project::resolvedSettings(p, sc)`; project-wide `project::blssUse(p)` / `blssUse(p, defaults)` (any/anyNative/anyNetwork/`mixed`). Per-scene machinery `BLSS_ENABLEDS[]`/`BLSS_NETWORKS[]`/`BLSS_NATIVE_SCENES` (eighth `configure()` argument, `setScene()`) appears only when scenes disagree. `p.settings.blssEnabled` is the project DEFAULT; the per-scene block gates on `s.blssEnabled`. `RendererCoreGS::setLowResTargetScale` (reserve before `needsBufferRealloc()`); `mixed` costs 0.375/0.875/0.125 MB free heap at 512x512.
- Codegen placeholders `{{BLSS_INCLUDE}}`, `{{BLSS_INIT}}`, `{{BLSS_SCENE_SETUP}}`, `{{BLSS_SCENE_RENDER}}` filled by `blssInclude`/`blssInit`/`blssSceneSetup`/`blssSceneRender`/`blssNetHeader`; `inc/blss_net.gen.hpp` is overwrite-listed. `blssBake()` uses the project net, else `resources/blss-default.net` (+ `.meta`), else random init; `--blss-eval`/`--blss-emit` print `[blss] net source=...`. `PipelineInfoBag::blssProxy = false` for sky dome, stars, sun/moon discs.
- Interlock: `blssClashes()` (templates.cpp) is the source of truth, mirrored by `blssClashesFor`/`drawBlssClashWarning`; clashes = DoF, portals (to another `Portal` in the same scene), split view (only with a second `Player`), plus frame extrapolation. `blssInterlock()` writes `src/gen/blss_interlock.gen.cpp` (deleted when it stops); ONE short line per clash, `errorSafe()` on the whole line. The closing 8-placeholders twin is `{{BLSS_SCENE_SETUP}}` in `loadScene`.
- Plain mode (`blssNetwork = false`): reduced raster + VRAM saving + interlock; no bag proxies/reprojection/feature grid/MLP (4 grid vertices vs 476); EE bill 4.60 -> 0.52 ms; break-even 13.1 -> 2.6 at 512x448. `blssAdaptive` reserved at boot, `updateAdaptiveResolution()` before the BLSS bracket.
- Shot plan: `Project::blssShots` (`BlssShotPlan`, `Section::BlssShots`); `project::blssResolveShot`; corpus is 13 shots (`strafe` etc. displaced by a take); a default plan writes NOTHING; `--ignore-shot-plan`; `parseCorpusScenes` compares `[blss] scene ...N shot(s)` lines.
- Twin/objective: `composite()` twins `RendererCoreBlss`'s five GS blend passes (`(A-B)*C>>7`, 12.4 UV quantisation, 4-bit bilinear weights); mirrored `accumulate`/`buildReproj`/`buildFeatures`/`Net::forward`; `bagOf()` <-> `RendererCoreBlss::addBagBox()`; proxy split `kProxyVerts` = 24 (cap 32, consecutive merge); `triLerp` pins the TRIANGLE_STRIP order. `buildFeatures()` is pure. Six channels `kFeatures` = 6, `kHidden` = 12 (was eight; `histAge`/`luma` deleted; `edgeDens` control); bump `kNetVersion` (now 3) + refit `resources/blss-default.net` in the SAME commit. Objective = accuracy + `--flicker-weight` + `--fill-weight` (per full-screen pass, a STEP on the quantised alpha byte); `kFlickerWeight` = 0; fill ships 16.
- Measurement: `--blss-eval --cv` (never a plain held-out column); bestiary +0.41 dB over plain bilinear; `corridor` loses (depth pinned at 1.0; 58.6% of tiles); `--features`; `--probe "<BLSSFEAT line>"`; `--blss-train`; `md5sum`; `--act-table 512`; `--threads N` (0 = all cores, clamped 32) bounds `parallelFrames`/`parallelFor` and is WALL-CLOCK only (`prevLow` re-rendered); `--blss-emit` round-trips through `selfTestEmitter()`. `texDetail` is identically zero on five projects.
- Provenance: `<net>.meta` sidecar; `blss::checkProvenance` compares `blss::kEngineActTable` (512, twin of `TYRA_BLSS_ACT_TABLE`) never `detail::gActN`; anchor md5s `e069f286...`/`6b2fba90...`/`879146bd...`. `FrameProfile::gsFillProbe`; `--blss-coverage <projectDir>` reads 72.23 vs 58.7 blended-pass equivalents (`kAnchorCoverages` 58.7).
- Speed model (`fill::` in blss_ui.hpp): `saved(ms) = 0.7548 x fill - 5.10` (RMS 0.093); `kSavedFraction` 0.7548, `kEeCostMs` 4.60, `kCompositeGsMs` 0.50, `kEeCostPlainMs` 0.52; `fill::kPassMsPerMpx` 2.2524 + `fill::passMs(rasterPx)`. `breakEven(rasterPx, network)` is DERIVED. Quote a break-even WITH its raster (13.1 at 512x448, 11.4 at 512x512). `CoverageReport` echoes `outW`/`outH`.

## BLSS literal anchors (upstream)
- `--blss-emit` round-trips through `selfTestEmitter()` (`%.9g` renders `0.0f` as `"0"` and `0F` is not a float literal - every net-less project failed to compile for three commits). Net md5 anchors: `6a93196c96aa15993150ec724716a27d` (shipped universal net), `6b2fba90d0f059f055134a55df478c8e` (`--threads`), `e069f286...` (shot plan), `879146bd...` (default).

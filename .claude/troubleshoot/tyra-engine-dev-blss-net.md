# troubleshoot/tyra-engine-dev-blss-net.md

The shipped default net, the jitter bob, the feature interlock, and cross-validated
training. Symptom first.

## The shipped default net works on a scene it has never seen, and the MLP is not why

**Cause:** 2026-08-09, real hardware, docs/profiling.md "The shipped default net". A stock
`--new` fpp project plus six haze banks, **no `blss.net` of its own**, so `blssBake` falls
back to `resources/blss-default.net`: **30.65 ms off against 15.62 ms on, d = +15.03 ms, CI
[+14.96, +15.10], n = 1024 paired frames, four cross-pairings spanning 0.018 ms — a 1.96x
speedup, 25 FPS to a locked 50.** The boot log names the net (`BLSS: network = the editor's
built-in default network (fitted on ...)`). **But `BLSSFILL` reads `passes = 1.00` and every
`BLSSOUT` channel is 0.000**: the composite is ONE bilinear pass and the network chooses
nothing, so the win is the quarter-area raster alone. `BLSSFEAT` shows `texDetail`
identically **zero** — a stock project's terrain is vertex-coloured, not textured, and
`texDetail` comes from a bag's `texelArea`.
**Fix:** this is NOT a defect: re-evaluated at that exact vantage the scene's headroom is
**+0.000 dB** (`bilinear = oracle = 38.435`, native 45.543), i.e. bilinear IS optimal there.
Training a project net changed frame time by **+0.03 ms (1.00x)**. Two traps this cost, both
now closed: `--blss-train <projectDir>` wrote `blss.net` into the **cwd** and not the project
(FIXED 2026-08-09 — a single project positional defaults the path to `<projectDir>/blss.net`
on the read side as well as the write); and the corpus RENDERER draws no emitters, so a PSNR
number for a billboard-heavy scene describes a frame the game never displays
(`docs/backlog.md`).

## The bob is the JITTER, and the per-field bias is NOT part of it

**Cause:** the ±1/4-pixel per-frame raster jitter in `beginScene` is the confirmed cause: a
person watched three builds of `examples/upscaler-lab` differing in nothing else and called
them steady (BLSS off) / **"like an earthquake"** (jitter on) / steady (jitter off). With
the emitters frozen, 16.3 % of the picture below the HUD alternates between two
byte-identical phases. It is **not** a displacement — cross-correlation lag `(0,0)` — but a
resample alternation on every textured edge.
**Fix:** `blssJitter` defaults to **false** now; this bullet previously claimed the bob
was net-dependent and that neither shipped fixture reproduced it; both are **retracted**
— that reading came from an instrument pointed at `blssbug` (untextured, so a
quarter-pixel resample can change nothing) and at `upscaler-lab` with its particles
running. `examples/upscaler-lab` ships jitter OFF
since 2026-08-09 and its net is refitted for that sampler. Two confounds will make the gate
lie, and both cost a burst: the debug HUD prints a live **frame counter** (turn
`showFps`/`showMemory`/`showProfiler` off), and **PAL interlaced mode alternates fields,
which is itself a period-2 signal in a window capture** — run the gate at `displayMode`
**progressive**. Separately, `getFieldYOffset16()` is non-zero for `DisplayMode::InterlacedField`
**only**, so in the usual `Interlaced` mode it contributes nothing; `beginScene` used to add
it anyway, unscaled, inside a raster whose row is `scaleY` physical rows, while `composite()`
added the real one on top — 1.5 physical rows of field bias at 2x2 instead of 0.5. It is
**removed** from `beginScene` now. `configure()`'s trailing `jitter` parameter (project field
`blssJitter`, default true, no UI yet) pins the offset to 0. The host twin in `src/blss.cpp`
does NOT model the switch, so a net trained today and run with the jitter off is slightly out
of distribution. **Test for the PERIOD-2 SIGNATURE, never for "did it change"**: a sampler
with an even frame stride lands on one jitter phase every time.

## Incompatible with depth of field, portals, split view and frame extrapolation

**Cause:** the first three read or write real GS depth at display resolution, which since the
z shrink is not merely unwritten but unallocated. **The fourth is a different mechanism and
cost a user a broken build to find** (2026-08-11, docs/frame-extrapolation.md "Why not with
the upscaler"): both features rebuild a frame by reprojecting the previous one through the
camera delta, and extrapolation presents twice per loop, so the world runs at half the field
rate and the camera moves **twice as far between two RENDERED frames** — exactly the interval
BLSS' temporal pass reprojects across. **It is invisible parked and only appears in MOTION**,
so every frozen-camera gate on this branch was built not to see it. Measured on the
reporter's project, PCSX2 software renderer, `--pad`-driven: upscaler alone clean,
extrapolation alone clean, the pair tearing the frame into cells that disagree; BLSS' own
per-corner reprojection offset peaks at 158 px of a 448 px raster with extrapolation off and
201 px with it on. **Two theories were disproved by measurement and must not be re-opened**:
it is NOT the two-buffer history degeneration `composite()` guards, and it is NOT raster state
leaking across the warp.
**Fix:** the build REFUSES the combination now (`332f3193`): `blssClashes()` + `blssInterlock()`
in `src/templates.cpp` put `#error` lines into the generated `inc/scene_data.hpp` naming the
feature and the scene, and `generate()` prints the same on the host. Each condition mirrors
what the generated game does — DoF per scene *and* the `Set Depth Of Field` flow node, portals
only when **linked**, split view only with a **second `Player` object**, frame extrapolation
whenever the project preference is on and a scene resolves the upscaler on. **That refusal is
per SCENE since the setting became one**: `blssClashes()` asks each scene that RESOLVES the
upscaler on, so a portal in one scene refuses that scene and leaves the others upscaled. Env
maps, camera feeds and projected shadows are **no longer on that list** — they nest now. The
HUD, 2D and every post effect still draw at full resolution, after the composite.

## Frame extrapolation x the neural upscaler: the history tap needs TWO guards

**Cause:** BLSS has no history buffer — the other display buffer IS its temporal history —
so a synthesised frame landing there feeds an accumulator its own warped output. Guard one
is `getPreviousRealFrameBuffer()`: `flipBuffers` records `lastRealBuffer` and a
`synthetic` flip does not claim it, so both the warp and BLSS ask for the last frame the
SCENE drew. Guard two is the one that is easy to miss, because it is the first guard
working correctly and producing a useless answer: with **two** display buffers, two flips
per loop return `context` to where it started, so every rendered frame is composited into
the buffer that holds the previous rendered frame — the history IS the render target.
`composite()` compares the addresses and drops the temporal pass with a one-shot warning.
**Fix:** with three buffers the indices are distinct by construction and neither guard
fires. **BOTH GUARDS ARE CORRECT AND BOTH ARE NOW UNREACHABLE FROM A BUILD** — the pair is
refused (see the interlock above), because the real interaction was never the buffer
bookkeeping: verified by LOGGING the three-buffer rotation frame by frame, `hist` is always
the previous RENDERED frame, intact, and `histIsTarget` never fires. What actually breaks is
that extrapolation halves the world rate, so the camera delta BLSS' temporal pass reprojects
across DOUBLES — and it only shows in MOTION. Keep the guards for the next feature that
presents without rendering; do not spend another session looking for the bug here.

## Out-of-distribution numbers: use `--blss-eval --cv`

**Cause:** a plain `--blss-eval`'s held-out columns are a single split, a sample of size one;
this feature quoted one five times, and the ±0.4 dB it blamed on the training seed was
**which shot got held out** (per-seed fold-mean sd: 0.04 dB against 0.35 fold to fold).
**Fix:** use leave-one-shot-out cross-validation
(`tyrax-editor --blss-eval --cv`). The current answer on
the built-in corpus is **+0.42 dB over plain bilinear**, 39 fold-runs, 3 of them below
bilinear, 1.80 mean passes, **at jitter ON**. That net is still not the one to ship into a
game, and the reason is now measured over seven projects: a bestiary-trained net is a
lottery, −0.34 dB on average and **−1.09 dB at worst**, because `texDetail` — the channel its
temporal gate leans on hardest — is identically zero on five of those seven. What DOES ship as
one net is a corpus that is **the bestiary AND real projects together**: leave-one-**project**-out
it scores +0.29 dB on a project it has never seen against that project's own net's +0.31 (fold
sds 0.37 and 0.34). `--blss-train <a> <b> bestiary --all-shots` builds it;
`--blss-eval <a> <b> bestiary --cv --cv-groups` is what measured it. `--blss-train
<projectDir>` still fits one project and still reaches the highest number of all in
distribution (+0.41 dB). Full account: docs/neural-upscaler.md, "Can one net ship for every
project?". Both verbs take **`--threads N`** (0 = every core, clamped to 32) for the corpus
render and the oracle; it moves the wall clock and nothing else — the same seed writes a
byte-identical `blss.net` at any thread count. `--threads 1` against every core, then
`md5sum`, is the check.

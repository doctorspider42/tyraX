# Profiling the generated game (EE frame time)

The PS2 games this editor generates are almost always **EE-bound**: a frame
that misses the vsync budget (20 ms PAL / 16.7 ms NTSC) drops straight to the
next divisor (50 → 25 FPS), and it is nearly always the EE, not the GS, that
ran out of time. Finding *which* EE phase overran is the whole game. This doc
describes the built-in profiler and the deeper manual technique behind it.

The worked example throughout is the usable-object highlight: it looked like a
cheap effect but dropped the showcase to 25 FPS. The full write-up is in the
retired `PROGRESS.md` (the usable-highlight rounds) — see [Backlog](backlog.md)
for how to read it out of git history.

## The built-in frame profiler

**Project > Preferences > Build**, set the profile to **debug**, then tick
**Show frame profiler** (alongside *Show FPS* / *Show memory usage*). The game
draws a per-phase EE-time breakdown in the top-left corner, averaged over ~1 s:

```
FRAME 22.00 SCENE 12.90
HL 1.46 PART 0.99
```

- **FRAME** — whole-frame wall-clock (ms). With vsync on this reads the
  quantized budget (20.0, 40.0 …); the phase sums below it are the real work.
- **SCENE** — sky + terrain + objects + animated models (+ the bodies of
  highlighted usables, which are real geometry).
- **HL** — usable-highlight *overhead* only (the shell/apron submits, not the
  object bodies).
- **PART** — particle systems.

Everything here is gated behind the `DEBUG_SHOW_PROFILER` constexpr in
`terrain_config.hpp`, so a release build (or the option unticked) contains
none of the timer reads — zero cost. The timing uses the EE **COP0 Count**
register (`mfc0 $9`), which ticks at 294.912 MHz (half the 590 MHz EE clock);
divide tick deltas by 294912 for milliseconds.

This is enough to answer "is the highlight/particles/scene the problem?". For a
finer breakdown you drop to the manual technique.

## Manual deep-dive (finer breakdown)

When you need to see *inside* a phase — e.g. "which branch of the engine's
`StaPipCore::render` is the highlight spending its time in?" — the recipe that
works:

1. **Own the generated `terrain_game.cpp`.** Copy the project to a short path
   (`%TEMP%\tyra-editor-test\<name>` — long `host:` ELF paths crash PCSX2), and
   delete the first-line ownership marker so the build stops regenerating it.
2. **Bracket the phases you care about with COP0 reads** and accumulate into
   file-scope counters, exactly like the built-in profiler does — but as deep
   as you need (per engine call site, per package branch). For engine-internal
   phases, add temporary `u32` counters inside `vendor/tyra` (e.g. in
   `StaPipCore::render` / `StaPipQBufferRenderer::sendPacket`) and `extern`
   them into the game to print. Revert those engine edits when done.
3. **Make the repro deterministic without a pad.** Overwrite the camera in
   `loop()` with a slow orbit around the object of interest, e.g.

   ```cpp
   static float a = 0.0F; a += 0.004F;
   entX = cx + r * cosf(a); entZ = cz + r * sinf(a);
   entY = terrainHeightAt(entX, entZ);
   entYaw = atan2f(cx - entX, cz - entZ);
   cameraPosition = Vec4(entX, entY + PLAYER_EYE_HEIGHT, entZ);
   cameraLookAt   = Vec4(entX + sinf(entYaw), cameraPosition.y, entZ + cosf(entYaw));
   ```

4. **A/B in the same run.** Flip the feature under test on/off every N frames
   (a `static bool`, toggled when the averaging window rolls over) and print
   two labelled rows. Comparing HL-on vs HL-off *in one session* removes
   scene/camera variance — this is how the highlight cost was isolated.
5. **Read it on screen.** EE `printf` / `TYRA_LOG` does **not** reach
   PCSX2's `emulog.txt`, so print with the debug glyph HUD (`drawHudText`).
   That atlas only has **digits, `.` and UPPERCASE** letters — keep labels
   uppercase (`SCENE`, `HL`, `PART`). Screenshot the window with
   `.claude/skills/tyra-testing/scripts/screenshot-window.ps1 -ProcessName pcsx2-qt`.

### Gotchas learned the hard way

- **PCSX2's EE is faster than a real PS2 relative to the budget**, so a drop
  that shows in the emulator is real, but the emulator can also *hide* a
  real-hardware drop. Numbers are directional; confirm pathological cases on
  hardware (see the `dynpip-guard-band-cost` / `vu1-clipping-cost-measurement`
  developer notes for the real-PS2 deploy recipe).
- **`endFrame` time is mostly vsync idle** on a vsync-locked frame — do *not*
  read it as GS load. The reliable signals are whole-frame time and the
  per-phase EE sums.
- **Submit count × near-object cost dominates.** A near object is always
  partially in the frustum, which routes it through the engine's most
  expensive path (subpackage classify + clip). Any effect that re-submits a
  near mesh N times multiplies that. The highlight's 25 ms was 5 extra
  full-mesh submits of a 9k-vertex primitive; the fix cut both the submit
  count and the per-submit vertex count.

## Timing a frame that BLSS is in (the frame-timing rig)

The built-in profiler above answers "which EE phase overran". It cannot answer
the question the neural upscaler (`neural-upscaler.md`) poses, which is a
different one: **did trading GS fill for EE work make the frame shorter?** For
that there is a second, finer instrument in the engine —
`vendor/tyra/engine/inc/debug/frame_profile.hpp`.

### Turning it on

```c
#define TYRA_FRAME_PROFILE 1        // the counters + the FRAMETIME line
#define TYRA_FRAME_PROFILE_CALIB 1  // ... and the destructive GS fill sweep
```

Both default to **0**, and at 0 neither `libtyra.a` nor the generated game
carries a single instruction of any of it — the whole file is inside the
`#if`. It is a source-level switch rather than a project preference on purpose:
the engine is compiled **once per checkout** into a shared Docker volume and
reused by every project, so there is no per-project engine to flag. Edit the
header, run any game build, and the Runner rsyncs + rebuilds the engine for
you. (`-DTYRA_FRAME_PROFILE=1` on both compiler command lines works too, but it
has to reach both halves — the game reads the same header.)

### Why not just turn vsync off and read FPS

Because **it changes what is being measured.** BLSS' pass 3 reprojects the
previous frame, and its `motion` feature is a per-frame camera delta
(`RendererCoreBlss::buildReproj`) — a different frame rate hands the network a
different input, lights different tiles and draws a different amount of fill.
The generated game's `updateFrameClock()` also clamps `dt` and stops tracking
above ~200 FPS. So the rig reads **COP0 `Count` inside a vsync-locked frame**:
once at the top of `RendererCore::beginFrame()` and once immediately before
`if (isFrameLimitOn) graph_wait_vsync()`. Everything after that second read is
idle, so what is left is the real sub-frame work — where a 22 ms → 17 ms
improvement is fully visible at a locked 50 Hz.

### The counters

| Counter | Where | Meaning |
|---|---|---|
| `tFrameWork` | `beginFrame()` → just before the vsync wait | **the primary metric** — per-frame work, vsync idle excluded |
| `tDrain` | the fairness fence, `endFrame()` | GS overhang of the whole frame |
| `tBlssBegin` | `RendererCoreBlss::beginScene` | raster redirect + clear + drain |
| `tBlssEnd` | `RendererCoreBlss::endScene` | **GS overhang of the half-res scene** |
| `tBlssComposite` | `RendererCoreBlss::composite` | features + MLP + packet + GS raster |
| `tBlssCompositeEe` | ... up to the DMA kick | the EE half of the line above |
| `tExcluded` | written by the game | its own logging, subtracted back out |

**The fairness fence is not optional.** BLSS' three brackets each end in
`dma_channel_wait(GIF)` + `draw_wait_finish()`, so a BLSS frame is *serialised*
— its GS work lands inside `tFrameWork`. A frame with BLSS off drains nothing
until `flipBuffers`, i.e. **after** the vsync wait, so its GS work would fall
outside the window entirely and the plain arm would read as free. `endFrame()`
therefore runs one guarded `sync.align3D()` at the same point in both arms and
charges it to `tDrain`. The guard is `path1.isVU1Configured()`: before a 3D
pipeline has brought VU1 up (the pure-2D loading screen) the FINISH handshake
spins forever.

`drain` is also the **EE-bound / GS-bound discriminator**, and it is the whole
question for an upscaler: near zero means the EE is the bottleneck and there is
no fill to trade, so BLSS cannot help however good the network is. `tBlssEnd`
says the same thing about the half-res scene specifically.

### The output line

Once a second, never once a frame — `TyraDebug::writeInLogFile` does a full
`ofstream` open + append + flush **per line** (`engine/src/debug/debug.cpp`),
over `host:` on real hardware. One `snprintf`, one `TYRA_LOG`, the same 1 Hz
cadence `logFeatureSpread` already uses:

```
FRAMETIME n=50 f=1200 work=17.42/16.98/21.30 submit=11.20 drain=2.11 blss=3.02/0.41/2.61 comp=1.90/0.71 over20=0 cam=1.2566
```

- `n` — frames in the window; `f` — index of its first frame, the **alignment
  key** between runs A and B.
- `work` — mean / median / p95 milliseconds. `submit` = `work − drain`.
- `blss` — mean `begin` / `end` / `composite`; `comp` — the composite split into
  EE (inference + packet build) / GS (raster). All zero when BLSS is off.
- `over20` — frames in the window past the 20 ms PAL budget.
- `cam` — camera heading, the independent confirmation that frame `f` of run A
  really was looking where frame `f` of run B was.

Every 512 frames it also dumps the raw per-frame `work` ticks as `FTRAW <first>
<64 hex values>` × 8 lines — same I/O cost, 512× the data, and the only way to
get **paired** per-frame samples out of two runs. The block's own cost
(sorting, `snprintf`, the log write) is charged to `tExcluded` and subtracted
from `tFrameWork`, so the frame that prints is not an outlier.

### The measurement protocol

1. **A frame-indexed camera in an object script.** Not `--pad`: the Remote Pad
   driver refreshes at 25 Hz off the *host* wall clock, so the stick lands at a
   different frame offset in each run. `src/scripts/*.cpp` is never
   regenerated (`object-scripts.md`) and `ScriptContext` exposes
   `cameraOverride`/`cameraEye`/`cameraAt`:

   ```cpp
   const float a = 0.004F * (float)(f++);   // the FRAME index, NOT g_frameDt
   ctx.cameraEye = Tyra::Vec4(R * cosf(a), H, R * sinf(a));
   ctx.cameraAt  = Tyra::Vec4(0, 0, 0);
   ctx.cameraOverride = true;
   ```

   Frame *k* of run A then shows exactly the view of frame *k* of run B, which
   makes the comparison **paired** — worth about an order of magnitude in
   statistical power over two independent means.
2. **Pin everything else.** `buildProfile: debug`, a fixed `videoSystem` and
   `displayMode`, and **Live Link / Live Debugger / Live Logic / Remote Pad /
   Time Machine off** — a debug build otherwise polls `livepad.bin` through
   HostFs every frame and writes `livedbg.bin` every 6, which is real per-frame
   network file I/O landing inside the thing you are measuring. Delete
   `bin/livedbg.cmd` before booting. Keep the ELF path ≤ 145 characters
   (`%TEMP%\tyra-editor-test\<name>`).
3. **Discard the first 150 frames** — texture uploads, VRAM eviction settling,
   and BLSS' `hasPrev == false` first frame.
4. **Report milliseconds, never FPS**, with the sign convention stated. Mean of
   the per-frame paired difference ± 1.96·sd/√n, plus per-arm median, p95, max
   and `over20`. Repeat each arm.

### The calibration gate — run it before trusting any emulator number

PCSX2 runs a **software rasteriser** and is not fill-rate accurate, and BLSS
trades GS fill for GS fill. So before quoting any emulator figure about this
feature's GS cost, measure whether the emulator can see GS cost at all.

`TYRA_FRAME_PROFILE_CALIB 1` makes the game draw *K* extra full-screen
textured, alpha-blended sprites per frame (K = 0, 2, 4, 8, 16, ten frames each,
rotating), each followed by its own `draw_finish` + wait, and log the sweep plus
a least-squares slope:

```
GSFILL k0=0.000 k2=... k4=... k8=... k16=... slope=...
```

`slope` is milliseconds per full-screen pass on the machine under test. Real
hardware should read roughly **0.2–0.4 ms** at 512×448 (229 376 px, ~8 px/clk
textured at 147 MHz). **A slope near zero means that machine cannot measure
this feature** — say so and stop quoting its GS numbers. The sweep is
destructive (it paints over the displayed frame), which is why it is a separate
switch.

### Getting the log off real hardware

**`bin/log.txt` does not exist on hardware.** `writeLogsToFile = !ps2link`
(`src/templates.cpp`), so under ps2link the game logs to the EE console,
forwarded over UDP 18194 to `ps2client`'s stdout, and never to disk. Host the
file server yourself from `<projectDir>/bin`:

```
tools/ps2client/bin/ps2client.exe -h <ps2-ip> execee host:<name>.elf -ps2link > run.log
```

`ps2client`'s stdout is **block-buffered when redirected**, so nothing appears
until it exits — let it run the whole benchmark and terminate gracefully rather
than hard-killing it, or the tail is lost. Only one file server at a time (kill
stale `ps2client` first); `reset`/`execee` are UDP fire-and-forget and "succeed"
against a dead console, so the first `[ps2]` line is the only liveness signal —
ping proves nothing. Prefer the main checkout's `ps2client.exe`: a new path
triggers an unattended Windows Firewall prompt.

### What the rig measured the first time it was run (2026-08-08)

Fixture: a scratch orbit project, 100×100 terrain plus six large slabs, PAL
576i (512×512), debug profile, Live Link / Live Debugger / Live Logic / Remote
Pad / Time Machine all off, network-deployed to a real PS2 over ps2link. The
camera is the frame-indexed script above, so frame *k* of each run shows the
same view. 1000 frames per arm after a 150-frame warm-up; three runs per arm,
compared in all nine cross-pairings. Sign convention: **d = work(BLSS off) −
work(BLSS on); d > 0 would mean BLSS made the frame shorter.**

**The calibration gate first.**

| machine | k=2 | k=4 | k=8 | k=16 | slope (ms per full-screen pass) |
|---|---|---|---|---|---|
| PCSX2 (software renderer) | 0.016 | 0.031 | 0.062 | 0.123 | **0.0077** |
| real PS2 | 1.177 | 2.352 | 4.700 | 9.396 | **0.5872** |

Both are perfectly linear, which is what makes the ratio meaningful: PCSX2
under-reports GS fill by **76×**. What it is measuring is the per-packet cost of
its emulated GIF, not raster time — 0.0077 ms for a 512×512 blended pass would
be 34 Gpixel/s. **Verdict: no, a PCSX2 GS number is not admissible for this
feature.** Its EE numbers are fine and its plumbing is worth debugging on, but
every GS figure below is from the console.

**The A/B, on hardware:**

| arm | mean | median | p95 | max | over20 |
|---|---|---|---|---|---|
| BLSS off | 9.42 | 9.15 | 11.18 | 11.86 | 0 / 1000 |
| BLSS on | 19.25 | 18.98 | 20.63 | 20.88 | 158 / 1000 |

mean(d) = **−9.83 ms**, 95 % CI [−9.85, −9.81], sd 0.25, n = 924 paired frames.
The nine cross-pairings of the three runs per arm span −9.817 to −9.838 — a
run-to-run spread of 0.02 ms against an effect of 9.8. BLSS made this frame
**9.8 ms longer** and pushed it from comfortably inside the 20 ms PAL budget to
16 % of frames over it.

Where it went, from the same line: `blss=0.45/0.03/5.41 comp=5.10/0.31`. The
composite is 5.41 ms and **5.10 ms of that is EE** — the reprojection, the
feature grid, the MLP and the ~5 700-qword packet build. The GS half of the
composite is 0.31 ms. `beginScene` is 0.45 ms. That accounts for 5.9 ms; the
remaining ~3.9 ms is scene submission getting more expensive, which is
`StaPipCore` projecting an object-space AABB per VU1 package for every bag to
describe it to the network.

**And what it saved: nothing, because there was nothing to save.** `drain` read
**0.02 ms in both arms**, and `blss=…/0.03/…` says the half-resolution scene had
0.03 ms of GS overhang. The GS was never behind the EE, so halving the raster
could not shorten the frame. That is *the wrong scene for this feature*, not a
verdict on the feature — but it was not for want of trying: a second fixture
built specifically to be GS-bound (no terrain at all, sixteen nested cubes
centred on the camera, so every one draws two full-screen coverages for one bag
of EE) still read `drain=0.02`. Untextured opaque geometry is simply too cheap
per pixel on this hardware — at the calibrated 0.587 ms a *blended textured*
pass, an opaque untextured one is a fraction of that, and it would take on the
order of a hundred full-screen coverages to overtake an EE frame that the
generated runtime already fills to ~9 ms. This is the same thing
[the top of this page](#profiling-the-generated-game-ee-frame-time) says: these
frames are EE-bound. An upscaler trades GS fill for EE work, and on the evidence
here the generated games do not have the GS fill to trade.

### The stability gate (period-2 / the "bob")

BLSS' ±¼-pixel raster jitter is the confirmed cause of a shaking picture
(neural-upscaler.md, "The oscillation"). **Three instruments in a row reported a
still picture at a console a person was watching shake**, each for a different
reason, so the rules below are the residue of three failures rather than a
recipe somebody liked:

- **Freeze the camera — and everything else that moves.** Particles are the one
  that bites, because they look like scenery. On `examples/upscaler-lab` the
  running emitters contribute **1.04/255** of frame-to-frame change all by
  themselves, which is *larger* than the 0.77/255 artefact underneath, and the
  clustering degenerates into contiguous blocks of time (a drift, ratio 1.4×)
  instead of an alternation. With the emitters off — `SetParticles`, one pad
  press on that fixture — the same scene splits 18/22 with **0.0000** within.
  An instrument whose noise floor is above the signal is not measuring the
  signal.
- **Use content that can show it.** A quarter-pixel resample changes nothing on
  a flat surface. The minimal `blssbug` fixture (untextured box, flat ground)
  measures **byte-identical, truthfully**, and says nothing at all about a
  textured scene. Point the gate at real material.
- **Capture as fast as the tool will go, never on a fixed stride.** The original
  failure was one frame every 40 at 50 Hz — an even stride lands on the same
  jitter phase forever. 40 GDI grabs back to back take ~1.7 s (≈24 Hz) and land
  on both phases without any timing argument being required.
- **Test for the period-2 signature, not for "did it change".** Cluster the
  captures by pairwise difference. Two *balanced* clusters, near-zero within and
  large between, **is** the alternation; the raw "% of pixels that changed
  between consecutive captures" was only 0.40 % for a build a person called an
  earthquake, which is exactly the number somebody dismisses.
- **Never let the crop move with the picture.** Take the render child's client
  rect once and reuse it; do **not** pass `-Trim`. Trimming removes black
  borders, so a picture that slides down a line comes back with one more black
  row on top and trims to an *identical* image — a whole-image displacement is
  invisible by construction to any instrument that trims. (It was not the
  mechanism here: the integer cross-correlation lag is `(0,0)` and the sub-pixel
  row shift 0.14 px. It is still a real way to measure zero.)
- **Report a displacement estimate as well as a pixel count.** A one-line shift
  produces a huge per-pixel diff in some crops and none in others; the lag that
  minimises SAD, plus a parabola-fitted row-profile shift, is honest in both.
- **Prove the capture is the game.** A GDI grab reads the *screen*. One run of
  the A/B/C below silently captured the editor's own chat window, produced a
  perfectly plausible "stable" table, and would have been believed. Select PCSX2
  by the project on its command line, and diff frame 0 against the other arm's
  frame 0 before trusting a single number.

**The acceptance test for any future version of this gate** is the labelled
A/B/C set in neural-upscaler.md: it must flag B and must not flag A or C. That
is worth far more than a tuned threshold, and it is what the current instrument
passes with A and C **byte-identical across 40 captures** and B splitting into
two clusters that are byte-identical *within* and 1.15/255 apart *between*. If a
rebuilt instrument cannot flag B, say so and stop — "visible to a human,
invisible to frame-buffer capture, because X" is a correct result. Tuning a
threshold until B trips is manufacturing agreement with an answer you already
had.

Measured in PCSX2 (480p, static camera, HUD off, 16–20 captures each):

| configuration | clusters | within | between | amplitude | verdict |
|---|---|---|---|---|---|
| BLSS off | 17 / 3 | 0.014 % | 0.053 % | 0.03/255 | stable |
| BLSS on, `blssJitter` **off** | 12 / 8 | 0.029 % | 0.046 % | 0.03/255 | stable |
| BLSS on, `blssJitter` **on** | **8 / 8** | 0.019 % | **30.8 %** | **1.42/255** | **period-2 alternation** |

**The bob is still there on the current build**, with the fill term in and the
shipped net trained on the project's own scenes. Two notes on reading this
table: the "clusters" column for the two stable rows is the clustering algorithm
splitting pure noise, which is why the between/within *ratio* rather than the
split is what decides it; and the BLSS-off row is also the check that the rig's
own fairness fence does not disturb buffer flipping or field parity — it does
not.

Re-measured with the rules above on `examples/upscaler-lab` — real content,
frozen camera, emitters off, 40 back-to-back captures, rows below the HUD, no
trim (2026-08-08):

| build | consecutive mean\|Δ\| | changed > 16 | clusters | within | between | picture alternating |
|---|---|---|---|---|---|---|
| **A** BLSS off | **0.0000/255** | 0.000 % | 40 / 0 | — | — | 0.00 % |
| **B** BLSS on, jitter **on** | **0.7695/255** | 0.395 % | **18 / 22** | **0.0000** | **1.1542/255** | **16.3 %** > 2/255, 3.9 % > 8/255 |
| **C** BLSS on, jitter **off** | **0.0000/255** | 0.000 % | 40 / 0 | — | — | 0.00 % |

A and C are *byte-identical* across the whole burst; B's two clusters are
byte-identical within and differ by up to 40/255 between, on every textured edge
in the frame. This is the run a human independently called steady / earthquake /
steady, in that order. `blssJitter` now defaults to **false** because of it.

Capturing this on Windows has one trap that will silently produce a wrong
answer: `screenshot-window.ps1 -ProcessName pcsx2-qt` takes the **first** PCSX2
it finds, and a GDI grab reads the screen, so with several worktrees running it
captures somebody else's game and looks exactly like a screenshot. Two of the
runs above had to be discarded for that reason before it was noticed. Select the
instance by the project on its command line
(`Get-CimInstance Win32_Process … CommandLine -like "*<project>*"`), and sanity
check the contact sheet against what your fixture is supposed to look like.

## See also

- `.claude/skills/tyra-testing` — building, booting PCSX2, the screenshot
  script, and the layered verification story.
- [VU1 clipping plan](vu1-clipping-plan.md) — real-hardware EE-clipper cost
  measurements and the clipping-to-VU1 design.

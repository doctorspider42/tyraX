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
| `tBlssProxy` | `StaPipCore::render` | **scene submission**: the whole bag-proxy feed |
| `tBlssAccum` | `addBagBox`/`addBagSphere` | the **grid-accumulation half** of the row above - `addBag` alone. A subset of it, so `proxy - accum` is the projection half |
| `tBlssReproj` | `composite` | `finishTileStats` + `buildReproj` |
| `tBlssFeat` | `composite` | `buildFeatures` |
| `tBlssNet` | `composite` | `runNet` - the MLP |
| `tBlssPacket` | `composite` | the grid packet build |
| `tExcluded` | written by the game | its own logging, subtracted back out |

**The last five exist because the first hardware A/B did not measure two of its
own terms.** It read the composite's EE half off one counter and got the other
big term - "~3.9 ms of extra scene submission" - **by subtracting everything
else from the A/B difference**, which is an inference, not a measurement, and it
pointed the next round of work at the wrong half of the feature. `tBlssProxy` is
charged inside `StaPipCore` (submission, not the composite); the other four
split the composite at its four phases and reconstruct `comp`'s EE figure to
within their own overhead. They print as a second line:

```
FTSPLIT f=3150 proxy=3.04/1.31 reproj=0.39 feat=0.14 net=3.86 pkt=0.62
```

`proxy` prints as **total/accum**. `accum` is the read-modify-write per (proxy,
*tile*); `total - accum` is the projection per VU1 package - eight corners
through the MVP, the near clip, the bbox reduce. They are two halves that the
same millisecond comes off in completely different ways, and this page has
already paid once for splitting a term by subtraction instead of measuring it.

**The fairness fence is not optional.** BLSS' three brackets each end in
`dma_channel_wait(GIF)` + `draw_wait_finish()`, so a BLSS frame is *serialised*
— its GS work lands inside `tFrameWork`. A frame with BLSS off drains nothing
until `flipBuffers`, i.e. **after** the vsync wait, so its GS work would fall
outside the window entirely and the plain arm would read as free. `endFrame()`
therefore runs one guarded `sync.align3D()` at the same point in both arms and
charges it to `tDrain`. The guard is `path1.isVU1Configured()`: before a 3D
pipeline has brought VU1 up (the pure-2D loading screen) the FINISH handshake
spins forever.

> **`drain` IS NOT THE EE-BOUND / GS-BOUND DISCRIMINATOR, and this page said it
> was for four months.** The claim - near zero means the EE is the bottleneck,
> so an upscaler cannot help - is what the verdict "BLSS saves nothing" rested
> on. It is **wrong**, and it was falsified on hardware on 2026-08-08:
> `examples/upscaler-lab` reads `drain = 0.02 ms` in **both** arms and BLSS
> still made the frame **3.4x faster** (530 -> 157 ms).
>
> The mechanism is DMA backpressure. When the GS falls behind, the GIF FIFO
> fills, VIF1's DMA stalls, and the EE blocks **inside the submission it is
> already in** - which the rig charges to `submit`, not to `drain`. `drain` only
> measures the tail still in flight *after the last packet was sent*, and in a
> fully saturated pipeline that tail is short. So `drain ~ 0` is equally
> consistent with "the EE is the bottleneck" and with "the GS has been throttling
> the EE all frame". It cannot tell those apart, and neither can `tBlssEnd`.
>
> **The only honest discriminator is to change the GS load and see whether the
> frame gets shorter** - which is exactly what a BLSS on/off A/B is. Run the A/B;
> do not predict it from `drain`. A large `submit` with a near-zero `drain` and
> a frame far over budget is the signature to be suspicious of.

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

### Where the EE time actually goes, and what came off it (2026-08-08)

The console was off the LAN for this round, so it was measured in **PCSX2**.
That is admissible *here and only here*: the calibration above says a PCSX2 GS
number is worth nothing (76x under-reported), but the same rig measured the
BLSS A/B at **+8.55 ms in PCSX2 against +9.83 ms on hardware**, so the EE side
transfers. Everything in this section is EE. **The GS half of every claim below
still awaits hardware.**

Fixture: `blssrig` again (the frame-indexed orbit script, `buildProfile: debug`,
Live everything off), 16x16 tiles at 2x2, 118 proxies, ~65 paired 50-frame
windows per arm after a 150-frame warm-up, compared window-by-window on the `f=`
key. Sign convention below: **d = before - after, so d > 0 is a saving.**

**First, the attribution the split counters bought:**

| term | ms | what it is |
|---|---|---|
| `net` | **3.96** | the MLP - *the largest single cost in the feature* |
| `proxy` | **3.26** | the bag-proxy feed, in scene submission |
| `pkt` | 0.61 | the grid packet build |
| `reproj` | 0.39 | `finishTileStats` + `buildReproj` |
| `feat` | 0.14 | `buildFeatures` |
| `begin` / `end` | 0.26 / 0.05 | the two brackets |

`runNet` being the biggest term was not what anyone expected, and it is the
whole reason this round found anything: **newlib's `tanhf`/`expf` compute in
double, and the EE has no double-precision FPU**, so the 12 + 3 activations per
tile are 15 software-emulated round trips - about 340 EE cycles each.

**What came off, each measured on its own counter:**

| cut | term | before -> after | d (95 % CI) |
|---|---|---|---|
| proxy: take each in-front corner once instead of twelve edges x 2 endpoints | `proxy` | 3.26 -> 2.17 | **+1.10** |
| runNet: deadzone compare in front of the logistic | `net` | 3.96 -> 3.39 | **+0.57** |
| composite: skip a pass with no non-zero corner | `pkt` | 0.61 -> 0.31 | **+0.30** |
| `addBag`: hoist the per-column overlap out of the row loop | `proxy` | 2.17 -> 1.97 | **+0.20** |
| *(now landed, both twins)* the activation table | `net` | 3.39 -> 1.29 | **+2.11** in PCSX2; **+1.14 on hardware** |

**The same four cuts, re-measured ON HARDWARE** (`upscaler-lab`, BLSS on, the
pre-cut engine carrying the identical counters, paired by window):

| term | pre-cut | post-cut | d (95 % CI) |
|---|---|---|---|
| `proxy` (cuts 1 + 4) | 3.95 | 2.39 | **+1.56** [+1.43, +1.68] |
| `net` (cut 2) | 2.20 | 1.97 | **+0.23** [+0.20, +0.27] |
| `pkt` (cut 3) | 0.73 | 0.56 | **+0.17** [+0.17, +0.17] |
| **BLSS EE total** | **7.92** | **5.95** | **+1.96** |

**Read the two tables together before optimising anything else, because they
disagree about what is expensive.** PCSX2 puts `net` at 3.96 ms and `proxy` at
3.26; hardware puts `net` at 2.20 and `proxy` at 3.95. The emulator
**over-weights libm** relative to the console, so the activation table - the
biggest win in PCSX2 - is worth at most ~1.5 ms on hardware (`net` is only
1.97 ms in total there), while **the bag-proxy feed is the largest EE term on
the real machine**. PCSX2 transfers well for the EE *in aggregate*; it does not
transfer per-function. Attribute on hardware.

All four landed cuts are **bit-identical by construction**, and that was
checked rather than asserted: with `blssDebugView` 2 the `BLSSGRID`, `BLSSFEAT`,
`BLSSOUT` and `BLSSFILL` lines - the network's own inputs and outputs to three
decimals - are **byte-identical across 44 seconds of paired frames** before and
after. That check is the reason the instrument is permanent.

**The restated A/B:**

| arm | mean `work` | d vs BLSS off |
|---|---|---|
| BLSS off | 5.54 | - |
| BLSS on, before this round | 14.30 | **+8.76** |
| BLSS on, after the four landed cuts | 11.93 | **+6.39** |
| BLSS on, + the activation table (needs the host half) | ~9.6 | ~+4.1 |

So **27 % of the EE overhead is gone**, and the activation table - which had to
move on the engine (`TYRA_BLSS_ACT_TABLE`) and the host (`src/blss.cpp`)
**in the same commit or not at all** - has since taken another 1.14 ms on
hardware, for a bill of **5.02 ms**. `drain` read **0.02-0.05 ms
in both arms**, exactly as on hardware: these frames are EE-bound, and none of
this makes the GS the bottleneck.

### THE ANSWER: a Tyra frame CAN be GS-bound, and BLSS wins 3.4x on one (2026-08-08, REAL HARDWARE)

Everything above this heading was measured on `blssrig` — terrain plus slabs,
a scene with almost no fill — and on that fixture BLSS is a pure loss. The
question that mattered was whether *any* scene is GS-bound. It is.

Fixture: `examples/upscaler-lab`, the haze demo (12 banks x 256 alpha-blended
billboards at `size 9.0` = 3 072 large blended sprites), **BLSS off vs on**,
particles on, static camera, PAL interlaced, debug profile, Live Link / Live
Debugger / Live Logic / Remote Pad / Time Machine **all off**, deployed over
ps2link to a real PS2. 262 paired per-frame samples from the static-camera
region, `FTRAW` per-frame ticks.

| arm | mean `work` | median | p95 | FPS | `drain` |
|---|---|---|---|---|---|
| BLSS **off** | **530.48** | 534.24 | 540.6 | 1.89 | **0.02** |
| BLSS **on** | **157.44** | 158.48 | 159.9 | 6.35 | **0.02** |

**d = +373.04 ms, 95 % CI [+369.41, +376.67], sd 29.97, n = 262.** A **3.37x**
speedup. The upscaler bought 373 ms of GS for **5.91 ms of EE**:

| BLSS EE term (hardware) | ms |
|---|---|
| `proxy` (bag proxies, in submission) | 2.47 |
| `composite` EE (reproj 0.28 + feat 0.19 + net 1.93 + pkt 0.56) | 2.89 |
| `beginScene` | 0.44 |
| `endScene` | 0.11 |
| **total EE spent** | **5.91** |
| composite's GS half | 0.46 |

Two things follow, and the second one is the expensive lesson.

1. **The feature has a regime.** "An upscaler trades GS fill for EE work, and the
   generated games do not have the fill to trade" was a conclusion drawn from one
   fixture. A scene with heavy alpha-blended overdraw has enormous fill to trade,
   and there the EE bill is a rounding error against the saving.
2. **`drain` never said otherwise** — see the boxed correction above. It read
   0.02 ms in both arms of the run that shows a 3.4x GS win.

**And the demo itself is badly scaled.** 530 ms/frame is 1.9 FPS with BLSS off
and 6.3 FPS with it on; neither is a shippable frame. `upscaler-lab` was tuned
against PCSX2's FPS counter, and PCSX2 under-reports GS fill by **76x** (the
calibration above), so the haze was raised until the *emulator* moved — roughly
two orders of magnitude past what the console can draw. The fixture is excellent
as a **GS-bound stress case** and useless as a demo. It has since been re-tuned
against hardware — see the next section, whose table is the one to quote.

### The re-tuned demo, and the number to quote (2026-08-08, REAL HARDWARE)

The 3.37x above is real and was measured on a scene running at 1.9 FPS, which
nobody can watch. The haze was re-tuned **against the console** rather than
against PCSX2: **12 banks x 256 billboards at `size 9.0` became 6 banks x 32**,
same `size`, same material, same everything else — 3 072 large alpha-blended
sprites down to **192**. Six banks instead of twelve keeps haze spread across the
yard (x from −8 to +9, z from −18 to +15, all above eye height); cutting `count`
rather than `size` keeps each puff's *look* identical and takes the EE cost of
2 880 vertices off both arms.

Fixture and protocol otherwise unchanged from "THE ANSWER" above: `buildProfile
debug`, PAL interlaced, Live Link / Live Debugger / Live Logic / Remote Pad /
Time Machine **all off**, deployed over ps2link, the 20 s Cutscene Director tour
followed by a parked camera. Samples are the `FTRAW` per-frame ticks of frames
**550–1611**, which are inside the parked region in **both** arms (the tour is
driven by `dt`, so it occupies a different *number* of frames in each arm — pair
inside the parked region, never across the tour). **Two runs per arm, all four
cross-pairings.** Sign convention: **d = work(BLSS off) − work(BLSS on);
d > 0 means BLSS made the frame shorter.**

| arm | mean | median | p95 | max | FPS | `drain` |
|---|---|---|---|---|---|---|
| BLSS **off** | **52.86** | 53.01 | 54.44 | 55.37 | **18.9** | 0.02 |
| BLSS **on** | **32.98** | 32.93 | 33.62 | 34.37 | **30.3** | 0.02 |

**d = +19.88 ms, 95 % CI [+19.81, +19.95], sd 1.12, n = 1024 paired frames per
pairing** (2 048 pooled frames per arm). The four cross-pairings span **0.014 ms**
against an effect of 19.9. A **1.60x** speedup, in a scene a person can look at.

> **The table above is the jitter-ON timing. The fixture ships jitter OFF, and
> the re-run has now LANDED (2026-08-09, real hardware) — the prediction held.**
> The re-run had failed twice before, both times because the console dropped off
> the LAN mid-session; on the third attempt it stayed up for eleven deploys.
> Same fixture, same protocol, same window (frames 550–1611, parked in both arms
> — the off arm parks at ~350 and the on arm at ~450), **two runs per arm and all
> four cross-pairings**:
>
> | arm | mean | median | p95 | max | FPS |
> |---|---|---|---|---|---|
> | BLSS **off** | **52.95** | 53.11 | 54.52 | 55.38 | **18.9** |
> | BLSS **on**, jitter **off** | **32.42** | 32.36 | 33.07 | 33.94 | **30.8** |
>
> **d = +20.53 ms, 95 % CI [+20.46, +20.61], sd 1.12, n = 1024 per pairing**
> (2 048 pooled per arm); the four cross-pairings span **0.010 ms**. A **1.63x**
> speedup, against **1.60x** with the jitter on.
>
> So the prediction — "the timing holds, because the retrained net asks for the
> same passes and the jitter moves *where* the raster samples rather than how
> much of it there is" — is **confirmed, and slightly beaten**. The off arm is
> unchanged (52.95 vs 52.86, well inside run-to-run drift) and the BLSS arm got
> **0.56 ms faster** (32.42 vs 32.98). `BLSSFILL` reads `passes = 1.56` on the
> shipped jitter-off build, the same figure the pre-change build logged, which is
> the substance of the prediction: the net did not start asking for more fill.
> The extra 0.56 ms is the jitter arithmetic itself coming out of the raster
> setup, and it is not large enough to be worth chasing further.
>
> **Two rig facts the earlier attempts paid for, so nobody pays again.**
> A plain `tyrax-editor --build` **deletes `bin/ps2link.run`** (`runner.cpp:350`,
> so a leftover PS2 marker cannot confuse a PCSX2 run) — and without that marker
> the generated game decides it is *not* under ps2link, sets `writeLogsToFile`,
> and writes its whole `FRAMETIME`/`FTRAW` stream into `bin/log.txt` over `host:`
> instead of to the console. The symptom is a `run.log` that stops after
> ps2link's own `open fd = -1` line, and a benchmark measured with per-line
> network file I/O inside the thing being measured. **Recreate the marker after
> every build** that will be deployed by hand. And `taskkill` without `/F` does
> not close `ps2client` (no window, no handler), so the tail of a redirected run
> is lost however you stop it — run long enough that the `FTRAW` blocks you need
> are well before the end, rather than trying to exit gracefully.

**Two points fix the whole fill model**, because the old 3 072-particle run and
this 192-particle one differ in nothing but particle count:

| | ms per haze particle | intercept (N = 0) |
|---|---|---|
| BLSS off | 0.1658 | 21.2 ms |
| BLSS on | 0.0429 | 24.8 ms |

Three things fall out, and the second one corrects this page.

1. **BLSS keeps 25.9 % of the scene's fill** (0.0429 / 0.1658). Not half —
   **`blssScale 0` is `Scale::X2Y2`, half in *each* axis**, so a quarter of the
   pixels survive and the extra 0.9 % is measurement. The break-even below was
   computed against a 2x factor and was therefore about 70 % too pessimistic.
2. **The base scene costs 21.2 ms with BLSS off and 24.8 ms with it on.** That
   +3.6 ms is BLSS' EE bill *net of* the ~2.6 full-screen coverages of base fill
   (terrain, sky, cottages, rain, campfires) that it also halves. It is the floor:
   this scene cannot go above ~40 FPS with BLSS on however thin the haze gets,
   which is why the demo lands at 19 → 30 FPS and not at the 20 → 45 that was
   asked for. **Raising the off-arm frame rate shrinks the win**, because the
   thing being removed is the only thing BLSS is paid to remove.
3. **Break-even on this scene is ~29 of these puffs**, i.e. about **8 full-screen
   coverages** — lower than the general figure below because the base scene's own
   fill is part of the saving.

**The activation table is on in this build**, on both twins, and the split line
says what it bought on hardware:

```
FTSPLIT f=2400 proxy=2.69 reproj=0.28 feat=0.19 net=0.79 pkt=0.55
```

`net` **1.93 → 0.79 ms**. The PCSX2 round predicted 2.11 ms and the hardware
bound was stated as "at most ~1.5"; the truth is **1.14 ms**, and the reason the
emulator over-predicted is the one already recorded above — it over-weights libm
and does not transfer per-function. **BLSS' EE bill on hardware is now
`proxy` 2.69 + `reproj` 0.28 + `feat` 0.19 + `net` 0.79 + `pkt` 0.55 +
`begin` 0.41 + `end` 0.11 = 5.02 ms**, and `proxy` is now more than half of it.

> **Superseded.** The `floorf` cut has since taken this to **4.60 ms** on
> hardware and the fill-retention factor has been measured rather than assumed,
> which moves break-even from 13 coverages to **11.5**. See "Both EE cuts, priced
> on hardware" and "Calibrating the speed model against hardware" below; quote
> those, not this paragraph.

### The EE floor, and whether the EE cost still matters

With everything above applied, the EE cost of BLSS on this fixture is
**~4.3 ms** and it decomposes into things that cannot simply be deleted:

| term | ms | why it is a floor |
|---|---|---|
| `proxy` | 2.39 | one box per VU1 package must be projected and accumulated into tiles; cutting further means describing the frame more coarsely, which is a **twin-contract change** |
| `net` | 0.79 | the MLP, with the activation table in (it took 1.14 ms off); what is left is 108 multiply-adds x N tiles and no table can remove it |
| `reproj` + `feat` + `pkt` | 1.03 | 255 corners, N tiles, and the grid packet |
| `begin` + `end` | 0.55 | the raster redirect and its drain |

(Hardware, `upscaler-lab`. `blssrig` in PCSX2 gives 1.97 / 1.29 / 0.84 / 0.14
for the same rows - a different scene and a different machine, so compare the
shape, not the digits.)

Against that floor, what BLSS WINS is **74.1 % of the scene's GS fill** (measured
— the render is quarter-area, see the re-tuned demo above; this page said "half"
for a week and it was wrong), minus the fill the composite adds back (0.46 ms
measured on hardware). At the calibrated **0.587 ms per full-screen blended
textured pass** and the post-activation-table EE bill of **5.02 ms**, break-even
is

> 0.741 x 0.587 x C > 5.02 + 0.46 ms, i.e. **a scene rasterising more than about
> 13 full-screen coverages.**

(**That line has since been re-measured too**: with the `floorf` cut in and the
retention factor fitted on hardware rather than assumed, it is
`0.7548 x 0.5872 x C > 4.60 + 0.50`, i.e. **11.5 coverages**. The paragraph below
is still the right way to think about it; only the number moved.)

The old figure on this line was **22**, computed off the wrong resolution factor
and the pre-table EE bill. `upscaler-lab` as first built rasterised on the order
of *nine hundred* coverages, which is why it won by 3.4x and also why it was a
broken demo; **re-tuned it rasterises 58.7 blended-pass equivalents by
measurement and 72.63 by the estimator** (that discrepancy is the open question
in "Calibrating the speed model against hardware" below) and still wins by
**1.63x**. `blssrig` rasterises a handful,
which is why it loses by 6.4 ms. **The break-even is a real line and generated
games fall on both sides of it** - so the EE floor is not academic: it is what
decides how much overdraw a project needs before the feature pays. Every
millisecond taken off the **4.60 ms** bill lowers that bar by roughly **2.3**
full-screen coverages.

One more measured negative, so nobody spends a day on it: the composite's EE
work *can* be moved in front of `endScene`'s drain, because it is a pure
function of the proxies gathered during submission. It is not worth doing.
`tBlssEnd` - the GS overhang that reordering would hide the EE work behind - is
**0.05 ms in PCSX2 and 0.03 ms on hardware**. The maximum possible saving is
that number.

### Pricing the proxy feed, and the two things that were actually wrong (2026-08-09)

`proxy` is the largest EE term on hardware (2.69 ms of a 5.02 ms bill), and the
round above could only say that much — not *which part of it*. It has two halves
that respond to completely different fixes, so `tBlssAccum` was added to split
the accumulator (`addBag`, i.e. per (proxy, **tile**)) from the projection (eight
corners through the MVP, once per VU1 package), and `BLSSGRID` gained the two
counts that turn either half into a per-unit cost:

```
BLSSGRID 16x14 tiles, 147 covered, 198 proxy(ies) of 262 projected, 1499 tile update(s), scale 2x2
FTSPLIT  f=4150 proxy=1.63/0.62 reproj=0.28 feat=0.12 net=0.79 pkt=0.38
```

**Read those two lines together before optimising this code, because they
overturn the guess written in `addBag`'s own comment.** On a parked frame of
`examples/upscaler-lab`, 262 boxes are projected, **198 survive**, and they touch
**1 499 tiles between them — 7.6 tiles each**. A proxy is therefore a *small*
object, roughly 3 × 2 tiles; the loop comment claiming the feed is "dominated by
THIS loop rather than by the eight-corner projection" is wrong, and the split
counter puts the projection at 1.01 ms against the accumulator's 0.79. Both
halves are dominated by their **per-proxy fixed costs**, not by per-tile work.
The 64 boxes that are projected and then rejected (24 %) are pure loss.

> **Everything in this section was PCSX2**, because the console was off the LAN
> for the whole session — the same failure as the owed jitter A/B above. That is
> admissible for the **counts**, which are scene facts and transfer exactly, and
> for **bit-identity**, which is a property of the code. It is **not** admissible
> for attribution — this page's own rule — so the hardware figures were owed.
> **They have since been paid: see "Both EE cuts, priced on hardware" below**,
> which supersedes every attribution figure in this section.

**What came off, each on its own counter:**

| cut | proxy | accum | pkt | twin? |
|---|---|---|---|---|
| baseline (the counters only) | 1.80 | 0.79 | 0.46 | — |
| interleave the six tile accumulators + an interior fast path | **1.95** | **0.92** | 0.46 | **reverted — see below** |
| `floorf` → an inline floor-to-int | **1.63** | **0.62** | **0.38** | none, bit-identical |
| …and the proxy budget on top | **1.25** | **0.45** | 0.38 | **yes — engine half only, switch off** |

**The `floorf` cut is the activation-table finding one function along.** `addBag`
derives its tile range with four `floorf` calls, and `emitGrid` two per grid
corner of the reprojected pass — and newlib's `floorf` is a real out-of-line
routine, **68 instructions plus the call sequence**, confirmed by disassembling
the shipped ELF (`jal 20dea8 <floorf>`, four of them in `addBag`'s prologue).
That is 792 + 510 library calls a frame to compute what a cast and a compare do
exactly: a cast to int truncates toward zero, which *is* floor for a non-negative
value and one too large for a negative non-integer, so one compare fixes
precisely that case and the result is bit-identical for every finite argument in
int range. Same lesson as `tanhf`/`expf`: **the arithmetic was never the cost,
the libm round trip was.** PCSX2 over-weights libm, so treat −0.25 ms as this
cut's *ceiling* on hardware rather than its value.

**And a measured negative, so nobody spends the afternoon on it.** The obvious
next move — interleave the six per-tile accumulators into one struct so a tile
update touches one cache line instead of six — **makes it slower** (`accum`
0.79 → 0.92), and the arithmetic says why it could never have helped: a float is
4 bytes and a cache line is 64, so **all sixteen tiles of a grid row already sit
in one line of each array**. Six parallel arrays cost six lines per row-span and
one interleaved array costs exactly six as well. The interior fast path that came
with it (a fully-covered tile has `a` exactly 1, so three multiplies drop out) is
aimed at proxies spanning many tiles when the measured mean is 7.6 — so nearly
every proxy is *all border and no interior*, and the scan that locates the
interior is pure overhead. Both reverted. **Check the size distribution before
optimising a loop for the big case.**

**The proxy budget is the real lever, and it is a twin-contract change.** The
rule is stated in [blss-reconstruction.md](blss-reconstruction.md) §2 and
implemented behind `TYRA_BLSS_PROXY_BUDGET`, which **ships at 0** because
`src/blsscorpus.cpp` has not been taught to cut the same way yet. Switched on it
takes **198 proxies to 116 and 262 projections to 174**, and what it costs in
description is one channel: `coverage` mean 0.631 → **0.638**, with `depth`,
`depthGrad`, `edgeDens` and `texDetail` unchanged to three decimals, the
covered-tile count identical at 147, `BLSSWORST` identical, and `BLSSFILL`
identical at `passes = 1.56`. A 41 % cut in the feed for 0.007 of one input's
range.

**Bit-identity was checked, not asserted**, the way this page requires: under
`blssDebugView 2` the `BLSSWORST` / `BLSSFEAT` / `BLSSOUT` / `BLSSFILL` lines of
the shipped build are **byte-identical** to the pre-cut build's across the parked
region. `BLSSGRID`'s tile-update column is *not* a byte-identity channel — it
wobbles ±5 of 1 499 frame to frame on this fixture, equally in every build.

**VU0 was evaluated and is not worth building.** The eight-corner projection
looks like exactly the shape `renderer/rt/vu0_raytracer` proves out, but three
things settle it. The one genuinely heavy piece of that math —
`M4x4::operator*(Vec4)` — **already runs on VU0**, in macro mode, as COP2 inline
asm (`m4x4.cpp`); what is left is 8 × 9 scalar flops of corner expansion, which
is not where the time is. Micro mode would have to pay a `vcallms` plus a `cfc2`
polling stall per call and the EE cannot overlap it (macro-mode COP2 shares VU0's
register file, which is why the ray tracer runs synchronously), so at 262 calls a
frame the sync alone would plausibly exceed the work. Batching a whole bag's
boxes into one kick would amortise that — but the budget rule above removes 34 %
of the calls outright for a fraction of the effort, and the honest ordering is to
take that first and re-measure. Recorded so it is not re-derived from scratch:
the batched form is one kick per bag, ≤ 32 boxes in at 2 qwords each and 2 qwords
of clamped bbox + `w` range out, comfortably inside VU0's 4 KB data memory.

**`emitGrid` on VU1 was assessed and declined.** Sending 255 corner alphas and
letting VU1 expand the strips would cut most of the ~5 700-qword packet build and
its DMA, and the machinery exists — the billboard programs already expand 21
centres into 126 vertices, and `ensureProgramSet` already swaps a non-resident
program set in. Three things against it, in order of weight. The prize is
**`pkt`, 0.55 ms on hardware**, a fifth of `proxy`, and part of it has just come
off for two lines of `floorf`. Micro memory sits at **2 036 of 2 042** with the
clip family resident, so a grid program is a third on-demand set and another swap
per frame. And the sparsity rule would have to move: `emitGrid` skips a cell when
all four of its corner alphas are zero, which on VU1 becomes ADC bits on a
fixed-length strip rather than a broken run — a different thing arriving at the
GS, and **`emitGrid`'s skip rule is what the host's cost model is fitted to**
(blss-reconstruction.md §6). A twin-contract risk for a fifth of the prize is the
wrong trade while `proxy` is unfinished.

**The break-even was left as a PROJECTION by this round** — "about 11
coverages", off an EE bill projected near 4.25 ms — and the page said in terms
that it must not be quoted as one. The next section replaces it with a
measurement.

### Both EE cuts, priced on hardware (2026-08-09)

Both cuts above were PCSX2-only, and this page's own rule is that **PCSX2 does
not transfer per-function attribution** — it ranked `net` above `proxy` where
hardware ranks the reverse. So both were re-measured on the console, each
against its own switch in one code base rather than against a different build:
`TYRA_BLSS_FLOORF_LIBM` (a temporary switch, added and removed in the measuring
session) restores newlib's `floorf`, and `TYRA_BLSS_PROXY_BUDGET` was forced on
locally. Fixture `examples/upscaler-lab` at `%TEMP%\tyra-editor-test\ulabhw`,
BLSS on, the parked six-bank region (frames 600–2559), **two runs per arm, all
four cross-pairings, paired on the `f=` window key — 160 paired 50-frame
windows per row.** Sign: **d > 0 = the cut saved time.**

| term | libm `floorf` | inline `floorToInt` | d (95 % CI) |
|---|---|---|---|
| `proxy` | 2.499 | **2.336** | **+0.164** [+0.162, +0.165] |
| …of which `accum` | 0.989 | **0.838** | **+0.151** [+0.150, +0.153] |
| `pkt` | 0.560 | **0.500** | **+0.060** [+0.060, +0.060] |
| `reproj` / `feat` / `net` / `begin` / `end` | — | — | **0.000 ± 0.001** |
| **BLSS EE bill** | **4.824** | **4.597** | **+0.227** [+0.225, +0.229] |
| whole-frame `work` | 32.677 | 32.312 | +0.365 [+0.357, +0.373] |

**PCSX2 called this one almost exactly right**, which is worth recording next to
the case where it did not: the emulator predicted `proxy` −0.17, `accum` −0.17
and `pkt` −0.08, and hardware paid −0.164, −0.151 and −0.060. The stated
"−0.25 ms is a ceiling on hardware" held, and hardware collected **91 %** of it.
The lesson is not "PCSX2 transfers after all" — it is that a libm cut in code
the emulator executes the same number of times transfers, while a libm cut whose
*cost per call* the emulator gets wrong (`tanhf`/`expf`, where it over-predicted
2.11 against a real 1.14) does not. **`accum` moved by 92 % of `proxy`'s total**,
which is exactly where the four `floorf` calls live, so the attribution is the
mechanism and not a coincidence.

Note the whole frame moved **more** than the counters attribute (+0.365 vs
+0.227). The counters bracket the call sites; the register spills a real call
forces on the enclosing loops land outside them. Quote **+0.227** as the
attributable figure and treat the cut as worth at least that.

The proxy budget, measured the same way (and **still shipping at 0** — the host
twin in `src/blsscorpus.cpp` does not cut the same way yet, and flipping one side
alone silently invalidates every trained net):

| term | budget off (shipped) | budget on | d (95 % CI) |
|---|---|---|---|
| `proxy` | 2.336 | **1.907** | **+0.429** [+0.427, +0.430] |
| …of which `accum` | 0.838 | **0.624** | **+0.214** [+0.212, +0.215] |
| everything else | — | — | **0.000 ± 0.003** |
| **BLSS EE bill** | **4.597** | **4.167** | **+0.429** [+0.427, +0.432] |

**Bit-identity was proved on hardware, not asserted**, and it needed a fixture
change to be provable at all. Under `blssDebugView 2` the four channels are
compared across builds — but the parked fixture still runs rain, two campfires
and their smoke, and those are **dt-driven**, so two runs at two frame rates put
them in different places and a byte comparison fails for a reason that has
nothing to do with the code under test. The sweep script therefore ends in a
segment that hides **every** emitter, giving a genuinely still scene. There,
across **44 paired 1 Hz samples**, `BLSSWORST` / `BLSSFEAT` / `BLSSOUT` /
`BLSSFILL` are **byte-identical** between the libm and inline builds — one
distinct value each, in both builds — and `BLSSGRID` is identical in every
column (`16x14 tiles, 147 covered, 198 proxy(ies) of 262 projected, scale 2x2`)
except the tile-update counter, which this page already records as a non-identity
channel and which spans **exactly the same 1464–1474** in both. The budget arm
differs in exactly one place, as designed: `coverage` 0.631 → 0.638.

**THE EE BILL AND THE BREAK-EVEN, FROM HARDWARE.** The shipped build's bill is
`proxy` 2.34 + `reproj` 0.28 + `feat` 0.19 + `net` 0.78 + `pkt` 0.50 +
`begin` 0.41 + `end` 0.10 = **4.60 ms**, plus the composite's own fill, measured
in the same runs at **0.50 ms**. With the fill-retention factor measured rather
than assumed (0.7548 — see the next section), break-even is

> 0.7548 × 0.5872 × C > 4.60 + 0.50, i.e. **11.5 full-screen coverages**

replacing the projected "about 11" and the earlier measured **13**. With the
proxy budget on it would be **10.5**. Each millisecond off the bill is still
worth about **2.3** coverages.

### Calibrating the speed model against hardware (2026-08-09)

The editor predicts a project's speedup before building, from
`saved ≈ 0.741 × (the scene's fill time)` and a 5.02 ms cost
(`fill::` in `src/blss_ui.hpp`). Against the single hardware point that existed,
**the model over-predicted badly**: at the anchor's ~75 coverages it says
27.1 ms saved and the console measured 19.88. One point cannot say whether the
retention term, the cost or the fill estimate is at fault, so the UI quoted a
1.6–2.6x range with named assumptions. This round replaces that with a fit.

**The knob.** `examples/upscaler-lab`'s Square/Circle flow graph turned out to be
useless for this: `SetParticles` is a single global switch, all emitters on or
all off, so it can remove the fill but not *vary* it — and a pad-driven toggle
would need the Remote Pad preference on, which is per-frame HostFs I/O inside the
thing being measured. Instead a **frame-indexed object script** steps the six
haze banks by index, so segment *k* of the BLSS-off run shows the same load as
segment *k* of the BLSS-on run and the samples stay paired. The camera is not
touched, so frames 0–2047 remain the published fixture exactly and the jitter A/B
window above comes out of the same runs.

**A script's own frame counter and the rig's `f=` index are the same number** —
checked, not assumed: the script logs each transition with its counter, and the
step in `work` lands in exactly the `FRAMETIME` window that spans it (the
transition at script frame 2560 shows as 53.0 → 44.4 → 39.4 across `f=2500`,
`f=2550`, `f=2600`). So segment boundaries can be quoted straight as FTRAW frame
indices, with no offset to hunt for. Do not take that for granted in a fixture
with a loading screen that renders frames before the first `update()`.

**The segment that makes the fit separable, and why it is not optional.** An
emitter that is hidden skips its simulation *and* its fill (`updateParticles`
bails on `!o.visible`), so stepping the bank count moves EE cost and GS fill
**together** and the slope conflates them — which is exactly the flaw in the
two-point model this section replaces. `emitSize` is read live inside the
per-particle loop, so one extra segment puts all six banks back at
`emitSize 0.05`: every particle still simulated, still submitted, raster area
gone. **seg4 − seg3 is the haze's EE cost alone; seg0 − seg4 is its fill alone.**

Two runs per arm, all four cross-pairings, 384 frames per segment after a
128-frame settle:

| segment | haze | BLSS off | BLSS on | saved (95 % CI) |
|---|---|---|---|---|
| seg0 | 6 banks (192) | 53.19 | 32.31 | **+20.88** [+20.78, +20.97] |
| seg1 | 4 banks (128) | 40.39 | 28.81 | **+11.58** [+11.49, +11.67] |
| seg2 | 2 banks (64) | 27.46 | 25.67 | **+1.79** [+1.75, +1.84] |
| seg3 | 0 banks | 18.93 | 23.52 | **−4.59** [−4.61, −4.57] |
| seg4 | 192, `emitSize 0.05` | 19.40 | 24.03 | **−4.63** [−4.66, −4.60] |

The separation: the haze's **EE cost is 0.47 ms** (off arm) / 0.51 (on arm) for
192 particles, and its **fill is 33.79 ms** — so EE is **1.4 %** of the load
being varied. seg4 and seg3 give the *same* `saved` to within 0.04 ms, which is
the internal check that the tiny particles really did stop rasterising while
still costing both arms the same EE.

**The fit**, over a 0.7–34 ms fill range:

> **saved(ms) = 0.7548 × (scene fill, ms) − 5.10**,
> residuals −0.094 / +0.148 / +0.006 / −0.060, **RMS 0.093 ms**

- **The retention term is right — if anything the model is 2 % conservative.**
  Measured **0.7548** against the model's 0.741, i.e. BLSS keeps 24.5 % of the
  fill, not 25.9 %. (The old 25.9 % came from a slope ratio that included the
  particles' EE cost in both arms, which biases it toward 1; that is the term
  the seg4 trick removes.)
- **The cost term is right too**: the fit's intercept, 5.10 ms, is the
  independently-counted 4.60 + 0.50 to two decimals. Two instruments, one number.
- **So the model's physics is sound and its INPUT is what is wrong.** Fed the
  scene's *true* fill it predicts every level within 0.9 ms:

  | haze | true fill | measured saved | old model (0.741 / 5.48) | fitted |
  |---|---|---|---|---|
  | 6 banks | 34.46 | +20.88 | +20.06 | +20.91 |
  | 4 banks | 21.82 | +11.58 | +10.69 | +11.37 |
  | 2 banks | 9.04 | +1.79 | +1.22 | +1.73 |
  | 0 banks | 0.67 | −4.59 | −4.98 | −4.59 |

  **The whole 7 ms over-prediction is the coverage estimate.** Working back from
  the measurement, this scene's total fill is **34.46 ms = 58.7 full-screen
  blended-textured passes**. The estimator itself — run headlessly against the
  same fixture, which is what `--blss-coverage` was added for — reports
  **72.63** (mean over its six corpus shots, p95 **94.39**). So
  `coverages × 0.587 ms` over-states this frame by **23.7 %**, and the effective
  cost of one *counted* coverage here is **0.474 ms**, not 0.587.

  The **75** this section originally compared against was `kAnchorCoverages` as
  recorded, not a figure re-derived from the tool. The tool says 72.63; quote
  that. The gap to close is **13.93 coverages**, and the next two paragraphs are
  about which mechanism closes it — because the obvious one does not.

**What this is worth to a caller.** The estimator lives in `src/blss_ui.hpp` /
`src/blss_window.cpp`, which this round does not own, so the numbers are recorded
here rather than applied: `kSavedFraction` 0.741 → **0.7548**, `kEeCostMs` 5.02 →
**4.60**, `kCompositeGsMs` 0.46 → **0.50**, `breakEven()` ~13 → **11.5**,
`kAnchorCoverages` 75 → **58.7**, `kAnchorSpeedup` 1.60 → **1.63**,
`kAnchorOffMs`/`kAnchorOnMs` 52.86/32.98 → **52.95 / 32.42**. The one that
matters is the anchor: **the fill model is fine and the coverage counter reads
23.7 % high**, so recalibrating the counter (or the ms-per-counted-coverage) is
what collapses the published 1.6–2.6x range, not touching the 0.741.

**The owed check has been paid, and it falsified the explanation above.**
`--blss-coverage <projectDir>` is now the headless twin of the window's "Will the
frame get faster?" button — same `blss::measureCoverage`, same
`blssui::speedFrom`, no second implementation to keep honest — and against
`examples/upscaler-lab` it prints **72.63**, split **0.98 geometry + 71.65
emitters**.

> **RETRACTED: "the estimator over-reads because it counts terrain, cottages and
> blended puffs alike in one unit."** That was this page's stated mechanism for
> the over-read, and on this fixture it is **wrong**, by its own numbers.
>
> - The estimator counts **0.98** coverages of geometry *in total*. The hardware
>   run's own 0-banks segment prices all geometry plus every non-haze emitter at
>   0.67 ms, i.e. **≤ 1.14 blended-pass equivalents** — so the counted geometry is
>   at or *below* its own measured ceiling. It is not over-priced.
> - **98.7 % of this scene's counted coverage is the emitter half**, and an
>   alpha-blended textured puff fragment *is* the 1.0-weight unit `kPassMs` was
>   calibrated on. There is nothing there to weight down.
> - Applying the weighting rule anyway moves this anchor by **0.49 coverages**
>   against a **13.93** gap. It cannot be the mechanism.
>
> So `kAnchorCoverages` was **not** set by rescaling the counter's output, and the
> counter has deliberately been left alone. A weight per fragment kind may still
> be right in general — a scene that really is mostly opaque untextured terrain
> would be over-priced by this unit — but it is not what is happening here, and
> shipping a recalibration fitted to a falsified mechanism is exactly the shape of
> mistake this feature has already made eight times.

**The live hypothesis is the camera, and if it holds there is no counter bug at
all.** The two instruments do not look at the same frames. The console ran the
fixture's **own gameplay camera** — the Cutscene Director tour, then the parked
vantage the A/B samples. The estimator averages the **six synthetic corpus
shots**, whose per-shot totals span **36.2 to 88.4** coverages. A single camera at
**57.7** sits comfortably inside that spread, right next to the 58.7 the hardware
fit implies. On that reading each instrument answers its own question correctly —
"how much fill does this scene ask for, averaged over six camera moves" versus
"how much did *this* camera ask for" — and one has been quietly used as the other.

**The experiment that would settle it**, and nothing should be rescaled before it
runs: take the coverage estimate **under the fixture's own camera** (the Cutscene
track plus the parked vantage, not the six automatic moves) and compare *that*
against 58.7. Land near 58.7 and the counter is fine and the anchor was a
sampling mismatch; still read near 72 and the over-read is real and its mechanism
is unidentified. `--blss-coverage` already reports per-shot, so this is a
shot-plan question rather than a new instrument.

**And this is still one scene.** The slope is fitted over five points on a single
fixture whose variable load is alpha-blended billboards; a scene whose overdraw is
opaque geometry may well retain a different fraction, and nothing here measures
that.

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

**Re-confirmed 2026-08-09 on the shipped example**, after `examples/upscaler-lab`
was switched to `blssJitter: false` and its net retrained: 40 back-to-back
captures, best two-cluster split **38 / 2**, within 0.0282, between 0.0352 — a
ratio of **1.25x** where build B gives a balanced 20/20 at 1.15/255 — and a
SAD-minimising lag of **(0,0)**. Not alternating.

**Two confounds cost a whole capture burst before that run, and neither is the
upscaler.** Both are worth checking before any future use of this gate:

- **The debug HUD prints a live frame counter.** `showFps` / `showMemory` /
  `showProfiler` are on in the fixtures, and the digits change every frame. The
  original recipe said "rows below the HUD"; cropping is fragile, and turning the
  three settings off is one edit and removes the problem at the source.
- **PAL interlaced mode alternates fields, which IS a period-2 signal.** A
  window capture of an interlaced build alternates between two images *by
  construction*, with nothing wrong with the renderer at all — this is the same
  mechanism that makes a strict pixel A/B between two runs impossible
  (tyra-testing). Set `displayMode` to **progressive** (480p) for the gate. This
  is why the row above says 480p, and it is now the reason rather than a detail.

With both left in, the same still scene measures **0.10-0.15/255** of pure
instrument noise — comfortably above the 0.77/255 artefact the gate exists to
find, and it clusters into nothing. That is the fourth time an instrument on this
page has reported the wrong thing about this artefact, and the first time it
would have reported a *shake* that was not there rather than missing one that
was.

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

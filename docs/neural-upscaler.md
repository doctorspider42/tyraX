# The neural upscaler (BLSS)

**BLSS** — *Bieda-Level Super Sampling* (Polish *bieda*, "poverty": the budget
cousin of DLSS). The 3D scene renders into a **half-resolution** GS render
target; a small **neural network**, trained on the host and baked into the game,
then decides — per 32×32 screen tile, every frame — *how* that image should be
blown up to the display buffer, and how much of the previous frame to reuse.

It is not a super-resolution network: nothing on the PS2 processes a whole image
through a net (327 680 pixels × even one MAC is out of the question at 50 Hz).
BLSS is the other half of DLSS — **the part that picks and blends
reconstruction kernels** — done honestly, on hardware that has a 147 MHz
rasteriser and no pixel shaders at all.

> Status: **proof of concept**, off by default.
>
> **Fit the project you will ship, and ship that net.** That is the one sentence
> on this page with a decision in it. A net fitted to the **built-in procedural
> corpus** measured **−0.40 dB — worse than doing nothing** — on a real project's
> own scenes, while the same trainer fitted to *that project's* scenes measured
> **+0.06 dB** against an oracle ceiling of +0.77. `--blss-train <projectDir>`,
> or the corpus switch in the window's header, which defaults to it; see
> [Training on your own project](#training-on-your-own-project). And **some
> scenes have no ceiling at all** — `--blss-eval <projectDir>` says so in one line
> before you spend an afternoon on it, and **needs no trained network to say it**
> ([net-free evaluation](#training)).
>
> The network is real, trained and measured on the host, where it beats every
> fixed kernel in distribution — and, **on the bestiary**, on content it was never
> trained on, beats a plain bilinear upscale by **+0.42 dB** (leave-one-shot-out
> cross-validation over 13 shots × 3 seeds = 39 fold-runs, sd 0.35, 3 of 39 below
> bilinear, 1.80 mean full-screen passes; **+0.26 dB** over the six folds that
> took no part in choosing the defaults). See
> [Measured](#the-out-of-distribution-number-and-how-to-get-one-that-means-something).
> That number is about the bestiary. It is not a promise about your game, and
> leading with it is a mistake this page and the settings panel both had to
> correct.
>
> **This page said "≈+0.1 dB, statistically a draw" until the measurement was
> done properly, and that was wrong in the pessimistic direction** — the ±0.4 dB
> it blamed on the training seed turned out to be which *shot* you held out. The
> methodology is [the fifth entry](#measured-is-not-optimised-six-times) and it
> is the most transferable thing here.
>
> What is now known, and both answers are bad. **The oscillation is still
> present** — measured, not watched: with the fill term in and a net trained on
> the project's own scenes, a static-camera frame alternates between two images,
> 30.8 % of the picture changing every frame
> (see [The oscillation](#the-oscillation)). `blssJitter: false` removes it
> completely, at the cost of the temporal supersampling. And **a BLSS frame has
> now been timed on a real PS2**: it cost **+9.83 ms per frame** and saved
> nothing, because the frame was EE-bound and had no GS fill to trade
> ([profiling.md](profiling.md), "Timing a frame that BLSS is in"). Do not
> enable this yet.

## Why this can work at all on a PS2

Three facts about the hardware line up:

1. **`XYOFFSET` is 12.4 fixed point.** The GS addresses the raster in
   *sixteenths of a pixel*, so the whole frame can be jittered by a precise
   sub-pixel offset for free — no projection-matrix change, no per-vertex work.
   That is a temporal supersampling pattern, handed to us by the register file.
2. **The GS blend unit `(A−B)·C ≫ 7 + D` takes `C` from per-vertex alpha.** Draw
   a Gouraud-shaded grid instead of a sprite and the blend factor becomes a
   *smooth spatial field* interpolated by the rasteriser. That is exactly the
   shape of the network's output: "how much of this kernel, here".
3. **Double buffering already keeps a full-resolution history.** The other
   display buffer holds the previously presented frame — finished, composited,
   512×448, sitting in VRAM whether you use it or not. So the temporal pass
   costs **zero** extra memory, and because the frame it reuses was itself
   composited, a still camera keeps accumulating instead of stopping at two
   samples.

So the network's inference does not have to touch pixels, and its output does
not have to be uploaded as a texture. It rides in on 255 vertex colours.

## The pipeline

```
                    jitter XYOFFSET by -4/16 or +4/16 px
                                    |
   3D scene  --->  low-res target  (256x224, PSMCT32, z shrinks to match)
                                    |
   per-tile features (EE, no framebuffer readback)
        motion, depth, depth gradient, geometric edge density,
        texel density, coverage
                                    v
              MLP  6 -> 12 -> 3   (per tile, 224 tiles)
                                    v
        wA (point)   wC (temporal)   wD (sharpen)
                                    v
   composite into the 512x448 display buffer, 1..5 Gouraud grid passes
        the temporal pass samples THE OTHER DISPLAY BUFFER
        (last frame's finished image, full res, already there)
```

The exact arithmetic — every sample, shift and clamp — is
[the reconstruction math](blss-reconstruction.md), which is also the contract
between the host trainer and the console.

### 1. Render low

`RendererCoreBlss::beginScene()` opens the same kind of raster-redirect bracket
the env map and the shadow map already use — `FRAME`/`ZBUF`/`SCISSOR`/`XYOFFSET`
pointed at the low-res target — and `endScene()` restores the display buffer. The
game-facing coordinate space does not change: the projection is built at the
*render* height, exactly the way `DisplayMode::InterlacedField` already does it
(see the `getRenderHeightF()` split in the engine skill).

#### The bracket is explicit state now, and that is why the others nest

The redirect is not private to BLSS. `RendererCoreGS` carries a **`RasterTarget`**
— frame address, `FBW`, scissor rect, `XYOFFSET` in 1/16 px — published through
`redirectRasterTo()` / `endRasterRedirect()` / `getRasterTarget()`, and every
bracket in the engine puts the raster back with the shared
`emitRasterRestore()`. The env map, the camera feed and the shadow map restore
*whatever was redirected before them* without any of them knowing BLSS exists.

They used to restore `gs->getCurrentFrameBuffer()` plus
`settings->getWidth()`/`getRenderHeightF()` — the **display** buffer,
unconditionally. All three run inside the generated `renderScene()`, which BLSS
brackets, so the first one cancelled the redirect and the whole rest of the frame
drew full-resolution into the display buffer, where the composite's opaque base
pass then painted over it. No assert, no visual signature beyond "BLSS did
nothing to that part of the frame".

**The obvious fix was the wrong one, and checking that was the point.** This page
and the backlog both used to prescribe the minimal version: make
`getCurrentFrameBuffer()` return the BLSS target while the bracket is open, "so
those three `end()` implementations restore the right thing and none of them
changes". They restore **four** registers and only `FRAME` comes from that
accessor. `SCISSOR` and `XYOFFSET` come from `RendererSettings`, so the
accessor-only fix would have left a 512-wide scissor over a 256-wide `FRAME`
(writes wrap into the next row) and a raster window centred on the wrong window,
with the frame's sub-pixel jitter silently dropped — a *differently* broken
frame, arrived at by a change that looks like it cannot be wrong. Moving the
accessor would also have been wrong for its four post-fx callers and for BLSS'
own `endScene()`/`composite()`, which genuinely do want the display buffer.
`getCurrentFrameBuffer()` therefore keeps its old meaning and is documented as
"the display buffer".

#### Two latent bugs the shared restore uncovered, neither of them BLSS's

Both predate the upscaler and both were live in any project using the affected
feature, with or without it:

- **None of the three restores carried the `InterlacedField` per-field
  `XYOFFSET` bias** (`RendererCoreGS::getFieldYOffset16`). In that display mode
  an env map, a camera feed or a shadow map handed the rest of the frame a raster
  window half a scan line off. One implementation of the restore instead of three
  is what fixed it.
- **The shadow-map restore left `ZBUF` pointing at its own 64×64 silhouette
  buffer.** It had been leaving that register to ps2sdk's `draw_enable_tests`;
  every bracket points `ZBUF` at its *own* depth buffer, so coming back from the
  shadow-map bracket meant every projected-shadow receiver patch was tested
  against the caster's light-space depth and failed `GEQUAL` — shadows drawn,
  then discarded. `emitRasterRestore()` writes `ZBUF` explicitly and **last**.
  That one cost a debugging session.

`XYOFFSET` gets the frame's jitter added inside the bracket: two phases,
`-4/16` and `+4/16` of a low-res pixel, alternating every frame. For a 2×2
upscale those are two of the four output-pixel centres inside one low-res pixel,
so the current frame and the history carry two genuinely distinct output samples
— a quincunx pair, exact rather than approximate. Two phases rather than a
longer Halton sequence because the history is one frame deep; a four-phase
pattern would keep re-visiting positions the reconstruction has already lost.

### 2. Features, without reading the framebuffer back

The PS2 *can* DMA GS local memory back to the EE, and every engine that tries it
pays a full pipeline stall for the privilege. BLSS does not. Every feature comes
from data the EE already holds while it is submitting the frame:

| Feature | Where it comes from |
|---|---|
| `motion` | the tile's screen-space reprojection between last frame's and this frame's view-projection, at the tile's representative depth |
| `depth` | representative 1/w of the geometry in the tile |
| `depthGrad` | max abs `depth` difference against the 4-neighbour tiles — a disocclusion/silhouette proxy |
| `edgeDens` | how much of each submitted bag's **screen bounding box outline** crosses the tile |
| `texDetail` | **texel density** — texels per screen pixel, from the bag's texture dimensions against its screen area. Minification is *the* predictor of texture aliasing, and the engine already holds both numbers |
| `coverage` | fraction of the tile covered by geometry at all (sky and empty tiles want nothing done to them) |

Six, and it was eight. Both deletions were measurements rather than tidying, and
they are the most transferable thing in this section:
[the two channels the network lost](#two-channels-the-network-lost-and-the-measurements-that-took-them).

#### Where a bag's screen box comes from, and why that was the bug

This paragraph used to read "a bag's screen bbox comes from the world bounding
**sphere** the renderer already computes for the dynamic-light pick". That was
true, cheap, and **it fed the network a constant**. For a floor or a terrain mesh
a bounding sphere is grotesque: `wNear = w − radius` collapses to the near-plane
clamp and the sphere's screen box covers the frame, so every tile reads
`depth = 1`, `depthGrad = 1`, `coverage = 1`. A generated game's whole frame was
being described by **two** proxies.

Nothing on the host could see it. The corpus could always describe its own
distribution (`--blss-eval --features`) and the console could describe nothing, so
for eleven commits the network was fitted to one distribution and run on another
with no way to compare them. Three changes fixed the description and one
instrument makes it checkable; the arithmetic for all of them is
[§2 of the math doc](blss-reconstruction.md),
and they are a **twin contract**, so each exists on the engine and in the corpus:

- **an object-space AABB through the MVP**, near-clipped along its twelve edges,
  instead of a sphere (`RendererCoreBlss::addBagBox()` ↔ the corpus' `bagOf()`).
  The sphere path survives only as the fallback for a bag with no package bbox;
- **one proxy per VU1 package** instead of one per bag. `StaPipBagPackagesBBox`
  already holds a box per `maxVertCount/3` vertices, cached, computed for frustum
  classification whether BLSS is on or not — so the granularity was already paid
  for. Capped at 32 per bag, consecutive parts merged above that;
- **a box that straddles the eye and still fills the frame after clipping is
  dropped**, threshold-free, by both producers — its bbox is the frame by
  construction and its `wNear` is the clip constant, not a measurement;
- **and a bag may opt out entirely** (`PipelineInfoBag::blssProxy`), which codegen
  sets for the sky dome, the star field and the sun/moon discs. A dome *cap*
  covering the top band of the frame is a perfectly well-formed box that still
  describes nothing, and only the submitter knows a mesh is a shell.

**Measured on the console**, scratch `fpp` project in PCSX2, software renderer,
448×448, BLSS 2×2, debug view 2 — the same instrument on two builds of the same
fixture (`6a4cbead`):

| | proxies | tiles covered | `depth` min/mean/max | `coverage` | mean passes |
|---|---|---|---|---|---|
| bounding spheres, one per bag | 2 | 196 of 196 | 1 / 1 / 1 | 1 / 1 / 1 | **5.00** |
| boxes, one per package | 41 | 159 of 196 | 0 / .737 / 1 | 0 / .724 / 1 | **1.96** |

The saturated constant is gone, the temporal weight varies 0–0.466 instead of
sitting at one value, the sky is no longer temporally reconstructed, and the frame
pays 1.96 passes against the worst case 5.00 it was paying — finally the same
order as the corpus' own. Re-probed against the corpus, that vector reads **0
channels out of range** against 1 before, and a `band` of 42–84 % against 0.0 %.
Those are *fill* counts read out of the game's log, not timings; nothing here has
been profiled. **The sky-dome opt-out landed one commit later and its console
numbers are not in that table** — it was verified at compile level (engine and ELF
built in Docker) and never booted, because the machine it was written on had no
working compositor.

#### The instrument that found it, and it stays this time

An earlier round added exactly this measurement, read it once and removed it.
Both halves are permanent now, and together they are how the twins get checked
against real content rather than against each other:

| | what it answers |
|---|---|
| engine **debug view 2** → `BLSSGRID` / `BLSSWORST` / `BLSSFEAT` / `BLSSOUT` / `BLSSFILL` in the game's `bin/log.txt` | what the console's own frame looks like: proxy count, the single widest proxy, min/mean/max per channel and per output, and the fill the frame paid |
| `--blss-eval --probe "<BLSSFEAT line>"` | where that vector sits **inside the corpus distribution** — spread, percentile, how much corpus lies within ±0.05, how much of the corpus the frame's band reaches, and a verdict per channel |
| `--blss-eval --cv --drop-feature <name>` | whether a channel earns its keep, in one run, without editing `kFeatures` on both twins |

`BLSSWORST` is the line that pays for itself: it names the widest single proxy,
which is the one that decides whether the grid describes anything, and it
identified the sky dome in one line. The channel names and their order are exactly
`blss::kFeatureNames`, so a `BLSSFEAT` line sits next to a row of
`--blss-eval --features`.

One caveat on reaching it: `logFeatureSpread()` is inside `#ifndef NDEBUG`, so it
is compiled out of a `make release` build (the editor's own game build is not
one). Otherwise it is a combo entry — *Debug view* → "Log the feature spread to
bin/log.txt (no tint)", in the window's *Project settings* tab and in
*Project ▸ Preferences*. That combo used to offer **only 0 and 1** against a
field `project.cpp` clamps to `0..2`, so reaching view 2 meant hand-editing
`"blssDebugView": 2` into the `.tyra`, and a project that already had it
displayed as "Off" — the UI lying about the project's own data, and writing the
widget back reset it. Fixed in `7d3dbf67`; **anything that still tells you to
edit the `.tyra` for this is stale.**

The obvious upgrade to `texDetail` is still to have the editor **bake** real
high-frequency energy per material at build time — it owns the content, so it can
measure how much detail a texture actually carries, which is a feature DLSS cannot
have. That is a follow-up; the texel-density proxy is what ships here.

### 3. The network

A plain MLP, **6 → 12 → 3**, tanh hidden layer, sigmoid outputs — 123 weights and
**108 MACs per tile**, so ~24 200 MACs plus ~3 400 transcendentals for a whole
512×448 frame's 224 tiles. It runs on the EE FPU in the frame's setup phase.
There is no VU1 microcode involved and no micro memory spent: the clip program
family has none left (see the engine skill), and this net is far too small to be
worth a DMA round trip.

> Both figures are recomputed from the topology — `(kFeatures + kOutputs + 1) ×
> kHidden + kOutputs` weights and `(kFeatures + kOutputs) × kHidden` MACs per
> tile, the second over `16×14` tiles — so they follow the input count and it has
> moved twice (8 → 7 → 6, and 147 → 135 → 123 weights). **Neither figure has ever
> been timed**: nothing has profiled the EE cost of the inference pass, in PCSX2
> or on hardware. "Far too small to matter" is arithmetic and a design argument,
> not a measurement.
>
> `blss.net` carries the weights and **nothing else** — no topology, no date, no
> settings — so a file written by a differently shaped net cannot be detected by
> reading it. `kNetVersion` is the guard: it is **3** today, and a v1 or v2 file
> is refused rather than loaded into a net of the wrong shape. Bump it with
> `kFeatures` or `kHidden`, always.

Outputs are the three blend fields:

- **`wA` — point** — how much crisp nearest-neighbour to mix in. Wins on hard
  edges, dithered gradients, and anything that was authored as pixels.
- **`wC` — temporal** — how much of the reprojected previous frame to reuse.
  This is where the anti-aliasing comes from: the two jitter phases averaged are
  a genuine 2× supersample, and the GS has no MSAA to offer instead.
- **`wD` — sharpen** — how hard to run the unsharp mask that recovers detail
  the half-res render lost.

Each output is evaluated per tile, then averaged onto the 17×15 grid corners and
written as vertex alpha.

### 4. The composite

Up to five full-screen passes of a **Gouraud-shaded, UV-textured triangle-strip
grid** (`PRIM.FST = 1`, so UVs are 12.4 and no perspective divide happens),
each sampling the low-res target:

| # | Pass | Blend | Drawn where |
|---|---|---|---|
| 1 | bilinear base | opaque | always |
| 2 | nearest | `(Cs−Cd)·As + Cd` | `wA > 0` |
| 3 | **the other display buffer** (last frame's finished image), per-vertex reprojected UVs | `(Cs−Cd)·As + Cd`, `As = wC/2` | `wC > 0` |
| 4 | bilinear, ×`k` | `Cd + Cs·As` | `wD > 0` |
| 5 | bilinear at +½ texel (a 2×2 box), ×`k` | `Cd − Cs·As` | `wD > 0` |

Passes 4 and 5 are the unsharp mask: `B + k·(B − blur(B))`, split into the two
blend equations the GS actually has.

**Sparsity is the performance knob.** Every pass is emitted per grid *cell*, as
runs of triangle strips, and a cell whose weight rounds to zero is not drawn at
all — so the network's own confidence decides how much fill the frame costs. The
worst case is 5.00 full-screen passes and plain bilinear is 1.00; the trained net
measures **1.80** on held-out content (`--blss-eval`'s `passes` column, sd 0.30
over 39 fold-runs — [numbers below](#the-out-of-distribution-number-and-how-to-get-one-that-means-something)),
and **1.96** in the one console frame that has been instrumented. That is a *fill*
count, not a millisecond: **nothing here has ever been timed.**

That is a knob only because the *objective* charges for it. Nothing did until
`kFillWeight`, and while nothing did the network asked for every kernel
everywhere and the composite degenerated to five full-screen passes — see
[The oscillation](#the-oscillation), which is the same mistake one level up.

### 5. VRAM

At 512×448 output, 256×224 render:

| Region | Baseline words | BLSS words |
|---|---|---|
| Display buffers × 2 (512×448, 32bpp) | 458 752 | 458 752 |
| Z buffer | 229 376 (512×448) | 57 344 (256×224) |
| Low-res colour target | — | 57 344 |
| History buffer | — | 0 — the other display buffer |
| **Total** | **688 128** | **573 440** |

**BLSS leaves more texture memory than not using it.** One buffer is added and
one shrinks: the low-res colour target costs 57 344 words (224 KB) of the
~1.08 MB texture heap ([GS VRAM residency](gs-vram.md)), and the z buffer —
which follows the **raster** now, not the display buffer — hands back 172 032
words (672 KB) at this output size. Net **114 688 words (448 KB) returned**. The
history is still free: it is the other display buffer.

The z buffer can shrink because with BLSS on nothing ever renders 3D at display
resolution. What makes that *safe* is one invariant, and it is the thing to keep
if you edit any of this: **`zBuffer.mask` is 0 only inside the low-res bracket.**
Every `draw_enable_tests` / `draw_setup_environment` in the engine reads that one
field, so the 2D/HUD/post-fx half of the frame — which draws full-screen sprites
at `z = 0xFFFFFFFF` and would otherwise stamp 448 rows at a 512 stride — cannot
reach past the smaller allocation. The ordering problem (z is allocated third in
`gs.init()`, long before a generated game's `init()` calls `blss.configure()`) is
solved by laying the permanent VRAM region out again from `configure()`, through
`RendererCore::rebuildPermanentBuffers()`.

**Measured, and this is the one console number on this page that is current.**
PCSX2 software renderer, scratch `fpp` project, `Pal576i` (512×512), from the
game's own `VRAMSTAT` line at frame 240:

| | free VRAM | evictions | largest free block |
|---|---|---|---|
| BLSS **off** | 0.227 MB | not recorded | not recorded |
| BLSS on, **before** the z shrink | 0.234 MB | 1 | 232 KB |
| BLSS on, **after** | **0.727 MB** | **0** | **744 KB** |

The z buffer went 262 144 → 65 536 words in this mode, i.e. **768 KB back** (672
KB is the 512×448 figure in the table above; 768 KB is this one, and the two are
not interchangeable). Against a 256×256 low-res target costing 256 KB that is
512 KB net — which is exactly the 0.227 → 0.727 MB the log shows, so the
arithmetic and the measurement agree.

Two things that table is easy to misread. The `0.234` in the middle row is *not*
evidence that BLSS was nearly free before: it had to evict a texture to fit, and
the eviction is what put the space back. That eviction is gone. And the largest
free *block* is the number that matters for whether a big texture can be placed
at all — a 256×256 32-bit texture wants 264 KB with its padding
([GS VRAM residency](gs-vram.md)), which does not fit in 232 KB and does fit in
744 KB.

The page previously claimed this feature was VRAM-positive, then had to retract
it because the saving was still hypothetical. It is measured now, in the display
mode named above, and nowhere else: **no other display mode has been booted since
the change.**

#### At `1×2` the VRAM saving is exactly ZERO, and nothing said so

Every figure above is a `2×2` figure. At `1×2` the raster is 512×224 of a 512×448
output, so the z buffer shrinks by 114 688 words — and the low-res colour target
is 512×224 at 32bpp, which is *the same 114 688 words*. Both buffers are
`width × height` at 32 bits per pixel, so at half the raster area the saving and
the cost are the same number **by construction**, at every output size:

| output | scale | z returned | low-res target | **net** |
|---|---|---|---|---|
| 512×448 | 2×2 | 672 KB | 224 KB | **+448 KB** |
| 512×448 | **1×2** | 448 KB | 448 KB | **0** |
| 512×512 (Pal576i) | 2×2 | 768 KB | 256 KB | **+512 KB** |
| 512×512 (Pal576i) | **1×2** | 512 KB | 512 KB | **0** |

So `1×2` is a choice about *fill and picture*, never about memory, and the
settings panel now says that on the line under the Render scale combo — computed
for the display mode the project actually boots in
(`App::blssVramLine`, a host twin of `RendererCoreGSVRam::getSize`). The tooltip
it replaced quoted "2×2 hands 672 KB back and 1×2 hands back 448 KB", which is
the z-buffer column alone with the target's cost left out — true of neither row
of that table. Derived, not measured: the arithmetic is the engine's own
(`renderer_core_gs.cpp` allocates z at `getRasterWidthUI/HeightUI`;
`renderer_core_blss.cpp` allocates the target at `lowBufW × lowH`), and no `1×2`
project has been booted to read a `VRAMSTAT` line back.

## Training

The network is trained **on the host, headless, by the editor itself** — no
Python, no external framework, no GPU:

```bash
tyrax-editor --blss-train            # train on the built-in corpus, write blss.net
tyrax-editor --blss-eval             # the table below: PSNR, flicker, occupancy
tyrax-editor --blss-eval <project>   # no net needed - see below
```

**`--blss-eval` needs no network, and that is the point of the third line.** The
row that answers *should this project have the upscaler on at all* is the
**oracle** row — the best any per-tile weighting can reach under the exact GS
composite — and nothing in it involves a trained net. Until this was fixed the
tool loaded `blss.net` first and bailed with `cannot open blss.net`, so the first
step this page and the settings panel both prescribe ("evaluate your project
BEFORE turning this on") was impossible to perform on a fresh project. Now:

- **no `-i`, and no `blss.net` where the tool is looking** → the table is printed
  without the `half-res + BLSS (trained)` row, the verdict is printed, and the
  exit code is 0;
- **no `-i`, but a `blss.net` is there** → it is used, exactly as before;
- **an explicit `-i <file>` that cannot be opened** → still an error. You asked
  for that net.

Both verbs take `--frames N`, `--assets <dir>`, `--seed N`, `--sharpen K`,
`--scale-1x2`, `--weight-decay W`, `--standardise`, `--threads N`
([below](#--threads-n-and-the-determinism-that-pays-for-it)), and the two weights
of the oracle's objective — `--flicker-weight` and `--fill-weight`. **Sweep those two as
a pair**, because they trade against each other and against sharpness; the
shipped defaults (`0` and `16`) are the result of such a sweep, recorded with its
numbers in `src/blss.hpp`. Changing either changes the *labels*, so a change is
only meaningful after a re-train:

```bash
tyrax-editor --blss-train --frames 84 --fill-weight 4 -o try.net
tyrax-editor --blss-eval  --frames 84 --fill-weight 4 -i try.net
```

`--blss-train` additionally takes `--all-shots` (fit every shot — the net you
would actually ship, after which `--blss-eval`'s held-out columns mean nothing);
`--blss-eval` additionally takes `--cv` / `--cv-seeds N` / `--cv-folds N`
(cross-validation, [below](#measured)), `--features` (what the six input channels
look like over the corpus, and how each correlates with the oracle) with its
`--probe` companion, and `--drop-feature <name>` (hold channels at zero — the
instrument that retired two of them).

### What `--blss-eval` prints for a machine to read

Two lines exist so that a caller — the window's Evaluate tab, a CI job, a script
— never has to guess at a table:

```
[blss] verdict headroom=+0.017 passes=1.00 bilinear=39.888 oracle=39.905 native=43.480
[blss] fold 7 of 12
```

The **verdict line is printed by every `--blss-eval`**, net or no net, on its own
line, after the human-readable verdict block. Every field is a quantity that
exists without a network, which is what makes it the right summary for a project
that has not trained one yet: `headroom` is the oracle's margin over plain
bilinear (the scene's **ceiling** — under +0.10 dB means there is nothing to
reconstruct), `passes` is what the oracle pays for it (1.00 *is* plain bilinear,
5.00 is every kernel everywhere), and the three PSNRs are frame-weighted over
both splits, the way `blssui::summarise()` computes them.

The **fold line is printed by `--blss-eval --cv`** as each fold finishes. The
fold loop turns the trainer's own verbosity off and prints nothing else until the
table at the very end, so a progress bar driven off this tool's output used to go
blank for minutes; the count is completion order, not fold index, because folds
run in parallel and a bar wants a fraction rather than an identity.

### Where the minutes go, and what was measured to move them

`--blss-train` has printed a three-phase timing line since `7d3dbf67`;
`--blss-eval` and `--blss-eval --cv` now print the same kind of line, because a
speedup nobody can read is a speedup nobody can check:

```
blss: timing - corpus 2.8 s, eval 16.1 s (18.9 s total)              # --blss-eval
blss: timing - corpus 2.6 s, oracle 7.4 s, folds 40.8 s (50.9 s total)   # --cv
```

**Measured** on `examples/showcase` (156 frames over 12 shots, 512×448 from
256×224, shipped defaults), `--threads 8`, one machine, the two binaries run
alternately — **minimum of the repeats**, because the box was not quiet and
contention can only ever inflate a wall clock:

| run | before | after | |
|---|---|---|---|
| `--blss-eval <project> -i net` | 89.8 s | **19.5 s** | **4.6×** |
| `--blss-eval <project> --cv --cv-seeds 1` | 48.3 s | **39.3 s** | **1.23×** |

(The spread over four repeats was 89.8–101.4 s / 19.5–20.2 s and 48.3–68.2 s /
39.3–51.0 s, which is why the table is minima rather than means: the *same*
binary measured minutes apart differed by 40 % on the `--cv` row. Only the
minimum is a statement about the code; everything above it is a statement about
what else the box was doing.)

Three changes, and **all three are bit-exact**: the eval and fold tables come out
character-identical to the runs above them, and `--threads 1` against
`--threads 8` still writes a byte-identical `blss.net` — indeed the same md5 the
pre-change binary wrote.

1. **The per-method evaluation is parallel over shot runs.** `evalRecurrent` was
   a serial frame loop and a plain `--blss-eval` was ~80 % one serial oracle. The
   temporal chain resets wherever the shot id changes — that is the only place
   `history` becomes null and the only place a flicker pair is dropped — so a
   maximal run of consecutive frames of one shot is an independent unit. Workers
   produce **per-frame** numbers and the six running sums are folded in
   afterwards *in corpus order*, so every accumulator sees the same addends in
   the same sequence and the floating-point answer cannot move. That is nearly
   all of the 4.6×, and it puts `--blss-eval` under the same "wall clock and
   nothing else" contract `--threads` already carried. **The shot count is the
   ceiling on it**: the held-out split of a 12-shot corpus is four runs, so four
   workers, whatever `--threads` says. More parallelism than that means splitting
   a shot, which means breaking the temporal chain, which is the one thing this
   must not do.
2. **`--cv` computes the two bilinear rows once per corpus instead of once per
   fold.** With an all-zero weight field `alphaBytes()` yields `aA = aC = aD = 0`
   and `blend()` hands back the base tap untouched, so a bilinear composite never
   reads the history: its PSNR is a per-frame constant, independent of the fold
   and of which side of the split the frame is on. Every fold used to
   re-composite its eleven *training* shots as well as its own to rediscover that
   — *n* folds × *n* shots against *n* distinct answers. It is the whole of the
   `--cv` row above, and only that row: **19 %** of the run, since neither of the
   other two changes can fire inside `--cv` (the fold loop is already parallel,
   so `evalRecurrent` deliberately runs serial in there, and the default sharpen
   is 0.5).
3. **The oracle does not sweep `wD` when `--sharpen` is 0.** `alphaScales()`
   returns a third scale of `k · 128`, so at `k = 0` every one of the nine
   candidates quantises to `aD = 0`: they score identically, the fill term
   charges none of them, and a third of the oracle's `errRegion` calls were
   spent proving it. Measured on its own, at `--threads 1` so change 1 is inert:
   26 frames, `--sharpen 0`, **16.1 s → 14.0 s** wall, of which ~2.6 s is the
   corpus render in both — so the evaluation itself, ~13.5 s → ~11.4 s, **about
   16 %**. And **nothing at all** at the default `--sharpen 0.5`, which is why it
   does not show in the table above.

What did **not** move: `--blss-train`. It never called the evaluation loop, and
the default sharpen is 0.5, so its phase table below is still a measurement of
the code that is here — checked by the net's md5, which is unchanged.

### `--threads N`, and the determinism that pays for it

`--threads N` bounds the parallel phases — the corpus render, the oracle
labelling, and (since the evaluation was threaded, above) `--blss-eval`'s
per-method loop. `0` (the default) means every core, and the count is clamped to **32**
at parse time, because a corpus worker owns ~30 MB of raster scratch and because
printing "on 999 thread(s)" would have been a lie. The trainer itself is
sequential and ignores it.

**It is a wall-clock knob and nothing else, and that is a contract rather than an
aspiration.** Both loops hand item *i* to a fixed worker and let it touch only
item *i*, so at any thread count the same `--seed` writes the **same `blss.net`,
byte for byte**. Measured on this branch — 156 frames, 400 epochs, `--all-shots`,
`examples/procedural` — md5 of `blss.net`:

| | md5 of `blss.net` |
|---|---|
| `--threads 1` / `3` / `6` / auto | `6b2fba90d0f059f055134a55df478c8e` |
| **the binary from before the corpus was threaded** (`7d3dbf67^`), two runs | `6b2fba90d0f059f055134a55df478c8e` |

**The last row is the load-bearing one.** The parallel corpus does not merely
agree with itself; it agrees with the serial loop it replaced, so every fold
table on this page is still a measurement of the code that is here now. That
required removing the loop's one order dependency: `prevLow` used to be carried
from iteration to iteration, and it is now re-rendered from its own camera and
its own jitter phase — the same image by construction, since `renderScene` is a
pure function of geometry, materials, camera, size and raster offset and clears
both targets, and 1.5 % more work because a low-res render is 1/64 of the
supersampled truth. What it buys is that frame *i* is computed from *i* alone.

`--threads 1` is how that gets checked rather than asserted. Run it against a
run on every core and diff the two nets; a difference is a real bug and not a
rounding artefact.

**Measured**, same seed and protocol, 6 cores, `examples/procedural`, 156 frames,
400 epochs, `--all-shots` — the tool's own `blss: timing` line:

| phase | `--threads 1` | `--threads 3` | `--threads 6` | auto | speedup, 1 → auto |
|---|---|---|---|---|---|
| corpus render | 7.5 s | 3.0 s | 1.7 s | 1.9 s | 3.9× |
| oracle labels | 54.6 s | 19.9 s | 10.5 s | 10.4 s | 5.2× |
| **fit** (Adam SGD) | **6.2 s** | **6.1 s** | **6.3 s** | **6.2 s** | **1.0×** |
| total | 68.4 s | 29.0 s | 18.6 s | 18.5 s | 3.8× |

End to end against the **previous commit**, both at auto threads, the gain is
much smaller: two runs of the `7d3dbf67^` binary took **24.4 s and 23.6 s**
against **18.5 s and 18.5 s** here, about **1.3×**. The oracle had been parallel
per frame since `1b9c7a74`, so only the corpus render was still serial — 7.2–7.7 s
of a ~24 s run — and 1.3× is the whole of what parallelising a ~31 % phase can be
worth.

> **The bottleneck is now the fit.** At auto threads the oracle is 10.4 s (56 %),
> the corpus 1.9 s (10 %) and the fit **6.2 s — 34 %**, which is sequential SGD:
> each Adam step reads the weights the previous one wrote, so it is the one phase
> `--threads` cannot touch. It is also not worth a GPU. Six seconds is not the
> problem, and an 18-second cycle is a workflow rather than a batch job.

> **That table is `examples/procedural` on a 6-core machine and it predates the
> evaluation work above**, which is fine, because none of that work touches
> `--blss-train`: the trainer never calls the evaluation loop, and the oracle's
> `--sharpen 0` shortcut cannot fire at the default 0.5. The check is the md5 —
> the net this tree writes is byte-identical to the one the pre-change binary
> wrote, at 1, 8 and auto threads. Do not re-measure this table on a different
> machine to "refresh" it: the row that carries the argument is the comparison
> against `7d3dbf67^`, and that comparison only exists at these conditions.

Two things the tool now prints that are worth reading. **All three verbs end with
a phase line** — `blss: timing - corpus X, oracle Y, fit Z (T total)` for
`--blss-train`, `corpus X, eval Y` for `--blss-eval`, `corpus X, oracle Y, folds
Z` for `--cv` — because "the oracle is nearly all of it" stopped being true and
the next person to optimise this should see which phase is actually slow. And the
corpus header says which it is doing: `blss: rendering 156 corpus frames at
512x448 on 6 thread(s)`, or `… on every core` when `--threads` was not given.

**The per-shot lines report cpu ms, not wall ms.** Wall clock stopped being a
per-shot quantity the moment the shots overlapped, so
`shot 2 main orbit orbit 26 frame(s) 1219 ms cpu (47 ms/frame)` is the summed CPU
time of that shot's frames. Adding the six up gives roughly the one-thread corpus
time, never the wall clock of a threaded run.

`--blss-train` runs a self-contained software rasteriser (`src/blsscorpus.cpp`)
over a **procedural corpus** of thirteen shots — the cases that actually alias on
a PS2, each with its own camera move:

| Shot | What aliases | Camera |
|---|---|---|
| `floor-horizon` | checkerboard running to the horizon, sampled nearest | dolly in |
| `boxes-sphere` | hard box silhouettes plus a faceted low-poly ball | orbit |
| `poles` | posts about one pixel wide — sub-pixel geometry | pan |
| `foliage` | alpha-cutout leaf quads | static (jitter only) |
| `grazing-wall` | high-frequency textures at a grazing angle | dolly along |
| `flat` | nothing: a large untextured area the net must leave alone | slow pan |
| `whip` | all of the above, swept ~150° | whip pan (reprojection is hopeless) |
| `corridor` | interior walls, ceiling and boxes, all within a few units | dolly down |
| `strafe-field` | near geometry sliding across far — real parallax, so disocclusion | lateral dolly |
| `pitch-sky` | coverage sweeping from 1 to nearly 0 over the shot | pitch up through the horizon |
| `distant-plain` | everything far away: extreme minification, near-perfect history | slow dolly |
| `sphere-field` | curved silhouettes at several distances at once | wide orbit |
| `foliage-walk` | the same cutouts as `foliage`, but *moving* | dolly through |

**Shots 0–6 are the original seven and they render bit-identically** with or
without the six that follow — the new ones are appended, never inserted, and draw
their randomness from fresh `mix32` purposes. That is what makes a before/after
fold table a comparison rather than two different experiments.

The corpus describes each frame to the network through **one proxy per VU1
package**, the same cut `StaPipCore` makes on the console (1 217 proxies over the
bestiary). `--no-package-split` reverts it to one per object, which is how the
fold tables measured before that change can still be reproduced; it is not a
shipping configuration.

### Training on your own project

```bash
tyrax-editor --blss-train <projectDir> --all-shots -o <projectDir>/blss.net
tyrax-editor --blss-eval  <projectDir> -i <projectDir>/blss.net
```

Both entry points take an optional **positional project directory**, and with one
the corpus is the project's own scenes — real geometry, real materials, real
terrain, walked / panned / orbited / whipped / pitched / strafed by six camera
moves derived from the scene's bounds and its player start, plus any authored
Cutscene Director camera track. `src/blssscene.cpp` walks a project into
world-space triangles through the same three sources the GI bake uses
(primitives, static `.obj`, terrain chunks); animated `.glb` is skipped
*because* it goes down the dynamic pipeline, which does not feed BLSS at all. A
project that will not load, or loads with nothing to draw, falls back to the
bestiary and says so.

**This is not a refinement, it is the difference between helping and hurting.**
Measured on `examples/procedural` (39 meshes, 15 098 triangles, no textures at
all), 72 frames over six shots, both nets fitted with `--all-shots`:

| over all 72 project frames | margin over bilinear | passes |
|---|---|---|
| bestiary-trained net | **−0.40 dB** | 1.72 |
| project-trained net | **+0.06 dB** | 1.19 |
| oracle (upper bound) | +0.77 dB | 1.20 |

A net trained on the built-in corpus is **worse than doing nothing** on that
project, by four tenths of a decibel, while paying half a pass more for the
privilege. The mechanism is in `--features` and it is not subtle: `texDetail` is
identically zero over all 16 128 project tiles (the scene is untextured) and it is
the bestiary's channel most correlated with the temporal weight (r = +0.251);
`edgeDens`, the next one (+0.186), saturates at 1.0 in 63 % of project tiles
against 29 % of bestiary tiles. The bestiary net's temporal gate is therefore
driven by inputs that are out of range, and it asks for 62–93 % temporal occupancy
where the oracle asks for 7–30 %.

Two rules follow, and they pull in opposite directions on purpose:

- **Fit the project with `--all-shots` and ship that net.** The console runs the
  frames it was fitted on, so in-distribution is the right question for the net
  you ship.
- **Do not quote a project corpus' held-out decibel.** Leave-one-shot-out over six
  camera moves of one scene reads **−0.17 dB, 9 of 18 fold-runs below bilinear**.
  Six moves over one scene do not generalise to a seventh — the same wall the
  bestiary hit at five shots (+0.10 dB) before it grew to thirteen.

**And not every project has anything to win.** On `examples/showcase` — 156
frames over two scenes × six moves — the *oracle* itself scores **+0.02 dB** over
bilinear at **1.00 passes**, frame-weighted over both splits the way the window's
verdict computes it (+0.04 held-out, +0.01 over the training shots): soft ground
texture, low-poly props, nothing that aliases. `--blss-eval <projectDir>` is how
you find that out before shipping BLSS on it, and the window says it in one line.

> **The window does this too, and it is the default.** *Tools ▸ Neural Upscaler
> (BLSS)* has a **corpus switch in its header** — one switch for all five verbs —
> and it starts on *This project's own scenes*, with *Fit every shot* on. See
> [the window](#the-window).

The corpus was the binding constraint, not the trainer. Measured on the original
seven when they were all there was (`1b9c7a74`, and **not re-run since** — the
old corpus no longer exists to re-run it on): holding out **one** shot and
training on six scored +0.31 dB, while holding out **two** and training on five
scored +0.10 dB at four times the spread. Same method, smaller training set,
noisier estimate. Six more shots is what fixed that, and it is the change that
moved the headline number.

Materials are real PNGs from `examples/*/res/{materials,models}` where the tree
has them and procedural checkers/noise/foliage otherwise, so training works in a
clean checkout. Frames are split evenly over the shots and `CorpusFrame::shot`
records which is which, because the eval split has to hold out whole shots —
neighbouring frames of one camera move are near-duplicates. For every frame it
produces:

- the **ground truth** at output resolution,
- the **half-res jittered render** and its predecessor, i.e. exactly what the
  console would have in its two targets,
- the **features**, from the same code the runtime uses,
- and the **oracle weights**: per tile, the `(wA, wC, wD)` that minimise the
  objective below, found by coordinate descent on the *exact* composite formula
  the GS will execute — including its `≫ 7` truncation and 8-bit clamps.

**The objective has three terms, and which terms exist is the single most
load-bearing decision in this feature** — a term that is not in it is a term the
network is structurally unable to learn, which is a mistake this feature shipped
four times (see [The oscillation](#the-oscillation)):

| Term | Weight | What it buys |
|---|---|---|
| MSE against the supersampled truth | 1 | sharpness / accuracy |
| MSE against the reprojected history | `--flicker-weight`, **default 0** | temporal stability — **measured to be a bad trade, see below** |
| the fill the candidate would make the GS draw | `--fill-weight`, **default 16** | sparsity, and stability as a side effect |

The fill term is charged as a **step on the quantised alpha byte**, not as a
smooth function of the weight, because the byte is what the engine's skip test
reads: a weight that rounds to alpha 1 costs a whole pass and buys nothing, and a
smooth penalty would park the oracle exactly there. Sharpen counts twice (passes
4 and 5 are always drawn together) and the bilinear base is free (always drawn).

Then it fits the MLP to the oracle weights (Adam, MSE, weight decay `1e-4`,
per-tile loss weighted by how much the choice actually changes that tile — tiles
where every kernel is equally good do not get a vote).

Held-out **shots** are what a plain `--blss-eval` reports: `shot % 3 == 1`, so 4
of the 13, striding the bestiary rather than taking the tail. **Do not quote that
split's decibel.** It is one draw — see
[Measured](#measured), where holding out each shot in turn says something
different and more useful.

`--blss-emit` bakes the trained weights into the C++ the game compiles — the
generated project carries them as **`inc/blss_net.gen.hpp`** (codegen writes it;
`--blss-emit -o <file>` writes the same body by hand, and with no `-o` prints it
to stdout). The network is 123 floats, so it is a header, not an asset.

> **The emitter used to produce a header that did not compile, and the path it
> broke was the documented one.** `%.9g` renders `0.0f` as `"0"`, so a weight of
> exactly zero was emitted as `0F` — an integer with a user-defined-literal
> suffix that does not exist, not a float. `Net::randomize()` zeroes *every*
> bias, and codegen emits a default-constructed net when a project has BLSS on
> and no `blss.net`, so **every BLSS project without a trained network failed to
> compile** with a wall of `unable to find numeric literal operator 'operator""F'`
> — while the code comment next to it promised that a missing net "is never a
> build failure". It had never once been executed. Fixed in `1b9c7a74`:
> `floatLiteral()` adds the decimal point, non-finite weights are written as zero
> and counted into a banner rather than spelled as a literal, and
> `selfTestEmitter()` checks every literal it produces on **every** `--blss-train`
> and `--blss-emit`. Verified here by generating a BLSS project with no
> `blss.net` and compiling the resulting header (host `g++ -std=c++20`; the
> literal syntax is what was wrong, and it is toolchain-independent).

## Using it

**Everything below is reachable from *Tools ▸ Neural Upscaler (BLSS)*** — training,
evaluation, cross-validation, the comparison renders, the channel report and the
emit step, none of which used to exist outside a terminal. The five project
settings live there too, on a *Project settings* tab, and are mirrored in
*Project ▸ Preferences ▸ Neural upscaler (BLSS)*. Off by default.

**Start with the one button above the tabs — `Will this scene benefit?`** It
needs no trained network and it is the only step that can tell you not to bother.
Everything else in the window is worth doing only after it says there is
headroom.

### The window

**It runs the editor's own binary and parses its output, deliberately.** Every
driver behind `--blss-eval` is file-static in `blss.cpp`, so a window that called
"the public API" would have had to re-implement them — and a re-implemented driver
is a second answer to *what does `--blss-eval` measure*, from a feature that has
already published five numbers measured on the wrong thing. So the window spawns
`tyrax-editor --blss-<verb>` on a worker thread and turns the printed tables back
into numbers, and **the tool's raw output sits on screen under every table**. That
is what makes a parsed number falsifiable instead of trusted: if a table looks
wrong, the text that produced it is one glance away. A parser that finds nothing
says so; it never invents a row.

#### The header: one button that answers the only question most people have

Above the tabs, under the corpus switch, is **`Will this scene benefit?`** — one
click, no network needed, and it is worth more than every tab under it.

It runs `--blss-eval` **with no `-i`**, which needs no `blss.net` at all
([net-free evaluation](#what-blss-eval-prints-for-a-machine-to-read)), and states
the answer in plain language:

> *THIS SCENE WILL NOT BENEFIT. Leave the upscaler off.* The oracle — the best any
> per-tile weighting can do — scores +0.02 dB over plain bilinear here.

or

> *Headroom: +0.95 dB available at 1.22 passes.*  [ Train a network for this scene ]

That button is there because the window used to tell people to *"run the Evaluate
tab on your project BEFORE turning this on"* and **that was impossible**: Evaluate
loaded `blss.net` first and bailed, and only Train could produce one — twenty
minutes of work before the first fact, on a scene that might have had nothing to
win. The verdict block is the same `drawBlssVerdict` the Evaluate tab draws, in a
compact form, so there is one wording and one arithmetic
(`blssui::summarise` / `blssui::parseVerdictLine`, both host-only and both
harness-checked against captured runs).

The header also states **what the network is**, not how many bytes it is:
`blss.net - 123 weights, 6->12->3, 2026-08-08 15:06`, with the byte count in the
tooltip. And it compares the provenance sidecar against the project's *current*
settings, because a `blss.net` stores none of them, so a mismatch is otherwise
silent and simply worse:

> *This net does not match the project's current settings — RETRAIN:*
> *— `--sharpen 0.50`, but the project now says 0.80*

It fires for the sharpen strength, the render scale, and a command line with no
project directory in it (i.e. a bestiary-trained net about to ship).

#### Every long-running button says what it costs

Train, Evaluate and — above all — Cross-validate are minutes to tens of minutes,
and until now the only one that said anything printed a *count* of fold-runs,
which is not a cost. Each now carries **"about N minutes on this machine (N
cores)"**, with a tooltip splitting it into corpus / oracle / fit.

`blssui::estimate()` is the model — a pure function of (verb, frames, epochs,
cores, seeds, folds, shots), next to the parsers for the same reason they are
there. It is calibrated against real runs of this tree, on one machine with 24
hardware threads, `examples/procedural`:

| run | model | measured |
|---|---|---|
| `--blss-train` 36 f / 100 e, `--threads 1` | 17.1 s | 14.7 s |
| `--blss-train` 156 f / 400 e, every core | 16.5 s | 14.5 s |
| `--blss-eval` 156 f, `--threads 1` | 152.3 s | 151.4 s |
| `--blss-eval` 156 f, `--threads 6` | 52.5 s | 52.2 s |
| `--blss-eval` 156 f, every core | 43.8 s | 42.4 s |
| `--blss-eval` 156 f, net-free, every core | 43.8 s | 43.1 s |
| `--blss-eval --cv` 36 f / 100 e, 2 folds | 5.1 s | 6.0 s |
| `--blss-eval --cv` 36 f / 100 e, 6 folds | 7.9 s | 9.0 s |
| `--blss-eval --cv` 156 f / 400 e, 6 folds | 52.7 s | 50.0 s |

Three facts fall out of those runs and each is one constant in the model. The
**fit is sequential** and at the shipped defaults is the largest phase of a
training run. The **corpus render saturates at about 4× however many cores it
gets** — a worker owns ~30 MB of raster scratch, so it is memory-bandwidth bound
(3.5× measured at 24 threads here, 3.95× at 6 cores in
[the threading table](#threads-n-and-the-determinism-that-pays-for-it) — the same
ceiling from both ends) — while the oracle's **labelling** pass scales nearly
linearly (12.9× at 24 threads).

And **`--blss-eval` is not `--blss-train` minus the fit**: it is ten times
slower, and it scales differently again. Evaluation closes the temporal loop, so
its unit of parallelism is a **shot run**, not a frame — a corpus of six camera
moves has about six independent chains however many cores are watching. Measured
here at 156 frames: **138.0 s at one thread, 46.6 at six, 39.1 at twenty-four**,
a ceiling around 3.6×. The model takes that six-move ceiling and does not try to
predict a thirteen-shot bestiary going further, which errs toward over-quoting
the wait — the right direction to be wrong in.

The window says *"about"* and `humanDuration()` rounds, because the model is one
machine and one project: a scene with ten times the triangles renders its corpus
proportionally slower. Treat a factor of two as within tolerance.

#### The corpus is a switch, in the header, and it defaults to the project

The first control in the window is not on a tab. **Which corpus produced this
table must have a single answer, visible above every table**, so one switch in
the header serves all five verbs — a per-tab copy is exactly how a net trained on
the project gets evaluated against the bestiary without anyone noticing. It
defaults to **this project's own scenes**, and every table carries a one-line
reminder of which corpus it came from so the question never needs a scroll.

`blssCommonArgs()` implements it by putting the project directory in as the
positional argument every verb already accepts. **Absolute**, for two reasons:
the job runs with cwd set to the project directory, so a relative `Project::dir`
would resolve against itself; and that string is what the provenance sidecar
records next to `blss.net`, where "trained on ." is not an answer to "trained on
what".

`--all-shots` — *Fit every shot* — **defaults on**, and its tooltip says why: the
console runs the frames the net was fitted on, so withholding a third of the
corpus buys an honest held-out column nobody can act on and costs the shipped net
real quality. Turning it off is still possible and now says in amber that what
comes out is a *measurement* net, not the one to ship.

The bestiary is presented as what it is — the fallback for a project with nothing
drawable, and the corpus the 13-shot fold tables below were measured on — with
the **−0.40 dB** in the radio button's own subtitle.

| Tab | What it is for |
|---|---|
| **Train** | **Frames and Epochs**, and nothing else that is not a decision. The other six controls are behind *Advanced — measured, and set where the measurement said* (below) |
| **Evaluate** | a plain-language **verdict**, then **three thumbnails** (bilinear / BLSS / the amplified difference), then the PSNR / flicker / occupancy table it all came from |
| **Cross-validate** | the fold table with its per-seed columns, its spread, its in-distribution control and the deadzone sweep — plus, on a project corpus, the caveat that says what a held-out decibel means there (below) |
| **Compare** | the dumped PNGs with an **A/B wipe** and a **difference view**, defaulting to bilinear against BLSS, because "is it actually better" is a question about pixels. The weight field is one pixel per tile and is magnified with NEAREST |
| **Inputs** | the per-channel distribution with saturation coloured — the diagnostic that explains the shot the network loses on |
| **Project settings** | the five settings below, the live VRAM line, and the build-interlock warning |

#### The Train tab asks for two numbers, not ten

A user needs **Frames** and **Epochs**. The other six — the seed, the weight
decay, the fill weight, the flicker weight, `--standardise` and *Materials from*
— are behind a collapsing header, with **every tooltip preserved verbatim**,
because they are research knobs: all six are measured, all six are already set
where the measurement said, and **two of them have tooltips that say in so many
words that moving them makes the feature worse** (`--standardise` cross-validates
at +0.24 dB against +0.40; the flicker weight costs 0.02 dB and moves the flicker
column not at all). That is an argument for folding them away, not for deleting
them — a knob measured and set off is not the same as a knob that was never there.

*Restore defaults* now covers **every** field the window owns, including the four
frame counts, the deadzone and the cross-validation settings; it used to reset
eight of them and leave the rest wherever they had been dragged, which produced a
configuration that was not the documented one, silently. It deliberately leaves
the **corpus switch** alone — which project you are measuring is a statement
about your game, not a training default. And because there are
four independent **Frames** fields — one per verb — each tab says so when they
have drifted apart: a table measured over a different number of frames than the
net was fitted to is not a comparison.

#### Evaluate answers the question in words, before the table

`--blss-eval` has always *contained* the answer to "will this scene benefit at
all", and has always buried it in the sixth row of a table: **the oracle row is
the scene's own ceiling**, because no network can beat the best per-tile
weighting under the exact GS composite. The tab states a verdict above the
table, in one of three forms:

| when | what it says |
|---|---|
| the oracle margin is under **0.10 dB** | **THIS SCENE WILL NOT BENEFIT** — leave the upscaler off. There is nothing to reconstruct, and no amount of training moves that |
| the net's own margin is **below zero** on a scene that *does* have room | **THE NETWORK YOU HAVE IS WORSE THAN NOT USING IT** — this is the net, not the content. On the bestiary corpus it adds that this is exactly what a bestiary-trained net does on a real project |
| otherwise | the margin, the pass count, the ceiling, and **what fraction of the ceiling** the network captured |

The arithmetic is `blssui::summarise()`, **not a calculation inside an ImGui draw
call** — same reason the parsers live there: a pure function of the parsed table
is checkable from the host-only harness, and a draw call is not. It is
frame-weighted over both splits, because the two splits partition one corpus and
neither half alone is an answer about the scene.

**The tool prints the same verdict itself**, in the same three forms and the same
words, plus the `[blss] verdict …` line
([above](#what---blss-eval-prints-for-a-machine-to-read)) — so a terminal, a CI
log and the tab cannot disagree about what the table said. There is a fourth
branch, **net-free**: a ceiling with no network to compare against it, which
states the headroom and offers *Train the network*. That is what the header's
`Will this scene benefit?` button reaches, and it is the same function drawing
it — `drawBlssVerdict(summary, compact)`. The window prefers the machine-readable
`[blss] verdict` line for the ceiling when it is present and falls back to
re-deriving it from the parsed table when it is not, so an older binary still
answers.

#### The picture goes under the verdict, and the difference view is the point

*"Is it actually better"* is a question about pixels — the Compare tab says so in
its own first line, and the answer used to be on the fourth tab behind two
tables. Directly under the verdict there is now a three-thumbnail strip —
**bilinear | BLSS | the difference, amplified 8×** — click-through to the Compare
tab (and two named buttons beside it, because an `ImageButton` has no label for
ImGui to report, so a cursor is the only thing that can reach one).

The **difference view** is the addition that earns its place. Two reconstructions
of one frame that differ by a few tenths of a decibel are indistinguishable side
by side and even under the wipe; at 8× the disagreement is a picture of *where*
the network spent its composite passes, and a black frame is the honest statement
that it changed nothing whatever the decibel column says. It is |A−B| per
channel on the CPU from the loaded PNGs, rebuilt only when the pair or the
amplification changes, and the caption reports the **raw** mean and peak, not the
amplified ones — the amplification is a magnifying glass and a number about it
would be a number about the magnifying glass. A and B of different sizes (the
weight field is 16×14) say there is nothing honest to subtract rather than
scaling one to the other.

#### Cross-validate says what it is measuring on a project

A held-out decibel means something different on a project corpus, and the tab
says so instead of quietly handing back a confident number. **The bestiary's 13
shots are 13 kinds of content**, so holding one out asks "does this generalise to
content it has not seen". **A project's shots are one scene from six camera
moves**, so holding one out asks whether walks and orbits predict a strafe — and
the measured answer on `examples/procedural` is no: −0.17 dB, 9 of 18 fold-runs
below bilinear. The tab leads with that and points at Evaluate for the number to
act on, while keeping the per-fold rows, which *are* useful there: they name the
camera move the net falls apart on.

Three more details that are load-bearing rather than decorative:

- **Progress is the tool's own milestones**, and it is honest where it cannot know
  one. Corpus frame *N* of *M*, labelling, epoch *N* of *M* are real fractions —
  frames rather than shots, because a threaded render finishes them out of order
  and because a project corpus only decides how many shots it has once it has
  loaded the scenes. The per-shot lines are a *summary* printed after the last
  frame, so they report the phase finished instead of winding the bar back to
  shot 1. Cross-validation used to print **nothing** for the whole fold loop — it
  turns the trainer's verbosity off — so that stretch was an indeterminate bar and
  an elapsed clock for minutes at a time; it now emits `[blss] fold k of n` as
  each fold finishes, which is a real fraction. A run that finishes while the tab
  is shut still lands — the poll runs every frame from the UI, not from the window
  body.
- **Provenance lives in a sidecar, because the net has nowhere to put it.** A
  `blss.net` is a bare list of floats and records nothing about how it was made, so
  the editor writes the exact command line to **`blss.net.args`** next to a net it
  trained. A net **newer** than its sidecar reports "unknown" rather than a stale
  answer, and a net with no sidecar says it was trained outside this editor. A
  project with the upscaler on and **no** net says in red that the game will be
  built with random weights.
- **The conflict warning is now one mirror, not two.** `blssClashes()` in
  `src/templates.cpp` is the source of truth; the settings block and its warning
  are drawn from one place and called by both this window and the Preferences
  dialog. Leaving the warning inlined in the dialog would have made this window a
  second mirror of an interlock that must not drift.
- **…and it names what caused it, and takes you there.** `blssClashesFor()` walks
  `project_.scenes` / `sc.objects` with the scene and the object *in hand* and
  used to throw both away into four bools, so on a ten-scene project *"a Set Depth
  Of Field flow node turns it on at runtime"* left the reader hunting through
  every graph for a node the editor had already found. Each clash now carries a
  list of `scene > object` labels with the offending value —
  `main > dof-switch  (Set Depth Of Field node 2, amount 0.35)` — and a
  **`Select it`** button that switches to that scene, selects the object and puts
  the camera pivot on it (`blssSelectClash`). Scene-level clashes (depth of field
  is a post-fx setting, not an object) offer *Go to the scene* instead.
- **The warnings are shown whether the feature is on or off.** They used to be
  inside `if (s.blssEnabled)`, so the reader who most needs them — the one
  deciding whether to tick the box — saw nothing at all, and the first thing they
  would then meet is a build refusing with a wall of `#error`. With the feature
  off the block is styled as a note and reads *"If you turn this on, the BUILD
  WILL REFUSE this project"*; with it on it is the amber warning it always was.

Also, `--blss-*` now writes stdout **unbuffered**. A C `stdout` on a pipe is
block-buffered, so `--blss-train | tee`, a CI log and this window all saw nothing
until the process exited and then the whole run at once. Piping one now streams.

Two things the window does **not** have, and both are deliberate: `--probe` and
`--drop-feature`, which belong to the instrument above and would have been
controls nobody could stand behind at the time.

> **What was and was not seen on screen.** The window in its present shape was
> driven with `--ui-script` on two fixtures — a copy of `examples/procedural` and
> a scratch project built to carry every clash at once — and screenshotted at
> each step: the header at idle, `Will this scene benefit?` mid-run and then
> showing *Headroom: +0.95 dB available at 1.22 passes* with its *Train the
> network* button, a finished evaluation with its verdict and parsed table, the
> three-thumbnail strip, the click-through landing on the Compare tab in the
> difference view, the Train tab with the Advanced header both collapsed and
> expanded, the cross-validation cost line, the frame-drift warning appearing and
> then cleared by *Restore defaults*, the provenance-drift warning, the clash
> block in both its informational and its warning form with all four kinds named,
> and a `Select it` that really did select `portal-a` in the Properties panel.
> The live VRAM line was read at both scales on a `Pal576i` project (+512 KB at
> 2×2, exactly 0 at 1×2).
>
> Still **read and never seen**: the finished cross-validation *table*, the error
> banner, and the Inputs tab's per-shot section. What is checked instead of
> looked at is the arithmetic, from a host-only harness (`blss_ui.cpp` +
> `platform.cpp`, ~120 lines) against captured runs: all three verdict branches
> plus the net-free one, `parseVerdictLine` against a real `[blss] verdict` line
> and against text that has none, and `estimate()` against all seven timed runs
> in the table above. That is a different claim from "somebody looked at it".

### The project settings

| Setting | Meaning |
|---|---|
| **Enabled** | render the 3D scene at reduced resolution and reconstruct |
| **Scale** | `2×2` (quarter the pixels) or `1×2` (half-height only — cheaper reconstruction, keeps horizontal detail). A **live line under the combo** works out what it is worth in GS VRAM on *this project's* raster, and says outright that [1×2 is worth nothing](#at-12-the-vram-saving-is-exactly-zero-and-nothing-said-so) |
| **Sharpen strength** | the `k` of passes 4/5; the net decides *where*, this decides *how much* |
| **Temporal** | allow the history pass at all (off = spatial-only, no ghosting, no AA) |
| **Debug view** | three entries: **0** off, **1** tint the frame by the winning kernel per tile (red = point, green = temporal, blue = sharpen), **2** log the feature spread to the game's `bin/log.txt` and leave the picture alone — [the instrument](#the-instrument-that-found-it-and-it-stays-this-time) |

Ticking **Enabled** also puts two notes under the checkbox, and both are there
because a user should not have to discover them from a broken build.

The **standing** one no longer leads with a bestiary decibel, and that was the
point of rewriting it. It used to open with **+0.40 dB / 5 of 39** — a number
about a corpus nobody ships, one re-run stale
([+0.42 dB / 3 of 39](#the-out-of-distribution-number-and-how-to-get-one-that-means-something)
is what this page measures), and the *first* thing a user read. It now opens with
the three facts that decide anything: a bestiary-trained net measured **−0.40 dB
on a real project**, the same trainer fitted to that project's own scenes
measured **+0.06** against a ceiling of **+0.77**, and **some scenes have no
ceiling at all** — on `examples/showcase` the oracle itself is +0.02 dB, which
the window's Evaluate tab will tell you in one line. The bestiary figure is still
in the tooltip, further down, labelled as a number about the bestiary. It ends
where it always did — except that the ending is now a measurement rather than an
absence: a BLSS frame on a real PS2 cost **+9.83 ms and saved nothing**
([profiling.md](profiling.md#timing-a-frame-that-blss-is-in)).

> These are **strings in `drawBlssSettings`, not computed values**, so they have
> to be edited whenever a table on this page is. One known drift today: the
> tooltip quotes the fold spread as **sd 0.40**, where the re-run in
> [Measured](#the-out-of-distribution-number-and-how-to-get-one-that-means-something)
> reads **0.35**.

The **conditional** one lists whichever of **depth of field / portals /
split-screen** *this project actually uses* — the pattern is "warn only about the
conflict you really have". Since `332f3193` the build refuses the same
combination, so the dialog and the build must agree, and the dialog asks the
build's four questions verbatim (`blssClashes()` in `src/templates.cpp` is the
source of truth; the dialog mirrors it because it has to answer for the *staged*
settings, live, while the modal is open):

| the dialog warns when | it used to |
|---|---|
| a scene's **resolved** DoF amount quantises above 1/128 **and** its focus distance is > 0 | any project-wide amount > 0, ignoring focus and the 1/128 step |
| **any `Set Depth Of Field` node** in any object's flow graph, mode 0, that turns DoF on at runtime | *nothing* — a project with an authored amount of 0 everywhere and one such node got a broken picture with no warning at all |
| a portal whose target resolves to **another `Portal` in the same scene** | any `Portal` object, including an unlinked one, which is only a tinted surface |
| split-screen **and** a scene with a **second `Player` object** (`PLAYER2_INDEXES` is what gates the split branch) | the multiplayer preference alone, so it warned about projects that never render a split frame |

Reflections, camera feeds and projected shadows are **not** on that list; they
nest correctly now.

## Measured

### The out-of-distribution number, and how to get one that means something

```bash
tyrax-editor --blss-eval --cv --cv-seeds 3 --assets examples
```

**`--cv` is leave-one-shot-out cross-validation**: every shot held out in turn,
its own net trained on the other twelve, `--cv-seeds N` independent corpora on
top. It ignores `-i` — it trains what it measures. That is the number to act on,
because a single held-out split is a sample of **size one**, and this feature
quoted one five times before anybody checked (see
[the fifth entry](#measured-is-not-optimised-six-times)).

13 shots × 3 seeds = **39 fold-runs**, 156 frames, 512×448 from 256×224, shipped
defaults (`--epochs 400`, `--seed 0xB1557`, `--flicker-weight 0`,
`--fill-weight 16`, weight decay `1e-4`, raw inputs):

| held-out shot | seed `B1557` | seed `CCD704ED` | seed `8814F396` | mean | sd |
|---|---|---|---|---|---|
| 0 `floor-horizon` dolly-in | +0.35 | +0.20 | +0.48 | **+0.34** | 0.12 |
| 1 `boxes-sphere` orbit | +0.58 | +0.61 | +0.53 | **+0.57** | 0.03 |
| 2 `poles` pan | +0.86 | +0.63 | +0.94 | **+0.81** | 0.13 |
| 3 `foliage` static | +0.90 | +0.68 | +1.04 | **+0.87** | 0.15 |
| 4 `grazing-wall` dolly-along | −0.13 | +0.05 | +0.07 | **−0.00** | 0.09 |
| 5 `flat` slow pan | +1.11 | +0.97 | +1.06 | **+1.05** | 0.06 |
| 6 `whip` whip pan | +0.23 | +0.16 | +0.17 | **+0.19** | 0.03 |
| 7 `corridor` dolly-down | −0.19 | +0.02 | −0.28 | **−0.15** | 0.13 |
| 8 `strafe-field` lateral | +0.41 | +0.36 | +0.43 | **+0.40** | 0.03 |
| 9 `pitch-sky` pitch-up | +0.82 | +0.27 | +0.34 | **+0.48** | 0.24 |
| 10 `distant-plain` slow dolly | +0.43 | +0.17 | +0.21 | **+0.27** | 0.11 |
| 11 `sphere-field` orbit-wide | +0.04 | +0.30 | +0.64 | **+0.33** | 0.25 |
| 12 `foliage-walk` dolly-through | +0.31 | +0.22 | +0.21 | **+0.25** | 0.05 |
| **mean over folds** | **+0.44** | **+0.36** | **+0.45** | **+0.42** | **0.04** |

> **BLSS beats plain bilinear out of distribution by +0.42 dB**, sd 0.35 over 39
> fold-runs, **3 of 39 below bilinear**, at **1.80 mean full-screen passes**
> (sd 0.30) against 1.00 for bilinear.

**The conservative figure is +0.26 dB.** Shots 7–12 are the six that were added
*after* the defaults were chosen and took no part in choosing them; their fold
means are −0.15, +0.40, +0.48, +0.27, +0.33, +0.25. Quote that one when the
question is "will it help on content nobody tuned for".

Per fold, mean over the three seeds — `in-dist` is the same margin on that fold's
twelve **training** shots, i.e. the control that says the fold trained at all
(a held-out number under a collapsed `in-dist` number means nothing):

| held-out shot | native | bilinear | BLSS | oracle | passes | flicker | in-dist |
|---|---|---|---|---|---|---|---|
| 0 `floor-horizon` | 19.57 | 18.88 | 19.22 | 20.28 | 1.49 | 33.98 | +0.49 |
| 1 `boxes-sphere` | 20.26 | 18.95 | 19.52 | 19.85 | 2.07 | 45.85 | +0.47 |
| 2 `poles` | 22.11 | 21.07 | 21.88 | 23.13 | 1.48 | 29.08 | +0.41 |
| 3 `foliage` | 28.13 | 25.61 | 26.48 | 27.78 | 1.85 | 6.13 | +0.42 |
| 4 `grazing-wall` | 29.31 | 27.55 | 27.54 | 28.01 | 1.87 | 7.82 | +0.55 |
| 5 `flat` | 58.30 | 54.67 | 55.72 | 54.67 | 2.12 | 0.04 | +0.52 |
| 6 `whip` | 25.48 | 22.58 | 22.77 | 23.62 | 1.96 | 41.93 | +0.56 |
| 7 `corridor` | 31.80 | 27.42 | 27.28 | 27.59 | 2.11 | 17.68 | +0.55 |
| 8 `strafe-field` | 22.58 | 21.67 | 22.07 | 22.79 | 1.81 | 21.11 | +0.49 |
| 9 `pitch-sky` | 22.86 | 22.15 | 22.62 | 24.40 | 1.53 | 38.19 | +0.51 |
| 10 `distant-plain` | 22.71 | 23.39 | 23.66 | 24.83 | 1.49 | 12.90 | +0.49 |
| 11 `sphere-field` | 32.76 | 31.06 | 31.39 | 31.74 | 1.90 | 14.66 | +0.54 |
| 12 `foliage-walk` | 29.32 | 27.40 | 27.64 | 27.82 | 1.77 | 14.22 | +0.55 |

> **These numbers were re-run for this page, not copied.** They moved, and mostly
> in the right direction: the mean is +0.42 against the +0.40 this page used to
> print, the folds below bilinear dropped from 5 to 3, and the pass count fell
> from 2.85 to **1.80** — the deadzone and the proxy fix between them took a whole
> full-screen pass out of the frame. Two numbers got *worse* and are worth naming:
> the sd of the per-seed fold mean is **0.04** rather than 0.01, and two folds
> (`pitch-sky`, `sphere-field`) now spread 0.24–0.25 dB across seeds. The seed
> still moves the answer far less than the fold does, but by four times less
> margin than this page claimed.
>
> **Re-run again at `7d3dbf67`, on the threaded corpus, and it reproduces cell
> for cell** — every fold mean, every per-seed column, the +0.42, the sd 0.35,
> the 3 of 39, the 1.80 passes and the 0.04 per-seed fold-mean sd. That is what
> the determinism contract behind `--threads`
> ([above](#--threads-n-and-the-determinism-that-pays-for-it)) is *for*. The
> whole table now costs **3 min 38 s** of wall clock (16 min 49 s of CPU) on 6
> cores, which is why it is reasonable to re-run it rather than quote it.

**The one shot it still loses on is `corridor` (−0.15), and the reason is a
feature the network effectively does not have there.** `depth` is `1/w`
normalised against `kDepthRef = 8` and clamped, so it reads 1.0 for anything
closer than eight units. `--blss-eval --features` prints the per-shot mean of each
channel, and `corridor`'s `depth` is **0.972** — the highest of the thirteen,
against a corpus mean of 0.713. A corridor is a few units wide; that channel
spends the shot against its clamp. **A saturated feature is a feature the network
does not have**, so on `corridor` it is deciding from five inputs, and it decides
slightly worse than not deciding. `grazing-wall` (−0.00) is the other fold that
buys nothing.

That is a *feature-scaling* problem with a name and a fix (a log or reciprocal
depth mapping), not an inherent limit — and it is not confined to one shot:
**58.6 % of all tiles in the corpus read `depth` at exactly 1.0**, and
`depthGrad` (61.5 %) and `coverage` (71.9 %) are worse. Add
`--features` to any investigation of "why does it not help here" before touching
the topology; the channel statistics are the first place to look.

`flat` is worth a second look for the opposite reason: 55.72 dB against
bilinear's 54.67 on a screen with nothing in it, at **2.12 passes** — the highest
pass count of the thirteen folds, on the one shot where no decibel is visible.
The old reading of this fold was 4.33 passes and it has come down a long way, but
it is still where the remaining fill lives.

### Against every fixed kernel

```bash
tyrax-editor --blss-train --assets examples
tyrax-editor --blss-eval  --assets examples
```

Three families of column, and each exists because a bug got past the ones before
it:

- **PSNR** against the 4× supersampled ground truth.
- **flicker** — mean per-pixel change between consecutive frames of one shot.
  Per-frame PSNR is structurally blind to temporal instability, and worse,
  *rewarded* the bug that produced it. Compare a row against `native`, which is
  the honest floor for a given camera move, not against zero.
- **point / temp / sharp / passes** — occupancy: what fraction of grid cells each
  pass actually draws, measured through the engine's own skip rule, and the mean
  full-screen passes per frame. **1.00 is plain bilinear, 5.00 is the worst
  case.** Until this was printed the network was quietly asking for every kernel
  everywhere.

Training shots (108 frames, the 9 shots with `shot % 3 != 1`):

| | PSNR | flicker | point | temp | sharp | passes |
|---|---|---|---|---|---|---|
| native full-res, 1 sample | 28.81 | 22.39 | — | — | — | — |
| half-res + point | 24.31 | 24.48 | 100 % | 0 % | 0 % | 2.00 |
| half-res + bilinear | 27.11 | 22.91 | 0 % | 0 % | 0 % | 1.00 |
| half-res + temporal | 23.07 | 13.65 | 0 % | 100 % | 0 % | 2.00 |
| half-res + sharpen | 24.85 | 25.34 | 0 % | 0 % | 100 % | 3.00 |
| **half-res + BLSS (trained)** | **27.81** | **21.43** | 0 % | 66.7 % | 0 % | **1.67** |
| half-res + oracle weights | 28.70 | 20.01 | 7.3 % | 43.5 % | 2.3 % | 1.55 |

Held-out shots (48 frames — `boxes-sphere`, `grazing-wall`, `corridor`,
`distant-plain`):

| | PSNR | flicker | point | temp | sharp | passes |
|---|---|---|---|---|---|---|
| native full-res, 1 sample | 26.02 | 24.86 | — | — | — | — |
| half-res + point | 21.63 | 25.05 | 100 % | 0 % | 0 % | 2.00 |
| half-res + bilinear | 24.32 | 22.47 | 0 % | 0 % | 0 % | 1.00 |
| half-res + temporal | 17.25 | 15.74 | 0 % | 100 % | 0 % | 2.00 |
| half-res + sharpen | 22.34 | 26.96 | 0 % | 0 % | 100 % | 3.00 |
| **half-res + BLSS (trained)** | **24.49** | **21.17** | 0 % | 86.6 % | 0 % | **1.87** |
| half-res + oracle weights | 25.19 | 19.96 | 3.0 % | 33.0 % | 0.0 % | 1.36 |

> **Both tables above were re-run at `7d3dbf67` and reproduce cell for cell**,
> and the re-run corrected a false note this page used to carry: *"the tool
> prints the oracle row for the held-out split only"*. It does not — the oracle
> is one of the six rows and both splits get all six. What is held-out-only is
> the **PNG dump**. The training table simply had no upper bound in it, and now
> it does: the oracle reaches **28.70 dB at 1.55 passes** in distribution against
> the network's 27.81 at 1.67, i.e. **the network is paying more fill than the
> oracle for 0.89 dB less** — the same diagnosis the held-out split gives, on the
> split where the win was never in question.

**In distribution the win is +0.70 dB over the best fixed kernel** (27.81 against
bilinear's 27.11), at less flicker than a full-resolution native render (21.43
against 22.39) — which is what the temporal pass is for. That row has never been
the problem.

**The occupancy columns are the ones that changed most, and they changed the
conclusion.** This page used to report 80–90 % point occupancy and read it as "the
network fails to generalise the cost model". At the shipped deadzone the point and
sharpen passes are now culled **completely** — 0 % on both splits — and only the
temporal pass is drawn over most of the screen (66.7 % / 86.6 %). The net spends
1.67–1.87 passes where the oracle reaches a better PSNR at 1.36, so the headroom
that remains is roughly **half a pass**, not one and a half. The diagnosis is the
same in kind and much smaller in size.

**The held-out row of that second table is +0.17 dB, and it is still the best
illustration on this page of why you should not quote it.** The same net,
measured properly by holding out each shot in turn, is **+0.42 dB**. Nothing
changed but the question: this split contains `corridor`, the one shot of
thirteen the network loses on, so a quarter of its held-out frames come from the
single worst case. (When this page was first written that same split read
**−0.02 dB** against a 13-fold mean of +0.40 — the split has caught up as the
`corridor` loss shrank, which is luck rather than a reason to start trusting it.)
A single split can be wrong in either direction, and until `--cv` existed nobody
could tell which.

### How to read these tables, and what NOT to read into them

- **In distribution the win is solid** and always has been — every fold's
  `in-dist` column above sits between +0.41 and +0.56 dB against bilinear on its
  own twelve training shots, and against the best *fixed kernel* the margin on
  the shipped split is the one in the table above. That win has never been in
  question and is not what this page kept having to retract.
- **The ±0.4 dB this page used to blame on the training seed is
  SPLIT-SELECTION variance, not seed variance.** The 39-fold table separates the
  two, and they are an order of magnitude apart:

  | source of spread | sd |
  |---|---|
  | which shot you hold out (fold to fold) | **0.35 dB** |
  | which seed you train at (per-seed fold **mean**) | **0.04 dB** |

  The per-fold sd across the three seeds runs 0.03–0.25 dB, so the seed does move
  an individual fold — on `pitch-sky` and `sphere-field` by a quarter of a decibel
  — but the *answer to the question* moves an order of magnitude less. Four earlier
  runs at four `--seed` values read −0.23 to +0.26 dB and this page called that
  seed noise. It was not. It was four draws from a distribution whose spread comes
  mostly from **which two shots the split happened to contain**, and the seed was
  along for the ride. (This page previously put the per-seed sd at 0.01 dB. It is
  0.04 on a re-run, so the gap is a factor of nine rather than forty; the
  conclusion survives the correction and the arithmetic did not.)
- **A single held-out split is a sample of size one, and quoting one was the
  mistake.** Earlier drafts quoted "+0.18 dB", "+0.24 dB", then "≈+0.1 dB, one
  loss in four seeds, statistically a draw". All three were one draw dressed as
  an estimate, and the last of them **understated a real win** that measures
  +0.42 dB today. Use `--cv`. The cost is a few minutes of CPU.
- **Sparsity is close to working now, and it took three separate changes.** All
  three occupancy columns have collapsed since this section was written: the fill
  term culled sharpen (100 % → 14.9 %), the inference deadzone then took sharpen
  and **point** to 0 %, and the proxy fix stopped the net being handed a constant.
  What is left is the temporal pass at 66.7 % / 86.6 % and 1.67–1.87 mean passes
  against the oracle's 1.36 — so the network still fails to generalise the cost
  model, by about **half a pass** rather than the one and a half this page used to
  describe. Anything on this page or in the math doc that says passes 2..5 "cover
  a minority of the screen" is now true of point and sharpen and **not** of
  temporal.
- **`native` is not a ceiling** — the reference is supersampled, so a good
  temporal reconstruction can in principle beat a 1-sample full-resolution
  render, and on several folds above it does not, while on the training side it
  nearly does.
- **The oracle column is the regression test.** A parity break between the host
  twin and the engine shows up as the BLSS column falling well below the oracle
  column. Note that on `flat` the oracle scores *below* the trained net (54.67
  against 55.72) — the oracle is optimising accuracy **plus fill**, and on an
  empty screen it correctly refuses to pay for a decibel nobody can see.

### Two channels the network lost, and the measurements that took them

The input vector was eight channels and is six. **Neither deletion was a
simplification** — both were negative results, both were found by the same
instrument, and this is the entry on this page most worth reading before adding a
channel to anything.

The instrument is `--blss-eval --cv --drop-feature <name>`, which holds a channel
at zero over the whole corpus (training, labelling and evaluation), i.e. does to a
trained net exactly what deleting the channel would. **It only means anything with
a CONTROL**, and `edgeDens` — a channel that is demonstrably pulling its weight —
is that control: dropping it says how large a difference the instrument can
resolve at all.

**`histAge` — frames since this tile last changed.** It was designed as the
recurrent channel: the network's own memory of which tiles have been stable.
Measured at `kFeatures = 8`, 39 fold-runs per row:

| held at zero | (nothing) | `histAge` | `edgeDens` (control) |
|---|---|---|---|
| held-out margin | +0.38 | **+0.41** | +0.36 dB over bilinear |
| mean passes | 1.85 | 1.84 | 1.76 |
| folds below bilinear | 4/39 | 5/39 | 4/39 |

Dropping the control costs 0.02 dB, so the instrument resolves that; `histAge` is
0.03 dB on the *other* side of it. **The channel was not neutral, it was harmful.**
And the fold that gained most is the one that indicts it: `foliage static`
(+0.46 → +0.77) — the shot with the **highest** `histAge` in the corpus. The
channel was hurting hardest exactly where it existed to help, which is the
signature of a network memorising "this shot has been still a while" instead of
learning anything about reconstruction.

Deleting it took the **entire recurrent path** with it: the per-tile counters,
`prevDepth`/`prevCover`, `updateHistAge()`, and the ordering rule between building
features and ageing tiles — which was the twin contract's most drift-prone rule,
and had already drifted once (the two sides implemented different thresholds for
it, feeding the network a training-time channel the console would never
reproduce). `buildFeatures()` became a **pure function of one frame** on both
twins.

**`luma` — the tile's mean brightness.** It went one commit later, and for a
reason the console instrument had already flagged: **the EE cannot produce it.** A
bag hands BLSS one scalar for its brightness and `stapip_core` can only fill it
when the bag has a *single* colour. A per-vertex-lit mesh — which is every static
mesh a generated game submits — has no cheap mean, so the channel read a constant
**0.5** on the console while the corpus spread it over 0–0.48. Fitted on a
photometric feature, run on a constant, and the constant was **out of the corpus'
range**. Measured the same way at `kFeatures = 7`:

| held at zero | (nothing) | `luma` | `edgeDens` (control) |
|---|---|---|---|
| held-out margin | +0.41 | **+0.43** | +0.35 dB over bilinear |
| mean passes | 1.83 | 1.80 | 1.75 |
| folds below bilinear | 5/39 | 5/39 | 4/39 |

Same shape: the control costs 0.06 dB, `luma` is 0.02 dB the other way. Deleting it
removed the last quantity the two sides computed **differently** — `BagProxy::luma`,
`TileStats::luma`, the EE's per-bag colour average, one engine accumulator and the
corpus' `measureLuma()`.

Three things to take from this rather than the decibels, which are small:

1. **A channel the two producers compute differently is worse than no channel.**
   Both deletions were of exactly that, and in both cases the host's own numbers
   looked fine while it was happening.
2. **A negative result needs a control or it is not a result.** 0.02–0.03 dB
   means nothing until something known-useful has been dropped for comparison.
3. **The tables above are kept, not the channels.** If you want a photometric
   feature back, the honest form is a per-mesh mean brightness **baked by the
   editor** into the bag, not sampled at run time — the same shape as the
   `texDetail` follow-up. Re-running these two experiments is minutes; finding out
   they were run is what this section is for.

What is left is six channels that are all geometric, all cheap, and all computed
the same way on both twins.

### The network is variance-limited, not optimisation-limited

**Anything that makes the fit easier makes the feature worse.** The clearest
demonstration is `--standardise`, which fixes a real defect: the input channels
have wildly different scales, and standardising them (mean 0, unit variance,
folded back into `w1`/`b1` so the engine still sees raw features and the twin
contract is untouched) fits the *training* shots better. Cross-validated on the
same 39 fold-runs, it generalises **worse**:

| | raw inputs (shipped) | `--standardise` |
|---|---|---|
| held-out margin over bilinear | **+0.40 dB** | +0.24 dB |
| sd over 39 fold-runs | 0.40 | 0.47 |
| folds below bilinear | 5 / 39 | **9 / 39** |
| mean passes | 2.85 | 2.94 |

It was worse on ten of the thirteen folds, turned `floor-horizon` (+0.35 → −0.20)
and `sphere-field` (+0.12 → −0.10) into losses, and made the one existing loss
deeper (`corridor` −0.52 → −0.88). The same experiment on the old seven-shot
corpus moved +0.31 dB to −0.15 (`1b9c7a74`'s own run; that corpus no longer
exists, but the direction is the same and larger).

> **That table was measured at `kFeatures = 8` and has NOT been re-run since the
> vector shrank to six.** Its raw-input column reads +0.40 / 2.85 passes where
> today's is +0.42 / 1.80, so both columns would move; what is being claimed here
> is the *direction*, which two independent corpora agree on. Re-run it with
> `--blss-eval --cv --cv-seeds 3 --standardise` before quoting either number.

So the search went to **regularisation** instead, and that is where the win came
from: weight decay `1e-5` → **`1e-4`**, measured at `1b9c7a74` as worth +0.19 dB
and a whole pass. `--standardise` stays as a flag with its numbers attached, on the same principle
as `--flicker-weight`: **a knob measured and set to zero is not the same as a
knob deleted**, and the next person to notice the input scales should find this
table instead of re-running it.

The moral generalises past this feature: a 6→12→3 MLP with 123 weights fitted to
24 000 tiles is not short of capacity or short of optimisation. Every measurement
in this section says the same thing — the corpus was the binding constraint, more
regularisation helped, and easier fitting hurt.

### Two bugs the table used to hide, both fixed

Kept because both were caught by a human watching an emulator rather than by any
metric in this repo, and both moved the numbers a long way:

- **The temporal pass was an accumulator with a one-frame time constant.** It
  capped at a flat 50 % mix of the history — but the history is the previous
  frame's own *composite*, so that is an exponential accumulator, and against a
  jitter that alternates every frame it *tracks* the alternation instead of
  averaging it out, settling into a stationary sub-pixel oscillation that reads
  as bob deinterlacing on a television. Per-frame PSNR **rewarded** it (a mix of
  two jitter phases really is closer to the supersampled truth than either).
  `kTemporalMax` is now 115 — ~0.9 retention, about a 10-frame constant — which
  fixed the bob *and* improved PSNR.
- **The network was reconstructing the sky.** The oracle's importance weighting
  gives "tiles where every kernel is identical" no vote, so empty tiles were never
  supervised and the net asked for full temporal reconstruction of them: free in
  PSNR, ghosting the moment the camera turns. Both twins now force tiles below
  `kMinCoverage` to zero.

Together those two moved the held-out row from **2.23 dB below** bilinear to
about parity with it. Everything since — six more corpus shots, weight decay
`1e-5` → `1e-4`, fill 6 → 16, an inference deadzone and a proxy fix that stopped
the console being handed a constant — took it from parity to the **+0.42 dB** at
the top of this section, and cross-validation is what showed that the "parity" reading
was itself one draw.

### On the console

Booted in PCSX2 from a scratch `fpp` project (512x512 interlaced, 2 640+ frames,
50 FPS, 100 % speed, no assert, one 256x256 low-res target at 256 KB — the
224 KB figure this line used to carry is the 256×224 target of a 512×448 mode,
not this one). Frame-to-frame change of the *displayed* picture on a static
camera, same emulator settings for both:

| | on-screen change per frame |
|---|---|
| BLSS off | **0.000 %** |
| BLSS on | 0.02 – 0.16 % |

So the reconstruction is stable but not perfectly still: the accumulator's tail
is visible as ~800 changed pixels out of 645 000.

Three things about that table, all of them limits rather than results:

- **It predates the fill term, and by now several retunes on top of that.** That
  stability table was taken from a net trained by the old fill-blind objective,
  which asked for four to five full-screen passes; the shipped net has since
  changed corpus (7 shots → 13), fill weight (6 → 16), weight decay
  (`1e-5` → `1e-4`), gained an inference deadzone and lost two input channels. So
  the *picture* half of this feature is **stale, not wrong**, and it is stale by
  more than it was.
- **One console measurement on this page is current, and it is the instrument's,
  not the picture's.** The `BLSSGRID`/`BLSSFEAT`/`BLSSFILL` figures in
  [§2](#where-a-bags-screen-box-comes-from-and-why-that-was-the-bug) — 2 → 41
  proxies, 5.00 → 1.96 passes, the channels unpinned — were read out of the game's
  own log on a booted `fpp` fixture at `6a4cbead`, and they are numbers about
  *what the network sees and what fill it asks for*, not about how the frame
  looks or how long it takes. **The sky-dome opt-out that landed after them has
  never been booted at all**: the machine it was written on lost its compositor
  mid-session and PCSX2 would not open a GS window, so that change is verified at
  compile level (engine and ELF built in Docker) and nowhere else. Re-run debug
  view 2 on a live session before quoting any on-console number past that
  commit.
- **The bob it describes was later re-measured and re-explained** — the
  interlacing story in the paragraph that used to sit here was refuted. See
  [The oscillation](#the-oscillation).
- **Frame timings are measured now, and the pass counts still are not
  milliseconds.** A real PS2 A/B exists
  ([profiling.md](profiling.md#what-the-rig-measured-the-first-time-it-was-run-2026-08-08)):
  **+9.83 ms per frame, saving nothing**, on a frame that turned out to be
  EE-bound. Every *other* performance statement on this page — the pass counts
  above all — is still arithmetic and host measurement about **fill**, and 1.8
  passes has never been converted into a millisecond by anything but the
  calibration on that page (0.587 ms per full-screen blended pass on hardware).
- **Its VRAM line predates the z-buffer shrink.** The residency numbers that are
  current are the `VRAMSTAT` table in [§5 VRAM](#5-vram), taken on a later boot
  of the same kind of fixture.

### The oscillation

**The picture shook, and it is not the interlacing.** Everything in this
subsection was measured on a scratch project with a static camera, **before the
fill term** — it is why the objective changed, not a description of what the
current build does on a television. In this order:

| display mode | BLSS | PCSX2 deinterlacer | result |
|---|---|---|---|
| interlaced | off | on *and* off | perfectly still |
| interlaced | on | off | **visible bob** |
| interlaced | on | on | bob hidden |
| progressive 480p | on | on *or* off | **still bobs**, deinterlacer irrelevant |
| progressive 480p | on, **jitter forced to 0** | off | **perfectly still** |

The first four rows killed the obvious hypothesis: a blending deinterlacer was
merely *averaging adjacent frames* and hiding the effect, which is why turning it
on looked like a fix. The last row is the answer.

And it is not a sign error in the jitter undo — the signs and units were checked
on both sides. It is more basic than that: **jittered sampling is supposed to
produce a different image every frame.** The point kernel snaps to a different
texel per phase; bilinear gets different weights; the low-res render itself
samples different scene points. That is where the extra information comes from.
The only thing entitled to fuse it back into a stable picture is the temporal
accumulator — and it was not converging hard enough, because nothing in the
objective asked it to.

#### Re-measured with the fill term in (2026-08-08): still there

The fill term culls the point and sharpen passes, which are the two that
alternate with the jitter, and that was the reason to hope. It is not enough,
and now there is a number instead of a hope. The measurement is in
[profiling.md](profiling.md#the-stability-gate-period-2--the-bob); the short
version is a static camera, captures at an interval that is deliberately **not**
frame-locked, and a test for the **period-2 signature** (two balanced clusters
of frames) rather than for "did the picture change" — which is the trap that hid
this the first time, when a 40-frame sampling stride landed on the same jitter
phase every time.

| configuration | clusters | within | between | amplitude |
|---|---|---|---|---|
| BLSS off | 17 / 3 | 0.014 % | 0.053 % | 0.03/255 |
| BLSS on, `blssJitter` **off** | 12 / 8 | 0.029 % | 0.046 % | 0.03/255 |
| BLSS on, `blssJitter` **on** | **8 / 8** | 0.019 % | **30.8 %** | **1.42/255** |

**30.8 % of the picture alternates between two images every frame**, on a net
trained on the project's own scenes. Why the fill term did not save it: it culls
point and sharpen, but on a real project's scenes it culls the **temporal** pass
too — and the temporal accumulator is the only thing entitled to fuse the two
phases. The remaining bilinear base pass still reconstructs from a low-res
render that sampled different scene points each phase. The shipping
configuration was "jitter on, nothing fusing it".

**The kill switch.** `blssJitter` (`ProjectSettings`, default **true**) pins the
offset to 0: pure spatial upscale, no temporal supersampling, stable by
construction — a known quality cost for a known cure, and the A/B that proves
the jitter is the cause on any given build. It reaches the engine as
`BLSS_JITTER` → `RendererCoreBlss::configure(..., jitter)`; the parameter is
defaulted to `true` so previously generated games are unchanged. **The host twin
in `src/blss.cpp` does not know about it**: the oracle and the corpus always
model the jittered sampler, so a net trained today and run with `blssJitter:
false` is being run slightly out of distribution. Wiring the flag through the
trainer is the obvious next step and has not been done.

### "Measured is not optimised", six times

**This is the most useful thing on this page.** The same mistake was made six
times, each time one level further up, and each time it cost a debugging session.
The first four are one sentence: *anything absent from the objective does not
exist for the network.* The fifth is the same sentence about the **measurement**
rather than the objective, and it produced the most wrong text. The sixth is the
same sentence about **what was being measured on**, and it is the one that had
been running longest.

1. **per-frame PSNR could not see flicker.** It is a still-image metric; a
   reconstruction that oscillates between two jitter phases scores *better* than
   one that fuses them, because the average of the two phases is genuinely closer
   to the supersampled truth than either.
2. **the on-screen sampler could not see it either.** 0.8 s at 50 Hz is 40
   frames — an even number, so every sample landed on the same jitter phase and
   the picture looked perfectly still to the tool while bobbing on the TV.
3. **flicker got measured, but only *reported*.** `--blss-eval` grew the column,
   and the oracle went on scoring single-frame PSNR alone, so the labels carried
   no notion of stability and asking for history stayed free.
4. **nothing ever charged for a kernel.** Sparsity is the entire performance case
   for BLSS — passes 2..5 are emitted per grid cell and a cell whose alpha byte
   rounds to zero is skipped — and the objective was blind to it, so the net asked
   for everything everywhere (`blssDebugView` showed the temporal channel
   saturated across the whole frame, sky included) and the composite degenerated
   to five full-screen passes.
5. **the measurement itself was a sample of size one, and nothing said so.**
   Every out-of-distribution decibel this feature ever quoted came out of a
   *single* held-out split — 2 shots of 7, chosen once by
   `isHeldOut(shot) = shot % 3 == 1` and never varied. Re-running the cycle at
   four `--seed` values moved the answer from −0.23 to +0.26 dB, and this page
   wrote that down as **seed noise**, five times, in five places. It is not:
   under cross-validation the sd of the per-seed *fold mean* is **0.04 dB**,
   while the sd from fold to fold is **0.35 dB**. The spread was the split.

   What that cost, in order of how wrong each one was:

   - the honest-sounding retraction — "≈+0.1 dB, one loss in four seeds,
     statistically a draw" — **understated a win that measures +0.42 dB today**
     and was the summary printed in the README, the preferences dialog, the
     engine skill and the backlog;
   - `kFillWeight`'s "**sharp knee at 6**", including "12 is a full decibel below
     plain bilinear", was an artefact of that one split. Over 21 fold-runs per
     point the shape is a **plateau from 12 to 24**, not a cliff, and the fill
     falls by most of a pass across it. The default is **16**;
   - `--flicker-weight 0.02` was reported to cost **0.22 dB**. Re-measured under
     cross-validation it costs **0.02 dB** — and moves the flicker column not at
     all, which is the actual reason it ships at zero. The old number made a
     knob look expensive; the new one makes it look **useless**, which is more
     useful to know.

   The clean illustration is that a single split can be *wrong in either
   direction and you cannot tell which*: when this was written the shipped split
   read about **zero** while the 13-fold mean read **+0.40**, because the split
   happens to contain `corridor` — the shot the network loses on. Nothing about
   the network changed between those two numbers. Only the question did. (The
   same split reads +0.17 against +0.42 today, which is the same lesson with a
   smaller gap and no more trustworthy for it.)

   The rule that follows: **`--blss-eval --cv` for anything you intend to act
   on.** A plain `--blss-eval` is a fast look at one split, and its held-out
   columns are worth exactly what one draw is worth.

6. **the network was fitted to one distribution and run on another, and for
   eleven commits nobody could see it.** `--blss-eval --features` could always
   describe the *corpus'* inputs. Nothing described the **console's**. An earlier
   round added exactly that probe, read it once and deleted it — so every channel
   statistic, every saturation percentage and every "this feature is doing
   nothing here" diagnosis on this page was a statement about the training set
   being read as a statement about the game.

   What it was hiding, once the instrument was permanent: a generated game's
   whole frame described by **two** bounding spheres, `depth`, `depthGrad` and
   `coverage` pinned at 1.0 in every tile, the network emitting a constant, and
   the composite paying **5.00 of a possible 5.00 passes**. None of that is
   visible in a host number, because the host's own frames were fine. And the
   `luma` channel was worse than pinned: it read a constant **outside** the range
   the corpus had taught.

   The rule that follows: **an instrument that only one side of a twin can run is
   not an instrument.** Both halves ship permanently now — engine debug view 2
   and `--blss-eval --probe` — and they are the pair to reach for before
   believing anything on this page about what the console sees.

### What was tried for (3), and what it cost

The fix this page used to prescribe — **score the oracle over a PAIR of
consecutive frames with a penalty on the difference** — has now been implemented,
swept twice and **measured to be a bad trade, for a different reason than the
first sweep gave.** It is `--flicker-weight`, and it ships at **0**.

The **first** sweep (84 frames, 600 epochs, 3–6 training seeds per point, at fill
6) read its answer off the single 2-of-7 split and reported a steep price:

| `--flicker-weight` | 0.00 | 0.02 | 0.05 | 0.15 |
|---|---|---|---|---|
| held-out PSNR | 23.38 | 23.16 | 22.90 | 22.37 |
| training flicker | 21.01 | 20.86 | 20.80 | 20.20 |

i.e. "0.02 already scores below plain bilinear". **That overstated it by an order
of magnitude.** Re-measured under cross-validation (`--cv --cv-seeds 3`, 13
shots, 156 frames, decay `1e-4`, fill 16 — the same 21 fold-runs with and without
it; recorded in `src/blss.hpp`, and **not re-run in this pass**):

| `--flicker-weight` | 0.00 | 0.02 |
|---|---|---|
| held-out margin over bilinear | +0.55 dB | +0.53 dB |
| mean passes | 3.00 | 2.89 |

**It costs 0.02 dB, not 0.22 — and it buys nothing**: the per-fold flicker column
is identical to two decimal places on every one of the six folds. So the reason
it ships at zero is not that it is expensive. It is that **it does not work.**

**Why the form is wrong, not just the weight.** The penalty is MSE between the
output and the reprojected history, and that quantity is minimised by
`out == history` — by the picture **FREEZING**. Freezing is free on the corpus's
near-static training shots and is *ghosting* on the held-out orbit and dolly. The
term cannot tell "stable because the jitter got fused" from "stable because
nothing moved". If the console still oscillates, this knob is available and the
table above is its price — but **fix the form first**: gate the penalty on
reprojection confidence so it cannot be paid by freezing.

The knob, the term and the CLI flag all stay. **Setting a weight to zero after
measuring it is not the same as deleting it**, and the next person to have this
idea should find the measurement instead of re-running it.

### What actually delivered the stability

**The fill term (4), by accident of what it culls.** Charging for kernels culls
the point and sharpen passes — which are exactly the two that alternate with the
jitter — so stability falls out of the cost model for free. At flicker weight 0,
moving fill from 0 to 6:

| | flicker before | flicker after |
|---|---|---|
| training | 21.49 | 21.01 |
| held-out | 27.12 | 26.62 |

and sharpen occupancy collapses from 79 % to 28 % in distribution and 93 % to
14 % out of it, at **no cost in distribution and a small gain out of it**.

**The fill weight's own sweep was the loudest casualty of the one-split
measurement.** It used to read like this, off a single 2-of-7 held-out split:

| `--fill-weight` | 0 | 2 | 4 | **6** | 7.5 | 9 | 12 | 24 |
|---|---|---|---|---|---|---|---|---|
| held-out PSNR | 23.26 | 23.44 | 23.44 | **23.38** | 22.85 | 22.69 | 22.45 | 22.86 |
| mean passes | 4.25 | 3.99 | 3.84 | **3.39** | 3.29 | 2.81 | 2.58 | 2.43 |

— "the knee is sharp: past 6 the network stops generalising, and 12 is a full
decibel *below* plain bilinear". **There is no knee at 6 and there is no cliff.**
Re-swept over 21 fold-runs per point (7 folds × 3 seeds, 13 shots, 156 frames,
400 epochs, decay `1e-4`, holding out each of the *original* seven shots in turn
so every column is the same held-out content; recorded in `src/blss.hpp`, and
**not re-run in this pass**):

| `--fill-weight` | 6 | 12 | **16** | 24 | 40 |
|---|---|---|---|---|---|
| held-out margin over bilinear | +0.36 | +0.53 | **+0.55** | +0.50 | +0.43 |
| mean passes | 3.92 | 3.59 | **3.00** | 2.76 | 2.67 |
| folds below bilinear | 3/21 | 2/21 | **2/21** | 1/21 | 1/21 |

**The shape is a plateau from 12 to 24**, across which the fill falls by most of
a pass, and 16 sits in the middle of it. The old table's cliff was one unlucky
pair of shots — and note that the *old* recommendation, 6, is the worst point on
the new sweep. The fill weight and the weight decay also have to be set
**together**: at decay `1e-5` the same corpus reads +0.36 at fill 6 and +0.33 at
fill 12, so a sweep of one at the wrong value of the other measures neither.

### What is still open

- **Nobody has watched the emulator since the fill term landed.** The host says
  the picture is more stable; whether the bob is gone from a television is
  **unverified**. The honest workaround until someone looks is unchanged: run with
  the jitter disabled, which gives up the temporal supersampling and keeps only
  the spatial kernel selection.
- **Fix the depth channel's saturation.** This is the most promising item left
  and it has a measurement behind it: `depth` is a clamped `1/w` against
  `kDepthRef = 8`, **58.8 % of all corpus tiles read it at exactly 1.0**, and the
  one shot of thirteen where BLSS loses to bilinear (`corridor`, −0.52 dB) is the
  one whose mean sits highest against that clamp at 0.972. `depthGrad` (61.6 % at
  1.0) and `coverage` (71.7 %) are worse. A log or reciprocal mapping is a change
  to `buildFeatures()` on **both twins at once**
  ([the math doc](blss-reconstruction.md), §4),
  so it is a contract change, not a tweak — and `--blss-eval --cv` is what says
  whether it worked.
- **The network spends fill where nothing is visible.** On `flat` — a slow pan
  over an empty untextured area — it draws **2.12 full-screen passes**, the most
  of any fold, to buy 1.05 dB that no eye can see, while the oracle scores *below*
  it because the oracle is charged for the fill and correctly declines. This fold
  read 4.33 passes before the deadzone and the proxy fix, so it has come a long
  way and is still the outlier. "Learn when the answer does not matter" is a
  training-weight question, not a topology one.
- **Occupancy is noisier than PSNR.** Mean passes over the 39 fold-runs is 1.80
  with sd **0.30**, i.e. a sixth of the whole budget, against a PSNR sd of 0.35 dB
  on a much smaller scale. So "≈1.8 passes" is the right order of magnitude and
  the wrong number to put in a fill budget — size anything that has to be
  *correct* off the 5.00 worst case.
- **The training bottleneck is now the fit, and it is the phase that cannot be
  threaded.** At every core the corpus render is 1.9 s (10 %), the oracle 10.4 s
  (56 %) and the Adam SGD **6.2 s (34 %)** — each step reads the weights the
  previous one wrote, so `--threads` does nothing for it
  ([the table](#--threads-n-and-the-determinism-that-pays-for-it)). It is not
  worth a GPU at these sizes; anyone wanting the cycle faster should look at the
  oracle's coordinate descent first, which is still more than half of it.
- **DONE — the Debug view combo has its third entry** (`7d3dbf67`). It offered
  only 0 and 1 against a field the loader clamps to `0..2`, so reaching the
  console instrument meant hand-editing the `.tyra` and a project that had it on
  displayed as "Off".

**Frame timings are still not measured, and real hardware has never run this
at all.** No profiling pass exists in the emulator and no PS2 has booted it, so
every performance claim on this page is fill arithmetic plus host measurement.
Occupancy is a count of grid cells, not a millisecond.

## Limitations

- **The features are geometric, not photometric.** Nothing reads the rendered
  image back, so the net infers "there is probably an edge here" from bag
  bounding boxes and baked texture statistics rather than seeing the edge. It is
  the honest trade for not stalling the GS pipeline once per frame.
- **Reprojection is per grid vertex, not per pixel.** 255 reprojected UVs, one
  representative depth per tile. Disocclusion inside a tile ghosts; the net
  learns to distrust history where `depthGrad` is high, which mitigates it
  rather than fixing it.
- **No per-scene overrides and no flow-graph control** — project-wide, baked at
  build time, like [custom screen effects](custom-screen-effects.md).
- **FIXED — it can be trained on your project now**, and it turns out to matter
  more than anything else on this list: `--blss-train <projectDir>` walks the
  project's own scenes, and a bestiary-trained net measured **worse than doing
  nothing** on a real project. See
  [Training on your own project](#training-on-your-own-project). **The window
  does it too, and defaults to it** — the corpus switch is the first control in
  the header and starts on this project's scenes, with `--all-shots` on.
- **The labels do not see the real history.** The true history is the previous
  frame's composite, which depends on the previous frame's weights, which depend
  on the net being trained — unrolling that is out of scope for a PoC. Label
  generation stands in the previous low-res render upscaled (what all-zero
  weights would produce); `--blss-eval` then closes the loop and feeds each frame
  the genuine previous composite, so the *reported* numbers are recurrent even
  though the labels are not.
- **FIXED — env maps, camera feeds and projected shadows used to switch it off
  mid-frame.** This entry used to be the most important thing on the list: those
  brackets live *inside* `renderScene()` and restored the display buffer
  unconditionally, so the first one cancelled the redirect and everything after
  it drew full-resolution into the display buffer, where the composite's opaque
  base pass painted over it — silently, with no signature beyond "BLSS did
  nothing". Reflective materials using the dynamic sky, "show in reflections"
  objects, `envProbeReflected`, camera feeds and every `projShadow` caster were
  all affected. They restore the previous **`RasterTarget`** now and nest
  correctly; see
  [the bracket is explicit state now](#the-bracket-is-explicit-state-now-and-that-is-why-the-others-nest),
  which also records why the fix this page used to prescribe — "just make
  `getCurrentFrameBuffer()` return the BLSS target" — was insufficient and would
  have produced a differently broken frame. Verified in PCSX2 on a fixture with
  three `projShadow` casters: before, no projected shadow was drawn at all;
  after, the shadows are there and visibly soft-edged, i.e. produced inside the
  low-res target and reconstructed by the composite.
- **Still incompatible with depth of field, portals and split-screen**, and the
  z-buffer shrink made the first two *harder*, not easier: the depth the display
  resolution wants is not merely unfilled now, it is not allocated.
  - **Depth of field** composites its blur through the GS depth test at display
    resolution (`RendererCorePostFx`'s three z-tested layers), after the
    composite. Making it work would mean upscaling depth, which the GS cannot do
    in a blend pass.
  - **Portals** want real display-resolution depth. They used to fail a second
    way as well — `RendererCorePostFx::portalMaskBegin/End` were the *fourth*
    copy of the raster-restore bug, taking `FRAME` from
    `gs->getCurrentFrameBuffer()` and writing a display-sized `SCISSOR` and
    `XYOFFSET` from inside `renderScene()`, so a portal cancelled the redirect
    exactly the way the env map used to. **Converted** (`332f3193`): both now
    read `getRasterTarget()` and put the raster back with `emitRasterRestore()`,
    and the bracket finally carries the `InterlacedField` per-field `XYOFFSET`
    bias it never had. `Begin` re-narrows the scissor to the portal's bbox after
    the restore. That leaves the depth half, which is why the pair is still
    refused.
  - **Split-screen** is never bracketed at all — codegen wraps only the
    single-view branch — and with BLSS on the engine masks scene depth writes
    outside the bracket, so a split frame would render full-resolution into a
    depth buffer that is both smaller than the display and never written.

  **The build refuses the combination now.** `blssClashes()` + `blssInterlock()`
  in `src/templates.cpp` put a comment block, one `#error` headline and one
  `#error` per clashing feature into `inc/scene_data.hpp` — naming the feature,
  the scene it is in and what to do about it — so nothing that produces an ELF
  gets past it: not the editor's build button, not `docker compose` + `make` by
  hand, not a CI job. `generate()` prints the same lines on the host as
  `[blss] BUILD WILL BE REFUSED: ...`, so `--refresh-gen` reports it and
  `--build` says it before Docker starts. It refuses rather than auto-disabling
  the upscaler: a build that quietly measured a different configuration than the
  project describes is the worst outcome available for a feature whose whole
  point is being measured.

  **Earlier drafts of this page and of the math doc said the generated game
  "does not emit them together". That was never true until `332f3193`** — for
  three commits the preferences warning was the whole guard, and a user who went
  past it got an ELF that compiled, booted and drew the wrong picture.

  Each interlock condition mirrors what the generated game will actually do, not
  the coarser question "does the project mention the feature", so it refuses no
  project that would have worked — and the preferences dialog now asks exactly
  the same four questions:

  - depth of field per **scene** (a scene can override the whole post-fx group),
    quantised the way `POSTFX_DOFS` is and gated on a non-zero focus distance,
    because `applyPostFx` runs the pass only for `dof > 0 && dofFocus > 0`;
  - **the `Set Depth Of Field` flow node** counts too. It raises DoF at runtime
    in a project whose authored amount is 0 everywhere, so the picture breaks
    with nothing in the project file to show for it. Neither the dialog nor any
    doc mentioned this until now;
  - a **portal** clashes only when its target resolves to another `Portal` in the
    same scene — `renderPortalView` skips `target < 0`, so an unlinked portal is
    a tinted surface;
  - **split screen** needs the preference *and* a scene with a second `Player`
    object, because `PLAYER2_INDEXES` is what gates the split branch. A project
    set to split with no player two anywhere never renders a split frame, and the
    dialog used to warn about it anyway.
- **The composite is not free, and it is now quantified.** The worst case is five
  full-screen passes, which is more fill than the half-res render saved — so the
  sparsity culling is a requirement, not an optimisation. The shipped net measures
  **1.67 passes in distribution and 1.87 out of it** (`--blss-eval`'s `passes`
  column; **1.80 with sd 0.30** over 39 cross-validation fold-runs) against 1.00
  for plain bilinear, and the oracle reaches better quality at 1.36 — so roughly
  **half a pass of the remaining fill is the network failing to generalise the
  cost model**, not a floor.
  Everything here is a pass count. Whether it is a *win* on a given scene is a
  millisecond question, and **no frame timing has ever been measured**, in the
  emulator or on hardware. Measure before shipping it on a scene that was never
  fill-bound in the first place.

## Reference

- Host side: `src/blss.hpp` / `src/blss.cpp` (features, MLP, oracle, trainer,
  emitter), `src/blsscorpus.{hpp,cpp}` (the software rasteriser that manufactures
  training frames) and `src/blssscene.{hpp,cpp}` (walking a real project into
  world-space triangles, so the corpus can be the user's own scenes). CLI in
  `src/main.cpp`: `--blss-train [<projectDir>]`, `--blss-eval [<projectDir>]`,
  `--blss-emit`. `blss.hpp` carries the measured tables for every constant it
  defines — read it before changing one. The parallel phases go through
  `parallelFor` (`blss.cpp` — the oracle, the `--cv` fold loop, and
  `evalRecurrent`'s shot runs) and `parallelFrames` (`blsscorpus.cpp`, the
  render), all bounded by `--threads` and all clamped to 32; **item *i* is
  handed to a fixed worker that may touch only item *i*, and that is what
  `--threads 1` against `--threads N` checks.** `evalRecurrent` adds one rule to
  that: its workers only ever produce **per-frame** values, and the sums are
  folded in serially in corpus order afterwards, because a parallel reduction
  would have moved the table by a ULP or two and a table that moves with the core
  count is worthless.
- Engine side: `vendor/tyra/engine/{inc,src}/renderer/core/blss/`
  (`RendererCoreBlss`), reached as `engine->renderer.core.blss`. The proxy
  producers are `addBagBox()` / `addBagSphere()`, fed from `StaPipCore` one box
  per VU1 package and gated on `PipelineInfoBag::blssProxy`; the instrument is
  `logFeatureSpread()` under debug view 2. The raster scale
  it needs is `RendererSettings::setRasterScale` / `getRasterWidthF/HeightF`
  (`getRasterWidthUI/HeightUI` is what the z buffer is sized from), and the
  history tap is `RendererCoreGS::getPreviousFrameBuffer()`.
- The raster bracket every redirect in the engine shares:
  `RendererCoreGS::RasterTarget` + `getRasterTarget()` / `redirectRasterTo()` /
  `endRasterRedirect()` / `emitRasterRestore()`. `getCurrentFrameBuffer()`
  deliberately still means **the display buffer**. The permanent-VRAM relayout
  `configure()` triggers is `RendererCore::rebuildPermanentBuffers()`, gated on
  `RendererCoreGS::needsBufferRealloc()`. `RendererCore3D::isForeignViewActive()`
  keeps env/portal re-submissions out of BLSS' screen-space feature grid.
- Project fields: `blssEnabled` / `blssScale` / `blssSharpen` / `blssTemporal` /
  `blssJitter` / `blssDebugView`, loose on `ProjectSettings` (`src/project.hpp`),
  serialised in `src/project.cpp`, format versions 4 and 5 (both additive, no
  migration step). `blssJitter` has **no UI control yet** — it is set by hand in
  the `.tyra`.
- Frame timing: the counters and the `FRAMETIME` line are
  `vendor/tyra/engine/inc/debug/frame_profile.hpp` (compiled out by default) plus
  the `ftrig` block in the generated `drawDebugHud`. The protocol, the PCSX2
  fill-rate calibration gate and the measured hardware A/B are in
  [profiling.md](profiling.md#timing-a-frame-that-blss-is-in).
- UI: `src/blss_window.cpp` (*Tools ▸ Neural Upscaler (BLSS)*) and
  `src/blss_ui.{hpp,cpp}` (the job that runs the editor's own CLI, and the parsers
  that read its tables back — host-only, no ImGui, no `App`, so they stay testable
  from a harness). The window **runs `tyrax-editor --blss-<verb>` as a subprocess
  and parses stdout**; do not give it a second implementation of anything in
  `blss.cpp`. `drawBlssSettings` / `blssClashesFor` / `drawBlssClashWarning` live
  there too and are drawn by **both** the window and `App::drawPreferencesModal`,
  so the conflict warning is one mirror of `blssClashes()`; edit the two together
  or the editor drifts away from the build again. The corpus switch is
  `App::blssCorpusProject_` + `blssCommonArgs()` (one switch, five verbs, the
  project directory made absolute); the Evaluate verdict is
  `App::drawBlssVerdict()` over `blssui::summarise()` — **the arithmetic belongs
  in `blss_ui.cpp`, not in the draw call**, for the same reason the parsers do.
- Code generation: `blssInclude` / `blssInit` / `blssSceneRender` /
  `blssNetHeader` in `src/templates.cpp`, reaching both the orbit and FPP
  templates through the `{{BLSS_INCLUDE}}` / `{{BLSS_INIT}}` /
  `{{BLSS_SCENE_RENDER}}` placeholders, plus **`blssClashes()` /
  `blssInterlock()`** — the build-time refusal, which lands in
  `inc/scene_data.hpp` (not the game cpp: that file carries the ownership marker
  and stops regenerating the moment somebody takes it). The trained network is
  baked as `inc/blss_net.gen.hpp`. **Everything is emitted only when the feature is on** —
  a project with BLSS off regenerates byte-identically, which is verified by
  A/B-generating an example against a binary built without the wiring.
- Related: [the reconstruction math](blss-reconstruction.md),
  [GS VRAM residency](gs-vram.md),
  [custom screen effects](custom-screen-effects.md).

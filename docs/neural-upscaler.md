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

> Status: **proof of concept**, off by default. The network is real, trained and
> measured on the host, where it beats every fixed kernel in distribution — and
> **on shots it was not trained on it is about level with a plain bilinear
> upscale** (≈ +0.1 dB averaged over four training seeds, one of the four
> *below* bilinear; see [the seed table](#how-to-read-those-two-tables-and-what-not-to-read-into-them)).
> The
> last time a human watched it in PCSX2 **the picture visibly oscillated**; the
> objective has since gained a fill term that culls the two passes which alternate
> with the jitter, and the host's flicker metric improved with it — but **nobody
> has re-watched the emulator since that change, so the oscillation is neither
> confirmed fixed nor confirmed present.** See
> [The oscillation](#the-oscillation). Do not enable this yet.

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
        texel density, coverage, luma, history age
                                    v
              MLP  8 -> 12 -> 3   (per tile, 224 tiles)
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
| `luma` | mean material brightness × light contribution of the bags in the tile |
| `histAge` | how many frames this tile has been temporally stable — the one recurrent input |

A bag's screen bbox comes from the world bounding sphere the renderer *already*
computes for the dynamic-light pick, so the feature pass costs one projection per
bag and one pass over 224 tiles. Nothing is read back, nothing stalls.

The obvious upgrade is to have the editor **bake** real high-frequency energy per
material at build time — it owns the content, so it can measure how much detail a
texture actually carries, which is a feature DLSS cannot have. That is a
follow-up; the texel-density proxy is what ships here.

### 3. The network

A plain MLP, **8 → 12 → 3**, tanh hidden layer, sigmoid outputs — 147 weights and
**132 MACs per tile**, so ~29 600 MACs plus ~3 400 transcendentals for a whole
512×448 frame's 224 tiles. It runs on the EE FPU in the frame's setup phase.
There is no VU1 microcode involved and no micro memory spent: the clip program
family has none left (see the engine skill), and this net is far too small to be
worth a DMA round trip.

> Two caveats on that number. It is recomputed here from the topology
> (`12×8 + 3×12` per tile × `16×14` tiles); `src/blss.hpp`'s comment says "1 812
> MACs for the whole frame", which does not follow from 8 → 12 → 3 over 224 tiles
> and appears to be an arithmetic slip. And **neither figure has been timed**:
> nothing has profiled the EE cost of the inference pass, in PCSX2 or on
> hardware. "Far too small to matter" is a design argument, not a measurement.

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
measures **2.99 in distribution and 3.29 out of it** (`--blss-eval`'s `passes`
column, numbers below).

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

## Training

The network is trained **on the host, headless, by the editor itself** — no
Python, no external framework, no GPU:

```bash
tyrax-editor --blss-train            # train on the built-in corpus, write blss.net
tyrax-editor --blss-eval             # the table below: PSNR, flicker, occupancy
```

Both take `--frames N`, `--assets <dir>`, `--seed N`, `--sharpen K`,
`--scale-1x2`, and the two weights of the oracle's objective — `--flicker-weight`
and `--fill-weight`. **Sweep those two as a pair**, because they trade against
each other and against sharpness; the shipped defaults (`0` and `6`) are the
result of one such sweep, recorded with its numbers in `src/blss.hpp`. Changing
either changes the *labels*, so a change is only meaningful after a re-train:

```bash
tyrax-editor --blss-train --frames 84 --fill-weight 4 -o try.net
tyrax-editor --blss-eval  --frames 84 --fill-weight 4 -i try.net
```

`--blss-train` runs a self-contained software rasteriser (`src/blsscorpus.cpp`)
over a **procedural corpus** of seven shots — the cases that actually alias on a
PS2, each with its own camera move:

| Shot | What aliases | Camera |
|---|---|---|
| `floor-horizon` | checkerboard running to the horizon, sampled nearest | dolly in |
| `boxes-sphere` | hard box silhouettes plus a faceted low-poly ball | orbit |
| `poles` | posts about one pixel wide — sub-pixel geometry | pan |
| `foliage` | alpha-cutout leaf quads | static (jitter only) |
| `grazing-wall` | high-frequency textures at a grazing angle | dolly along |
| `flat` | nothing: a large untextured area the net must leave alone | slow pan |
| `whip` | all of the above, swept ~150° | whip pan (reprojection is hopeless) |

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
| the fill the candidate would make the GS draw | `--fill-weight`, **default 6** | sparsity, and stability as a side effect |

The fill term is charged as a **step on the quantised alpha byte**, not as a
smooth function of the weight, because the byte is what the engine's skip test
reads: a weight that rounds to alpha 1 costs a whole pass and buys nothing, and a
smooth penalty would park the oracle exactly there. Sharpen counts twice (passes
4 and 5 are always drawn together) and the bilinear base is free (always drawn).

Then it fits the MLP to the oracle weights (Adam, MSE, per-tile loss weighted by
how much the choice actually changes that tile — tiles where every kernel is
equally good do not get a vote). Held-out **shots** are what `--blss-eval`
reports — 2 of the 7, striding the bestiary rather than taking the tail.

`--blss-emit` bakes the trained weights into the C++ the game compiles — the
generated project carries them as **`inc/blss_net.gen.hpp`** (codegen writes it;
`--blss-emit -o <file>` writes the same body by hand, and with no `-o` prints it
to stdout). The network is 147 floats, so it is a header, not an asset.

## Using it

*Project Settings ▸ Rendering ▸ Neural upscaler (BLSS)* — off by default.

| Setting | Meaning |
|---|---|
| **Enabled** | render the 3D scene at reduced resolution and reconstruct |
| **Scale** | `2×2` (quarter the pixels) or `1×2` (half-height only — cheaper reconstruction, keeps horizontal detail) |
| **Sharpen strength** | the `k` of passes 4/5; the net decides *where*, this decides *how much* |
| **Temporal** | allow the history pass at all (off = spatial-only, no ghosting, no AA) |
| **Debug view** | tint the frame by the winning kernel per tile (red = point, green = temporal, blue = sharpen) |

Ticking **Enabled** also puts two notes under the checkbox, and both are there
because a user should not have to discover them from a broken build: the standing
one says the feature is a proof of concept that measures about level with plain
bilinear on unseen content and has never been timed on hardware, and the
conditional one lists whichever of **depth of field / portals / split-screen**
*this project actually uses* — the pattern is "warn only about the conflict you
really have", checked against the staged project settings plus every scene's
resolved post-fx group and object list. Reflections, camera feeds and projected
shadows are **not** on that list any more; they nest correctly now.

## Measured

```bash
tyrax-editor --blss-train --frames 84 --assets examples
tyrax-editor --blss-eval  --frames 84 --assets examples
```

84 corpus frames over 7 shots, 512×448 output from a 256×224 render, shipped
defaults throughout (`--epochs 400`, `--seed 0xB1557`, `--flicker-weight 0`,
`--fill-weight 6`). Three families of column, and each exists because a bug got
past the ones before it:

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

Training shots (60 frames — `floor-horizon`, `poles`, `foliage`, `flat`, `whip`):

| | PSNR | flicker | point | temp | sharp | passes |
|---|---|---|---|---|---|---|
| native full-res, 1 sample | 30.56 | 21.98 | — | — | — | — |
| half-res + point | 25.57 | 24.80 | 100 % | 0 % | 0 % | 2.00 |
| half-res + bilinear | 28.45 | 23.41 | 0 % | 0 % | 0 % | 1.00 |
| half-res + temporal | 25.69 | 12.35 | 0 % | 100 % | 0 % | 2.00 |
| half-res + sharpen | 26.45 | 25.30 | 0 % | 0 % | 100 % | 3.00 |
| **half-res + BLSS (trained)** | **29.54** | **21.15** | 59.1 % | 81.3 % | 29.3 % | **2.99** |
| half-res + oracle weights | 30.16 | 19.79 | 19.7 % | 53.9 % | 10.0 % | 1.94 |

Held-out shots (24 frames — `boxes-sphere` orbit, `grazing-wall` dolly):

| | PSNR | flicker | point | temp | sharp | passes |
|---|---|---|---|---|---|---|
| native full-res, 1 sample | 24.80 | 29.96 | — | — | — | — |
| half-res + point | 20.57 | 30.54 | 100 % | 0 % | 0 % | 2.00 |
| half-res + bilinear | 23.26 | 28.40 | 0 % | 0 % | 0 % | 1.00 |
| half-res + temporal | 15.69 | 21.38 | 0 % | 100 % | 0 % | 2.00 |
| half-res + sharpen | 21.37 | 33.46 | 0 % | 0 % | 100 % | 3.00 |
| **half-res + BLSS (trained)** | **23.42** | **26.80** | 95.7 % | 98.6 % | 17.1 % | **3.29** |
| half-res + oracle weights | 24.10 | 25.57 | 10.0 % | 39.9 % | 1.5 % | 1.53 |

### How to read those two tables, and what NOT to read into them

- **In distribution the win is solid: +1.10 dB over the best fixed kernel**, at
  less flicker than a full-resolution native render (21.15 against 21.98), which
  is exactly what the temporal pass is for.
- **Out of distribution, do not quote a decibel figure.** The held-out row is
  +0.16 dB over bilinear here, and **that is inside the noise**: the split is
  *2 shots out of 7*, and held-out PSNR moves **±0.4 dB on the seed alone**.
  Re-running the whole `--blss-train` + `--blss-eval` cycle at four different
  `--seed` values, changing nothing else:

  | `--seed` | held-out, BLSS − bilinear | training, BLSS − bilinear | mean passes (train / held) |
  |---|---|---|---|
  | `0xB1557` (default) | **+0.16** | +1.10 | 2.99 / 3.29 |
  | `0x1111` | **+0.26** | +1.03 | 3.60 / 4.27 |
  | `0x2222` | **−0.23** | +1.09 | 3.47 / 3.73 |
  | `0x3333` | **+0.22** | +1.15 | 3.40 / 3.72 |
  | **mean** | **+0.10** | **+1.09** | |

  So **one seed in four has BLSS losing to plain bilinear out of distribution**,
  and the honest claim is "**about parity with bilinear, ≈+0.1 dB**", not any
  single run's number. In distribution the same four runs are tight (+1.03 to
  +1.15) — *that* win is real. Earlier drafts of this page quoted "+0.18 dB" and
  "+0.24 dB" out of distribution from single runs; both were noise, and repeating
  that mistake has cost a debugging session every time. The same caveat applies
  to every held-out number on this page and to `src/blss.hpp`'s sweep tables.
  (`--seed` sets the corpus seed *and* the trainer's init; the bilinear row moves
  only 0.04 dB across the four corpora, so essentially all of the spread is the
  network, not the data.)
- **Sparsity is only half-working, and the occupancy columns are what say so.**
  Sharpen is genuinely culled (100 % → 29.3 % / 17.1 %), which is what the fill
  term bought. **Point and temporal are not**: 59.1 % / 81.3 % in distribution and
  95.7 % / 98.6 % out of it, i.e. out of distribution the net asks for both over
  essentially the whole screen. The oracle shows the headroom — it reaches a
  better PSNR at **1.53–1.94 passes** where the net spends 2.99–3.29 — so this is
  the network failing to generalise the cost model, not the cost model being
  wrong. Anything on this page or in the math doc that says passes 2..5 "cover a
  minority of the screen" is describing the sharpen pass only.
- **`native` is not a ceiling** — the reference is supersampled, so a good
  temporal reconstruction can in principle beat a 1-sample full-resolution
  render, and on the training half it nearly does.
- **The oracle row is the regression test.** A parity break between the host twin
  and the engine shows up as the trained row falling well below the oracle row.

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
about parity with it — which is where it still is, and where the seed table above
says it honestly sits.

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

- **It predates the fill term.** Every console figure on this page was taken from
  a net trained by the old fill-blind objective, which asked for four to five
  full-screen passes. Nothing has been booted since the retune, so the emulator
  half of this feature is **stale, not wrong**.
- **The bob it describes was later re-measured and re-explained** — the
  interlacing story in the paragraph that used to sit here was refuted. See
  [The oscillation](#the-oscillation).
- **Frame timings and real hardware are still not measured at all.** No profiling
  pass has been run in the emulator and nothing has ever run on a physical PS2, so
  every performance statement on this page — including the pass counts, which are
  *fill* and not *milliseconds* — is arithmetic and host measurement, never a
  stopwatch on the console.
- **Its VRAM line predates the z-buffer shrink.** The residency numbers that are
  current are the `VRAMSTAT` table in [§5 VRAM](#5-vram), taken on a later boot
  of the same kind of fixture.

### The oscillation

**The picture shook, and it is not the interlacing.** Everything in this
subsection was measured on a scratch project with a static camera, **before the
fill term** — it is why the objective changed, not a description of what the
current build does on a television. Nobody has looked since. In this order:

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

### "Measured is not optimised", four times

**This is the most useful thing on this page.** The same mistake was made four
times, each time one level further up, and each time it cost a debugging session:
*anything absent from the objective does not exist for the network.*

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

### What was tried for (3), and what it cost

The fix this page used to prescribe — **score the oracle over a PAIR of
consecutive frames with a penalty on the difference** — has now been implemented,
swept and **measured to be a bad trade.** It is `--flicker-weight`, and it ships
at **0**. Swept jointly with the fill weight (84 frames, 600 epochs, 3–6 training
seeds per point), at fill 6:

| `--flicker-weight` | 0.00 | 0.02 | 0.05 | 0.15 |
|---|---|---|---|---|
| held-out PSNR | 23.38 | 23.16 | 22.90 | 22.37 |
| training flicker | 21.01 | 20.86 | 20.80 | 20.20 |

(bilinear is 23.26 dB / 23.41 flicker.) Every non-zero setting pays **0.2–1.0 dB
of out-of-distribution quality for a few percent of flicker**, and 0.02 upwards
already scores *below plain bilinear* out of distribution.

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
14 % out of it, at **no cost in distribution and a small gain out of it**. The
fill weight's own sweep, at flicker 0:

| `--fill-weight` | 0 | 2 | 4 | **6** | 7.5 | 9 | 12 | 24 |
|---|---|---|---|---|---|---|---|---|
| held-out PSNR | 23.26 | 23.44 | 23.44 | **23.38** | 22.85 | 22.69 | 22.45 | 22.86 |
| mean passes | 4.25 | 3.99 | 3.84 | **3.39** | 3.29 | 2.81 | 2.58 | 2.43 |

The knee is sharp: up to 6 the quality is flat and the fill comes down; past it
the network stops generalising — 7.5 costs half a decibel out of distribution and
buys a tenth of a pass, and 12 is a full decibel *below* plain bilinear. Read the
held-out row of that table with the ±0.4 dB seed caveat in mind: what it
establishes is the *shape* (flat, then a cliff), not any individual cell.

### What is still open

- **Nobody has watched the emulator since the fill term landed.** The host says
  the picture is more stable; whether the bob is gone from a television is
  **unverified**. The honest workaround until someone looks is unchanged: run with
  the jitter disabled, which gives up the temporal supersampling and keeps only
  the spatial kernel selection.
- **Occupancy is seed-sensitive too**, and more so than PSNR. Over the four seeds
  in the table above the mean passes span **2.99–3.60 in distribution and
  3.29–4.27 out of it**, almost all of it in the sharpen channel (held-out sharpen
  occupancy ran 17 %, 39 %, 39 %, 67 %) while PSNR barely moved. So "≈3 passes" is
  the right order of magnitude and the wrong number to put in a fill budget — and
  a retrain is entitled to cost you a whole extra pass.

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
- **Trained on a procedural corpus, not on your project.** Training against the
  actual project's geometry is the obvious next step (`--blss-train
  <projectDir>`), and the corpus generator is already structured for it.
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
  - **Portals** want real display-resolution depth *and* have the fourth copy of
    the bracket bug: `RendererCorePostFx::portalMaskBegin/End` still take `FRAME`
    from `gs->getCurrentFrameBuffer()` and write a display-sized `SCISSOR` and
    `XYOFFSET`, from inside `renderScene()`. They were not converted in the pass
    that fixed the other three, so a portal cancels the redirect exactly the way
    the env map used to. Converting them to `emitRasterRestore()` would fix the
    *redirect* half and leave the depth half.
  - **Split-screen** is never bracketed at all — codegen wraps only the
    single-view branch — and with BLSS on the engine masks scene depth writes
    outside the bracket, so a split frame would render full-resolution into a
    depth buffer that is both smaller than the display and never written.

  **The editor warns; codegen does not refuse.** Nothing in `src/templates.cpp`
  gates DoF, portals or split-screen on `blssEnabled` — earlier drafts of this
  page and of the math doc said the generated game "does not emit them together",
  and that was never true. The warning in *Project Settings ▸ Rendering* is the
  only thing standing between the two.
- **The composite is not free, and it is now quantified.** The worst case is five
  full-screen passes, which is more fill than the half-res render saved — so the
  sparsity culling is a requirement, not an optimisation. The shipped net measures
  **2.99 passes in distribution and 3.29 out of it** (`--blss-eval`'s `passes`
  column, ±0.5 on the training seed) against 1.00 for plain bilinear, and the
  oracle reaches better quality at 1.53–1.94 — so roughly **half the remaining
  fill is the network failing to generalise the cost model**, not a floor.
  Everything here is a pass count. Whether it is a *win* on a given scene is a
  millisecond question, and **no frame timing has ever been measured**, in the
  emulator or on hardware. Measure before shipping it on a scene that was never
  fill-bound in the first place.

## Reference

- Host side: `src/blss.hpp` / `src/blss.cpp` (features, MLP, experts, oracle,
  trainer, emitter) and `src/blsscorpus.{hpp,cpp}` (the software rasteriser that
  manufactures training frames). CLI in `src/main.cpp`: `--blss-train`,
  `--blss-eval`, `--blss-emit`.
- Engine side: `vendor/tyra/engine/{inc,src}/renderer/core/blss/`
  (`RendererCoreBlss`), reached as `engine->renderer.core.blss`. The raster scale
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
  `blssDebugView`, loose on `ProjectSettings` (`src/project.hpp`), serialised in
  `src/project.cpp`, format version 4 (additive, no migration step).
- UI: the *Neural upscaler (BLSS)* block of `App::drawPreferencesModal`
  (`src/app.cpp`).
- Code generation: `blssInclude` / `blssInit` / `blssSceneRender` /
  `blssNetHeader` in `src/templates.cpp`, reaching both the orbit and FPP
  templates through the `{{BLSS_INCLUDE}}` / `{{BLSS_INIT}}` /
  `{{BLSS_SCENE_RENDER}}` placeholders. The trained network is baked as
  `inc/blss_net.gen.hpp`. **Everything is emitted only when the feature is on** —
  a project with BLSS off regenerates byte-identically, which is verified by
  A/B-generating an example against a binary built without the wiring.
- Related: [the reconstruction math](blss-reconstruction.md),
  [GS VRAM residency](gs-vram.md),
  [custom screen effects](custom-screen-effects.md).

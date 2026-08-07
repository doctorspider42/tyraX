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
> measured on the host, where it beats every fixed kernel in distribution —
> **and, on content it was never trained on, beats a plain bilinear upscale by
> +0.40 dB** (leave-one-shot-out cross-validation over 13 shots × 3 seeds = 39
> fold-runs, sd 0.40, 5 of 39 below bilinear; **+0.23 dB** over the six folds
> that took no part in choosing the defaults). See
> [Measured](#the-out-of-distribution-number-and-how-to-get-one-that-means-something).
>
> **This page said "≈+0.1 dB, statistically a draw" until the measurement was
> done properly, and that was wrong in the pessimistic direction** — the ±0.4 dB
> it blamed on the training seed turned out to be which *shot* you held out. The
> methodology is [the fifth entry](#measured-is-not-optimised-five-times) and it
> is the most transferable thing here.
>
> What is still not known: the last time a human watched it in PCSX2 **the
> picture visibly oscillated**; the objective has since gained a fill term that
> culls the two passes which alternate with the jitter, and the host's flicker
> metric improved with it — but **nobody has re-watched the emulator since that
> change, so the oscillation is neither confirmed fixed nor confirmed present**
> (see [The oscillation](#the-oscillation)) — and **no BLSS frame has ever been
> timed**, in the emulator or on hardware. Do not enable this yet.

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
measures **2.85** on held-out content (`--blss-eval`'s `passes` column, sd 0.61
over 39 fold-runs — [numbers below](#the-out-of-distribution-number-and-how-to-get-one-that-means-something)).
That is a *fill* count, not a millisecond: **nothing here has ever been timed.**

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
`--scale-1x2`, `--weight-decay W`, `--standardise`, and the two weights of the
oracle's objective — `--flicker-weight` and `--fill-weight`. **Sweep those two as
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
(cross-validation, [below](#measured)) and `--features` (what the eight input
channels look like over the corpus, and how each correlates with the oracle).

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
to stdout). The network is 147 floats, so it is a header, not an asset.

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

*Project ▸ Preferences ▸ Neural upscaler (BLSS)* — off by default.

| Setting | Meaning |
|---|---|
| **Enabled** | render the 3D scene at reduced resolution and reconstruct |
| **Scale** | `2×2` (quarter the pixels) or `1×2` (half-height only — cheaper reconstruction, keeps horizontal detail) |
| **Sharpen strength** | the `k` of passes 4/5; the net decides *where*, this decides *how much* |
| **Temporal** | allow the history pass at all (off = spatial-only, no ghosting, no AA) |
| **Debug view** | tint the frame by the winning kernel per tile (red = point, green = temporal, blue = sharpen) |

Ticking **Enabled** also puts two notes under the checkbox, and both are there
because a user should not have to discover them from a broken build.

The **standing** one says the feature is a proof of concept that beats bilinear
by +0.40 dB on unseen content (13-shot cross-validation, 39 fold-runs, 5 of them
below bilinear) and has never been timed on console or hardware.

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
[the fifth entry](#measured-is-not-optimised-five-times)).

13 shots × 3 seeds = **39 fold-runs**, 156 frames, 512×448 from 256×224, shipped
defaults (`--epochs 400`, `--seed 0xB1557`, `--flicker-weight 0`,
`--fill-weight 16`, weight decay `1e-4`, raw inputs):

| held-out shot | seed `B1557` | seed `CCD704ED` | seed `8814F396` | mean | sd |
|---|---|---|---|---|---|
| 0 `floor-horizon` dolly-in | +0.30 | +0.35 | +0.40 | **+0.35** | 0.04 |
| 1 `boxes-sphere` orbit | +0.64 | +0.70 | +0.69 | **+0.68** | 0.02 |
| 2 `poles` pan | +1.05 | +0.97 | +1.04 | **+1.02** | 0.04 |
| 3 `foliage` static | +0.64 | +0.76 | +0.47 | **+0.62** | 0.12 |
| 4 `grazing-wall` dolly-along | −0.07 | −0.04 | +0.13 | **+0.01** | 0.09 |
| 5 `flat` slow pan | +0.97 | +0.70 | +1.04 | **+0.90** | 0.14 |
| 6 `whip` whip pan | +0.29 | +0.27 | +0.20 | **+0.25** | 0.04 |
| 7 `corridor` dolly-down | −0.67 | −0.39 | −0.52 | **−0.52** | 0.12 |
| 8 `strafe-field` lateral | +0.59 | +0.52 | +0.61 | **+0.57** | 0.04 |
| 9 `pitch-sky` pitch-up | +0.78 | +0.73 | +0.35 | **+0.62** | 0.19 |
| 10 `distant-plain` slow dolly | +0.38 | +0.36 | +0.37 | **+0.37** | 0.01 |
| 11 `sphere-field` orbit-wide | +0.16 | +0.03 | +0.18 | **+0.12** | 0.06 |
| 12 `foliage-walk` dolly-through | +0.27 | +0.21 | +0.15 | **+0.21** | 0.05 |
| **mean over folds** | **+0.41** | **+0.40** | **+0.39** | **+0.40** | **0.01** |

> **BLSS beats plain bilinear out of distribution by +0.40 dB**, sd 0.40 over 39
> fold-runs, **5 of 39 below bilinear**, at **2.85 mean full-screen passes**
> (sd 0.61) against 1.00 for bilinear.

**The conservative figure is +0.23 dB.** Shots 7–12 are the six that were added
*after* the defaults were chosen and took no part in choosing them; their fold
means are −0.52, +0.57, +0.62, +0.37, +0.12, +0.21. Quote that one when the
question is "will it help on content nobody tuned for".

Per fold, mean over the three seeds — `in-dist` is the same margin on that fold's
twelve **training** shots, i.e. the control that says the fold trained at all
(a held-out number under a collapsed `in-dist` number means nothing):

| held-out shot | native | bilinear | BLSS | oracle | passes | flicker | in-dist |
|---|---|---|---|---|---|---|---|
| 0 `floor-horizon` | 19.57 | 18.88 | 19.23 | 20.28 | 2.25 | 33.27 | +0.56 |
| 1 `boxes-sphere` | 20.26 | 18.95 | 19.62 | 19.86 | 3.03 | 44.85 | +0.53 |
| 2 `poles` | 22.11 | 21.07 | 22.09 | 23.13 | 2.63 | 28.72 | +0.52 |
| 3 `foliage` | 28.13 | 25.61 | 26.23 | 27.78 | 2.72 | 6.50 | +0.44 |
| 4 `grazing-wall` | 29.31 | 27.55 | 27.55 | 28.01 | 2.93 | 7.80 | +0.60 |
| 5 `flat` | 58.30 | 54.67 | 55.57 | 54.67 | 4.33 | 0.04 | +0.61 |
| 6 `whip` | 25.48 | 22.58 | 22.84 | 23.62 | 3.10 | 41.61 | +0.58 |
| 7 `corridor` | 31.80 | 27.42 | 26.90 | 27.59 | 3.33 | 17.57 | +0.59 |
| 8 `strafe-field` | 22.58 | 21.67 | 22.24 | 22.77 | 2.51 | 20.50 | +0.54 |
| 9 `pitch-sky` | 22.86 | 22.15 | 22.77 | 24.40 | 2.40 | 38.08 | +0.55 |
| 10 `distant-plain` | 22.71 | 23.39 | 23.76 | 24.83 | 2.33 | 12.67 | +0.55 |
| 11 `sphere-field` | 32.76 | 31.06 | 31.19 | 31.74 | 2.87 | 14.30 | +0.59 |
| 12 `foliage-walk` | 29.32 | 27.40 | 27.61 | 27.82 | 2.61 | 14.31 | +0.57 |

**The one shot it loses on is `corridor` (−0.52), and the reason is a feature the
network effectively does not have there.** `depth` is `1/w` normalised against
`kDepthRef = 8` and clamped, so it reads 1.0 for anything closer than eight
units. `--blss-eval --features` prints the per-shot mean of each channel, and
`corridor`'s `depth` is **0.972** — the highest of the thirteen, against a corpus
mean of 0.713. A corridor is a few units wide; that channel spends the shot
against its clamp. **A saturated feature is a feature the network does not
have**, so on `corridor` it is deciding from seven inputs, and it decides worse
than not deciding.

That is a *feature-scaling* problem with a name and a fix (a log or reciprocal
depth mapping), not an inherent limit — and it is not confined to one shot:
**58.8 % of all tiles in the corpus read `depth` at exactly 1.0**, and
`depthGrad` (61.6 %) and `coverage` (71.7 %) are worse. Add
`--features` to any investigation of "why does it not help here" before touching
the topology; the channel statistics are the first place to look.

`flat` is worth a second look for the opposite reason: 55.57 dB against
bilinear's 54.67 on a screen with nothing in it, at **4.33 passes**. The network
happily spends four full-screen passes buying a decibel on a picture where no
decibel is visible. That, not the mean, is where the remaining fill lives.

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
| half-res + temporal | 23.06 | 13.64 | 0 % | 100 % | 0 % | 2.00 |
| half-res + sharpen | 24.85 | 25.34 | 0 % | 0 % | 100 % | 3.00 |
| **half-res + BLSS (trained)** | **27.92** | **21.05** | 80.1 % | 81.3 % | 14.9 % | **2.91** |
| half-res + oracle weights | 28.70 | 20.01 | 7.5 % | 43.4 % | 2.4 % | 1.56 |

Held-out shots (48 frames — `boxes-sphere`, `grazing-wall`, `corridor`,
`distant-plain`):

| | PSNR | flicker | point | temp | sharp | passes |
|---|---|---|---|---|---|---|
| native full-res, 1 sample | 26.02 | 24.86 | — | — | — | — |
| half-res + point | 21.63 | 25.05 | 100 % | 0 % | 0 % | 2.00 |
| half-res + bilinear | 24.32 | 22.47 | 0 % | 0 % | 0 % | 1.00 |
| half-res + temporal | 17.21 | 15.68 | 0 % | 100 % | 0 % | 2.00 |
| half-res + sharpen | 22.34 | 26.96 | 0 % | 0 % | 100 % | 3.00 |
| **half-res + BLSS (trained)** | **24.30** | **21.03** | 90.2 % | 90.6 % | 4.8 % | **2.90** |
| half-res + oracle weights | 25.20 | 19.96 | 3.0 % | 32.7 % | 0.0 % | 1.36 |

**In distribution the win is +0.80 dB over the best fixed kernel** (27.92 against
bilinear's 27.11), at less flicker than a full-resolution native render (21.05
against 22.39) — which is what the temporal pass is for. That row has never been
the problem.

**The held-out row of that second table is −0.02 dB, and it is the best
illustration on this page of why you should not quote it.** The same net,
measured properly by holding out each shot in turn, is **+0.40 dB**. Nothing
changed but the question: this split contains `corridor`, the one shot of
thirteen the network loses on, so a quarter of its held-out frames come from the
single worst case. A single split can be wrong in either direction, and until
`--cv` existed nobody could tell which.

### How to read these tables, and what NOT to read into them

- **In distribution the win is solid** and always has been — every fold's
  `in-dist` column above sits between +0.44 and +0.61 dB against bilinear on its
  own twelve training shots, and against the best *fixed kernel* the margin on
  the shipped split is the one in the table above. That win has never been in
  question and is not what this page kept having to retract.
- **The ±0.4 dB this page used to blame on the training seed is
  SPLIT-SELECTION variance, not seed variance.** The 39-fold table separates the
  two, and they are two orders of magnitude apart:

  | source of spread | sd |
  |---|---|
  | which shot you hold out (fold to fold) | **0.40 dB** |
  | which seed you train at (per-seed fold **mean**) | **0.01 dB** |

  The per-fold sd across the three seeds runs 0.01–0.19 dB, so the seed does move
  an individual fold a little — but the *answer to the question* barely moves at
  all. Four earlier runs at four `--seed` values read −0.23 to +0.26 dB and this
  page called that seed noise. It was not. It was four draws from a distribution
  whose spread comes almost entirely from **which two shots the split happened to
  contain**, and the seed was along for the ride.
- **A single held-out split is a sample of size one, and quoting one was the
  mistake.** Earlier drafts quoted "+0.18 dB", "+0.24 dB", then "≈+0.1 dB, one
  loss in four seeds, statistically a draw". All three were one draw dressed as
  an estimate, and the last of them **understated a real +0.40 dB win**. Use
  `--cv`. The cost is a few minutes of CPU.
- **Sparsity is still only half-working, and the occupancy columns are what say
  so.** Sharpen is genuinely culled — 100 % → 14.9 % in distribution and **4.8 %**
  out of it — which is what the fill term bought. **Point and temporal are not**:
  80 % / 81 % in distribution and 90 % / 91 % out of it, i.e. the net asks for
  both over most of the screen. The oracle shows the headroom, reaching a *better*
  PSNR at **1.36–1.56 passes** where the net spends 2.90–2.91, so this is the
  network failing to generalise the cost model, not the cost model being wrong.
  Anything on this page or in the math doc that says passes 2..5 "cover a minority
  of the screen" is describing the **sharpen** pass only.
- **`native` is not a ceiling** — the reference is supersampled, so a good
  temporal reconstruction can in principle beat a 1-sample full-resolution
  render, and on several folds above it does not, while on the training side it
  nearly does.
- **The oracle column is the regression test.** A parity break between the host
  twin and the engine shows up as the BLSS column falling well below the oracle
  column. Note that on `flat` the oracle scores *below* the trained net (54.67
  against 55.57) — the oracle is optimising accuracy **plus fill**, and on an
  empty screen it correctly refuses to pay for a decibel nobody can see.

### The network is variance-limited, not optimisation-limited

**Anything that makes the fit easier makes the feature worse.** The clearest
demonstration is `--standardise`, which fixes a real defect: the eight input
channels have wildly different scales, and standardising them (mean 0, unit
variance, folded back into `w1`/`b1` so the engine still sees raw features and
the twin contract is untouched) fits the *training* shots better. Cross-validated
on the same 39 fold-runs, it generalises **worse**:

| | raw inputs (shipped) | `--standardise` |
|---|---|---|
| held-out margin over bilinear | **+0.40 dB** | +0.24 dB |
| sd over 39 fold-runs | 0.40 | 0.47 |
| folds below bilinear | 5 / 39 | **9 / 39** |
| mean passes | 2.85 | 2.94 |

It is worse on ten of the thirteen folds, turns `floor-horizon` (+0.35 → −0.20)
and `sphere-field` (+0.12 → −0.10) into losses, and makes the one existing loss
deeper (`corridor` −0.52 → −0.88). The same experiment on the old seven-shot
corpus moved +0.31 dB to −0.15 (`1b9c7a74`'s own run; that corpus no longer
exists, but the direction is the same and larger).

So the search went to **regularisation** instead, and that is where the win came
from: weight decay `1e-5` → **`1e-4`**, measured at `1b9c7a74` as worth +0.19 dB
and a whole pass. `--standardise` stays as a flag with its numbers attached, on the same principle
as `--flicker-weight`: **a knob measured and set to zero is not the same as a
knob deleted**, and the next person to notice the input scales should find this
table instead of re-running it.

The moral generalises past this feature: an 8→12→3 MLP with 147 weights fitted to
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
`1e-5` → `1e-4`, fill 6 → 16 — took it from parity to the **+0.40 dB** at the top
of this section, and cross-validation is what showed that the "parity" reading
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

- **It predates the fill term, and by now two retunes on top of that.** Every
  console figure on this page was taken from a net trained by the old fill-blind
  objective, which asked for four to five full-screen passes; the shipped net has
  since changed corpus (7 shots → 13), fill weight (6 → 16) and weight decay
  (`1e-5` → `1e-4`). A later boot exists — the interlock commit booted a BLSS
  `fpp` project to frame 360 with no assert — but that was a *does it run* check,
  not a picture measurement, and it predates the retune too. So the emulator half
  of this feature is **stale, not wrong**, and it is stale by more than it was.
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

### "Measured is not optimised", five times

**This is the most useful thing on this page.** The same mistake was made five
times, each time one level further up, and each time it cost a debugging session.
The first four are one sentence: *anything absent from the objective does not
exist for the network.* The fifth is the same sentence about the **measurement**
rather than the objective, and it is the one that produced the most wrong text.

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
   under cross-validation the sd of the per-seed *fold mean* is **0.01 dB**,
   while the sd from fold to fold is **0.40 dB**. The spread was the split.

   What that cost, in order of how wrong each one was:

   - the honest-sounding retraction — "≈+0.1 dB, one loss in four seeds,
     statistically a draw" — **understated a +0.40 dB win** and was the summary
     printed in the README, the preferences dialog, the engine skill and the
     backlog;
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
   direction and you cannot tell which*: at today's defaults the shipped split
   reads about **zero** while the 13-fold mean reads **+0.40**, because the
   split happens to contain `corridor` — the one shot of thirteen the network
   loses on. Nothing about the network changed between those two numbers. Only
   the question did.

   The rule that follows: **`--blss-eval --cv` for anything you intend to act
   on.** A plain `--blss-eval` is a fast look at one split, and its held-out
   columns are worth exactly what one draw is worth.

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
  over an empty untextured area — it draws **4.33 full-screen passes** to buy
  0.9 dB that no eye can see, while the oracle scores *below* it because the
  oracle is charged for the fill and correctly declines. That single fold is
  where the mean pass count goes, and "learn when the answer does not matter" is
  a training-weight question, not a topology one.
- **Occupancy is noisier than PSNR.** Mean passes over the 39 fold-runs is 2.85
  with sd **0.61**, i.e. a fifth of the whole budget, against a PSNR sd of 0.40 dB
  on a much smaller scale. So "≈3 passes" is the right order of magnitude and the
  wrong number to put in a fill budget — size anything that has to be *correct*
  off the 5.00 worst case.

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
  **2.91 passes in distribution and 2.90 out of it** (`--blss-eval`'s `passes`
  column; **2.85 with sd 0.61** over 39 cross-validation fold-runs) against 1.00
  for plain bilinear, and the oracle reaches better quality at 1.36–1.56 — so
  roughly **half the remaining fill is the network failing to generalise the cost
  model**, not a floor.
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
  (`src/app.cpp`). Its conflict warning **mirrors `blssClashes()`** condition for
  condition; edit the two together or the dialog drifts away from the build again.
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

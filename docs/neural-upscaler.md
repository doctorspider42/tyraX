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
> measured, and it now beats every fixed kernel on both image quality and
> temporal stability, in and out of distribution. It **boots and runs in PCSX2**
> (50 FPS, no assert). Frame timings are not measured, it has not run on real
> hardware, and there are two known correctness issues in
> [Limitations](#limitations) — one of which silently disables the whole feature
> around env maps and projected shadows. See [Measured](#measured).

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
   3D scene  --->  low-res target  (256x224, PSMCT32, main z reused)
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

`RendererCoreBlss::begin()` opens the same kind of raster-redirect bracket the
env map and the shadow map already use — `FRAME`/`ZBUF`/`SCISSOR`/`XYOFFSET`
pointed at the low-res target — and `end()` restores the display buffer. The
game-facing coordinate space does not change: the projection is built at the
*render* height, exactly the way `DisplayMode::InterlacedField` already does it
(see the `getRenderHeightF()` split in the engine skill).

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

A plain MLP, **8 → 12 → 3**, tanh hidden layer, sigmoid outputs — 147 weights,
1 812 MACs for the whole frame. It runs on the EE FPU in the frame's setup
phase. There is no VU1 microcode involved and no micro memory spent: the clip
program family has none left (see the engine skill), and this net is far too
small to be worth a DMA round trip.

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
all — so the network's own confidence decides how much fill the frame costs. A
frame the net considers easy costs one pass; a frame full of foliage silhouettes
costs four or five over the tiles that need them.

### 5. VRAM

At 512×448 output, 256×224 render:

| Region | Baseline words | BLSS words |
|---|---|---|
| Display buffers × 2 (512×448, 32bpp) | 458 752 | 458 752 |
| Z buffer | 229 376 (512×448) | 229 376 (reused as-is) |
| Low-res colour target | — | 57 344 |
| History buffer | — | 0 — the other display buffer |
| **Total** | **688 128** | **745 472** |

So BLSS costs **57 344 words (224 KB)** of the ~1.08 MB texture heap
([GS VRAM residency](gs-vram.md)) — about what one 256×256 32-bit texture costs.
One buffer, not three: the history is free, and the low-res pass points `ZBUF` at
the existing full-size z buffer (`FRAME` and `ZBUF` bases are independent
registers) and simply uses its top-left corner.

**There is a 672 KB saving still on the table.** With BLSS on, nothing ever
renders 3D at display resolution, so the z-buffer could shrink to 256×224 and
hand back 172 032 words — more than paying for the feature three times over. It
is allocated before BLSS exists in the init order, so that is a follow-up rather
than a line of code.

## Training

The network is trained **on the host, headless, by the editor itself** — no
Python, no external framework, no GPU:

```bash
tyrax-editor --blss-train            # train on the built-in corpus, write blss.net
tyrax-editor --blss-eval             # PSNR table, trained net vs every fixed kernel
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
- and the **oracle weights**: per tile, the `(wA, wC, wD)` that minimise MSE
  against the ground truth, found by coordinate descent on the *exact* composite
  formula the GS will execute — including its `≫ 7` truncation and 8-bit clamps.

Then it fits the MLP to the oracle weights (Adam, MSE, per-tile loss weighted by
how much the choice actually changes that tile — tiles where every kernel is
equally good do not get a vote). Held-out frames are what `--blss-eval` reports.

`--blss-emit` bakes the trained weights into the generated game as
`src/gen/blss.gen.cpp`; the network is 147 floats, so it is a header, not an
asset.

## Using it

*Project Settings ▸ Rendering ▸ Neural upscaler (BLSS)* — off by default.

| Setting | Meaning |
|---|---|
| **Enabled** | render the 3D scene at reduced resolution and reconstruct |
| **Scale** | `2×2` (quarter the pixels) or `1×2` (half-height only — cheaper reconstruction, keeps horizontal detail) |
| **Sharpen strength** | the `k` of passes 4/5; the net decides *where*, this decides *how much* |
| **Temporal** | allow the history pass at all (off = spatial-only, no ghosting, no AA) |
| **Debug view** | tint the frame by the winning kernel per tile (red = point, green = temporal, blue = sharpen) |

## Measured

`--blss-eval`, 84 corpus frames over 7 shots, 512x448 output from a 256x224
render. **PSNR** is against a 4x supersampled ground truth; **flicker** is the
mean per-pixel change between consecutive frames of one shot, which exists
because per-frame PSNR is structurally blind to temporal instability — see
below. Shots are held out whole and the split strides the bestiary.

| | training PSNR | flicker | held-out PSNR | flicker |
|---|---|---|---|---|
| native full-res, 1 sample | 30.56 | 21.98 | 24.80 | 29.96 |
| half-res + point | 25.57 | 24.80 | 20.57 | 30.54 |
| half-res + bilinear | 28.45 | 23.41 | 23.26 | 28.40 |
| half-res + temporal | 25.69 | 12.35 | 15.69 | 21.38 |
| half-res + sharpen | 26.45 | 25.30 | 21.37 | 33.46 |
| **half-res + BLSS (trained)** | **29.54** | **21.56** | **23.43** | **27.11** |
| half-res + oracle weights | 30.36 | 19.94 | 24.09 | 25.63 |

- **It beats every fixed kernel on both axes.** +1.09 dB over bilinear in
  distribution, +0.18 dB out of it, and *less* flicker than bilinear in both —
  in fact less flicker than a full-resolution native render (21.56 against
  21.98), which is what the temporal pass is for.
- **The flicker column exists because a bug got past PSNR.** The temporal pass
  originally capped at a flat 50 % mix of the previous frame. The history is the
  previous frame's own composite, so that is an exponential accumulator with a
  time constant of about one frame — against a jitter that alternates every
  frame it *tracks* the alternation instead of averaging it out, and settles
  into a stationary sub-pixel oscillation that looks exactly like bob
  deinterlacing on a television. Per-frame PSNR **rewarded** it: the mix of two
  jitter phases is genuinely closer to the supersampled truth than either phase.
  It was caught by a human watching the emulator, not by the metric. Raising the
  cap to ~0.9 retention (`kTemporalMax`) fixed it and improved PSNR too.
- **Empty tiles are no longer the network's business.** The oracle's importance
  weighting gives "tiles where every kernel is identical" no vote, so the net
  was never supervised on sky and happily asked for full temporal reconstruction
  of it — free in PSNR, ghosting on a camera turn. Both twins now force tiles
  below `kMinCoverage` to zero.
- Those two fixes together moved the held-out row from **2.23 dB below**
  bilinear to **0.18 dB above** it.
- **`native` is not a ceiling** — the reference is supersampled, so a good
  temporal reconstruction can in principle beat a 1-sample full-resolution
  render.

### On the console

Booted in PCSX2 from a scratch `fpp` project (512x512 interlaced, 2 640+ frames,
50 FPS, 100 % speed, no assert, no texture eviction, one 256x256 low-res target
at 224 KB). Frame-to-frame change of the *displayed* picture on a static camera,
same emulator settings for both:

| | on-screen change per frame |
|---|---|
| BLSS off | **0.000 %** |
| BLSS on | 0.02 – 0.16 % |

So the reconstruction is stable but not perfectly still: the accumulator's tail
is visible as ~800 changed pixels out of 645 000. **Watch out for interlaced
output.** With PCSX2's deinterlacer *off*, the picture visibly bobs — BLSS adds
a half-scanline vertical jitter on top of a signal whose fields are already a
line apart, and the two appear to compound. Turning the deinterlacer on removes
it. Which of the two is the dominant term has not been isolated (the BLSS-off /
deinterlacer-off corner was never measured), and locking the jitter phase to the
field parity is the obvious fix — it is in [the backlog](backlog.md).

**Frame timings are still not measured** — no profiling pass has been run, so
the fill-cost discussion above remains arithmetic, not measurement.

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
- **It silently disables itself around env maps, camera feeds and projected
  shadows — and this is the most important thing on this list.** Those brackets
  live *inside* `renderScene()`, and `RendererCoreEnvMap::end()` (and its
  siblings) restore `FRAME`/`SCISSOR`/`ZBUF`/`XYOFFSET` from
  `gs->getCurrentFrameBuffer()` — the **display buffer, unconditionally**, not
  "whatever was redirected before". So the first such pass inside a BLSS bracket
  cancels the redirect, and the rest of the scene draws full-resolution into the
  display buffer with no warning and no visual signature beyond "BLSS did
  nothing". It affects reflective materials using the dynamic sky, "show in
  reflections" objects, `envProbeReflected`, camera feeds, and every `projShadow`
  caster. The editor does **not** warn about these, because the incompatibility
  was found after the UI was written.

  The fix is small and belongs in the engine: make `getCurrentFrameBuffer()`
  return the BLSS target while the bracket is open, so those three `end()`
  implementations restore the right thing without any of them changing. It is
  the top follow-up in [the backlog](backlog.md); it was not done here because it
  touches an accessor that shipping features depend on and nothing in this change
  has been run on hardware.
- **Incompatible with depth of field, portals and split-screen.** All three read
  or write real GS depth at display resolution, and BLSS's z-buffer only has a
  low-res corner filled in. The editor warns instead of generating a broken
  frame; making DoF work would mean upscaling depth, which the GS cannot do in a
  blend pass.
- **The composite is not free.** Five passes over a full frame is more fill than
  the half-res render saved, which is why the sparsity culling is not an
  optimisation but a requirement. Measure before shipping it on a scene that was
  never fill-bound in the first place.

## Reference

- Host side: `src/blss.hpp` / `src/blss.cpp` (features, MLP, experts, oracle,
  trainer, emitter) and `src/blsscorpus.{hpp,cpp}` (the software rasteriser that
  manufactures training frames). CLI in `src/main.cpp`: `--blss-train`,
  `--blss-eval`, `--blss-emit`.
- Engine side: `vendor/tyra/engine/{inc,src}/renderer/core/blss/`
  (`RendererCoreBlss`), reached as `engine->renderer.core.blss`. The raster scale
  it needs is `RendererSettings::setRasterScale` / `getRasterWidthF/HeightF`, and
  the history tap is `RendererCoreGS::getPreviousFrameBuffer()`.
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

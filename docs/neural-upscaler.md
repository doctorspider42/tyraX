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

> Status: **on when your scene earns it**, off by default. The feature has a
> **regime**, it has been measured on both sides of it, and the line between
> them is a number.
>
> **What it costs, on a real PS2: 5.02 ms of EE per frame.** What it buys:
> the scene rasterises at half the linear resolution, and **25.9 % of the
> scene's fill survives** — measured, not assumed (see
> [profiling.md](profiling.md), "The re-tuned demo"). So the trade is
> ~74 % of your GS fill against 5.02 ms of EE, and at the calibrated
> **2.2524 ms per megapixel of full-screen alpha-blended textured pass**
> (0.5174 ms at 512×448, 0.5896 at 512×512) break-even is
>
> > **a scene rasterising more than about 13 full-screen coverages**
> > (13.1 at 512×448, 11.4 at 512×512 — the price is per pixel, so the
> > break-even moves with the display mode).
>
> Above that line the feature is a large win. Below it, it is a straight loss of
> about 4 ms. Both halves are measured on hardware, on the same afternoon, with
> the same instrument:
>
> | fixture | overdraw | BLSS off | BLSS on | verdict |
> |---|---|---|---|---|
> | `blssrig` — terrain + six slabs | a handful of coverages | 9.42 ms | 19.25 ms | **−9.83 ms**, a pure loss |
> | `examples/upscaler-lab` — the haze demo | ~75 coverages | **52.86 ms** | **32.98 ms** | **+19.88 ms, 1.60x** |
>
> (Both rows were measured with `blssJitter` **on**, which is no longer what
> either fixture ships. The jitter moves where the half-res raster samples, not
> how much of it there is, so these timings are expected to carry over
> unchanged — but that is a prediction and the re-run is still owed. See
> [examples/upscaler-lab](../examples/upscaler-lab/README.md).)
>
> **That regime is ONE OF TWO, and the second one is most 3D scenes.** The
> break-even above is the price of the *network* — 4.60 ms of EE to decide, per
> tile, how to blow the image up. **Plain mode
> ([below](#plain-mode--the-reduced-raster-without-the-network)) buys none of
> that and keeps everything else**: the same reduced raster, the same VRAM
> saving, one bilinear composite pass, **0.52 ms** of EE and a break-even of
> **2.6 full-screen coverages at 512×448** (2.3 at 512×512). It draws the
> **byte-identical picture** to the neural mode whenever the network asks for
> nothing, which is often. Ask the Evaluate tab whether your scene has a ceiling
> before paying for a network; if it has none, the mode to turn on is the plain
> one.
>
> **The regime is heavy alpha-blended overdraw.** Haze, smoke, layered
> billboards, anything that paints the same pixels many times. Triangle count is
> not the trigger and neither is texture size: it is coverage. Run
> `--blss-eval <projectDir>` for the image-quality ceiling and read your own
> frame's fill before switching it on — a project that is EE-bound gets nothing
> here but a bill.
>
> **Ask whether your scene has a ceiling. If it has one, turn it on — a network
> already ships.** That is the one sentence on this page with a decision in it,
> and it is the second thing to replace *"fit the project you will ship, and ship
> that net"*, which was right about the net it named and wrong about the general
> case. `--blss-eval <projectDir>` answers the ceiling question in one line and
> **needs no trained network to do it** ([net-free evaluation](#training));
> **twelve of the thirty-two example projects have an oracle ceiling under
> +0.10 dB**, where no net can win anything and the question of which net to ship
> does not arise.
>
> **Ask it about a scene whose overdraw is PARTICLES and you get no answer at
> all, by design as of 2026-08-09.** The corpus renderer draws no emitters, so a
> ceiling for such a project is measured on the geometry alone — 4.4 % of the
> frame on `examples/showcase`. `--blss-eval` now prints **`NO VERDICT:`** there
> instead of a confident *"WILL NOT BENEFIT"*, and
> [says why](#the-corpus-renderer-draws-no-emitters). Use `--blss-coverage`,
> which counts the particles.
>
> **A project with no `blss.net` is built with the network the editor ships**
> ([the net that ships](#the-net-that-ships)) — fitted on seven example projects
> *and* the bestiary, embedded in the editor binary, named in the generated
> header and in the boot log. It used to be built with **random weights** behind
> a warning banner, which is not a neutral fallback: a random net is a per-tile
> blend chosen by noise. Training your own is now an optimisation, not a
> prerequisite.
>
> On a scene that *does* have a ceiling, the choice has now been measured
> properly — leave-one-**project**-out over seven projects rather than one
> anecdote ([Can one net ship for every project?](#can-one-net-ship-for-every-project)):
>
> - a net fitted to the **built-in bestiary alone** is a lottery: **−0.34 dB on
>   average over seven projects and −1.09 dB at worst.** Do not ship one;
> - a net fitted to **real projects alone** degenerates — it asks for two
>   full-screen passes of the wrong kernels, and only the inference deadzone
>   keeps it from costing 0.10 dB;
> - a net fitted to **the bestiary and real projects together** scores
>   **+0.29 dB on a project it has never seen**, against **+0.31 dB** for that
>   project's own net — a tie at fold sds of 0.37 and 0.34. **One net can
>   ship**, and one now does: `resources/blss-default.net`, fitted with
>   `--blss-train <the seven projects> bestiary --all-shots`.
> - per-project training still reaches the highest number of all (**+0.41 dB**
>   in distribution, which is what the console runs), so the retrain button
>   stays — but **the reason it stays is no longer "otherwise it hurts you"**.
>   It is worth a fraction of a decibel on a scene that has decibels to win, and
>   nothing at all on the twelve examples that have no ceiling.
>   `--blss-train <projectDir>`, or the corpus switch in the window's header,
>   which defaults to it.
>
> **The picture still bobs with `blssJitter` on**, and that is a separate axis
> from the timing. A person was shown three builds of `examples/upscaler-lab`
> differing in nothing but these flags and called them steady / **"like an
> earthquake"** / steady, for BLSS-off / jitter-on / jitter-off. `blssJitter`
> defaults to **`false`** because of it, at the cost of the temporal
> supersampling. **Every shipped example, `examples/upscaler-lab` included, now
> ships it off and is measurably still**; that example used to keep it `true` as
> "the jitter-on reference", which meant the flagship demo shook until you
> edited it. Flipping one line in its `.tyra` reproduces build **B** whenever
> the reference is wanted. See
> [A/B/C](#abc-a-human-looked-at-three-builds-2026-08-08).
>
> ### What this page believed, and why it was wrong
>
> Kept, because the errors are more instructive than the conclusion and all
> three were the *instrument*, not the arithmetic.
>
> 1. **"`drain` ≈ 0 means the frame is EE-bound, so an upscaler cannot help."**
>    This page said it for four months and the verdict *"BLSS saves nothing"*
>    rested on it entirely. It is false. When the GS falls behind, the GIF FIFO
>    fills, VIF1's DMA stalls and the EE blocks **inside the submission it is
>    already in** — which the rig charges to `submit`. `drain` only ever measured
>    the tail still in flight after the last packet, and in a saturated pipeline
>    that tail is short. `upscaler-lab` reads `drain = 0.02 ms` in **both** arms
>    of a run where BLSS makes the frame 1.6x shorter. **The only honest
>    discriminator is to change the GS load and see whether the frame gets
>    shorter** — which is what an on/off A/B is. Run it; do not predict it.
> 2. **"The generated games do not have the fill to trade."** Drawn from one
>    fixture that had no fill, and generalised to the feature. Wrong: generated
>    games fall on *both* sides of the break-even, which is why the break-even is
>    now the headline instead of a verdict.
> 3. **"BLSS wins half the scene's fill."** It wins about three quarters:
>    `blssScale 0` is `Scale::X2Y2`, half in *each* axis, so a quarter of the
>    pixels survive. The break-even computed off the wrong factor read ~22
>    coverages; it is ~13. Measured slope: 25.9 % of the fill survives.
> 4. **"Fit the project you will ship, and ship that net — otherwise it hurts
>    you."** This was the feature's training rule for two months and it rested on
>    one row of three numbers taken on `examples/procedural`: −0.40 for a
>    bestiary net, +0.06 for the project's own, against a +0.77 ceiling. **That
>    row had two samplers in it.** The ceiling was measured with the sub-pixel
>    jitter ON (re-measured today: **+0.773**) and the margins with it OFF, where
>    the same scene's ceiling is **+0.345** — so the project net's +0.06 was
>    being read against a ceiling it could never have reached, which is what made
>    it look like a poor result rather than a scene with nothing left to win. At
>    one sampler the row is −0.48 / −0.00 / **+0.345**. `generate()` now announces
>    the sampler in both directions so a table cannot be assembled this way
>    again. The *warning* half of the rule survived re-measurement — a
>    bestiary-only net really is a lottery, −0.34 dB over seven projects and
>    −1.09 at worst — but its *conclusion* did not: a corpus holding the bestiary
>    **and** real projects ties per-project training on the only example that can
>    tell them apart, so [a default net ships](#the-net-that-ships) and
>    retraining is an optimisation.
>
> Two provisional figures this page used to carry are now retired. The
> **+9.83 ms** regression was taken on a build carrying the z-mask defect
> ([the math doc, §6](blss-reconstruction.md#fixed-blss-deleted-palettised-textures--the-z-mask-was-never-on));
> it stands as the *below-break-even* datum and nothing more. And the
> **3.37x / 530 → 157 ms** headline was real but taken on a scene running at
> **1.9 FPS** — tuned against PCSX2, which under-reports GS fill by **76x**.
> A number nobody can run is not a demo; `upscaler-lab` has been re-tuned
> against hardware and the table above is the figure to quote.
>
> **The EE bill has been priced twice and cut twice.** Splitting the composite
> onto its own counters found the largest single cost was **`runNet`, the MLP** —
> newlib's `tanhf`/`expf` compute in double and the EE has no double-precision
> FPU. Four **bit-identical** cuts took the hardware bill from 7.92 to 5.95 ms,
> and the **activation table**, now enabled on *both* twins, took `net` from
> **1.93 ms to 0.79 ms** for a total of **5.02 ms**. Every millisecond off that
> number lowers the break-even by roughly two full-screen coverages. Full tables
> in [profiling.md](profiling.md).

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
the cost are the same number **by construction**, at every output size.

**The general form, since the raster scale is now sweepable** (`--scale WxH`,
[below](#below-half-resolution-swept)). Write `A = scaleX · scaleY` for the area
divisor; both buffers are `outW·outH/A` words, so

> **net words returned = outW · outH · (1 − 2/A)**

— zero at `A = 2` (`1×2`), and everything above it is profit. The memory axis is
the one that does *not* have diminishing returns:

| output | scale | area | z returned | low-res target | **net** |
|---|---|---|---|---|---|
| 512×448 | **1×2** | 1/2 | 448 KB | 448 KB | **0** |
| 512×448 | **2×2** (shipped) | 1/4 | 672 KB | 224 KB | **+448 KB** |
| 512×448 | **4×2** or **2×4** | 1/8 | 784 KB | 112 KB | **+672 KB** |
| 512×448 | **4×4** | 1/16 | 840 KB | 56 KB | **+784 KB** |
| 512×512 (Pal576i) | **2×2** | 1/4 | 768 KB | 256 KB | **+512 KB** |
| 512×512 (Pal576i) | **4×4** | 1/16 | 960 KB | 64 KB | **+896 KB** |

`4×2` and `2×4` are **the same number of pixels**, so they return the same
memory, cost the same fill, and differ *only* in the picture — which is what
makes the choice between them a pure quality question and why the sweep below
asks it directly.

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

## Plain mode — the reduced raster without the network

Plain mode is BLSS with the neural half deleted: the 3D scene still rasterises
into the reduced-resolution target, the z buffer still shrinks with it, and the
display buffer is reconstructed by **one full-screen bilinear pass** — no bag
proxies, no reprojection, no feature grid, no MLP, and four grid vertices
instead of 476. It is *Reconstruction: Plain* in the settings block
(`ProjectSettings::blssNetwork`, `BLSS_NETWORK` in the generated
`scene_data.hpp`, `configure()`'s seventh argument in the engine).

**Why it exists.** Every measurement above prices the *network*, and the network
is nearly the whole bill: `proxy` 2.34 + `reproj` 0.28 + `feat` 0.19 + `net` 0.78
of a 4.60 ms total, all of it spent deciding what to do per tile. On a project
whose trained net asks for nothing — all three outputs under the inference
deadzone, `BLSSFILL … passes=1.00`, the composite already a single bilinear pass
— **the frame pays that bill to produce a weight field of zeros.** Plain mode is
the same picture without the bill.

| | neural | plain |
|---|---|---|
| EE a frame | **4.60 ms** | **0.52 ms** |
| composite fill | 0.50 ms | 0.50 ms (one pass, by construction) |
| break-even, 512×448 | **13.1** coverages | **2.6** |
| break-even, 512×512 | 11.4 | 2.3 |
| GS VRAM handed back at 2×2 | 448 KB | 448 KB — *unchanged* |
| picture | the network's | identical, when the network asks for nothing |

**That is what changes the feature's reach.** 13.1 full-screen coverages is a
fog demo. 2.6 is most 3D scenes with any overdraw at all — a room with a floor,
walls, props and a couple of transparent surfaces clears it.

### The floor plain mode cannot go below, and it is bigger than it looks

`begin` 0.41 + `end` 0.10 = **0.51 ms** survives every deletion. Those two are
the raster redirect itself — two GIF packets, each ending in `draw_finish` and a
wait, i.e. a GS round trip — and plain mode still renders into a low-res target,
so it still pays both. Half a millisecond of *"4.58 ms to produce a weight field
that is zero"* was never the network's; it is the price of rendering small at
all.

So an estimate of ~0.2 ms for this mode is not reachable, and a break-even
derived from it (~1.8) is about 30 % optimistic. The number is 2.6, and
`blssui::fill::breakEven(rasterPx, network)` derives it rather than storing it,
exactly as the neural one is derived — take a millisecond off either bill and
both move.

> **Quote a break-even with its raster AND its mode.** They are more than four
> times apart. A bare 13.1 read against a plain project would tell a scene at 4
> coverages to leave the feature off when it is a clear win, which is the same
> class of mistake as the bare 11.5 that was a 576i figure quoted at every
> resolution.

### Plain mode draws the same picture

Not asserted — **measured, and the first attempt at the measurement was
invalid.**

The composite's base pass reads the low-res target with `u(x) = (x << 4)/scaleX
+ jitter`, a linear function of the output pixel, through a bilinear filter and
a region clamp. The grid samples that function at every 32-pixel corner; plain
mode samples it at the four screen corners. Same `PRIM` flags, same vertex
order, same diagonal, same gradient, same constant vertex colour — so the packet
the GS receives is the same drawing, and identity is a property of the packet
rather than of a comparison that happened to come out clean. (A GS **sprite** was
written first and would have saved seven qwords. It hands the picture to a
different rasteriser path, and whether that path sets its texture DDA up bit for
bit like the triangle one is a question about hardware nobody here can answer, so
it was dropped rather than measured.)

**The fixture, because the obvious one is not valid.** `examples/upscaler-lab`'s
own net does **not** ask for nothing: under `blssDebugView 2` it reads

```
BLSSOUT  point=0.000/0.000/0.000 temporal=0.000/0.044/0.169 sharpen=0.000/0.000/0.000
BLSSFILL point=0.0% temporal=58.0% sharpen=0.0% passes=1.58
```

— point and sharpen are dead, but the temporal pass runs on 58 % of the grid. A
naive plain-vs-neural A/B on that project reads **230 930 of 811 426 pixels
different, every one by at most 2/255 in one channel**, and the difference is the
*temporal pass*, not the composite. It looked exactly like a primitive-level
divergence and was not one. The degenerate case has to be *constructed*: the same
project with **Temporal** off, so the network still runs and still measures
point 0.000 / sharpen 0.000, and the composite is pass 0 and nothing else.

**The result.** Fixture: `examples/upscaler-lab` at 512×448, the Cutscene tour
allowed to park, every emitter hidden and the camera pinned by an object script
so the frame is a pure function of the frame index; PCSX2 software renderer,
`-PrintWindow` captures, three frames per arm.

| comparison | differing pixels of 811 426 | max Δ |
|---|---|---|
| neural vs neural (control, 3 pairs) | **0** | 0 |
| plain vs plain (control, 3 pairs) | **0** | 0 |
| **neural vs plain (all 9 cross-pairings)** | **0** | **0** |

The compared region excludes three bands that move by design and are named
rather than trimmed away: PCSX2's own FPS overlay, the game's `FPS`/`MEM` HUD
text, and 40 rows at the two campfires, whose light pools flicker. Including the
last one the whole frame differs by 880 pixels — the fires — in *both* arms
equally.

The controls are what make the zero mean anything: a fixture whose own frames are
byte-identical between captures is a clean instrument, and this one is.

### What it costs, and what is inferred rather than measured

**The console was unreachable for this whole round** (`ps2client reset` returns 0
and nothing answers), so the numbers split into two kinds and the split is
stated rather than blurred.

**Measured, in PCSX2, on the identical fixture and camera** — counts and EE
aggregates, which is what an emulator is admissible for here:

| arm | `work` (frame EE) | `FTSPLIT proxy/reproj/feat/net/pkt` |
|---|---|---|
| BLSS off | **11.68 ms** | — |
| neural, `passes=1.00` | **14.19 ms** | `1.680/0.653 0.290 0.090 0.794 0.188` |
| **plain** | **12.34 ms** | `0.000/0.000 0.000 0.000 0.000 0.003` |

Every deleted term reads **exactly 0.000**, and the composite packet build falls
to **1.6 %** of the grid's. What BLSS charges the frame over native drops from
**+2.51 ms to +0.66 ms**. (These are emulator milliseconds and are quoted only as
an aggregate and a ratio: PCSX2 under-reports GS fill by 76× and its per-function
attribution does not transfer — its `begin` reads 0.07 where hardware reads 0.41.)

**Carried over from hardware, not re-measured:** `begin` 0.41 and `end` 0.10,
which plain mode does not change, and the four deleted terms' 3.59 ms. `pkt` is
taken as the PCSX2 ratio applied to a packet that is 13 qwords against ~1450.
Hence **0.52 ms**, and it is arithmetic over hardware measurements rather than a
hardware measurement of plain mode. It wants one run on a console.

> **One caveat the PCSX2 arms make visible.** BLSS' brackets are serialised, so
> in a frame whose GS is still busy at `endScene`, EE work deleted before it
> comes back as `end`: the two arms show **3.4 ms of EE removed and 1.9 ms of
> frame removed**, the balance landing there. That is the fill-bound regime,
> which is exactly where the model's own fill term already says BLSS wins — but
> it is why the plain break-even is a floor and not a promise.

### What plain mode does not change

- **The VRAM saving is identical.** It comes from the z buffer following the
  raster (`RendererCoreGS::allocateVramBuffers`), which is a property of the
  reduced render and has nothing to do with the network: 448 KB back at 2×2 on a
  512×448 output, and [exactly zero at 1×2](#at-12-the-vram-saving-is-exactly-zero-and-nothing-said-so).
- **The build interlock is identical.** Depth of field, portals and split screen
  still want real GS depth at display resolution, which the shrunken z buffer
  still does not allocate, so `blssClashes()` refuses the same combinations.
- **The sub-pixel jitter is forced off**, in one place, in `configure()`. The
  only thing that can fuse two jitter phases back into one image is the temporal
  pass, and plain mode has none — jitter without it is the
  [period-2 bob](#the-oscillation) and nothing else.
- **Training and evaluation still work and still mean something.** They answer
  what the *neural* mode would be worth on this scene, which is exactly the
  question a reader of the Plain setting is asking. The window says so and prices
  both modes side by side.
- **No `blss.net` is baked into a plain build** — no `#include`, no `setNet()`,
  ~2 KB less `.rodata` — and the boot log says
  `BLSS: reconstruction = PLAIN (no network) …` rather than naming a network it
  never loads.

## Training

**You do not have to train anything.** A default network ships inside the editor
and a project with no `blss.net` is built with it
([the net that ships](#the-net-that-ships)) — measured as a tie with per-project
training on the only example project that can discriminate. Everything below is
how to beat it on your own scene, and how the shipped one was made.

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

- **no `-i`, but a `blss.net` is there** → it is used, exactly as before;
- **no `-i`, and no `blss.net` where the tool is looking** → **the editor's
  built-in default net is used**, because that is the net this project would be
  *built* with and the Evaluate tab has to measure what the game will run. It
  used to drop the `half-res + BLSS (trained)` row entirely, which was the honest
  answer while a missing net meant random weights and is the wrong one now;
- **an explicit `-i <file>` that cannot be opened** → still an error. You asked
  for that net.

Every verb that resolves a net prints one line naming which one it got, plus any
provenance complaint ([provenance](#provenance-what-a-net-says-about-itself)):

```
[blss] net source=default path=(built into the editor) corpus=examples/upscaler-lab … bestiary scale=2x2 jitter=off
```

Both verbs take `--frames N`, `--assets <dir>`, `--seed N`, `--sharpen K`,
`--scale WxH` (or `--scale-1x2`, the same setting spelled the old way),
`--weight-decay W`, `--standardise`, `--threads N`
([below](#--threads-n-and-the-determinism-that-pays-for-it)), and the two weights
of the oracle's objective — `--flicker-weight` (with `--flicker-form lag1|period2`,
which picks *what* that weight charges for) and `--fill-weight`. **Sweep those two as
a pair**, because they trade against each other and against sharpness; the
shipped defaults (`0` and `16`) are the result of such a sweep, recorded with its
numbers in `src/blss.hpp`. Changing either changes the *labels*, so a change is
only meaningful after a re-train:

Six more take a configuration **no project can currently ask for**, and each
prints a line saying so, because a table of decibels whose configuration is not
on the page is a table nobody can reproduce:

| flag | what it measures | where its numbers are |
|---|---|---|
| `--tile N` | the decision tile edge; the engine's `kTile` is a compile-time constant | [The tile size, swept](#the-tile-size-swept) |
| `--scale WxH` | the raster scale, any positive pair. The **engine is already generic** (`setRasterScale`); it is `blssScale`, an int with 0 = 2×2 and 1 = 1×2, that can only name two of them | [Below half resolution, swept](#below-half-resolution-swept) |
| `--act-table N` | `tanh`/logistic from a shared table instead of libm, on the fit *and* the inference | [The transcendentals, as a table](#the-transcendentals-as-a-table) |
| `--no-anim` | leaves the project's animated models out of the corpus, the way it worked before they were added | [The animated models the corpus was not drawing](#the-animated-models-the-corpus-was-not-drawing) |
| `--still` | freezes each shot at one camera and one pose so only the jitter phase advances — the host twin of the console's frozen-camera experiment, and a **fixture for the period-2 table only** (it is refused by `--blss-train` and by `--cv`) | [The still fixture](#the-still-fixture-and-what-it-showed-the-metrics-floor-was) |
| `--proxy-budget` | caps a bag's proxy count at the tiles it covers — the fifth rule of the twin contract, which **ships off on both sides** | [The proxy budget](#the-proxy-budget-what-the-cheaper-frame-description-costs-the-network) |
| `--emitter-proxy` | gives each enabled particle emitter a bag proxy — the sixth rule of the twin contract, which **ships off on both sides**. It changes what the network is SHOWN, not what the corpus DRAWS, so a PSNR taken with it on is a cost and not the benefit | [The sixth rule](#the-sixth-rule-emitter-bags-describe-themselves) |
| `--ignore-shot-plan` | do not read the project's training-shot plan — six automatic moves, takes on, no authored vantages, an equal frame share | [Choosing what the corpus sees](#choosing-what-the-corpus-sees) |

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

#### More than one project: the union corpus and `--cv-groups`

**Every positional after the first is another project, and the corpus is their
concatenation.** The word `bestiary` stands in for the built-in procedural
corpus, so it can be a member like any project:

```bash
# fit one net to seven projects and the bestiary
tyrax-editor --blss-train examples/upscaler-lab examples/cube bestiary --all-shots -o default.net

# LEAVE-ONE-PROJECT-OUT: every shot held out in turn, trained on every project
# EXCEPT the one the shot belongs to
tyrax-editor --blss-eval examples/a examples/b examples/c bestiary --cv --cv-groups --cv-seeds 3
```

`--cv-groups` is the difference between two questions that look alike and are
not. Plain `--cv` holds out one *shot* and trains on the other eleven camera
moves **of the same scene** — "does this net generalise to a seventh move of
content it has already seen". `--cv-groups` holds out the same one shot and
removes its **whole project** from the training set — "does this net generalise
to a project it has never seen", which is the only form of the question
"can I ship one net" that means anything. The rows stay per shot (a fold has to
be one kind of content or its row says nothing) and a **per-project summary**
is printed under the fold table, with each project's oracle `ceiling` next to
its margin so a tie can be told from a win.

Three rules the union corpus enforces rather than assumes:

- **frames are still spread evenly over SHOTS**, so a member with more scenes
  contributes proportionally more frames. The header prints each member's shot
  count for exactly that reason;
- **a member that will not load is DROPPED, loudly.** A single `<projectDir>`
  still falls back to the bestiary, which is the documented behaviour; a union
  must not, because a member that quietly became the bestiary would put
  procedural frames into a table whose whole claim is which project they came
  from;
- **the sampler is one sampler.** `blssJitter` is per project and the corpus
  renders through one `setJitter()`, so members that disagree are a warning and
  the first member wins. Pass `--jitter` / `--no-jitter` to say which you meant.

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

`emitters=N` was appended to that line on 2026-08-09 and is the count of enabled
particle emitters the corpus walked past **without drawing** — see
[the emitter caveat](#the-corpus-renderer-draws-no-emitters). Any non-zero value
means `headroom` describes a frame missing that much fill, and a caller reading
only this line still learns it. **Appending is the compatible move and inserting
would not be:** this line is parsed key=value with unknown keys ignored, while
the tables above it are read by column position (next paragraph).

The **fold line is printed by `--blss-eval --cv`** as each fold finishes. The
fold loop turns the trainer's own verbosity off and prints nothing else until the
table at the very end, so a progress bar driven off this tool's output used to go
blank for minutes; the count is completion order, not fold index, because folds
run in parallel and a bar wants a fraction rather than an identity.

**The period-2 alternation table is printed UNDER the PSNR table, never as a
column in it, and that is a compatibility rule rather than a layout preference.**
`blssui::parseEval` and `parseCv` (`src/blss_ui.cpp`) read those tables **by
column position** — a new column in the middle of them is not a parse error, it
is a silently misread table, which is the worst failure mode a parser has. The
same rule bounds the rows: every alternation row is short enough that the
parsers' own shape check rejects it, and the `--cv` block sits where its
four-number rows fall under the seven-number guard.

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
tyrax-editor --blss-train <projectDir> --all-shots
tyrax-editor --blss-eval  <projectDir>
```

**The net lands next to the project's `.tyra`**, which is where
`templates::blssBake()` looks for it, so "train, then rebuild" works from any
directory. Until 2026-08-09 both verbs defaulted to a bare `blss.net` resolved
against the **current** directory, so unless you had `cd`-ed into the project
first the trainer wrote its net somewhere the build never reads, the rebuild
went on baking the shipped default, and the boot log said so in a line nobody
read. The explicit `-o` / `-i` above were the workaround; they still override,
and a bestiary or multi-project corpus still writes to the current directory
because a net fitted on several projects belongs to none of them.

#### The corpus renderer draws no emitters

> **A PSNR number for a particle-heavy project is not measured — it is measured
> on a different scene.** `blsscorpus.cpp` models emitters only in the coverage counter; the
> renderer that produces the truth images and the `--dump` comparisons has no
> emitter path and no blending at all.
>
> **The scene that shows it is `examples/showcase`, not `examples/upscaler-lab`,
> and this page said the wrong one for a day.** Re-measured 2026-08-09 on this
> tree, jitter **off**, scale **2×2**, shipped defaults:
>
> | | `--blss-eval` | `--blss-coverage` (geometry + emitters) | emitter share |
> |---|---|---|---|
> | `examples/showcase` | `headroom=+0.006` → **"WILL NOT BENEFIT"** | 15.24 = 0.67 + **14.57** | **95.6 %** |
> | `examples/upscaler-lab` | `headroom=+1.108` at 1.39 passes | 72.23 = 0.96 + **71.27** | **98.7 %** |
>
> So the confident sentence is real, and it is `showcase` that prints it: a
> near-zero ceiling measured on **4.4 %** of the frame's fill, quoted verbatim by
> the BLSS window. `upscaler-lab` no longer prints it — it reads a +1.11 dB
> ceiling today — so the older claim on this page that *it* read `+0.000` and
> *"THIS SCENE WILL NOT BENEFIT"* is **retracted**; it was measured before the
> shot plan and the demo's re-tune. The billboard count quoted with it (3 072)
> was wrong too: the project has **11 emitters and 568 billboards**.
>
> **What the tools do about it now.** `--blss-train` and `--blss-eval` print a
> WARNING naming the emitter count, and — since that warning scrolls off the top
> of a run that ends in three tables — the caveat also reaches the answer itself:
>
> - a project with **any** enabled emitter and a ceiling under +0.10 dB gets
>   **`NO VERDICT:`** instead of *"THIS SCENE WILL NOT BENEFIT"*, naming the
>   count and pointing at `--blss-coverage`. "Nothing to reconstruct" is a claim
>   about content, and the content was not all rendered;
> - the machine-readable line carries **`emitters=N`** (appended — that line is
>   parsed key=value and ignores unknown keys, unlike the tables above it).
>
> Until the renderer grows billboards: read the **speed** verdict for such a
> project and treat its decibels as absent. What a fix needs, why the blocker is
> the **engine** rather than the rasteriser, and why it must not be half-landed,
> is in [backlog.md](backlog.md).

Both entry points take an optional **positional project directory**, and with one
the corpus is the project's own scenes — real geometry, real materials, real
terrain, walked / panned / orbited / whipped / pitched / strafed by six camera
moves derived from the scene's bounds and its player start, plus any authored
Cutscene Director camera track. `src/blssscene.cpp` walks a project into
world-space triangles through the same three sources the GI bake uses
(primitives, static `.obj`, terrain chunks) **plus its animated models, posed per
console frame** — see [below](#the-animated-models-the-corpus-was-not-drawing). A
project that will not load, or loads with nothing to draw, falls back to the
bestiary and says so.

> **That last clause used to read "animated `.glb` is skipped *because* it goes
> down the dynamic pipeline, which does not feed BLSS at all". It was wrong**,
> and it had the same shape as the whole-bag proxy: a sentence about the engine
> that stopped being true, believed on both sides of a twin for as long as nobody
> compared the two frames.

**This is not a refinement, it is the difference between helping and hurting.**
Measured on `examples/procedural` (39 meshes, 15 098 triangles, no textures at
all), 72 frames over six shots, both nets fitted with `--all-shots`:

| over all 72 project frames | margin over bilinear | passes |
|---|---|---|
| bestiary-trained net | **−0.40 dB** | 1.72 |
| project-trained net | **+0.06 dB** | 1.19 |
| oracle (upper bound) | +0.77 dB | 1.20 |

> **RE-MEASURED 2026-08-09, and that table is a JITTER-ON table.** Two of its
> three rows survive and the third does not, which matters because the third is
> the one the other two were read against. At the sampler `examples/procedural`
> actually ships (`blssJitter` false) the ceiling is **+0.345 dB**, not +0.77 —
> the +0.77 is what the same scene reads at **jitter ON**, re-measured today as
> **+0.773**. The bestiary net is **−0.48** (and −0.85 if the bestiary is fitted
> jitter-off to match), and the project net is **−0.00 at 1.04 passes**: with
> half the ceiling gone it correctly asks for nothing. The generalisation of this
> row from one project to seven, and the answer to the question it was used to
> settle, are in
> [Can one net ship for every project?](#can-one-net-ship-for-every-project) —
> including the finding that **the bestiary in the training mix is what makes a
> universal net work**, which this row could not have shown because it only ever
> tried the bestiary alone.

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

**And not every project has anything to win** — but the example this section has
always used to say so is the **worst possible one**, and that is now flagged
rather than quietly repeated. On `examples/showcase` — 156
frames over two scenes × six moves — the *oracle* itself scores **+0.02 dB** over
bilinear at **1.00 passes**, frame-weighted over both splits the way the window's
verdict computes it (+0.04 held-out, +0.01 over the training shots): soft ground
texture, low-poly props, nothing that aliases.

> **That number is measured on 4.4 % of the frame.** `showcase` has **8 enabled
> emitters** and `--blss-coverage` reads **14.57** emitter coverages against
> **0.67** of geometry, so "nothing that aliases" is a statement about the
> geometry and the fog was never rendered. The tool now refuses to turn it into a
> verdict (`NO VERDICT:` rather than *"WILL NOT BENEFIT"*). The claim that a
> project *can* have no ceiling stands — `examples/procedural` reads +0.325 dB
> with **zero** emitters and there are ten more emitter-free examples under
> +0.10 dB — but **`showcase` is no longer evidence for it.**

`--blss-eval <projectDir>` is how
you find that out before shipping BLSS on it, and the window says it in one line.

> **The window does this too, and it is the default.** *Tools ▸ Neural Upscaler
> (BLSS)* has a **corpus switch in its header** — one switch for all five verbs —
> and it starts on *This project's own scenes*, with *Fit every shot* on. See
> [the window](#the-window).

### Choosing what the corpus sees

The six automatic camera moves are derived from the scene's own bounds and its
player start, and for a long time that was the whole story: whatever they framed
was what the network learned. **The training-shot plan** (*Tools ▸ Neural
Upscaler (BLSS)*, and `Project::blssShots` in the `.tyra`, format v10) makes it an
authoring decision instead. It carries four things:

| | |
|---|---|
| **which of the six automatic moves survive** | each is a checkbox. They are, in order, `walk` (dolly-forward — what the player sees most of the running time), `pan` (a yaw sweep from one standpoint: the same content at every reprojection offset the stick can produce), `orbit` (the only move that sweeps silhouettes across the whole tile grid), `whip` (eased, so the angular velocity peaks mid-shot and the net sees history that is fine, history that is useless, and both transitions), `pitch-up` (coverage sweeping from 1 to nearly 0) and `strafe` (a lateral translation — **the only move with real parallax**, so the only one that teaches disocclusion) |
| **how many frames each gets** | 0 = an equal share of `--frames`; a number = exactly that many |
| **whether Cutscene Director takes join** | on by default: a take is the author having already said which frame matters |
| **the author's own vantages** | typed, grabbed from the viewport, or bound to a placed Camera object. One key is a still standpoint — a legitimate shot, because the history is perfect there and the net has to learn not to spend passes on it — and two keys are a move |

**A default plan writes nothing to the `.tyra`, and produces the byte-identical
corpus it always did.** That is the compatibility guarantee and it is checked
rather than asserted: `--blss-train examples/procedural --all-shots --frames 72
--no-jitter` writes md5 `e069f286ea0c524999bfd9dac769608c` before and after the
plan existed. Every fold table on this page therefore stays a measurement of the
code that is here.

`--ignore-shot-plan` reads the plan not at all — six moves, takes on, no
authored vantages, an equal share. Same one role as `--no-package-split` and
`--no-anim`: it is how a table taken *before* a project authored a plan stays
runnable on that project afterwards, and it prints the usual "this is a
measurement configuration" line. A project whose plan is default gets the same
shots either way.

#### A take DISPLACES an automatic move, and nothing said so

`kShotsPerScene` is **6 shots total**, not 6 automatic ones: authored takes are
pushed first and the automatic set fills up to the cap. On
`examples/upscaler-lab` — one Cutscene take — the corpus is therefore
`take / walk / pan / orbit / whip / pitch` and **there is no `strafe` at all**,
which is the one move that produces real parallax and the one the disocclusion
behaviour is learned from. Nobody had noticed, because nothing printed the
breakdown. It does now:

```
[blss]   scene 'main': 32 mesh(es) + 4 animated part(s), 11650 triangle(s), 6 shot(s) (1 take, 0 authored, 5 automatic)
```

**That is why a non-default plan LIFTS the cap.** An author who has asked for
particular shots has also said the six-shot budget is not the constraint any
more, so takes, then authored vantages, then every enabled automatic move all
get in. The count keeps its position and its `shot(s)` token in that line
because the window parses it back (`blssui::parseCorpusScenes`) to check the
trainer actually obeyed the plan — **a plan the tool ignores looks exactly like a
plan it honours** from the outside, same project, same scenes, a different
corpus.

Frame budgets follow the same "say it out loud" rule. Explicit counts are
honoured first and the rest share what is left; if the explicit counts alone
exceed `--frames` they are **scaled down with a printed line** naming the
`--frames` that would satisfy the plan as authored, and any shot that ends up
with no frames at all is reported — a shot with zero frames is a fold that does
not exist and a row the window will not find.

### Is the corpus good enough?

A corpus can be the right scene and still teach nothing, and until the *Inputs*
tab grew a health check nothing said so in words — it printed the numbers that
say a channel is dead and left the reading to you. The thresholds, in the order
they are applied:

| test | threshold | what it means |
|---|---|---|
| **no channel predicts anything** | peak \|channel↔oracle r\| **< 0.05** over all 18 (channel, output) pairs | **Unusable — there is nothing here to learn.** The oracle wants the same weights in every tile of every frame, so a per-tile network has no decision to make and can only add fill |
| a channel is **constant** | `sd < 0.005` and ≥ 99 % of tiles at 0 or at 1 | **Unusable.** A constant channel is a channel the network does not have, and the console will feed it a value the corpus never contained |
| a channel is **pinned** | ≥ 50 % of tiles at a clamp | trainable, but the net decides from fewer inputs than it has |
| a channel is **flat** | `sd < 0.02` | as above |
| a channel **cannot tell the shots apart** | per-shot mean spread < 0.02 | it is not carrying the difference between camera moves, which is what it exists to do |
| the corpus is **thin** | fewer than 4 camera moves | neighbouring frames of one move are near-duplicates, so this is a much smaller sample than the frame count suggests |

**The first test outranks the rest**, because a corpus whose oracle asks for the
same answer everywhere has nothing to teach however healthy its inputs look. It
is anchored on two projects at a stated configuration — 12 frames per shot,
jitter off, `--blss-eval --features`:

| | peak \|r\| | verdict |
|---|---|---|
| `examples/showcase` | **0.015** | THERE IS NOTHING HERE TO LEARN — and its oracle ceiling is **+0.00 dB**, which is the independent confirmation. Both were measured **without its 8 emitters**, which the corpus renderer does not draw ([backlog](backlog.md)) |
| `examples/upscaler-lab` | **0.277** | trainable, but thin — `coverage` is saturated on two thirds of its tiles |

Both are an order of magnitude away from the 0.05 line, which is what makes it a
usable gate. It is a coarse one: the number moves with the frame count and with
the sampler, so treat it as "which side of the line" and never as a score.

**Then probe a console frame against it before shipping.** A channel that is
pinned in the corpus and not pinned on the console is the mismatch that costs
decibels — it is the whole of
[what this page believed and got wrong, entry 6](#measured-is-not-optimised-eight-times).
Run the game with *Debug view* → the feature-spread entry, take the `BLSSFEAT`
line and paste it into `--blss-eval --probe`. **Under ps2link there is no
`bin/log.txt`** — the game's output comes back over the `[ps2]` stream instead —
so the window falls back to that stream, or to a pasted line, and says which
source it used.

The corpus was the binding constraint, not the trainer. Measured on the original
seven when they were all there was (`1b9c7a74`, and **not re-run since** — the
old corpus no longer exists to re-run it on): holding out **one** shot and
training on six scored +0.31 dB, while holding out **two** and training on five
scored +0.10 dB at four times the spread. Same method, smaller training set,
noisier estimate. Six more shots is what fixed that, and it is the change that
moved the headline number.

Materials are real PNGs from `examples/*/res/{materials,models}` where the tree
has them and procedural checkers/noise/foliage otherwise, so training works in a
clean checkout. Frames are split evenly over the shots — except where the
project's training-shot plan asked for a particular count, which is honoured
first ([above](#choosing-what-the-corpus-sees)) — and `CorpusFrame::shot`
records which shot each frame belongs to, because the eval split has to hold out
whole shots: neighbouring frames of one camera move are near-duplicates.
`CorpusFrame::group` records which **project** it came from, which is what
`--cv-groups` holds out. For every frame it
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
| the stability term | `--flicker-weight`, **default 0** | temporal stability — **measured to be a bad trade in both forms, see below** |
| the fill the candidate would make the GS draw | `--fill-weight`, **default 16** | sparsity |

The stability term has **two forms**, selected by `--flicker-form`, and the
distinction is the whole term rather than a variant:

| `--flicker-form` | what it charges for | |
|---|---|---|
| `period2` (**default**) | the stationary period-2 alternation the candidate weights would leave, clamped, derived in closed form from the alpha bytes | cannot be paid by freezing |
| `lag1` | MSE against the reprojected history — the original | minimised by the picture freezing, and blind to a period-2 artefact |

Both ship at weight 0. [The trade curve](#the-trade-curve) is why, and it is the
reason to reach for the table rather than the knob.

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

or, on a project with particle emitters, **neither** — see
[the emitter caveat](#the-corpus-renderer-draws-no-emitters). The first form is
withheld there rather than shown, because the corpus did not draw the emitters
and a near-zero ceiling for the geometry is not a fact about the scene.

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

#### The overdraw count is an INDEX, and the camera theory of its gap is dead

*Will the frame get faster?* counts how many times over a project's scenes paint
the screen (`blss::measureCoverage`, headless twin `--blss-coverage`) and puts
that against the hardware break-even. On `examples/upscaler-lab` it reads
**72.23** where the five-point hardware fit implies **58.70** blended-pass
equivalents — an over-read that has now had two explanations and lost both.

The first was that the counter prices an opaque untextured fragment as the
blended textured pass `kPassMs` was calibrated on; that was falsified in the
round that measured it (geometry is ~1.0 of the ~72.5 against a measured ceiling
of ≤1.14, and re-weighting moves the total by 0.49 against a 13.93 gap).

The second was **the camera**: hardware ran the fixture's own gameplay camera
while the estimator averages six synthetic corpus moves spanning 36.2 to 88.4,
so a single camera near 57.7 would have meant no counter bug at all. The
training-shot plan made that testable — an authored still vantage at the parked
FPP standpoint the A/B sampled, eye `(0, 1.8, 27)` looking −Z, every automatic
move switched off — and it came out the other way:

| what the coverage is taken over | counted |
|---|---|
| the fixture's own parked camera (what the console A/B ran) | **78.99** |
| its Cutscene Director tour | 85.64 |
| the six-move corpus mean | 72.63 |

(The three counted rows above were taken before the fixture's art assets were
replaced with CC0 ones on 2026-08-09; the six-move mean reads 72.23 since, and
the emitters — which are 98.7 % of it — were not touched.)
| *the hardware fit's blended-pass equivalents* | *58.70* |

**The game's own camera reads higher than the corpus average, not lower.** So the
over-read is real and larger than recorded — 34.6 % under the camera that was
actually measured — and vantage is not the mechanism.

Two more measurements say where it *is*. Switching the six haze banks off leaves
that same camera counting **1.55** coverages of geometry plus fire/smoke/rain
against the hardware's **≤1.14** for the same content: the *counted* half is
right, and the whole residual is the emitter term, which is modelled rather than
counted. And stepping the banks 6/4/2/0 exactly as the hardware rig did:

| haze banks | counted | hardware | ratio |
|---|---|---|---|
| 6 | 78.99 | 58.70 | 1.35 |
| 4 | 46.82 | 37.17 | 1.26 |
| 2 | 19.55 | 15.40 | 1.27 |
| 0 | 1.55 | 1.14 | 1.36 |

A constant ratio across a fifty-fold range of load is not a wrong pool position
or a wrong puff size — those could not hold their ratio over three different bank
subsets. It is a wrong **price per coverage**, and part of that price is
arithmetic rather than hypothesis: `FrameProfile::gsFillProbe` sizes its sprite
from the current framebuffer and the calibration fixture ran PAL 576i, so
0.5872 ms is per **512×512**, while a coverage out of `measureCoverage` is per
the project's own raster — 512×448 here, **14.3 % fewer pixels**, i.e. 0.5138 ms.
That is 14 of the ~30 points. The rest (a counted haze coverage costs
33.79 / 77.45 = **0.436 ms**) is a 128²-texture puff magnified across the screen
being cheaper per pixel than the probe's 1:1 framebuffer blit — texture cache,
and only a console settles that.

**That half is now rescaled** (2026-08-09). `kPassMsPerMpx` replaces the single
`kPassMs` scalar: both rasters were measured back to back on one console
(0.5896 ms at 512×512, 0.5174 at 512×448, `perMpx` agreeing to 0.3 %), so the
price is per **pixel** and `breakEven()`, `speedFrom()` and `--blss-coverage`
all take the raster the coverages were counted at. Break-even is **13.1**
coverages at 512×448 and **11.4** at 512×512 — quote it with its raster or not
at all, because the single 11.5 this page used to print was a 576i number
applied to every project, i.e. 14 % optimistic for the common case.

**It closes none of the residual above, and that is the cross-check worth
keeping.** The over-read was measured with both instruments looking at the same
fixture at the same resolution, so a per-raster correction moves both sides of
that comparison equally and cannot explain any of it: the remaining ~26 % is
still the modelled emitter term, still the magnified-puff-versus-1:1-blit
question, and still only a console can settle it. What the window says about the
count is therefore unchanged: it is an overdraw **index**
that tracks the console's fill and over-states its scale by about a third, stated
next to the spread of the moves it averaged and next to the *Training shots* tab,
where the player's own vantage can be added to that set (`project::blssResolveShot`
is read by the count and by the trainer alike, so a row in the per-move table is a
frame the corpus really renders).

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
| the oracle margin is under **0.10 dB** **and the project has enabled emitters** | **NO VERDICT** — naming the count. The ceiling was measured on the geometry alone, so "nothing to reconstruct" is not a claim this corpus can make ([why](#the-corpus-renderer-draws-no-emitters)) |
| the oracle margin is under **0.10 dB** | **THIS SCENE WILL NOT BENEFIT** — leave the upscaler off. There is nothing to reconstruct, and no amount of training moves that |
| the net's own margin is **below zero** on a scene that *does* have room | **THE NETWORK YOU HAVE IS WORSE THAN NOT USING IT** — this is the net, not the content. On the bestiary corpus it adds that this is exactly what a bestiary-trained net does on a real project |
| otherwise | the margin, the pass count, the ceiling, and **what fraction of the ceiling** the network captured |

The emitter row is **first** because it is a guard on the row under it: the CLI
prints the same four branches in the same order (`src/blss.cpp`), and the window
quotes the CLI verbatim, so there is exactly one answer to "will this scene
benefit" however the question was asked.

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

#### The tabs get a floor, because the tables are the point

The window is three bands — header, tab strip, the tool's raw output — and until
2026-08-09 the tabs got whatever the other two left over. That is backwards, and
it broke the tab this window exists for. Measured with `--ui-script` on a 1080p
screen (1017 px of work area, a 1060×820 window, 791 px of body): once a coverage
answer renders the header is **514 px**, the output pane takes its 150, and the
tab child comes out **111 px**. At that height the Evaluate tab's own controls —
the *Network* field, *Frames*, the deadzone, *Run the evaluation* — are submitted
**below the child's bottom edge**: not visible, not clickable, and `dump` lists
them with rects outside their own window, so a scripted click on one reports
success while landing on whatever is really at those coordinates. That is how it
survived a scripted check, and it is why the previous round could not screenshot
the tab's own net-source line.

Both other bands are bounded now and the tabs get the floor. The output pane may
take at most 35 % of the body (it was 70 % of whatever the header left). What
remains is split tabs-first — at least 60 % of it, capped at 420 px — and the
header takes the rest inside an `AutoResizeY` child with a size constraint, so it
is unchanged while it fits and **scrolls** past the cap when an answer makes it
tall. Its top, which is the part with the controls in it — the net source, the
corpus switch, the three question buttons — stays exactly where it was. Same
fixture after the change: header 246 px, tabs **366 px**, every Evaluate control
inside the child, the child taking the mouse wheel, and the net-source line plus
the whole held-out table readable by scrolling within the tab.

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
| **Scale** | `2×2` (quarter the pixels) or `1×2` (half-height only — cheaper reconstruction, keeps horizontal detail). A **live line under the combo** works out what it is worth in GS VRAM on *this project's* raster, and says outright that [1×2 is worth nothing](#at-12-the-vram-saving-is-exactly-zero-and-nothing-said-so). **Two entries and not more, on purpose**: the tool can now sweep any raster scale (`--scale WxH`) and [going below half resolution was measured and declined](#below-half-resolution-swept) — the picture loses up to 2.6 dB for a break-even that barely moves |
| **Reconstruction** | **Neural** (the per-tile network) or **Plain** — one bilinear pass, no network, 0.52 ms of EE instead of 4.60 and a break-even of 2.6 coverages instead of 13.1, with the VRAM saving and the picture unchanged whenever the network asks for nothing. [Plain mode](#plain-mode--the-reduced-raster-without-the-network). Its own switch rather than a third value of the two rows above, because it is a third question: *Enabled* asks whether the raster shrinks, *Scale* by how much, this asks what blows it back up. Picking Plain greys the four rows below it — every one of them is a knob on the network |
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
> to be edited whenever a table on this page is. **Four known drifts today**, all
> in the window's copy and none in the numbers below:
>
> 1. the tooltip quotes the fold spread as **sd 0.40**; the re-run in
>    [Measured](#the-out-of-distribution-number-and-how-to-get-one-that-means-something)
>    reads **0.34**;
> 2. the standing note's three facts are the **−0.40 / +0.06 / +0.77** row, which
>    is [retracted](#first-the-040-db-re-measured-and-the-ceiling-next-to-it-was-a-different-sampler) —
>    it had two samplers in it. At one sampler the same scene reads
>    −0.48 / −0.00 / +0.345;
> 3. the bestiary radio button's subtitle carries that same **−0.40 dB**; the
>    generalised figure is **−0.34 dB mean over seven projects, −1.09 at worst**,
>    and the actionable sentence is that the bestiary belongs *in* a corpus, not
>    *as* one;
> 4. **"Project network: none — the game will be built with RANDOM weights"** is
>    no longer true of any project: the bake falls back to
>    [the net that ships](#the-net-that-ships).
>
> They are listed rather than fixed here because they are UI text, and this page
> is not where UI text lives.

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
[the fifth entry](#measured-is-not-optimised-eight-times)).

> **It answers "a seventh camera move", not "a new project".** Leave-one-shot-out
> trains on eleven other moves of the same content, so it is the right question
> for a corpus of thirteen unrelated bestiary shots and the wrong one for a
> corpus of six moves over one scene. `--cv-groups` is the other question and it
> needs a union corpus:
> [Can one net ship for every project?](#can-one-net-ship-for-every-project).

13 shots × 3 seeds = **39 fold-runs**, 156 frames, 512×448 from 256×224, shipped
defaults (`--epochs 400`, `--seed 0xB1557`, `--flicker-weight 0`,
`--fill-weight 16`, weight decay `1e-4`, raw inputs), and the bestiary's own
sampler — **jitter ON**, which is what `--blss-train` with no project gives you
and is not what any example project ships:

| held-out shot | seed `B1557` | seed `CCD704ED` | seed `8814F396` | mean | sd |
|---|---|---|---|---|---|
| 0 `floor-horizon` dolly-in | +0.28 | +0.20 | +0.47 | **+0.32** | 0.11 |
| 1 `boxes-sphere` orbit | +0.58 | +0.61 | +0.53 | **+0.57** | 0.03 |
| 2 `poles` pan | +0.85 | +0.62 | +0.93 | **+0.80** | 0.13 |
| 3 `foliage` static | +0.90 | +0.38 | +1.03 | **+0.77** | 0.28 |
| 4 `grazing-wall` dolly-along | −0.13 | +0.06 | +0.06 | **−0.00** | 0.09 |
| 5 `flat` slow pan | +1.04 | +0.98 | +1.06 | **+1.03** | 0.04 |
| 6 `whip` whip pan | +0.23 | +0.16 | +0.17 | **+0.19** | 0.03 |
| 7 `corridor` dolly-down | −0.18 | +0.02 | −0.21 | **−0.12** | 0.10 |
| 8 `strafe-field` lateral | +0.40 | +0.35 | +0.43 | **+0.39** | 0.04 |
| 9 `pitch-sky` pitch-up | +0.81 | +0.27 | +0.34 | **+0.47** | 0.24 |
| 10 `distant-plain` slow dolly | +0.43 | +0.19 | +0.21 | **+0.28** | 0.11 |
| 11 `sphere-field` orbit-wide | +0.04 | +0.31 | +0.64 | **+0.33** | 0.24 |
| 12 `foliage-walk` dolly-through | +0.31 | +0.21 | +0.21 | **+0.24** | 0.05 |
| **mean over folds** | **+0.43** | **+0.34** | **+0.45** | **+0.41** | **0.05** |

> **BLSS beats plain bilinear out of distribution by +0.41 dB**, sd 0.34 over 39
> fold-runs, **3 of 39 below bilinear**, at **1.79 mean full-screen passes**
> (sd 0.33) against 1.00 for bilinear.

**The conservative figure is +0.27 dB.** Shots 7–12 are the six that were added
*after* the defaults were chosen and took no part in choosing them; their fold
means are −0.12, +0.39, +0.47, +0.28, +0.33, +0.24. Quote that one when the
question is "will it help on content nobody tuned for".

Per fold, mean over the three seeds — `in-dist` is the same margin on that fold's
twelve **training** shots, i.e. the control that says the fold trained at all
(a held-out number under a collapsed `in-dist` number means nothing):

| held-out shot | native | bilinear | BLSS | oracle | passes | flicker | in-dist |
|---|---|---|---|---|---|---|---|
| 0 `floor-horizon` | 19.57 | 18.88 | 19.20 | 20.28 | 1.49 | 34.10 | +0.46 |
| 1 `boxes-sphere` | 20.26 | 18.95 | 19.52 | 19.85 | 2.07 | 45.86 | +0.47 |
| 2 `poles` | 22.11 | 21.07 | 21.87 | 23.13 | 1.47 | 29.09 | +0.40 |
| 3 `foliage` | 28.13 | 25.61 | 26.38 | 27.78 | 1.78 | 6.30 | +0.42 |
| 4 `grazing-wall` | 29.31 | 27.55 | 27.54 | 28.01 | 1.85 | 7.82 | +0.55 |
| 5 `flat` | 58.30 | 54.67 | 55.70 | 54.67 | 2.02 | 0.04 | +0.51 |
| 6 `whip` | 25.48 | 22.58 | 22.77 | 23.62 | 1.95 | 41.95 | +0.56 |
| 7 `corridor` | 31.80 | 27.42 | 27.30 | 27.59 | 2.21 | 17.64 | +0.54 |
| 8 `strafe-field` | 22.58 | 21.67 | 22.06 | 22.79 | 1.80 | 21.14 | +0.49 |
| 9 `pitch-sky` | 22.86 | 22.15 | 22.62 | 24.40 | 1.52 | 38.19 | +0.51 |
| 10 `distant-plain` | 22.71 | 23.39 | 23.66 | 24.83 | 1.47 | 12.89 | +0.51 |
| 11 `sphere-field` | 32.76 | 31.06 | 31.40 | 31.74 | 1.90 | 14.67 | +0.54 |
| 12 `foliage-walk` | 29.32 | 27.40 | 27.64 | 27.82 | 1.73 | 14.29 | +0.54 |

> **These numbers were re-run for this page, not copied.** They moved, and mostly
> in the right direction: the mean is +0.41 against the +0.40 this page printed
> before the deadzone and the proxy fix, the folds below bilinear dropped from 5
> to 3, and the pass count fell from 2.85 to **1.79** — those two changes between
> them took a whole full-screen pass out of the frame. Two numbers got *worse*
> and are worth naming: the sd of the per-seed fold mean is **0.05** rather than
> 0.01, and two folds (`pitch-sky`, `sphere-field`) spread 0.24 dB across seeds.
> The seed still moves the answer far less than the fold does, but by four times
> less margin than this page once claimed.
>
> **The re-run at the shipped activations is done, and this IS it.** The table
> above used to be a libm measurement carrying an "owed a re-run" flag, because
> `--act-table 512` had since been turned on for both twins and only two cells
> had been spot-checked. Re-run whole: the **mean moves +0.42 → +0.41**, the sd
> **0.35 → 0.34**, the passes **1.80 → 1.79**, and 3 of 39 below bilinear is
> unchanged. Both spot-checks land exactly where they were predicted to —
> `floor-horizon` seed `B1557` **+0.35 → +0.28** and `boxes-sphere` **+0.58**,
> unmoved — and the bestiary's proxy count is still **1 217**, which is the
> control saying the corpus did not change underneath the activations. The
> largest single-cell move is `foliage` (+0.87 → +0.77), driven entirely by its
> middle seed (+0.68 → +0.38) against a fold sd that is now 0.28: a seed spread,
> not a regression. **The ±0.1 dB caveat is withdrawn — read the cells as
> measured.**
>
> The whole table costs **65.6 s** of wall clock on 6 cores (corpus 7.7 s, oracle
> 13.2 s, folds 44.6 s), which is why it is reasonable to re-run it rather than
> quote it. It reproduces cell for cell across binaries — first at `7d3dbf67` on
> the threaded corpus, now again with the provenance and default-net work in the
> tree — which is what the determinism contract behind `--threads`
> ([above](#--threads-n-and-the-determinism-that-pays-for-it)) is *for*.

**The one shot it still loses on is `corridor` (−0.12), and the reason is a
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

`flat` is worth a second look for the opposite reason: 55.70 dB against
bilinear's 54.67 on a screen with nothing in it, at **2.02 passes** — and
`corridor`'s 2.21 is now the only fold above it. The old reading of `flat` was
4.33 passes and it has come down a long way, but the pair of them is still where
the remaining fill lives.

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

### Can one net ship for every project?

**Yes — but only if the bestiary and real projects are in the same training
corpus. Either one alone fails, in two different ways.** This section replaces
the one-project anecdote this page used to decide the question on, and it
retracts part of it.

Everything below is at **512×448 from 256×224** (`blssScale 0`), shipped
defaults (400 epochs, decay `1e-4`, flicker 0 / period2, fill 16, sharpen 0.5,
inference deadzone 8, activation table 512, raw inputs), **12 frames per shot**,
and **`--no-jitter`** — which is the sampler *every* example project ships:
`blssJitter` defaults to `false` and `examples/upscaler-lab` writes `false`
explicitly. Read the sampler off every table on this page before comparing it to
anything; the mistake this section corrects is exactly that.

#### First: the −0.40 dB, re-measured, and the ceiling next to it was a different sampler

`examples/procedural`, 72 frames over its six camera moves, both nets fitted
with `--all-shots`, margin frame-weighted over both splits against the
**project's own** bilinear:

| the net | fitted on | margin | passes |
|---|---|---|---|
| the bestiary's, as `--blss-train` writes it (**jitter ON**) | 156 frames / 13 shots | **−0.48 dB** | 1.40 |
| the bestiary's, fitted at the project's own sampler (**jitter OFF**) | 156 / 13 | **−0.85 dB** | 1.26 |
| the project's own | 72 / 6 | **−0.00 dB** | 1.04 |
| the oracle — the ceiling | — | **+0.34 dB** | 1.13 |

**The claim holds and it is worse than it was written down as.** A
bestiary-trained net is −0.48 dB on that project, and fitting the bestiary at
the project's own sampler makes it −0.85 rather than better.

**What does not hold is the row's third number.** This page printed the ceiling
as **+0.77 dB**, and `examples/procedural` at **jitter ON** reads **+0.773 dB**
today — to three decimals. That figure was measured before `blssJitter` existed,
when the corpus was always jittered; at the sampler the project actually ships,
the same scene's ceiling is **+0.345**. And the project-trained net is not
`+0.06` any more but `−0.00` at 1.04 passes: with half the ceiling gone there is
nothing left for it to win, so it correctly asks for nothing. **A ceiling and a
margin measured under two different samplers are two different experiments**, and
this page put them in one row for two months.

#### Leave-one-PROJECT-out, which is the experiment nobody had run

Seven projects, chosen by oracle ceiling rather than by taste — every
`examples/*` was screened net-free first
([the ceilings](#which-projects-can-discriminate-anything)) and these are the
seven with the most headroom. `--cv-groups`, 3 seeds × 6 shots = **18 fold-runs
per project**; the *sd* column is over those 18, which is the fold-to-fold
spread this page insists on and not a seed spread.

| held-out project | **(1)** other 6 projects | **(2)** other 6 **+ bestiary** | **(3)** its own net, held out | **(4)** its own net, `--all-shots` | **(5)** ceiling |
|---|---|---|---|---|---|
| `upscaler-lab` | +0.00 ±0.00 | **+0.29 ±0.37** | **+0.31 ±0.34** | +0.41 | **+0.72** |
| `material-lab` | −0.08 ±0.07 | −0.07 ±0.04 | −0.05 ±0.20 | +0.00 | +0.62 |
| `endless-runner` | +0.00 ±0.00 | −0.04 ±0.12 | −0.20 ±0.32 | +0.08 | +0.48 |
| `cube` | +0.00 ±0.00 | +0.06 ±0.04 | +0.00 ±0.00 | +0.00 | +0.19 |
| `save-points` | −0.00 ±0.00 | +0.01 ±0.02 | −0.00 ±0.00 | +0.00 | +0.25 |
| `procedural` | +0.00 ±0.00 | −0.04 ±0.12 | −0.04 ±0.07 | −0.00 | +0.34 |
| `endless-scroller` | +0.00 ±0.02 | +0.01 ±0.03 | +0.00 ±0.00 | +0.00 | +0.15 |
| the bestiary, held out | — | +0.08 ±0.07 | — | — | +0.59 |
| **mean over the seven** | **+0.00** | **+0.03** | **+0.00** | **+0.07** | |

Columns 1 and 2 are the same run with one member added; column 3 is a separate
`--cv` per project (leave-one-**shot**-out inside it, 3 seeds); column 4 is the
shipping recipe evaluated on the frames it was fitted on, so it is the
optimistic number and is here to bound the other three. Column 5 is the oracle's
own margin on the same folds, and it reproduces **cell for cell** between the
leave-one-project-out run and the seven separate per-project runs — which is the
check that the two are measuring the same content.

**Read column 5 first, because most of this table is ties.** Only
`upscaler-lab` has a ceiling that a 0.3-dB difference can fit inside. On the
other six the whole question is worth at most a fifth of a decibel and every
column is inside the fold sd; a mean over all seven is dominated by content with
nothing at stake, which is why the per-project rows are the answer and the mean
is a footnote.

**On the one row that can discriminate anything, a net that has never seen the
project scores +0.29 against the project's own net's +0.31, at fold sds of 0.37
and 0.34.** That is a tie, and it is the answer to the question this section
asks. Note also that the honest per-project number (column 3) *loses* to the
universal net on the mean, because two projects' own nets are actively harmful
out of their own distribution (`endless-runner` −0.20, `material-lab` −0.05):
six camera moves of one scene do not generalise to a seventh, which this page
has said since the corpus was seven shots.

**One discriminating project is a weak base and this conclusion should be read
as such.** The honest form is: *the universal net has been shown not to lose,
on the only content in this tree where losing would be visible.* What would fix
it is more content with a real ceiling — the screening below says the whole
`examples/` tree contains exactly one scene above +0.5 dB, so this cannot be
fixed by measuring harder, only by authoring or acquiring heavy-overdraw scenes.
That is the same shortage the break-even table has: one fixture on each side.

#### The net that ships

The measurement above says one net can ship. This is that net.

```bash
tyrax-editor --blss-train \
    examples/upscaler-lab examples/material-lab examples/endless-runner \
    examples/cube examples/save-points examples/procedural \
    examples/endless-scroller bestiary \
    --all-shots --frames 660 --no-jitter -o resources/blss-default.net
```

`resources/blss-default.net` (500 bytes) plus `resources/blss-default.net.meta`,
both **embedded into the editor binary** by `cmake/embed_binary.cmake` — the app
icon's arrangement, for the app icon's reason: the editor is a single executable
people copy around, and a default that is only present when someone remembered
to carry a data file is worse than no default. `templates.cpp` falls back to it
whenever a project has BLSS on and no `blss.net`, and both the generated header
and the boot log name which network the build got.

**Why those seven projects and not all thirty-two.** Three reasons, in order of
weight:

1. **A shot with no ceiling teaches "do nothing".** The oracle's labels on a
   scene with +0.00 dB of headroom are all "ask for plain bilinear", and the
   measured failure mode of a projects-only corpus is precisely that
   degeneration ([below](#why-the-projects-only-union-collapses-and-it-is-not-it-learned-nothing)).
   Adding twenty-five low-ceiling projects would add label mass pulling in the
   one direction the corpus already has too much of. These seven are every
   `examples/*` above **+0.24 dB** on the net-free screening in the next section.
2. **It is the corpus the result was measured on.** Column 2 of the table above
   trains on six of these seven plus the bestiary; the shipped net trains on all
   seven plus the bestiary, which is the same recipe with nothing held out.
   Adding a member would make the thing that ships a different object from the
   thing that was cross-validated, and this feature has published five numbers
   measured on something other than what shipped.
3. **They span the content kinds the screening found.** Heavy alpha overdraw
   (`upscaler-lab`), textured surface detail (`material-lab`), scrolling
   parallax (`endless-runner`, `endless-scroller`), dense procedural scatter
   (`procedural`), bare primitives against sky (`cube`) and terrain-with-props
   (`save-points`). The bestiary supplies the *difficulty* range and, critically,
   the `texDetail` spread — that channel is identically zero on five of the seven
   and is the bestiary's most oracle-correlated input, which is the whole
   mechanism behind "either corpus alone fails".

**The other flags, and why each is the value it is.** `--all-shots` because
nothing is held out of a net you ship (the fold tables are how it is measured,
not how it is fitted). `--frames 660` because the corpus is **55 shots** and the
leave-one-project-out experiment ran at **12 frames per shot**; frames are split
evenly, so any other total is a different experiment, and at the bare default of
156 the tail shots would get three frames each — a shot with three frames teaches
the temporal channel almost nothing. `--no-jitter` because `blssJitter` defaults
to `false` and every shipped example writes `false`; the sampler is resolved once
for a union corpus, and a net fitted at the other one is fitted for a different
picture. Everything else is the shipped default (400 epochs, decay `1e-4`,
flicker 0 / period2, fill 16, sharpen 0.5, activation table 512, seed `0xB1557`).

**What this net's number is, and what it is not.** The honest estimate of its
performance on *your* project — a project it has never seen — is the **+0.29 dB
±0.37** of column 2, on the one row in this tree that can discriminate. It is
**not** the +0.41 in column 4: that is a net evaluated on the frames it was
fitted on. And it is not a promise about a project whose ceiling is +0.00, where
it will correctly ask for plain bilinear and win nothing.

Reproducing it costs **39 s** on six cores (corpus 7.8 s, oracle 15.9 s, fit
15.6 s) and writes md5 `879146bdee7f3b183c05985012753649`. That equality is
checked rather than asserted: the net was trained once with the binary at
`0187c887` and again with the provenance work in it, and both runs wrote the same
500 bytes — which is also the check that adding provenance did not touch
training.

#### Provenance: what a net says about itself

A `blss.net` is a four-byte magic, a `kNetVersion` and 123 floats. That version
answers exactly one question — *can these bytes be read into this `Net`* — and
`load()` refuses when they cannot. **Everything that decides whether a net is the
RIGHT net was invisible**: its corpus, the raster scale it was fitted at, whether
the sampler jittered, which activation table produced its labels. A net fitted at
jitter ON, baked into a project that ships jitter OFF, loads perfectly and
composites perfectly and is measurably worse, with nothing anywhere saying so.

That was survivable while every net was trained by the person who baked it ten
seconds earlier. It stops being survivable the moment a **default** net ships:
nobody trained it, nobody remembers its corpus, and it outlives several editor
versions.

`--blss-train` now writes **`<net>.meta`** next to every net it produces:

```
blss-provenance 1
net-version 3
features 6
hidden 12
outputs 3
tile 32
act-table 512
scale 2x2
jitter 0
sharpen 0.5
frames 660
shots 55
epochs 400
seed 0xB1557
corpus examples/upscaler-lab … bestiary
command tyrax-editor --blss-train … -o resources/blss-default.net
```

**Why a sidecar and not a longer file header**, which is the obvious fix and was
refused: the net file's bytes are a *published reproducibility anchor*. This
feature's shot-plan compatibility check, its thread-determinism contract and its
default-net check are all of the form "does this command still write md5 X"
(`e069f286ea0c524999bfd9dac769608c`, `6b2fba90d0f059f055134a55df478c8e`,
`879146bdee7f3b183c05985012753649`). Adding one byte to the format invalidates
every one of them silently, and the next person to run a check learns only that
it failed. So the weights keep their format to the byte and the provenance sits
beside them.

**The sidecar carries no timestamp and no editor version**, and both omissions
are the same decision: re-running `command` must reproduce the file byte for
byte, or the CI check that guards the shipped default — *re-run it and diff* —
cannot fire. A clock would break that outright (the chat-store precedent: the
mtime is already there and a clock in the file is a diff nobody wants), and a
semver would break it on every unrelated release, dirtying
`resources/blss-default.net.meta` while the 500 bytes beside it are unchanged.
That is a diff which teaches nothing and trains people to ignore the one that
matters. Which editor wrote a net is recoverable from git; whether it is the
*right* net is not, and that is what the fields are for. The property is checked
rather than asserted: the sidecar was first written by hand from the format spec,
and the trainer's own output came out identical.

The price is stated rather than hidden: **a sidecar can be separated from its net
by a copy.** A net with no sidecar reports *unknown provenance* — a warning, not
an error, because every net trained before this existed is in that state and must
keep working.

**What the checks do.** Two severities, and the split is the point:

| class | fields | what happens |
|---|---|---|
| **fatal** — the weights are not a net of this shape | `net-version`, topology, `tile` | the net is **refused**; the bake falls through to the next candidate and says why |
| **warn** — the net runs, it was fitted for something else | `act-table`, `scale`, `jitter` | baked anyway, named in the generated header **and** in the boot log |

The bake's candidate order is: the project's own `blss.net`, then the editor's
built-in default, then — only if the embedded asset cannot be read by this build
at all — the random initialisation, which is now a defect report rather than a
routine outcome. One trap worth recording because it fired immediately: the
activation-table check must compare against **`blss::kEngineActTable`**, the
compile-time twin of the engine's `TYRA_BLSS_ACT_TABLE`, and never against
`blss::detail::gActN`. The latter is the *host's* live `--act-table` setting and
is 0 in any process that never ran a BLSS verb — so a `--refresh-gen` compared
every net against 0 and warned about all of them.

#### Which projects can discriminate anything

`--blss-eval <projectDir>` needs no network and prints the ceiling in one line,
so the whole tree can be screened. All 32, 48 frames, jitter off, sorted:

| ceiling | projects |
|---|---|
| **> +0.4 dB** | `upscaler-lab` **+0.80**, `material-lab` +0.47, `endless-runner` +0.45 |
| +0.2 … +0.4 | `cube` +0.33, `save-points` +0.31, `procedural` +0.30, `endless-scroller` +0.24, `custom-nodes` +0.24, `portals` +0.22, `vu-lab` +0.20 |
| +0.1 … +0.2 | `script-demo`, `video-modes`, `credits`, `object-spawning`, `probe-aim`, `glow`, `nav-ai`, `blocks-terrain`, `large-terrain`, `cutscene-demo` |
| **< +0.1 dB — a tie however it reads** | `raytraced-mirror`, `physics-playground`, `layer-streaming`, `mirror-room`, `reflections`, `two-players`, `global-illumination`, `gi-showcase`, `lighting`, `texture-feeds`, `day-night`, **`showcase` +0.000** |

Twelve of the thirty-two cannot tell two nets apart at all. Any mean over
`examples/` is mostly those twelve.

> **Eleven of those twelve stand; `showcase` does not.** It is the only member of
> the bottom row with particle emitters (8 of them), and
> [the corpus renderer draws none](#the-corpus-renderer-draws-no-emitters) — its
> +0.000 is a ceiling for the 4.4 % of the frame that is geometry. `portals` in
> the +0.2 row has one enabled emitter and is flagged for the same reason. Every
> other project in this table is emitter-free, so the shape of the result — most
> examples have no ceiling — is unaffected.

#### Why the projects-only union collapses, and it is not "it learned nothing"

Column 1 reads **+0.00 at 1.00 passes on every project** — the composite
degenerates to the plain bilinear base pass. The `in-dist` control says the same
thing about the training side (+0.00 to +0.04 dB, against +0.41…+0.56 for a
bestiary fold), so the temptation is to call it a failed fit.

**It is not. The deadzone is hiding a wrong answer.** Same nets, evaluation
only, `--deadzone-sweep` over 42 fold-runs per row:

| inference deadzone, alpha | 0 | 2 | 4 | **8 (shipped)** | 16 |
|---|---|---|---|---|---|
| held-out margin | **−0.10** | −0.03 | −0.00 | **−0.00** | +0.00 |
| mean passes | **2.15** | 1.39 | 1.04 | **1.00** | 1.00 |
| point / temporal occupancy | 48 % / 67 % | 1.5 % / 37 % | 0 % / 4 % | 0 % / 0.1 % | 0 % / 0 % |
| folds below bilinear | **22 / 42** | 17 / 42 | 10 / 42 | **1 / 42** | 0 / 42 |

Without the deadzone the cross-project net asks for **two and a bit full-screen
passes** and is **−0.10 dB with half its folds below bilinear**. The shipped
deadzone does not make it right; it throws away everything it asks for and
leaves plain bilinear. So "the universal net is harmless" and "the universal net
is correct" are different statements and only the first one is true here.

#### The mechanism, shown rather than asserted

The channel this page named — `texDetail`, the bestiary's most
oracle-correlated input — is **identically zero on five of the seven projects**.
`--blss-eval --features`, jitter off, 12 frames per shot:

| corpus | `texDetail` mean | % of tiles at 0 | `edgeDens` % at 1.0 | the bestiary net's margin there |
|---|---|---|---|---|
| the bestiary | 0.120 | 30.6 % | 29.2 % | — |
| `upscaler-lab` | **0.443** | 29.3 % | 45.2 % | **+0.23** |
| `cube` | 0.000 | **100 %** | 31.8 % | +0.24 |
| `material-lab` | 0.082 | 30.0 % | 43.6 % | −0.29 |
| `save-points` | 0.000 | **100 %** | 30.9 % | −0.18 |
| `endless-scroller` | 0.000 | **100 %** | 34.3 % | −0.44 |
| `procedural` | 0.000 | **100 %** | **63.4 %** | −0.85 |
| `endless-runner` | 0.000 | **100 %** | 40.0 % | **−1.09** |

Two things reproduce exactly and one is new. The **63 % against 29 %**
`edgeDens` saturation this page reported for `procedural` is still 63.4 %
against 29.2 %, and `texDetail` is still identically zero there. What is new is
that this is not a `procedural` quirk: **most example projects are untextured
enough that the channel is a constant zero**, so the bestiary net's temporal gate
is driven by an input that is dead on most real content.

The other half of the instrument places the frame *inside* the corpus.
`--blss-eval --features --probe` with `endless-runner`'s own band — the project
the bestiary net loses 1.09 dB on — against the bestiary corpus:

```
feature      console min/mean/max   spread   corpus min..max     pct    supp    band  verdict
texDetail       0.000/0.000/0.000    0.000      0.000..1.000   30.6%   34.0%    0.0%  CONSTANT; in distribution
coverage        0.000/0.616/1.000    1.000      0.000..1.000   25.7%    0.5%   78.2%  no support - net extrapolates
```

`band 0.0 %` is the sentence: **none of the bestiary corpus lies inside the
frame's own `texDetail` band**, because the frame's band is a single point the
corpus only ever visits as one end of a range. That is the same shape of failure
the whole-bag proxy had, arrived at from the other direction, and it is why this
probe is a permanent instrument rather than a debugging session.

(The probe takes the engine's `BLSSFEAT` line. Feeding it a *host* corpus'
`--features` row, as above, is a legitimate second use — it answers "does corpus
A cover corpus B" — but say which you did: this run compared two host
distributions and no console was involved.)

#### Every net against every project

The full transfer matrix, `--all-shots` nets, 72 frames per target, margin over
that project's own bilinear. `union7` is one net fitted to all seven projects
and `union7b` adds the bestiary; both have **seen** the target, so those two
columns are in-distribution and the honest generalisation numbers are the
leave-one-project-out table above:

| target (ceiling) | bestiary only | union7 | union7 + bestiary | its own net |
|---|---|---|---|---|
| `upscaler-lab` (+0.94) | +0.23 | +0.25 | +0.20 | **+0.41** |
| `material-lab` (+0.66) | **−0.29** | −0.03 | −0.00 | +0.00 |
| `endless-runner` (+0.51) | **−1.09** | −0.03 | −0.01 | +0.08 |
| `cube` (+0.17) | +0.24 | +0.02 | +0.01 | +0.00 |
| `save-points` (+0.36) | −0.18 | +0.00 | +0.00 | +0.00 |
| `procedural` (+0.34) | **−0.85** | −0.00 | +0.01 | −0.00 |
| `endless-scroller` (+0.22) | **−0.44** | +0.00 | +0.00 | +0.00 |
| **mean** | **−0.34** | +0.03 | +0.03 | +0.07 |

**Shipping the bestiary net is a lottery with a −1.09 dB worst case**, and that
is this page's old rule generalised from one project to seven. It wins on
`upscaler-lab` (real texture detail) and on `cube` (whose ceiling is +0.17, so
the win is tie-sized), and loses on the other five.

#### `--standardise`, re-run at six channels

Measured and rejected once at **eight** inputs, before two were retired, so it
was owed a re-run. Same leave-one-project-out configuration, union + bestiary,
3 seeds, 165 fold-runs:

| | raw inputs (shipped) | `--standardise` |
|---|---|---|
| mean over the seven projects | **+0.03** | **−0.05** |
| `upscaler-lab` | +0.29 ±0.37 | +0.21 ±0.15 |
| `material-lab` | −0.07 ±0.04 | **−0.59 ±0.33** |
| overall, 165 fold-runs | +0.04, sd 0.17 | −0.01, sd 0.26 |
| folds below bilinear | 52 / 165 | 45 / 165 |
| mean passes | 1.25 | 1.27 |

**The verdict survives the drop to six channels and the failure changed shape.**
Standardising reduces the *count* of losing folds (45 against 52) and makes the
worst one far worse: `material-lab` goes from −0.07 to −0.59, which is most of
its whole ceiling spent in the wrong direction. It stays off. As with
`--flicker-weight`, setting a knob to zero is not the same as deleting it — the
flag still reaches the configuration and this is its number at `kFeatures = 6`.

#### What to do with this

- **A default net can ship, and it must be fitted to the bestiary *and* real
  projects.** `--blss-train <every example> bestiary --all-shots` is the recipe;
  on the one project with a ceiling it is a tie with per-project training, and
  its worst case over seven projects is −0.07 dB against the bestiary-only net's
  −1.09.
- **Keep the retrain button and keep the advice to press it**, but the reason is
  no longer "otherwise the net hurts you". It is that per-project training is the
  only thing that reached +0.41 in distribution, which is the number the console
  actually runs, and `--blss-eval` costs seconds.
- **Neither is worth anything on a project whose ceiling is under +0.1 dB**, and
  twelve of the thirty-two examples are. That question is answered net-free,
  first, in one line.

### The animated models the corpus was not drawing

**A real train/run mismatch, host-only to fix, and the correctness argument is
much stronger than the decibel.** `src/blssscene.cpp` built the project corpus
from primitives, static `.obj` and terrain chunks and **skipped animated models
outright**, with a comment saying they "go down the dynamic pipeline, which does
not feed BLSS at all". Three files say otherwise, and all three were checked:

- `src/templates.cpp` — `updateAndRenderAnimObjects` poses and skins on the EE
  and then submits each part with **`stapip.core.render()`**; the header comment
  at the `GameAnimModel` declaration says it outright, "the skinned arrays render
  through the SAME static pipeline as the rest of the scene";
- a generated game shows it: `examples/showcase/src/terrain_game.cpp` draws them
  through the same call;
- `stapip_core.cpp` submits a BLSS proxy for any bag whose
  `PipelineInfoBag::blssProxy` is true, **which is the default** — animated bags
  never opt out.

So on the console a character is **drawn and described**, and in the corpus it
was **neither**. The network was fitted on frames that did not contain it. That
is the whole-bag-proxy bug's shape again, one layer up, and this half needs no
engine change because **the console side is already right**.

**What the corpus does now.** `blssscene.cpp` bakes each animated part's clip at
**50 Hz — the console's frame rate — into a table of poses**, because two
consecutive corpus frames *are* two consecutive console frames (the history is
one frame deep, the phase alternates every frame, and `motion` is the
reprojection between them), so the pose has to advance by exactly one console
frame between them. The object's clip, `animSpeed`, loop and autoplay flags are
folded in, so the frame loop only ever indexes by frame number — which is what
keeps a frame a pure function of its index and the corpus bit-identical at any
`--threads`. `.glb` and `.fbx` both go through `animimport::bake`, the same entry
the editor's own preview and matbake use, so the replacement-UV sidecar the
`.tskl` carries is applied here too. Each part becomes one bag, exactly as
`setupAnimObject` makes one `StaPipBag` per material, and the **package split is
recomputed per pose** because the boxes `StaPipCore` cuts are cut over the
*skinned* vertices (`bboxVersion` is bumped on every re-skin).

**The measurement, and the honest answer is "too small to resolve on any project
in this tree".** `--no-anim` restores the old behaviour, and it is a clean A/B:
the animated meshes still vote for the scene bounds and the triangle centroid, so
**both arms shoot exactly the same six camera moves** and differ only in whether
those frames contain the models. `--blss-eval --cv --cv-seeds 3`, 6 shots × 3
seeds = 18 fold-runs:

| `examples/upscaler-lab` | held-out margin | sd | below bilinear | passes |
|---|---|---|---|---|
| animated models in the corpus | **+0.69** | 0.50 | 0/18 | 1.74 |
| `--no-anim` (the old behaviour) | +0.66 | 0.53 | 1/18 | 1.74 |

+0.03 dB against a fold sd of 0.50. **That is not a result, and the reason is
worth more than the number**: the two animated models were **48 of that project's 1 512 bag
proxies (3.2 %)** and, in the frame the comparison run dumped, **154 of 229 376
pixels — 0.07 % of the picture.** The input distribution barely moves with them:
every channel mean is identical to three decimals except `texDetail`
(0.444 against 0.446). A corpus cannot measure a difference that is three
thousandths of the frame.

The **mismatch itself** — a net fitted the old way and then run on the corpus
that has the models, which is exactly what the console was doing — agrees, from
a second direction: `--all-shots` nets, plain `--blss-eval` on the animated
corpus, **+0.81 dB (47 % of the ceiling) fitted without them against +0.85 dB
(49 %) fitted with them.** Same 0.04 dB, same conclusion.

`examples/large-terrain` looked like the strong case — **80 animated parts** —
and turned out to be a different kind of useless: it is a scene with nothing to
reconstruct, where the trained net asks for **1.00 passes, i.e. plain bilinear,
on every one of the six folds** and BLSS equals bilinear to two decimal places
with or without the models. And those 80 parts are **0.04 % of its 213 676
proxies**, so even a scene that *did* have a ceiling would not have shown them.
`examples/showcase` is the same story for the same reason its oracle is +0.02 dB
([above](#training-on-your-own-project)): +0.00 dB at 1.00 passes over 36
fold-runs, animated or not.

> **So all three example projects that contain animated models fail to resolve
> this**, two because the animated share of the frame is a fraction of a percent
> and one because the scene has no ceiling at all. That is a statement about the
> examples in this tree, not about the change.

**Land it anyway, and the reason is not the decibel.** The corpus' one job is to
describe the frame the console draws; the price of doing that correctly here is
zero (no engine change, no quality cost, no fill change, no `kNetVersion`) and
the benefit scales with how much of a frame is animated. A project whose
characters fill the screen — which is most games that have characters — is
exactly the case the old corpus described worst, and the case none of the
example projects in this tree happen to be. **What this section cannot claim is
that it helps**: nothing here has measured a project where animated geometry is a
significant share of the frame, and until something does, the argument is
correctness and not quality.

Three approximations remain, all recorded rather than hidden:

- **No mesh or animation LOD.** The console renders a distant instance from a
  decimated variant and refreshes its pose every 2nd or 4th frame
  (`MESH_LOD_DISTANCE` / `ANIM_LOD_DISTANCE`); the corpus always draws tier 0 and
  re-poses every frame. At the corpus' camera distances tier 0 is usually right,
  and a coarser tier would only make the proxy boxes *less* precise.
- **No pose sharing.** Two instances in the same pose share one skinned buffer on
  the console; here they are skinned independently. That changes nothing about
  what a proxy describes.
- **The clip is sampled at 50 Hz and held past `kAnimPoses` (48 frames).** A shot
  is a dozen-odd frames, so the hold is unreachable in every configuration this
  page measures.

### The tile size, swept

**`kTile` had never been moved, and it is the largest EE saving this feature has
on the table.** The grid is 16 × 14 = 224 tiles at 512 × 448, and every tile costs
108 MACs plus 15 libm calls; the composite's weight field is 17 × 15 = 255 grid
corners of packet. At `kTile = 64` that is **56 tiles and 72 corners** — a
quarter of the inference and 3.5× less packet build — and the obvious worry is
fill: a lit cell becomes 64 × 64 instead of 32 × 32, and `emitGrid`'s
four-corner rule lights the nine cells around any tile that asks for a kernel, so
one decision now costs four times the pixels.

`--tile N` sweeps it on the host (`blss::tileSize()`); the engine's
`RendererCoreBlss::kTile` is a compile-time constant, so the tool prints a line
saying the run is a measurement configuration. Leave-one-shot-out
cross-validation, 13 shots × 3 seeds = 39 fold-runs per row, 156 frames,
everything else at the shipped defaults:

| `--tile` | grid | inference | corners | held-out margin | sd | below bilinear | **passes** | point | temporal | in-dist |
|---|---|---|---|---|---|---|---|---|---|---|
| 16 | 32 × 28 = 896 | 4× | 957 | +0.33 | 0.34 | 5/39 | **2.02** | 36.4 % | 66.0 % | +0.43 |
| **32** (shipped) | 16 × 14 = 224 | 1× | 255 | **+0.42** | **0.35** | **3/39** | **1.80** | 10.3 % | 69.6 % | +0.50 |
| 64 | 8 × 7 = 56 | ¼× | 72 | +0.46 | **0.49** | **6/39** | **1.77** | 0.0 % | 77.3 % | +0.61 |

> **The occupancy columns are fractions of the SCREEN, not counts of cells**, so
> they are directly comparable across tile sizes and they answer the fill worry
> directly: **the frame does not draw more.** 1.80 → 1.77 passes. The point pass
> disappears entirely (10.3 % → 0 %) and the temporal pass spreads (69.6 % →
> 77.3 %), and those two almost exactly cancel.

**And yet the recommendation is: do not do it.** Read the last four columns
together rather than the margin alone:

- The **mean is a draw.** +0.46 against +0.42 is 0.04 dB against a fold-to-fold
  sd of 0.35–0.49. By this page's own rule that is not a difference.
- The **spread grows by 40 %** (0.35 → 0.49) and the **folds where the network
  is worse than doing nothing double** (3/39 → 6/39). A coarser grid is a
  smaller, easier fitting problem — the `in-dist` control rises from +0.50 to
  +0.61, i.e. it fits its own training shots *better* — while generalising less
  reliably. That is the signature this page already has a section for: **the
  network is variance-limited, and anything that makes the fit easier makes the
  feature worse.** kTile 64 is `--standardise` again, with a real EE saving
  attached to it this time.
- The per-fold rows say where it goes: `poles` +0.81 → +1.31 and `pitch-sky`
  +0.48 → +1.41 (a coarse grid suits a frame that is mostly one thing), against
  `foliage-walk` +0.24 → **−0.11**, `strafe-field` +0.40 → +0.10 and
  `sphere-field`'s **oracle** dropping 31.74 → 31.29 — at 64 the trained net
  actually scores *above* the oracle on that fold, which means the oracle can no
  longer express the answer either. The tile is now wider than the features being
  separated.

**16 is unambiguously worse** and settles the direction: +0.33 dB at **2.02
passes**, i.e. it pays 0.22 of a full-screen pass more than 32 and 4× the
inference to lose 0.09 dB. Finer is not better; the grid is not what limits this.

On a project corpus — `examples/procedural`, 6 shots × 3 seeds = 18 fold-runs,
and remember that a project's held-out decibel does not generalise
([above](#training-on-your-own-project)), so read the **passes** column here and
not the dB:

| `--tile` | margin | sd | below bilinear | passes | point | temporal |
|---|---|---|---|---|---|---|
| 16 | +0.01 | 0.17 | 7/18 | 1.79 | 58.1 % | 20.9 % |
| **32** | −0.03 | 0.34 | 9/18 | **1.35** | 3.8 % | 30.8 % |
| 64 | +0.09 | 0.51 | 6/18 | **1.51** | 0.0 % | 51.4 % |

**Here the fill worry is real**: 1.35 → 1.51 passes, +12 %, because the temporal
occupancy jumps 30.8 % → 51.4 % and a coarse grid cannot decline as precisely.
So the bestiary's "fill is flat" is a property of the bestiary, not of the
change: on content where the correct answer is *sparse*, a coarser grid draws
more.

**What it would buy, in units an engine change can be checked against.** Per
frame at 512 × 448, every quantity that scales with the grid:

| per frame | `kTile` 32 | `kTile` 64 | |
|---|---|---|---|
| tiles inferred (`runNet`) | 224 | **56** | −75 % |
| MACs | 24 192 | **6 048** | −75 % |
| `tanhf` | 2 688 | **672** | −75 % |
| `expf` + `fdiv` | 672 + 672 | **168 + 168** | −75 % |
| grid corners (`buildReproj`, `cornerAlpha`) | 255 | **72** | −72 % |
| strip vertices per full-screen pass (`emitGrid`) | 476 | **126** | −74 % |

(The inference counts are the worst case: both twins skip a tile whose
`coverage` is below `kMinCoverage`, and the one instrumented console frame had
159 of 196 tiles covered, so scale by ~0.8.)

> **That is aimed straight at the 5.10 ms.** The hardware measurement attributes
> `composite()` at 5.41 ms of which **5.10 ms is EE** — inference plus packet
> build — and this change is a 3–4× cut to *both* halves of it, which is the
> largest single lever the feature has. **It still does not close the gap**: the
> frame is 9.83 ms in the red and the other ~4.4 ms is extra scene submission
> from the per-package bag proxies (~3.9 ms) plus `beginScene()` (0.45 ms),
> neither of which the tile size touches.
>
> **The recommendation is still: do not flip the default on this evidence.** The
> mean is a draw and the tail is worse — 6 of 39 fold-runs below bilinear against
> 3, on a feature whose entire remaining defence is that it usually helps. But
> this is now a genuine trade rather than a free lunch declined, and it is priced
> on both sides: a **4× cheaper inference and packet build** against **twice the
> rate at which a shot comes out worse than doing nothing**. If the choice is
> made to take it, take it deliberately and re-measure everything on this page.
>
> It is a **twin change**:
> `vendor/tyra/engine/inc/renderer/core/blss/renderer_core_blss.hpp` — `kTile`,
> and `kMaxCols`/`kMaxRows`, which are written as `512/kTile` and
> `ceil(540/kTile)` — plus `src/blss.hpp`'s `kTile`, in one commit, and every
> table on this page re-measured. `HiDef1080i` (448 × 540) does not divide at 64
> any more than it does at 32; see
> [the math doc's Symbols](blss-reconstruction.md#symbols).

### Below half resolution, swept

**Can BLSS render at less than half resolution?** It always could — the
restriction was the host, not the hardware. `RendererSettings::setRasterScale(sx,
sy)` takes any positive pair and every derived size divides through it;
`RendererCoreBlss::configure()` only clamps them to ≥ 1. What could not express
anything else was `blss::Scale`, an enum of two members whose `scaleY()` returned
a literal `2`, and `blssScale`, an int the loader clamps to `0..1`. So the
question had never been asked, and `--scale WxH` asks it.

The answer is **no, not by default — and the reason is not the one that was
expected.** The network does not stop working at a bigger factor; the *picture*
does.

`examples/upscaler-lab`, `--blss-eval --cv --cv-seeds 5 --frames 120`, 6 shots ×
5 seeds = **30 fold-runs per row**, 400 epochs, decay `1e-4`, fill 16, deadzone 8,
jitter **off** (the project's own setting). The `2×2` row is the control: it
reproduces [the trade curve's](#the-trade-curve) published jitter-off row —
+0.33, sd 0.34, 2/30, 1.65 passes — to the last digit, which is what says the
generalisation did not change the shipped configuration.

| scale | low raster | area | **margin over ITS OWN bilinear** | sd | below bilinear | passes | **absolute BLSS, dB** | vs `2×2` |
|---|---|---|---|---|---|---|---|---|
| **2×2** (shipped) | 256×224 | 1/4 | +0.33 | 0.34 | 2/30 | 1.65 | **26.98** | — |
| **4×2** | 128×224 | 1/8 | +0.37 | 0.31 | **0/30** | 1.70 | **26.00** | **−0.97** |
| **2×4** | 256×112 | 1/8 | +0.47 | 0.38 | **0/30** | 1.75 | **24.76** | **−2.22** |
| **4×4** | 128×112 | 1/16 | +0.45 | 0.34 | **0/30** | 1.76 | **24.35** | **−2.63** |

Read the last two columns against the fourth, because that contrast is the whole
result:

- **The margin does not decay.** Every scale beats its own bilinear by about the
  same amount, and the two most aggressive ones lose *fewer* folds to it than the
  shipped one does. The oracle agrees: the net-free ceiling
  (`[blss] verdict headroom=`) reads **+1.023 / +0.994 / +1.048 / +0.990 dB**
  across the four rows — flat to within its own noise. **A per-tile kernel
  decision is worth the same at 4× as at 2×.** That was not obvious and it is the
  one genuinely encouraging thing here.
- **The picture falls off a cliff anyway.** The absolute held-out PSNR drops
  1.0–2.6 dB, monotonically in area. The entire trained-net margin is +0.33 dB,
  so **moving from `2×2` to `4×4` costs eight times what the whole feature buys.**
  A relative win on a much worse image is still a much worse image.

`examples/procedural`, same harness, is the replication and it agrees on the
shape while having no margin of its own to speak of (as
[already recorded](#the-second-project-could-not-confirm-it-and-says-so) — half
its folds lose to bilinear at any setting, so read the last two columns only):

| `examples/procedural` | margin | sd | below bilinear | passes | absolute BLSS | vs `2×2` |
|---|---|---|---|---|---|---|
| **2×2** | −0.04 | 0.09 | 15/30 | 1.16 | **31.40** | — |
| **4×2** | −0.01 | 0.12 | 14/30 | 1.26 | **28.75** | −2.64 |
| **2×4** | −0.01 | 0.04 | 19/30 | 1.21 | **28.90** | −2.49 |
| **4×4** | +0.01 | 0.12 | 15/30 | 1.46 | **27.46** | −3.94 |

#### `2×4` is the WORST of the two half-area modes, not the best

The hypothesis going in was *"`2×4` is the sweet spot, because vertical detail is
already compromised by interlace"*. **It is refuted on the fixture that can
discriminate.** `4×2` and `2×4` are the same pixel count — identical fill,
identical VRAM (672 KB), identical break-even — so the only thing separating them
is the picture, and on `upscaler-lab` `4×2` is **1.25 dB better** (26.00 against
24.76; on the net-free bilinear baselines, 25.63 against 24.29, a gap of 1.34 dB
that owes nothing to the network). Throwing away horizontal resolution is
markedly cheaper than throwing away vertical.

A mechanism that fits: at 512×448 the frame is *already* sampled more finely
vertically than horizontally. The corpus' projection is `tanHalfFovX = 0.577`,
`tanHalfFovY = 0.433` — a 4:3 ratio against a 512:448 = 8:7 raster — so there are
444 pixels per unit of horizontal tangent against **517** per unit of vertical.
The vertical axis carries more resolvable detail per pixel row, so halving it
destroys more.

**That mechanism is a hypothesis, and the second fixture does not confirm it.**
On `procedural` the two modes are within 0.15 dB of each other and the sign is
*reversed* (`2×4` 28.90 against `4×2` 28.75). The projection is the same in both
runs, so whatever separates them is content: `upscaler-lab` is layered haze
billboards and textured architecture, `procedural` is untextured meshes with
nothing high-frequency in either axis. **The honest statement is the measurement,
not the theory: on the one fixture with real headroom, `4×2` strictly dominates
`2×4`, and on the one without, the choice does not matter.** Anyone shipping a
sub-half mode should run this on their own content rather than inherit either
answer.

#### The fill does come back — the network asks for it, and the deadzone eats it

The worry was that at a bigger magnification plain bilinear is blurrier, so the
`point` and `sharpen` kernels become worth their passes and the composite spends
back what the raster saved. **The first half of that is exactly right and the
second half does not happen**, and the reason is a knob that was tuned for
another purpose entirely.

`--deadzone-sweep 8,0` over the same 30 fold-runs, so both rows are the *same
nets* — the deadzone is an inference knob and never reaches the labels
(`upscaler-lab`; occupancy is a fraction of the screen):

| scale | | passes | **point** | temporal | sharpen | margin | below bil |
|---|---|---|---|---|---|---|---|
| `2×2` | deadzone **8** (shipped) | 1.65 | **0.0 %** | 65.1 % | 0.0 % | +0.33 | 2/30 |
| | deadzone 0 | 2.03 | **24.3 %** | 79.0 % | 0.0 % | +0.33 | 3/30 |
| `4×2` | deadzone **8** | 1.70 | **0.0 %** | 69.5 % | 0.0 % | +0.37 | 0/30 |
| | deadzone 0 | 2.32 | **52.4 %** | 79.1 % | 0.0 % | +0.36 | 0/30 |
| `2×4` | deadzone **8** | 1.75 | **0.0 %** | 75.2 % | 0.0 % | +0.47 | 0/30 |
| | deadzone 0 | 2.23 | **44.2 %** | 79.2 % | 0.0 % | +0.46 | 0/30 |
| `4×4` | deadzone **8** | 1.76 | **0.0 %** | 76.2 % | 0.0 % | +0.45 | 0/30 |
| | deadzone 0 | 2.29 | **50.2 %** | 79.1 % | 0.0 % | +0.44 | 0/30 |

**The point channel is the prediction coming true.** Undeadzoned, the network's
demand for nearest-neighbour more than doubles as the factor grows — 24.3 % of
the screen at `2×2`, 44–52 % at 1/8 and 1/16 — which is precisely "plain bilinear
is blurrier at 4×, so the crisp kernel is worth its pass".

**And the shipped deadzone deletes all of it, for free, at every scale.** At
alpha 8 the point occupancy is **0.0 % everywhere** and the margin does not move
by more than 0.01 dB — inside a fold sd of 0.31–0.38 by a factor of thirty. So
the extra kernel the bigger factor makes the net *want* is one it cannot cash:
`kDeadzoneAlpha` was chosen to buy a whole pass for 0.02 dB on the bestiary (its
sweep is in `src/blss.hpp`) and it turns out to generalise to the raster scale
without being re-tuned. Sharpen never appears at any scale — the fill term had
already taken it.

What is left is the temporal pass spreading (65 % → 76 %), and that is the whole
of the fill that actually comes back:

| mean full-screen passes | `2×2` | `4×2` | `2×4` | `4×4` |
|---|---|---|---|---|
| `upscaler-lab` | 1.65 | 1.70 | 1.75 | **1.76** |
| `procedural` | 1.16 | 1.26 | 1.21 | **1.46** |

At the calibrated **0.587 ms per full-screen blended textured pass**
([profiling.md](profiling.md)) the worst of those, `procedural`'s +0.30 pass, is
**0.18 ms** given back against a raster saving of eleven sixteenths of the
scene's 3D fill. On `upscaler-lab` it is +0.11 pass, **0.065 ms**. Nothing here
is a fill trade — **but it would have been at deadzone 0**, where `4×2` pays 2.32
passes against `2×2`'s 2.03 and buys nothing for the difference.

#### What the speed actually buys, and why the memory is the real argument

The EE bill does **not** move with the raster scale, by construction: the tile
grid is 16×14 at *output* resolution, the net is one evaluation per tile, the
proxies are projected into output pixels, and the composite's packet is the same
255 corners. Every term in the 5.02 ms is an output-resolution quantity. (By
construction — **not measured**; no hardware has run any scale but `2×2`.)

So the saving is only the raster fill, and the raster fill is already three
quarters gone at `2×2`. Extending the page's own break-even model
(`0.741 × 0.587 × C > 5.02 + 0.46 ms`, i.e. C > 12.6 — the "**about 13**" this
page quotes) to an area divisor `A`, the fill kept becomes `1/A` and the
inequality `(1 − 1/A) × 0.587 × C > 5.48 ms`:

| scale | area | fill kept | **break-even, coverages** | VRAM returned |
|---|---|---|---|---|
| **2×2** | 1/4 | 25.9 % (measured; 25 % geometric) | **~12.6** | **448 KB** |
| **4×2** / **2×4** | 1/8 | 12.5 % | **~10.7** | **672 KB** |
| **4×4** | 1/16 | 6.25 % | **~10.0** | **784 KB** |

(All three are derived, not measured — the only *measured* point on that line is
`2×2`. And the 0.46 ms composite term does grow slightly with the scale, by the
+0.11 pass above, which is another 0.065 ms and inside the rounding.)

**The speed axis has sharply diminishing returns and the memory axis has none.**
Quadrupling the upscale factor moves break-even by 21 % — because at `2×2` three
quarters of the fill is *already* saved and the fixed 5.02 ms is untouched —
while VRAM returned rises **75 %**, on a console with 4 MB of it where the
texture heap has ~1.7 MB free. Anyone reaching for a bigger factor should be
doing it because textures do not fit, never because the frame is slow.

#### Jitter stops meaning anything below `2×2`, and the rows above are jitter-off

Every row here was measured with the jitter **off**, which is what
`upscaler-lab` and every shipped example set. That is the right default for a
second reason at these scales, and it is structural rather than measured: the
engine writes a literal `±4/16` of a **low-res** pixel into `XYOFFSET` at every
raster scale (`renderer_core_blss.cpp`, `jitter16X`), so what the offset *means*
moves with the scale. At `2×2` it is ±½ an output pixel — two of the four output
pixel centres inside one low-res pixel, a genuine quincunx. At `4×4` there are
**16** sub-positions, the two phases reach 2 of them, and ±¼ of a low-res pixel
does not even land on an output pixel centre (those are at ⅛, ⅜, ⅝, ⅞).

**That argument is now measured, and it is exactly right.** Same harness with
`--jitter` forced on — the `2×2` row reproduces
[the trade curve's](#the-trade-curve) published jitter-on figure (+0.61, sd 0.51,
1/30, 1.73 passes) to the digit:

| | jitter **OFF** | jitter **ON** | what the jitter is worth |
|---|---|---|---|
| `2×2` | +0.33 (sd 0.34) | **+0.61** (sd 0.51) | **+0.28 dB** |
| `4×4` | +0.45 (sd 0.34) | **+0.45** (sd 0.26) | **+0.00 dB** |

**The quincunx bonus is worth a quarter of a decibel at `2×2` and nothing at all
at `4×4`** — two of sixteen sub-positions, sampled off the output grid, carry no
information the reconstruction can use. So a sub-half mode gives up the *only*
thing the jitter was ever paying for, while keeping the bob it costs.

**Do not enable `blssJitter` on a sub-half mode**, and if a sub-half mode ever
ships, the project settings should interlock the two the way `blssClashes`
already interlocks depth of field. This is the rare case where the interlock
costs the user nothing: the setting it disables has been measured to be worth
zero there.

#### The recommendation

**Ship `2×2` as the default, unchanged.** Nothing measured here moves it.

- **`4×2` is the only sub-half mode worth exposing**, and only for a project that
  is **VRAM-bound rather than fill-bound**: it hands back 672 KB instead of
  448 KB (+50 %) for about **1 dB** on the fixture with headroom — the only
  sub-half mode whose cost is inside the ~1 dB the feature's own ceiling is
  worth. It also lost **no** folds to plain bilinear, against `2×2`'s 2 of 30.
- **`2×4` should never ship.** Same fill, same memory, **1.25 dB worse**. There
  is no configuration in which it is the right answer, and the interlace
  intuition that recommends it is wrong.
- **`4×4` is for one situation only**: a project whose textures do not fit at
  all. 784 KB back, ~2.6 dB gone — nearly eight times the trained margin.
- **`1×2` still returns nothing** ([above](#at-12-the-vram-saving-is-exactly-zero-and-nothing-said-so))
  and is a picture choice only.

**What the engine side would need — nothing.** `setRasterScale` is generic and
`configure()` already clamps to ≥ 1, so a sub-half mode is a *project format and
codegen* change, not a renderer one:

| where | what |
|---|---|
| `src/project.hpp` / `project.cpp` | `blssScale` gains a third value (`2 = 4×2`); the loader's `if (st.blssScale > 1) st.blssScale = 1;` becomes `> 2 … = 2`. No format bump is needed — an older editor reading a `2` clamps it to `1×2`, which degrades gracefully rather than corrupting |
| `src/templates.cpp` | the `blssScale == 1 ? X1Y2 : X2Y2` ternary gains its third arm, and `BLSS_SCALE_X/Y` follow automatically |
| `src/blss_window.cpp`, `App::blssVramLine` | the Render scale combo gains the entry, the VRAM line already computes from `(sx, sy)`, and `--scale-1x2` in the window's argument builder becomes `--scale WxH` |
| project settings interlock | `blssJitter` forced off whenever `scaleX·scaleY > 4` — see the jitter note above |
| **the engine** | **nothing.** `RendererCoreBlss` is already generic in `scaleX`/`scaleY` |

Divisibility is clean where it matters: 512/4 = 128 and 448/4 = 112, Pal576i's
512/4 = 128, HiDef1080i's 448/4 = 112 and 540/4 = 135. The corpus prints a
warning when a scale does not divide the output, because a remainder means output
pixels sampling past the edge of the low-res target — the two twins describing
different frames.

> **`--scale 1x1` is the generalisation's own self-test, and it passes exactly.**
> With no reduction at all the composite must degenerate to the render it sampled
> — `(x + 0.5)/1` is the pixel centre, so the bilinear base tap is that pixel and
> nothing else. It does: on `examples/upscaler-lab` the `half-res + bilinear` row
> comes out **identical to `native full-res` in every column and every per-shot
> figure** (28.308 dB, flicker 19.47, and 28.590 / 28.026 per shot), and the
> period-2 table reads 0.000 on both. A sampling generalisation that was off by
> half a texel anywhere could not produce that. (The oracle still finds +0.90 dB
> of headroom at `1×1`, which is not a contradiction: the truth is supersampled,
> so the temporal kernel can beat a one-sample render even when there is nothing
> to upscale — the same reason
> [`native` is not a ceiling](#how-to-read-these-tables-and-what-not-to-read-into-them).)

### The proxy budget: what the cheaper frame description costs the network

**The host half of the twin contract's fifth rule is in, and it costs nothing
measurable.** The rule itself is the engine's — it is stated normatively in
[§2 of the math doc](blss-reconstruction.md) — and it says that describing a bag
with more boxes than it covers *tiles* buys nothing the accumulator can
represent, while costing a projection and a tile update per extra box. So the
cap becomes the bag's own tile footprint:

```
tiles = (cx1-cx0+1) * (cy1-cy0+1)   of the WHOLE bag's box, addBag's arithmetic
cap   = clamp(tiles, 1, 32)         = 32 when the whole box describes nothing
group = ceil(parts / cap)           the existing consecutive-part merge
```

It is **camera-dependent**, which is why the corpus applies it in `bagList()` per
frame rather than in `finishObject()` at build time: the same bag is worth 32
boxes across the screen and one in the distance.

**It ships OFF on both sides** — `TYRA_BLSS_PROXY_BUDGET` is 0 and
`--proxy-budget` is the host's opt-in — because a host that describes a frame
with 122 proxies while the console describes it with 187 is precisely the twin
drift that had this network [fitted to bounding
spheres for eleven commits](#where-a-bags-screen-box-comes-from-and-why-that-was-the-bug).
The two move in one commit or not at all.

What it costs the network, since the question was worth asking rather than
assuming — same protocol as everything above, `--cv --cv-seeds 5`, 120 frames,
**30 fold-runs per row**:

| | proxies/frame | margin | sd | below bilinear | passes | temporal occ. |
|---|---|---|---|---|---|---|
| `upscaler-lab`, off | **187.2** | +0.33 | 0.34 | 2/30 | 1.65 | 65.1 % |
| `upscaler-lab`, **on** | **121.8** (−35 %) | **+0.34** | 0.34 | 2/30 | 1.65 | 64.7 % |
| `procedural`, off | **281.7** | −0.04 | 0.09 | 15/30 | 1.16 | 15.6 % |
| `procedural`, **on** | **219.5** (−22 %) | **−0.04** | 0.09 | 15/30 | 1.15 | 15.5 % |

**A third of the frame description, for 0.01 dB** — one thirty-fourth of the
fold-to-fold sd, i.e. below what this instrument can resolve. Nothing else moves
either: the spread, the folds that lose to plain bilinear, the mean passes and
the per-kernel occupancy are the same to the digit, at deadzone 8 and at deadzone
0. The engine's own counts on its own fixture are 198 → 116 proxies and
262 → 174 projections for `proxy` 1.63 → 1.25 ms; the host's −35 % over six
camera moves is the same order and the same direction, and the two are not
expected to be equal because they are walking the scene differently.

**And the two sides agree on WHICH channel moves**, which is the check worth
having, because a description change that quietly altered a feature the network
leans on would not show up in a margin this project has plenty of.
`--blss-eval --features`, `upscaler-lab`, 26 880 tiles, off → on:

| | motion | depth | depthGrad | edgeDens | texDetail | **coverage** |
|---|---|---|---|---|---|---|
| host mean, budget **off** | 0.422 | 0.552 | 0.577 | 0.514 | 0.445 | **0.693** |
| host mean, budget **on** | 0.423 | 0.554 | 0.579 | 0.517 | 0.447 | **0.695** |

`coverage` moves up by 0.002 and everything else by ≤0.003 — the engine reported
`coverage` **0.631 → 0.638** on its own fixture and no movement anywhere else,
which is the same channel moving in the same direction by the same tiny amount.
(One thing the host sees that the engine's summary did not mention: `texDetail`'s
saturation falls, 7.8 % → 4.5 % of tiles at exactly 1.0. Merging boxes enlarges
the screen bbox, which is the denominator of the minification ratio, so a
slightly coarser description is a slightly *less* clipped `texDetail` — mildly in
the right direction for a page that keeps complaining about
[saturated channels](#what-is-still-open).)

**A screen-area floor is the idea to not have here**, and it was already rejected
on the engine side: a rule that can take a distant bag's proxy count to *zero*
hands its tiles `coverage = 0`, which the network reads as "there is nothing
here" rather than "there is something small here". The cap is never below 1.

### The sixth rule: emitter bags describe themselves

**The feature grid did not describe particles at all, and now it can — behind a
switch that ships off, because measuring it is what says whether to flip it.**
The rule is stated normatively in
[§2 of the math doc](blss-reconstruction.md); this section is the measurement.

The gap first, because it is larger than it sounds. A billboard bag runs
`frustumCulling = None` (VU1 culls per quad), so `StaPipCore` had no package
bbox for it, fell back to a radius-0 bounding sphere and `addBag()` threw the
empty box away. The corpus agreed by accident — `bagList()` only walked
geometry. **Both halves matched, and what they matched on was describing
nothing over 71.27 of 72.23 counted coverages on `upscaler-lab` (98.7 %) and
14.57 of 15.24 on `showcase` (95.6 %).** Every kernel the network chose over
fire, fog and rain, it chose from the geometry behind them.

The rule gives such a bag one box: the AABB over the particle centres it is
about to submit, grown per axis by the widest quad those centres expand into,
then projected exactly as a package box. It is **one box per bag and not one per
VU1 package** — a pool's order is its spawn order, which the corpus does not
simulate, and the AABB of a *set* is the one description that does not depend on
that order. `TYRA_BLSS_EMITTER_PROXY` and `--emitter-proxy`, both 0.

**What the console does with it.** PCSX2, `examples/upscaler-lab` at its parked
boot vantage, jitter off, 2×2, debug channels off, `blssDebugView 2`; the OFF
arm reproduces this page's published figures exactly (198 proxies of 262, 147
covered tiles, `coverage` 0.631, `BLSSFILL passes = 1.56`), which is the
regression check that the switch at 0 changes nothing.

| `BLSSGRID` / `BLSSFEAT` | off | **on** |
|---|---|---|
| proxies / projections | 198 / 262 | **207 / 273** |
| covered tiles | 147 of 224 | **224 of 224** |
| tile updates | ~1 495 | **~2 649** |
| `depth` min/mean/max | 0.000/0.461/1.000 | 0.292/**0.610**/1.000 |
| `depthGrad` | 0.000/0.455/1.000 | **0.899**/0.938/1.000 |
| `edgeDens` | 0.000/0.455/1.000 | 0.000/**0.614**/1.000 |
| `texDetail` | 0.000/0.466/1.000 | 0.069/**0.211**/0.789 |
| `coverage` | 0.000/**0.631**/1.000 | **1.000/1.000/1.000** |
| `BLSSWORST` | 36 of 224 tiles | **224 of 224**, `w 18.30..42.80` |

Nine of the eleven emitters are accepted (all eleven are projected), and they
touch about **128 tiles each** — the tile-update column nearly doubles for nine
boxes. `texDetail` is the channel that most clearly starts telling the truth: it
stops reporting the walls' and crates' textures and starts reporting `puff.png`
over its real screen footprint.

**And `coverage` becomes a constant.** `1.000/1.000/1.000`, min = mean = max, in
every tile of the frame; `depthGrad` follows it with a spread of 0.101. The
mechanism is exactly the one this feature already paid for once with the sky
dome, and §4's formula names it:
`depthGrad = clamp(max(neighbour delta, (depthMax − depthMin) * 8), 0, 1)` — one
AABB over a haze bank 24 world units deep reports **the whole bank's depth
range in every tile it touches**, so the term saturates everywhere. A constant
channel is a network making no per-tile decision. `--blss-eval --probe` says so
in as many words: the console's own vector goes from *one* constant channel
(`motion`, an artefact of the parked camera) to **two**.

**What it costs the EE.** Same two runs, `FTSPLIT`, means over 50-frame windows,
PCSX2 — admissible for the aggregate and the counts, **not** for per-function
attribution, which the emulator gets wrong in a known direction:

| term | off | on | Δ |
|---|---|---|---|
| `proxy` total | 1.656 | 2.139 | **+0.483** |
| …of which `accum` | 0.626 | 0.920 | +0.294 |
| `reproj` | 0.290 | 0.359 | +0.069 |
| `feat` | 0.090 | 0.091 | +0.001 |
| `net` | 0.795 | **1.143** | **+0.348** |
| `pkt` | 0.380 | 0.361 | −0.019 |
| **BLSS EE** | **3.211** | **4.093** | **+0.882 (+27 %)** |

Two of those terms are second-order effects worth naming, because neither is
about proxies: `net` runs the MLP per *covered* tile, so covering 224 tiles
instead of 147 costs 52 % more inference, and `reproj` averages over adjacent
covered tiles for the same reason. **Describing more of the frame makes the rest
of the pipeline do more work**, which is not what "one extra box per emitter"
suggests.

Carried into the break-even formula
(`0.7548 × 0.5174 × C > EE + composite`, 512×448) that takes the bill from
4.60 + 0.50 to ~5.48 + 0.50 and **break-even from 13.1 to ~15.3 full-screen
coverages** — the outcome the flag exists to make visible before it is flipped.
(The ON arm's `BLSSFILL` actually *falls*, 1.56 → 1.34 passes, but that is the
**shipped net running out of distribution** on inputs it was never fitted to,
not a saving anyone may bank.)

**What it costs the host, which is a cost and not the benefit.** The corpus
renderer still draws no particles, so with the flag on it predicts a frame whose
ground truth has none — `--blss-eval --cv` here prices the *description* against
a particle-free truth:

| project | flag | margin | fold sd | folds | below bilinear | passes (sd) | proxies/frame |
|---|---|---|---|---|---|---|---|
| `upscaler-lab` | off | +0.25 dB | 0.27 | 6 | 1 of 6 | 1.66 (0.24) | 187.2 |
| `upscaler-lab` | **on** | +0.23 dB | 0.20 | 6 | 0 of 6 | 1.73 (0.30) | 194.1 |
| `showcase` | off | +0.00 dB | 0.00 | 12 | 0 of 12 | 1.00 (0.00) | 169.9 |
| `showcase` | **on** | +0.00 dB | 0.00 | 12 | 0 of 12 | 1.00 (0.00) | 171.8 |

−0.02 dB against a fold sd of 0.20–0.27, i.e. **nothing this instrument can
resolve**, and exactly zero on `showcase`, whose oracle equals bilinear on most
folds either way. Read that as "adding the predictor costs the geometry fit
nothing", not as "it does not help" — the host cannot see the help. (Its
period-2 tables are *not* comparable across the two arms: the stability gate is
derived from the feature grid, so the two runs measure different pixel sets.)

**The twin checker works, and it can see drift** — which is the thing this repo
has to be able to do before either switch may move. Pasting the console's own
`BLSSFEAT` line into `--blss-eval --probe`:

| pairing | `coverage` support | `depth` band | `depthGrad` band |
|---|---|---|---|
| console **on** vs corpus **on** | **96.9 %** | **97.7 %** | **89.4 %** |
| console **on** vs corpus **off** | 67.9 % | 62.2 % | 42.6 % |

The matched pairing places the console's frame inside the corpus distribution;
the deliberately mismatched one loses a third of the support on `coverage` and
more than half the reachable band on `depthGrad`. That is the instrument
detecting exactly the class of divergence it exists for, on a real vector rather
than a hypothetical.

**The verdict, and it is a "not yet".** Describing an emitter with **one** box
makes three channels see the particles and turns a fourth into a constant, for
+0.88 ms of EE and +2.2 coverages of break-even. That trade is not worth
flipping.

#### …and the spatial split, which was the named next step and is now a measured NO

The paragraph above used to end "what would change it is splitting a pool
**spatially** — bin the centres by their coordinates, so each box carries a
*local* depth range". It was implemented on both twins and measured on
2026-08-09, and **it makes the two channels it was meant to rescue MORE constant,
not less.** The rule, its numbers and the reason are in
[§2 of the math doc](blss-reconstruction.md#a-seventh-rule-that-was-measured-and-rejected-the-spatial-split);
the short form is that the premise was wrong.

Host, `--blss-eval --features`, 156 frames / 34 944 tiles, jitter off, 2×2 — the
share of tiles reading exactly 1.000, which is what "a constant" means here:

| project | channel | flag off | one box | **split (8)** |
|---|---|---|---|---|
| `upscaler-lab` | `coverage` | 67.8 % | 96.9 % | **98.4 %** |
| `upscaler-lab` | `depthGrad` | 41.5 % | 87.8 % | **99.0 %** |
| `showcase` | `coverage` | 68.1 % | 78.9 % | **83.9 %** |
| `showcase` | `depthGrad` | 59.7 % | 76.0 % | **81.4 %** |

and the console agrees on the **real** pool (PCSX2, `upscaler-lab` parked at the
same vantage in both arms — `cam=3.1416`, `motion=0.000`, frames 2450–2650; the
one-box arm reproduced the published 207 proxies of 273 and 4.09 ms exactly,
which is what makes the pair comparable):

| | one box | **split (8)** |
|---|---|---|
| `BLSSGRID` proxies / projected | 207 / 273 | **241 / 310** |
| covered tiles | 224 of 224 | **224 of 224** |
| tile updates | 2 631–2 641 | **5 989–6 133** |
| `coverage` min/mean/max | 1.000/1.000/1.000 | **1.000/1.000/1.000** |
| `depthGrad` | 1.000/1.000/1.000 | **1.000/1.000/1.000** |
| `edgeDens` mean | 0.617 | 0.884–0.928 |
| `texDetail` mean | 0.211 | 0.133 |
| `BLSSWORST` | 224/224, w 18.73..42.80 | 224/224, w 18.30..24.09 |
| BLSS EE | 4.07 ms | **5.25 ms** |
| break-even @ 512×448 | ~15.3 | **~18.2** |

The split does exactly what it was designed to do — the worst proxy's depth
range more than halves (42.80 → 24.09) — and **not one covered tile changes**,
because of a geometric identity nobody had checked: **a partition of a solid
region is a tiling of that region, and a tiling has the same union.** A Tyra
emitter's pool *is* solid on both machines (`updateParticles` spawns uniformly
over the emitter's own XZ rect and integrates one velocity; `emitterCentres`
models the same box with a Halton pool), so there are no clusters to find and
no empty space to stop claiming. All the split can add is boxes — and their
extra bbox edges, which is `edgeDens` 0.617 → 0.9.

**And the constant was never the rule's fault.** Strip `upscaler-lab` to ONE
small fire emitter instead of eleven and re-run all three arms:

| | flag off | one box | split (8) |
|---|---|---|---|
| `coverage` mean / % at 1 | 0.690 / 67.8 % | 0.690 / 67.8 % | 0.690 / 67.8 % |
| proxies/frame | 187.2 | 187.7 | 189.3 |

Nothing becomes constant, at either cluster count. `coverage = 1.000` on the
shipped fixture is the **truth about that fixture**: `--blss-coverage` counts
71.27 of 72.23 full-screen coverages as emitters there, so the particles really
do blanket every tile many times over. The sixth rule describes a sparse emitter
perfectly well; what it cannot do is report a haze soup as anything but covered.

So the lever is spent, and it is spent by measurement rather than by argument.
What is left for the emitter half is unchanged and unblocked: **the corpus
renderer still draws no particles** — `docs/backlog.md`.

### The transcendentals, as a table

**This one is free, and it is the recommendation.** `runNet` evaluates
`12 tanhf + 3 expf + 3 fdiv` per tile — **3 360 libm calls and 672 divides per
frame** at 224 tiles — in a build with **no `-ffast-math`**
(`vendor/tyra/Makefile.base:19` is `-D_EE -Wall -O3`). Every one of those results
is then truncated to a byte by `cornerAlpha()` and snapped to zero below alpha 8.
Full libm precision is computed and thrown away.

A shared lookup table replaces all of it, and **one table serves both
activations**, because `logistic(z) = (1 + tanh(z/2)) / 2` exactly — so the
divide goes with the `expf`. The exact definition, index mapping, clamping,
rounding rule, integer type and checksum are
[§5 of the math doc](blss-reconstruction.md#the-activation-table--the-contract-not-yet-the-code);
that page is the contract, this one is the measurement.

**Why a table rather than a polynomial**: an approximation has to be *matched*
between two compilers and a table does not. `int16 -> float` and `× 2^-15` are
both exact, and the lookup is nearest-entry with no interpolation, so the
activation step is **bit-identical on the two twins by construction** rather than
by tolerance. (The float MACs around it stay whatever the EE FPU makes of them.
The table does not fix that and does not claim to.)

Measured the same way as everything else — `--blss-eval --cv --cv-seeds 3`,
39 fold-runs per row, `--act-table N` on both the fit and the inference:

| activation | stored | held-out margin | sd | below bilinear | passes | point | temporal |
|---|---|---|---|---|---|---|---|
| `std::tanh` / `std::exp` (shipped) | — | **+0.42** | 0.35 | **3/39** | **1.80** | 10.3 % | 69.6 % |
| table, N = 512 (**recommended**) | 514 B | +0.41 | 0.34 | 3/39 | 1.79 | 10.8 % | 68.2 % |
| table, N = 256 | 258 B | +0.41 | 0.34 | 3/39 | 1.80 | 11.5 % | 68.5 % |
| table, N = 128 | 130 B | +0.43 | 0.36 | 3/39 | 1.80 | 10.7 % | 69.3 % |
| table, N = 64 | 66 B | +0.41 | 0.34 | 3/39 | 1.77 | 7.3 % | 69.2 % |
| table, N = 32 | 34 B | +0.41 | 0.34 | **5/39** | **1.84** | 10.1 % | 74.1 % |

> **The delta is 0.01 dB against a fold sd of 0.35** — one thirty-fifth of the
> noise, and *smaller than the 0.02 dB `--drop-feature edgeDens` control this page
> uses to decide whether an effect is resolvable at all*. It is not that the table
> is cheap; it is that this instrument cannot tell it apart from libm. Occupancy,
> the pass count and the number of losing folds are all unchanged.
>
> **The size at which it stops being free is 32, not 512.** Every row from 64 up
> reproduces the shipped one; at N = 32 — where nearest-entry lookup can be
> 0.125 out in `tanh` — the losing folds go 3 → 5 and the frame pays 0.04 of a
> pass more, which is the first thing on this table that moves at all. That is a
> 4× margin between "measurably fine" and the recommendation.
>
> **Ship 512 anyway.** The whole table is 514 bytes of `.data` against a 224-tile
> grid whose own accumulators are 30 KB, so there is nothing to buy by shaving it,
> and a comfortable margin over the point where the answer starts to wobble is
> worth more than 448 bytes. Reporting the small rows is the useful half: it says
> the result is not balanced on a knife edge.

**What it saves, per frame, at the shipped `kTile = 32`:**

| per frame | libm | table |
|---|---|---|
| `tanhf` | 2 688 | **0** |
| `expf` | 672 | **0** |
| `fdiv` (the logistic's `1/(1+e)`) | 672 | **0** |
| table lookups (clamp, multiply, add, convert, load) | — | 3 360 |
| MACs | 24 192 | 24 192, unchanged |

(Worst case; a tile below `kMinCoverage` skips the evaluation on both twins, so
scale by the covered fraction — ~0.8 in the one instrumented console frame.
**With `kTile = 64` as well it is 840 lookups and 6 048 MACs.**)

**AND IT IS NOW MEASURED ON A CONSOLE-SHAPED FRAME, not argued from an
instruction count.** PCSX2, the `blssrig` fixture, 65 paired 50-frame windows,
the engine's own `tBlssNet` counter: `runNet` goes **3.39 → 1.29 ms**, i.e.
**−2.11 ms** with a 95 % CI of [−2.10, −2.12] and no measurable movement in any
other counter. That is the **largest single EE saving available to this
feature** — larger than the entire bag-proxy feed — and the reason is specific:
newlib's `tanhf`/`expf` compute in **double**, the EE has **no double-precision
FPU**, and so each of the 15 activations per tile is a software-emulated round
trip of roughly 340 EE cycles. The 1.29 ms that remains is the 108 MACs a tile,
which no table can touch.

**It was also, until 2026-08-08, not actually wired.** The engine's `actTanh` /
`actLogistic` existed, were hashed and were documented, and `runNet` called
`tanhf`/`expf` directly — so `TYRA_BLSS_ACT_TABLE` controlled nothing. `runNet`
calls them now; the measurement above is what the switch is worth.

**It is still not switched on.** `--act-table` defaults to 0, i.e. libm, because
**`blss.net` records nothing about which activation fitted it**, so a host that
trains against a table while the console evaluates libm is a silent twin
divergence with no file to detect it. Switching it on is a two-line commit —
`TYRA_BLSS_ACT_TABLE` 0 → 512 in `renderer_core_blss.cpp`, and `int actTable =
0;` → `512` in `src/blss.cpp`'s option parsing — followed by a `--blss-eval -i`
parity run. Both lines or neither.

**No `kNetVersion` bump.** The topology and the file format are untouched, and
bumping it would refuse every existing net to guard a difference of 0.01 dB.

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

**RETRACTED (2026-08-08, later the same day).** The paragraph below stood here
for a few hours and every sentence in it is wrong. It is kept because it is the
third time an instrument on this page reported a still picture at a shaking
console, and the shape of the error is the transferable part:

> ~~**Re-measured 2026-08-08 on the fixed build, and it does not reproduce on the
> shipped fixtures.** Same method, frozen camera, PCSX2 software renderer,
> captures at a non-frame-locked 0.29 s stride: `examples/upscaler-lab` (jitter
> **on**, its own net) and `blssbug` both give consecutive pictures that are
> **byte-identical below the HUD** - 0 differing pixels in the rendered image, the
> only motion being the HUD digits and the PCSX2 status bar. The same fixture on
> the *pre-fix* engine measures the same, so the z-mask defect was not the cause
> either. The table above therefore describes a **net**, not the feature: with a
> project-trained net whose fill term culls the temporal pass there is nothing
> fusing the two phases, and that is when it bobs.~~

`upscaler-lab` **does** reproduce it, on the shipped net, on the fixed build.
See [the A/B/C below](#abc-a-human-looked-at-three-builds-2026-08-08).

**30.8 % of the picture alternates between two images every frame**, on a net
trained on the project's own scenes. Why the fill term did not save it: it culls
point and sharpen, but on a real project's scenes it culls the **temporal** pass
too — and the temporal accumulator is the only thing entitled to fuse the two
phases. The remaining bilinear base pass still reconstructs from a low-res
render that sampled different scene points each phase. The shipping
configuration was "jitter on, nothing fusing it".

#### A/B/C: a human looked at three builds (2026-08-08)

**This is the measurement that settles the section, and it is the cheapest one
on the page.** Three builds of `examples/upscaler-lab` — real content, the
shipped net, a camera frozen at the tour's haze vantage, differing in *nothing*
but the two flags — handed to a person, who was asked which ones shake:

| build | configuration | a human, watching | the instrument, below the HUD |
|---|---|---|---|
| **A** | `blssEnabled: false` | steady | 40 captures **byte-identical** |
| **B** | `blssEnabled: true`, `blssJitter: true` | **"like an earthquake"** | **two clusters, 18/22, within 0.0000, between 1.15/255** |
| **C** | `blssEnabled: true`, `blssJitter: false` | steady | 40 captures **byte-identical** |

So, in order, what this retracts and what it establishes:

- **The jitter is the cause and turning it off is the cure.** Not a theory any
  more: B and C differ in one boolean and one of them shakes.
- **The fill term did not fix it.** Row B is the current build, fill term and
  z-mask fix included.
- **Heavy temporal weighting does not fuse the phases.** `upscaler-lab`'s own
  net puts 72–78 % of its weight on the temporal pass — the configuration this
  page hoped would be self-fusing — and B still shakes. The temporal
  accumulator being *asked for* is not the same as it converging.
- **It is not net-dependent in the way the retracted paragraph claimed.** The
  shipped net does it.
- **It is not a displacement.** The instrument's integer cross-correlation lag
  between consecutive captures is `(0,0)` and the sub-pixel row shift is
  0.14 px: the picture does not move. What alternates is the *resample*, on
  **16.3 % of the pixels below the HUD**, p99 13.8/255 and peak 40/255, and the
  difference image lights up every textured edge in the frame — wall, cobbles,
  fence rails, buildings — while the flat sky stays black. A period-2 change of
  that extent at the field rate is what "the screen is shaking" looks like from
  a chair.
- **And that last point is why `blssbug` was never going to show it.** An
  untextured box on flat ground has almost no surface that a quarter-pixel
  resample can change. A fixture that cannot exhibit the artefact measures
  clean, truthfully, about a case that is not the user's.

**The kill switch, now the default.** `blssJitter` (`ProjectSettings`, default
**false** since this measurement) pins the offset to 0: pure spatial upscale, no
temporal supersampling, stable by construction. A feature that visibly shakes
the screen is unshippable whatever its decibels, so the default is the
configuration a person can look at — and the price is the 0.43 dB in the table
below, which is roughly two thirds of the available reconstruction. It stays a
setting because that trade is right to *default* and wrong to *force*: content
that does not show the shake should be free to buy the samples back. It reaches
the engine as `BLSS_JITTER` → `RendererCoreBlss::configure(..., jitter)`; the
engine parameter still defaults to `true`, so previously generated games are
unchanged.

**A project saved before the key existed now opens with it off** — the one
deliberate exception to this repo's "an older file opens byte-identical" rule
(`src/version.hpp`), because the behaviour it declines to preserve is a
flickering picture.

**`examples/upscaler-lab` ships it off too, since 2026-08-09.** It spent a day
shipping `"blssJitter": true` on the argument that its committed `blss.net` had
been fitted with the jittered sampler and the trainer reads the project's own
flag, so flipping the flag alone would leave it running a net fitted for a
sampler it no longer used. That argument was sound and the conclusion was
backwards: **retrain the net, do not ship the shake.** The example is the
project's flagship demo, and its README told the reader to edit the `.tyra`
before showing it to anyone — a default nobody can use as it stands. The flag is
now `false`, `blss.net` is refitted against the un-jittered sampler
(+0.85 → +0.51 dB trained, ceiling +1.730 → +1.058 dB, 49 % → 48 % of it — measured
on the fixture's pre-CC0 geometry; the jitter-off ceiling reads +1.108 dB on the
current one), and
the captures say the shipped build does not alternate. Build **B** of the A/B/C
is one line in that `.tyra` plus a retrain, documented in its README.

**FIXED — the host twin knows about it now.** That paragraph used to end "the
oracle and the corpus always model the jittered sampler, so a net trained today
and run with `blssJitter: false` is being run slightly out of distribution", and
it was right to flag it: a net fitted against a sampler the generated game does
not use is fitted out of distribution, and `blss.net` records nothing that could
detect it — the same shape as the whole-bag proxy and the animated models.
`blss::jitterEnabled()` / `setJitter()` are the twin, `jitterX`/`jitterY` return
0 in both phases when it is off, and **`--blss-train <projectDir>` /
`--blss-eval <projectDir>` read the project's own `blssJitter` and fit against
the sampler that project will ship with.** `--no-jitter` / `--jitter` force it
either way, the bestiary keeps it on because that is what every fold table here
was measured with, and the corpus prints a line when it is off. The contract is
[§1 of the math doc](blss-reconstruction.md#1-jitter), which is why that section
now opens by saying jitter is a mode rather than a constant.

**And the switch has a price, which is what the twin was for.** Both arms fitted
and measured with the same sampler, `--blss-eval --cv --cv-seeds 3` on
`examples/upscaler-lab`, 6 shots × 3 seeds = 18 fold-runs:

| `examples/upscaler-lab` | held-out margin | sd | below bilinear | passes | temporal |
|---|---|---|---|---|---|
| `blssJitter` on | **+0.69** | 0.50 | 0/18 | 1.74 | 73.7 % |
| `blssJitter` off | **+0.26** | 0.29 | 3/18 | 1.66 | 65.8 % |

**Replicated 2026-08-09 at a bigger N, and it held**: 5 seeds × 6 folds =
**30 fold-runs**, 120 frames, same objective, reads **+0.61** (sd 0.51, 1/30
below bilinear, 1.73 passes) with the jitter on against **+0.33** (sd 0.34, 2/30,
1.65 passes) with it off. Same ordering, same ~0.3 dB gap, same fill. Two
independent draws agreeing at this feature's noise level is worth writing down,
because most of the numbers this page has retracted were single draws.

**0.43 dB — the one number on this page bigger than its own fold sd.** The
scene's *ceiling* moves with it too: the oracle's headroom over bilinear falls
from +0.84 dB to +0.27 on the same shots, which is the honest statement of what
is lost. Without jitter the current frame and the history are the same sample,
so the temporal pass stops being a supersample and becomes plain accumulation,
and what BLSS has left is spatial kernel selection. That is a real cure for a
real bob, and it costs most of the reason to run the feature.

### "Measured is not optimised", eight times

**This is the most useful thing on this page.** The same mistake was made eight
times, each time one level further up, and each time it cost a debugging session.
The first four are one sentence: *anything absent from the objective does not
exist for the network.* The fifth is the same sentence about the **measurement**
rather than the objective, and it produced the most wrong text. The sixth is the
same sentence about **what was being measured on**, and it is the one that had
been running longest. The seventh is the same sentence about **the instrument
built to catch the other six**, which is the strongest argument on this page that
the rule is structural rather than a run of bad luck. The eighth is the same
sentence about the **CONFIGURATION** — a table whose rows were taken under two
different samplers, printed as one row.

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

7. **the replacement metric could not see the artefact either, twice, and it was
   written specifically to avoid this mistake.** `period2Alternation()` is the
   motion-compensated second difference that `flicker` structurally cannot be —
   and its first build read a **3.56-level floor** under a 1.42-level artefact,
   because motion compensation has its own error and this reprojection field is
   255 UVs for a whole frame. Gated on warp length it read a clean floor and then
   turned out to be measuring the **animated models**, which a camera-derived
   reprojection never compensates.

   Both were caught the same way, and it is the only reason this entry is a note
   rather than a retraction: **the metric was validated against two known cases
   before anything was optimised against it** — `examples/upscaler-lab` with the
   jitter on, which a human calls shaking, and the same scene with it off, which
   the console captures byte-identical. A metric that scores those two the same
   is not measuring the artefact, whatever its derivation says.

   The rule that follows, and it is the general form of all seven: **a new
   instrument is not evidence until it has separated two cases whose answer you
   already know.** Deriving it correctly is not enough — entries 1, 2, 3 and this
   one were all derived correctly and all measured the wrong thing.

8. **the row that set this feature's shipping rule had two samplers in it, and
   the question it answered was never the question.** *"Fit the project you will
   ship, and ship that net"* rested on one table:
   **−0.40 / +0.06 / +0.77 dB** on `examples/procedural`. Every number in it is
   reproducible and the row is still wrong twice over.

   - **The ceiling was measured with the jitter ON and the margins with it OFF.**
     Re-run today, `examples/procedural` reads **+0.773 dB** at jitter on and
     **+0.345** at jitter off, and the project ships jitter off. So the two
     margins were being read against a ceiling more than twice the one they could
     ever have reached, which is what made **+0.06 dB look like a poor result
     with room above it** instead of what it is — a net correctly declining to
     spend fill on a scene with a third of a decibel in it. Nothing recorded
     which sampler the row came from, because `blssJitter` did not exist when it
     was taken and there was only one.
   - **And "bestiary versus one project" is not "can I ship one net".** The
     experiment that answers that is leave-one-**project**-out, and when it was
     finally run it found the opposite of what the rule assumed: a net trained on
     **the bestiary AND six other projects** is a tie with the held-out
     project's own net (+0.29 against +0.31, fold sds 0.37 and 0.34), while the
     bestiary alone is −0.34 dB on average and the six projects alone degenerate
     to bilinear. **The rule generalised from a corpus of one to a policy**, and
     the missing arm — the two corpora together — was never tried.

   The rule that follows: **every number needs its configuration attached, and a
   comparison needs its configuration to be the SAME.** This page already
   demanded that of `--tile` and `--scale`, and prints an announcement line for
   both; `blssJitter` became a third such knob and no line was added. It is
   announced now, by `generate()`, on every run.

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
nothing moved". And it is blind on top of that: a **lag-1** difference is equally
large for a picture alternating between two images and for a smooth pan, so it
cannot see a period-2 artefact at all.

The knob, the term and the CLI flag all stay. **Setting a weight to zero after
measuring it is not the same as deleting it**, and the next person to have this
idea should find the measurement instead of re-running it. It is reachable as
`--flicker-form lag1`.

This section used to end "**fix the form first**: gate the penalty on
reprojection confidence so it cannot be paid by freezing." That was done. The
next section is what it measured.

### Fixing the form: the period-2 objective, swept

**The form was fixed, the metric was rebuilt twice to be able to judge it, and
the answer is that the objective cannot buy the stability at a price worth
paying.** Jitter-off is now the *measured* answer rather than a retreat, and this
section exists so nobody runs this again.

#### The new term, and why freezing is not expressible in it

`--flicker-form period2` charges for the **stationary alternation the candidate
weights would leave**, derived rather than sampled so it can live in the oracle's
innermost loop (`altAmplitude()` in `blss.cpp`). The console alternates two jitter
phases forever, so with `c = aC/128` the temporal blend, `P_p` the base+point
result of phase `p` and `A_p` the unsharp mask — which lands *after* the
accumulator and so is never fed back through it:

```
out_t = S_p( (1-c) * P_p + c * out_{t-1} )
out_0 - out_1 = [ (1-c) * (P_0 - P_1)  +  (A_0 - A_1) ] / (1 + c)
```

Three things fall out of that one line and all three are load-bearing:

- the base and point passes' phase difference **is** damped by the accumulator,
  by `(1-c)/(1+c)` — 1.00 at `c = 0`, 0.73 at alpha 20, 0.054 at the
  `kTemporalMax` ceiling. Temporal weight is the only cure for it, so **a fill
  term that culls the temporal pass makes the bob worse, not better**;
- the sharpen pass's phase difference is **not damped at all**. The only way to
  remove it is `aD = 0`;
- nothing in it is a difference against the history, so **freezing is not
  expressible**. The only ways down are fusing the phases (raising `c`) and not
  amplifying them (lowering `aA`/`aD`), and both are real cures.

**The gate that was designed for it was measured wrong before it was ever swept,
and that is worth more than the sweep.** The first version multiplied the charge
by `(1 - motion) * (1 - depthGrad)`, reasoning that `motion` at 1.0 puts the
history a whole tile away and `depthGrad` at 1.0 is a silhouette. Both readings
are right about what the channels *mean* and wrong about what they *contain*: on
`examples/upscaler-lab`, `--blss-eval --features` over 5376 tiles reads `motion`
at **exactly 1.0 on 49.1 %** of them and `depthGrad` on **41.0 %**. That product
is zero on most of the frame, and zero specifically on the moving, geometrically
busy part — which is exactly where the console's difference image lights up. It
would have swept as "buys nothing", which is the same non-answer `--flicker-weight`
already gave twice. **A gate built out of saturated channels is a gate that is
always shut.** Outliers are handled where they belong instead: the charge itself
is clamped at `kAltClamp = 8` levels, which bounds a disocclusion *and* flattens
its gradient, so an outlier costs a constant and therefore buys nothing.

#### The metric, and the two things that had to be fixed before it could judge anything

`flicker` is a lag-1 difference and cannot see a period-2 artefact. The
replacement is the **motion-compensated second difference**,
`|O(t) - 2*O(t-1) + O(t-2)| / 4` — exactly zero for anything moving at a constant
rate, and the alternation amplitude for a bob. Both predecessors are warped into
frame `t` first. It is printed under the `--blss-eval` table and as a per-fold
column in `--cv`, always with a **native full-res row as its own floor**, because
a metric without its own residual printed next to it is a number nobody can read.

It did not work the first time, and it did not work the second time. Both
failures are the same shape as everything else on this page — *the instrument
measured something other than the artefact* — and both were caught by validating
against the two known cases before optimising against it, which is the only
reason this section is not another retraction.

**(1) Motion compensation has its own error, and ungated it is larger than the
artefact.** 36 frames, held-out split, shipped net, the fixture a human called
"like an earthquake" with jitter on and "steady" — byte-identical on hardware —
with it off:

| ungated | jitter ON | jitter OFF |
|---|---|---|
| native full-res | 3.564 | 3.564 |
| half-res + bilinear | 3.346 | 2.964 |
| half-res + BLSS | 2.687 | 2.398 |

A floor of 3.56 levels under an artefact the console measured at 1.42, and the
**known-still arm reading 2.40 where the console captured zero**. The floor is
the warp: this reprojection field is per tile *corner* (255 UVs for the whole
frame), half these tiles move a full tile per frame, and a second difference
warps twice. So a pixel now counts only when **both** of its warps are under a
gate length, which is the honest twin of the console experiment — what a frozen
camera is, on footage that moves, is the pixels whose warp is short enough to
trust. Every gate is accumulated in one pass and the whole sweep is printed,
because a gated number whose gate is not shown is a magic constant.

**(2) The gate exposed a second contaminant: the animated models.** The
reprojection field is built from camera matrices, so geometry that moves *on its
own* is never compensated, and at a short camera warp its pixels sail through the
gate carrying a large difference. Same 120-frame corpus, `<=1px` gate, with and
without the four animated parts:

| `<=1px` gate | with animation | `--no-anim` |
|---|---|---|
| native full-res (the floor) | 2.614 | **0.075** |
| half-res + bilinear, jitter ON | 2.779 | 0.136 |
| half-res + bilinear, jitter OFF | 2.388 | **0.065** |

With the animation out, the metric behaves exactly as it should: the jitter-off
arm sits **at or below the native floor** — the byte-identical result the console
captured — and the jitter-on arm sits about **2x above it**. With the animation
in, the floor is 35x larger and swallows the artefact whole. **`--no-anim` is the
configuration in which this metric discriminates**, and the animated corpus is
the one the network must be trained on, so the two cannot be the same run. That
is a limitation of the instrument, stated here rather than discovered later.

For the sweep below the readable statistic is therefore not the mean level but
the **fraction of gated pixels alternating by at least 2/255** — the same
threshold and the same convention the console capture reported its 16.3 %
against. It pins jitter-off to the native floor on the animated corpus too, which
the mean does not.

#### The still fixture, and what it showed the metric's floor was

**FIXED (2026-08-09). The limitation above was the fixture, not the objective,
and `--still` removes it — the animated corpus no longer needs `--no-anim` at
all.** This page prescribed the fix and then had to be told it was a change to
`blss::generate()`; it is one, and it is eleven lines: every frame of a shot uses
the shot's **first camera and first pose**, so the only thing that advances
between consecutive frames is the jitter phase. That makes the reprojection the
**identity**, which makes the warp gate keep **100 % of the frame** instead of
29.6 %, and it freezes the animation, which is what was contaminating the
measurement in the first place. It is the console's frozen-camera experiment,
exactly rather than approximately.

`examples/upscaler-lab`, 30 frames, `--still`, held-out split, **animated**
(8-bit levels, and every gate column is now identical because there is no warp
to gate):

| `--still` | jitter **ON** | jitter **OFF** | ratio |
|---|---|---|---|
| `native full-res` — the floor | **0.095** | **0.095** | — |
| half-res + point | 6.268 | 0.069 | 91× |
| half-res + **bilinear** | **4.434** | **0.055** | **81×** |
| half-res + temporal | **0.263** | 0.058 | 4.5× |
| half-res + sharpen | 6.062 | 0.068 | 89× |
| half-res + oracle | 0.979 | 0.055 | 18× |

Four things that fall out, and the first is the point of the exercise:

- **The floor was the fixture.** 2.614 levels with the camera moving,
  **0.095** with it still — on the *same animated corpus*. The artefact is now
  **47× the floor** (4.434 against 0.095) where before it sat *below* it. And
  `--still --no-anim` reads **6.264 / 4.432 / 0.263 / 6.058 / 0.977** — the same
  numbers to within 0.004, so **with the camera frozen the animation contributes
  nothing** and the `--no-anim` workaround is retired. The mean level is a
  readable statistic again; the "fraction above 2/255" convention stays as the
  thing the console capture is directly comparable to, not as a crutch.
- **The temporal pass is what fuses the phases, measured rather than derived.**
  4.434 → **0.263**, a 17× reduction, from turning one kernel on. That is
  [`altAmplitude()`'s](#the-new-term-and-why-freezing-is-not-expressible-in-it)
  `(1−c)/(1+c)` with a number attached, and it confirms the mechanism that
  retracted "the fill term fixed the bob": **a fill term that culls the temporal
  pass makes the bob worse**, because that pass is the only thing damping it.
- **Sharpen is worse than doing nothing, also as derived.** 6.062 against
  bilinear's 4.434 — the unsharp mask lands *after* the accumulator, so it is
  divided by `(1+c)` and no more. Point (6.268) amplifies it too.
- **The oracle leaves 0.979 where an all-temporal composite leaves 0.263.** With
  `--flicker-weight 0` the oracle is optimising accuracy and fill and nothing
  else, so that gap is exactly what the shipped objective trades away in
  stability — the quantity [the trade curve](#the-trade-curve) was trying to
  price with a much blunter instrument.

`--still` is a **fixture, not a corpus**: every frame of a shot is the same
frame, so `--blss-train` and `--blss-eval --cv` both refuse it outright rather
than fitting a net to `shots` distinct examples repeated. Read the period-2 table
and nothing else from it.

#### The trade curve

`examples/upscaler-lab`, `--blss-eval --cv --cv-seeds 5`, 120 frames over 6
shots, 400 epochs, decay `1e-4`, fill 16, deadzone 8 — **30 fold-runs per row**.
Jitter **ON** except the last row. "alternating" is the fraction of gated pixels
at or above 2/255; the native floor for that column is **12.4 %**.

| `--flicker-weight` (period2) | held-out margin | sd | folds below bilinear | passes | alternating |
|---|---|---|---|---|---|
| 0 | **+0.61** | 0.51 | 1/30 | 1.73 | 14.8 % |
| 0.05 | +0.62 | 0.52 | 1/30 | 1.73 | 14.8 % |
| 0.2 | +0.63 | 0.53 | 0/30 | 1.73 | 14.9 % |
| 0.5 | +0.61 | 0.55 | 0/30 | 1.72 | 14.9 % |
| 1.5 | +0.55 | 0.85 | 6/30 | 1.73 | 14.9 % |
| 2 | +0.48 | 1.01 | 6/30 | 1.74 | 14.9 % |
| 3 | +0.44 | 1.13 | 12/30 | 1.74 | 14.4 % |
| 4 | +0.35 | 1.32 | 15/30 | 1.75 | 13.0 % |
| 5 | +0.29 | 1.40 | 15/30 | 1.75 | **12.4 %** |
| `--flicker-form lag1` 0.2 | +0.59 | 0.75 | 11/30 | 1.78 | 14.8 % |
| **jitter OFF**, weight 0 | **+0.33** | 0.34 | 2/30 | 1.65 | **12.3 %** |

**The answer, in one line: the alternation only reaches the jitter-off floor at
weight 5, and at weight 5 the margin is +0.29 — *below* the +0.33 that turning
the jitter off buys for free.** There is no point on this curve where jitter-on
is both as stable as jitter-off and worth more.

What the curve says on the way there, and none of it is a near miss:

- **It is not a fill trade.** Mean passes moves 1.73 → 1.75 across the entire
  sweep. The feature's EE bill and its ~13-coverage break-even are untouched, so
  nothing here was bought or lost at the fill. What moves is generalisation.
- **The variance is the real price.** Fold-to-fold sd goes 0.51 → 1.40 and the
  number of folds that lose to plain bilinear goes **1/30 → 15/30**. At the
  weight that fixes the bob, *half the content is worse off than not running the
  feature at all*. A mean of +0.29 with that spread is not a feature.
- **Everything below weight 1.5 is free and useless.** 0.05 through 0.5 move the
  margin by less than the sd of its own mean (0.09 dB) and move the alternating
  fraction by 0.1 point. The knob has no cheap setting: it does nothing until it
  does damage.
- **The form change was still worth making, and this is the evidence.** At the
  same weight 0.2, `lag1` loses 11 folds of 30 to bilinear and period2 loses
  **none**, at the same margin. The old term was hurting generalisation without
  buying stability; the new one at least does nothing harmlessly. That is the
  narrow sense in which "fix the form first" was right — and it did not change
  the verdict.
- **It is not the deadzone or the net capacity.** Nothing else in the
  configuration moved between rows; the only difference is one scalar in the
  oracle's objective.

#### The second project could not confirm it, and says so

`examples/procedural` was the intended replication and **it cannot discriminate
anything at this configuration**, which has to be stated rather than quietly
dropped. Same harness, same 30 fold-runs:

| `examples/procedural` | margin | sd | below bilinear | passes | alternating |
|---|---|---|---|---|---|
| jitter ON, weight 0 | −0.04 | 0.28 | 15/30 | 1.30 | 0.5 % |
| jitter ON, weight 3 | +0.00 | 0.36 | 15/30 | 1.41 | 0.5 % |
| jitter OFF, weight 0 | −0.04 | 0.09 | 15/30 | 1.16 | 0.3 % |

**Half its folds lose to plain bilinear whatever you do**, and the alternating
fraction is 0.5 % against a 0.3 % floor — there is almost no bob in it to remove
and no margin to trade. It joins `examples/showcase` on the list of fixtures that
[cannot answer this kind of question](#how-to-read-these-tables-and-what-not-to-read-into-them).

> **RESOLVED (2026-08-09), and the flag was the error rather than either
> number.** This paragraph used to say the table above "contradicts a number
> already on this page, which said `procedural` scores +0.77 with the jitter on
> and +0.33 without it… one of the two is wrong". **Neither is wrong. They are
> not the same quantity, and the flag also misattributed a third figure.**
>
> - **+0.77 is an ORACLE CEILING**, not a network's score — the upper-bound row
>   of [the project-net table](#training-on-your-own-project), 72 frames, jitter
>   on. Re-measured with `--blss-eval examples/procedural --frames 72 --jitter`
>   it reads **`headroom=+0.773 passes=1.20`**, reproducing the published
>   +0.77 / 1.20 to three digits and the pass count.
> - **−0.04 is a TRAINED NET's cross-validated held-out margin**, jitter off. The
>   page already carried its sibling and always has:
>   [six camera moves over one scene read −0.17 dB, 9 of 18 fold-runs below
>   bilinear](#training-on-your-own-project). An upper bound and an
>   out-of-distribution margin are separated by exactly the gap the network fails
>   to close, which on this project is most of it.
> - **The jitter accounts for the rest.** `procedural`'s oracle ceiling is
>   **+0.773** with the jitter on and **+0.345** with it off (72 frames;
>   +0.937 / +0.382 at 120), the same near-halving `upscaler-lab` shows
>   (+1.73 → +1.06). The flag's "+0.33 without it" is not a `procedural` number
>   at all — it is `upscaler-lab`'s jitter-off row **in the table above this
>   one**, which `procedural`'s +0.345 sits close enough to to be mistaken for.
>
> **What was actually missing was the configuration, not the correctness**: an
> oracle ceiling quoted without its sampler, next to a margin quoted without its
> row. Both are stated now. The conclusion of this section is unchanged and still
> rests entirely on `upscaler-lab`; what changes is that `procedural`'s numbers
> are no longer under suspicion.

**So the default stays `--flicker-weight 0`, and `blssJitter` stays off.** The
knob, both forms, the metric and this table all ship, because the whole reason
this measurement exists is that the same idea was proposed three times.



### What actually delivered the stability

**RETRACTED (2026-08-09): nothing in this subsection delivered stability, and the
mechanism it proposed is backwards.** It is kept because the numbers in it are
real and because the error is the instructive part — the flicker column it reads
improvements off is a **lag-1** quantity, which cannot see a period-2 artefact,
so "flicker fell from 21.49 to 21.01" was never evidence about the bob.

The mechanism, from [the derivation](#the-new-term-and-why-freezing-is-not-expressible-in-it):
the accumulator is the **only** thing that damps the base and point passes' phase
difference, by `(1-c)/(1+c)`. The fill term culls the temporal pass along with the
others, so on a real project's scenes **it removes the one pass that was fusing
the phases** — it makes the bob worse, not better. The console agrees: with the
fill term in and a project-trained net, 30.8 % of a frozen frame alternated at
1.42/255, and the A/B/C above is a human calling that build "like an earthquake".
The sharpen pass is worse still: it is applied *after* the accumulator, so no
amount of temporal weight damps it at all and only `aD = 0` removes it.

What actually delivered the stability is **turning the jitter off**, which is why
that is the default. The fill term's own numbers below stand as a fill
measurement; they are simply not a stability one.

**The fill term (4), and what it culls.** Charging for kernels culls the point
and sharpen passes — which are two of the three that alternate with the jitter.
At flicker weight 0, moving fill from 0 to 6:

| | flicker before | flicker after |
|---|---|---|
| training | 21.49 | 21.01 |
| held-out | 27.12 | 26.62 |

and sharpen occupancy collapses from 79 % to 28 % in distribution and 93 % to
14 % out of it, at **no cost in distribution and a small gain out of it**. Read
that as what it is — a fill result with a flicker column alongside it that was
not measuring the artefact.

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

- **CLOSED (2026-08-08): palettised textures drew nothing under BLSS.** It was
  never a texturing bug. `RendererCoreBlss::configure()` set `zBuffer.mask`
  one statement before the VRAM rebuild it triggers, and the rebuild's
  `allocateVramBuffers()` cleared the field again — so the shrunken z buffer ran
  **unmasked at display resolution** all run, and since `ZBUF` has no width of
  its own a 512-wide pass stamps depth 512×448 words past `ZBP` whatever the
  256×224 allocation says. That range covered the texture heap: on the `blssbug`
  fixture the texture sat at 669 696 and its CLUT at 679 936, both inside
  458 752…688 120. Zeroing a CLUT zeroes its alpha, so `ATEST NOTEQUAL`/`AREF 0`
  discarded every fragment of 4-bit geometry while 24-bit textures (no CLUT,
  alpha from `TEXA`) kept drawing. The mask is now **derived from the
  allocation** in `allocateVramBuffers`, where it cannot be undone; the VRAM
  saving is unchanged. Verified with before/after screenshots on `blssbug` and
  `examples/upscaler-lab`. Full account, including why the earlier
  "VRAM overlap — nothing overlaps" elimination missed it, in
  [§6 of the math doc](blss-reconstruction.md#fixed-blss-deleted-palettised-textures--the-z-mask-was-never-on).
- **TWO TWIN CHANGES ARE MEASURED AND WAITING ON THE ENGINE, and this is the top
  of the list now that the frame has been timed.** Both attack the 5.10 ms of EE
  inside `composite()`; both are switched off on the host until their engine half
  exists, because `blss.net` cannot record which configuration fitted it.
  - **The transcendental table — the engine half is WRITTEN now, and both
    halves are still off.** [Free](#the-transcendentals-as-a-table): 0.01 dB
    against a fold sd of 0.35, no change in occupancy or passes. Deletes
    2 688 `tanhf`, 672 `expf` and 672 divides per frame. `runNet` in
    `renderer_core_blss.cpp` calls `actTanh` / `actLogistic`, which are libm
    unless `TYRA_BLSS_ACT_TABLE` is 512; the host is libm unless `--act-table`
    is passed. What remains is the **switch-on**, and it is one commit that
    moves both defaults together plus a `--blss-eval -i` parity run — never one
    side at a time. Definition:
    [§5 of the math doc](blss-reconstruction.md#the-activation-table--both-halves-now-exist-both-are-off).
  - **`kTile` 32 → 64 — do NOT do it on this evidence, but the price is now
    known.** [4× less inference and 3.5× less packet build](#the-tile-size-swept)
    for a mean that is a draw (+0.46 against +0.42) and a tail that is worse:
    6 of 39 fold-runs below bilinear against 3, at 40 % more fold-to-fold spread.
  - **Neither closes the gap.** 5.10 ms of the 9.83 is what they share; the other
    ~4.4 ms is scene submission through the per-package proxies and
    `beginScene()`, and nothing on this page addresses it.
- **DONE — the bob was watched, and it is measured rather than watched now.** This
  bullet used to say "nobody has watched the emulator since the fill term landed"
  and prescribe disabling the jitter as a workaround. Both halves are answered:
  the bob is **still there on hardware** (30.8 % of the picture alternating,
  1.42/255) and `blssJitter` is that workaround as a real project setting, with
  a host twin so the net is fitted to whichever sampler the build uses.
- **CLOSED (2026-08-09): the objective cannot buy the jitter back.** The form was
  fixed as this page prescribed — a motion-compensated period-2 penalty that
  freezing cannot pay — and
  [swept over nine weights at 30 fold-runs each](#the-trade-curve). The
  alternation only reaches the jitter-off floor at weight 5, where the margin is
  **+0.29 against the +0.33 that turning the jitter off buys for free**, and
  where 15 folds of 30 lose to plain bilinear. Below weight 1.5 the knob does
  nothing at all. **Do not re-run this**; `--flicker-weight 0` and `blssJitter`
  off are measured answers now, not a retreat. The remaining route to that 0.3 dB
  is a *different* one — something that fuses the two phases without asking the
  oracle to pay for it, e.g. a jitter pattern the composite can undo exactly, or
  a temporal pass the fill term is not allowed to cull.
- **CLOSED (2026-08-09): the stability metric's fixture problem.** It was the
  fixture and not the objective, exactly as this bullet predicted, and the fix is
  the one it prescribed: **`--still`** freezes each shot at one camera and one
  pose so only the jitter phase advances (`blss::generate()`, eleven lines). The
  warp becomes the identity — the gate keeps **100 %** of the frame instead of
  29.6 % — and the animation freezes with it. The metric's floor on the
  **animated** corpus goes **2.614 → 0.095** levels while the artefact reads
  4.434, so it is now 47× the floor instead of below it, and `--still --no-anim`
  reproduces `--still` to within 0.004 levels: **`--no-anim` is retired as a
  prerequisite for reading this column.** Full table and the three mechanisms it
  confirmed at
  [The still fixture](#the-still-fixture-and-what-it-showed-the-metrics-floor-was).
  It is a fixture and not a corpus, so `--blss-train` and `--cv` refuse it.
- **CLOSED (2026-08-09): can one net ship for every project?** Yes, if it is
  fitted to **the bestiary and real projects together** — measured
  leave-one-project-out over seven projects, and it is a tie with per-project
  training on the only project with a ceiling big enough to tell
  ([the section](#can-one-net-ship-for-every-project)). What is **owed** is the
  product decision and its plumbing, none of which is in the files this was
  measured in: a `blss.net` shipped in the repo (or generated at build), a
  window that offers "use the default net" beside "train on this project", and
  the recipe in CI so the default is rebuilt when the corpus, the topology or
  `kNetVersion` moves. `blss.net` records **no** topology and no provenance, so
  a shipped default needs `kNetVersion` policed harder than a locally trained
  one does.
- **`texDetail` is dead on most real content, and that is now a measurement
  rather than a suspicion.** It is identically zero on **five of seven** example
  projects and 0.082 on a sixth, against the bestiary's 0.120 and
  `upscaler-lab`'s 0.443 — and it is the bestiary's most oracle-correlated
  channel. The follow-up this page has flagged twice, **having the editor bake
  real high-frequency energy per material at build time**, is now the item with
  the most evidence behind it: it would turn a channel that is a constant on
  most projects into one that varies, and it is a change to `buildFeatures()`'
  *input*, not to the twin contract.
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
- **Four process-wide switches, all in `blss.hpp`, all set once by
  `applySweepKnobs()` (or by `generate()`, for the jitter) before any corpus
  exists and never written again** — they reach `WeightField::sample()`, the
  oracle's innermost loop and `Net::forward()`, none of which has a config to
  thread a parameter through, and a per-call version would be a second place for
  the two producers to disagree. No worker thread ever writes one, which is what
  keeps `--threads` a wall-clock knob:
  `tileSize()` / `setTileSize()` (`--tile`), `actTanh()` / `actLogistic()` /
  `setActTable()` / `actTableHash()` / `emitActTable()` (`--act-table`, and
  `--blss-emit --act-table N` prints the engine's literals),
  `jitterEnabled()` / `setJitter()` (`--jitter` / `--no-jitter`, and the
  project's own `blssJitter`), and `CorpusConfig::animated` (`--no-anim`).
  The animated corpus itself is `AnimMesh` + `appendAnimObject()` in
  `blssscene.{hpp,cpp}` and `animObjectsOf()` in `blsscorpus.cpp`; it goes
  through `animimport::bake` so `.glb` and `.fbx` behave identically.
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

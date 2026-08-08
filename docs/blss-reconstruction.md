# BLSS reconstruction math

Developer note, not a user guide. This page is the **twin contract** of the
neural upscaler ([docs/neural-upscaler.md](neural-upscaler.md)): the exact
arithmetic that `src/blss.cpp` executes on the host and
`vendor/tyra/engine/.../renderer/core/blss/` executes on the PlayStation 2.

It exists because the network is trained against the *hardware* formula. The
oracle in `blss::oracle()` optimises weights for what the GS will actually do —
truncating shifts, 8-bit clamps and all — so if the two implementations drift,
the network is not merely inaccurate, it is optimising the wrong objective. Any
change to one side is a change to this page and to the other side.

`tyrax-editor --blss-eval` is the regression test: for the trained net, the
oracle and every fixed kernel it prints PSNR, flicker and **occupancy** — what
fraction of grid cells each pass draws and the mean full-screen passes per frame.
A parity break shows up as the trained net scoring below the oracle by more than
the usual ~0.7 dB (24.49 against 25.19 on the shipped held-out split; it was
0.8–0.9 dB before the proxy fix and the deadzone). (`--blss-eval --cv` is the *generalisation* measurement and
a different question; for parity, the plain run is the one to watch, because the
gap to the oracle is the quantity that moves when the twins drift.)

**Pass `-i <net>` when you are checking parity.** `--blss-eval` runs *net-free*
when there is no `blss.net` to load — it prints the oracle row and every fixed
kernel, which is what answers "does this scene have anything to reconstruct", and
omits the `half-res + BLSS (trained)` row entirely. That row is the one this page
is a contract about, so a parity check without it silently measures nothing. See
[the trainer section](neural-upscaler.md#training).

The evaluation is threaded over shot runs and its numbers are **bit-exact at any
thread count** — the workers produce per-frame values and the sums are folded in
afterwards in corpus order. If a table ever moves with `--threads`, that is a
real bug in this file, not a rounding artefact, and it invalidates every parity
reading taken since.

**What is NOT part of this contract: the oracle's objective.** The three terms it
minimises (accuracy, `--flicker-weight`, `--fill-weight`) live entirely in
`blss::oracle()`, which has no engine counterpart — the console only ever
evaluates the network. Retuning the objective changes the *weights the network
learns*, never the arithmetic below, so it moves nothing on this page. The
arithmetic is what both sides execute; the objective is only what the host uses
to decide what to teach.

## Symbols

| Symbol | Meaning |
|---|---|
| `outW`, `outH` | display resolution (512×448 in the default PAL mode) |
| `sx`, `sy` | upscale factors (2,2 or 1,2) |
| `lowW = outW/sx`, `lowH = outH/sy` | the low-res render target |
| `kTile` | 32 — decision tile edge, in output pixels |
| `cols = outW/kTile`, `rows = outH/kTile` | 16 × 14 |
| `jx`, `jy` | this frame's jitter, in low-res pixels: ±0.25, i.e. ±4/16 exactly |

## 1. Jitter

Two phases, alternating every frame:

```
phase 0: (jx, jy) = (-0.25, -0.25)
phase 1: (jx, jy) = (+0.25, +0.25)
```

Applied by adding `jx`, `jy` to the low-res raster's `XYOFFSET`, which stores
sixteenths of a pixel — so ±4 raw units, reproducible bit-exactly on the host.
For a 2×2 upscale those two positions are two of the four output-pixel centres
inside one low-res pixel: current + history is a genuine quincunx pair.

**Sampling must undo it.** Content drawn at raster position `p + jx` is stored at
texel `p + jx`, so recovering the value at `p` means reading texel `p + jx`:

```
u(x) = (x + 0.5) / sx + jx          // low-res texel coordinate
v(y) = (y + 0.5) / sy + jy
```

The engine expresses this as a constant `+jx*16`, `+jy*16` offset on the grid's
12.4 UVs.

## 2. Bag proxies -> tile stats  (`accumulate()`)

The EE knows a frame as a list of `BagProxy` — a screen bbox, a `w` range, and
one material scalar. For every tile the accumulator sums, over every bag,
`a = overlapX * overlapY / kTile²`:

```
coverAcc += a
depthAcc += a / max(wNear, 1e-4)
detAcc   += a * texDetail
dmin      = min(dmin, 1 / max(wFar,  1e-4))
dmax      = max(dmax, 1 / max(wNear, 1e-4))
edgeAcc  += length of each of the bag's four bbox EDGES that falls inside
            this tile     (a horizontal edge at y contributes overlapX
                           when tileY0 <= y < tileY1)
```

then

```
cover     = min(1, coverAcc)
depthMean = depthAcc / max(coverAcc, 1e-6)
texDetail = detAcc   / max(coverAcc, 1e-6)
depthMin  = dmin,  depthMax = dmax        (0 when nothing covered)
edge      = min(1, edgeAcc / (2 * kTile))
```

`texDetail` on the PS2 side is **not** an editor bake (that is a follow-up): it
is the minification ratio the engine can compute for free from what it already
holds — texels per screen pixel, `clamp(sqrt(texelArea / screenArea) / 4, 0, 1)`,
computed off the **clamped** bbox so a proxy running off the side of the screen
reports the detail of what is on screen. The corpus computes the same ratio from
its own materials.

### Where a proxy comes from, and the three rules that make it describe anything

This half of the contract used to be one sentence — "the bag's world bounding
sphere, which the renderer already computes for the dynamic-light pick". It was
wrong in a way no host measurement could see, because the corpus never had the
problem: `--blss-eval --features` described the corpus' distribution and nothing
described the console's, so for eleven commits the network was fitted to one and
run on the other. §2a below is the instrument that closed that; these are the
three rules it found.

**A proxy is an object-space AABB through the MVP, not a bounding sphere.**
`RendererCoreBlss::addBagBox()` and the corpus' `bagOf()` are twins: eight
corners (clip space is affine in the box's parametric coordinates, so it is one
matrix-vector product plus three scaled columns), near-clipped along the box's
**twelve edges** so an edge that crosses the near plane contributes its
intersection, then reduced to a screen bbox and a `w` range. A sphere is
grotesque for a floor or a terrain mesh: `wNear = w − radius` collapses to the
near clamp and the screen box covers the frame, so **every tile reads
`depth = 1`, `depthGrad = 1`, `coverage = 1`** — a constant, which is a network
making no per-tile decision at all. `addBagSphere()` survives as the fallback for
a bag with no package bbox.

**One proxy per VU1 package, not one per bag.** `StaPipCore` feeds BLSS one box
per run of `maxVertCount/3` consecutive vertices — `StaPipBagPackagesBBox`
already holds them, cached, computed for frustum classification whether BLSS is
on or not, so the granularity is already paid for. The corpus cuts the same way:
`kProxyVerts = 24` is derived from `StaPipVU1Program::getMaxVertCount` for the
Cull-TC class (the finest of the three, so it cannot flatter the corpus). Both
cap at **32 proxies per bag** and merge consecutive parts above that; merging by
vertex range can only *enlarge* a box, so the worst case degrades toward the old
whole-bag proxy rather than lying about where geometry is.
`--blss-train --no-package-split` reverts the corpus to one proxy per object,
which exists to reproduce the fold tables measured before the split.

**A box that straddles the eye AND still fills the frame after clipping is
dropped, by both producers.** Its bbox is the frame by construction and its
`wNear` is the clip constant, not a measurement, so it hands every tile it
touches "fully covered, at the nearest representable depth". The condition is
threshold-free on purpose:

```
eyeInside  = wNear <= near * 1.0001
fillsFrame = x0 <= 0 && y0 <= 0 && x1 >= outW && y1 >= outH
if (eyeInside && fillsFrame) drop the proxy
```

It is a **no-op on the corpus by construction rather than by luck** — nothing
there encloses the camera (the floors are zero-thickness quads under the eye, the
walls zero-thickness in x) — which is what keeps a fold table comparable across
the change.

**And one rule that only the bag can state: `PipelineInfoBag::blssProxy`.** The
straddle rule cannot catch a dome *cap*, because a box covering only the top band
of the frame is perfectly well formed and still describes nothing — it is a shell
patch, and no AABB of a shell describes where its surface is. Only the submitter
knows a mesh is a shell, so codegen sets `blssProxy = false` for the sky dome, the
star field and the sun/moon discs, and `StaPipCore` then submits no proxy for
them at all. `SceneMesh::proxy` is its corpus-side twin. It costs the network
nothing: those tiles read `coverage = 0`, which `kMinCoverage` already treats as
"do nothing here".

The corpus also honours what the console does **not** submit — a bag past its
`drawDistance`, or a terrain chunk past the streaming view distance, is neither
drawn into the ground truth nor described by a proxy.

## 2a. The instrument, and it is permanent this time

Nothing on this page is checkable against real content without a way to read the
console's own feature vector, and the one previous attempt at that was added,
read once and deleted. Both halves are permanent now, and this pair is how the
twins get checked:

- **Engine, BLSS debug view 2** (`RendererCoreBlss::logFeatureSpread()`). One
  line group a second into the game's `bin/log.txt`, picture untouched:

  | line | what it carries |
  |---|---|
  | `BLSSGRID` | tile counts, how many tiles are covered, **how many proxies described the frame**, the scale |
  | `BLSSWORST` | the single widest proxy — tiles touched, bbox, `w` range, the near plane. This is the line that names the culprit when the grid describes nothing |
  | `BLSSFEAT` | min/mean/max of all six inputs over the tile grid |
  | `BLSSOUT` | min/mean/max of the three outputs |
  | `BLSSFILL` | occupancy per pass and the frame's mean passes, through `emitGrid`'s own four-corner skip rule — the same quantity `blss::occupancy()` reports on the host |

  The channel names and their order are **exactly `blss::kFeatureNames`**, so a
  line here sits next to a row of `--blss-eval --features`. It is compiled out of
  an `NDEBUG` build (`TYRA_LOG`), which the editor's own game build is not.
  **It is reachable from the UI**: *Debug view* → "Log the feature spread to
  bin/log.txt (no tint)", in the upscaler window's *Project settings* tab and in
  *Project ▸ Preferences*. Until `7d3dbf67` that combo offered only two entries
  against a field the loader clamps to `0..2`, so reaching view 2 meant editing
  `"blssDebugView": 2` into the `.tyra` by hand and a project that already had it
  displayed as "Off".

- **Host, `--blss-eval --probe "<BLSSFEAT line>"`.** Paste that line back and it
  places the console's vector *inside the corpus distribution*: per channel the
  spread, the percentile, how much corpus lies within ±0.05 (which is what
  decides interpolation vs extrapolation — a percentile alone reads the same at
  both ends), how much of the corpus the frame's own band reaches, and a verdict.
  It accepts the retired `luma=` and `histAge=` spellings and ignores them, so
  vectors recorded in older commit messages stay placeable.

- **Host, `--blss-eval --cv --drop-feature <name>`** holds a channel at zero over
  the whole corpus — training, labelling and evaluation — which is what deleting
  it from the vector would do. "Does this channel earn its keep" is then one
  `--cv` run instead of an edit to `kFeatures` on both twins. Both channels this
  network has lost were retired on that measurement; the tables are in
  `src/blss.hpp` and
  [the upscaler page](neural-upscaler.md#two-channels-the-network-lost-and-the-measurements-that-took-them).

## 3. Reprojection  (`buildReproj()`)

Per grid **corner** `(i, j)`, at output pixel `(i*kTile, j*kTile)`, with the
representative `1/w` averaged over the adjacent covered tiles (no coverage ->
`du = dv = 0`; sky does not reproject):

```
w    = 1 / max(invW, 1e-6)
sX   = (2*px/outW - 1) * cur.tanHalfFovX
sY   = (1 - 2*py/outH) * cur.tanHalfFovY
dir  = cur.fwd + cur.right*sX + cur.up*sY
wp   = cur.pos + dir * w                       // world position
rel  = wp - prev.pos
wPrev = dot(rel, prev.fwd)
if wPrev < 1e-3: du = dv = 0                   // behind the previous camera
sXp  = dot(rel, prev.right) / (wPrev * prev.tanHalfFovX)
sYp  = dot(rel, prev.up)    / (wPrev * prev.tanHalfFovY)
du   = (sXp*0.5 + 0.5) * histW - px * histW/outW
dv   = (0.5 - sYp*0.5) * histH - py * histH/outH
```

`tanHalfFovY = tan(fov/2)` and `tanHalfFovX = tan(fov/2) * aspectRatio`, and
**both are independent of the raster scale** — the projection's x/y term and the
raster half-extent each carry the scale factor, so it cancels. That is what makes
"the frustum planes stay untouched" a fact rather than an intention, and it is
exactly the `Pinhole` both sides must build.

Offsets are in **history-buffer texels**. The history is the other display
framebuffer (see below), so `histW/histH == outW/outH` and one texel is one
output pixel.

## 4. Tile stats -> features  (`buildFeatures()`)

All normalisation, differencing and clamping lives here, so the two producers
cannot disagree. `kDepthRef = 8.0` world units.

```
motion    = clamp(|mean of the tile's 4 corner offsets| / kTile, 0, 1)
depth     = clamp(depthMean * kDepthRef, 0, 1)
depthGrad = clamp(max(  max over 4-neighbours |depthN - depth|,
                        (depthMax - depthMin) * kDepthRef ), 0, 1)
edgeDens  = edge
texDetail = texDetail
coverage  = cover
```

**`motion` is the length of the MEAN of the four corner offsets, not the field
sampled at the tile centre.** Once the field is piecewise linear those differ —
`triLerp` at (0.5, 0.5) weights only two of the four corners — so an unstated
choice here is a guaranteed drift. Both sides take the mean.

**`buildFeatures()` is a PURE function of one frame, on both twins**, and that is
now a property worth defending rather than an accident. This section used to
carry a ninth line, `histAge = min(histAge / 8, 1)`, plus an update rule with
three thresholds, an ordering requirement ("run it AFTER the features are built")
and a reset-on-scene-cut clause — the most drift-prone paragraph on this page, and
it had already drifted once (an earlier draft left the update to "the caller" and
the two sides promptly implemented different thresholds, feeding the network a
channel at training time that the console would never reproduce). The channel was
measured and deleted, which took the whole recurrent path with it: no per-tile
counters, no `prevDepth`/`prevCover`, no ordering hazard. `luma` went the same way
one commit later. The measurements are
[on the upscaler page](neural-upscaler.md#two-channels-the-network-lost-and-the-measurements-that-took-them);
the consequence here is that **no per-tile state survives a frame on either
side**, so there is nothing in this section for the two implementations to
sequence differently.

## 5. The network

MLP 6 → 12 → 3, `tanh` hidden, logistic outputs, 123 weights:

```
h[k] = tanh( sum_i w1[k][i] * f[i] + b1[k] )
o[m] = 1 / (1 + exp( -( sum_k w2[m][k] * h[k] + b2[m] ) ))
```

Outputs are `wA` (point), `wC` (temporal), `wD` (sharpen). The per-tile values
are averaged onto the `(cols+1) × (rows+1)` grid corners — a corner is the mean
of the up-to-four tiles touching it — and shipped as vertex alpha, so the
rasteriser's Gouraud interpolation *is* the upsampling of the weight field.

**The interpolation is piecewise linear over two triangles, not bilinear**, and
the host models that exactly (`triLerp` in `src/blss.cpp`). That pins the
vertex order: each tile row is emitted as one `TRIANGLE_STRIP` in the order

```
(i, j)  (i, j+1)  (i+1, j)  (i+1, j+1)  (i+2, j)  (i+2, j+1)  ...
```

so every quad's diagonal runs from `(i, j+1)` to `(i+1, j)`, and for a point at
fractional position `(fx, fy)` inside the quad:

```
fx + fy <= 1 :  v = v00*(1 - fx - fy) + v01*fy       + v10*fx
else         :  v = v11*(fx + fy - 1) + v01*(1 - fx) + v10*(1 - fy)
```

Emit the strip in a different order and the two implementations disagree in the
middle of every tile — which is precisely where the oracle's labels were fitted.
The same interpolation applies to the reprojection UVs of pass 3, since those
are per-vertex too.

## 6. The composite

The history is **the other display framebuffer** — the previously presented
frame, already at full resolution, at `frameBuffers[1 - context]`. This is why
BLSS allocates no history buffer at all: double buffering already keeps one, one
frame old, for free. It also makes the temporal pass a true accumulation
(the previous frame was itself composited), so a static camera converges toward
supersampled over several frames rather than stopping at two samples.

Per output pixel, in 8-bit integers, with GS blend semantics
`out = clamp( ((A - B) * C >> 7) + D )`:

```
aA = trunc(clamp(wA,0,1) * 128)
aC = trunc(clamp(wC,0,1) * 128 * 0.5)
aD = trunc(clamp(wD,0,1) * sharpen * 128)

B   = bilinear(low, u(x), v(y))                    pass 1, opaque
out = B
out = ((nearest(low,u,v) - out) * aA >> 7) + out   pass 2  ALPHA(0,1,0,1,0)
out = ((bilinear(hist, x+0.5+du, y+0.5+dv) - out) * aC >> 7) + out
                                                   pass 3  ALPHA(0,1,0,1,0)
out = out + (B * aD >> 7)                          pass 4  ALPHA(0,2,0,1,0)
out = out - (bilinear(low, u+0.5, v+0.5) * aD >> 7)
                                                   pass 5  ALPHA(2,0,0,1,0)
```

Note the `C` field of passes 4 and 5: it is **0 (`As`)**, not 2 (`FIX`). An
earlier draft of this page wrote `ALPHA(0,2,2,1,0)` and `ALPHA(2,0,2,1,0)`,
which select the `FIX` constant and then supply 0 for it — the multiplier is
zero and both sharpen passes silently do nothing. The whole point is that the
strength arrives as per-vertex alpha, so `C` must be `As`.

Passes 4 and 5 are `B + k·(B − box(B))`, the unsharp mask split into the two
blend equations the GS has. Every pass clamps to 0..255 (the GS's default
`COLCLAMP`), and `>> 7` **truncates** — the host uses an arithmetic shift, not a
divide, so negative intermediates round the same way.

### Getting per-vertex alpha out of a textured draw

The blend factor must be the *vertex* alpha while RGB stays the untouched texel.
No GS texture function does that directly, so the passes use
`TFX = MODULATE`, `TCC = 0` (RGB only, alpha from the `TEXA` register),
vertex RGB pinned to 128, and an explicit `GS_SET_TEXA(0x80, 0, 0x80)`:

```
RGB = Ct * 128 >> 7 = Ct           exact, no loss
A   = TA0 * Av >> 7 = 128 * Av >> 7 = Av
```

Writing `TEXA` is what makes this deterministic — it is the first `TEXA` write
in the engine, and without it the passes inherit whatever
`draw_setup_environment` left there.

### Three pieces of GS state the passes must write, not inherit

The engine writes **none** of `COLCLAMP`, `TEXA`, `DTHE`, `DIMX`, `PABE` or
`FBA` anywhere, so they hold whatever ps2sdk's `draw_setup_environment` left —
and that is re-established after any `graph_set_mode` GS reset. Two of them
matter here, and a third piece of state comes from the drawing environment:

- **`COLCLAMP` must be written on.** The formula above clamps every pass to
  0..255 and so does the host twin. If `COLCLAMP` is off the GS *wraps* instead,
  and the two implementations diverge on every saturated pixel — a bug that is
  free to prevent here and very expensive to recognise on a television.
- **`TEXA` must be written** (`0x80, 0, 0x80`), for the per-vertex-alpha trick
  above.
- **The alpha test must be disabled for the duration of the passes.** The
  drawing environment's `ATEST_METHOD_NOTEQUAL` discards fragments by alpha, and
  in these passes alpha *is* the weight — so a zero-weight corner would be
  discarded rather than blended at zero. The post-fx path already does this and
  documents the same bug from the other direction
  (`renderer_core_postfx.cpp:440-446`). `RendererCoreBlss` builds its own packet
  rather than going through `applyCustom`, so it inherits none of that bracket
  and must do the disable, the `ZBUF` write-mask and the restore itself.

`PRIM.IIP` must be 1 or there is no Gouraud interpolation and the whole weight
field collapses to flat per-triangle values (`blit` passes 0).

### One known parity gap: the UV clamp at the top-left edge

The UV register's fields are **14 bits unsigned**, so the engine cannot emit the
jitter-undone UV of the grid's first corner: at `px = 0`, phase 0 wants
`u = -0.25` texels, which as 12.4 is `-4` and wraps to 1023.75. The engine
therefore clamps vertex UVs to >= 0.

The host does not need to — at pixel *centres* the analytic `u(x)` is never
negative — so across the first tile column and the first tile row the two
disagree by up to a quarter of a texel in the UV *gradient*, converging to zero
at the far corner of those cells. That is ~13 % of the frame's tiles at sub-texel
magnitude, and it does not invalidate the trained weights. Closing it means
having the host interpolate clamped CORNER UVs the way the rasteriser does,
instead of evaluating `u(x)` per pixel — it is in the backlog.

### Packet budget

One tile row is a strip of `2*(cols+1)` = 34 vertices, and RGBAQ+UV+XYZ2 in
PACKED mode is **3 qwords per vertex** (a REGLIST saves register addresses, not
qwords) — so ~102 qwords per row per pass, ~1 430 per full-screen pass, and
~5 700 for passes 2..5. The shared post-fx packet is 768 qwords and would
corrupt the GIF stream; BLSS allocates its own at init. **Size it for the worst
case, not the typical one** — sparsity reduces what a frame draws, never what a
frame *may* draw.

### The history buffer needs an accessor

`RendererCoreGS::frameBuffers[2]` is private and the stock accessor returns the
buffer being drawn *into*. Pass 3 samples `frameBuffers[1 - context]`, so the
fork adds a previous-buffer accessor alongside it.

### Sparsity

Every pass is emitted per grid cell as runs of `TRIANGLE_STRIP`, and a cell
whose alpha byte rounds to 0 is skipped — a run break, not a transparent draw.
So the network's confidence sets the frame's fill cost. This is not an
optimisation, it is what keeps the composite from costing more than the half-res
render saved.

**The skip rule is a property of the CELL, not of the tile**, and the difference
is fill: `emitGrid` breaks the strip only when *all four* corner alpha bytes of a
cell are zero, so one tile asking for a kernel lights up the **nine** cells that
touch its corners. That bleed is real and it is what the occupancy measure has to
count.

**How much it actually culls, measured.** `--blss-eval` prints the fraction of
grid cells each pass draws and the resulting mean full-screen passes per frame
(1.00 = plain bilinear, 5.00 = every kernel everywhere), through that same
four-corner rule rather than through the raw weights. On the shipped net, at 156
corpus frames over 13 shots:

| | point | temporal | sharpen | mean passes |
|---|---|---|---|---|
| trained net, training shots | 0.0 % | 66.7 % | 0.0 % | 1.67 |
| trained net, held-out shots | 0.0 % | 86.6 % | 0.0 % | 1.87 |
| oracle, held-out shots | 3.0 % | 33.0 % | 0.0 % | 1.36 |

(The tool prints the oracle row for the held-out split only.)

**An earlier draft of this section claimed "passes 2..5 typically cover a minority
of the screen". That was aspirational and it was false when written** — with a
fill-blind objective the held-out mean was **4.85 of a possible 5.00 passes**,
i.e. every kernel over essentially the whole screen. It is true today of **point
and sharpen**, which the inference deadzone culls completely, and **not** of
temporal, which is still drawn over most of the screen. The oracle row shows the
headroom that remains: a *better* PSNR at 1.36 against the net's 1.87, i.e. about
half a pass, so this is still the network failing to generalise the cost model
rather than a floor — by a third of what it used to be.

Occupancy is **noisier than PSNR**: over 39 leave-one-shot-out fold-runs the mean
is 1.80 passes with an sd of **0.30**, and one fold (`flat`, an empty untextured
pan) reaches 2.12 on its own. Treat the table as one measurement of a noisy
quantity, re-run `--blss-eval --cv` rather than quoting it, and size anything that
has to be *correct* off the worst case.

None of this is a *timing*. Occupancy counts grid cells; **no frame time has ever
been measured for BLSS**, in PCSX2 or on hardware. Size the packet for the worst
case regardless — see the packet budget above.

## 7. What does *not* have a twin

- The **z-buffer**. The low-res 3D pass shares the main z buffer (`ZBUF` carries
  a base but no width), and **the main z buffer is now the size of the raster,
  not of the display**: `RendererCoreGS::allocateVramBuffers` takes its
  dimensions from `RendererSettings::getRasterWidthUI/HeightUI`, so at 2×2 it is
  57 344 words instead of 229 376 (672 KB back at 512×448, 768 KB at 512×512).
  That is what makes the feature VRAM-positive; the measurement is in
  [the upscaler page](neural-upscaler.md#5-vram).

  Note what the sharing means for addressing: the row stride comes from
  `FRAME.FBW`, so the low-res pass writes a contiguous **prefix** of z at the
  low-res stride, not a top-left rectangle of a wider layout.

  **The safety invariant is one field: `zBuffer.mask` is 0 only INSIDE the
  low-res bracket.** `configure()` sets it to 1 when BLSS is on,
  `beginScene()`/`endScene()` open and close the window. Every
  `draw_enable_tests` / `draw_setup_environment` in the engine reads that single
  field, so the 2D/HUD/post-fx half of the frame — which draws full-screen
  sprites at `z = 0xFFFFFFFF` and would otherwise stamp 448 rows at a 512 stride
  — cannot write past the smaller allocation. Anything that adds a draw outside
  the bracket inherits the protection for free; anything that clears or forces
  `mask` locally defeats it.
- **Depth of field, portals and split view** read or write real GS depth at
  display resolution and are therefore incompatible with a raster-sized z — and
  since the shrink, that depth is not merely unwritten, it is not allocated.
  **This bullet used to end "the generated game does not emit them together with
  BLSS", and that was not true when it was written.** It is true now, and by a
  different mechanism than the sentence implied: `blssClashes()` +
  `blssInterlock()` in `src/templates.cpp` emit `#error` lines into
  `inc/scene_data.hpp`, so the pair does not *build* rather than not being
  emitted. The editor's *Neural upscaler (BLSS)* block warns about the same four
  conditions live, and is now the early warning rather than the whole interlock —
  drawn from **one** place (`drawBlssSettings` / `blssClashesFor` /
  `drawBlssClashWarning`) and called by both *Tools ▸ Neural Upscaler (BLSS)* and
  *Project ▸ Preferences*, so there is one mirror of `blssClashes()` and not two.
  One of the three had a second, independent problem and no longer does:
  - **Portals** were the fourth copy of the raster-restore bug.
    `renderPortalView` runs inside `renderScene()` and
    `RendererCorePostFx::portalMaskBegin/End` took `FRAME` from
    `gs->getCurrentFrameBuffer()` and wrote a display-sized `SCISSOR` and
    `XYOFFSET` — i.e. they cancelled the BLSS redirect exactly the way the env
    map used to before it was converted. **Converted in `332f3193`**: both read
    `getRasterTarget()` and restore through `emitRasterRestore()`, so there is
    one implementation and not two, and this bracket now carries the
    `InterlacedField` per-field `XYOFFSET` bias (`getFieldYOffset16`) it never
    had. `Begin` re-narrows the scissor to the portal's bbox after the restore;
    `End` lets it go. The depth half is untouched, so portals and BLSS are still
    refused together.
  - **Split view** is never bracketed: codegen wraps only the single-view branch,
    so a split frame renders at full resolution with scene depth writes still
    masked.
- The **HUD, 2D and every post effect** still draw at full resolution, after the
  composite. That is deliberate: text and sprites stay crisp, which is the one
  thing a real upscaler must not spoil.

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

`tyrax-editor --blss-eval` is the regression test: it prints PSNR for the
trained net, the oracle and every fixed kernel. A parity break shows up as the
trained net scoring below the oracle by more than the usual ~0.3 dB.

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
two material scalars. For every tile the accumulator sums, over every bag,
`a = overlapX * overlapY / kTile²`:

```
coverAcc += a
depthAcc += a / max(wNear, 1e-4)
lumaAcc  += a * luma
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
luma      = lumaAcc  / max(coverAcc, 1e-6)
texDetail = detAcc   / max(coverAcc, 1e-6)
depthMin  = dmin,  depthMax = dmax        (0 when nothing covered)
edge      = min(1, edgeAcc / (2 * kTile))
```

`texDetail` on the PS2 side is **not** an editor bake (that is a follow-up): it
is the minification ratio the engine can compute for free from what it already
holds — texels per screen pixel, `clamp(sqrt(texelArea / screenArea) / 4, 0, 1)`,
which is the direct predictor of texture aliasing. The corpus computes the same
ratio from its own materials.

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
luma      = luma
histAge   = min(histAge / 8, 1)
```

**`motion` is the length of the MEAN of the four corner offsets, not the field
sampled at the tile centre.** Once the field is piecewise linear those differ —
`triLerp` at (0.5, 0.5) weights only two of the four corners — so an unstated
choice here is a guaranteed drift. Both sides take the mean.

`histAge` is the one recurrent channel, and **its update rule is part of this
contract**. It was left to "the caller" in an earlier draft and the two sides
promptly implemented different thresholds, feeding the network a channel at
training time that the console would never reproduce. Run it AFTER the features
are built, from the normalised values:

```
changed = |depth - prevDepth| > 0.02
       or |cover - prevCover| > 0.05
       or motion > 0.25
histAge = changed ? 0 : min(histAge + 1, 255)
prevDepth = depth ;  prevCover = cover
```

and reset the whole grid to 0 on a scene cut (the first frame of a shot).

## 5. The network

MLP 8 → 12 → 3, `tanh` hidden, logistic outputs, 147 weights:

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
So the network's confidence sets the frame's fill cost: passes 2..5 typically
cover a minority of the screen. This is not an optimisation, it is what keeps
the composite from costing more than the half-res render saved.

## 7. What does *not* have a twin

- The **z-buffer**. The low-res 3D pass reuses the main full-size z buffer
  (`ZBUF` carries a base but no width). Note what that means: the row stride
  comes from `FRAME.FBW`, so the pass reinterprets the main z at the low-res
  stride and overwrites a contiguous **prefix** of it — roughly the top 112
  rows of the 512-stride display layout — not a top-left rectangle. Harmless
  here because nothing else needs display-resolution z while BLSS is on, and
  exactly the thing to know before trying to keep a z region alive alongside it.
  Shrinking z to the render size would return 172 032 words — a follow-up.
- **Depth of field, portals and split view** read or write real GS depth at
  display resolution and are therefore incompatible with a low-res z. The
  generated game does not emit them together with BLSS.
- The **HUD, 2D and every post effect** still draw at full resolution, after the
  composite. That is deliberate: text and sprites stay crisp, which is the one
  thing a real upscaler must not spoil.

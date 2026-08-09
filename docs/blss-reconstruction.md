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

**Pass `-i <net>` when you are checking parity**, and read the `[blss] net
source=` line before believing the table. With no `-i` and no `blss.net` where
the tool is looking, `--blss-eval` falls back to **the editor's built-in default
net** — which is the right answer for "what will my game do" and the wrong one
for a parity check, because the row this page is a contract about would then be
describing a net you did not choose. (It used to omit the row entirely, which was
at least loud.) See [the trainer section](neural-upscaler.md#training).

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
| `T[0..N]` | the shared activation table, §5 — **wired on both twins and ON by default on both** (`N = 512`) |
| `jx`, `jy` | this frame's jitter, in low-res pixels: ±0.25, i.e. ±4/16 exactly |

**`kTile` is 32 on both sides and the host can sweep it.** `--tile N` moves
`blss::tileSize()` for one run, which is how the 16 / 32 / 64 tables on
[the upscaler page](neural-upscaler.md#the-tile-size-swept) were measured; the
engine's `RendererCoreBlss::kTile` is a compile-time constant, so **any value
but 32 is a measurement configuration and the host says so on stdout when you
ask for one.** Moving it for real is a change to this line, to
`renderer_core_blss.hpp` (`kTile`, and `kMaxCols`/`kMaxRows`, which are spelled
`512/kTile` and `ceil(540/kTile)`), and to nothing else — every other use on both
sides derives from it.

**One display mode does not divide, at any tile size**, and it is worth knowing
before reading `cols`/`rows` as exact: `HiDef1080i` is 448 × **540**, so at
`kTile = 32` the engine ceils to 17 rows whose last is 12 px tall, and at 64 it
ceils to 9 rows whose last is 28 px. The host **floors** (`outW/kTile` in
`blsscorpus.cpp`) because the corpus only ever renders 512 × 448, where every
supported tile divides exactly; at a size that does not divide it prints a
warning naming both grids rather than quietly measuring a smaller frame than the
console draws. The other four modes divide at 32 and at 64: 512 × 448, 448 × 448,
512 × 512, and `InterlacedField`'s 512 × 448.

## 1. Jitter

**Jitter is a MODE, not a constant, and that is the newest thing on this page.**
The project setting is `blssJitter` (`ProjectSettings`, format v5, **default
false** since 2026-08-08 — a human watched a jitter-on build and called it "like
an earthquake", so the default is now the configuration that can be looked at)
and codegen bakes it into the generated game, because the bob this feature has
been chasing since the beginning **is confirmed present on real hardware**: with
a frozen camera, a project-trained net and the fill term in, **30.8 % of the
picture alternates between two images every frame** (amplitude 1.42/255) with
jitter on, and **0.03/255 — the noise floor, identical to BLSS off** — with it
off.

**Two corrections to that paragraph, both measured 2026-08-08 on the fixed
build** (the 30.8 % figure was taken on a build carrying the z-mask defect of
[§6](#fixed-blss-deleted-palettised-textures--the-z-mask-was-never-on), so every
earlier observation of this artefact was made through a frame that was missing
surfaces):

- ~~**The bob is NET-dependent, and neither shipped fixture reproduces it.**~~
  **RETRACTED the same day.** That bullet said `examples/upscaler-lab` on its
  shipped net gives byte-identical consecutive pictures, and concluded the
  artefact belonged to one net rather than to the feature. It was measured on a
  fixture whose particles were running (their motion is *larger* than the
  artefact and buries the period-2 signature), reported as a pixel count, and
  cross-checked against `blssbug` — an untextured box on flat ground, which
  cannot exhibit a quarter-pixel resample at all. A human then looked at three
  builds of `upscaler-lab` differing only in these two flags and called them
  steady / **earthquake** / steady for BLSS-off / jitter-on / jitter-off. With
  the emitters frozen the instrument agrees: 16.3 % of the picture below the HUD
  alternates between two byte-identical phases with jitter on, and 0.00 % with
  it off. The jitter is the cause, the shipped net does reproduce it, and
  `blssJitter` now defaults to **false**. See
  [neural-upscaler.md, "A/B/C"](neural-upscaler.md#abc-a-human-looked-at-three-builds-2026-08-08)
  and [profiling.md's stability gate](profiling.md#the-stability-gate-period-2--the-bob)
  for the rules that make the measurement able to see it.
- **`getFieldYOffset16()` is identically 0 in every fixture that has ever shown
  this artefact.** `RendererSettings::isFieldRendering()` is true for
  `DisplayMode::InterlacedField` alone, and both fixtures are `"interlaced"` =
  `DisplayMode::Interlaced`. The per-field bias therefore contributes nothing to
  the bob, which is also why turning `blssJitter` off drops the alternation to
  the noise floor — the two terms are independent and only one was ever
  non-zero.

**The field bias was still wrong, though, and is fixed now.** Inside the low-res
bracket `XYOFFSET` is 1/16 of a raster pixel of the *current* `FRAME`, and that
raster is the low-res target — one row of it is `scaleY` physical buffer rows.
`beginScene()` added `getFieldYOffset16()` (8 = half a **physical** row
everywhere else in the engine) unscaled, so it acted as `0.5·scaleY` physical
rows; and `composite()` then added the real one on top at display resolution.
The odd field ended up biased by `0.5·scaleY + 0.5` = **1.5 physical rows at
2×2 instead of 0.5** — the two fields interleaved a whole line apart. The fix is
not a rescale but a **removal**: the low-res target is an offscreen texture that
nothing scans out, so the interleave belongs solely to the pass that writes the
buffer the CRT reads, which is `composite()`. Keeping the low-res raster
field-independent is also what pass 3 wants, since a scene that shifted every
field would fight its own reprojection. Measured on `blssbug` forced to
`interlaced-field`, BLSS 2×2, frozen camera: between-cluster amplitude
**0.109/255 at 0.20 % of the picture before, 0.016/255 at 0.10 % after**, against
a within-cluster floor that also fell (0.013 → 0.003). Small on a scene with few
horizontal edges; pure field misregistration on any scene with many.

**Twin implication:** the field bias never belonged in the low-res raster, so the
host's sampler model is *not* incomplete — `±4` raw jitter units remain the whole
of what `beginScene` contributes, exactly as this section documents, and
`src/blss.cpp` needs no change for it. Had the term stayed, it would have.

So both sides of the contract now have two configurations, and **they must be
the same one.** The host twin is `blss::jitterEnabled()` /
`blss::setJitter()`: `--blss-train <projectDir>` and `--blss-eval <projectDir>`
**read the project's own `blssJitter`** and fit against the sampler that project
will ship with, `--no-jitter` / `--jitter` force it either way, and the bestiary
(which has no project to ask) keeps it on, because that is the configuration
every fold table on [the upscaler page](neural-upscaler.md#measured) was measured
with. The corpus prints a line when it is off.

**A net fitted with jitter on and run in a jitter-off build is fitted out of
distribution.** `blss.net` recorded nothing that could detect it — the same shape
as the whole-bag proxy and the animated models — until the trainer started
writing a `<net>.meta` sidecar beside every net it produces
([provenance](neural-upscaler.md#provenance-what-a-net-says-about-itself)). The
sampler is one of its fields, the bake compares it against the project's
`blssJitter`, and a disagreement is now named in the generated header and in the
boot log instead of being invisible. With jitter off the two
phases sample the same position, so `u(x)`/`v(y)` below undo nothing, the
current frame and the history are no longer a quincunx pair, and the temporal
pass is accumulating genuinely identical samples rather than fusing two.

Two phases, alternating every frame (jitter **on**):

```
phase 0: (jx, jy) = (-0.25, -0.25)
phase 1: (jx, jy) = (+0.25, +0.25)
```

and with jitter **off**, `(jx, jy) = (0, 0)` in both phases — on both twins, in
the raster offset and in the sampling that undoes it.

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

**A FIFTH RULE IS WRITTEN AND WAITING FOR ITS TWIN: the proxy budget.** The fixed
cap of 32 above is a constant where the grid has a natural one. A proxy's only
effect is on the tiles its screen bbox overlaps, and the grid resolves nothing
finer than a `kTile` square — so describing a bag with more boxes than it covers
tiles buys nothing that the accumulator can represent, while costing a full
projection and a full tile update per extra box. The rule is:

```
project the bag's WHOLE object-space AABB through mvp exactly as a package box
is projected  (eight corners, the twelve-edge near clip, the straddle rule,
               the screen clamp)
count its tile range   tiles = (tx1 - tx0 + 1) * (ty1 - ty0 + 1)
                       with addBag()'s clamps and its -0.001F
cap   = clamp(tiles, 1, kMaxProxiesPerBag)
      = kMaxProxiesPerBag  if the whole box describes nothing
group = ceil(parts / cap)              -- the existing consecutive-part merge
```

The fidelity loss is bounded by the mechanism the fixed cap already documents —
merging by vertex range can only *enlarge* a box, never move it — and the cap is
never 0, so no bag stops being described (a rule that could empty a bag would
hand its tiles `coverage = 0`, which the network reads as "nothing here").

It is implemented in the engine behind **`TYRA_BLSS_PROXY_BUDGET`**, which
**ships at 0**, exactly like `TYRA_BLSS_ACT_TABLE` before it: one number in two
files, moved in the same commit or not at all. `bagOf()` / `bagList()` in
`src/blsscorpus.cpp` must apply the same cap — the same projection of the whole
object AABB, the same tile arithmetic, the same `ceil(parts / cap)` — before the
switch may go to 1. Measured on `examples/upscaler-lab` with it on: **198 proxies
become 116 and 262 projections become 174**, `coverage` mean moves **0.631 →
0.638**, and `depth`, `depthGrad`, `edgeDens`, `texDetail`, the covered-tile
count, `BLSSWORST` and `BLSSFILL`'s `passes = 1.56` do not move at all.

**Confirmed on REAL HARDWARE, 2026-08-09**, on a fixture segment where every
emitter is hidden so the scene is genuinely still (see profiling.md, "the
bit-identity segment"): **198 proxies → 115, 262 projections → 173** (the
emulator said 116/174 — a one-proxy scene-state difference, not a rule
difference), covered tiles identical at **147**, `coverage` **0.631 → 0.638**,
and `BLSSOUT`, `BLSSFILL` and `BLSSWORST` **byte-identical across 44 paired
1 Hz samples**. So the description really does cost one channel and 0.007 of
its range. What the emulator could not price is what it buys, and hardware now
does: **`proxy` 2.336 → 1.907 ms, the EE bill 4.597 → 4.167, d = +0.429 ms
[+0.427, +0.432]** over 160 paired windows, with `reproj`/`feat`/`net`/`pkt`/
`begin`/`end` all flat to three decimals. That is **41 % of the feed for 0.007
of one input's range**, and it lowers break-even by a full coverage — but the
switch still **ships at 0** until `src/blsscorpus.cpp` cuts the same way, and
the two halves still move in one commit or not at all.

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
  | `BLSSGRID` | tile counts, how many tiles are covered, **how many proxies described the frame**, how many boxes were **projected** to get them, how many **tile updates** that cost, the scale |
  | `BLSSWORST` | the single widest proxy — tiles touched, bbox, `w` range, the near plane. This is the line that names the culprit when the grid describes nothing |
  | `BLSSFEAT` | min/mean/max of all six inputs over the tile grid |
  | `BLSSOUT` | min/mean/max of the three outputs |
  | `BLSSFILL` | occupancy per pass and the frame's mean passes, through `emitGrid`'s own four-corner skip rule — the same quantity `blss::occupancy()` reports on the host |

  `proxies of N projected` and `tile update(s)` are what price the feed: a box
  that is projected and then rejected (behind the eye, off screen, or dropped by
  the straddle rule) pays in full and describes nothing, and the tile count is
  what `FrameProfile::tBlssAccum` is proportional to. On a parked frame of
  `examples/upscaler-lab` they read 198 of 262, and 1 499 tile updates — 7.6
  tiles per proxy, which is what says the feed's cost is per-PROXY rather than
  per-tile (docs/profiling.md, "Pricing the proxy feed"). The tile-update column
  wobbles by a few frame to frame, so it is not a byte-identity channel; the
  other four lines are.

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

A tile whose `coverage` is below `kMinCoverage` skips the whole evaluation on
both sides, so the per-frame count below is the worst case rather than the
typical one — the one console frame that has been instrumented had 159 of 196
tiles covered.

### The activation table — both halves exist and both are ON

**The table is the shipped configuration on both twins**, `N = 512`. The host
implements it behind `--act-table N` and defaults to `blss::kEngineActTable`;
`renderer_core_blss.cpp` carries the same table behind `TYRA_BLSS_ACT_TABLE`,
which also defaults to **512**. They are one number in two files and they move in
one commit — a host that fits against a table while the console evaluates libm is
exactly the twin drift this page exists to prevent. The measurement that says it
is free is [on the upscaler page](neural-upscaler.md#the-transcendentals-as-a-table),
and the whole 39-fold table has since been re-run at the shipped activations
(+0.42 → **+0.41 dB**, sd 0.34, proxy count unchanged at 1 217). The 257 stored `short`s are
`tyrax-editor --blss-emit --act-table 512` **verbatim**; the FNV-1a below was
re-derived from the pasted literals on 2026-08-08 and is `0x47A59E3C`, with a
maximum deviation from `tanh` of 1.5e-05, i.e. half a Q15 LSB.

**Until 2026-08-08 the engine half was landed, hashed, documented and DEAD.**
`actTanh` and `actLogistic` were defined and never called — `runNet()` went
straight to `tanhf`/`expf` — so the switch controlled nothing and "the engine
half landed" (which this page used to say) was not true of the code that ran.
`runNet()` calls them now, which makes `TYRA_BLSS_ACT_TABLE` a real switch for
the first time, and both defaults moved to 512 in one commit.

**Flipping one side alone is silent divergence** — nothing in `blss.net` records
which activation fitted it — so the switch-on is its own commit that moves
`src/blss.cpp`'s default (`--act-table`, `int actTable = 0;`) and
`TYRA_BLSS_ACT_TABLE` together, followed by a `--blss-eval -i` parity run.
**It is worth 2.11 ms of EE**, measured on a console-shaped fixture in PCSX2
(`runNet` 3.39 → 1.29 ms, 65 paired windows, CI [2.10, 2.12]) — the largest
single saving available to this feature, and it has been taken: on hardware it
moved `net` from 1.93 ms to 0.79 ms, for a total EE bill of 5.02 ms. See
[profiling.md](profiling.md).

Why it is worth doing at all: the engine evaluates `12 tanhf + 3 expf + 3 fdiv`
per tile (`renderer_core_blss.cpp`, the `logFeatureSpread` neighbourhood), in a
build with **no `-ffast-math`** (`vendor/tyra/Makefile.base:19` is `-D_EE -Wall
-O3`), so at 224 tiles that is **3 360 libm calls and 672 divides a frame** —
and every result is then thrown away down to a byte by `cornerAlpha()` and the
alpha-8 deadzone.

**One table serves both**, because `logistic(z) = (1 + tanh(z/2)) / 2` exactly.
So the divide disappears with the `expf`, and the whole activation budget becomes
15 table lookups per tile.

**Why a table and not a polynomial.** A minimax polynomial has to be *matched*
between two compilers — same coefficients, same association order, same FMA
contraction — and a mismatch is a silent drift in the objective the network was
fitted against. A table of integers is the same integers on both sides or it is
visibly not, and reconstruction from an integer is exact: an `int16 -> float`
conversion and a multiply by `2^-15` are both exactly representable, so **the
activation step contributes zero divergence between the twins**. The float MACs
around it stay whatever the EE FPU makes of them, exactly as they already are —
the table does not fix that and does not claim to.

**The definition.** `N` is the number of intervals; the table has `N + 1`
entries. `R = 4` is the domain half-width. `N` must be **even**, so the midpoint
entry is exactly `tanh(0) = 0`.

```
step = 2*R / N
T[i] = round_half_away_from_zero( tanh(-R + i*step) * 32768 ),   as int16
       clamped to [-32768, 32767]
```

and the **lower half is the negation of the upper**, computed rather than
evaluated (`T[i] = -T[N-i]` for `i < N/2`), so the two ends cannot round apart
and the engine only has to store `N/2 + 1` entries — 257 `short`s, 514 bytes, at
the measured `N = 512`.

```
tanh(a):
    if a <= -R:  return T[0] * 2^-15
    if a >=  R:  return T[N] * 2^-15
    x = (a + R) * (N / (2*R))       // float; at N=512, R=4 that is (a + 4) * 64
    i = (int)(x + 0.5f)             // NEAREST, via a truncating cast; 0 <= i <= N
    return T[i] * 2^-15             // int16 -> float, then * 3.0517578125e-05

logistic(z) = 0.5f + 0.5f * tanh(0.5f * z)
```

Five rules, each of which is a way the two sides could otherwise differ:

- **Clamp first, index second.** `|a| >= R` returns the end entry, so the index
  can never leave `[0, N]` and no bounds check is needed in the hot path.
- **Nearest, not truncate, and no interpolation.** Rounding halves the worst-case
  error for one add; *not* interpolating is what makes the result a table value
  **exactly**, which is the whole bit-exactness argument. Interpolating would put
  a float multiply back between the two twins for ~2e-5 of accuracy that a byte
  cannot hold.
- **`2^-15`, not `/32767`.** A power of two is exact; anything else re-introduces
  a rounding difference.
- **The logistic reuses the same table** and never gets one of its own. Its
  argument is halved, so `|z| >= 8` saturates — `logistic(8) = 0.99966`, which is
  alpha 127.96 against libm's 128, i.e. one byte at the extreme and nowhere else.
- **`kActRange = 4`** because `tanh(4) = 0.99933`: clamping there costs at most
  6.7e-4, well inside the ~4e-3 that a half-byte of output alpha is worth.

**The checksum is how the two sides check rather than assume.** Both generate the
table from the formula above; if two libms ever round one entry differently the
tables differ by one Q15 step and nothing visible happens, which is precisely the
kind of drift that goes unnoticed for eleven commits. So both sides take
**FNV-1a** (offset basis `2166136261`, prime `16777619`) over all `N + 1` entries
as little-endian `uint16` (low byte first) and compare it against the constant:

| N | entries | stored (half) | FNV-1a |
|---|---|---|---|
| 512 | 513 | 257 × `short` = 514 B | **`0x47A59E3C`** |

`tyrax-editor --blss-emit --act-table 512` prints the upper half as a C++ array
with that hash in its header comment, so if the formula ever disagrees between
two toolchains the argument ends by pasting the literals.

**`kNetVersion` does not move for this.** The file format and the topology are
untouched, the measured quality difference is 0.01 dB against a fold sd of 0.34,
and a bump would refuse every existing `blss.net` to guard a difference no
measurement can see. What the switch **must** do is land on both twins in one
commit — and the reason has softened rather than gone away: the `<net>.meta`
sidecar now records `act-table`, so a net fitted against one table and baked for
another is *reported* (a warning, not a refusal, because the difference is 0.01
dB). Nothing in `blss.net` itself records it, so a net that arrives without its
sidecar is still undetectable, which is why the two sides still move together.
`blss::kEngineActTable` is the host constant that names what the console
evaluates and is the value a bake compares against — never `detail::gActN`, which
is the host's own `--act-table` and is 0 in any process that never ran a BLSS
verb.

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

### …and what they must RESTORE, which is not the GS reset value

This subsection exists because the one above was **incomplete, and being
incomplete is what let a real defect ship**. It said what the passes must
write. It said nothing about what they must put back, and the code guessed:
`composite()`'s restore block wrote `TEXA = (0, 0, 0)` and
`COLCLAMP = COLOR_CLAMP_MASK`, with a comment claiming those were "the GS reset
values, which is what the whole engine has always run with". **They are not.**
The engine runs on whatever ps2sdk's `draw_setup_environment` left, and that
function ships no sources in the toolchain image — so the only way to know is to
disassemble `libdraw.a`:

```
mips64r5900el-ps2-elf-objdump -d $(find /usr/local/ps2dev -name libdraw.a)
```

Its `draw_setup_environment` is one 15-register A+D block, and the registers it
writes — the complete list of what a bracket may be inheriting — are:

| register | value it leaves |
|---|---|
| `FRAME_1` `ZBUF_1` `XYOFFSET_1` `SCISSOR_1` | from the framebuffer/z arguments |
| `PRMODECONT` | 1 (PRIM register, not PRMODE) |
| `TEST_1` | `ATE=1`, `ATST=NOTEQUAL`, `AREF=0`, `AFAIL=ZB_ONLY`, `ZTE=1`, `ZTST` from the z buffer |
| `ALPHA_1` | `(Cs−Cd)·As + Cd` |
| `CLAMP_1` | `WMS=1, WMT=1` (plain CLAMP) |
| `FOGCOL` `PABE` `DTHE` `FBA_1` | 0 |
| `DIMX` | the stock dither matrix |
| **`COLCLAMP`** | **1 — CLAMP, not MASK** |
| **`TEXA`** | **`TA0 = 0x80`, `AEM = 0`, `TA1 = 0x80`** |

Note what is **absent**: `TEX0` and `TEX1` are never written by the environment,
so they belong entirely to whoever drew last — the pipelines set both per mesh,
which is why leaving them is harmless.

So the rule is: **a bracket restores the drawing environment's value, not zero,
and if you do not know that value, disassemble it — do not assume the register
is at its reset state just because no engine code writes it.** `TEXA` in
particular is load-bearing for any 24-bit (`TEXTURE_COMPONENTS_RGB`, `TCC = 0`)
texture, whose fragment alpha *is* `TEXA.TA0`; with `TA0 = 0` such a fragment
has alpha 0 and the environment's `ATEST_METHOD_NOTEQUAL`/`AREF = 0` discards
it. `COLCLAMP` back at MASK makes every saturated blend in the rest of the
frame wrap instead of clamp. Both are now written from one constant,
`kEnvTexa`, with the mechanism recorded at the top of
`renderer_core_blss.cpp`.

**Honesty about what that fixed.** It is a correctness fix argued from the
disassembly, not a measured picture change: on PCSX2's software renderer a
24-bit-textured box draws under BLSS both with and without it (A/B'd on the
`blssbug` fixture, 2026-08-08), so PCSX2 is evidently more forgiving here than
the register semantics are. It is still wrong to hand a register back a value
its owner never had, and hardware is the machine that decides.

### FIXED: BLSS deleted PALETTISED textures — the z mask was never on

**Closed 2026-08-08.** With BLSS on, a static primitive or terrain chunk whose
texture was **indexed** (`PSMT4`/`PSMT8` plus a CLUT — which is what the
editor's texture bake produces for a material regardless of the project's
`textureQuant`) drew nothing at all. Same scene, BLSS off: it drew.
`examples/upscaler-lab` and the `blssbug` minimal fixture (one untextured box,
one `map_Kd` box, `--new … fpp`) both reproduced it.

The measured split, which is what made it look like a texturing bug:

| texture | under BLSS (before the fix) |
|---|---|
| none (vertex colour) | draws |
| 24-bit RGB (`PSMCT32/24`, `TCC = 0`) | **draws** |
| 4-bit palettised (`PSMT4` + CLUT) | **gone** |

**It was not a texturing bug at all. It was the z-buffer shrink writing over
the texture heap**, and it is the same field section 7 calls "the safety
invariant": `zBuffer.mask`.

The mechanism, end to end:

1. `RendererCoreBlss::configure()` set `gs->zBuffer.mask = enabled ? 1 : 0`
   **one statement before** the VRAM rebuild it triggers.
2. That rebuild runs `RendererCoreGS::allocateVramBuffers()`, whose opening
   block assigns `zBuffer.mask = 0` unconditionally — it is the initial value
   for a fresh allocation. So the mask the invariant depends on was **cleared
   again on the same call**, and stayed 0 for the whole run.
3. Every `draw_enable_tests` outside the low-res bracket therefore emitted
   `ZBUF` with `ZMSK = 0`: z writes **enabled**, at display resolution.
4. The GS addresses the z buffer at `FRAME.FBW` stride, and outside the bracket
   `FBW` is the display width. So a 512×448 pass stamps depth across
   `458 752 … 688 128` words regardless of the 256×224 = 57 344-word
   allocation. Under BLSS the texture heap starts just above that allocation —
   measured on `blssbug`: texture at **669 696**, its CLUT at **679 936**, both
   *inside* the range.
5. A palettised texture's CLUT is 16 `PSMCT32` entries in one 8×2 block. Depth
   values written over it destroy every entry **including its alpha byte**
   (`0x80` → whatever depth lands there, in practice 0). StaPip's standard path
   runs `ATEST_METHOD_NOTEQUAL` / `AREF = 0`, so **every fragment failed the
   alpha test and nothing was rasterised** — which is exactly what the earlier
   eliminations observed. A 24-bit texture has no CLUT and takes its alpha from
   `TEXA`, so it kept drawing; it merely lost some texels to the same writes.

Two things about the earlier elimination round are worth keeping, because both
were correct work that pointed the wrong way:

- **"VRAM overlap — nothing overlaps"** compared the *allocations*. The
  allocation was never the addressable extent: `ZBUF` carries no width, so the
  bytes a pass can reach come from `FRAME.FBW`, not from what
  `allocateBuffer()` handed out. That is the hole, and it is a general lesson
  for any buffer this engine shrinks.
- **The alpha test, blending and z-test experiments** were all done on the
  *drawing* side. The data was already gone before the draw started.

The fix moves the invariant to where the buffer is sized: `allocateVramBuffers`
now **derives** `zBuffer.mask` from its own allocation (1 whenever the z buffer
is smaller than the display raster, 0 otherwise) and `configure()` no longer
assigns it. Nothing can clear it on the way through any more, the mask is right
for the "no VRAM rebuild hook" path too (z keeps display size ⇒ mask 0), and
the VRAM saving is untouched — the fixed build reports the same
`low-res target at VRAM 593920` and `texture heap free MB: 1.73438` as the
broken one.

**Also settled by this, and it is a correction.** The comment on `composite()`'s
`TEXA`/`COLCLAMP` restore claimed the previous zero-valued restore "deleted
every textured primitive and every textured terrain chunk in the game". **That
attribution was wrong** — the deletion was this z-mask bug, which is
CLUT-specific and was present either way. The restore itself is still correct
and stays (it is argued from the `libdraw.a` disassembly above), but it remains
**unconfirmed by any picture**: `TEXA` governs `TCC = 0` only, and both fixtures
now draw their 24-bit and their palettised box with the restore in place. Do
not re-attribute a symptom to it without a screenshot.

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
  low-res bracket.** `RendererCoreGS::allocateVramBuffers()` **derives** it from
  the allocation it just made — 1 whenever the z buffer came out smaller than
  the display raster — and `beginScene()`/`endScene()` open and close the
  window. Deriving it there is not tidiness: `configure()` used to assign the
  field itself, one statement before the rebuild it triggers, and the rebuild
  runs `allocateVramBuffers`, which cleared it again. The flag was 0 for the
  whole run, and the depth that leaked past the allocation deleted every 4-bit
  palettised texture in the scene — see
  [§6](#fixed-blss-deleted-palettised-textures--the-z-mask-was-never-on). Every
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

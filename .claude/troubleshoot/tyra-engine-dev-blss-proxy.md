# troubleshoot/tyra-engine-dev-blss-proxy.md

How a bag is DESCRIBED to the network, emitter bags, the spatial split, the
permanent instrument and `emitGrid`'s skip rule. Symptom first.

## Every tile read `depth = 1` and the frame was described by TWO proxies

**Cause:** `StaPipCore` used to hand BLSS the bag's bounding SPHERE, once per bag. For a
floor or a terrain mesh that is grotesque — `wNear = w − radius` collapses to the near
clamp and the screen box covers the frame — so **every tile read `depth = 1`,
`depthGrad = 1`, `coverage = 1`**, i.e. the network was handed a constant and a
generated game's entire frame was described by TWO proxies. This was wrong for eleven
commits and is the most expensive mistake in this feature's history.
**Fix:** four rules replaced it, every one TWINNED with `blsscorpus.cpp` (`bagOf()`,
`kProxyVerts = 24`): `addBagBox()` projects an object-space AABB through the MVP and
near-clips it along its **twelve edges** (clip space is affine in the box's parametric
coordinates, so eight corners cost one matrix-vector product plus three scaled columns);
`StaPipCore` submits **one box per VU1 package** from the `StaPipBagPackagesBBox` it
already caches for frustum classification, capped at `kMaxProxiesPerBag = 32` with
consecutive parts merged above that (merging can only enlarge a box); a box that
**straddles the eye AND still fills the frame after clipping is dropped**, threshold-free,
because its bbox is the frame by construction and its `wNear` is the clip constant; and a
bag may opt out entirely via **`PipelineInfoBag::blssProxy`**, which codegen clears for the
sky dome, the star field and the sun/moon discs — the straddle rule cannot catch a dome
CAP, and only the submitter knows a mesh is a shell. `addBagSphere()` survives as the
fallback for a bag with no package bbox. Measured on a booted `fpp` fixture, debug view 2,
before → after: **2 → 41 proxies, 196/196 → 159/196 tiles covered, `depth` 1/1/1 →
0/.737/1, and 5.00 → 1.96 mean passes.**

## Emitter bags shipped nothing to describe the particles

**Cause:** a billboard bag runs `frustumCulling = None` on purpose, so it had no package
bbox, fell to the radius-0 sphere and was rejected — i.e. **nothing described the
particles**, which on `upscaler-lab` is 98.7 % of the fill and on `showcase` 95.6 %.
**Fix:** a sixth rule landed 2026-08-09 and it also SHIPS OFF: `TYRA_BLSS_EMITTER_PROXY`
(default 0, host twin `--emitter-proxy`) gives such a bag ONE box: the AABB over the
centres it is about to submit, grown per axis by `|R.axis|*(max|m00|+max|m10|) +
|U.axis|*(max|m01|+max|m11|)` off the params channel — exact for an ordinary emitter,
√2-conservative for fog's swirl, and one box per BAG rather than per VU1 package because a
pool's order is its SPAWN order and only a set's AABB is order-independent enough for the
corpus to match. Measured with it on (PCSX2, parked `upscaler-lab`): 198 → 207 proxies,
147 → **224 of 224** covered tiles, `texDetail` 0.466 → 0.211 (it finally reports
`puff.png`) — **and `coverage` becomes a CONSTANT 1.000/1.000/1.000** with `depthGrad`
spread 0.101. That is the sky-dome failure in the channel the rule meant to rescue. Cost:
BLSS EE **3.21 → 4.09 ms** (+0.88) and break-even **13.1 → ~15.3** coverages. So it stays
at 0.

## The spatial split is a measured NO — do not re-open it

**Cause:** 2026-08-09. Binning the centres by COORDINATE is order-independent and perfectly
twinnable, it was implemented on both twins, and it makes the two channels it was for MORE
constant: at the same parked vantage, **224 of 224 covered tiles before and after**,
`coverage` and `depthGrad` still 1.000/1.000/1.000, proxies 207 -> **241** of 310, tile
updates 2 636 -> **6 077**, BLSS EE 4.07 -> **5.25 ms**, break-even ~15.3 -> **~18.2**.
Two reasons, both general: **a partition of a solid region is a TILING of it, and a tiling
has the same union** (so no spatial split can shrink `coverage`), and a Tyra pool is never
clustered — `updateParticles` spawns uniformly over the emitter's XZ rect.
**Fix:** **The flat channel is the FIXTURE**: strip `upscaler-lab` to one small emitter and
`coverage` reads 0.690 / 67.8 % at 1.000 in all three arms, while `--blss-coverage` counts
71.65 of 72.23 coverages as emitters on the shipped one. The rule and its tables are in
docs/blss-reconstruction.md section 2; the emitter half's open item is that the CORPUS
DRAWS NO PARTICLES — `docs/backlog.md`.

## The instrument is PERMANENT, and deleting it is how this went unseen

**Cause:** `logFeatureSpread()` under `blssDebugView = 2` logs one group a second into the
game's `bin/log.txt`: `BLSSGRID` (tile/proxy counts), **`BLSSWORST`** (the widest single
proxy — the line that named the sky dome in one shot), `BLSSFEAT` and `BLSSOUT` (min/mean/max
per channel and per output) and `BLSSFILL` (occupancy through `emitGrid`'s own four-corner
rule). Channel names and order are EXACTLY `blss::kFeatureNames`, because the host half is
`tyrax-editor --blss-eval --probe "<BLSSFEAT line>"`. A previous round added this
measurement, read it once and removed it — after which the network was fitted to one
distribution and run on another with nobody able to compare them.
**Fix:** keep it. One gotcha: it is inside `#ifndef NDEBUG`, so a `make release` build has
no `TYRA_LOG` (the editor's own game build is not one). **Switching it on is a combo
entry** — *Debug view* → "Log the feature spread to bin/log.txt (no tint)", in the BLSS
window's *Project settings* tab and in *Project > Preferences*. Until `7d3dbf67` that
combo offered only 0 and 1 against a field `project.cpp` clamps to `0..2`, so reaching view
2 meant editing `"blssDebugView": 2` into the `.tyra` by hand.

## `emitGrid`'s skip rule is what the host's cost model is fitted to

**Cause:** a cell is drawn unless ALL FOUR of its corner alpha bytes are zero, so one tile
asking for a kernel lights up the nine cells around it. That rule is now inside the oracle's
objective on the host (`--fill-weight`, charged as a step on the quantised byte), and
`--blss-eval` reports occupancy through the same rule — so changing when a cell is skipped
changes what the network was trained to want.
**Fix:** measured on the shipped net: mean **1.87 full-screen passes on held-out shots and
1.67 in distribution**, against 1.00 for plain bilinear and 5.00 for the worst case, while
the oracle reaches a *better* PSNR at 1.36 — so about half a pass of the remaining fill is
the network failing to generalise the cost model. At the shipped inference deadzone the
point and sharpen passes are culled COMPLETELY (0 %) and only temporal is drawn over most
of the screen. Occupancy is noisy (sd 0.30 over 39 cross-validation fold-runs, one fold at
2.12), and these are **fill counts, not timings**.

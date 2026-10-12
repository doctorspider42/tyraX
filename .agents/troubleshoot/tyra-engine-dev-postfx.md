# troubleshoot/tyra-engine-dev-postfx.md

`RendererCorePostFx` (bloom, film grain, depth of field, god rays, custom effects),
the portal through-view machinery, and the projected-shadow / destination-alpha
raster brackets. Symptom first.

## The GIF stream corrupts when a post-fx pass gains primitives

**Cause:** `packet2_create(768, …)` in `renderer_core_postfx.cpp` sizes the packet for
the WORST case of every pass at once — a blit is 12 qwords, a flat quad 7 — and an
undersized packet corrupts the GIF stream. (An older note said 512; the code says 768.)
**Fix:** grow the packet whenever a pass gains primitives.

## The god-ray pass washed the whole frame white

**Cause:** the bright-pass threshold was too low (96). The sky IS bright.
**Fix:** god rays (`PassGodRays`, `setGodRays` strength + per-frame `setGodRaysSun`
screen position/visibility fed by the game) bright-pass the frame on the
quarter-res buffers, subtracting flat threshold **150**, then double back up, and
iteratively zoom it toward the sun (2 ping-pong passes, `s = 0.72`) before
compositing additively at **half** the requested strength.

## Bloom veils the frame instead of glowing on emissive materials

**Cause:** no bright-pass threshold, so sub-threshold pixels entered the blur.
**Fix:** `setBloomThreshold` adds one extra quarter-res sprite subtracting a flat
grey from the downsampled frame through `(0 - Cs)*128/128 + Cd`; the GS clamps at
zero, so sub-threshold pixels drop out of the blur (`docs/emissive-materials.md`).
A **spread** (`setBloomSpread`, 1..4 soften rounds with doubled tap offsets each,
buffers ping-ponging) grows the halo into a corona for 4 sprites a round.

## Depth of field punches sharp rectangles into a later z-tested pass

**Cause:** sprites stamp `z = max` across their whole rect.
**Fix:** `PassDof` (authored in the UI Editor / overridden by the Set Depth Of
Field flow node) reuses the bloom blur chain and composites the blur back through
full-screen sprites drawn at real GS depths under the pass's GEQUAL z-test, world
distance → GS z solved from the shared perspective matrix; it must run right after
the 3D scene, before ANY 2D.

## A user-authored full-screen effect needs the state setup/teardown

**Cause:** the effect body needs the framebuffer/noise/scratch accessors and the
GS-primitive emitters.
**Fix:** `applyCustom()` + `RendererCore::applyCustomPostFx()` — the effect body
appends GS primitives through the now-public `blit()`/`flatQuad()` and the
framebuffer/noise/scratch-buffer accessors, and the engine wraps the state
setup/teardown + DMA kick. See the editor's custom screen effects,
`docs/custom-screen-effects.md`.

## The portal opening is not shaped / the mask NLOOP is wrong

**Cause:** the GS has no stencil.
**Fix:** the TyraX portal through-view machinery (`RendererCore3D::pushPortalView`
— view-matrix-only swap, main projection kept, exact frustum planes at the virtual
camera — plus `RendererCore::portalViewBegin/End` →
`RendererCorePostFx::portalMaskBegin/portalMaskEnd`)
renders the destination scene IN-PLACE into the real framebuffer right after the
frame clear, scissored to the quad's screen bbox, and carves the shaped opening with
reversed-z ops: re-far the bbox z, cap the quad interior with a z-only ALWAYS fan
at the surface depth, repaint the still-far ring via a GEQUAL sprite at z=0 that
hits exactly the reset pixels. Both wrappers drain PATH1 unconditionally and do NOT
latch the post-fx drain gate; `portalMaskEnd`'s NLOOP is **13 + fan verts**. See
`docs/portals.md`.

## A projected-shadow receiver patch was drawn then discarded

**Cause:** returning from the shadow map with `ZBUF` still on the 64×64 silhouette
z made every receiver patch fail `GEQUAL`.
**Fix:** `RendererCoreShadowMap` (projected silhouette shadows: 4 lazy-allocated
64×64 VRAM slots + one shared cleared z, the env map's raster-redirect bracket per
caster — `begin(slot)` per caster, ONE `end()`; the game re-submits the caster's
existing bags under a `pushEnvView` "light camera" and draws a terrain patch
sampling the slot's VRAM-resident texture by light-space UVs). `allocate()` is
called by generated games only when a project has "Cast shadow" objects, and
`init()` re-places the buffers after a display-mode VRAM reset.

## The SDTV flicker filter shows per-pixel framebuffer alpha / the shadow mask is wrong

**Cause:** the interlaced flicker filter displays per-pixel framebuffer alpha, and
the GS cannot count in alpha (blending never writes A).
**Fix:** `RendererCoreAlphaMask` (the shadow volumes' destination-alpha mask — the
torch's and, since 1.67.0, the frame's one carving spot light's, docs/flashlight.md
"The shadow" + docs/shadows.md "Spot-light shadow volumes") keeps ONE band and ONE
bracket per LIGHT per frame, so each user clears, counts, draws its own passes and
repaints before the next starts: `begin`/`beginKeep`/`end` bracket FBMSK to the
alpha bits for the convex 1-bit set/clear, and **`repaintAlpha()` is MANDATORY
after the last DATE pass**. The COUNTING half for true mesh-shaped volumes:
`allocateCount()` claims a raster-sized PSMCT16 count target (permanent-region
discipline, refusal graceful via `countReady()`), `countBegin()` redirects FRAME
there while ZBUF stays the scene's z (one pixel grid, two independent addresses),
volume bags count with `PipelineInfoBag::additiveBlendFix`/`subtractiveBlendFix`
(+N front faces / -N back faces, TestOnly z), and `countResolve()` converts count>0
into the alpha MSB with ONE textured sprite — `TEXA.AEM=1` makes an all-zero texel
alpha 0 and anything else `TA0=0x80`, `ATEST!=0` makes the write an OR, and the
packet restores CLAMP to REPEAT itself because `emitRasterRestore` does not know
texture state.

## "Dashed green marks down two fixed screen columns" at 16-bit

**Cause:** the marks were blamed on a masked write at a PSMCT16 destination and it
is NOT that (measured on a console). A four-mask FBMSK probe (flat sprite + a
DATE-revealed alpha strip per mask) reads IDENTICALLY on hardware and in PCSX2 —
`0x00FFFFFF` is colour-neutral and its alpha half works. A paired 8-vantage sweep
one knob apart put the marks on `countResolve`'s TEX0 base: the SLID band base
scores 8/8, the band's own base 0/8, flipping back 8/8 (A-B-A).
**Fix:** the write side slides FRAME by `bandY0` page rows, so the read must NOT
slide as well as subtracting `bandY0` from V. N=32 clears the 16-bit channel's
8-step quantization plus dithering's ±4, so DTHE needs no save/restore. Counting
runs at BOTH colour depths (the band follows the frame's PSM: PSMCT32/512 KB,
PSMCT16/256 KB). Still open: why texels sampled by an alpha-only masked pass tint
the picture at all, and why the marks also appear above the band boundary,
docs/flashlight.md.

## A world-anchored sun / shadow is mirrored across the screen centre

**Cause:** the EE used the NDC projection form (see
`tyra-engine-dev-raster.md`, "The EE put something from a world position in the
wrong place").
**Fix:** use `px = W/2 + x/w·2048`, `py = H/2 + y/w·2048`.

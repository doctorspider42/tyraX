# troubleshoot/tyra-engine-dev-blss.md

`RendererCoreBlss` — the neural upscaler's console half. Proxies/emitters are in
`tyra-engine-dev-blss-proxy.md`; EE pricing, break-even and the interlock are in
`tyra-engine-dev-blss-cost.md`. Symptom first.

## The neural upscaler's console half

**Cause:** `renderer/core/blss/` (docs/neural-upscaler.md; the arithmetic is
docs/blss-reconstruction.md). The 3D scene renders into a half-resolution VRAM target and
a per-tile MLP decides how to reconstruct it. Reached as `engine->renderer.core.blss`;
generated games call `configure()` + `setNet()` in `init()` and bracket their scene with
`beginScene()` / `endScene()` / `composite()`. Inert and zero-VRAM when a project has it
off.
**Fix:** know the mode before changing anything (below).

## PLAIN MODE deletes the half this class is named after

**Cause:** PLAIN MODE (`configure()`'s seventh argument, `network`) is the mode most
projects should be in (docs/neural-upscaler.md, "Plain mode"). false = no bag proxies, no
tile accumulators, no reprojection, no feature grid, no MLP, and a composite that is ONE
textured quad instead of the 476-vertex Gouraud grid; the raster redirect, the shrunken z
buffer and the whole VRAM saving are untouched. Three things to know: the gate the
pipeline asks is **`wantsProxies()`, never `isEnabled()`** — StaPipCore computes a world
bounding sphere per bag before it calls, and `proxy` is 2.34 ms of a 4.60 ms bill;
`configure()` **forces `jitterOn` false** there, in one place, because the only thing that
can fuse two jitter phases is the temporal pass and plain mode has none; and the base pass
is emitted as one CELL of the grid rather than as a GS **sprite** — a sprite would save
seven qwords and would hand the picture to a different rasteriser path; a one-cell
TRIANGLE_STRIP makes the identity a property of the packet.
**Fix:** a neural build whose net asks for nothing and a plain build are byte-identical
over 811 426 compared pixels, nine cross-pairings.

## It is one half of a TWIN

**Cause:** `src/blss.cpp` in the editor is the other half, and `docs/blss-reconstruction.md`
is the contract: the same sampling (12.4 UV quantisation, bilinear taps at UV − half a
texel with 4-bit weights combined `>>8`), the same blend chain in 8-bit with the GS's
`(A−B)*C>>7` truncation and 0..255 clamps, the same
`accumulate`/`buildReproj`/`buildFeatures`/forward pass. The network's training labels are
fitted against this formula by an oracle running on the host, so a divergence trains the
network for a machine that does not exist.
**Fix:** `tyrax-editor --blss-eval` is the regression test (a parity break shows as the
trained row falling well below the oracle row). Change one side, change the doc and the
other side.

## It is a per-scene setting now, and `configure()` is still init-only

**Cause:** the two halves are `configure(..., nativeScenes)` and
`setScene(upscale, network)` (docs/neural-upscaler.md, "Per scene").
**Fix:**
- **`configure()` re-lays the permanent VRAM region and evicts every texture**, because
  the z buffer's size follows the raster. Safe at the top of `init()` and nowhere later —
  and the theory that a scene change would absorb it is FALSE: `loadScene()` frees and
  re-acquires textures one at a time, ref-counted, and never calls `vram.reset()` or
  `evictAll()`.
- **So a mixed game does not reconfigure. It PINS the layout.** `nativeScenes` true makes
  `configure()` call `RendererCoreGS::setZRasterScale(1, 1)` before the realloc decision,
  so the z buffer covers the full display for the whole run and `needsBufferRealloc()`
  stays false however often the active raster scale changes afterwards. `setScene()` then
  flips `enabled`/`useNet`, republishes `settings->setRasterScale`, re-derives the
  projection (`core3D->setFov`), drops `hasPrev` and restores the z mask. Measured in
  PCSX2 over ~1 200 scene switches: **zero evictions**, resident count and free VRAM
  constant, scene-load rate within 0.5 ms of both controls.
- **`endScene()` restores `gs->getZMaskDefault()`, never a literal 1.** Same number in
  every uniformly upscaled game, 0 in a mixed one — the literal left the next NATIVE scene
  rendering its whole depth pass into a mask. The mask is still DERIVED by
  `allocateVramBuffers` and never assigned from outside.
- **`init()` re-places the low-res target on `allocated`, not on `enabled`.** In a mixed
  game a display-mode switch can land while a native scene is active, and gating on
  `enabled` would leave `lowVram` pointing into memory `vram.reset()` just returned.
- **`jitterWanted` is kept apart from `jitterOn`.** Plain mode forces the jitter off; the
  REQUEST has to survive a plain scene or the first one would silently disable the jitter
  for every neural scene after it.
- **The three frame-loop entry points need no `if`.** `beginScene`, `endScene` and
  `composite` all return immediately when `enabled` is false.

## The raster redirect must be EXPLICIT STATE, not a private trick

**Cause:** `RasterTarget` (frame address, `FBW`, scissor rect, `XYOFFSET` in 1/16 px) is
published by `redirectRasterTo()` / `endRasterRedirect()`, read by `getRasterTarget()`,
and put back by the **shared `emitRasterRestore()`** that the env map, the camera feed and
the shadow map all call — so those brackets **nest** inside BLSS' instead of cancelling
it. They used to restore `gs->getCurrentFrameBuffer()` +
`settings->getWidth()/getRenderHeightF()`, i.e. the display buffer unconditionally, from
inside the generated `renderScene()`; the first one silently turned the upscaler off for
the rest of the frame.
**Fix:** **Do not "simplify" this back into making `getCurrentFrameBuffer()` return the
redirect target** — that was the prescribed fix and it is wrong: the restores touch four
registers and only `FRAME` comes from that accessor, so it would leave a 512-wide `SCISSOR`
over a 256-wide `FRAME` and a window-centred `XYOFFSET` for the wrong window with the
jitter dropped. `getCurrentFrameBuffer()` means **the display buffer**. Three traps inside
the bracket itself: `XYOFFSET` is written BEFORE the clear sprite; the 3D path wants a
**window-centred** offset (`2048 - lowW/2`) while the composite's 2D-style passes want the
**screen-origin** one (`2048, 2048`); and `emitRasterRestore()` writes `ZBUF` explicitly
and **LAST** rather than leaving it to ps2sdk's `draw_enable_tests` — returning from the
shadow map with `ZBUF` still on the 64×64 silhouette z made every projected-shadow
receiver patch fail `GEQUAL`. The jitter is added to the offset as raw 1/16 units —
`XYOFFSET` is 12.4, so ±4 is exactly ±¼ pixel. The shared restore also fixed a latent
`InterlacedField` bug none of the three brackets had: the per-field `XYOFFSET` bias
(`RendererCoreGS::getFieldYOffset16`) was never re-applied.
`RendererCorePostFx::portalMaskBegin/End` was the same bug a **fourth** time and is
**converted now** (`332f3193`): both read `getRasterTarget()` and restore through
`emitRasterRestore()`. `Begin` re-narrows the scissor to the portal's bbox *after* the
restore; `End` lets it go. **All four brackets now share one restore — do not add a fifth
that does not.**

## The z buffer follows the RASTER, not the display buffer

**Cause:** `allocateVramBuffers()` sizes it from `RendererSettings::getRasterWidthUI/HeightUI`
— 57 344 words instead of 229 376 at 2×2, so 672 KB back at 512×448 and 768 KB at 512×512
against 224/256 KB for the low-res colour target. **BLSS leaves more texture VRAM than not
using it** (measured, `Pal576i` 512×512, `VRAMSTAT` at frame 240: 0.227 MB free with BLSS
off, 0.727 MB with it on and one eviction fewer). `FRAME` and `ZBUF` bases are independent
registers, so the low-res pass writes a contiguous prefix of z at the low-res stride.
**Fix:** the invariant that makes it safe is `zBuffer.mask == 0` only INSIDE the low-res
bracket (`allocateVramBuffers` DERIVES the flag from the allocation it just made — 1
whenever z came out smaller than the display raster — and `beginScene`/`endScene` open and
close it): every `draw_enable_tests` / `draw_setup_environment` reads that one field, so
the 2D/HUD/post-fx half — full-screen sprites at `z = 0xFFFFFFFF` — cannot stamp past the
smaller allocation. Sizing z needs the scale, which only `blss.configure()` knows, and z is
allocated third in `gs.init()`: the ordering is resolved by re-laying the permanent region
from `configure()` through `RendererCore::rebuildPermanentBuffers()` (gated on
`needsBufferRealloc()`), which is `setDisplayOutput`'s mode-change branch minus the mode. It
deliberately does **not** call `gs.reinit()` — `programDisplay()`'s `graph_set_mode` would
reset the GS mid-init for nothing.

## Every 4-bit palettised texture drew nothing (that invariant has been broken TWICE)

**Cause:** deriving the flag in `allocateVramBuffers` is the fix for this feature's worst
bug and must not be moved back to a caller: `configure()` used to assign the flag one
statement before the rebuild it triggers, the rebuild runs `allocateVramBuffers`, and that
cleared it again — so the mask was 0 for the whole run and every full-resolution pass
stamped depth 512×448 words past `ZBP` (`ZBUF` carries no width; the stride comes from
`FRAME.FBW`), straight through the texture heap. Symptom: **every 4-bit palettised texture
in the scene drew NOTHING** — the depth landed on the 8×2 CLUT, a zeroed CLUT has alpha 0,
and `ATEST NOTEQUAL`/`AREF 0` discards it; 24-bit textures kept drawing, which made it look
like a CLUT-descriptor bug for two sessions. It broke a second time in
`RendererCorePostFx::apply()`'s restore block, which re-programmed `ZBUF` with a
**hardcoded mask of 0** (and the `draw_enable_tests()` on the next line does not cover for
it: disassembled from ps2sdk's `libdraw.a`, it emits one A+D qword at `GS_REG_TEST_1` and
nothing else). It read as **"BLSS deletes the terrain"** because `examples/showcase` has
exactly one texture in the whole project, and it stayed hidden because `apply()`
early-returns when no post-fx pass is enabled.
**Fix:** **treat `zBuffer.mask` as owned by whoever made the allocation. Grep
`GS_SET_ZBUF` before adding a full-screen pass**, and never write the mask from one. The
diagnostic worth stealing: the same broken arms were **also missing the crosshair sprite**
— a second, unrelated-looking thing going missing at the same time is what said "this is
not the terrain path". General lesson: **an allocation is not an addressable extent.**

## The history is `frameBuffers[1 - context]`

**Cause:** the previously presented frame, full resolution, free.
**Fix:** that needed a new accessor on `RendererCoreGS` (the stock one returns the buffer
being drawn INTO).

## The composite writes GS state the engine never wrote before

**Cause:** `TEXA` (for the per-vertex-alpha trick: `TFX=MODULATE` + `TCC=0` + vertex RGB
pinned to 128 makes RGB the untouched texel and A the vertex alpha) and **`COLCLAMP`** —
the formula clamps and so does the host twin, and inheriting a `COLCLAMP` of 0 would make
the GS wrap instead and silently break parity on every saturated pixel. It also disables
the **alpha test** for the duration: the weights ARE vertex alpha, so
`ATEST_METHOD_NOTEQUAL` would discard zero-weight corners rather than blend them at zero.
**Fix:** keep the state writes; see also `tyra-engine-dev-raster.md`, "Restoring a GS
register nothing else writes".

## It owns its own packet, sized for the worst case

**Cause:** one tile row is a 34-vertex `TRIANGLE_STRIP` at 3 qwords per vertex, so passes
2..5 are ~5 700 qwords — the shared 768-qword post-fx packet would corrupt the GIF stream.
Sparsity reduces what a frame draws, never what a frame MAY draw. And the strip's **vertex
order is load-bearing**: `(i,j) (i,j+1) (i+1,j) (i+1,j+1) …`, because the weight field is
piecewise linear over two triangles and the host models exactly that diagonal.
**Fix:** never size it off the typical case.

# troubleshoot/tyra-engine-dev-raster.md

GS raster state, texture wrap, dither, destination alpha, the 2D origin, the EE
world→screen projection, and the frame warp. Post-fx passes live in
`tyra-engine-dev-postfx.md`. Symptom first.

## A full-screen pass looks right on a bare fixture and wrong in a real scene

**Cause:** a pass that does not blend still WROTE a GS register nothing else
writes. `TEXA` and `COLCLAMP` are written by exactly one place in this engine (the
BLSS composite), so a second pass has no previous value to restore and can only
assert what it believes the GS reset value to be. The frame warp did that and got
it wrong: every blend AFTER the pass inherited it, and `examples/showcase`
(bloom + film grain) came back with the whole picture hue-shifted — orange sky
olive, green grass orange, a cyan crate magenta — while the geometry stayed
perfect. It passed every earlier test because those fixtures had **no post fx**,
and with nothing blending a broken blend state is invisible.
**Fix:** a pass that does not blend must not write those registers at all.
Whenever a full-screen pass looks right on a bare fixture and wrong in a real
scene, diff the two on POST FX before suspecting anything else, and check what
global state the pass leaves behind.

## The frame warp does nothing / the game hangs in `draw_wait_finish` with a clean log

**Cause:** `renderer/core/warp/` (`docs/frame-extrapolation.md`). `RendererCoreWarp`
re-draws the last finished frame under a newer camera
as a textured grid. Two traps: **an overcounting NLOOP is as fatal as an
undercounting one** (the restore block claimed 4 registers and wrote 3, so the GS
read the next giftag as a register write); and **a degenerate camera basis
silently becomes an identity copy**, because every corner takes the
`wPrev < 1e-3` fallback, so "the warp does nothing" and "the caller passed a bad
basis" look identical.
**Fix:** when `from == to` the result must reproduce its source EXACTLY — a wrong
TEX0 binding, region clamp, UV encoding or strip vertex order all show as tearing
or garbage. Check that first. Real and warped frames alternate every field, so a
Wayland compositor screencast cannot isolate one of two images at 50 Hz (ten
captures of a marked warp frame came back byte-identical) — build a game-side A/B
rather than trusting a screenshot.

## The GS dithers render-to-texture COUNTS; PCSX2 never will

**Cause:** `DTHE` is global GS state and the project leaves it on for the 16-bit
picture. On a console it also applied to the shadow-volume count band (`+N` / `-N`
per face, each with its own `DIMX` offset), so the pair stopped cancelling and the
mask came back with residues — green slivers along silhouettes and a dark halo at
16-bit, a one-pixel checkerboard in the pool at 32-bit — while PCSX2 drew the band
exactly.
**Fix:** any arithmetic render target must bracket itself with `DTHE = 0`
(`countBegin` does; `emitRasterRestore` does NOT carry DTHE, so restore it
yourself). Bisect a console-only GS symptom with the hidden `shadowVolumesDebug`
key (1/2/3, docs/flashlight.md) and `--capture-frame`.

## A rect built from projected vertices ran to row 3792 and slid `FRAME.FBP` below zero

**Cause:** the count rect clamped only its top-left; a vertex off the bottom of the
screen made a rect to row 3792, fifteen brackets a frame and `FRAME.FBP` slid below
zero for every band past the raster (1.70.5).
**Fix:** a screen rect from projected vertices must be clamped on BOTH sides.
Anything that walks a rect in bands or slides a base by it: clamp to the raster
first and skip the empty rect.

## A slid `FRAME.FBP` is not a trick PCSX2 can vouch for

**Cause:** the count band addressed its lower half by pointing `FRAME` below its
own base (into the z buffer at 16-bit) so page rows landed back in the band; the
arithmetic is right and the console still drew marks along straight lines.
**Fix:** prefer a full-height target whenever VRAM allows (`allocateCount` does at
16-bit); treat any remaining slide as suspect on hardware until measured there.

## `ATEST_KEEP_*` names what is PRESERVED, not what is written

**Cause:** ps2sdk's `ATEST_KEEP_*` constants (`ps2sdk/ee/include/draw_tests.h`):
`ATEST_KEEP_ZBUFFER` = 1 = GS FB_ONLY (colour written, z untouched),
`ATEST_KEEP_FRAMEBUFFER` = 2 = ZB_ONLY (z written, colour untouched), `ATEST_KEEP_ALL`
= 0 = write nothing. Upstream's alpha test passed `ATEST_KEEP_FRAMEBUFFER` on the
standard path, so every fully transparent texel of a cutout texture stamped the z buffer
while drawing no colour — foliage, grates and decals silently occluded whatever was
drawn behind them later.
**Fix:** cutout wants `ATEST_KEEP_ALL` (both pipelines now use it;
`PipelineZTest_TestOnly` deliberately keeps FB_ONLY for its depth-tested-no-z-write
trick). Symptom: transparency works, but geometry behind a transparent area is missing,
cut along the straight edges of the quad in front of it.

## Never arm `DTHE` for PSMCT32/24

**Cause:** the GS calls that unspecified and PCSX2 can hide the resulting
physical-hardware checker.
**Fix:** use `RendererSettings::isDitherActive()` at every `DTHE` restore site,
including alpha-mask brackets; `getDither()` is only the authored preference.

## A texture that needs clamping in 3D gets it per bag, and it costs a PATH1 drain

**Cause:** `GS_REG_CLAMP` is global GS state and nothing in the static or dynamic
3D pipeline emits it per mesh (there is not enough micro memory left to carry it
in-band the way the ALPHA qword is — the clip program set sits at 1676/2042).
**Fix:** `StaPipCore::render` programs the wrap a bag wants only when it differs
from `RendererCoreGS::currentTextureWrap`, and it no longer restores REPEAT per bag
(lazy since 1.123.1), so a run of bags sampling one clamped target costs one
barrier (physical PS2, Motor District night: `Light_pools` 1.605 -> 1.342 ms). The
REPEAT contract is closed in exactly three places: the next bag wanting a different
wrap, `Renderer2D`'s first sprite (inside the existing once-a-frame `drained3DFor2D`
barrier), and `RendererCore::endFrame` before `applyPostFx`. The older advice still
applies to STs that leave 0..1 for geometric reasons — clamp them where you BUILD
them, on the EE (the projected-shadow receiver patch's STs ran -0.38..1.39 and left
thin dark streaks; writing `GS_SET_CLAMP` from the shadow pass changed nothing
measurable because the pass never drained PATH1 first).

## The GS left a register in the wrong state across a frame pass

**Cause:** three global GS registers are written by code the 3D pass does not own.
`Path3::clearScreen` writes REPEAT (CLAMP_1) into the PATH3 packet that always precedes
the frame's 3D pass, so the wrap contract is re-asserted once a frame with no extra
transfer (`initDrawingEnvironment` writes it right after `draw_setup_environment()`).
ps2sdk's `draw_setup_environment()` ends with "Setup whole texture clamping"
(CLAMP/CLAMP at init) and programs `FBA = 1` for a 16-bit frame PSM. And ps2sdk's
`graph_set_framebuffer_filtered` leaves `PMODE.MMOD = 0`, so the interlaced flicker
filter blends by per-pixel framebuffer ALPHA instead of a fixed blend — moiré and
rectangles on a TV only, invisible to an RGB capture but visible with
`--capture-frame --alpha`.
**Fix:** `presentFrameBuffer` forces `MMOD = 1, ALP = 0x80`; `initDrawingEnvironment`
writes `FBA = 0` right after `draw_setup_environment()` (`RendererCoreAlphaMask::begin()`
/ `maskClear()` re-assert it per frame); and `Path3::clearScreen` re-asserts REPEAT. The
16-bit FBA / 16-bit destination-alpha traps are in `tyra-engine-dev-vram.md`.

## The EE put something from a world position in the wrong place

**Cause:** the screen edge is at `width/4096` of w — **0.125** at 512 px — so the
EE projection is `px = W/2 + x/w·2048`, `py = H/2 + y/w·2048`, never
`(x/w·0.5+0.5)·W` and never with a second y flip (the matrix already carries the
GS's downward y in its `data[5] = -h`). **This mistake has been made twice**: the
flashlight's count-rect scissor had the NDC form and sliced every mesh shadow flat
at its rows until 1.65.0, and the god-rays / lens-flare sun (`updateSunFx` in the
generated game) had the NDC form *and* the extra flip until 1.65.1 — on the dawn
plaza of `examples/day-night`, a sun disc drawn at (410, 127) of a 512×512 raster
was reported at (275, 272).
**Fix:** add `2048·y/w` (`RendererCoreBlss::addBagSphere`,
`Renderer3DUtility::convertVertices`, the shadow-map STs all do). A 2D SPRITE is
not in that space: `RendererCore2D` authors sprites in the stock 512×448 layout and
letterboxes it, so anything world-anchored drawn as a sprite must subtract
`(getHeight() − 448) / 2` (0 stock, 32 Pal576i, 46 HiDef1080i). The projection
divides by `projectionScale` 4096, so the screen edge is at `width/4096` of w.

## The 2D origin is not the top-left of the picture in a taller mode

**Cause:** sprite coordinates are framebuffer pixels with the origin at the
top-left — every 2D consumer assumes it (`drawHudText` at 16,16; the generated menu
compositor scaling its authored layout by `height/448`; `Renderer2D::note2dRect`
handing the rect to the frame warp as image coordinates). Upstream pinned the vertical
`XYOFFSET` for the sprite draw to a bare `SCREEN_CENTER`, only the top of the picture
while the buffer is the stock 448 rows.
**Fix:** `RendererCore2D::render` (`SPRITE_SPACE_HEIGHT`) centres the authored
448-row space in `getRenderHeightF()`. HALVE the space for `InterlacedField`
(its sprites are already squeezed into the 224-row buffer by the `v0.y *= 0.5`);
take the unhalved 448 and every sprite jumps 112 rows up. The X axis keeps the raw
constant on purpose (measured correct at both 512 and 448 wide). Verify by BOOTING
each mode.

## Motion blur gets ONE 16-bit conversion, then an explicit history break

**Cause:** `RendererCorePostFx` lerps the previous real frame over the fresh
destination in one sprite. Its 1:1 UVs are biased by +0.5 texel at both ends;
splitting the blend into two passes feeds PSMCT16 rounding back as brightness loss.
**Fix:** the generated game emits one clean frame after the camera settles, then
restores the authored amount. See `docs/motion-blur.md`.

## A camera feed samples the raster target UPSIDE DOWN

**Cause:** GS rows run top-down, texture V grows down from row 0, and feeds sample
through plain surface UVs.
**Fix:** flip the surface V. `RendererCore::camFeed` is a second
`RendererCoreEnvMap` instance for the camera texture feeds (CCTV monitors,
docs/texture-feeds.md), permanently allocated below every texture like the env map,
+128 KB VRAM, Clamp wrap.

# troubleshoot/tyra-engine-dev-lighting.md

Dynamic scene lights, the spot cone, DIMX, signed RGB SH, skin reuse and the
matcap/env-basis program. Symptom first.

## One light per bag, and a bag may REFUSE a light

**Cause:** the colour programs carry a single dynamic-light slot per mesh;
`RendererCore::pickDynLight` chooses it by score.
**Fix:** the **scene dynamic lights** registry is `RendererCore::dynLights[8]` +
`clearDynLights`/`addDynPointLight`/`pickDynLight`; `StaPipCore::render` picks the
strongest contributor — flashlight or point light — per bag on its world bounding
sphere and routes it through `StaPipQBufferRenderer::setBagLight`. A *point* light
is expressed through the SAME spot-cone constants — zero direction, `cosCut2 = -1`,
saturated `invSoft` — so no VU1 program changed and no micro memory was spent.
Three different levers, do not conflate them:
`PipelineInfoBag::dynLightPick = false` means "no scene light at all", and
`dynLightPick = false` (the generated games set it on TERRAIN CHUNKS
— neighboring chunks picking different lights truncate a pool in a hard rectangle
at the chunk border; the lights' ground pools draw as smooth additive patches
instead — and on the sky dome, camera-centered, where a nearby light would tint the
whole sky); `PipelineInfoBag::dynLightSkipSlot` names a `dynLights` index the pick
must skip (the generated game's spot receiver pass draws that lamp projected
instead); `spotLit = false` means "no torch".

## A selected light still lights a bag it should not reach

**Cause:** the picker's approximate world sphere is not the shader's object-space
range.
**Fix:** since 1.150.2 `stapip_spot_bounds.hpp` checks the existing local AABB
against the exact `StaPipClipperSpot` constants, after selection/transform.
Conservative interval expansion retains boundary cases; unknown bounds, billboards
and custom overrides remain lit as before. `setBagLight(light, bounds)` borrows
bounds only for immediate uniform preparation, never for DMA. Do not use the
picker's approximate world sphere to switch off a shader. A full transformed-light
cache was rejected: 65/65 warm hits still increased work 21.196 -> 21.436 ms and
held 24 KiB — a hit rate proves reuse, not an EE saving. Measurements:
`examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/lighting/README.md`.

## Every additive quad darkened its whole footprint by half a step on a console

**Cause:** a DIMX entry below zero. DIMX entries are **SIGNED 3-bit**; the 16-bit
blender stores `v - 1` when a pass adds zero to a pixel (`(v << 3 + dimx) >> 3`),
so every additive quad — corona, pool canvas, wall pass, particle — darkens.
PCSX2 does not dither and shows nothing.
**Fix:** keep DIMX entries non-negative; Bayer `>> 2` (0..3) since 1.70.4. When a
console shows a rectangle the size of a sprite, suspect the sprite before the pass
that happens to share its frame.

## The flashlight is black on anything close and bright only near its far end

**Cause:** the spot-light cone term's magnitude scales with distance² — only its
SIGN is the (exact, distance-independent) angular cutoff. The original
`softness / (objRange2 * (1 - cosCut2))` made the ramp up over its whole reach.
**Fix:** size `invSoft` in `buildSpotForBag` off a FRACTION of the range, never the
full range.

## The spot-light gate cost more on the pose where the light is on

**Cause:** a branch is a scheduling barrier. The first shipped shape gated the spot
INSIDE the loop; at night nearly every mesh takes the LIT path, where the branch
cost 11 cycles a triangle of lost pairing (`cull_tc` 133 -> 144). Garage day
**-1.059 ms**, outer day -0.620, outer night -0.701, **garage night +1.264 ms**.
25 090 triangles × 11 cycles predicts 0.93 ms against 1.26 measured.
**Fix:** `CalculateTyraSpotLight` is 21 upper-pipe operations a vertex — 63 a
triangle — and is gated on the SIGN of `VU1_OPTIONS_ADDR.y` (do NOT trust
`--vu-check`'s budget line for this: its upper bound is pessimistic by construction
and reads `1102..2201 of 2042`, above a ceiling the real build clears by 180), which
the EE sets from
`StaPipClipperSpot::enabled` (the same predicate `addSpotToColor` uses). Three
shapes, `cull_tc` lit/unlit: gate per corner **154/91**, one gate before the first
`MatrixMultiplyVertex` **144/81**, **two whole loops picked once per batch 133/73**.
The cull pair ships as two loops with the lit one byte-for-byte the original body;
the clip pair keeps a branch but DUPLICATES the colour clamp (+2 rather than +7).
**Price the path that does NOT take your new branch, on the pose where it is the
common one.** Measured cycles/triangle, unlit -> lit: `cull_c` 130 -> **72** / 130,
`cull_tc` 133 -> **73** / 133, `clip_c` 230 -> **173** / 232, `clip_tc` 241 ->
**184** / 243. Micro memory 1684 -> 1862 of 2042 (~1950 since 1.134.3's fog gate:
`cull_tc` carries a third, fog-free unlit loop, entered when `VU1_OPTIONS_ADDR.z`
— the fog SCALE — reads 0 in its low 16 bits). Details: docs/flashlight.md, "The
cone costs nothing when nothing is lit".

## `VU1_OPTIONS_ADDR.y` is three-state now

**Cause:** it must carry `> 0` the shared clip image's peer path, `0` base, `< 0`
base plus a live dynamic light.
**Fix:** that was free because every reader of the lane only ever tested `> 0`
against `<= 0`. Any new reader must keep that discipline, and anything that wants a
THIRD fact in that lane has to widen the tests first.

## `--vu-check` passed a spot-light program it could not see

**Cause:** `stageInput` filled the lights-direction block with random xyz and left
every W at zero — and all three of `invRange2`, `cosCut2` and `invSoft` are Ws, so
the macro's colour addend was exactly 0 in every trial.
**Fix:** fixed (plausible constants plus a per-trial alternation of the gate lane),
and the fix was FALSIFIED before being trusted: flipping one `ibgez` to `ibltz` in
the handwritten `cull_c` now fails the check within three trials. **If you add a
gate to a microprogram, prove the harness fails when the gate is inverted.**

## Signed RGB SH

**Cause:** classic lighting sums only non-negative terms.
**Fix:** `PipelineDirLightsBag::signedSH` defaults false. Both packet writers always
set `ambient.w` to 0 (classic) or -1 (SH); output alpha is unchanged. The macro
loads `ambient.xyzw`, uses w as the dot-product floor, then clamps the RGB sum
before clipping. SH uses identity directions and signed RGB axis coefficients. Keep
`vugen.cpp`, expanded shared clip C and generated as-is D/TD in sync; run
`--vu-check`, including its RGB numeric oracle, then rebuild the PS2 engine. No
extra packet qwords or probe bytes are used.

## Skin reuse

**Cause:** hard normals cannot share skinning with neighbouring corners.
**Fix:** `SkelInstance::PartLod::skinSource` points to the first corner with
bit-identical position, normal, joints and weights. UV differences can share
skinning; hard normals cannot. Skin the first corner and copy its output with VU0
loads/stores, without calling helpers that might clobber the running VU0 AABB. All
render vertices remain, with a four-byte index per corner. The temporary hash table
exists only during loading. Set `TYRA_SKEL_PROFILE` in `skel_instance.hpp` to 1 for
per-instance COP0 pose/skin timings every 100 skins; keep it 0 when shipping.

## The matcap/env program evaluates right/up in two chains

**Cause:** the StaPip `TCE` env program family computes matcap ST from normals in
the ST slot (`StaPipTextureBag::coordinatesAreNormals`) plus the camera basis at
`VU1_ENV_BASIS_ADDR`.
**Fix:** the EE uploads the basis transposed and pre-scaled as three qwords so VU1
evaluates right/up together in one accumulator chain.

## A microcode experiment that selects a whole image

**Cause:** `vclpp` has no `#if`.
**Fix:** a microcode experiment is a compile-time toggle that selects a whole IMAGE
(`stapip_vu1_experiments.hpp`; folded lights and normalized env normals ship
enabled after the 2026-09-27 physical-PS2 A/B). The alternative program is its own
`.vclpp` (`*_fold`, `*_envn`, derived from the shipping file — re-derive after
editing it), always assembled, and the wrapper picks symbols under
`#if TYRA_VU1_EXP_...` / `#else`. The k255/ADC-table image was bit-identical but
slower in all four physical console poses (+0.11..+0.20 ms); its program and switch
were removed. The winners saved 0.23..0.25 and 0.14..0.16 ms in the augmented
garage poses. See docs/vu1-clipping.md, "Measured VU1 follow-ups".

## Light beams are two batched submissions per view

**Cause:** coronas and cones must not turn the batch's packages into clip work.
**Fix:** 1.127.4, generated game: coronas in one bag, cones in another, brightness
in the vertex colours at FIX 128, one slot per call per frame (the chain REFs the
arrays), and an EE frustum test per lamp so offscreen lamps do not turn the batch's
packages into clip work.

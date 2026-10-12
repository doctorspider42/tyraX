# troubleshoot/tyra-engine-dev-clip.md

The StaPip VU1 clip program family, the billboard family, and the micro-memory /
in-band-state rules around them. Symptom first.

## Widening the cull programs' ADC test corrupted ADC bits

**Cause:** three attempts at a guard band inside `PerformClipCheck` all corrupted ADC
bits (documented in `vcl_sml.i`).
**Fix:** retired. The working approach is the StaPip `clip` programs (the default
`"clipping": "vu1"` mode), which never derive ADC from clip flags. VU1 microcode traps
paid for there: `fcand` sets VI01 to 0/1 (any-bit), NOT the masked bit pattern; a
vertex clipped to exactly `|x| = w` scales to GS coordinate 4096.0 and wraps the 12.4
XYZ2 field (clip the sides at 0.9w, let the scissor finish); every VU1 vertex-loop
count must be a multiple of 3 or the loop runs off into memory; overflowing the
2048-instruction micro memory is SILENT in release builds (the assert is compiled
out) — check program sizes with `nm` on the `.o` files when adding VU1 code.

## A lit surface BRIGHTENS the moment it touches a screen edge

**Cause:** the cull programs run `FixColor` (mini 255 / max 0) per vertex right after
the spot light; the clip programs feed Sutherland-Hodgman and only clamp in the
emitter — after the lerp. An unclamped saturated colour (a bright dynamic light
easily pushes past 255) then interpolates from its raw value.
**Fix:** clamp vertex colours BEFORE the VU1 clipper interpolates them.
`stapip_clip_{c,tc}_vu1.vclpp` now clamp before storing into the scratch polygon
(float clamp only — `ftoi0` belongs in the emitter). **Budget note:** the ceiling
alone (`loi 255` + `mini.xyz` per vertex, 6 instructions) is all that fits — adding
the matching `max.xyz … vf00[x]` floor too (9 per program) tripped the real
`VU1 pipeline programs overflow into the draw-finish program` assert (path1.cpp:145)
on the boot logo. After the 2026-08-11 shared-image work the all-class set has 366 slots
of micro-memory headroom; measure with `mips64r5900el-ps2-elf-size obj/.../clip/*.o`
(bytes / 8 = instructions) after ANY edit there. **And the clip family now has a C++
description** (`buildClipBody` in `src/vugen.cpp`), so an edit to one of those five
files has to be made in BOTH places or `--vu-check` fails — which is the point: the
ceiling above is exactly the kind of change that used to be applied to `clip_c` and
forgotten on `clip_tc`.

## The five logical clip programs occupy TWO resident images

**Cause:** `C/D` share the C image (same two-stream input and stride-2 scratch ABI)
and `TC/TCE/TD` share TC (same three-stream/stride-3 ABI — TD's normals sit where
TC's colours do, which is why the old "TD has an ABI of its own" was wrong; merging it
saved 230 words).
**Fix:** `VU1_OPTIONS_ADDR.y > 0` selects D in C's image and TCE in TC's; TD is
`.y > 0` AND `.x < 0` (`sendObjectData` writes -1 into the single-colour lane for a
resident TD bag — every reader of that lane tests `> 0` only, and TD is never
single-colour). The TD test sits INSIDE the env branch, so the TC colour path pays
nothing; TD and TCE pay two instructions. The TD path reuses the env-basis and spot
registers the preamble already loaded from the same addresses and loads only the light
colours + ambient per triangle. That still left 33 VF live in `sharedDirMode` —
Sony's `vcl` refused it ("no opt table") and only openvcl's load sinking fitted it —
so the TC image loads its five GIF-tag-block constants once per buffer at the tag
block (`tagsPerBuffer` in vugen) instead of pinning them. The wrapper objects
deliberately point at the same `CodeStart`/`CodeEnd` range and
`Path1::createProgramsCache` must alias that range to one destination instead of
uploading it twice. If a generated override replaces one peer, it gets its own image
while the built-in image remains for the other. `--vu-check` has a separate peer-path
section and an `-- EE wrappers --` section that reads the `extern u32 ..._CodeStart`
out of the wrapper in THIS tree. `stapip_clip_d_vu1.vclpp`, `..._tce_vu1.vclpp` and
`..._td_vu1.vclpp` are still compiled but never LINKED — nothing references their
symbols, so the archive member stays out of the ELF; they exist as the reference the
peer-path check runs against (TC/TD runs with `runtimeColorLane = -1`). Do not "fix"
them by pointing a wrapper back at them.

## Three output-preserving trims (2026-09-26), and the rules they rely on

**Cause:** micro memory is the constraint.
**Fix:** (1) `CalculateTyraFog` is ONE `mul.x` now: `LoadTyraFogParams` copies the fog
scale into the REGISTER's x lane (the single-colour flag in that lane is only ever
read with `ilw` from memory, the dynpip lerp reads only y), so a new caller of
`CalculateTyraFog` must take its params from `LoadTyraFogParams`. (2) A lit program's
colour is not clamped twice: the light macro clamps to 0..255 and `FixColor` is
reduced to its `ftoi0` (`cull_d`/`cull_td`, `as_is_d`/`as_is_td`), and `clip_c`'s
emitter drops its ceiling because every path into C's scratch polygon already capped
at 255 (TC's image keeps it: its env path carries raw stream colours). (3) The cull
colour loops no longer branch on single colour: the batch header writes three copies
(`+ vf00`) to `VU1_SINGLE_COLOR_COPIES_ADDR` = 1016 (free data memory above polygon
B) and the loop walks the colour pointer with a stride register, 0 or 3. Net, with
clip TD on the TC image: VU1-clipping set 1944 -> 1698 words, EE-clipper set
1602 -> 1508, both leaving room for the 206-word billboard pair (per-program table in
docs/vu1-clipping.md).

## The C++ side must keep the prim giftag NLOOP at 6× the input count

**Cause:** an undercounting NLOOP stalls the GIF.
**Fix:** keep it at 6× `gsVertexCount`. Billboard bags require multi-color, no
lighting, frustum culling `None` + no clip checks (the one legitimate `None` — the
program's per-quad ADC replaces the wrap protection), and a texture bag whose image
may be null — it carries the params channel.

## The billboard pair swaps in and out per frame

**Cause:** the StaPip `billboard` program family (`StaPipBillboardBag`: the vertex slot
carries PARTICLE CENTERS, the ST slot one qword of 2×2 basis weights per particle,
colors one per particle; VU1 expands each center into a camera-facing quad — 6 GS
vertices — from the camera right/up basis at `VU1_BILLBOARD_BASIS_ADDR` transformed by
the MVP once per mesh, and culls per QUAD with one `clipw` judgement per corner).
**Fix:** the two programs are RESIDENT whenever they fit (since 2026-09-26, the VU1
audit): `StaPipQBufferRenderer::setProgramsCache` sizes the class set the way
`createProgramsCache` will pack
it (`programSetWords` — even-rounded, a shared image counted once) and appends the
billboard pair when the total stays under `Path1::getDrawFinishAddr()`;
`ensureProgramSet` is then a no-op. With clip TD on the TC image the all-class
VU1-clipping set is ~1740 of 2042 words and the pair is 206, so every built-in
configuration fits. Only a set a project's own programs have grown past that falls
back to the old separate packet, swapped in on demand and lazily restored by the next
non-billboard bag — two VIF1 drains plus a ~15.5 KB MPG upload per transition, which
the district paid several times a frame. The boot log says which case it is (`StaPip
VU1 program set: N programs, billboards resident|swapped on demand`). Swapping
`right`/`up` on the bag and re-rendering draws the same centers for another view,
which is what a portal through-view pass needs.

## Which clipper is the default

**Cause:** TyraX adds Cohen–Sutherland outcodes in the EE clipper and the StaPip `clip`
VU1 program family (on-VU1 Sutherland–Hodgman).
**Fix:** the StaPip `clip` family is **the default** clipping mode for new projects since
M4; the EE clipper stays selectable as "Precise clipping on EE (legacy)" and remains the
load-time default for pre-M4 `.tyra` files without a `clipping` key — the routing, the
guard band and the measured numbers are in `docs/vu1-clipping.md`.

## A project's clipping mode decides whether the `clip_*` programs run at all

**Cause:** check it before concluding anything from a green frame. `"clipping": "vu1"`
in the `.tyra` generates `CLIP_VU1S[...] = {true}` in `inc/scene_data.hpp` and makes
`StaPipQBufferRenderer` upload the five `clip_*` programs; `"precise"` and `"fast"`
leave them off VU1 and use the EE clipper's `as_is_*` instead. A `vclab` copy sitting
on `"precise"` produced a pixel-identical openvcl frame that was read as "the VU1
clipper works" — it was the EE clipper, and with `"vu1"` set the same build differs in
506784 of 514600 pixels.
**Fix:** grep the generated header, not the intent:
```bash
grep -n "CLIP_VU1S" <project>/inc/scene_data.hpp
```

## The CLIP flag is a shift window, not a value

**Cause:** 24 bits, six pushed in by every `CLIP`, so a mask names a POSITION in it.
Reading it too soon after its `clipw` gives the previous vertex's answer, and a
clipper then keeps and drops the wrong edges (data-dependent: correct in one scene, a
blank frame in another). SCE keeps positional tests 3-4 rows behind their `clipw` and
reaches the source's semantics by issuing the NEXT `clipw` before the read.
**Fix:** when you touch flag scheduling, measure the window depth per read
(`clipflags.py`: how many `clipw`s precede each `fcand`), not just the row gap. And
measure it in EMITTED ROWS — four cycles of separation can come out as two rows.

## A GIF A+D giftag NLOOP undercounts / an overcounting NLOOP

**Cause:** the stray qword parses as a new giftag with a garbage NLOOP.
**Fix:** count the qwords after every PACK_GIFTAG edit (see
`tyra-engine-dev-vu1.md`, "`vclpp` / `vcl` authoring traps").

## The blend equation was a global-state barrier

**Cause:** per-bag additive blending for the reflective-material env pass
(`PipelineInfoBag::additiveBlendFix` — non-zero makes `StaPipCore::render` drain PATH1
via `sync.align3D()` and switch the global GS `ALPHA` register to `Cs*FIX/128 + Cd`
through `RendererCoreGS::setAlpha`, restoring alpha-over after the bag's own drain).
**Fix:** superseded by the in-band per-mesh ALPHA qword: `VU1_ALPHA_ADDR` +
`StoreTyraGifTags*Alpha` — every StaPip mesh's tag block carries its blend equation, no
barriers; dynpip keeps the original 7/5-qword macros.

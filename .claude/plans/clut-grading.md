# Colour grading through the CLUT

## The idea in one sentence

Every texture in a TyraX project is already palettized, so the GS is already
doing a per-pixel colour lookup for free — re-map those palette entries through
a grading curve and textured surfaces get **true per-pixel grading at zero
runtime cost**, which is strictly more than the full-screen blender pass can do.

## Why the current grading is limited

`src/grading.hpp` says it plainly: a preset compiles to what the GS blender can
do with full-screen sprites — per-channel gain, per-channel lift, and a mix
toward a constant colour. Consequences, all of them stated in that file:

- **No gamma control at all.** The blender multiplies and adds; it cannot curve.
- **Saturation is an approximation** — a mix toward mid-grey, because the GS has
  no per-pixel luma.
- Everything is affine per channel, so S-curves, crushed blacks, split-toning by
  luminance and film-style shoulders are all out of reach.

That is not a shortcoming of the implementation, it is the ceiling of that
route. The GS has **no dependent texture read**: you cannot index a texture by
the pixel's own colour, so a full-frame 3D LUT is not available in any number of
passes.

## Why the CLUT is a way around it

The lookup the GS *can* do is the palette, and it does it on the way to every
textured pixel:

- `Project::settings.textureQuant` is `4bit` (16 entries) or `8bit` (256) —
  4-bit is what `examples/vu-lab` ships and what the texture baker defaults to
  (`src/texbake.cpp`).
- `Tyra::Texture::clut` is a **separate `TextureData*`**, allocated and uploaded
  to VRAM on its own (`renderer_core_texture_sender.cpp`,
  `allocateTextureClut`). It is not fused into the pixel data.
- The VU1 tag block already carries a CLUT tag per bag (`VU1_CLUT_ADDR`), so
  nothing about the draw path needs to change.

So grading a texture is: run 16 (or 256) RGB triples through the grading curve
and re-upload one small buffer. Per pixel it costs **nothing** — the GS was
going to read the palette anyway.

## The shape this should take

Grading the CLUT alone would grade only *textured* surfaces and leave
vertex-coloured geometry, the sky and particles untouched — half a scene. And
leaving the existing full-screen pass on top of it grades textured surfaces
**twice**, which cannot be undone per object. So the three pieces belong
together:

| What | Graded by | Where |
|---|---|---|
| Textured surfaces | the CLUT, full curve, per pixel | bake or run-time re-upload |
| Untextured / vertex-coloured geometry | a per-vertex LUT on VU1 | `Slot::Color`, ~7 instructions |
| HUD, 2D, anything deliberately outside the grade | nothing | — |

and the full-screen blender pass is **switched off** when this mode is on.

The VU1 half is the other half of the same conversation and is cheap: the
builder can already express an indexed load (`Vu::mtir` puts a float in an
integer register, `Vu::lq(IVal base, …)` reads from a computed address), so a
luminance-indexed table is

```
luma  = dot3(colour, weights)
idx   = ftoi0(luma * (N-1)/255)
vi    = mtir(idx) + tableBase
out   = lq(vi, 0)
```

A 32- or 64-entry table costs that many quadwords of VU1 data memory, which has
to come out of the double buffer (22..944; the top is taken by the clip planes
and the two scratch polygons at 944..1016, leaving eight free). A true 3D LUT is
**out** — 16³ is four times the whole memory, and there is no cheap trilinear
interpolation.

## Traps, before anyone starts

- **Double grading.** The full-screen pass and the CLUT must never both be on.
  Whichever way this is wired, that has to be structural rather than a checkbox
  someone remembers.
- **Sixteen samples is not sixteen bits.** The curve is applied to entries the
  quantiser already chose, so it introduces no new banding — but a strong curve
  can collapse two neighbouring entries onto the same colour and lose detail the
  quantiser had preserved. Worth a warning in the panel when it happens; it is
  cheap to detect (count distinct entries after the curve).
- **Shared palettes.** `src/texbake.cpp` quantizes an atlas page as ONE image
  with a shared 256-colour CLUT. Grading is global, so that is fine — but it
  means the unit of re-grading is the page, not the texture, and any future
  per-material grade would break on it.
- **Is the CLUT cached in VRAM?** The renderer binds and caches texture uploads
  (the `VRAMSTAT` line in a game log counts binds, hits and re-uploads). A
  run-time re-grade has to invalidate whatever caches the CLUT, or it will
  silently keep the old palette. **Unverified** — this is the first thing to
  check, because it decides whether run-time grading is cheap or needs a new
  eviction path.
- **Bake-time vs run-time.** Baking the curve into the palette is simplest and
  free, but then the grade is fixed at build time and a cutscene cannot change
  it. Run-time re-upload keeps `Set Colour Grading`-style flow nodes working.
  These are different features; decide which one is being built.

## Verified vs reasoned

**Verified by reading the tree** (2026-08-04): the 4-bit/8-bit quantisation and
its entry counts; `Texture::clut` being a separate uploaded buffer; the CLUT tag
already being in the VU1 tag block; what `grading.hpp` compiles to and what it
says it cannot do; the VU1 memory map and the eight free quadwords; that the
builder already has `mtir` and an indexed `lq`.

**Reasoned, not measured**: that per-pixel cost is zero (it is the palette read
the GS already performs, but nobody has timed a frame with and without);
that the collapse-detection is cheap; the whole cache question above.

**Not started.** No code, no branch.

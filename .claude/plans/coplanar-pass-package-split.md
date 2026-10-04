# Coplanar companion passes must split into the same packages

## The bug, in one sentence

An object's base pass and its companion passes (env matcap, baked AO/lightmap,
emissive add) run over the *same* vertex array but split it into *different*
packages, so the same triangle can take the VU1 path in one pass and the EE
clipper in the other - and two coplanar passes then disagree about depth.

## Symptoms

- Grey, dithered wedges bleeding out from under a reflective object, worst up
  close and at the edge of the screen. The env pass's vertex colour is
  `Color(128,128,128,128)` blended additively, so the artifact is *grey*.
- Baked AO/lightmap shadows "fighting z-index" with the ground they sit on.
- Both are per-object: only objects that actually draw a companion pass show it.

Confirmed 2026-08-03 in `examples/vu-lab`: unchecking **Ambience > Bake ambient
occlusion** removed the shadow half; the reflective ball kept bleeding grey.

## Why it happens

`StaPipVU1Program::getMaxVertCount`
(`vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp`)
derives the package size from the *program class*:

```cpp
u16 res = bufferSize - 9;
u8 colorElementsPerVertex = singleColorEnabled ? elementsPerVertex - 1
                                              : elementsPerVertex;
res /= (colorElementsPerVertex + reglistCount);
res = res / 3 / 3; res = res * 3 * 3;
```

The env class carries normals in the ST slot, the base class does not, so the
two bags get different `elementsPerVertex` and therefore different package
boundaries over identical geometry.

`StaPipCore::render` (`stapip_core.cpp:141`) then feeds that size straight into
per-package bounding boxes and classifies each package against the frustum. With
`fullClipChecks = true` (what generated games set for objects), a fully-inside
package gets its perspective divide on VU1 while a straddling package is clipped
on the EE and drawn by an `as_is` program. Two routes, two microscopically
different z values, coplanar passes -> GEQUAL fails in patches. At the frustum
edge the divergence is in *coverage*, not just depth: one pass draws a region
the other drops.

## Already ruled out - do not re-investigate

- **The bbox cache is fine.** `StapipBagBBoxesCacher::getBBoxes` keys on
  `getCache(maxVertCount, id)`, so each package size gets its own correct boxes.
- **The generated `as_is` microprograms are not the regression.** `bc9921bc`
  verified them pixel-identical to the handwritten build (0 of 1 258 400 pixels
  differing, frozen camera).
- **No VU script is involved.** Deactivating the cell-shading script at runtime
  leaves the artifact untouched.
- **`clip_tce` / VU1 clipping is not involved** in the scene where this was
  reproduced: `CLIP_VU1S = {false}`, so it runs on the EE clipper.

## History

`67e2893f` (2026-07-14) "Env pass: standard z-test instead of TestOnly
(close-up punch-through)" hit this same defect from the other side. It swapped
the env pass to `PipelineZTest_Standard` and states outright that the root cause
was not pinned down. That change masked the symptom; it did not fix it. Keep the
standard z-test - it is still correct - but it is not the fix.

## The fix

Pin **one package size across every pass of an object**, so all passes classify
identically and take the same route.

1. Add an optional package-size override to `StaPipInfoBag`
   (`vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/bag/stapip_info_bag.hpp`),
   `0` meaning "derive as today".
2. Honour it in `StaPipCore::getMaxVertCountByBag` / `render`
   (`stapip_core.cpp:66`, `:141`) - it must reach both the bbox call and
   `setMaxVertCount`.
3. Game side, in `src/templates.cpp`: compute the **minimum** package size over
   the classes an object actually draws and set it on every one of that object's
   info bags - base, env (~10589), AO/lightmap (~10670), emissive add, and the
   terrain-chunk equivalents (~15081).

**The minimum is not optional.** Raising a class above its own derived size
overflows its VU1 buffer. Pinning to the minimum costs the base pass a few
vertices per package; that is the price of correctness.

## Verification

- Console A/B on `examples/vu-lab` at the camera position where the ball bleeds:
  before/after screenshots, plus AO re-enabled to check the shadow half.
- `examples/reflections` - the fixture `67e2893f` was isolated with. It must
  stay clean and keep its reflection.
- Frame time before/after on both examples; the smaller packages cost something
  and the number belongs in the PR.
- Regenerate the affected examples; every touched example's generated files go
  in the same commit.

## Scope note

This changes the draw path of every project that uses reflections, baked AO/GI
or emissive materials. It does not belong in the VU-authoring branch - cut a
worktree off `main` for it.

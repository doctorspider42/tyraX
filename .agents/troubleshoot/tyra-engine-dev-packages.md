# troubleshoot/tyra-engine-dev-packages.md

Package sizing, the bbox caches, frustum classification and the guard band, and
triangle strips. The coplanar-pass and backface-culling entries are in the index
(`tyra-engine-dev.md`). Symptom first.

## A package derived from the wrong program class overflows its VU1 buffer

**Cause:** `StaPipVU1Program::getMaxVertCount` derives the package size from the
bag's PROGRAM CLASS — how many verts of (position [+ ST] [+ normal] + color) fit in
half a VU1 double buffer. With the shipping buffer that is **111** for untextured +
per-vertex colors, **75** for textured, **90** for textured + a single color, **150**
for untextured + a single color.
**Fix:** `StaPipBag::packageSize` (0 = derive) pins the size; the generated game sets
it to the **MINIMUM** over an object's passes (`pinPackageSize` in templates.cpp).
The minimum is not a preference — pinning a class above its own derived size
overflows its VU1 buffer. The frustum-bbox cache is keyed by
`(maxVertCount, vertex pointer)`, so differing sizes made each pass recompute the
boxes the previous one had just built — pinning measured **+4–6 %** frame rate.
The objects that hit this are the mixed ones: an untextured base pass with a
textured companion (a reflective sphere without `map_Kd`, a baked lightmap, an
untextured terrain under layer passes) split one array at different boundaries, and
with `fullClipChecks` a fully-inside package divides on VU1 while a straddling one is
EE-clipped and drawn `as_is` — last-bit z differences and grey wedges.
`67e2893f`'s switch of the env pass to `PipelineZTest_Standard` was a mask for this
defect, not its fix (keep it, it is still correct).

## 75 is essentially the ceiling

**Cause:** almost everything left in `dispatch` scales with the package count, static
geometry ships as 75-vertex strip runs that ARE the packages, and the obvious next
move is to lengthen the run. `setDoubleBuffer` splits 22..944 in two for **460
quadwords** a half, `getMaxVertCount` takes 9 for the GIF tag block and divides the
remaining **451** by `elementsPerVertex + reglistCount` — so the textured classes get
451/6 = **75**. That used to round down to 72 on a multiple-of-9 step, relaxed to a
multiple of 3 and that is the whole of the slack at this memory layout.
**Fix:** the whole of VU1 data memory caps a six-quadword-per-vertex package at
**81** (the clipping scratch would have to go entirely: `DBUFFER_END` ≥ 1014 against
80 quadwords of scratch), and **144 wants 1 770 of 1 024 quadwords** — the double
buffer is exactly the factor of two that makes a doubling impossible. Moving the
clip plane table DOWN into the per-mesh constants buys **exactly zero**. The
per-class pin costs the Motor District frame nothing (every class derives 75, by two
independent routes: `cull_tc`/`cull_tce` with per-vertex colours, and `cull_td` with
a single colour; `cull_td` with per-vertex colours would derive 63 and is
unreachable, `StaPipCore::render` asserts against it). The only lever left is the six
quadwords per vertex, where the claim to measure is the PACKAGE COUNT, not the
payload (adding 16 bytes per vertex moved garage-day VIF1 wait by 0.067 ms).
Derivation, sweep, baseline and the way to reach 81 (−8.0% of packages):
docs/render-submission-attribution.md, "Round three" and "Round four".

## Raising `maxVertCount` moves the clip buffer too

**Cause:** `clipPackageSize()` is derived from `maxVertCount`, so a bigger package
means a bigger clip package, and a clip package plus its entire Sutherland–Hodgman
fan-out has to fit one 460-quadword double-buffer half. An overrun is silent
corruption PCSX2 cannot show.
**Fix:** the worst case is **7 output triangles per input triangle** and that is
exact — a convex polygon gains at most one vertex per plane and the plane loop runs
exactly six times. The constructor's `elementsPerVertex` / `reglistCount` pair is the
SIZING BUDGET, not the layout: `clip_d` is budgeted `2 + 3` but really uploads two
streams and stores two quadwords per emitted vertex, so using that pair for the
footprint reports false overruns. **Read the layout off the `.vclpp`.** This is what
took `StaPipCore::clipDivisor()` from 5 to 6 when the ceiling moved to 75: at 5 the
untextured single-colour class landed on 459 of 460 quadwords. Runnable per class:
`examples/vehicle-playground/authoring/package-ceiling-75-2026-09-16`.

## Culling used stale boxes

**Cause:** the engine bbox cache is keyed by the **vertex-buffer pointer**, not the
bag pointer (`StapipBagBBoxesCacher`'s `id` is
`reinterpret_cast<u32>(bag->vertices)`) — two bags sharing one vertex array share an
entry, and one bag swapped between two arrays gets two.
**Fix:** geometry that changes at runtime must bump `bboxVersion` on its `StaPipBag`.
**The converse is a real cost**: do NOT bump `bboxVersion` when the vertex buffer did
not actually change — the stamp disables two caches at once (the package boxes are
recomputed in `bounds`, and the retained command blocks are thrown away in
`dispatch`). Two constraints on keeping a stamp sticky: the cacher stores **no
count**, so a reused stamp is only valid while the buffer's ADDRESS and LENGTH are
also unchanged, and the retained key has no bag pointer in it, so sharing one bag
across several buffers is fine but sharing one buffer across several contents is
not. See `docs/wheel-rebake-skip.md`.

## Object ids must be unique, not random

**Cause:** `Sprite`/`Mesh`/`MeshFrame`/`MeshMaterial`/`MeshMaterialFrame` share ONE
lookup namespace in `TextureRepository`: `addLink(id)` binds a texture to a
sprite/material and `getBySpriteId`/`getByMeshMaterialId` return the FIRST texture whose
links contain that id. Upstream drew ids from `rand() % 1000000` (never seeded), so a
collision bound the wrong texture to a sprite → garbled/black HUD sprites, worst right
after opening a menu (a burst of new sprites raises the collision odds against the
always-present debug-HUD glyph).
**Fix:** `renderer/models/unique_id.hpp` `generateUniqueId()`. Use it for any new
id-bearing render object; don't reintroduce `rand()` ids.

## Full geometry and reflective prefixes thrash bbox counts

**Cause:** the 2026-09-28 stationary night-entry probes found four fresh package-bbox
replacements per frame in Ravager/Pica, about 2.9 ms of EE recomputation. Their base
and coat bags share a vertex pointer and VU package size but use different counts.
The pointer/package-size cache key merges them; its count validation then rebuilds
the entry twice per car, every frame.
**Fix:** a bounded two-count-variant cache preserves the coat in production. Keep
version-in-place invalidation and bound variants (dynamic particle counts must not
produce 250 frames of growing entries). A third count replaces the least recently
touched variant; expiry stays 250 frames. Canonical bounds, changing counts,
recycled-buffer versions and full/partial expiry passed on physical PS2: 52 273
comparisons, zero mismatches. Deep-counter-off same-ELF work drops 23.933–23.995 →
21.372 ms, still above the PAL budget. Raster and dynamic-light suppression are
diagnostic probes, not shipped quality cuts. See
`examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/fix/README.md`
and the matching lights report `…/night-entry-hardware-2026-09-28/lighting/README.md`
(docs/flashlight.md).

## A clip plane table derived from `FrustumPlanes` cut the wrong geometry

**Cause:** the clipper uses the actual MVP plus `clipMargin`, while environment-map
passes widen the view frustum on purpose.
**Fix:** do not derive the VU1 active-plane mask from `FrustumPlanes`.
`StaPipBagPackager` transforms the six VU clip half-spaces into object space once per
mesh and tests partial package AABBs against those exact planes. The six bits travel
through qbuffer ORs in bits 10..15 of the clip-only 16-bit count header; bits 0..9
remain the vertex count. A zero mask must fail safe to all six planes for manually
built clip qbuffers. The VU program reverses the bits so each `ibltz` consumes the
next plane from the VI sign bit. Upstream's default `PlanesClipAlgorithm::clipMargin`
pushes the near plane ~10 units from the camera; generated games override it.

## `PARTIALLY_IN_FRUSTUM` is not "needs clipping" (worth 2.2 ms a frame)

**Cause:** a package is classified against the VIEW frustum — the screen edge —
while VU1 cuts against the near/far pair and an X/Y band at `VU1_CLIP_XY_BAND` (0.9)
of w. So a package straddling the screen border typically crosses no VU clip plane at
all, and it used to be split into thirds, `memcpy`-ed stream by stream and run
through Sutherland-Hodgman with an empty plane mask.
**Fix:** `StaPipBagPackage::guardBandOnly` (set by the packager, read by
`StaPipCore::isGuardBandOnly`) routes it to **cull**, whole and by pointer. (With
`fullClipChecks` a fully-inside package takes the perspective divide on VU1 while a
straddling one is clipped on the EE.) Measured
on `examples/large-terrain`, 2922 paired frames: clip-routed packages 11 164 -> 2 127
per 50-frame window, qbuffer flushes 1 287 -> 756, work 6.887 -> 4.670 ms
(**1.475x**, CI [-2.258, -2.175]); the picture is unchanged (ALWAYS capture a
same-build control here — this fixture's chunk streaming is the noise floor). **The
mask is tested over EIGHT planes**: the cull programs' `fcand 0x3FFFF` covers all six
clip flags of all three vertices, `z` against `+/-w` included, while the guard band's
near constant is deliberately looser, leaving a thin shell where the clipper draws a
triangle the cull program would ADC away — a hole at point-blank range.
`computeClipObjectSpacePlanes` therefore builds the six VU planes PLUS the exact near
(`z <= w`) and far (`z >= -w`) pair at indices 6..7; those two are EE-only, never
uploaded, and `clipPlaneMask` is masked back to six bits before VU1 sees it. If you
add a plane here, decide first which side of that line it is on.

## Triangle strips for static geometry (1.95.0)

**Cause:** a strip's runs ARE the packages.
**Fix:** `StaPipBag::stripped` says the bag's `vertices` are a TRIANGLE STRIP. The
whole feature is **zero VU1 instructions** — no microprogram changed, micro memory is
still 1862 of 2042 words — because the per-vertex ADC judgement the cull programs
already write (`fcand 0x3FFFF` over the last three `clipw` results) is exactly the
triangle a strip vertex kicks. `StaPipQBuffer::stripped` is what
`StaPipVU1Program::addStandardBufferDataToPacket` turns into `PRIM_TRIANGLE_STRIP` per
BUFFER, on a COPY of the shared `prim_t` (a stripped bag's
clip-routed packages are expanded back to lists and travel in the same flush). The
bake chops the strip into independent runs of exactly `packageSize` vertices
(`meshstrip::kRun` = 72, the smallest size any static program class derives) and the
submitter pins `packageSize` to that. Every run length is a multiple of 3, padded with
a repeat of the last vertex. `renderStrippedPkgs` is a separate route and must stay
one — a stripped bag never reaches `renderSubpkgs`. A package that crosses a clip
plane is expanded to a LIST on the EE (`StaPipQBuffer::fillByStripExpand`, into the
double-buffered copy pool) and odd primitives MUST swap their first two vertices to
preserve the alternating winding (without it, near-plane geometry grows into giant
textured wedges). At the garage-day pose this fired ZERO times; from inside a
building, 29.5 packages a frame. Anything that walks a bag's TRIANGLES must read the
run length (`GeoPart::stripRun` in the generated game; the portal clipper's OUTPUT is
a list, so it clears `stripped` and the pin on its cached bag). Anything that walks
VERTICES — the shading bake, env normals, `renderAtFloor`'s y-clamp copy, the coarse
AABB — needs no change.

## The generated game's own grids (1.96.0)

**Cause:** roads and terrain are stripped too, and they are where the geometry is —
93 150 road vertices against 13 176 in all the district's models. Neither goes
through `meshstrip`: a ribbon's and a heightfield's rows ARE the strip, and
`buildRoads` tessellates on the **EE at scene load**.
**Fix:** both keep the run contract exactly (`meshstrip::kRun`, `static_assert` in
`roadgen.cpp`, multiples of 3, padded tails, two-vertex joins, `stripped` +
`packageSize` pinned — roads in `procFinishChunks`,
terrain through `pinPackageSize(pins, stripRun)`). **The strip must follow the grid's
LONG axis, and for roads that axis changes** (a collapsed span is 4 vertices plus a
2-vertex join against the list's 6 — exactly break-even, and three times the GS
primitives with two thirds degenerate). **Which interleaving you pick decides which
DIAGONAL splits each quad** — only one reproduces the list stitch's own cut.
**Terrain strips only with a terrain MATERIAL** (the untextured fallback is a
two-green checker and that colour belongs to the QUAD, while a strip vertex belongs
to two — stripping it flattens the checker). Everything else on a terrain vertex
(height, shade, ST, splat weight, the geomipmap edge snap) is a function of the grid
coordinate and shares correctly; `emisCols` reads the same per-quad base, so it rides
the same gate.



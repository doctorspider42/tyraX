# tyra-editor-dev — baked lighting, lightmaps, VRAM, motion blur

From `### 5. Conventions` (analytic per-vertex lighting bakes, shadowing an
analytic light, atlas region sizing, lightmap alpha, per-texel bakes, terrain AO
map, facing terms), `### 3`, and `## Motion-blur idle history (1.89.0)`. See
`docs/ambient-occlusion.md`, `docs/emissive-materials.md`,
`docs/global-illumination.md`, `docs/bag-content-version.md`, `docs/gs-vram.md`,
`docs/motion-blur.md`.

## Analytic per-vertex lighting bakes drift from the game
**Cause/rule:** AO occluders and emissive lights all follow one shape: `aobake`
extracts the analytic box/sphere per object (`objectShape`, shared by
`collectOccluders`/`collectEmitters`), codegen emits the table into
`ao_data.gen.hpp`, and the response is evaluated per vertex at scene load from ONE
distance-to-shape query (`occShapeAt`, a THREE-way twin: host, generated game,
viewport FS).
**Fix:** **the load-bearing rule is pruning** — the local list is collected once
per object / per terrain chunk, never scanned per vertex (an 1100-object scene
cost ~170 ms per chunk when it was), and every bake site needs its own collect
call. `rebuildStaticBatch` is the easy miss: it bakes MANY objects in one call, so
the collect belongs inside its member loop, not before it. **Per-vertex is not
good enough for a strong gradient**: a plain box face is two triangles, so the
diagonal split shows as a hard seam, and the terrain grid is coarser still
(Terrain detail 32 over 64 units = one sample every 1.94 u). Both take such bakes
through a **lightmap**: primitives through `aobake::bakeSceneLightAtlas`, the
terrain through `aobake::terrainAOMap` — each ONE 256² RGBA32 image where `A` =
occlusion and `RGB` = emissive light, read by two passes whose VERTEX COLOR
selects the channels (texturing is MODULATE: black sees only the alpha multiply;
the RGB add is modulated by white for objects, by the terrain's base tint for the
ground). A new per-texel bake means claiming a free channel in those two images,
not adding a third texture. Anything that stays on the vertex path (models,
spawned clones, physics bodies, textured object receivers) needs a flag saying
which route it took, or the term lands twice — `SCENE_AO_ATLAS_LIT` per object,
`SCENE_AO_MAP_LIT` per scene for the terrain. **Baked global illumination is the
same mechanism one step further**: with a fresh GI bake that RGB channel stops
meaning "emissive light" and starts meaning ALL the incoming light, so
`SCENE_AO_ATLAS_GI`/`SCENE_AO_MAP_GI` say the vertex shade must go BLACK
(`g_giLightmap`) — and every surface the lightmap cannot cover reads the probe
grid instead (`g_giProbeShade`, `inc/probe_data.gen.hpp`). Never both, and in
either case the ambient + directional term, the baked point lights and the
emissive pools are ALL skipped. A new geometry-baking site (a new builder, a new
LOD path, a new batch kind) must STAGE those two globals explicitly — they persist
between calls, so inheriting whatever the last object left set silently mis-shades
a whole batch.

## A re-shade leaves a stale VIF stream
**Cause/rule:** chunk/part passes that reuse one vertex buffer must share ONE
`contentVersion` (docs/bag-content-version.md) — the SECOND stamp, and the one the
generated game does not have to remember. `bboxVersion` means "the bounding BOX
moved" and `StapipBagBBoxesCacher` is its consumer; the baked VIF stream INLINES
the vertex payload, so an array rewritten in place (a re-shade, a per-frame
fresnel) leaves its block stale while every other key field is unchanged.
**Fix:** do NOT widen `bboxVersion` to cover it — that conflation IS the defect.
Every bag-backing array in the generated game is a `BagArray<T>`
(`inc/bag_array.gen.hpp`): `data()` is const, there is no public writable pointer,
the four `bind()` overloads aim the stream pointer AND the stamp together, and
every mutating member stamps. A converted fixed C array must be resized before its
FIRST updater, not merely before its bag is lazily bound — indexing an empty
`BagArray` is a null write that PCSX2 hides and hardware reports as a cause-3 TLB
miss at `BadAddr 0` (vehicle smoke, skid and glow all wrote before their lazy
render-bag setup). The one exception is engine-owned skinning (writes LOD 0 in
place into the mesh frame's arrays, which moves positions and normals, which
`bboxVersion` legitimately covers). `tools/bag-array-enforcement.sh` is the
negative test and goes red when `data()` is made non-const.

## A per-frame rebuilder bumps `bboxVersion` every frame
**Cause:** the stamp is a claim about CONTENTS, so an unconditional
`++g_bboxStamp` costs twice — `bounds` recomputes the package boxes and `dispatch`
throws away and rebuilds the retained command blocks (`bboxVersion` is in that key
too). The wheel batch did this; it was a fifth of the Motor District's render
submission (docs/wheel-rebake-skip.md). The engine's package-bbox cache is keyed
by the vertex pointer, so differing stamps make each pass recompute the boxes the
previous one just built, every frame.
**Fix:** address the buffer by SLOT rather than clearing it, keep an exact (not
hashed) signature of the inputs per slot, rewrite only the slots whose signature
moved, and bump the stamp only when one did. Two rules make a sticky stamp safe:
the bbox cacher stores no count, so the buffer's ADDRESS and LENGTH must be
checked alongside the content; and a slot table trimmed out of step with the
vertex vector claims a match for vertices that no longer exist (one frame of stale
geometry).

## A bag lit by VU1 instead of baked renders at flat white
**Cause:** the opt-in per-object dynamic lighting (`GeoPart::litBag`) inverts the
usual arrangement.
**Fix:** three things go with it. The per-vertex NORMAL capture is per PART, not
per object (`g_litNormals` staged inside each builder loop — point it at
`parts[0]` for a whole model and every part's normals pile into part 0, the
size-equals-vertices gate fails, and the model renders at the flat white albedo
`pushVert` wrote); the light colours must be built in the same colour space
`pushVert` uses for that part (255 untextured / 128 textured —
`GeoPart::litScale`), because the untextured lit program never reads the colour
bag and the albedo has to ride in the light; and the object must be excluded from
`staticBatchEligible` or the batch rebuild renders it with ordinary baked shading.
Seed the colours where the bag is WIRED, not only in the per-frame pass
(`renderObjects` rebuilds a dirty object from inside the draw loop, after that
frame's update has already run).

## Shadowing an analytic light
**Cause/rule:** it is a segment test against the same occluder shapes
(`aobake::shapeBlocksRay` — slab for a box, quadratic for a sphere; twins in the
generated game and the viewport FS).
**Fix:** three things go wrong if you skip them: the caster list must be pruned by
the LIGHT's reach (not `SCENE_AO_RADIUS`, so it needs its own list), the
receiver's own shape and the emitter's own must be excluded (the ray starts on one
and ends on the other), and the ray needs a small bias off the surface. Emitters
are AREA sources, so ONE ray only answers lit-or-black: the host bake and the
viewport cast `aobake::kEmisShadowSamples` rays (ray 0 to the nearest surface
point, the rest over the silhouette via the fixed `kEmisShadowDisk` — no RNG,
bakes must be reproducible) and use the unblocked fraction as a visibility
multiplier. Rays aimed below the receiver's horizon are left OUT of the vote.
The generated game's per-vertex path deliberately stays at one ray (measured: 8
rays = +200 ms of EE scene load on examples/glow) — the ONE place these three
twins diverge, written down in docs/emissive-materials.md.

## Atlas region sizing is importance-weighted, per AXIS
**Cause/rule:** a 6×6 probe grid per region gives the peak signal (sets the
region's AREA, density `sqrt(peak)`) and the signal's gradient along each of its
two axes (splits that area between them — a long wall's height needs density its
length does not). A bisection then raises the density until the image is full.
**Fix:** the atlas DIMENSION still comes from the unweighted area on purpose —
VRAM must not move when the weighting does. Measured dead end: simply RAISING the
old flat 128-texel per-axis cap made things worse; isotropic density was the
problem.

## A lightmap texel's ALPHA is 0
**Cause:** StaPip draws with the GS alpha test set to "pass only when alpha != 0"
(the cutout rule for foliage/decals, `stapip_qbuffer_renderer.cpp`). Both lightmap
passes sample the SAME texture, so a texel whose occlusion is zero fails that test
and takes the ADDITIVE LIGHT pass down with it — baked light silently clipped to
wherever the AO was non-zero, as hard texel-aligned holes (it looks like "the
lightmap is too low-res").
**Fix:** `aobake::kMinLightmapAlpha`. If a bake looks cut off, dump the alpha
channel before touching resolution. The engine's PNG loader scales alpha 0..255 →
0..128 by integer division, so the floor is 2, not 1.

## Per-texel bakes alias into a staircase
**Cause/rule:** a fixture close to a wall throws a sub-texel-sharp penumbra and
point-sampling it aliases.
**Fix:** average over the texel footprint (`kSuper`). Host-side cost only.

## The terrain light lands twice
**Cause/rule:** the terrain takes the same treatment through the terrain AO map,
whose RGB channels carry the light while the alpha keeps the occlusion.
**Fix:** `SCENE_TERRAIN_LIT` gates the extra additive chunk pass AND tells the
vertex bake to leave the light out — miss the second half and it lands twice. Two
traps: the occlusion pass's vertex color MUST be black once RGB is populated
(white drags the light into the multiply), and a TEXTURED terrain has to stay on
the vertex path (a flat add blows out dark texels).

## Facing terms
**Cause/rule:** `max(0, N.L)` is wrong for anything standing in for an AREA source
— it lights one face of a box fully and its neighbour not at all, seaming on the
corner.
**Fix:** the occluder bake uses a linear wrap (`0.35 + 0.65*N.L`); emissive light
uses **half-Lambert squared** (`((1+N.L)/2)²`), because a linear wrap still
reaches zero at a finite angle and in a dark scene that angle reads as a hard
shading edge.

## VRAM rule for any new texture the game loads
**Cause/rule:** the engine uploads to GS VRAM on a texture's FIRST RENDER
(`RendererCoreTexture::useTexture`) and pins it forever — no LRU, only an
all-or-nothing flush when the next texture doesn't fit, on a ~1.33 MB budget
(+8 KB per allocation).
**Fix:** add lazily; never call `useTexture()` eagerly unless you *want* it
resident (the streamed model textures do); prefer 4-bit for anything the runtime
tints. Do NOT "fix" residency by freeing on hide: `RendererCoreGSVRam::free` is
`pointer = address` (a bump-pointer stack pop), so freeing anything but the newest
allocation rewinds past live textures.

## Motion-blur idle history (1.89.0)
**Cause/rule:** `ProjectSettings::motionBlurIdleClear` is a project-wide policy
beside the authored motion-blur amount. Generated games keep the amount from the
UI Editor or `Set Motion Blur` in `g_motionBlurBase`; after about 0.12 seconds
below the meaningful camera translation/turn-rate threshold they send zero to the
renderer for ONE frame, then restore that base even if the camera remains parked.
**Fix:** never turn this back into continuous speed scaling — that silently
disables blur for objects moving past a stationary player. Reset the camera/idle
state on scene load so history cannot cross scenes. The optional key is written
only when false; update equality, settings serialization, UI, `sceneDataContent`
and both generated game templates together. Keep the detector in units per second
— the original per-frame comparison worked at 60 FPS in PCSX2 but physical-pad
drift crossed the same threshold when a console fell to ~30 FPS.

## Reference facts (upstream `runtime-rendering-and-meshes.md`, batching/bags/matrix half)
- Static batching: `staticBatchEligible`/`batchBlockedNames` in templates.cpp; `buildStaticBatchList`/`rebuildStaticBatch` in the game; `SceneObject::batchExclude` first; `staticbatch::Reason` two stages; grouping cell `max(mapW/4, 48)` replaced by `cellFor` bounded by shared draw distance (`min(80, 145)`); `drawDistance` is now part of the group KEY; `stripVerts`/`stripRun`/`pinPackageSize`; one dynamic light per bag via `lampOf`; `wantsMatrixPath` guard; host twin `staticbatch.{hpp,cpp}` + `staticbatchdisk.cpp` + `batch_ui.cpp`; oracle `verify-batch-twins.py`; `--batch-report`; `BatchOverlayBox`; `acquireTexture` caches by path; `SceneObject::dynamicLighting` vs `wantsLightPick`.
- `dirty`/matrix fast path: `RuntimeObject::dirty` vs `rebuildObjectGeometry(i, localSpace=true)` gated on `physFastPathEligible` + `updateObjMat`; `wantsMatrixPath`/`onMatrixPath`; `ObjectGeometry::matrixMode`; `coarseBox`/`coarseObjectOutside()`; `renderProcChunks()`; `k` rejects. `updateSpinners`.
- Bag arrays: `contentVersion` (chunk/part passes share ONE), `bboxVersion` (`StapipBagBBoxesCacher` keyed by pointer), `BagArray<T>` (`inc/bag_array.gen.hpp`; `bind()` overloads; `tools/bag-array-enforcement.sh`), `GeoPart::litBag`/`g_litNormals`/`litScale`; `SceneData::terrainBaseStochastic`.
- Frame settings: `project::tripleBufferingFit`/`tripleBufferingModes`/`supportedModes` twin of `RendererCoreGS::allocateVramBuffers` + `RendererCore::setDisplayOutput`; hybrid (1.153.0); GS headroom whole page rows (64x32/64x64); `presentExtrapolatedFrame`; `motionBlurIdleClear`/`g_motionBlurBase`/`g_vehBlurFix`; submission scopes; retained static commands.
- Static models: `templates::bakeStaticModels` merge order; `modelproxy.cpp` + `SceneObject::reflectionProxy` (format v57); dynamic env-map paths (`renderTerrain()` then `renderRoadChunks()`); impostors `treeimpostor.cpp`/`modelimpostor.cpp`/`impostorbake.hpp`/`impostorgpu.cpp`, fields `impostorPath`/`impostorDistance`/`impostorBillboard` (format v40), `impostorViews` (format 41).
- Viewport<->game twin rule: terrain splat (`buildTerrainChunk` + `buildTerrainChunkMesh`, per-vertex `SceneData::splat`), `tintNoise2`, reflective matcap from camera-space normal; `RecomputeFaceNormals`.

## Batching identifiers (upstream)
- The grouping runs on the EE at scene load over loaded `gameModels`/`gameMaterials`, live `g_dynLights` and engine `Texture*` pointers (so it cannot move host-side); mesh LOD, impostors, reflections, dynamic lighting and runtime-reference objects stay solo (`meshLod`).

# tyra-editor-dev — static models, materials, UV parity

From `### 5. Conventions` (material features live in the `.mtl`; anything that
imports real-world measurements), `## Static material consolidation and cheap
visual proxies (1.111.0)`, and `### Material refresh and primitive UV parity
(1.152.0)`. See `docs/model-pipeline.md`, `docs/material-baking.md`,
`docs/world-scale.md`.

## Static model consolidation merges parts it must not
**Cause/rule:** `templates::bakeStaticModels` merges OBJ parts only after their
final atlas texture, Kd/Ke, reflection state and LOD chain are known.
**Fix:** exact render-state equality is required — material names are not render
state. Build triangle strips after the merge so they may cross former `usemtl`
boundaries. Never merge ordered billboard-impostor parts: their part index is the
capture view. `modelproxy.cpp` writes a one-material convex XZ footprint prism and
assigns it through the existing non-billboard `impostorPath`; it is an ordinary
OBJ/MTL, not a new runtime asset type. `SceneObject::reflectionProxy` (format v57)
is a separate opt-in: the generated game lazily builds one 12-triangle untextured
box bag from current visual bounds and submits it only inside the two dynamic
env-map object loops (main rendering, collision and picking remain full detail).
Since 1.117 both dynamic env-map paths submit `renderTerrain()` followed by
`renderRoadChunks()` before reflected objects; the latter filters `procChunks` to
reserved owner `-3` — never call the general procedural renderer there or a cheap
128px ground cue turns into every prefab and runtime volume submitted a second
time. The probe reuses the main camera's resident ring; do not start independent
terrain streaming for a 128px auxiliary view. Since 1.117.1 both this road-only
pass and the main `renderProcChunks()` loop reject a chunk's world AABB against
the current frustum before calling StaPip (keep StaPip's precise path for
intersecting chunks; the caller-side test only removes wholly invisible bags).

## A material edit does not show, or an edit invalidates everything
**Cause/rule:** `App::listMaterialAssets` returns a cached const reference; asset
mutations invalidate it and external changes are picked up within 1.5 s.
`drawMaterialCombo` owns the direct Edit button before Texture feed. Material
saves use `Viewport::invalidateMaterial`, preserving unrelated geometry and
textures. Static models record parsed mtllib dependencies; animated overrides
become stale without joining background jobs; road and crossing draws record
their source material.
**Fix:** do not replace this with global `invalidateAssets` on a slider commit.
**Box front/back V=0 is the top**: keep `primmesh::unitBox`, generated `addBox`
and the atlas region inverse mapping in `aobake.cpp` together. Geometry UV changes
also invalidate procedural bake hashes and GI caches (including derived prelit
freshness).

## A material feature works in the editor but not in the game
**Cause/rule:** material features live in the `.mtl`, not in `project.json`. A
`SceneObject` only carries a `materialPath`; anything a material *is* rides in the
Wavefront file as a standard-looking statement, parsed FOUR times and those four
must stay in sync: `src/objparser.cpp` (host/viewport),
`App::loadMaterialFile`/`saveMaterialFile` (the Material Editor's staged
`MatEdEntry`), and `parseMtl` in the engine's `lean_obj_loader.cpp` (filling BOTH
`LeanObjMaterial` for models and `LeanMtlMaterial` for primitive materials).
**Fix:** existing members are `refl` (docs/reflective-materials.md), `Ke` emission
(docs/emissive-materials.md), `# tyra-uvrect` (atlasing), plus the editor-only
`# tyra-brightness`/`# tyra-glow`/`# tyra-glow-light`/`# tyra-bake` hint lines
that make a color x strength split round-trip. A hint that carries authored
controls behind a resolved statement (`Ke`) must have ONE resolve function
(`App::matEdKe`) used by the writer AND every preview, and the reader must keep
the raw statement out of the staged fields. Runtime plumbing for a new field means
a member on BOTH `GameModelPart` and `GameMaterial` **in both game-hpp templates**
(TPL_GAME_HPP_ORBIT and TPL_GAME_HPP_FPP — they are duplicated), copies in
`loadModelAsset`/`loadMaterialAsset`, and a staging global next to `g_primKd` set
in BOTH `rebuildObjectGeometry` and `rebuildStaticBatch` (miss the second and
batched props silently lose the feature). The asset-import and texbake `.mtl`
rewriters pass unknown lines through verbatim, so they need no change. Animated
`.glb`/`.fbx` models take a `.mtl` as an override through
`objparser::applyMaterialOverride`, which only carries `Kd` + texture — anything
else (refl, Ke) has no `.tskl`/VU1 slot and is silently ignored there; say so in
the docs rather than faking it.

## Anything that imports real-world measurements
**Cause/rule:** it converts through `ProjectSettings::unitsPerMeter`
(docs/world-scale.md) — the project's world scale, host-side only, never generated
into the game.
**Fix:** a unit is whatever a project decided it is, but a camera take is metres
and a model is metres, so a new importer (mocap, photogrammetry, a scan) reads
that one number instead of inventing a second scale field. Two rules: don't bake
the factor into the copied asset file (it then can't be corrected without
re-importing — the per-asset size lives in `Project::modelUnitMeters` / the
`"modelUnits"` section), and don't rescale content that is already placed as a
side effect of a setting change.

## Reference facts (upstream `runtime-rendering-and-meshes.md`, model/material/mesh half)
- Model formats: `tmdl.cpp/.hpp` (`.tmdl` from `templates::bakeStaticModels`; read by `TmdlLoader` -> same `LeanObjMesh`; 4-byte magic, `u32` version range, packed LE; v3 shadow proxy over `meshlod::kShadowProxyMaxTris` = 1200). `meshlod.cpp/.hpp`: `weld(..., keyNormals)` FALSE for static meshes, `kRatios`/`kMinCorners`/`kShrinkSlack`, `generateTiers`, `recomputeFaceNormals`, `primShadowMesh`, `generateShadowProxy`. `meshstrip.cpp/.hpp`: weld exact 32 bytes, `kRun` = 75, `Weld::kFull` vs `Weld::kNoNormal` (unlit single colour only, `vehbake` wheels); six seed orientations. `primmesh.cpp/.hpp` unit primitives. `objparser.cpp/.hpp`: `load(..., splitObjects)`; `applyMaterialOverride`.
- Materials live in the `.mtl`: parsed FOUR times in sync (`src/objparser.cpp`, `App::loadMaterialFile`/`saveMaterialFile` + `MatEdEntry`, and `parseMtl` in the engine's `lean_obj_loader.cpp` filling `LeanObjMaterial` + `LeanMtlMaterial`). Members `refl`, `Ke`, `# tyra-uvrect`, `# tyra-brightness`/`# tyra-glow`/`# tyra-glow-light`/`# tyra-bake`; `App::matEdKe` is the ONE resolve. Runtime plumbing: a member on BOTH `GameModelPart` and `GameMaterial` in `TPL_GAME_HPP_ORBIT` and `TPL_GAME_HPP_FPP`, copies in `loadModelAsset`/`loadMaterialAsset`, staging next to `g_primKd` in BOTH `rebuildObjectGeometry` and `rebuildStaticBatch`. `mateditor_ui.cpp` (~3.9k).
- Texture bakes: `texbake.cpp/.hpp` (`,res-baked/` mirror, `Project::textureQuality`, modelao multiply, atlas pages, `net`/stoch/sky/shadow); `pngquant.cpp/.hpp` `quantizeRGBAToMemory`; `texatlas.cpp/.hpp` (`ProjectSettings::textureAtlas`, 256x256 pages, `# tyra-uvrect`, `TEXTURE_ATLAS_INFO`, `LeanMtlMaterial::uvRect`, `g_primUvRect`, `Project::atlasControl`, `texatlas::vram()`, `--atlas-report`, `atlas_ui.cpp`); `stochtile.cpp/.hpp` (`.res-baked/stoch`, `bakedBinPath`/`factorFor`, `SceneData::terrainBaseStochastic`); `particletex.cpp/.hpp` (`writeAssets`).
- Particles: `SceneObject::particleEffect` (by NAME) -> `project::applyParticleEffects` copies values in (`ParticleEffect` + `operator==`); `particle_ui.cpp` (window key `particles`). Vehicle smoke `VehicleDef::smokeEffect` -> `VEHICLE_SMOKE_LOOKS` via `vehbake::smokeLookOf`; `VehFx`; `acquireTexture`; `MATERIAL_PATHS`; built-in `vehicles/fx-smoke.png` = `particletex::generate(vehbake::builtinSmokeRecipe())`; `--bake-particles`. Flipbooks `framePath(mtl, k)` -> `emitterFlipbookFrames`. Billboard basis screen-LEFT/DOWN, weights `m00..m11` negation.

## Model/vehicle identifiers (upstream)
- The static-batch disk supplier `staticbatchdisk.cpp` is a separate TU that may include templates.hpp for `bakedModelPath` so the host twin links alone. `VehicleDef::smokeEffect` wins over `smokeMaterial`.

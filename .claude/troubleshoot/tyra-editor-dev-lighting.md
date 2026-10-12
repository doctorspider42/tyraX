# tyra-editor-dev — probes, SH receivers, impostors

From `## Animated probe directions`, `### Full RGB SH receivers (1.74.0)` and
`## Distant foliage impostors (1.73.0)`. See `docs/global-illumination.md`,
`docs/impostors.md`.

## Animated probe directions
**Cause/rule:** `templates.cpp::giSHLights` is shared by `fillDynLitColors` and
animated rendering. Both TerrainGame header templates carry `AnimPart::litDirs`
and `ObjectGeometry::animLightMat`.
**Fix:** pose sharing must never share light directions. The latter matrix removes
instance scale from the normal transform (the VU1 lit programs do not normalize).
Viewport `SHADE_COMMON::giProbe` uses `uGiReceiver` for the matching centre lookup
and signed RGB sum; static probes also use full L1. See
`docs/global-illumination.md` and `examples/probe-lighting`.

## Full RGB SH receivers (1.74.0)
**Cause/rule:** the generated `giSHLights` replaces dominant-direction extraction
for animated and explicitly dynamic-lit receivers. It fills identity directions,
signed RGB axis colors and L0 plus live ambient, setting each part's `signedSH`
flag every frame.
**Fix:** clear all unused colors when falling back to classic lighting. The
animated viewport twin uses the same centre sample and signed sum. The probe
cache/file format is unchanged.

## Distant foliage impostors (1.73.0)
**Cause/rule:** `treeimpostor.cpp` bakes eight orthographic captures with binary
alpha and RGB padding into a 4x2 atlas and eight ordered OBJ material parts; only
one part (two triangles) draws. `treegen::writeImpostor` serves the UI and grove
generator. Objects store `impostorPath`, `impostorDistance`, `impostorBillboard`
(JSON `impostor`, `impostorDistance`, `impostorBillboard`, format v40).
**Fix:** update equality, serialization and recipe hash together. Default false
preserves legacy far OBJs. Codegen retains the original collision identity; the
billboard path selects a camera-local yaw sector and updates six positions/STs in
place. All eight parts must survive the model bake in order. Unsupported
tilted/nonuniform-XZ transforms fall back to near geometry. Viewport capture
selection and stable upward lighting must match the runtime. The OBJ loader
recomputes face normals: writing `vn` alone cannot change their shading. No
engine/VU changes.

## Universal OBJ capture (1.74.0)
**Cause/rule:** `impostorbake.hpp` describes textured parts; `treeimpostor.cpp`
owns the shared rasterizer/tree adapter and `modelimpostor.cpp` loads OBJ/MTL
parts, overrides, Kd and source images. `props_ui.cpp` exposes Bake impostor.
**Fix:** it returns one scene commit after success, invalidating viewport assets.
No serialization addition. Missing inputs and reflective/emissive parts fail
before writing. The grove's host helper bakes both its trees and a waystone via
this adapter — keep that helper's compiler command in sync with its dependencies.

## Selection bounds (1.74.1)
**Cause/rule:** viewport `selectionBounds` grows a world AABB from `pickBounds`
plus the actual far representation (`pickVisual`). The same bounds serve
static-model picking and all local/peer outlines.
**Fix:** surface hits use cached CPU OBJ triangles and alpha masks before
AABB/margin fallback; placement keeps its original authored bounds. Clear pick
caches with asset invalidation and alpha masks on texture painting. Selection
outlines disable depth only for their pass. Keep capture yaw/eligibility in sync
with the viewport model draw.

## Impostor capture/counts (1.75.0, format 41)
**Cause/rule:** `impostorViews` defaults to 8 and accepts 4/8/16. The requested UI
count is pending until successful bake; the committed count drives picking,
viewport and the generated runtime. `impostorgpu.cpp` supplies an optional capture
callback registered in main, leaving CPU-only tools free of GL dependencies.
`bakegl.hpp` shares hidden-context creation and RAII restoration with GI, but the
two own separate contexts (GI may bake on a worker).
**Fix:** GPU capture uses a private function table, never overwrites gl_loader's
viewport pointers, and allocates texture storage before filling it (AMD driver
workaround).

## Reference facts (upstream `lighting-and-bakes.md`)
- Shared rules: host-only, no GL; determinism seeded from each element's OWN identity; signatures hash CONTENT via `wire::hashFileEolAgnostic` (GI cache v8, model AO cache v2, pre-lit `kSigVersion` 2, shadow cache v8); bumping a cache version forces re-baking examples; a discrete decision must not ride on a tolerance; objects absent at runtime (`scrollsim::memberTemplateFlags`, invisible walls) do not bake; pollers run from `drawUI` (`giBakerPoll`, `litBakerPoll`, `modelAoPoll`, `shadowBakerPoll`, `blssPoll`, `updateTick`). CLI: `--bake-status <dir>` (exit 3 = stale), `--bake-gi`, `--bake-shadows`, `--bake-prelit`, `--bake-model-ao`, `--bake-particles`, `--gi-gpu-check`.
- Infra: `bakepar.hpp` (~70) dynamic-schedule parallel-for (`jobBake`/`jobLit`, `rowOcc`/`rowLight`); `bvh.cpp/.hpp` flat binned-SAH (matbake + gibake); `bakegl.hpp` hidden context (`ScopedCurrent`); `Bake::timings`.
- AO/lightmap `aobake.cpp/.hpp` (~2.1k+370): per-pixel RGBA32 `terrainAOMap` (RGB = baked emissive light; `SCENE_AO_MAP_OCC`/`SCENE_AO_MAP_LIT`; `buildTerrainChunk`); `bakeSceneAoAtlas`/`bakeSceneLightAtlas` -> `.res-baked/aoatlas/` + `ao_data.gen.hpp` (regions mirror primmesh UV: box 6/sphere 1/cylinder 3/cone 2/plane 2; `g_aoRegion`; `primmesh::unitBox` + generated `addBox`); `collectOccluders` (uses GL-FREE `Viewport::modelBounds`, never `modelDraw`) is the single source for codegen and viewport uniforms (`SceneObject::castShadow` in `liveLinkRecipeHash`); per-vertex fallback `occShapeAt`/`aoShadeMul`/`shapeBlocksRay` (three-way twins); `kEmisShadowSamples` (8) + `kEmisShadowDisk`; `kMinLightmapAlpha` = 2; `kSuper`; facing wrap `0.35 + 0.65*N.L`, half-Lambert squared. Settings `aoEnabled`/`aoStrength`/`aoRadius`. `aobake::LightFn`/`ModelAabbFn`/`objectShape`/`regionCountFor`; `SCENE_AO_ATLAS_LIT`. Emissive: `ambience::bakedHour`.
- GI `gibake.cpp/.hpp` (~1.55k+290): tessellates (primmesh, objparser, terrain); ground grid `Scene::groundX/groundZ/groundH`; `groundSurfaceY`; `aobake::LightFn`; `SCENE_AO_ATLAS_GI`/`SCENE_AO_MAP_GI`; L1 SH probe grid (`g_giLightmap`/`g_giProbeShade`, `inc/probe_data.gen.hpp`); cached `.res-baked/gi/`; `App::drawGiBakeSection`; `gibake::Timings` (atlas/terrain passes = 98%).
- GPU GI `gigpu.cpp/.hpp` (~670+105): GL 4.3 compute twin of `gibake::gather`+`directAt`+`skyRadiance`+`bvh::trace`/`rayTri`; `--gi-gpu-check <projectDir>` fails above 1% relative mean error; hidden GLFW context, never `glfwTerminate`, `ScopedCurrent`; machine-global `EditorConfig::giGpuBake` / `App::giGpuBake_`.
- Probes: `templates.cpp::giSHLights` shared by `fillDynLitColors`; `AnimPart::litDirs`/`ObjectGeometry::animLightMat`; viewport `SHADE_COMMON::giProbe`/`uGiReceiver`.
- matbake `matbake.cpp/.hpp` (~1.1k+185): conservative UV rasterization, BVH, cosine golden-spiral rays; `matbake::Baker` (`snapshot()`/`version()`, `MeshInput::signature`).
- Model AO `modelao.cpp/.hpp` (~520+195): matbake AO per asset multiplied into the shipped texture; `applyToRgba` is THE multiply (called by texbake, `Viewport::glTexture`, litbake); cache `.res-baked/modelao/`; `Project::modelAoMode`; `App::drawModelAoSection`/`modelAoPoll`.
- Pre-lit `litbake.cpp/.hpp` (~750+215): per-TEXEL light multiplied into one object's texture; albedo from the model's OWN `.mtl` but `modelao::applyMapFile` folded in; `applyToObject`; `sceneSignature` hashes the scene AS AUTHORED; fields `prelitWanted`/`prelitSig`/`prelitSource`; `App::drawPrelitSection`, `App::litBakerPoll`.
- Shadows `shadowbake.cpp/.hpp` (~1.44k+280): `shadowMode == 4` -> tile -> 256x256 atlas -> `decalproj`; `project::resolvedSettings(...).lightDir`; `casterTree`; `kSelfFloor` (1.0); `decalproj::projectorBasis`/`eulerFromBasis`; ground shadow maps `Options::groundRes` + `kGroundChunkCells` (16) twin `TERRAIN_CHUNK_CELLS`; `bakedShadowAutoBake`/`App::shadowBakerPoll`; `loadAny` vs `load`; cache `.res-baked/shadow/`.
- Other: `decalproj.cpp/.hpp` (`project`, `decalDataHeader` -> `inc/decal_data.gen.hpp`; `Receivers::roads`); `blobshadowbake` (`SceneObject::blobShadowTexture`/`blobShadowSize`, `objectBlobTextureFor`); `occlusionbake` (`occlusion_data.gen.hpp`); `reflscenery.cpp/.hpp` (`reflectionScenery`, `verdictFor`, `collect`, `reflSceneryTable`, `REFL_SCENERY`, `objectRuntimeMovable`, `reflectionGroundProxy`, `REFL_GROUND_*`).
- Ambience/day-night `ambience.hpp/.cpp` (~300+360): `AmbiencePreset`; `DayCycle`+`DayKey`; `ambience::evaluate`; `project::resolvedSettings`; clamp +5 deg elevation; `Resolved::shadowFade`; `renderProjShadows`; `kMaxLightElevation` (88); `M4x4::lookAt`; `project::clampDayCycle`; `templates::dayNightHeader` -> `inc/daynight.gen.hpp` (`driftGrade`); `postFx.setGrading`; `ambience::bakedHour` (`DAYCYCLE_STARTS` vs `DAYCYCLE_BAKEDS`); grading.hpp (~80).
- Night sky `starfield.cpp/.hpp` (~150+60): `buildStarField`, `StaPipInfoBag::additiveBlendFix`, `kTiers` = 3, `mix32(seed, index, channel)`, corona `flare-corona.png`; `skytex.cpp/.hpp` (~55+45): `crop`, `kVMax`, `domeUv`, `SKY_TEXTURE_*`, `buildSkyDome`. Shells set `blssProxy = false`.

## Lighting twin/verification identifiers (upstream)
- Per-vertex AO twins: host `occluderOcclusionAt` <-> viewport `aoOcclusion` <-> generated `aoShadeMul`; analytic shape test `occShapeAt`/`shapeBlocksRay` (viewport FS too). `giLayout` is the CPU-pinned atlas pre-pass. `g_shadowFade` multiplies only DIRECTIONAL caster alpha; `depthGrad` is a BLSS feature; grading fixed point uses `mixAmt` (0..128). `modelAO` + `.aov` per-vertex model receive is parked. `SKY_TEXTURES_ON` gates the sky-dome `domeUv` copy. Verify deterministic bakes with `cmp` on `.res-baked/gi/scene<N>.gi` and `.shadow`, never by assertion.

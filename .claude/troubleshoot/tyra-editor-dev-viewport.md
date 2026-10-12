# tyra-editor-dev — object grouping, picking, screen icons, terrain

From `## Object grouping`, `### 3` (comments, screen icons, terrain optional) and
the `viewport.cpp` source-map notes. See `docs/object-groups.md`,
`docs/comments.md`, `docs/editor-performance.md`, `docs/terrain.md`.

## Object grouping
**Cause/rule:** `SceneObject::editorGroup` is a scene-local persistent selection
identity, saved by objectJson/parseObject and compared by operator==
(history/session sync). Expanded child rows call `selectOnly(i, false)` for
individual Properties/editing; `selectionGroupMember_` keeps undo from expanding
that selection. Other App selection helpers expand membership before manipulation;
the existing gizmo applies a world delta about the centroid. Properties uses a
rigid matrix delta for group rotation too.
**Fix:** paste renames groups and internal object references; prefab capture
clears group membership. Groups are editor-only, with no generated runtime
hierarchy.

## A static model is culled when it should be visible (or vice versa)
**Cause/rule:** static-model viewport draws call `modelInView` on imported local
bounds and the actual MVP before the per-material loop.
**Fix:** keep bounds conservative — animated poses bypass this rejection, and
reflected views keep their own draw path. See docs/editor-performance.md for
measurement caveats.

## Comments and screen-space icons
**Cause/rule:** `PrimitiveType::Comment` (docs/comments.md) is the reference for a
type NOT drawn as geometry: the viewport skips it and the app draws a
**screen-space icon** over the finished image (`App::screenIcons` /
`drawScreenIconOverlay`, the `drawMeasureOverlay` shape over
`Viewport::projectToImage`). Particle emitters joined the same list
(`ScreenIcon::emitter`).
**Fix:** three things the arrangement needs. **ONE function computes where the
icons are** and both the overlay and the picker read it (the axis-gizmo
arrangement) — two answers is a feature you can see and cannot click. **The hit
test is in SCREEN space, at the head of `App::viewportPick`** — a fixed-size icon
stays clickable at any distance while the object's 3D `pickBounds` box shrinks
below it, so the 3D box is only there for the rubber band and the gizmo. **The
View toggle controls text, not existence** — comment icons always stay visible and
clickable; `showCommentText_` only decides whether every note's text expands or
just the selected note's, and defaults off. Layer visibility still goes through
`App::isObjectHiddenInEditor`. Its text reaches NOTHING downstream, which is why
`commentText` is deliberately out of `liveLinkRecipeHash`. Creating one focuses
the Properties prose field, but that focus is necessarily two-stage:
`pendingFocusWindow_` brings the dock tab forward at the END of the frame, so
`commentFocus_` must stay armed until `IsWindowFocused` says the tab is really
front (an inactive dock tab still executes the Properties body with `SkipItems`
set).

## The terrain is optional
**Cause/rule:** `TerrainConfig::enabled` (docs/terrain.md) is the reference point
for "a subsystem a scene can be built without". The flag lives in `TerrainConfig`,
so it travels through `project::create`'s `terrain` argument, the scene table's
`"terrain"` object and `SceneData`'s `operator==` with no new plumbing — but
*reading* it is spread by design.
**Fix:** **one decision, made once, in the height sampler**:
`terrainHeightAtScene` returns `TERRAIN_VOID_Y` (a deep but FINITE -1e6) when the
scene has no terrain, and ~30 call sites in the generated game (walkers, physics
contact, spring arm, raycasts, `aoShadeMul`'s ground term, blob-shadow fade)
become correct with no branch of their own — finite matters (an infinity produces
NaN geometry). **Explicit `TERRAIN_ENABLED` only where a site BUILDS something**:
`resetTerrainChunks`, `setupLightPools`, the projected-shadow patch, the rain
particle's fall length, and the player spawn Y. **The host bakes each read
`sc.terrain.enabled` themselves** (`navmesh::bake` returns an empty grid,
`decalproj` drops the terrain receiver, `gibake` skips the ground soup + the
terrain lightmap and mixes the flag into its cache signature, `aobake`'s atlas
leaves the ground-contact term out, texbake skips the ground
textures/stochastic supertiles); `collectTexturePaths` skipping a terrain-less
scene is what makes `TERRAIN_TEXTURES` -1. **The editor's twin is
`Viewport::terrain_.enabled`**: `buildTerrainMesh` builds nothing,
`terrainRaycast` misses, and the AO ground-contact uniform is forced off;
`App::placementHeight()` returns an EMPTY `placement::HeightFn`. So a new consumer
of the terrain asks `sc.terrain.enabled` on the host and `TERRAIN_ENABLED` in a
generated builder — but a new consumer of the terrain *height* needs nothing.

## Reference facts (upstream `viewport.md`)
- GL: `glUploadTexRgba` only (allocate-then-fill; AMD `atio6axx.dll` `0xc0000005`); `gl_loader.h/.cpp`; every preview owns its OWN FBO (`renderMaterialPreview`/`prevFbo_`, `renderAnimPreview`/`animFbo_`, `renderTreePreview`/`treeFbo_`, `renderImportPreview`); `ensurePreviewBackdrop` shared; particles drawn with `glColorMask` alpha off.
- Camera: `kOrbitPitchLimit` (1.5 rad = +-85.9 deg); `pan`/`dolly`/`fly` (`NavConfig::dollySensitivity` 0.1..8); `camView()` resolves `Viewport::Projection` (perspective/parallel/six axis views) + Cutscene override; `camRay()`/`projectToImage()` inverse; `orbitBase_` recorded by `setProjection`; `App::drawAxisGizmo` (app-side, vetoes click-consuming branches).
- Picking: `pickBounds()` is the ONE answer; `selectionBounds`(1.74.1)/`pickVisual`; `pickAll()` two tiers + wire boxes; `App::viewportPick` cycles `##pickmenu`; `pickStackStatus`; screen icons hit first; `gizmoEdited_`/`gizmoClick`/`Manipulate`; `placementRaycast`; `modelInView`; `setPeerSelections`; `RoadDraw`; noise caches.
- Previews: `shadeOf`/`PreviewLight`/`ScopedShade`/`matPrevModel_`; `buildMatPrevAnimated`; `materialPreviewPick`; `updateTexturePixels` (`texCache_`); `modelBounds()` (GL-free, `aobake::ModelAabbFn`); `grabPreviewRgb()`/`lastImageFbo_`; `App::listMaterialAssets`/`drawMaterialCombo`/`Viewport::invalidateMaterial`; async `AnimBakeJob`/`animBakeCollect`/`invalidateAnimatedModels`/`SkelCache`/`clearModelCache`/`animBakesPending`.
- PS2 output `setPs2Output`/`Ps2Output`: `render()` reassigns `width`/`height` to GS framebuffer; final pass `PS2_FS` shares `GRADE_VS`/`gradeVao_` into `outFbo_`; `fbWidth_`/`fbHeight_` = GS size, panel = `outW_`/`outH_`; `Viewport::ps2LetterBox` agrees with `App::drawSafeAreaOverlay`; geometry from `App::ps2ViewportOutput` via `project::displayModes()` (host twin of `RendererSettings::updateGeometry` + `RendererCoreGS::presentFrameBuffer`).
- PS2 shading `setPs2Shading`: `SHADE_COMMON` (litShade/envSt/envColor) shared with the per-pixel FS; `uPs2Flat`; `useSceneProgram`; `PS2_VERTEX` divergence; `buildSpotForBag`/`CalculateTyraSpotLight` (vendor/tyra); `FS_VTX_MAIN`/`uAoPerPixel`. GS colour `setGsColorSim` (5-bit quantize + DIMX; `ProjectSettings::colorDepth`/`dither`); static vertex colours Gouraud.
- Baked lighting display: `setGiTerrain`/`setGiAtlas` -> `uploadGiMaps`/`uLmMode`/`lmApply`/`lmMeshFor`/`regionPoint`; `uLmTex` sampler unit 4 (both programs); `SHADE_COMMON::giProbe`/`uGiReceiver`; `collectOccluders`; `Viewport::terrain_.enabled`; `buildTerrainMesh`/`terrainRaycast`; `buildTerrainChunkMesh`; `tintNoise2`.
- Light beams `drawLightBeams`: corona billboard + eight-segment cone mirror `updateAndRenderLightBeams` incl. camera PULL; `coronaTex()` shared with ground pools; `menubake::kCoronaSpriteSize` (pools once read `kFlareSpriteSize`); `kFlareSpriteSize`; `ProjectSettings::colorDepth`; `dither`; `drawLightBeams`.

## Viewport identifiers (upstream)
- PS2 output pass point-scales `fbo_`/`gradeFbo_` into the panel-sized `outFbo_`. `grabPreviewRgb()` avoids a straight `glReadPixels` of a 1600x900 frame. `dolly` leaves `distance_` alone (it is a pan, not a zoom). The preview Light combo uses `ScopedShade`, NEVER `setLighting` (scene-wide).

# tyra-editor-dev — assets, model import, animation, UV unwrap, tree generator

From upstream `assets-import-animation.md`.

## Asset Browser (`assetbrowser.cpp` ~2.2k, docs/asset-browser.md)
- `res/` IS the asset database: views over the file system (`scanAssetTree`).
  **`rebuildAssetUsage`**: ONE pass over the model recording every stored asset
  path (unused ring, inspector user list, delete warnings), keyed off
  `modelEditSerial_`.
- **The sibling invariant**: Wavefront references (`mtllib`/`map_Kd`/`refl`) are
  bare names resolved next to the referencing file and the PS2 cannot walk `..`,
  so `moveAssets` moves a transitively closed dependency group
  (`assetWavefrontDeps`), COPIES a dependency still needed, and REFUSES rather
  than half-applying; `renameAsset` rewrites siblings (`rewriteWavefrontRef`) and
  carries an exclusively owned `.mtl`.
- `retargetAssetPath` is the single list of every field storing an asset path.
  Sidecars (`.uvs`, `.aov`, `.drone`, `<tex>.layers/`) travel with their asset; a
  baked `.tmdl` is deleted for the next build.
- A new file type joins `assetKindOf`/`assetKindName`, filter-chip counts,
  `matchesFilter`, `kindColor` (APPEND; cases are numeric), `activateAsset` and the
  inspector switch. Host-only except `Viewport::assetThumb` (a preview FBO).

## Model importer (`modelimport.cpp/.hpp` ~1.17k+150, `modelimport_ui.cpp` ~940)
- The ONE importer: host-only `load` -> `plan` -> `write` (+ `staticTwin`),
  shared by the Import model window and `--import-model`. Every format lands in
  one neutral `Source`; static = `.obj` + `.mtl` + PNGs, animated = `.glb`/`.fbx`
  + `.mtl` + PNGs. There is always a `.mtl` (white `default`); an animated model
  wears it through `materialPath` (`addModelObject`/`assignModel`,
  `applyMaterialOverride`).
- `.fbx` is read straight off ufbx, NOT through `parseSkel` (the skeletal parse's
  byte palette slot lost objects past the 256th in a 546-prop pack). `.glb` still
  goes through `parseSkel`. Static vs animated is the ENTITY's choice
  (`openModelPicker`); a `.glb` placed as static gets a `<stem>.obj` twin
  (`staticTwin`, bind pose). `assignModel` is the one answer to "what resets when
  a model changes".
- Pickers list from `scanAssetTree`, never `listAssetFiles("models")`;
  `pickableModel` excludes `impostors/`, `proxies/`, `procgen-` chunks; the
  Folder field goes through `normalizeModelFolder`. `objparser::load(...,
  splitObjects)` keeps `o`/`g` groups apart. Preview
  `Viewport::renderImportPreview`.

## Animated model parsing
- `glbparser.cpp/.hpp` (~1.6k+210): glTF binary importer + morph-frame baker; the
  PS2's DynPip renders MD2-style morph frames, so clips are SAMPLED on the PC.
  `parseSkel` builds `glbparser::Skel` for the `.tskl` bake; `uniqueNodeNames`
  runs at the end of BOTH `parseSkel` implementations.
- `fbxparser.cpp/.hpp` (~1.1k+80): FBX via vendored ufbx (`vendor/ufbx`), filling
  the SAME `glbparser::Baked`/`Skel` structures; the `animimport::` namespace is
  the extension dispatch. FBX curves resampled at 24 Hz + RDP-reduced.

## Clip editing (`animedit.cpp/.hpp` ~380+80, docs/animated-models.md)
- Non-destructive, host-only. `Project::animClipEdits` holds one `AnimClipEdit`
  per touched (model, SOURCE clip): rename, time scale, trim window, default loop.
  `ProjectSettings::animSourceFps`/`animPlayFps` give `projectTimeScale`.
  `applyClipEdits` folds them into a parsed `Skel` before `writeTskl` in
  `bakeAnimAssets`. The viewport is the twin: `Viewport::setAnimEdits` /
  `updateAnimPose` / `renderAnimPreview`. Clip names are SOURCE names; references
  store the EFFECTIVE name (`effectiveName`/`sourceName`, `App::effectiveClips`,
  `App::renameAnimClipRefs`). `animedit::importsFor` is the ONE translation of
  `Project::animImports` to `ImportSpec` (read by `bakeAnimAssets` and
  `Viewport::setAnimImports`).

## Animation import and retargeting (`animmerge.cpp/.hpp` ~2.35k+360)
- Host-only; the merge operates on the PARSED SKELETON (`glbparser::Skel`).
  Same-skeleton merge matches bones BY NAME (exact, then namespace-stripped +
  lowercased: `mixamorig:Hips` finds `Hips`). **`TranslationMode`** keeps
  translation on ROOT BONES ONLY by default; dropped channels fall back to the
  TARGET's bind transform. Root motion re-expressed via `retargetRoot`.
  `SkelNode::name` is HOST-ONLY.
- Traps: `poseGlobals` may not assume parent-before-child; never parse two models
  per frame (`compatibility()` inside `App::animImportProbe`, cached in
  `animProbeCache_`). `skelToBaked` is the PREVIEW half; `bakedWithImports` is the
  gate; `refreshPoseBounds` re-unions `Skel::min/max`.
- Bone-mapping editor: `MergeOptions::boneMap` consulted BEFORE name matching;
  `suggestBoneMap` only offered; `bindGlobals`; `buildBoneInfos` multi-signal
  (synonym table `boneSynonym` e.g. upperarm/arm, thigh/upleg, token Dice +
  joined-name LCS, side words collapsed under a HARD side guard `geoSide` = sign
  of X, bone-tree structure, normalized bind geometry, anchored consistency over
  8 rounds); weights 0.5/0.15/0.2/0.15, threshold 0.5. `detectAffixRule` /
  `applyAffixRule`. Alias book: `canonicalBoneKey` -> target name in
  `<configDir>/bone-aliases.ini`, capped 800. `posedPreview` (Test pose) bounded
  at 0.06. AI assist `aiMapPrompt`/`parseAiBoneMap` via `aigen::Generator`.
- Stall rules: `animmerge::SkelCache` (`App::skelCache_`) parses a FILE once
  (size+mtime); `invalidateAnimCaches` deliberately does NOT clear it;
  `glbInfo` for a model WITH imports reads `animmerge::mergedSkel`.
- Full retargeter (v1.30): `buildRetargetCtx`/`retargetLocals`/`mergeRetargeted`;
  path by BIND ORIENTATION GAP (`retargetInfo`; < 3 deg = verbatim, else full
  path). Full path RESAMPLES at 24 Hz; facing from `feetForward`; mirror via
  `canonicalBoneKey`; twist bones HALF the child's twist. `TranslationMode`.
- Pickers list `App::effectiveClips`; a model with NO imports routes straight to
  `animimport::bake`.

## UV unwrap (`uvunwrap.cpp/.hpp` ~510+55, Material Editor > UV check)
- Smart-project: normal-clustered charts, per-chart planar projection + tightest
  bbox rotation, ONE global scale, shelf packing with bleed margin; deterministic.
  `unwrapObjFile` rewrites a static `.obj` IN PLACE (non-`vt` lines byte-for-byte);
  `unwrapTriangles` unwraps a position-welded soup for ANIMATED models writing a
  `<model>.uvs` sidecar ("TXUV", per part: material name + corner UVs) that
  `animimport::bake` AND `animimport::parseSkel` fold in (fbxparser.cpp). Parts
  match by material name + vertex count (stale sidecars self-ignore); texbake
  treats `.uvs` as editor-only.

## Tree generator (`treegen.cpp/.hpp` ~830+130, docs/tree-generator.md)
- EZ-Tree-inspired, host-only pure functions over `Params` (harness: link
  treegen.cpp + define `STB_IMAGE_WRITE_IMPLEMENTATION`; PROGRESS 104).
  `Params::height` is the tree's SIZE (`thickness`/`leafSize` are fractions).
  `Params::crown`: 0 spread or 1 conical (`conicalWhorls()`). `generate()` builds
  a recursive skeleton; `bakeBarkTexture`/`bakeLeafTexture` bake tileable 128^2
  textures (hard 0/255 alpha, dilated margin); `writeAssets()` emits `.obj` +
  `.mtl` + PNGs into `res/models/trees/`; `writeImpostor` bakes impostors.
  Deterministic in `Params` (RNG derives from parent seed + child index). Enters
  the scene through `addModelObject()`; never a scene-object type for it. UI:
  `App::drawTreeGeneratorWindow`/`rebuildTreePreview`/`addTreeToScene` (hud_ui.cpp)
  + `Viewport::renderTreePreview` on `treeFbo_`.

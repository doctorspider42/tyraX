# troubleshoot/tyra-editor-dev-model-import.md

The model importer (`modelimport.cpp/.hpp` ~1.17k+150, `modelimport_ui.cpp` ~940,
`--import-model`) and the Model / Animated model pickers. Symptom first. Feature
guide: `docs/model-import.md`.

## A large multi-object `.fbx` imports only its first 256 objects

**Cause:** `.fbx` was read through `parseSkel`, whose rigid-mesh matrix-palette
slot is a BYTE, so a 546-prop pack lost every object past the 256th.
**Fix:** `.fbx` is read straight off the vendored `ufbx`, never through
`parseSkel`. `.glb` still goes through `parseSkel` and keeps that documented
256 limit.

## The model picker shows ZERO models for a project

**Cause:** the picker listed `listAssetFiles("models")` - only `res/models`.
Models may live anywhere under `res/` (`examples/showcase` keeps 105 in
`res/aster/`), so such a project listed nothing. This is the whole old-project
compatibility story; no migration exists.
**Fix:** pickers list from `scanAssetTree` (every model under `res/`), never
`listAssetFiles("models")`. `pickableModel` excludes build products and derived
models (`impostors/`, `proxies/`, `procgen-` chunks); the Folder field goes
through `normalizeModelFolder` (res/-relative, safe segments, no `..`).

## A static `.glb` has no `.obj` twin, or the sibling `.mtl` got rewritten

**Cause:** static vs animated is the ENTITY's choice (Add object > Object >
Model... vs Animated model...), not the file's - the kind is the extension of
`modelPath`.
**Fix:** a `.glb` placed as a static Model gets a `<stem>.obj` twin from
`staticTwin` (bind pose) and an existing `<stem>.mtl` is left untouched. There is
always a `.mtl` (white `default` when the source has none); an animated model
wears it through `materialPath` (`applyMaterialOverride` maps an unnamed part to
`default`).

## A model change leaves stale clips or the wrong material

**Cause:** "what resets when a model changes" was answered in more than one place.
**Fix:** `assignModel` is the ONE answer: it resets the clips, the Player
locomotion clips and the material that came with the old model (a deliberately
assigned shared `.mtl` stays).

## Rules of the importer

- **One importer, one neutral shape.** Host-only `load` -> `plan` -> `write`
  (+ `staticTwin`), shared by the Import model window and `--import-model`. Every
  format lands in one neutral `Source` (objects -> material pieces, 8-float
  corners, PNG images) and leaves the same shape: static = `.obj` + `.mtl` +
  PNGs; animated = the `.glb`/`.fbx` + `.mtl` + PNGs. No new `PrimitiveType` or
  field is added - the kind is the extension of `modelPath`.
- `objparser::load(..., splitObjects)` keeps `o`/`g` groups apart for the split.
- Preview = `Viewport::renderImportPreview` (own FBO; per-mesh offset = a split
  model's origin); the window's VU1 vertex readout runs `meshstrip::build` on
  flat-normal copies of the plan (the bake's own answer).
- `modelimport_ui.cpp` only holds `App` definitions; a new member is declared in
  `app.hpp`, whatever file defines it.

## Animation import and clip edits feed this path

- `glbparser`/`fbxparser` fill the SAME `glbparser::Baked`/`Skel` structures, so
  everything downstream is format-agnostic; the `animimport::` namespace in
  fbxparser's header is the extension dispatch every import site calls.
  `uniqueNodeNames` runs at the end of BOTH `parseSkel` implementations.
- Clip edits (`Project::animClipEdits`, Tools > Animation Editor) are
  non-destructive and folded in by `applyClipEdits` right before `writeTskl`; the
  viewport is the twin (`Viewport::setAnimEdits`) - **change both**. Clip names
  in the model are SOURCE names; every reference stores the EFFECTIVE name
  (`effectiveName`/`sourceName`; `App::renameAnimClipRefs` retargets on rename).
- `animmerge` operates on the PARSED skeleton, never a ufbx scene; bones match by
  name (exact, then namespace-stripped + lowercased). Two traps: **`poseGlobals`
  may NOT assume parent-before-child** (a forward pass measured 0.42 units of
  vertex error - resolve locals first, then compose); and **never parse two
  models per frame** (`compatibility()` lives in `App::animImportProbe`, cached
  in `animProbeCache_` - never via `glbInfo`, whose full morph bake stalls the
  editor).

# Project model, serialization, format versioning

Read when you add/change a field on `SceneObject`, `SceneData`, `ProjectSettings`, `Project`, touch `project.cpp` save/load, the `Section` enum, object ids, `version.hpp`/`migrations.cpp`, or a project-wide value that indexes scenes.

## Files

- `project.cpp/.hpp` (~9.8k / ~5.1k): `Project`, `SceneData`, `SceneObject`, `TerrainConfig`, `ProjectSettings`; save/load; `refreshGenerated()` (see codegen-and-generated-files.md).
- `version.hpp` (~6.1k lines, almost all of it the per-version change log in comments): `TYRAX_VERSION_MAJOR/MINOR/PATCH` (editor semver, title bar + informational `"editorVersion"`), `kFormatVersion` (now 94), `kMinFormatVersion` (1).
- `migrations.cpp/.hpp` (~180+80), `json.cpp/.hpp` (~280+50: small JSON parser; `json::write` exists only to store model-authored tool-call arguments as values), `history.hpp` (~60: undo snapshot stack).

## On-disk shape

- `<name>.tyra` manifest = project-wide data + per-scene ordered object-id list + editor state + named window layouts; one `objects/<id>.json` per object (merge-friendly). `save()` writes every live object then prunes orphaned files; `load()` -> `readSceneObjects()` reads each scene's id list and one body per id. Also `terrain-*.heights` (`saveHeights/loadHeights`), splat (`saveSplat`), `<name>.history` (undo stack, monolithic, gitignored; `saveHistory/loadHistory`).
- Save/load are composed of per-section writer/reader pairs: `Section` enum + `sectionJson()` / `applySectionJson()` - one manifest key group as a standalone JSON blob; apply is total-replace-with-defaults; it is the collaboration last-writer-wins unit and the AI Assistant's section read/write surface.
  - **A new manifest key must join a `write*Section`/`read*Section` pair** (or the scene table / editor tail), or it reaches the file but never the collaboration wire.
  - `Section` is looped by INDEX (`for s < kSectionCount`), so the count must equal the enum size or the LAST section silently stops being written (with 17 sections and a count of 16 every save dropped `"prefabs"`). Now `Section::Count` + `static_assert(kSectionCount == 26)`: a new section bumps the asserted number deliberately.
- `objectJson()` / `parseObject()` (public): one object <-> wire string. `manifestFiles()`: in-memory byte images of .tyra + objects/*.json + heights from the LIVE model (session sync). `Project::projectId` + `ensureProjectId()`: stable 16-hex identity (remote-cache key, chat store key).
- `create()`, `seedBuiltinLayouts()` (see editor-ui-and-conventions.md for layouts).

## Object identity

`SceneObject::id`: opaque stable id, first JSON key, part of `operator==`; never reaches codegen (references resolve by name). `project::ensureObjectIds()` stamps missing ids and reissues duplicates; runs in `create`, end of `load`, and `commitChange()`. **A code path that clones an object (like `pasteObject`) must clear the copy's `id`.** Default-constructed objects need nothing. An object with an empty id at save is written to `objects/.json` and lost (procbake uses `project::newObjectId()`). Round-trip format changes headlessly with `--resave` (tyra-testing).

## Adding a scene-object property (full chain)

1. `SceneObject` in project.hpp **including `operator==`** - `History::push()` short-circuits on equality; `shadowMode` shipped in 1.62.0 without it and every Default->Blob edit was dropped (read as "undo broken").
2. `objectJson` + the object reader in project.cpp (save AND load; default the read; match the emission style of a similar field - some bools always emitted, some omitted at default).
3. Properties UI in `props_ui.cpp` (+ `commitChange()`); join the collapsing section whose question it answers.
4. `sceneDataContent()` in templates.cpp so the game sees it; runtime in the game templates (`game_templates.inc`).
5. Viewport if visual. Live Link: decide whether it joins `liveLinkRecipeHash` (devkit-and-live-channels.md). Time machine: runtime-mutated fields join the capture walk.
6. If it stores an asset path: `App::retargetAssetPath` (+ `rebuildAssetUsage` if it is a real reference) - see below.
7. `kFormatVersion++` and editor semver bump (below).

## Format versioning

- **Any change to what `project::save()` writes** -> `version::kFormatVersion++`, so an older editor refuses the newer file instead of dropping the field on its next save. Keep reads tolerant (default). Register a step in `migrations.cpp` ONLY when existing files need active transformation (rename, unit/semantic change, moved data) - that triggers the backup-and-migrate prompt. Bump the editor semver in the same PR (feature -> MINOR, fix -> PATCH). docs/format-versioning.md.
- `load()` refuses newer formats and anything below `kMinFormatVersion` (v0 = pre-versioning; refused by name rather than opening empty). Older files open silently unless `migrations::stepsFor` returns steps: then the GUI (`App::openProjectAt`, the single funnel for every LOCAL open) prompts, backs up format-bearing files into `_backup/` and migrates in memory (save only on success); headless `--build`/`--resave`/`--refresh-gen`/`--apply-graph`/`--ai-graph` refuse (use `--migrate`).
- `migrations::backup` must copy everything the post-migration save writes (`save` + `saveHeights` + `saveSplat`) or the skipped file is unrecoverable. `App::openRemoteProject` (collaboration client) REFUSES rather than migrates (the project is the host's). `migrations::validate()` guards the registry (ascending, unique, in range), called by `run`.
- **Retiring a flow node type DELETES every graph that used it** (`readFlowGraph` drops unknown types; no rename table). Surviving a retirement needs a real migration step + format bump.
- **Additive-and-written-only-when-set** keeps `--resave` byte-identical for untouched projects; prefer it (the per-scene upscaler override, player speed tiers, a default BLSS shot plan all write nothing at default).

## A member initializer is NOT the new-project default

Every `read*Section` guards on `find("key")`, so the struct initializer is what a project saved before the key existed loads as - changing it silently changes those projects. Keep the legacy answer in the struct; **`project::create` assigns the new default** (precedents: AmbiencePreset `aoEnabled`, `buildProfile = "debug"`, `keyboardMouse = false`, the Empty preset's `orbitSpeed = 0`). `create` is also the only place that may scale metric-by-definition defaults by `ProjectSettings::unitsPerMeter` (the FPP preset is a 1.8 m player at any scale); an existing project's numbers are never touched.

**Starting preset (`Project::gameTemplate`) is create-only.** The three New Project presets (`fpp`, `thirdperson`, `orbit` = Empty) live in ONE table, `kNewPresets` (app.cpp), read by the dialog and the disabled Preferences row; `project::create` is the only writer. It picks which USER-OWNABLE sources are generated, so flipping it later would overwrite or orphan owned files. The two player presets generate the SAME sources (`Project::hasPlayerTemplate()` is the one place they are treated as one) and differ only in the seeded Player's `playerMode`. New preset = a table row + a `project::create` branch; a new game template (genuine source fork) also needs a `hasPlayerTemplate`-style predicate at the `templates::generate` fork.

## Project preferences

- **New project preference** (travels with the `.tyra`, part of the game): `ProjectSettings` -> save/load -> the Project Preferences window (`drawPreferencesWindow`, app.cpp; its shape is in editor-ui-and-conventions.md) -> usually a constant baked into `inc/terrain_config.hpp` or `scene_data.hpp`.
- **A preference a SCENE can override**: one `bool` on `SceneOverrides` (+ `operator==`), a branch in `project::resolvedSettings`, and a serializer that writes the flag and scene-local values ONLY when the override is on (`writeSceneVisuals` emits every other category regardless - following that adds a key to every project's `.tyra` and breaks `--resave` byte-identity). Worked example: the upscaler `"upscaler"`/`"blss"` pair. Downstream reads `resolvedSettings(p, sc)`, never the raw field.
- **A project-wide field that INDEXES THE SCENE LIST** (`Project::startScene`): (1) travels in `writeScenesTable`/`applyScenesLayout`, NOT a `Section` (index without its list is meaningless on the wire); (2) a clamp helper (`clampStartScene`) called from BOTH `project::load` and `applyScenesLayout`; (3) `scenes.erase` in app.cpp shifts it (`> deleted` decrements, `== deleted` falls back), like `activeScene` - scenes are never reordered, delete is the only motion; (4) codegen clamps again (generated C++ would walk off the array). Omit from JSON at default.
- **Editor Play is a one-run choice**: `App` passes `activeScene` to `Runner`, which writes `bin/launch.scene` just before launch; the generated boot reads and removes it once, else uses `START_SCENE`. Build-only, CLI and exported games use the saved default; both generated boot loops and the boot loading screen must use the same chosen scene.

## Asset paths

- **An asset path the GAME opens must be `lexically_normal()`.** The PS2 cannot walk `..`; a Wavefront reference resolves relative to the file that named it, so `.mtl` folder + `map_Kd` gives `materials/../textures/x.png`. PCSX2's `host:` resolves it through the OS, so the bug is **invisible in the emulator and black on hardware** (PROGRESS 199, `project::resolveTerrainMaterial`). Normalizing also keeps codegen and `bin/` agreeing (the bake copies to the normalized location).
- **Any field that stores an asset path** (a `res/...` file) joins **`App::retargetAssetPath`** (assetbrowser.cpp) or Asset Browser move/rename breaks it silently - including maps KEYED by an asset path (`textureQuality`, `modelLods`, `modelUnitMeters`, `musicBuild`, `modelAoMode`, `modelCollision`) and editor staged paths. A real *reference* also joins **`App::rebuildAssetUsage`**; a per-asset *setting* deliberately does NOT (or no imported asset would ever read unused). Name-keyed references (font entry, menu, sequence) are not asset paths.
- **Imported real-world measurements convert through `ProjectSettings::unitsPerMeter`** (docs/world-scale.md; host-side only, never generated). Don't bake the factor into the copied asset (per-asset size lives in `Project::modelUnitMeters` / `"modelUnits"`), and don't rescale placed content as a side effect of a setting change.
- **Material features live in the `.mtl`, not in the project** - see runtime-rendering-and-meshes.md ("Materials").

## Renames and by-name references

- `App::renameObjectRefs` (app.cpp) is the ONE remap for every by-name object reference (sequence tracks/shots, mirror/scroller/portal/camera-feed lists, area lookups); called by the Properties name field and the AI Assistant's `set_object`. `objRenameFrom_` only remembers the old name during editing. A new by-name reference joins it.
- Fonts: `App::renameFont` (texts, menus, `FontName` node params). Facts: `project::renameFactRefs`. Clips: `App::renameAnimClipRefs`. Prefabs: `rename` retargets `prefabSource`.

# tyra-editor-dev — project model, serialization, lifetime, settings

From `### 3`, `project.cpp/.hpp`, `assetbrowser.cpp`, and the source map. See
also `docs/format-versioning.md`.

## A subsystem outlives the project it was reading
**Cause/rule:** a project reaches the editor through
`openProjectAt`/`openRemoteProject`/the New Project modal, all ending in
`attachProject()`; it leaves through `closeProject()` (File > Close Project),
which returns the editor to its boot state (`hasProject_ = false`, an empty
`Project`, the welcome screen). Most subsystems need nothing from either: the
per-frame channels (`liveLinkTick`/`livedbgTick`/`livetimeTick`/`liveLogicTick`/
`remotePadTick`/`pollGameError`) and every tool window already open with
`if (!hasProject_)` and stand down — **that guard is what a new window or channel
owes the close path**, and it costs one line.
**Fix:** `closeProject` handles only state that would otherwise OUTLIVE the
project: worker threads still writing into it (the phone camera link, the GI
baker, a running build, the Drone Generator's offline render) and disk-derived
caches keyed by project-relative paths (`viewport_.invalidateAssets()`, the
layer-RAM / WAV / model-info / GLB-info caches). A new subsystem with a worker
thread or such a cache joins `closeProject`; one that only reads `project_` per
frame does not. The unsaved-edit guard is NOT there — `requestCloseProject()`
owns it, like `requestOpen/New/Exit`, via `PendingAction` + the discard modal.
Caches always invalidated together are cleared together (`modelInfoCache_` and
`glbInfoCache_` are siblings at every eviction site).

## A tick above its own `!hasProject_` return still runs after a close
**Cause:** `droneTickRender()` sits above that guard in
`drawDroneGeneratorWindow()` on purpose (a render must survive closing the
window), and its completion appends the track to `project_.music` and calls
`saveAll()` — after a close that writes into an empty `Project`, or into
whichever project is opened next.
**Fix:** such a worker must be cancelled+joined in `closeProject`, in the same
order the shutdown path in `App::run` uses: audio device first (its callback
holds the `LiveSynth`), then the render thread. Audio does not stop by itself —
`closeProject` calls `droneStop()` so an audition does not keep playing over the
welcome screen with no control left.

## A large open shows a frozen copy of the previous project
**Cause:** local opening is a staged main-thread operation.
**Fix:** `openProjectAt` records the folder and returns; `projectLoadTick` first
gives `drawProjectLoadingScreen` a frame to reach the GPU, then performs
`project::load`, migration and `attachProject()`. Do not move `project::load` onto
a worker casually: it replaces the process-wide custom flow-node, screen-effect
and menu-style registries as part of parsing. The bar is deliberately
indeterminate.

## A new manifest key reaches the file but never the collaboration wire
**Cause/rule:** `save()`/`load()` are recomposed from per-section writer/reader
pairs (`Section` enum + `sectionJson()`/`applySectionJson()` — one manifest key
group as a standalone JSON blob; apply is total-replace-with-defaults, the
collaboration LWW unit). **`Section` is looped by INDEX (`for s < kSectionCount`),
so the count must equal the enum size or the LAST section silently stops being
written** — with 17 sections and a count of 16 every save dropped `"prefabs"`
outright, no error, and it survived several branches.
**Fix:** a new manifest key must join a `write*Section`/`read*Section` pair (or
the scene table / editor tail). It is now `Section::Count` + a `static_assert`
(maintained by the compiler), but a new section still needs the number in the
assert bumped deliberately. `objectJson()`/`parseObject()` are the one-object
wire string; `manifestFiles()` builds in-memory byte images of `.tyra` +
`objects/*.json` + heights from the LIVE model. `Project::projectId` +
`ensureProjectId()` is the stable 16-hex remote-cache key. `<name>.history`
undo stack stays monolithic/inline and gitignored.

## A project-wide field that indexes the scene list goes stale
**Cause:** an index that arrives without the list it indexes is meaningless, and
`scenes.erase` shifts the list. `Project::startScene` is the worked example.
**Fix:** four sites beyond the usual chain. (1) Travel in
`writeScenesTable`/`applyScenesLayout`, NOT a `Section` — the collaboration wire
sends the scene layout as one message. (2) A clamp helper called from BOTH
`project::load` and `applyScenesLayout` (`clampStartScene`). (3) `scenes.erase` in
app.cpp must shift it (`> deleted` decrements, `== deleted` falls back), the way
`activeScene` is fixed up; scenes are never REORDERED (indices are baked into
every generated table), so delete is the only motion. (4) Codegen clamps again on
the way out. Omit it from the JSON at its default.
Editor Play is a separate one-run choice: `App` passes `activeScene` to `Runner`,
which writes `bin/launch.scene` just before launch; the generated boot reads and
removes it once, otherwise using `START_SCENE`. Keep build-only, CLI and exported
games on the saved default; both generated boot loops and the boot loading screen
must use the same chosen scene. When making something that was always 0
configurable, grep the generated template for the OTHER uses of that 0 — the FPP
player was positioned from scene 0's spawn in `init()`, and the game .cpp is
assembled from an ORBIT or FPP head around a shared middle, so a loop-level fix
usually has two homes.

## Object identity: `SceneObject::id`
**Cause/rule:** every object carries an opaque, stable `id` (first JSON key, part
of `operator==`) — the merge/persistence key for the multi-user file format. It
is *not* a user field and never reaches codegen (references resolve by name).
**Fix:** `project::ensureObjectIds()` stamps a fresh unique id on any object that
lacks one and reissues duplicates (runs in `create`, at the end of `load`, and in
`commitChange()`). When you add a code path that clones an object (like
`pasteObject`), clear the copy's `id` so it gets its own. Freshly
default-constructed objects need nothing. Migrate/round-trip with `--resave`.

## A format change silently drops a field on an older editor
**Cause:** a new field was added to what `project::save()` writes without a
format bump.
**Fix:** `version::kFormatVersion++` in version.hpp. Keep the read tolerant
(default it); register a step in migrations.cpp only when existing files need
active transformation (rename, unit/semantic change, moved data) — that triggers
the backup-and-migrate prompt. Bump the editor semver in the same PR (feature →
MINOR, fix → PATCH). `version::kMinFormatVersion` is the oldest accepted file.

## An asset path the game opens is black on hardware but fine in PCSX2
**Cause:** the PS2 cannot walk `..`; a Wavefront `map_Kd` resolved relative to
its `.mtl` folder yields `materials/../textures/x.png`. PCSX2's `host:` fs
resolves it through the OS.
**Fix:** the path must be `lexically_normal()` (`project::resolveTerrainMaterial`,
PROGRESS 199). The bake copies files to their normalized location, so normalizing
also keeps codegen and `bin/` agreeing.

## The Asset Browser's move/rename silently breaks a stored asset path
**Cause:** a field that stores a `res/...` path was not registered.
**Fix:** any new field storing an asset path (model, material, texture, WAV, TTF)
must join **`App::retargetAssetPath`** (assetbrowser.cpp) — including a map KEYED
by an asset path (`textureQuality`, `modelLods`, `modelUnitMeters`,
`musicBuild`) and the editor's staged paths. If it is a real *reference* it also
joins **`App::rebuildAssetUsage`**; a per-asset *setting* (quality, recorded
real-world size, clip edits) deliberately does NOT, or no imported asset would
read as unused. Both are single flat walks. `rebuildAssetUsage` is keyed off
`modelEditSerial_`. The sibling invariant: a Wavefront reference
(`mtllib`/`map_Kd`/`refl`) is a bare name resolved next to the file that named it
and the PS2 cannot walk `..`, so `moveAssets` moves a transitively closed
dependency group (`assetWavefrontDeps`), COPIES a dependency the files left behind
still need, and REFUSES rather than half-applying a move; `renameAsset` (same
folder = safe) rewrites the siblings (`rewriteWavefrontRef`) and carries the
`.mtl` a model exclusively owns. A new file type joins `assetKindOf`/
`assetKindName`, the filter-chip counts, `matchesFilter`, `kindColor` (append —
the cases are numeric), `activateAsset` and the inspector switch. Sidecars
(`.uvs`, `.aov`, the `.drone` patch, `<tex>.layers/`) travel with their asset; the
baked `.tmdl` is deleted for the next build to redo.

## A new machine-global editor setting is dropped on save
**Cause/rule:** a per-installation setting (NOT in the `.tyra` — UI scale,
viewport navigation, emulator path, dev-PS2 IP) lives on `EditorConfig`
(app.cpp) with load/save lines in `loadEditorConfig`/`saveEditorConfig`
(key=value in `editor.ini` under `%LOCALAPPDATA%`).
**Fix:** every save funnels through `App::saveGlobalConfig()` so no field is
dropped; add an App member seeded from it at startup, edited in the *Edit*
Preferences dialog (`drawEditorPreferencesModal`). If the Runner needs it, feed
it into `project_` in `attachProject` (as `emulatorPath`/`ps2LinkIp` do — those
live on `Project` only as the Runner's transport, not serialized game data). A
machine-wide setting need not live in that modal (`errorPopup` is edited from the
*Debug* window) but still goes through `EditorConfig`/`saveGlobalConfig()`.

## The game dies but the editor never reports it
**Cause/rule:** `App::pollGameError` (each frame from `drawUI`) tails the game's
log — a failed `TYRA_ASSERT` prints a dump bracketed by the stable
`======= TYRA =======` … `================` delimiters to `bin/log.txt` (PCSX2,
host: fs) or the `[ps2]` runner-log stream (network deploy) and halts quietly.
`extractLastTyraAssert` pulls the last block; a new one raises the copyable
`drawErrorModal` (and flashes/focuses the window via
`glfwRequestWindowAttention`/`glfwFocusWindow`).
**Fix:** the same block format covers a fatal assertion and a `TYRA_SOFT_ERROR`
(recovered asset load) — `drawErrorModal` switches wording on the `Non-fatal`
header marker. Dedupe is by block text (`errorSeenSig_`), but the signature is
**forgotten when a log source shrinks** (`errorGameLogSize_`/`errorRunnerLogSize_`):
the Runner deletes `bin/log.txt` before each launch, so a new run drops the size
and an *identical* re-run error pops again. Both size and signature are baselined
in `attachProject`. `EditorConfig::errorPopup` (default on) gates the dialog.

## Reference facts (upstream `project-model-and-serialization.md` extras)
- `load()` refuses newer formats and anything below `kMinFormatVersion`; `migrations::stepsFor` returns the steps (GUI `App::openProjectAt` prompts, backs up into `_backup/`, migrates in memory; headless verbs refuse and use `--migrate`). `migrations::backup` copies everything the post-migration save writes (`save` + `saveHeights` + `saveSplat`); `migrations::validate()` guards the registry.
- Terrain saves through `saveHeights`/`loadHeights` and `saveSplat`; `Project::projectId` + `ensureProjectId()` (16-hex, remote-cache/chat key); `manifestFiles()`; `objectJson`/`parseObject`; `project::newObjectId()`.
- Asset-path key maps that join `App::retargetAssetPath`: `textureQuality`, `modelLods`, `modelUnitMeters`, `musicBuild`, `modelAoMode`, `modelCollision`. A real reference also joins `App::rebuildAssetUsage`. `Project::modelCollision` (`Section::ModelCollision`) holds per-model smaller boxes.
- Prefabs: `SceneObject::prefabSource` (prefab NAME, editor bookkeeping; `rename` retargets it). Renames: `App::renameObjectRefs`, `App::renameFont`, `project::renameFactRefs`, `App::renameAnimClipRefs`.

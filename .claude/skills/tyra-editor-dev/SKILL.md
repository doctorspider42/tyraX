---
name: tyra-editor-dev
description: >
  Architecture map and change-making guide for the TyraX codebase — a C++20
  ImGui/GLFW/OpenGL Windows editor that authors 3D scenes and flow graphs, then
  generates complete Tyra PS2 game projects (built in Docker, run in PCSX2).
  Use this skill BEFORE making ANY change to editor code in src/ — new features,
  panels, scene object types, flow-graph nodes, preferences, project.json fields,
  code generation — and whenever you need to understand how the editor, the data
  model, codegen and the generated game fit together. Also consult it when asked
  "how does X work" about this repo, even for seemingly small one-file edits:
  most features cut across the model → serialization → codegen → UI → viewport
  chain, and this skill tells you which files each kind of change must touch.
---

# TyraX development

## What this project is

An editor for the [Tyra](https://github.com/h4570/tyra) PlayStation 2 game engine.
The editor itself is a Windows desktop app (C++20, Dear ImGui docking + GLFW +
OpenGL 3.3). It edits a **data model** (scenes, objects, terrain heightmaps, flow
graphs, preferences) stored in a `<name>.tyra` manifest plus one `objects/<id>.json`
file per scene object (merge-friendly split), and on every build
**generates a complete PS2 game project** (C++ sources, Makefile, Dockerfile).
The game is compiled inside a Docker container (`h4570/tyra` image, PS2DEV
`mips64r5900el-ps2-elf-g++` toolchain) and launched in the PCSX2 emulator.

The one-line pipeline to keep in your head:

```
ImGui UI (app.cpp) ──edits──> Project model (project.hpp)
        │ commitChange()              │ save()/load()
        ▼                             ▼
  undo history (history.hpp)     <name>.tyra (manifest) + objects/<id>.json + <name>.history + terrain-*.heights
                                      │ refreshGenerated() at build start
                                      ▼
                templates::generate() (templates.cpp) → game sources
                                      │ Runner (runner.cpp)
                                      ▼
              docker compose build → make → bin/<name>.elf → PCSX2
```

Two sibling skills cover the rest of the system:
- **tyra-engine-dev** — editing the in-tree PS2 engine fork in `vendor/tyra`.
- **tyra-testing** — building, running and verifying anything (editor, codegen,
  PCSX2 e2e). Read it before claiming a change works.

## Source map (`src/`, one flat directory)

| File | ~Lines (.cpp) | What it is |
|---|---|---|
| `main.cpp` | 76 | Entry point. GUI by default; headless `--new <name> <dir> [w] [d] [empty\|fpp]` and `--build <projectDir> [--run]`. |
| `app.cpp/.hpp` | 2751 | The whole ImGui shell: menus, all panels (Project, Scene, Scripts, HUD, Music, Sounds, Output, Disc Layout, Flow Graph), modals, gizmo + sculpt input, undo/redo, clipboard, wiring viewport ↔ project ↔ runner. |
| `project.cpp/.hpp` | ~1050 | **Data model + JSON (de)serialization + generated-file refresh.** `Project`, `SceneData`, `SceneObject`, `TerrainConfig`, `ProjectSettings`. `save()`/`load()`: the `<name>.tyra` **manifest** (project-wide data + per-scene ordered object-id list + editor state + the named window layouts) plus one `objects/<id>.json` body per object — `save()` writes every live object then prunes orphaned files; `load()`→`readSceneObjects()` dispatches split (id strings) vs legacy inline (object bodies). `ensureObjectIds()` (stamps stable ids), `create()`, `seedBuiltinLayouts()` (the Default/Director/Material `WindowLayout` set, also used to migrate a legacy single `"layout"` dump), `saveHeights()/loadHeights()`, `saveHistory()/loadHistory()` (`<name>.history` undo stack — stays monolithic/inline, gitignored), `refreshGenerated()`. **Window layouts** are `std::vector<WindowLayout> windowLayouts` + `activeLayout`; a `WindowLayout` is `{name, ini, recipe, openWindows}` where an empty `ini` + a `LayoutRecipe` id is (re)built by `App::buildLayoutRecipe` (DockBuilder) the first time it's shown. The Layout menu / switching / capture logic lives in app.cpp (`switchLayout`/`applyActiveLayout`/`captureActiveLayout`/`buildLayoutRecipe`, applied at a frame boundary). Editor state, not undo. |
| `templates.cpp/.hpp` | 3522 | **All code generation.** `templates::generate(Project)` returns `vector<File>` (relativePath + content). Scene tables, terrain game sources, flow-graph compilation, Dockerfile/Makefile/compose, VS Code IntelliSense config (`.vscode/c_cpp_properties.json` always-overwritten; `.vscode/extensions.json` written-if-missing — recommends the `tools/vscode-tyrax` extension). |
| `flowgraph.hpp` | 216 | Flow-graph data model: `FlowNode`, `FlowLink` (exec / object-id / position / bool link kinds), `FlowGraph`, the built-in `flowNodeTypes()` registry, and the project-scoped custom-node registry (`CustomFlowNode`, `customFlowNodes()`). Per-object graphs, stored inside each object's `objects/<id>.json` body. |
| `flownode.cpp` | 230 | Loads project-defined **custom flow nodes** from `<project>/flow-nodes/*.flownode` text files into the global `customFlowNodes()` registry (`flownode::loadForProject`). Called by `project::load` *before* graphs are parsed. Parses the manifest (title/category/params, `in`/`out` pins, `exec_out`, `call`) into a `FlowNodeType`; the node's behavior is an inline C++ snippet or a `call = fn` into `inc/scripts/flow_nodes.hpp`. Also scaffolds the starter file (`writeExample`). See `docs/custom-flow-nodes.md`. **When you add/change a header key or `{placeholder}` here, mirror it in the VS Code extension's `SPEC` table (`tools/vscode-tyrax/extension.js`) and the grammar** — that is what colours/validates `.flownode` files (see `docs/vscode-extension.md`). |
| `screenfx.cpp` / `.hpp` | ~230 | Loads project-defined **custom screen effects** from `<project>/screen-effects/*.screenfx` text files into the global `customScreenEffects()` registry (`screenfx::loadForProject`, called by `project::load` before placements are read). The full-screen-post-effect analogue of custom flow nodes: a manifest (title + up to four numeric params) plus a raw low-level GS-blit C++ body. A `Project` references one only by placement (`ScreenFxPlacement` in project.hpp: key + stack `layer` + `enabled` + params). Placed/reordered in the *UI Editor* screen stack (like bloom/grain), codegen'd to `src/scripts/screen_fx.gen.cpp` (`screenFxSource`/`screenFxHeader` in templates.cpp), run via `RendererCore::applyCustomPostFx` at the effect's slot in the frame loop. Unknown-key placements are dropped on load. See `docs/custom-screen-effects.md`. (Header keys/`{pN}` placeholders are also mirrored in the VS Code extension's `SPEC` — `tools/vscode-tyrax/extension.js`; keep them in sync.) |
| `sequence.hpp` | ~230 | Cutscene Director data model + shared math: `Sequence` (object tracks + camera *shots* - free or bound to Camera entities - plus widescreen bars, fades, skippable), `seqEase`/`seqSample`/`seqBarsFractions`/`seqShakeOffset`/`seqCameraForward` (each mirrored in the generated PS2 player - keep in sync). Project-wide (like presets), persisted but not in undo. Compiled to `src/scripts/sequences.gen.cpp` (a runtime player Script + the bars/fade `renderOverlay`); the dopesheet UI + viewport scrub live in `app.cpp`. Object renames remap track/shot name references (`objRenameFrom_`). |
| `camtake.cpp/.hpp` | ~420 | Phone-recorded 6DoF camera takes (ARKit) → Cutscene Director camera keys. Two strictly separated stages: *acquisition* (loaders producing a `CamTake`: CamTrackAR `.hfcs` via a minimal XML subset reader, canonical CSV — spec + conventions in `docs/camera-takes.md`; phase 2 adds a live streaming receiver) and *bake* (`bakeCamTake`: scale/yaw/origin/time mapping + time-parameterized RDP decimation → free `SeqCameraKey`s, pure and harness-testable). UI = the "Import take..." modal in `app.cpp` (`seqTake*_` members). |
| `viewport.cpp/.hpp` | 1184 | Offscreen GL 3.3 preview: unit-primitive meshes, terrain grid + heightmap, sky dome, selection outline, live point-light shader, sculpt-brush raycast, orbit/pan camera. Also the Material Editor preview (a primitive or a project .obj with the selected entry's staged values), its paint raycast (`materialPreviewPick`) and the live painted-texture upload (`updateTexturePixels`, shared texCache_ id so the scene updates too). |
| `runner.cpp/.hpp` | 301 | Docker + PCSX2 pipeline on a worker thread (states Idle/Running/Success/Failed). `buildAndRun()`, `runEmulatorOnly()`, `exportIso()`. |
| `pcsx2_config.cpp` | 86 | Finds PCSX2.ini (portable dir first, then the Documents known folder — beware OneDrive redirection) and force-enables `HostFs = true` before launch. |
| `iso9660.cpp`, `isoexport.cpp` | 379+264 | In-tree ISO9660 writer + disc layout planning (`Project > Export PS2 ISO`, Disc Layout window). |
| `json.cpp/.hpp` | 158 | Tiny standalone JSON parser used for reading the `.tyra` project file. |
| `objparser.cpp` | 109 | Wavefront .obj importer for custom models. |
| `primmesh.cpp/.hpp` | ~180 | Shared, GL-agnostic **unit-primitive tessellation** (box/sphere/cylinder/cone/plane → raw `pos+normal+uv`). The single host source: the viewport bakes shade on top of it, and `decalproj` uses it as receiver geometry, so a projected decal conforms to exactly the geometry the viewport draws. (templates.cpp keeps its own generated-string builders for the PS2 runtime — the pre-existing twin.) |
| `decalproj.cpp/.hpp` | ~230 | **Projected-decal geometry** (host-only, no GL). `project(Project, SceneData, decal)` clips the receiver triangles (terrain + overlapping objects, auto) against the decal's oriented unit-cube projector, computes projected UVs and a surface-normal offset, and returns a world-space triangle list. Used by the viewport (live preview) AND codegen (`decalDataHeader` bakes it into `inc/decal_data.gen.hpp`); the game just draws it — **no projection/clipping on the PS2 EE**. See PROGRESS (99). |
| `navmesh.cpp/.hpp` | ~160 | **NavMesh bake** (host-only, no GL — the decalproj pattern). `bake(Project, SceneData)` rasterizes a scene into a walkable-cell grid (terrain slope on the game's own bilinear heightmap + `collidePlayer`-box-mode blockers inflated by the agent radius; capped 128×128). Used by codegen (`navDataHeader` → `inc/nav_data.gen.hpp`, gated on AI nodes existing) AND the viewport nav overlay (`App::updateNavOverlay`, signature-cached). The generated `src/scripts/navigation.gen.cpp` runs A* over the bitmap on the EE and ticks all AI agents (Patrol/Chase/Flee flow nodes set agent state; one state per object). See `docs/navigation-ai.md` + PROGRESS (108). |
| `history.hpp` | 59 | Undo/redo snapshot stack. |
| `gl_loader.h/.cpp` | 137 | Minimal hand-rolled GL 3.3 loader (only what the viewport needs). |

`examples/script-demo/` is a complete generated project checked into the repo.
Its generated files are only as fresh as the last time someone rebuilt it — if
codegen changed since, they drift silently. Regenerate (load + save +
`refreshGenerated`, or a `--build`) before trusting it as a reference for what
`templates.cpp` emits today.

## The rules that keep the system consistent

### 1. Editing model: mutate, then `commitChange()`
UI code in `app.cpp` mutates `project_` freely; one logical user action ends with
a single `commitChange()`, which pushes an undo snapshot and saves. If you add
an editable property and skip this, undo/redo and autosave silently break.

### 2. Generated-file ownership markers
`project::refreshGenerated()` (project.cpp:914) runs at the start of every build
and decides per file:
- **Always overwritten** (first line `// Generated by TyraX. Do not edit -
  regenerated on every build.`): `Dockerfile`, `docker-compose.yml`,
  `inc/scene_data.hpp`, `inc/terrain_config.hpp`, all `*.gen.hpp` / `*.gen.cpp`.
- **User-ownable** (first line `// Generated by TyraX. Delete this line to
  take ownership of this file.`): `src/terrain_game.cpp`, `inc/terrain_game.hpp`,
  `inc/controls.hpp`, `inc/scripts/script.hpp`. Regenerated only while the marker
  line is intact; the user deletes the line to take over.

Consequences: anything the game must know about the scene goes through codegen
in `templates.cpp`, never by hand-editing a generated file; new generated files
use the `.gen.hpp`/`.gen.cpp` suffix; never make a template emit something that
breaks projects where the user took ownership of `terrain_game.cpp` (data goes
into the always-regenerated headers, behavior into ownable sources).

### 3. A feature usually touches the whole chain
Before coding, list which of these your change needs — most features need most:

**New scene-object property** → `SceneObject` in project.hpp **including its
`operator==`** (History::push() short-circuits on equality — miss this and undo
silently drops your field) → `objectJson` + `readObjectsArray` in project.cpp
(save AND load; default the read for backward compatibility, and match the
emission style of a similar field — some bools are always emitted, some omitted
at their default) → properties UI in app.cpp (+ `commitChange()`) →
`sceneDataContent()` in templates.cpp so the game sees it → game runtime in the
`terrain_game.cpp` template (`TPL_*` strings in templates.cpp) → viewport
rendering if it's visual.

**New object type** → `PrimitiveType` enum (0–15 used so far; keep values stable,
they're serialized) → mesh/marker in viewport.cpp → insert menu in app.cpp →
codegen + runtime as above. If the type needs per-object variable-length data
(like Mirror's reflected-object list), don't grow the fixed `SceneObjectData`
POD — emit a flat side table into scene_data.hpp keyed by (scene, object), the
`OBJECT_SCRIPT_ATTACHES` / `MIRRORS` pattern.

**Object identity: `SceneObject::id`.** Every object carries an opaque, stable
`id` (first JSON key; part of `operator==`) — the merge/persistence key for the
multi-user file format. It is *not* a user field and never reaches codegen
(references still resolve by name). `project::ensureObjectIds()` stamps a fresh
unique id on any object that lacks one and reissues duplicates; it runs in
`create`, at the end of `load`, and in `commitChange()`. **When you add a code
path that clones an existing object** (like `pasteObject`), clear the copy's
`id` so it gets its own identity — otherwise two objects share one id. Freshly
default-constructed objects need nothing (empty id → `ensureObjectIds` fills
it). Migrate/round-trip `.tyra`-format changes headlessly with `--resave` (see
tyra-testing).

**New flow-graph node** → node kind in flowgraph.hpp → node UI (pins, params)
in the flow-graph editor in app.cpp → codegen in `flowGraphScript()`
(templates.cpp), which compiles graphs to `src/scripts/flow_graph.gen.cpp` — one
script class per object graph; object references resolve to indices at codegen;
bool logic folds into inline C++ expressions. (If the node is project-specific
rather than a general editor feature, prefer a **custom node**: a
`flow-nodes/*.flownode` file, no C++ change — see `flownode.cpp` and
`docs/custom-flow-nodes.md`. Custom nodes plug into the same `flowNodeType()`
lookup, the add-menu via `flowAllNodeTypes()`, and a `flowCustomNode()` branch
in `actionCode()`. A `call = fn` custom node runs a user function in
`inc/scripts/flow_nodes.hpp` via the `FlowNodeIO` struct and can have any pins;
its **object output is a runtime value**, which is why `resolveTarget()` returns
a C++ int-*expression* (a literal index for built-in sources, `objOut<id>` for a
custom node's runtime output) — built-in object actions fed such a ref are
bounds-guarded via `isRuntimeIdx`. The built-in **Raycast** node uses the same
runtime-latch machinery: every `flowCustomNode(...)` check on that path also
accepts `type == "Raycast"` — a new built-in node with runtime outputs should
extend those same spots. The **AI nodes** (Patrol Waypoints / Chase Player /
Flee / Stop AI / On Player Seen) compile to calls into the generated
`navigation.gen.cpp` runtime — a new AI-family node usually only needs a new
`nav*` entry point there plus an `actionCode` branch; the shared per-object
agent state, movement and A* already exist. Anything that changes what blocks
walkability must update `navmesh::bake` (host) — there is no game-side twin,
the game only reads the baked bitmap.)

**New project preference** (travels with the `.tyra`, part of the game) →
`ProjectSettings` → save/load in project.cpp → the *Project* Preferences dialog
(`drawPreferencesModal`) in app.cpp → usually a constant baked into
`inc/terrain_config.hpp` or `scene_data.hpp` by templates.cpp.

**Menus & option blocks** (`GameMenu`/`MenuEntry` in project.hpp; project-wide,
not per scene). A menu is baked to a panel sprite at build (menubake.cpp — the
PS2 has no font), edited in the *Menu Editor* (`drawMenusWindow` in app.cpp),
and driven at runtime by `updateGameMenu`/`renderGameMenu` (generated in
`TPL_GAME_CPP_SCENE`, data in `menu_data.gen.hpp` via the `MenuEntryData`/
`MenuData` codegen). Stateful **Toggle/Choice** rows store an option index in a
named save value (cycled by the pad, label drawn from a baked value strip).
**Option blocks** build on that: `MenuEntry::settingBind` (a `Setting` enum,
0=none) marks a stateful row as driving a built-in engine setting; codegen emits
it as the `bind` column and `TerrainGame::applyMenuBindings()` (called each
frame in both loops before `applyVideoRequests`) maps the row's option index
onto the setting - music/sfx volume, deadzone (`g_deadzoneL/R`), stick curve
(`g_stickCurve`), display mode / widescreen (via `scriptCtx` video requests).
The *Menu Editor* "+ Option block" popup and "+ Options menu" scaffolder
(`addOptionBlock`/`addOptionsMenuPages`, app.cpp) create pre-configured rows +
their backing save values. So a menu change can touch: `MenuEntry` (+ `==`) →
menu JSON in project.cpp → `MenuEntryData` codegen + `applyMenuBindings` in
templates.cpp → the runtime setting site (audio call, `axis`/`axisValue`,
`applyVideoRequests`) → the Menu Editor UI.

**New machine-global editor setting** (per-installation, NOT in the `.tyra` —
e.g. UI scale, viewport navigation, emulator path, dev-PS2 IP) → a field on
`EditorConfig` (app.cpp) with load/save lines in `loadEditorConfig`/
`saveEditorConfig` (key=value in `editor.ini` under `%LOCALAPPDATA%`) → an App
member seeded from it at startup → edited in the *Edit* Preferences dialog
(`drawEditorPreferencesModal`). Every save funnels through `App::saveGlobalConfig()`
so no field is dropped. If the Runner needs it, feed it into `project_` in
`attachProject` (as `emulatorPath`/`ps2LinkIp` do — those live on `Project` only
as the Runner's runtime transport, not as serialized game data). A machine-wide
setting doesn't have to live in that modal — the `errorPopup` toggle (below) is
edited from the *Debug* window and the error dialog instead, but it still goes
through `EditorConfig` / `saveGlobalConfig()` the same way.

**Game error catcher** (`App::pollGameError`, called each frame from `drawUI`):
the running game's fatal errors reach the editor through its log, not a return
code — a failed `TYRA_ASSERT` in the engine prints a dump (bracketed by the
stable `======= TYRA =======` … `================` delimiters) to
`bin/log.txt` (PCSX2, host: fs) or the `[ps2]` runner-log stream (network
deploy) and halts quietly (see tyra-engine-dev — the engine no longer takes over
the screen). `pollGameError` tails both (throttled), `extractLastTyraAssert`
pulls the last block, and a new one raises the copyable `drawErrorModal` (and
flashes/focuses the window via `glfwRequestWindowAttention`/`glfwFocusWindow` —
PCSX2 has the foreground when the game dies). The same block format covers both
a fatal assertion (game stopped) and a `TYRA_SOFT_ERROR` (recovered asset load,
game running — see tyra-engine-dev); `drawErrorModal` switches its wording on the
`Non-fatal` header marker. Dedupe is by block text
(`errorSeenSig_`), but the signature is **forgotten when a log source shrinks**
(tracked via `errorGameLogSize_`/`errorRunnerLogSize_`): the Runner deletes
`bin/log.txt` before each launch, so a new run drops the size and an *identical*
re-run error pops again instead of being deduped away. Both size and signature
are baselined in `attachProject` so a stale dump present at open neither pops nor
reads as a shrink. (Don't revert to text-only dedup re-baselined per build/run —
it silently misses the second identical run's error.) `EditorConfig::errorPopup`
(default on) gates the dialog; off = errors go only to the Debug window / console.

**Live Link** (`App::liveLinkTick`, called each frame from `drawUI`; docs in
`docs/live-link.md`): with the **debug** build profile and the
`ProjectSettings::liveLink` preference on (default; toggled by the toolbar
LIVE chip, *Build > Live Link* and *Preferences > Build*), the editor streams
scene edits into the running game by rewriting `bin/livelink.bin` (atomic
tmp→rename; `TXLL` v2 header + one 64-byte record per object + seq-echo
footer), which the generated `src/scripts/live_link.gen.cpp`
(`templates::liveLinkScript`; empty TU in release or with the preference off)
polls over host: — the same file channel both PCSX2 and ps2link already serve
assets through. Records address objects by `project::liveLinkIdHash` (baked
as `SCENE_*_OBJECT_ID_HASHES` in scene_data.hpp), so renames/reorders are
safe, newly added objects are **live-spawned** from an equal-recipe template
via the runtime spawn pool, and deleted ones are hidden. Consistency is
guarded by the as-built record `bin/livelink.sig`
(`project::liveLinkSigFile`: per-object id + `liveLinkRecipeHash` + a context
hash, stamped by the Runner at build start, which also deletes stale
`livelink.bin`); recipe drift / unspawnable new objects
(`liveLinkCanSpawnLive`: baked lights, projecting decals, mirrors, objects
with graphs/scripts) flip the chip from green LIVE to amber LIVE (rebuild)
and stop writes. **If you add an object property**, decide where it belongs:
build-time-baked or clone-relevant fields go into `liveLinkRecipeHash` (and
new unspawnable categories into `liveLinkCanSpawnLive`), or Live Link will
silently not show that edit while claiming LIVE. The snapshot seq is seeded
from the clock at attach — a restarted editor must never reuse a seq the
still-running game already applied.

### 4. Conventions
- Files: `snake_case.cpp/.hpp`, paired header/impl, flat `src/`.
- One feature = one commit. `PROGRESS.md` is a living log — every finished
  feature gets a numbered entry there describing what was done and **how it was
  verified** (read a few entries to match the tone; they double as the project's
  institutional memory, including dead ends).
- Comments explain constraints, not narration; match the existing density.
- The editor viewport and the PS2 game must agree: shading, terrain sampling
  and sky are implemented twice (GLSL/C++ in viewport, codegen in templates).
  When you change one formula, grep for its twin.
- **DPI/zoom: wrap literal pixel sizes in `App::scaled(px)`.** `applyUiScale()`
  scales fonts (`FontScaleMain`) and style spacing (`ScaleAllSizes`) but NOT the
  pixel literals you pass to ImGui. So a hardcoded `SetNextItemWidth(180)`,
  `BeginChild(ImVec2(170,0))`, absolute `SameLine(190)`/`Indent(46)`, fixed
  button size, or hand-drawn preview stays literal and clips/misaligns at high
  scale (a 4K laptop runs ~250%). Route such sizes through `scaled()` (=
  `px * uiScaleApplied_`); negative/`-FLT_MIN`/fill widths and text-measured
  (`CalcTextSize`) sizes already track scale, leave those alone. Free functions
  that draw fixed-size widgets take a `scale` param (see `gradingWheel`).

## Building the editor

```powershell
./build.ps1          # configure (if needed) + build → build/tyrax-editor.exe
./build.ps1 -Run     # build and launch
./build.ps1 -Clean   # nuke build/ first
```

First run auto-clones `vendor/` deps via `setup.ps1` (imgui docking, glfw 3.4,
imguizmo, imnodes, stb — all git-ignored; `vendor/tyra` is versioned, see
tyra-engine-dev). Toolchain: `scoop install mingw cmake ninja`; build.ps1 finds
scoop's mingw even off-PATH. Single CMake target `tyrax-editor`, statically
linked (MinGW `-static`), console subsystem on purpose (logs stay visible).

For how to test what you built — headless CLI, codegen checks without Docker,
full PCSX2 e2e, screenshots — read **tyra-testing**.

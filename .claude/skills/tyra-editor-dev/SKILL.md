---
name: tyra-editor-dev
description: >
  Architecture map and change-making guide for the tyra-editor codebase — a C++20
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

# tyra-editor development

## What this project is

An editor for the [Tyra](https://github.com/h4570/tyra) PlayStation 2 game engine.
The editor itself is a Windows desktop app (C++20, Dear ImGui docking + GLFW +
OpenGL 3.3). It edits a **data model** (scenes, objects, terrain heightmaps, flow
graphs, preferences) stored in `project.json`, and on every build **generates a
complete PS2 game project** (C++ sources, Makefile, Dockerfile) from that data.
The game is compiled inside a Docker container (`h4570/tyra` image, PS2DEV
`mips64r5900el-ps2-elf-g++` toolchain) and launched in the PCSX2 emulator.

The one-line pipeline to keep in your head:

```
ImGui UI (app.cpp) ──edits──> Project model (project.hpp)
        │ commitChange()              │ save()/load()
        ▼                             ▼
  undo history (history.hpp)     project.json  +  terrain-*.heights
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
| `main.cpp` | 76 | Entry point. GUI by default; headless `--new <name> <dir> [w] [d] [orbit\|fpp\|showcase]` and `--build <projectDir> [--run]`. |
| `app.cpp/.hpp` | 2751 | The whole ImGui shell: menus, all panels (Project, Scene, Scripts, HUD, Music, Sounds, Output, Disc Layout, Flow Graph), modals, gizmo + sculpt input, undo/redo, clipboard, wiring viewport ↔ project ↔ runner. |
| `project.cpp/.hpp` | 979 | **Data model + JSON (de)serialization + generated-file refresh.** `Project`, `SceneData`, `SceneObject`, `TerrainConfig`, `ProjectSettings`. `save()`, `load()`, `create()`, `saveHeights()/loadHeights()`, `saveSolution()/loadSolution()` (editor state: selection, undo history), `refreshGenerated()`. |
| `templates.cpp/.hpp` | 3522 | **All code generation.** `templates::generate(Project)` returns `vector<File>` (relativePath + content). Scene tables, terrain game sources, flow-graph compilation, Dockerfile/Makefile/compose, VS Code IntelliSense config. |
| `flowgraph.hpp` | 216 | Flow-graph data model: `FlowNode`, `FlowLink` (exec / object-id / position / bool link kinds), `FlowGraph`. Per-object graphs, stored inside objects in project.json. |
| `viewport.cpp/.hpp` | 1184 | Offscreen GL 3.3 preview: unit-primitive meshes, terrain grid + heightmap, sky dome, selection outline, live point-light shader, sculpt-brush raycast, orbit/pan camera. |
| `runner.cpp/.hpp` | 301 | Docker + PCSX2 pipeline on a worker thread (states Idle/Running/Success/Failed). `buildAndRun()`, `runEmulatorOnly()`, `exportIso()`. |
| `pcsx2_config.cpp` | 86 | Finds PCSX2.ini (portable dir first, then the Documents known folder — beware OneDrive redirection) and force-enables `HostFs = true` before launch. |
| `iso9660.cpp`, `isoexport.cpp` | 379+264 | In-tree ISO9660 writer + disc layout planning (`Project > Export PS2 ISO`, Disc Layout window). |
| `json.cpp/.hpp` | 158 | Tiny standalone JSON parser used for reading project.json. |
| `objparser.cpp` | 109 | Wavefront .obj importer for custom models. |
| `history.hpp` | 59 | Undo/redo snapshot stack. |
| `gl_loader.h/.cpp` | 137 | Minimal hand-rolled GL 3.3 loader (only what the viewport needs). |

`samples/script-demo/` is a complete generated project checked into the repo.
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
- **Always overwritten** (first line `// Generated by tyra-editor. Do not edit -
  regenerated on every build.`): `Dockerfile`, `docker-compose.yml`,
  `inc/scene_data.hpp`, `inc/terrain_config.hpp`, all `*.gen.hpp` / `*.gen.cpp`.
- **User-ownable** (first line `// Generated by tyra-editor. Delete this line to
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

**New object type** → `PrimitiveType` enum (0–9 used so far; keep values stable,
they're serialized) → mesh/marker in viewport.cpp → insert menu in app.cpp →
codegen + runtime as above.

**New flow-graph node** → node kind in flowgraph.hpp → node UI (pins, params)
in the flow-graph editor in app.cpp → codegen in `flowGraphScript()`
(templates.cpp), which compiles graphs to `src/scripts/flow_graph.gen.cpp` — one
script class per object graph; object references resolve to indices at codegen;
bool logic folds into inline C++ expressions.

**New preference** → `ProjectSettings` → save/load in project.cpp → Preferences
dialog in app.cpp → usually a constant baked into `inc/terrain_config.hpp` or
`scene_data.hpp` by templates.cpp.

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

## Building the editor

```powershell
./build.ps1          # configure (if needed) + build → build/tyra-editor.exe
./build.ps1 -Run     # build and launch
./build.ps1 -Clean   # nuke build/ first
```

First run auto-clones `vendor/` deps via `setup.ps1` (imgui docking, glfw 3.4,
imguizmo, imnodes, stb — all git-ignored; `vendor/tyra` is versioned, see
tyra-engine-dev). Toolchain: `scoop install mingw cmake ninja`; build.ps1 finds
scoop's mingw even off-PATH. Single CMake target `tyra-editor`, statically
linked (MinGW `-static`), console subsystem on purpose (logs stay visible).

For how to test what you built — headless CLI, codegen checks without Docker,
full PCSX2 e2e, screenshots — read **tyra-testing**.

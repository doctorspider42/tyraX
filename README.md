# Tyra Editor

An editor for the [Tyra](https://github.com/h4570/tyra) PlayStation 2 game engine. C++20 + Dear ImGui (docking) + GLFW + OpenGL 3.3.

## Quickstart

```powershell
# 1. Install prerequisites (skip what you already have)
scoop install mingw cmake ninja       # editor toolchain
# + Docker Desktop (running) and PCSX2 in Program Files\PCSX2 (with BIOS configured)

# 2. Build and run the editor (clones deps + configures + builds)
./build.ps1 -Run
```

`build.ps1` handles everything: clones `vendor/` dependencies on first run, finds the scoop mingw toolchain even when it's not on PATH, configures CMake and builds. Flags: `-Run` launches the editor after building, `-Clean` rebuilds from scratch.

Then in the editor:

1. `File > New Project` (`Ctrl+N`) — pick a name, location and the flat terrain size, hit **Create**.
2. The *Viewport* shows the terrain preview (drag to orbit, scroll to zoom).
3. Press **Build & Run** (`F5`) — the first build downloads the `h4570/tyra` Docker image and compiles the engine inside the container (a few minutes, one time). Subsequent builds take seconds. PCSX2 boots your ELF automatically.

## Features (MVP)

- **New project** (`File > New Project`, `Ctrl+N`) — name, location, flat terrain size (width × depth in world units) and a game template: **Terrain orbit** (camera circles the terrain) or **FPP walkthrough** (walk with the left analog stick, look around with the right one; the scene starts with a spawn point). Generates a complete Tyra game project: `Makefile`, `Dockerfile`, `docker-compose.yml`, C++ sources rendering the scene (StaticPipeline, no asset files needed), `project.json` and the `<name>.tyra` solution file.
- **Open project** (`Ctrl+O`) — pick the project's `<name>.tyra` solution file.
- **Spawn point** — a special scene object (marker with a direction arrow, no geometry in the game). In the FPP template the player starts at the first spawn point, facing its Y rotation.
- **Scene objects** — insert simple 3D primitives (box, sphere, cylinder, cone) via the *Scene* menu or the buttons in the *Project* panel. Each object has a name, position, rotation, scale and color, editable in the *Project* panel and saved to `project.json`. Objects render both in the editor viewport and on the PS2 (scene data is regenerated into `inc/scene_data.hpp` on every build).
- **Transform gizmos** (ImGuizmo) — click an object in the viewport to select it, then drag the gizmo to move / rotate / scale it. Switch tools with the buttons in the viewport corner or the `W` / `E` / `R` keys; `Delete` removes the selected object. Left-drag on empty space or right-drag orbits the camera, wheel zooms.
- **Solution file & undo history** — editor state lives in `<name>.tyra` next to `project.json`: selection, active tool and the undo history (up to 100 snapshots), so undo/redo survives editor restarts. `project.json` stays the source of truth — if it is edited outside the editor, the stored history is discarded automatically.
- **Copy/paste** — duplicate scene objects with `Ctrl+C` / `Ctrl+V` (the copy lands next to the original with a unique name).

## Shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | New / open / save project |
| `F5` | Build & run in PCSX2 |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+C` / `Ctrl+V` | Copy / paste the selected object |
| `W` / `E` / `R` | Move / rotate / scale tool (viewport) |
| `Delete` | Delete the selected object (viewport) |
- **3D terrain preview** — *Viewport* window with a checkerboard terrain grid (matching what the PS2 renders), scene objects with a selection outline, world axes and an orbit camera (mouse drag = rotate, wheel = zoom).
- **View modes** — *Solid*, *Wire* (colored wireframe) and *Wire+Solid* (solid shading with a wireframe overlay); buttons in the viewport corner, the choice is persisted in the solution file.
- **Scripts** — user C++ scripts live in `src/scripts/` and are compiled into the game by the normal build. A script derives from `Script` (see the generated `inc/scripts/script.hpp`), overrides `init`/`update` and registers itself with `TYRA_SCRIPT(MyScript);` — the editor never parses or regenerates your script files. The *Scripts* section in the *Project* panel lists them, creates new ones from a stub (**New script...**) and opens the project in VS Code (**Open in VS Code**) with working IntelliSense — the generated `.vscode/c_cpp_properties.json` points at the bundled Tyra engine headers and the PS2SDK headers exported from the docker toolchain on first build. Each `ScriptContext` gives access to the engine (pad!), player/camera position, scene objects and the sky color. New FPP projects include an example: walk up to the box and press X — the sky changes color and a message lands in the PCSX2 log.
- **Project preferences** (`Project > Preferences`, `Ctrl+,`) — game template, terrain size and detail (max grid cells), **triangle handling** (*Precise clipping* — no holes at screen edges but costs EE time, vs *Fast culling* — fastest, large near triangles may vanish), sky color, FPP camera (eye height, walk/look speed) and orbit speed. Stored in `project.json` and baked into the generated `terrain_config.hpp` on every build; the viewport reflects sky color and terrain detail immediately.
- **Build & Run in PCSX2** (`F5`) — builds the ELF inside a Docker container (the `h4570/tyra` image with the PS2DEV toolchain) and launches it in PCSX2. Build logs stream into the *Output* window.

## Requirements

- Windows, [Docker Desktop](https://www.docker.com/products/docker-desktop/) (running), [PCSX2](https://pcsx2.net/) installed in `Program Files\PCSX2` (with BIOS configured).
- To build the editor: CMake, Ninja, GCC/MinGW (e.g. `scoop install mingw cmake ninja`).

## CLI

Projects can also be created and built headlessly:

```powershell
tyra-editor.exe --new <name> <parentDir> [width] [depth] [orbit|fpp]
tyra-editor.exe --build <projectDir> [--run]
tyra-editor.exe <projectDir|solution.tyra>   # open GUI with a project loaded
```

## How Build & Run works

1. `docker compose up -d --build` in the project directory (the first run downloads the `h4570/tyra` image). Each project gets its own container (`<name>-compiler-1`).
2. If the shared engine volume (`tyra-engine-shared`, mounted at `/tyra`) is empty, the editor clones and builds the Tyra engine into it once (a few minutes) — all projects reuse it afterwards.
3. `rsync` sources into the container volume (`/host` → `/src`).
4. `make` inside the container (`mips64r5900el-ps2-elf-g++` compiler).
5. `rsync` the `bin/` directory back to the host.
6. Launch `pcsx2-qt.exe -elf <project>\bin\<name>.elf`.

The editor also fixes two Tyra engine clipping bugs (holes in the ground for triangles crossing the screen edge): generated games enable the real per-triangle clipper (`fullClipChecks` + `Precise` culling) and correct `PlanesClipAlgorithm::clipMargin` (the default pushes the near clip plane ~10 units away from the camera), and the Runner patches the engine's hardcoded bbox guard band (`render_bbox.cpp`) in the shared volume — one-time, marked with `/tyra/.tyra-editor-patch-1`.

Before each build the editor refreshes its generated files: `Dockerfile`, `docker-compose.yml`, `inc/terrain_config.hpp` and `inc/scene_data.hpp` are always rewritten from project data; `src/terrain_game.cpp` and `inc/terrain_game.hpp` are rewritten only while their first line still contains the `Generated by tyra-editor` marker — delete that line to take ownership of a file (scene objects will then stop syncing into the game code automatically).


## Sample project

[samples/script-demo](samples/script-demo) is a complete FPP project with the example script: walk up to the box and press X — the sky changes color. Open it via `File > Open Project` and pick its `project.json` (the `.tyra` solution file and `.vscode/` IntelliSense config are local state — the editor recreates them on open/build). Then just Build & Run.

## Structure

- `src/` — editor code (`app` UI, `viewport` GL preview, `project`+`templates` project generator, `runner` docker/PCSX2 pipeline, `gl_loader` minimal GL loader).
- `samples/` — example projects.
- `vendor/` — dependencies (not versioned; see `setup.ps1`).

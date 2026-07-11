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

- **New project** (`File > New Project`, `Ctrl+N`) — name, location, flat terrain size (width × depth in world units) and a game template: **Terrain orbit** (camera circles the terrain), **FPP walkthrough** (walk with the left analog stick, look around with the right one; the scene starts with a spawn point) or **FPP showcase** — a fresh copy of every feature in one scene: a .obj house, a physics ball, a pillar to jump on, a HUD crosshair and a starter flow graph (Circle toggles the box, walking up to the house logs a greeting). Use showcase for experiments instead of wrecking a shared sample. Generates a complete Tyra game project: `Makefile`, `Dockerfile`, `docker-compose.yml`, C++ sources rendering the scene (StaticPipeline, no asset files needed) and the single `<name>.tyra` project file.
- **Open project** (`Ctrl+O`) — pick the project's `<name>.tyra` file.
- **Spawn point** — a special scene object (marker with a direction arrow, no geometry in the game). In the FPP template the player starts at the first spawn point, facing its Y rotation.
- **Scene objects** — insert simple 3D primitives (box, sphere, cylinder, cone) via the *Scene* menu or the buttons in the *Project* panel. Each object has a name, position, rotation, scale and color, editable in the *Project* panel and saved to the `<name>.tyra` project file. Objects render both in the editor viewport and on the PS2 (scene data is regenerated into `inc/scene_data.hpp` on every build).
- **Transform gizmos** (ImGuizmo) — click an object in the viewport to select it, then drag the gizmo to move / rotate / scale it. Switch tools with the buttons in the viewport corner or the `W` / `E` / `R` keys; `Delete` removes the selected object. Left-drag on empty space or right-drag orbits the camera, wheel zooms.
- **Project file, window layout & undo history** — the whole project is a single `<name>.tyra` file: game data plus editor-side state (selection, active tool, view mode) and the ImGui window layout (docking, panel sizes), so your window arrangement is restored per project. The undo history (up to 100 snapshots) lives in a sidecar `<name>.history` file next to it, so undo/redo survives editor restarts; it is gitignored in generated projects. The `<name>.tyra` file is the source of truth — if it is edited outside the editor, the stored history is discarded automatically.
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
- **Textures (PNG)** — per-object textures (*Set...* in object properties; the object color modulates the texture, white = plain) and a tiled terrain texture (*Preferences > Rendering*). UVs are generated for all primitives, `vt` is read from .obj models. Tyra loads PNG natively: 32/24bpp or palletized 8/4bpp (fastest on the PS2); keep sizes power-of-two.
- **Terraforming** — *Sculpt (T)* mode in the viewport: paint the terrain with a smooth brush (LMB raises, Shift+LMB lowers, RMB orbits; radius/strength sliders). The heightmap is stored in `terrain.heights`, compiled into the game, and everything follows the relief: FPP walking, object physics, shading.
- **View modes** — *Solid*, *Wire* (colored wireframe) and *Wire+Solid* (solid shading with a wireframe overlay); buttons in the viewport corner, the choice is persisted in the `<name>.tyra` project file.
- **Scripts** — user C++ scripts live in `src/scripts/` and are compiled into the game by the normal build; the editor never regenerates your script files. The primary, Unity-style flavor is the **object script**: a class derives from `ObjectScript` (see the generated `inc/scripts/script.hpp`), overrides `onStart`/`onUpdate`/`onUsed` and registers with `TYRA_OBJECT_SCRIPT(MyScript);` — it runs only when **attached to an object** (*Properties > Scripts*, attach the same class to any number of objects across scenes). Every attachment becomes its own instance at scene load with its own members and a `self` pointer to the object it hangs on — spin `self`, read its color, react to the player using it. Global scripts still exist for game-wide logic: derive from `Script` (`init`/`update`), register with `TYRA_SCRIPT(MyScript);` and they run every frame in every scene. The *Scripts* section in the *Project* panel lists the files, creates new ones from an attachable stub (**New script...**, also available straight from *Properties > Scripts*, which attaches the new class to the selected object) and opens the project in VS Code (**Open in VS Code**) with working IntelliSense — the generated `.vscode/c_cpp_properties.json` points at the bundled Tyra engine headers and the PS2SDK headers exported from the docker toolchain on first build. Each `ScriptContext` gives access to the engine (pad!), player/camera position, scene objects and the sky color. New FPP projects include an example: walk up to the box and press X — the sky changes color and a message lands in the PCSX2 log. Full guide: [docs/object-scripts.md](docs/object-scripts.md).
- **Empty objects** — `+ Add object > Empty` inserts a pure transform (a small sphere marker in the editor, invisible and non-colliding in the game): an anchor to hang object scripts on, a waypoint for flow-graph logic; scripts read its position/rotation/scale/color.
- **Flow graph** — visual logic (CryEngine-style): wire triggers (On Start, On Button, Near Object, Every N Seconds) to actions (Set Sky Color, Show/Hide/Toggle/Move/Recolor Object, Log) in the *Flow Graph* tab; the graph compiles into `src/scripts/flow_graph.gen.cpp` on every build.
- **Physics** — per-object gravity (*Physics* checkbox) and FPP player physics: gravity, jumping on X, collision with scene objects, walking on top of them.
- **Custom .obj models** — `+ Model` imports a mesh into `res/models/`; it renders in the viewport and is compiled into the game (≤3000 tris per model). Models behave like any scene object (gizmos, physics, scripts, lighting).
- **Directional lighting** — light direction + ambient/diffuse in preferences, baked into vertex colors; identical shading in the viewport and on the PS2.
- **Sky** — gradient dome (horizon/zenith colors) or flat clear color; scripts can retint it at runtime.
- **HUD from images** — `+ Image (PNG)` imports into `res/hud/`; position/size editable with a live preview over the viewport; rendered in-game as 2D sprites.
- **Runtime scene** — scripts receive mutable `RuntimeObject`s (move/hide/recolor objects every frame); geometry rebuilds automatically.
- **Project preferences** (`Project > Preferences`, `Ctrl+,`) — game template, terrain size and detail (max grid cells), **triangle handling** (*Precise clipping* — no holes at screen edges but costs EE time, vs *Fast culling* — fastest, large near triangles may vanish), sky color, FPP camera (eye height, walk/look speed) and orbit speed. Stored in the `<name>.tyra` project file and baked into the generated `terrain_config.hpp` on every build; the viewport reflects sky color and terrain detail immediately.
- **Build & Run in PCSX2** (`F5`) — builds the ELF inside a Docker container (the `h4570/tyra` image with the PS2DEV toolchain) and launches it in PCSX2. Build logs stream into the *Output* window.

## Requirements

- Windows, [Docker Desktop](https://www.docker.com/products/docker-desktop/) (running), [PCSX2](https://pcsx2.net/) installed in `Program Files\PCSX2` (with BIOS configured).
- To build the editor: CMake, Ninja, GCC/MinGW (e.g. `scoop install mingw cmake ninja`).

## CLI

Projects can also be created and built headlessly:

```powershell
tyra-editor.exe --new <name> <parentDir> [width] [depth] [orbit|fpp]
tyra-editor.exe --build <projectDir> [--run]
tyra-editor.exe <projectDir|project.tyra>    # open GUI with a project loaded
```

## How Build & Run works

1. `docker compose up -d --build` in the project directory (the first run downloads the `h4570/tyra` image). Each project gets its own container (`<name>-compiler-1`).
2. The Tyra engine sources live **in this repo** (`vendor/tyra`, bind-mounted read-only at `/engine-src`). The Runner syncs them into the shared build volume (`tyra-engine-shared`, mounted at `/tyra`) with a checksum `rsync`; when anything changed, `libtyra` is rebuilt once and every game relinks against it — all projects share the result.
3. `rsync` project sources into the container volume (`/host` → `/src`).
4. `make` inside the container (`mips64r5900el-ps2-elf-g++` compiler).
5. `rsync` the `bin/` directory back to the host.
6. Launch `pcsx2-qt.exe -elf <project>\bin\<name>.elf`.

## Run on a real PS2 (network deploy)

With a PS2 connected to the LAN and running [ps2link](https://github.com/ps2dev/ps2link)
(one-time memory-card install), **Build > Build && Run on PS2** (`F6`) boots the
game on the console over ethernet: the ELF and every asset are served straight
from the project's `bin\` on this PC (no ISO, no SMB), and the console's log
streams live into the *Output* window as `[ps2]` lines. Set the console's IP
under `Project > Preferences > Real PS2`; headless: `--build <projectDir>
--run-ps2 [ip]`. The editor drives the console with a patched
[ps2client](https://github.com/ps2dev/ps2client) shipped in `tools/ps2client`
(see the README there — the patch fixes a Nagle/delayed-ACK stall that made
file serving ~100x slower).

## The in-tree Tyra engine

`vendor/tyra/engine` is a fork of the [Tyra engine](https://github.com/h4570/tyra) (Apache License 2.0, forked at `9273416`), maintained directly in this repo — edit it and the next Build & Run picks the change up automatically. The editor's modifications over upstream (marked `Modified by tyra-editor` / `tyra-editor guard band` in the sources):

- `planes_clip_algorithm.cpp` — Cohen–Sutherland outcodes: fully-visible triangles skip the 6-plane clipper, fully-outside ones are rejected instantly.
- `stapip_clipper.cpp`, `stapip_qbuffer.cpp` — static pools instead of per-call heap allocations.
- `render_bbox.cpp` — guard-band frustum margins; only near-plane-crossing geometry goes to the EE clipper, side overflow is left to the GS scissor.
- `vcl_sml.i` (`PerformClipCheck`) — VU1 guard band: XY accepted up to 3× outside the clip volume (the GS scissor trims pixels), with an explicit w ≤ 0 test for behind-the-camera vertices.
- `audio_song.cpp` — the song player reads the WAV header (PCM 8/16-bit, mono/stereo, standard rates, arbitrary chunk layout) instead of assuming 16-bit/22050 Hz/stereo with samples at a fixed offset.

Generated games additionally enable the real per-triangle clipper (`fullClipChecks` + `Precise` culling) and correct `PlanesClipAlgorithm::clipMargin` (the upstream default pushes the near clip plane ~10 units away from the camera).

Before each build the editor refreshes its generated files: `Dockerfile`, `docker-compose.yml`, `inc/terrain_config.hpp` and `inc/scene_data.hpp` are always rewritten from project data; `src/terrain_game.cpp` and `inc/terrain_game.hpp` are rewritten only while their first line still contains the `Generated by tyra-editor` marker — delete that line to take ownership of a file (scene objects will then stop syncing into the game code automatically).


## Sample project

[samples/script-demo](samples/script-demo) is a complete FPP project with the example script: walk up to the box and press X — the sky changes color. Open it via `File > Open Project` and pick its `script-demo.tyra` (the `.history` undo file and `.vscode/` IntelliSense config are local state — the editor recreates them on open/build). Then just Build & Run.

## Structure

- `src/` — editor code (`app` UI, `viewport` GL preview, `project`+`templates` project generator, `runner` docker/PCSX2 pipeline, `gl_loader` minimal GL loader).
- `samples/` — example projects.
- `vendor/tyra/engine` — the in-tree Tyra engine fork (versioned; Apache License 2.0).
- `vendor/` (rest) — editor dependencies (not versioned; see `setup.ps1`).
- `tools/` — PS2 network-deploy tools (`ps2client` versioned, the rest fetched by `setup.ps1`).

## Credits

This project stands on the shoulders of the PS2 homebrew community:

- **[Tyra engine](https://github.com/h4570/tyra)** by Sandro Sobczyński (h4570)
  and contributors — Apache License 2.0. `vendor/tyra/engine` is an in-tree
  fork; every departure from upstream is marked `Modified by tyra-editor` in
  the sources. The `h4570/tyra` Docker image provides the PS2 toolchain.
- **[ps2client](https://github.com/ps2dev/ps2client)** and
  **[ps2link](https://github.com/ps2dev/ps2link)** by the
  [ps2dev project](https://ps2dev.github.io/) contributors — the network link
  that "Run on PS2" is built on. `tools/ps2client/bin/ps2client.exe` is their
  work built from source with one local patch
  ([`tools/ps2client/nodelay.patch`](tools/ps2client/nodelay.patch),
  `TCP_NODELAY` on the request socket); upstream declares no explicit license
  file, so it is redistributed here in the spirit of the ps2dev homebrew SDK
  with full credit to its authors. `ps2link` is downloaded unmodified from
  upstream releases by `setup.ps1`.
- **[PS2SDK](https://github.com/ps2dev/ps2sdk)** (ps2dev) — the SDK every
  generated game links against; the custom `audsrv` build in
  `vendor/tyra/audsrv-pan` derives from its audsrv module.
- Editor dependencies fetched by `setup.ps1`: [Dear ImGui](https://github.com/ocornut/imgui)
  (MIT), [GLFW](https://www.glfw.org/) (zlib/libpng),
  [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (MIT),
  [imnodes](https://github.com/Nelarius/imnodes) (MIT),
  [stb](https://github.com/nothings/stb) (public domain / MIT).
- **[PCSX2](https://pcsx2.net/)** — the emulator behind every `F5`.

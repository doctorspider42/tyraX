# Changelog

What changed in each TyraX release, newest first. Every release is also on
[GitHub Releases](https://github.com/doctorspider42/tyraX/releases) with its
installers; releases before 1.170.0 are described only there and in the git
history.

## 1.173.0 - 2026-10-16

### A calmer, more modern editor

- **A refreshed look**, the first step of a main-window redesign: the default *Face buttons* theme has a new, deeper palette, docked panels now sit on a darker background with a small gap between them instead of meeting edge to edge, and value fields are inset. [The editor's look](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-theme.md)
- **Position, rotation and scale name their axis inside each field**: a red X, green Y and blue Z, the same colours as the move gizmo, and the numbers are in a fixed-width face so they no longer jitter while you drag. Collision box min/max read the same way.
- **The Output and Debug logs use a fixed-width font**, so timestamps, paths and counts line up.
- Inspector section titles use the interface font's heavier cut.
- **A new top bar**: the TyraX mark and the menus, then *project / scene* - the scene picker switches scenes and holds *New scene* and *Scene preferences*, and hovering the project name shows its location, terrain and target - and an **Unsaved** marker that saves when clicked. On the right: the build profile, a **PCSX2 | PS2** switch for the run target, Build, a green **Run** button and Stop. [The editor's look](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-theme.md)
- **The viewport's buttons became floating tool cards**: a vertical tool palette with icons (Move, Rotate, Scale, Sculpt, Paint, Measure, Surface snap) and their shortcut keys at the top left, the camera controls on a card at the bottom left and the World | Camera switch at the bottom right.
- **Properties reads as a card for the object**: its colour tile, its name in large type and its type under it at the top; sections are quiet titles instead of blue bands; every on/off option is a switch.
- **An icon rail on the left** switches the Project panel between Outliner, Layers, Assets, Audio and Scripts - one page at a time, so the object list gets the panel's full height - with the AI Assistant and Project Preferences at the bottom. Object rows show an icon per type.
- **The Output and Debug logs** mark error and warning lines in a gutter, their level filters are coloured pills, and their on/off options are switches.
- **A status bar** along the bottom: the BUILDING / BUILD FAILED, LIVE, DBG, LOGIC and SESSION chips moved there from the top, with the scene's object and selection counts on the right. The Project panel no longer repeats the project's details and scene list - they are in the top bar.

## 1.172.0 - 2026-10-12

### The macOS app

- **TyraX for macOS as a download**: `tyrax-<version>-macos-arm64.dmg` (Apple silicon) and `-macos-x86_64.dmg` (Intel), macOS 15 or newer. Open it, drag TyraX into Applications - signed with a Developer ID and notarized by Apple. [Installing and updating](https://github.com/doctorspider42/tyraX/blob/main/docs/updates.md#macos)
- **Builds PS2 games with nothing else installed** - no Homebrew, no Xcode. The app brings GNU make, the libraries the PS2 compilers need and ready-built OpenVCL/vclpp/bin2s; the first Build & Run downloads PS2DEV and is ready seconds later. Only a project with its own VU programs asks for the Xcode Command Line Tools.
- **It updates itself**: *Install* swaps the whole app for the new one and reopens it.
- Started from Finder, TyraX now also finds tools in `/usr/local/bin` and `/opt/homebrew/bin` (VS Code's `code`, Docker, a Homebrew PCSX2).
- From source, the PS2 toolchain no longer breaks when Homebrew upgrades `libmpc` and friends: it keeps its own copies.

## 1.171.0 - 2026-10-12

### macOS

- **The editor runs natively on macOS** (Apple silicon and Intel, built from source): `xcode-select --install`, `./setup.sh --deps` (Homebrew, no root), `./build.sh --run`. Native file dialogs, Finder reveal, image copy to the clipboard and the system fonts (Helvetica Neue for the interface) all work; the interface is drawn at full Retina density.
- **Games build natively on a Mac with PS2DEV + OpenVCL**, no Docker: the first Build & Run installs the pinned PS2DEV v2.0.0 macOS archive, builds the in-tree OpenVCL (and runs its tests), vclpp, bin2s and audsrv, exactly as on Linux. A first game build took 43 seconds on an Apple silicon Mac; an unchanged one under a second. [docs/native-toolchain.md](https://github.com/doctorspider42/tyraX/blob/main/docs/native-toolchain.md#macos)
- PCSX2 is found as `PCSX2.app` in `/Applications` or `~/Applications`, and the generated `run.sh` launcher looks there too.
- No macOS release package yet - see [Installing and updating](https://github.com/doctorspider42/tyraX/blob/main/docs/updates.md).

## 1.170.2 - 2026-10-12

### AI support in game projects

- The guides **Add AI support** installs into a game project (Claude Code, Codex, Copilot) are up to date again: which files the editor rewrites (now including `src/main.cpp`, `Makefile`, `.vscode/c_cpp_properties.json` and the split `src/gen/game_*.gen.cpp` sources) and which you can own (`inc/scripts/flow_nodes.hpp` added), object vs global scripts, the flow-graph JSON (`number` links, `pin`/`fpin`), the one model importer and the Model / Animated model entities, and the bakes a build reads vs. re-bakes.
- They now list the CLI verbs an assistant needs: `--migrate`, `--bake-status`, `--capture-frame`, the full `--import-model`, `--build --run-ps2` / `--rebuild` - and `--search-docs`, so an assistant can read the editor documentation the guides cite.
- `--help` shows `codex` for `--add-ai-support`, and lists `--bake-shadows`, `--bake-particles`, `--bake-object-light` and `--capture-frame --timeout`.
- `--list-nodes` (and so the AI flow-graph generator and assistant) names the three outputs of **On Fact Changed** (changed / became true / became false) - it used to show one - and describes the wired number of Clamp, Lerp and similar nodes as the value they operate on rather than as overriding the first parameter.
- `docs/vu-authoring.md`: project VU code is compiled by the build machine's own `g++` in the default native build, in the container only with `--docker`.

## 1.170.0 - 2026-10-11

### One model importer

- **One importer for `.obj`, `.glb` and `.fbx`**, with a live preview before anything is written (right drag orbits, middle drag pans, wheel zooms; missing textures show as white surfaces). [docs/model-import.md](https://github.com/doctorspider42/tyraX/blob/main/docs/model-import.md)
- **Always the same result**: a static model is `.obj` + `.mtl` + PNG textures, an animated one keeps its `.glb`/`.fbx` + `.mtl` + PNGs. Every import writes a `.mtl`, even for a model without materials, and extracts every texture (embedded ones included) as PNG.
- **Split a pack** into one model per top-level object or per mesh, untick the ones you do not want, click a piece in the preview to find it in the list, and place them all at the file's own layout as one editor group.
- **Pivot, real-world size, clean-up** (degenerate and duplicate triangles), **triangle reduction** and **max texture size**, with the VU1 vertex count the build will actually ship.
- **Folder**: file imports into a catalogue (`res/models/terrain/rocks`); remembered between imports.
- **Search folder...** finds textures that are not where the file says (packs that point at the artist's own disk).
- Headless: `tyrax-editor --import-model <project> <model> [--split top|mesh] ...`.

### Model and Animated model

- **Add object > Object > Model... / Animated model...** replace the long list of files. Each opens a **model picker**: folder tree, thumbnails, search, *Import new model...*.
- The entity decides static or animated, not the file: a `.glb`/`.fbx` placed as a static Model gets an `.obj` twin.
- The picker lists models from **anywhere under `res/`**, so projects made before this release (models in `res/aster/`, hand-made folders) show all of their models. Nothing to migrate.

### Properties

- Models, primitives and save points have **collapsible sections**: Model, Transform, Material, Gameplay, Distance and LOD, Performance, Lighting and shadows, Scripts.
- The impostor controls became one **Distant version** choice (None / Billboard / Hull proxy) with one Bake button and *Switch at*. The checkbox labelled "Impostor" (really "capture on the GPU") is gone; GPU capture with CPU fallback is automatic.
- **Material > Edit...** is always there on a model and opens the model's own `.mtl` when no override is assigned. The per-material list moved to the Material Editor; Properties only warns about missing textures. The Material field lists every `.mtl` under `res/`.

### Fixes

- A `.fbx` with more than 256 meshes lost every object past the 256th (the skeletal parser's palette index is a byte). Static `.fbx` geometry is now read directly: 545 of 546 objects on a real pack.
- `--ui-script`'s `drag` takes an optional `left|right|middle` button.

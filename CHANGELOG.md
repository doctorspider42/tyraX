# Changelog

What changed in each TyraX release, newest first. Every release is also on
[GitHub Releases](https://github.com/doctorspider42/tyraX/releases) with its
installers; releases before 1.170.0 are described only there and in the git
history.

## 1.173.0 - 2026-10-16

### A redesigned main window

- **The editor has its own typeface, Geist** (Geist Mono for numbers and logs), built in - the same letters on every platform, with the system fonts as a fallback for symbols Geist lacks.
- **A new look**: a deeper default palette, renamed *Graphite* (the *Face buttons* name and the PlayStation button symbols in the editor's own chrome are gone; the editor now carries a trademark notice in Help and on the welcome screen), every panel a rounded card on a darker background, inset value fields, and the interface font's heavier cut for titles. A panel docked on its own shows its own header (the Project panel reads "Outliner 81" with a + button) instead of a tab strip and a ▼ menu - drag the header to move it; panels sharing a dock node keep their tabs, drawn as quiet pills. [The editor's look](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-theme.md)
- **The title bar is part of the editor** on macOS and Windows: the traffic lights (or the minimize / maximize / close buttons) sit in the top bar; drag its empty parts to move the window, double-click to zoom. *Edit > Preferences > Appearance > Use the native title bar* brings the system bar back after a restart; Linux keeps its own.
- **The menus moved behind the TyraX logo** at the top left (click it, press F10 or tap Alt); menu paths like *Tools > Remote Pad* are unchanged. The top bar now holds *project / scene* (the scene picker switches scenes; hovering the project shows its location, terrain and target), a "3 unsaved" marker that saves when clicked, four **workspaces** - Scene, Logic, Cinema, UI & HUD, one click each to a window layout - a wide **search** field, and on the right the build profile, a **PCSX2 | PS2** target switch, Build, a green **Run** and Stop.
- **Command palette** - Ctrl+K (Cmd+K on a Mac) or the search field: find and run commands, jump to an object, a scene, a window, a workspace or a documentation page by typing; Ctrl+Enter (Cmd+Return) hands what you typed to the AI Assistant as a draft.
- **A status bar** along the bottom with the BUILDING / BUILD FAILED, LIVE, DBG, LOGIC and SESSION chips, the last message, and the scene's object and selection counts.
- **An icon rail on the left** switches the Project panel between Outliner, Layers, Assets, Audio and Scripts - one page at a time, so the object list gets the full height - with the AI Assistant and Project Preferences at the bottom.
- **Outliner**: an icon per object type, filter chips (All, Models, Primitives, Lights, Areas, Audio, Cameras, Other), layers / prefabs / groups as folders with counts, an eye on every row to hide an object in the editor for the session, and a strip of layer chips that toggle a layer's visibility.
- **The viewport's controls float on frosted cards** that blur the scene behind them: a tool palette with icons and shortcut keys (Move, Rotate, Scale, Sculpt, Paint, Measure) at the top left; a view pill at the top centre with the projection, the look-through camera, the render mode, Surface snap and World | Camera; the selection at the bottom left - name, type, triangles and size in metres - with **Frame** (new hotkey **F**); and **Editor | As PS2** at the bottom right. *Center view* moved into the gear menu.
- **Properties** is headed by the object - its colour tile, its name in large type, its type - with a ... menu holding Duplicate and Delete object. Fields are rows with the caption on the left; every on/off option is a switch whose "(...)" text shows as a dim second line; help moved onto the caption (hover the underlined label). Solid objects get **Object / Material / Physics** tabs, kept while you select other objects. Position, rotation and scale name their axis inside each field (red X, green Y, blue Z, like the gizmo) in a fixed-width face; colour is a swatch with a picker and a #RRGGBB field.
- **The console drawer**: in the Default layout Output, Debug and the Debugger are tabs of one bottom panel, and a debug launch brings the Debugger forward (saved layouts keep their arrangement - *Layout > Reset to built-in arrangement* applies it). Logs use a fixed-width font with a time per entry, a gutter mark on error and warning lines, coloured level pills on one header row, and a count badge on the Output and Debug tabs.
- The Debugger layout (the Logic workspace) now docks the Project panel on the left instead of leaving it floating.
- `--ui-script`: menus are reached with `click 'TyraX menu'`, but a step naming a menu (`click Tools`) opens it by itself, so existing scripts keep working; `key ctrl+X` now reaches Ctrl shortcuts on macOS.
- Dear ImGui is now kept in the repository (`vendor/imgui-tyrax`) with small, documented patches, instead of being downloaded by the setup scripts.

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

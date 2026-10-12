# Changelog

What changed in each TyraX release, newest first. Every release is also on
[GitHub Releases](https://github.com/doctorspider42/tyraX/releases) with its
installers; releases before 1.170.0 are described only there and in the git
history.

## 1.170.2 - 2026-10-12

### AI support in game projects

- The guides **Add AI support** installs into a game project (Claude Code, Codex, Copilot) are up to date again: which files the editor rewrites (now including `src/main.cpp`, `Makefile`, `.vscode/c_cpp_properties.json` and the split `src/gen/game_*.gen.cpp` sources) and which you can own (`inc/scripts/flow_nodes.hpp` added), object vs global scripts, the flow-graph JSON (`number` links, `pin`/`fpin`), the one model importer and the Model / Animated model entities, and the bakes a build reads vs. re-bakes.
- They now list the CLI verbs an assistant needs: `--migrate`, `--bake-status`, `--capture-frame`, the full `--import-model`, `--build --run-ps2` / `--rebuild` - and `--search-docs`, so an assistant can read the editor documentation the guides cite.
- `--help` shows `codex` for `--add-ai-support`.

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

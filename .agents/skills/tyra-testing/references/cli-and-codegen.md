# Layer 1/2 — headless CLI verbs, bakes, codegen inspection, format versioning

Read when: you use a headless verb beyond `--new`/`--build`, verify a bake (GI, model AO, pre-lit, particles, vehicles, procedural), inspect generated code without a game build, or test a `.tyra` format/migration change.

`TYRAX` = `build\tyrax-editor.exe` (Windows) / `build/tyrax-editor` (Linux). Full flag list: SKILL.md cheat sheet or `TYRAX --help`.

## Fixtures: `--new`

- `--new <name> <parentDir> [w] [d] [empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]` scaffolds a complete project (all generated sources, Makefile, docker-compose.yml) with no Docker.
- `fpp` = one Player in walk mode, `thirdperson` = same entity in third-person mode (identical game sources; mode is a per-object property). `empty` = orbit-camera scene, no objects. Preset becomes the permanent `template` field — the only way to fixture either game template (no switch afterwards).
- Defaults = *New Project* dialog: 100x100 terrain, 1 unit = 1 m, **debug** profile, Live Link on, USB keyboard & mouse off — except preset defaults to `empty` here (dialog: `fpp`). It echoes terrain size + scale: `--new` + grep the `.tyra` is the cheapest "did a new-project default land" check. A fixture needing old release-profile behaviour must set it.
- `--no-terrain` (anywhere among optionals): `.tyra` gets `"enabled": false`, `inc/scene_data.hpp` reads `TERRAIN_ENABLEDS = {false}`, `TERRAIN_TEXTURES = {-1}`. In PCSX2 the player falls at boot unless the scene has a floor object — that is the feature (docs/terrain.md).
- Put scratch projects in a **short path** outside the repo (`%TEMP%\tyra-editor-test\<name>`, `~/tyra-projects/<name>`). Never the session scratchpad for anything that boots: ~180+ chars, PS2 `loadelf` truncates (`secname ...` in emulog) → null PC before the Tyra banner, looks like an engine bug.

## `--resave`, `--refresh-gen`, `--build`

- `--resave`: load (runs every migration) + write `.tyra` + heights, no Docker. Round-trip a format change: strip/alter a field, resave, inspect. Also the batch-migration tool.
- `--refresh-gen`: `project::refreshGenerated` without toolchain/Docker. **Does NOT run texbake** — nothing under `.res-baked/` (quantized textures, atlas pages, terrain AO, lightmap atlas) is rebuilt; comparing a baked image after it compares a stale file with itself. Use `--build` when measuring an asset.
  - It DOES run the bakes inside refreshGenerated: animated models → `res/models/*.tskl`, static → `*.tmdl` (`[anim bake]`/`[model bake]` lines; read tier vertex counts from bytes via `src/tmdl.hpp`/`glbparser.cpp` layout), credits page strips → `res/credits/<roll>-<k>.png` (stitch + look; `CREDITS_PAGE_TOTAL`/`contentH` in `inc/credits_data.gen.hpp`, docs/credits.md).
  - It DOES run the **vehicle bake** (`vehbake::bakeProject`, `[vehicle]` lines): `vehbake::adoptMeasured` hands the emissive `lamps` part index + rear corner count back to the definition and codegen writes them into `VEHICLE_DEFS` (without the bake a refresh emits `-1`). Check: `grep -o '{[^}]*}, {[^}]*}, [0-9-]*, [0-9]*, [0-9-]*, [0-9.]*F}' inc/scene_data.hpp` reads `..., 2, 60, ...` on the CC96. GS-capture lamp colours must read exactly (175,32,24) lit / (78,14,12) off / (255,45,35) braking.
- Both `--build` and `--refresh-gen` run the **procedural bake** first (`procbake::bakeAll`, docs/procedural-generation.md): stale volumes baked into chunk meshes, project SAVED (`procedural: baked N volume(s) -> ...`) — expect a `.tyra` diff. Fast graph check: refresh + grep `inc/scene_data.hpp` for `<volume>#<asset>-x<i>z<j>` chunk objects. procgraph/procgen/procbake link without GUI/GL/templates.cpp (PROGRESS 171 property list caught three bugs).
- `--build` streams the native (or `--docker`) log to stdout and returns a real exit code. `--run`, `--run-ps2 [ip]`, `--rebuild`.
- `--dump` / `--dump-graph` / `--apply-graph` = machine-readable project I/O (apply validates node types + pin rules, saves). `--ai-graph` runs AI generation (docs/ai-tools.md); stub backend: a `claude.cmd` on PATH that swallows stdin (`findstr /r ".*" > nul`) and echoes a graph JSON (PROGRESS 65).
- `--road-crossings <dir> [scene]`: every road crossing + override; exit 1 on an orphaned junction override (docs/roads.md). `--batch-report <dir> [scene]`: static batching and why each object does not batch. `--atlas-report <dir>`: what atlasing packed/refused.

## Bakes

- **`--bake-gi`** (docs/global-illumination.md): every scene into `.res-baked/gi/`, then refreshes generated files. Headless twin of *Tools > Ambience Editor > Global illumination*. Check: `inc/ao_data.gen.hpp` `SCENE_AO_ATLAS_GIS`/`SCENE_AO_MAP_GIS` (1 = shipped GI), `inc/probe_data.gen.hpp` has a `SCENE_PROBE_GRIDS` entry, then `--build --run` and A/B against `"giEnabled": false`. `--gpu` uses the GPU path; `--gi-gpu-check` checks GPU vs CPU agreement.
  - Not part of a build by default (build only READS the cache): a scene change silently falls back to pre-GI lighting until re-baked. `"giAutoBake": true` makes `--build` re-bake STALE scenes first (`gi: baked GI ...` / `gi: fresh ...`); the check is a second build saying `fresh` for all.
  - Prints per-scene time + atlas/terrain/probe dims; `atlas 0` = no eligible receivers.
- **`--bake-status`**: read-only. Per scene GI + baked shadows (`fresh` / `STALE` with cache + live signatures / `absent (or an older cache version)` / `off`), per stamped pre-lit object, per model-AO map, then `bake status: fresh|STALE`; exit 0/3. Run after any bake-signature or cache-version change on every example with a checked-in cache (`git ls-files examples | grep res-baked`). `absent` on a checked-in cache = format version moved without re-bake. Line-ending test (1.164.2): copy example out of tree, rewrite `.obj`/`.mtl` to CRLF → still `fresh`.
- **`--bake-model-ao [--texbake]`** (docs/ambient-occlusion.md "Model AO"): per-model self-AO into `.res-baked/modelao/`, one line per (model, texture): `baked`/`fresh`/`skipped ... : reason`. `--texbake` also runs the multiply into `.res-baked` (`model AO: multiplied into <texture>`) — the only container-free check of the chain. Then compare `res/models/x.png` vs `.res-baked/models/x.png`: RGB mean darker, alpha IDENTICAL (GS cutout rule). Re-run → `fresh`, byte-identical map. Refuses unless `"modelAo": true` in settings.
- **`--bake-prelit <dir> [scene]`** (docs/prelit-models.md): re-bakes objects with `prelitWanted` whose baked texture no longer matches; saves + regenerates; `baked`/`fresh` per object, non-zero exit on failure. **Two runs are the test** (second: all `fresh`). To make one stale move it in `objects/<id>.json`; `mean light` changes. Stamp: `prelitSig` (hex), `prelitWanted`, `prelitSource` (only if a material override existed before first bake). `"prelitAutoBake": true` → `--build` runs it first (`pre-lit: ...`; second build `0 baked, N already fresh`). `--bake-object-light <dir> <object> [size] [rays]` bakes one object's light into `res/materials/<name>-lit.png` + `.mtl` (twin of the Properties button).
- `--bake-shadows <dir>`: baked-shadow cache (content-hashed; survives toggling `bakedShadows`).
- `--bake-particles <dir>`: see acceptance-physics-procedural-import.md. `--bake-impostor`: acceptance-rendering.md.

## Far vehicle tier, without eyes (1.134.0, docs/vehicles.md "An authored far model")

- Bake log: `far model X N tris (wheels in), S submit(s) past D units (T for cars nobody drives)` or `no far tier carries the wheels`, plus per part `part K name L list verts -> S strip (x); tier ...; hidden far` (strip ratio = packing acceptance). Game logs `VEHLOD car N tier T swap at D`.
- To SEE it: scratch copy, `farDistance` 3 (driven car) or `trafficDistance` 3 (others), `--capture-frame`. Compare with shine off (`settings.vehicleShineBudget` 1 = only driven car shiny) — a far car drops its shine. The vehicle-playground chase camera does not orbit under `--pad "stick r ..."` at boot; move the parked car into view in the copy's object JSON (forward = +z).

## Layer 2 — inspecting codegen

- `--new` writes every generated file; `--refresh-gen` refreshes `inc/scene_data.hpp`, `src/gen/flow_graph.gen.cpp`, etc. for an existing project.
- Ownership (tyra-editor-dev): `.gen.*` + `scene_data.hpp` always rewritten; `terrain_game.cpp`/`controls.hpp`/`script.hpp` regenerate only while their marker line is intact — stale-looking there may be user-owned by design.
- Test a specific graph/scene shape by editing `<name>.tyra` directly (source of truth; stale undo in `<name>.history` is discarded), refresh, inspect.
- `examples/script-demo/` is a checked-in generated project — only as fresh as its last regeneration; regenerate before using as a diff baseline. All examples' generated files drift silently.
- Codegen harness: scratch `main()` building a `Project`, calling `templates::generate()`, asserting on strings (`project.cpp`, `templates.cpp`, `json.cpp` have no ImGui/GLFW deps). Keep harnesses in the scratchpad. Link recipes: host-harnesses.md.
- A new generated file must join `refreshGenerated`'s allowlist, not only the `--new` scaffold (`inc/bag_array.gen.hpp` was missing → every existing project failed to compile; same shape as the `live_pad.gen.cpp` mistake).

## Format versioning (docs/format-versioning.md)

- `--build`, `--resave`, `--refresh-gen`, `--apply-graph`, `--ai-graph` refuse (exit 1) a project whose `formatVersion` has pending REGISTERED migrations. `--migrate` backs up `.tyra` + `objects/` + heights + splat + flow-nodes/screen-effects into `_backup/`, applies, resaves the `--resave` file set; plain resave when current.
- Gate test: hand-edit `"formatVersion"`: above `version::kFormatVersion` must be refused by every path; lower opens silently unless a step is registered in `migrations.cpp`.
- With no step registered, prompt/backup is unreachable → register a temporary throwaway step to check: `_backup/` holds **every** file the post-migration save rewrites (drop a sentinel `terrain-<scene>.splat` first — the save deletes it when the scene has no layers), and a step returning `false` leaves every file md5-identical.
- `migrations::validate()` catches mis-registered steps: `BROKEN MIGRATION REGISTRY` on stderr at the first consulting command, `run` aborts with disk untouched. **A step whose `kFormatVersion` bump you forgot produces NO steps** — that stderr line is the only clue.

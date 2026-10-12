# troubleshoot/tyra-testing-cli.md

Layer 1 — the headless CLI verbs and their bakes. Symptom first. BLSS and VU1 flags live in `tyra-testing-blss.md`.

```
TYRAX --new <name> <parentDir> [width] [depth] [empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]
TYRAX --build <projectDir> [--run | --run-ps2 [ip]] [--rebuild] [--docker]   # exit code 0 = success
TYRAX --resave <projectDir>          # load + save, no Docker
TYRAX --migrate <projectDir>         # backup + apply format migrations
TYRAX --refresh-gen <projectDir>     # regen sources, no Docker
TYRAX --bake-gi <projectDir>         # bake global illumination, no Docker
TYRAX --bake-status <projectDir>     # read-only: is every bake cache fresh? exit 3 = stale
TYRAX --bake-model-ao <projectDir> [--texbake]   # per-model self-AO, no Docker
TYRAX --bake-prelit <projectDir> [sceneName]     # re-bake STALE pre-lit objects
TYRAX --dump <projectDir>            # JSON project summary
TYRAX --atlas-report <projectDir>    # what texture atlasing packed, refused (with reasons), and costs
TYRAX --batch-report <projectDir> [scene]  # how static objects batch, and WHY each one that does not (docs/static-batching.md)
TYRAX --chat-prompt [projectDir]     # what the AI Assistant is told (docs/ai-chat.md)
TYRAX --list-nodes <projectDir>      # what the graph generator is told
TYRAX --dump-graph <projectDir> <object> [scene]
TYRAX --apply-graph <projectDir> <object> <g.json> [scene] [--append]
TYRAX --vehicle-check                # drive-model property tests, exit 0 = pass
TYRAX --pad <projectDir> "<script>"  # drive the RUNNING game's pad, no focus
TYRAX --ui-script [projectDir] "<script>"  # drive the EDITOR's own UI, no focus
TYRAX <projectDir|project.tyra>      # open GUI on a project
```

## `--new` does not match the New Project dialog

**Cause:** `--new` defaults match the dialog except the preset, which stays `empty` here while the dialog starts on `fpp`. `fpp` seeds one Player in walk mode and `thirdperson` the same entity in third-person mode (both generate the SAME game sources — the mode is a per-object property); `empty` is an orbit-camera scene with no objects. The preset is the project's permanent `template` field.
**Fix:** `--new` + a grep over the `.tyra` is the cheapest check that a new-project default landed. Defaults: 100x100 terrain, 1 unit = 1 m, the **debug** profile with Live Link on, USB keyboard & mouse off. A fixture that needs old release-profile behavior must set it explicitly.

## `--no-terrain` scaffold

**Cause:** `--no-terrain` (accepted anywhere among the optional arguments) scaffolds a scene with no ground at all.
**Fix:** the `.tyra` gets `"enabled": false` and `inc/scene_data.hpp` reads `TERRAIN_ENABLEDS = {false}` with `TERRAIN_TEXTURES = {-1}`. Verifying it in PCSX2 needs a floor object in the scene, or the player falls at boot — that is the feature, not a bug.

## A GI bake "does nothing" after a scene change

**Cause:** the bake is NOT part of a build by default; a build only READS the cache, so a changed scene silently falls back to pre-GI lighting.
**Fix:** `--bake-gi` runs the whole GI bake into `.res-baked/gi/` then refreshes generated files — no Docker, no GUI. Grep `inc/ao_data.gen.hpp` for `SCENE_AO_ATLAS_GIS`/`SCENE_AO_MAP_GIS` (1 = the scene shipped GI), check `inc/probe_data.gen.hpp` has a `SCENE_PROBE_GRIDS` entry, then `--build --run` and A/B the screenshot against the same project with `"giEnabled": false`. `"giAutoBake": true` makes `--build` re-bake exactly the STALE scenes first, printing `gi: baked GI ...` / `gi: fresh ...`; the check is a second build that says `fresh` for all. It prints per-scene timing plus the atlas/terrain/probe dimensions (`atlas 0` = no eligible receivers).

## `--bake-status` says `absent` on a checked-in cache

**Cause:** the format version moved without a re-bake.
**Fix:** `--bake-status <dir>` answers "is the cache fresh?" WITHOUT baking or writing anything: one line per scene for GI and baked shadows (`fresh` / `STALE` with signatures / `absent (or an older cache version)` / `off`), one per stamped pre-lit object and one per model-AO map, then `bake status: fresh|STALE`; exit 0 or 3. Run it after any change to a bake signature or cache version, on every example that checks a cache in (`git ls-files examples | grep res-baked`). Line-ending recipe (1.164.2): copy the example out of the tree, rewrite its `.obj`/`.mtl` to CRLF, and it must still read `fresh`.

## `--bake-model-ao` and the texture-bake chain

**Cause:** the shipping path for Model AO is `texbake`, which runs only inside a real Docker build.
**Fix:** `--bake-model-ao <dir> --texbake` is how the whole chain is checkable with no container; the per-model self-AO lands in `.res-baked/modelao/` (one line per model,texture), the second flag runs the actual multiply into `.res-baked` and logs `model AO: multiplied into <texture>`. The honest check afterwards is a pixel one: decode `res/models/x.png` and `.res-baked/models/x.png`, compare RGB means (baked must be darker) and alpha channels (must be IDENTICAL — the GS cutout rule). Re-run to confirm idempotence (`fresh`, byte-identical map). The verb refuses when the feature is off, so a fixture needs `"modelAo": true` in its `.tyra` settings.

## `--bake-prelit` never reports fresh

**Cause:** the bake signature is unstable, or the object moved.
**Fix:** `--bake-prelit <dir> [scene]` re-bakes every `prelitWanted` object whose baked texture no longer matches, then saves and regenerates; prints `baked`/`fresh` per object plus a summary, exits non-zero on failure. **Two runs are the test**: the second must report everything `fresh` and bake nothing. To see a stale one, edit the object's position in `objects/<id>.json` and run again — it re-bakes and the reported `mean light` moves. Stamp: `objects/<id>.json` gets `prelitSig` (hex string), `prelitWanted`, and (only with a prior material override) `prelitSource`. Refuses a project needing a format migration. `"prelitAutoBake": true` makes `--build` run the SAME loop first, printing `pre-lit: ...`; the check is `0 baked, N already fresh` on the second build. `--bake-object-light <dir> <object> [size] [rays]` bakes one object's light into `res/materials/<name>-lit.png` + `.mtl` (twin of the Properties button).

## `--resave` round-trips a `.tyra` format change

**Cause:** `project::load` runs every format migration.
**Fix:** strip/alter a field, `--resave`, and inspect the rewritten file. Also the one-shot batch-migration tool for existing projects.

## `--refresh-gen` "proves" my asset change did nothing

**Cause:** `--refresh-gen` does NOT run `texbake`, so nothing under `.res-baked/` is rebuilt; comparing a baked image compares a stale file against itself.
**Fix:** use `--build` whenever the thing you measure is an asset. `--refresh-gen` DOES run the animated-model bakes into `res/models/*.tskl` and static ones into `res/models/*.tmdl` (`[anim bake]`/`[model bake]` lines; read tier vertex counts from bytes via `src/tmdl.hpp`/`glbparser.cpp` layout) and the credits page strips into `res/credits/<roll>-<k>.png` — read `CREDITS_PAGE_TOTAL` / `contentH` out of `inc/credits_data.gen.hpp`. `--dump`/`--dump-graph`/`--apply-graph` are machine-readable project I/O (apply validates node types + link pin rules and saves); `--ai-graph` stubs to a `claude.cmd` on PATH that swallows stdin (`findstr /r ".*" > nul`) and echoes a graph JSON.

## A refresh emits `-1` for a vehicle lamp part

**Cause:** `--refresh-gen` DOES run the vehicle bake (`vehbake::bakeProject`, the `[vehicle]` lines) — the bake hands measurements BACK to the definition (`vehbake::adoptMeasured`) and codegen bakes those into `VEHICLE_DEFS`.
**Fix:** check with `grep -o '{[^}]*}, {[^}]*}, [0-9-]*, [0-9]*, [0-9-]*, [0-9.]*F}' inc/scene_data.hpp` on the `VEHICLE_DEFS` row — reads `..., 2, 60, ...` on the CC96 (part 2, 60 rear corners). The lamp colours on a GS capture then read EXACTLY the runtime's constants ((175,32,24) lit / (78,14,12) off / (255,45,35) braking).

## Far tier without eyes (1.134.0)

**Cause:** see docs/vehicles.md "An authored far model".
**Fix:** the bake logs `far model X N tris (wheels in), S submit(s) past D units (T for cars nobody drives)` or `no far tier carries the wheels`, plus a line per body part: `part K name L list verts -> S strip (x); tier ...; hidden far` — the strip ratio is the packing acceptance number. The game logs `VEHLOD car N tier T swap at D` on every swap. To SEE a tier, set `farDistance` 3 (the driven car) or `trafficDistance` 3 (every other car) on a scratch copy and `--capture-frame`. Compare against the full car with the shine off (`settings.vehicleShineBudget` 1 leaves only the driven car shiny). The chase camera in vehicle-playground does not orbit under `--pad "stick r ..."` at boot; move the parked car into view in the copy's object JSON instead (forward is +z there).

## A headless build mutates the `.tyra`

**Cause:** both `--build` and `--refresh-gen` run the procedural bake first (`procbake::bakeAll`): stale Procedural volumes are baked into their chunk meshes and the project is saved, printing `procedural: baked N volume(s) -> ...`. The chunk objects are real scene objects.
**Fix:** expect that diff, and use it — the fastest way to test a graph change is `--refresh-gen` + grep `inc/scene_data.hpp` for the `<volume>#<asset>-x<i>z<j>` chunk objects. The graph model itself is harness-testable (procgraph/procgen/procbake link without GUI, GL or templates.cpp).

## A headless verb refuses my project (exit 1)

**Cause:** format versioning. `--build`, `--resave`, `--refresh-gen`, `--apply-graph` and `--ai-graph` refuse a project whose `formatVersion` has pending REGISTERED migration steps.
**Fix:** `--migrate` is the explicit tool — backs up `.tyra` + `objects/` + heights + splat + flow-nodes/screen-effects into `_backup/`, applies the steps, resaves the same file set as `--resave`; degrades to a plain resave when current. To test a version gate, hand-edit `"formatVersion"` in the manifest: a value above `version::kFormatVersion` must be refused by every path, a lower one opens silently unless a step is registered in `migrations.cpp`.

## My migration does nothing

**Cause:** a step whose `kFormatVersion` bump you forgot produces NO steps to run; `migrations::validate()` prints `BROKEN MIGRATION REGISTRY` on stderr at the first command that consults it and `run` aborts with disk untouched.
**Fix:** read that stderr line. Two things a throwaway step is the only way to reach: that the `_backup/` copy holds EVERY file the post-migration save rewrites (drop a sentinel `terrain-<scene>.splat` in first — the save deletes it when the scene has no layers), and that a step returning `false` leaves every file md5-identical.

## A scratch project crashes to a null PC before the Tyra banner

**Cause:** the path is too long. PS2 `loadelf` truncates long `host:` ELF paths (emulog shows a mangled `secname ...`).
**Fix:** create scratch projects in a **short** path outside the repo — the convention is `%TEMP%\tyra-editor-test\<name>`. Do NOT use the session scratchpad for anything that boots in PCSX2: its path is ~180+ chars.

# tyra-editor-dev — troubleshooting index

This is the index for the traps, rules-with-a-dead-end and per-feature
verification procedures that used to live in `.agents/skills/tyra-editor-dev/SKILL.md`.
The skill holds the rules; this folder holds "I hit X, the cause was Y, the fix
is Z". Start here, then open the topic file.

| Symptom / topic | File |
|---|---|
| Adding a property/type/node/preference; which files a change touches | `tyra-editor-dev-chain.md` |
| Batching, matrix fast path, `dirty`, lit bags, AO/GI lightmaps, VRAM | `tyra-editor-dev-rendering.md` |
| Static model consolidation, material refresh, primitive UV parity | `tyra-editor-dev-models.md` |
| Runtime geometry, physics, areas, reverb, scroller, player/multiplayer, input | `tyra-editor-dev-runtime.md` |
| Live Link / Live Logic / Debugger / time machine / Remote Pad / recorder / devkit | `tyra-editor-dev-live.md` |
| Panels, Preferences window, menus & styles, DPI, ImGui traps, fonts, credits | `tyra-editor-dev-ui.md` |
| Project lifetime, serialization sections, format version, AI assistant | `tyra-editor-dev-project.md` |
| Object grouping, editor picking, screen icons, terrain optional | `tyra-editor-dev-viewport.md` |
| Build, packaging, CMake, deps, VU aliases | `tyra-editor-dev-build.md` |
| Probe directions, SH receivers, impostors | `tyra-editor-dev-lighting.md` |
| Vehicles (damage, exhaust, lamps, controls, tuning) | `tyra-editor-dev-vehicles.md` |
| Roads (spans, strips, junctions, editing) | `tyra-editor-dev-roads.md` |
| Procedural placement, frozen bakes | `tyra-editor-dev-procedural.md` |
| Devkit captures, cadence, hardware timeline, submission scopes | `tyra-editor-dev-devkit.md` |
| Portal doorway rule | `tyra-editor-dev-portals.md` |

## An edit lands locally but never reaches session peers
**Cause:** the mutation path writes project state without bumping
`modelEditSerial_` — live-session sync detects edits only through
`commitChange()`, `applySnapshot()` and `setDirty(true)`.
**Fix:** route the edit through `commitChange()` (or `setDirty(true)` for
editor-only state); never mutate project data silently.

## The save icon never lights and the edit is quietly losable
**Cause:** a widget called `saveAll()`, which writes the project AND the history
file and clears the dirty flag.
**Fix:** call `commitChange()` per logical action. `saveAll()` is only for an
explicit save command or an action whose filesystem side effect the model must
match on disk (asset import).

## Undo silently drops a new field
**Cause:** the field was added to `SceneObject` but not to its `operator==`;
`History::push()` short-circuits on equality (`shadowMode` shipped in 1.62.0
this way, fixed 1.67.0).
**Fix:** add the field to `operator==` (and to `objectJson`/`readObjectsArray`)
in the same commit; `project::ensureObjectIds()` and equality are the two
places a new object field is easiest to forget.

## A generated file never refreshes after the first build
**Cause:** it was added to `templates::generate()` but not to
`refreshGenerated`'s explicit overwrite/write-once list, so only
`project::create` wrote it (the Remote Pad's `live_pad.gen.cpp` shipped the
devkit runtime into a release build).
**Fix:** add the path to the correct list in `refreshGenerated` in the same
commit as the generator; the `.gen.cpp` suffix alone is not a rule.

## Opening a tool instantly crashes on an AMD GPU
**Cause:** an upload site called `glTexImage2D(..., pixels)`; the AMD GL driver
faults inside `atio6axx.dll` (`0xc0000005`) with valid arguments (PROGRESS 101).
**Fix:** use `glUploadTexRgba(w, h, pixels)` (`gl_loader.h`), which allocates the
level empty then fills it with `glTexSubImage2D`.

## File index (from upstream `source-map.md`, one flat `src/`)
- Shell/UI: `main.cpp`; `app.cpp`/`app.hpp`/`app_internal.hpp`; `props_ui.cpp`, `flowgraph_ui.cpp`, `hud_ui.cpp`, `procui.cpp`, `cutscene_ui.cpp`, `credits_ui.cpp`, `menustyle_ui.cpp`, `mateditor_ui.cpp`, `devkit_ui.cpp`, `chat_ui.cpp`, `facts_ui.cpp`, `prefab_ui.cpp`, `particle_ui.cpp`, `batch_ui.cpp`, `atlas_ui.cpp`, `junction_ui.cpp`, `vehicle_ui.cpp`, `vu_ui.cpp`, `modelimport_ui.cpp`, `update_ui.cpp`, `blss_window.cpp`, `droneui.cpp`, `assetbrowser.cpp`, `save_assets.cpp`.
- Model/serialization: `project.cpp/.hpp`, `version.hpp`, `migrations.cpp/.hpp`, `history.hpp`, `json.cpp/.hpp`, `editorcfg.hpp`, `input.hpp`, `flowgraph.hpp`, `flownode.cpp`, `screenfx.cpp/.hpp`, `facts.cpp/.hpp`, `sequence.hpp`, `ambience.cpp/.hpp`, `grading.hpp`, `hudanim.hpp`, `theme.cpp/.hpp`, `logview.cpp/.hpp`, `uiscript.cpp/.hpp`, `viewport.cpp/.hpp`, `gl_loader.h/.cpp`.
- Codegen/bakes: `templates.cpp/.hpp`, `game_templates.inc`, `texbake.cpp/.hpp`, `pngquant.cpp/.hpp`, `texatlas.cpp/.hpp`, `stochtile.cpp/.hpp`, `tmdl.cpp/.hpp`, `meshlod.cpp/.hpp`, `meshstrip.cpp/.hpp`, `primmesh.cpp/.hpp`, `objparser.cpp/.hpp`, `menubake.cpp/.hpp`, `menulayout.cpp/.hpp`, `menustyle.cpp/.hpp`, `savebake.cpp/.hpp`, `staticbatch.cpp/.hpp`, `staticbatchdisk.cpp`, `particletex.cpp/.hpp`, `modelproxy.cpp/.hpp`, `treeimpostor.cpp`, `modelimpostor.cpp`, `impostorbake.hpp`, `impostorgpu.cpp/.hpp`, `decalproj.cpp/.hpp`, `aobake.cpp/.hpp`, `gibake.cpp/.hpp`, `gigpu.cpp/.hpp`, `bakegl.hpp`, `bakepar.hpp`, `bvh.cpp/.hpp`, `matbake.cpp/.hpp`, `modelao.cpp/.hpp`, `litbake.cpp/.hpp`, `shadowbake.cpp/.hpp`, `blobshadowbake.cpp/.hpp`, `occlusionbake.cpp/.hpp`, `reflscenery.cpp/.hpp`, `starfield.cpp/.hpp`, `skytex.cpp/.hpp`, `navmesh.cpp/.hpp`, `physhull.cpp/.hpp`, `placement.cpp/.hpp`, `scrollsim.cpp/.hpp`, `wavconvert.cpp/.hpp`.
- Subsystems: `procgraph.cpp/.hpp`, `procgen.cpp/.hpp`, `procbake.cpp/.hpp`, `procrt.cpp/.hpp`, `prefab.cpp/.hpp`, `roadgen.cpp/.hpp`, `vehbake.cpp/.hpp`, `vehiclesim.cpp/.hpp`, `vehcheck.cpp/.hpp`, `modelimport.cpp/.hpp`, `glbparser.cpp/.hpp`, `fbxparser.cpp/.hpp`, `animedit.cpp/.hpp`, `animmerge.cpp/.hpp`, `uvunwrap.cpp/.hpp`, `treegen.cpp/.hpp`, `camtake.cpp/.hpp`, `phonecam.cpp/.hpp`, `aigen.cpp/.hpp`, `aichat.cpp/.hpp`, `aisupport.cpp/.hpp`, `blss.cpp/.hpp`, `blsscorpus.cpp/.hpp`, `blssscene.cpp/.hpp`, `blss_ui.cpp/.hpp`, `dronegen.cpp/.hpp`, `audiopreview.cpp/.hpp`, `vuir`, `vuasm`, `vusim`, `vugen`, `vucap.cpp/.hpp`, `vushader.cpp/.hpp` + `vushader_sample.cpp`/`vushader_palette_sample.cpp`/`vushader_snap_sample.cpp`/`vushader_wobble_sample.cpp`/`vukernel_sample.cpp`, `vumain.cpp`, `runner.cpp/.hpp`, `pcsx2_config.cpp/.hpp`, `iso9660.cpp/.hpp`/`isoexport.cpp/.hpp`, `elfsym.cpp/.hpp`, `livelogic.cpp/.hpp`, `livedbg.cpp/.hpp`, `livetime.cpp/.hpp`, `livepad.cpp/.hpp`, `livereplay.cpp/.hpp`, `hardware_timeline.cpp/.hpp`, `devsession.cpp/.hpp`, `sessionlog.cpp/.hpp`, `session.cpp/.hpp`, `wire.cpp/.hpp`, `platform.cpp/.hpp`, `update.cpp/.hpp`.

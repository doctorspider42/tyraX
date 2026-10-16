# Source map (`src/`, one flat directory)

Read when you need to find which file owns something, or before adding a file. Line counts are approximate (`wc -l`, .cpp + .hpp). "Ref" names the reference file holding that file's rules. Every `src/*.cpp/.hpp/.h/.inc` is listed.

## Shell, UI translation units, model

| File | ~Lines | What it is | Ref |
|---|---|---|---|
| `main.cpp` | 5.1k | Entry point: GUI by default, plus every headless CLI verb (`--new`, `--build [--run\|--run-ps2] [--rebuild]`, `--resave`, `--refresh-gen`, `--migrate`, `--dump`, `--list-nodes`, `--dump-graph`, `--apply-graph`, `--ai-graph`, `--add-ai-support`, `--chat-prompt`, `--search-docs`, `--import-model`, `--bake-*`, `--bake-status`, `--gi-gpu-check`, `--blss-*`, `--batch-report`, `--atlas-report`, `--audit-release`, `--pad`, `--replay`, `--ui-script`, `--profile-frame`, `--capture-frame`, `--debug-state`, `--vehicle-check`, `--vu-*`, `--dump-vucap`...). docs/ai-tools.md | tyra-testing |
| `app.cpp` / `app.hpp` | 17.2k / 3.9k | ImGui shell; `app.hpp` declares the WHOLE `App` class | editor-ui-and-conventions |
| `app_internal.hpp` | 300 | Helpers shared by >1 App TU | editor-ui-and-conventions |
| `props_ui.cpp` | 3.7k | Properties inspector (sections for solid objects) | editor-ui-and-conventions |
| `flowgraph_ui.cpp` | 2.3k | Flow Graph editor window | flow-graph-input-player |
| `hud_ui.cpp` | 5.8k | UI Editor, Loading Screen, Splash, Font Manager, icons, Input Map, Animation Editor, GI/prelit/model-AO sections, Tree generator window | game-screens-menus-text |
| `procui.cpp` | 1.9k | Procedural window, preview, bake verbs, seed simulator | procedural-and-prefabs |
| `cutscene_ui.cpp` | 2.3k | Cutscene Director, phone link UI, take import | scene-objects-and-gameplay |
| `credits_ui.cpp` | 980 | Credits Editor | game-screens-menus-text |
| `menustyle_ui.cpp` | 1.5k | Menu Editor Style/Stylesheet tabs + preview | game-screens-menus-text |
| `mateditor_ui.cpp` | 3.9k | Material Editor (.mtl, paint layers + own undo, map bake, UV validator, texture hot reload) | runtime-rendering-and-meshes |
| `devkit_ui.cpp` | 3.8k | Devkit host side: channel ticks, Debugger, time machine, error modal, Remote Pad, ui-script tick | devkit-runner-and-network |
| `chat_ui.cpp` | 1.8k | AI Assistant window + Edit/Command tools | ai-features |
| `facts_ui.cpp` | 1.5k | World Facts window | flow-graph-input-player |
| `prefab_ui.cpp` | 550 | Tools > Prefabs | procedural-and-prefabs |
| `particle_ui.cpp` | 480 | Tools > Particle Editor | runtime-rendering-and-meshes |
| `batch_ui.cpp` | 460 | Tools > Static Batches panel | runtime-rendering-and-meshes |
| `atlas_ui.cpp` | 400 | Tools > Texture Atlas | runtime-rendering-and-meshes |
| `junction_ui.cpp` | 370 | Road junction overrides UI | vehicles-and-roads |
| `vehicle_ui.cpp` | 1.7k | Tools > Vehicle Editor | vehicles-and-roads |
| `vu_ui.cpp` | 1.3k | Tools > VU Programs | vu-framework |
| `modelimport_ui.cpp` | 940 | Import model window | assets-import-animation |
| `update_ui.cpp` | 260 | Update check UI (`updateTick`) | build-platform-release |
| `blss_window.cpp` | 4.5k | Neural Upscaler window + shared BLSS settings block | blss |
| `droneui.cpp` | 1.9k | Drone Generator window | audio |
| `assetbrowser.cpp` | 2.2k | Asset Browser, `retargetAssetPath`, `rebuildAssetUsage` | assets-import-animation |
| `save_assets.cpp` | 540 | Built-in save-menu HUD sprites (embedded PNGs) | game-screens-menus-text |
| `project.cpp` / `project.hpp` | 9.8k / 5.1k | Data model, (de)serialization, sections, `refreshGenerated` | project-model-and-serialization |
| `version.hpp` | 6.1k | Editor semver macros + `kFormatVersion` (94) + `kMinFormatVersion`; mostly the per-version change log | project-model-and-serialization |
| `migrations.cpp/.hpp` | 260 | Format migration registry, backup | project-model-and-serialization |
| `history.hpp` | 90 | Undo/redo snapshot stack + distance from the saved state (`stepsFromSaved`) | project-model-and-serialization |
| `json.cpp/.hpp` | 330 | Small JSON parser/writer | project-model-and-serialization |
| `editorcfg.hpp` | 30 | editor.ini path / recent projects for non-GUI code | editor-ui-and-conventions |
| `input.hpp` | 270 | Input actions/bindings/presets, `inputCodes()` (append-only) | flow-graph-input-player |
| `flowgraph.hpp` | 2.75k | Flow-graph model + node registry + `reverbPresets()` | flow-graph-input-player |
| `flownode.cpp` | 260 | Custom `.flownode` loader | codegen-and-generated-files |
| `screenfx.cpp/.hpp` | 270 | Custom `.screenfx` loader | codegen-and-generated-files |
| `facts.cpp/.hpp` | 1.3k | World Facts model/evaluator/validator | flow-graph-input-player |
| `sequence.hpp` | 430 | Cutscene model + shared camera math | scene-objects-and-gameplay |
| `ambience.cpp/.hpp` | 660 | Ambience presets, day/night `evaluate` | lighting-and-bakes |
| `ambience_simple_ui.cpp` | 900 | Ambience Editor > Sky & time, Simple view: looks, time strip, plain-word rows (reads/writes the preset's own fields) | lighting-and-bakes |
| `grading.hpp` | 80 | Colour grading presets -> GS integers | lighting-and-bakes |
| `hudanim.hpp` | 130 | Host twin of HUD animation math | game-screens-menus-text |
| `theme.cpp/.hpp` | 430 | Editor themes / semantic colours | editor-ui-and-conventions |
| `logview.cpp/.hpp` | 290 | Log severity classification | editor-ui-and-conventions |
| `shell_ui.cpp` | 1000 | Window frame: the logo menu (`drawLogoMenu`, opens `drawMainMenus`), top-bar breadcrumb + unsaved count + workspaces + search + run controls (`drawToolbar`), merged-title-bar controls (`drawTitleBarControls`), status bar (`drawStatusBar`), `selectScene` | editor-ui-and-conventions |
| `palette_ui.cpp` | 450 | Command palette (Ctrl/Cmd+K; Ctrl+Enter hands the query to the AI Assistant): commands, objects, scenes, windows (`windowTitle` table over `chatWindowKeys`), layouts, docs (`aichat::docIndex`) | editor-ui-and-conventions |
| `toolkit.cpp/.hpp` | 330 | Tool-window kit (`ui::`): PrimaryButton/DangerButton/GhostButton, VSplitter/HSplitter, BeginCard, Caption, ListRow, ThumbTile+TileFlow, EmptyState, ProgressInline, StatusLine, Badge, UsedBy, Segmented | editor-ui-and-conventions |
| `widgets.cpp/.hpp` | 130 | `ui::` shared widgets (AxisDrag3, Keycap, ShortcutChord) + heading/mono faces | editor-ui-and-conventions |
| `uiscript.cpp/.hpp` | 740 | `--ui-script` item registry + script | editor-ui-and-conventions |
| `viewport.cpp/.hpp` | 8.8k / 1.6k | Offscreen GL 3.3 preview | viewport |
| `gl_loader.h/.cpp` | 215 | Hand-rolled GL loader, `glUploadTexRgba` | viewport |

## Codegen and bakes run at build

| File | ~Lines | What it is | Ref |
|---|---|---|---|
| `templates.cpp/.hpp` | 34.9k / 230 | All code generation, model/anim bakes | codegen-and-generated-files |
| `game_templates.inc` | 27.6k | Game runtime templates (`TPL_GAME_*`) | codegen-and-generated-files |
| `texbake.cpp/.hpp` | 1.0k | `res/` -> `.res-baked/` texture bake | runtime-rendering-and-meshes |
| `pngquant.cpp/.hpp` | 610 | Palette quantization | runtime-rendering-and-meshes |
| `texatlas.cpp/.hpp` | 670 | Texture atlas plan | runtime-rendering-and-meshes |
| `stochtile.cpp/.hpp` | 200 | Stochastic terrain supertiles | runtime-rendering-and-meshes |
| `tmdl.cpp/.hpp` | 350 | Shipped static-model format | runtime-rendering-and-meshes |
| `meshlod.cpp/.hpp` | 460 | Bake-time decimation, shadow proxy | runtime-rendering-and-meshes |
| `meshstrip.cpp/.hpp` | 420 | Triangle stripification | runtime-rendering-and-meshes |
| `primmesh.cpp/.hpp` | 200 | Unit-primitive tessellation | runtime-rendering-and-meshes |
| `objparser.cpp/.hpp` | 560 | .obj/.mtl reader | runtime-rendering-and-meshes |
| `menubake.cpp/.hpp` | 3.2k | Menu panels, text, credits strip, icon/glyph atlases | game-screens-menus-text |
| `menulayout.cpp/.hpp` | 700 | Menu geometry engine | game-screens-menus-text |
| `menustyle.cpp/.hpp` | 2.1k | Menu stylesheets | game-screens-menus-text |
| `savebake.cpp/.hpp` | 1.07k | Memory-card icon/save bakes | game-screens-menus-text |
| `staticbatch.cpp/.hpp`, `staticbatchdisk.cpp` | 830 + 170 | Host twin of static batching | runtime-rendering-and-meshes |
| `particletex.cpp/.hpp` | 330 | Procedural particle textures | runtime-rendering-and-meshes |
| `modelproxy.cpp/.hpp` | 125 | Footprint-prism visual proxy | runtime-rendering-and-meshes |
| `treeimpostor.cpp`, `modelimpostor.cpp`, `impostorbake.hpp`, `impostorgpu.cpp/.hpp` | 190 + 60 + 30 + 220 | Impostor capture (CPU rasterizer, OBJ adapter, optional GPU) | runtime-rendering-and-meshes |
| `decalproj.cpp/.hpp` | 580 | Projected-decal geometry | lighting-and-bakes |
| `aobake.cpp/.hpp` | 2.5k | AO, lightmap atlas, analytic shapes | lighting-and-bakes |
| `gibake.cpp/.hpp` | 1.85k | Global illumination + probes | lighting-and-bakes |
| `gigpu.cpp/.hpp` | 775 | GI gather on GL 4.3 compute | lighting-and-bakes |
| `bakegl.hpp` | 25 | Hidden GL context for GPU bakers | lighting-and-bakes |
| `bakepar.hpp` | 70 | Bakers' parallel-for | lighting-and-bakes |
| `bvh.cpp/.hpp` | 280 | Shared BVH | lighting-and-bakes |
| `matbake.cpp/.hpp` | 1.3k | UV-space map baker | lighting-and-bakes |
| `modelao.cpp/.hpp` | 710 | Automatic per-asset model AO | lighting-and-bakes |
| `litbake.cpp/.hpp` | 970 | Pre-lit models | lighting-and-bakes |
| `shadowbake.cpp/.hpp` | 1.7k | Baked shadow decals | lighting-and-bakes |
| `blobshadowbake.cpp/.hpp` | 175 | Cheap-shadow masks | lighting-and-bakes |
| `occlusionbake.cpp/.hpp` | 255 | Software occlusion boxes | lighting-and-bakes |
| `reflscenery.cpp/.hpp` | 445 | Static scenery/ground in the reflection probe | lighting-and-bakes |
| `starfield.cpp/.hpp` | 210 | Procedural night sky | lighting-and-bakes |
| `skytex.cpp/.hpp` | 100 | Painted sky crop | lighting-and-bakes |
| `navmesh.cpp/.hpp` | 230 | NavMesh bake | flow-graph-input-player |
| `physhull.cpp/.hpp` | 295 | Rigid-body hulls | scene-objects-and-gameplay |
| `placement.cpp/.hpp` | 310 | Placement snapping, collision box twin | scene-objects-and-gameplay |
| `scrollsim.cpp/.hpp` | 580 | Endless-scroller belt math | scene-objects-and-gameplay |
| `wavconvert.cpp/.hpp` | 315 | WAV inspect/convert, sfx bake | audio |

## Subsystems

| File | ~Lines | What it is | Ref |
|---|---|---|---|
| `procgraph.cpp/.hpp` | 1.3k | Procedural graph model + node registry | procedural-and-prefabs |
| `procgen.cpp/.hpp` | 2.6k | Procedural evaluator | procedural-and-prefabs |
| `procbake.cpp/.hpp` | 590 | Procedural bake to chunks | procedural-and-prefabs |
| `procrt.cpp/.hpp` | 1.4k | Runtime procedural compiler | procedural-and-prefabs |
| `prefab.cpp/.hpp` | 700 | Prefab verbs, `memberMerges` | procedural-and-prefabs |
| `roadgen.cpp/.hpp` | 1.6k | Road tessellator, crossings | vehicles-and-roads |
| `vehbake.cpp/.hpp` | 2.6k | Vehicle import bake | vehicles-and-roads |
| `vehiclesim.cpp/.hpp` | 2.2k | Wheel detection + drive model | vehicles-and-roads |
| `vehcheck.cpp/.hpp` | 1.3k | `--vehicle-check` property tests | vehicles-and-roads |
| `modelimport.cpp/.hpp` | 1.3k | The model importer | assets-import-animation |
| `glbparser.cpp/.hpp` | 1.8k | glTF importer + morph baker | assets-import-animation |
| `fbxparser.cpp/.hpp` | 1.2k | FBX importer (ufbx), `animimport::` dispatch | assets-import-animation |
| `animedit.cpp/.hpp` | 460 | Non-destructive clip edits | assets-import-animation |
| `animmerge.cpp/.hpp` | 2.7k | Animation import, bone mapping, retargeting | assets-import-animation |
| `uvunwrap.cpp/.hpp` | 560 | Automatic UV unwrap | assets-import-animation |
| `treegen.cpp/.hpp` | 960 | Procedural tree generator | assets-import-animation |
| `camtake.cpp/.hpp` | 720 | Phone camera takes -> keys | scene-objects-and-gameplay |
| `phonecam.cpp/.hpp` | 910 | Live phone camera link | scene-objects-and-gameplay |
| `aigen.cpp/.hpp` | 1.3k | AI flow-graph generation, backends | ai-features |
| `aichat.cpp/.hpp` | 2.5k | AI Assistant brain | ai-features |
| `aisupport.cpp/.hpp` | 160 | AI support file installer | ai-features |
| `blss.cpp/.hpp`, `blsscorpus.cpp/.hpp`, `blssscene.cpp/.hpp` | 5.4k + 3.3k + 1.3k | Neural upscaler host half | blss |
| `blss_ui.cpp/.hpp` | 2.3k | BLSS window parsers/arithmetic (no ImGui) | blss |
| `dronegen.cpp/.hpp` | 3.1k | Drone music synthesizer | audio |
| `audiopreview.cpp/.hpp` | 230 | Editor audio output (miniaudio) | audio |
| `vuir`, `vuasm`, `vusim`, `vugen` (.cpp/.hpp) | 7.8k | VU framework | vu-framework |
| `vucap.cpp/.hpp` | 830 | VU1 packet capture decoder | vu-framework |
| `vushader.cpp/.hpp`, `vushader_sample.cpp`, `vushader_palette_sample.cpp`, `vushader_snap_sample.cpp`, `vushader_wobble_sample.cpp`, `vukernel_sample.cpp` | 480 + 450 | Project VU authoring layer + samples | vu-framework |
| `vumain.cpp` | 760 | Generator for project VU scripts (runs in the build container) | vu-framework |
| `runner.cpp/.hpp` | 2.1k | Build/run pipeline | devkit-runner-and-network |
| `pcsx2_config.cpp/.hpp` | 225 | PCSX2.ini discovery + HostFs/USB setup | devkit-runner-and-network |
| `iso9660.cpp/.hpp`, `isoexport.cpp/.hpp` | 440 + 380 | ISO writer + disc layout | devkit-runner-and-network |
| `elfsym.cpp/.hpp` | 440 | ELF reader + release audit | devkit-runner-and-network |
| `livelogic.cpp/.hpp` | 1.08k | Live Logic compiler/IR | devkit-runner-and-network |
| `livedbg.cpp/.hpp` | 725 | Live Debugger formats | devkit-runner-and-network |
| `livetime.cpp/.hpp` | 275 | Time machine | devkit-runner-and-network |
| `livepad.cpp/.hpp` | 455 | Remote Pad | devkit-runner-and-network |
| `livereplay.cpp/.hpp` | 730 | Input recorder | devkit-runner-and-network |
| `hardware_timeline.cpp/.hpp` | 180 | Native hardware timeline view | devkit-runner-and-network |
| `devsession.cpp/.hpp` | 255 | Which project each editor is debugging | devkit-runner-and-network |
| `sessionlog.cpp/.hpp` | 175 | Console session log files | devkit-runner-and-network |
| `session.cpp/.hpp` | 1.55k | Live collaboration session | devkit-runner-and-network |
| `wire.cpp/.hpp` | 970 | Sockets, frame codec, TCP + WebSocket transports, file hashing | devkit-runner-and-network |
| `platform.cpp/.hpp` | 1.66k | All OS differences | build-platform-release |
| `platform_mac.mm` | 170 | macOS half of the merged title bar (NSWindow, the only Objective-C++) | build-platform-release |
| `update.cpp/.hpp` | 520 | Update check | build-platform-release |

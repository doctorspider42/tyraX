---
name: tyra-editor-dev
description: >
  Architecture map and change-making guide for the TyraX codebase — a C++20
  ImGui/GLFW/OpenGL cross-platform (Windows + Linux) editor that authors 3D
  scenes and flow graphs, then
  generates complete Tyra PS2 game projects (built in Docker, run in PCSX2).
  Use this skill BEFORE making ANY change to editor code in src/ — new features,
  panels, scene object types, flow-graph nodes, preferences, project.json fields,
  code generation — and whenever you need to understand how the editor, the data
  model, codegen and the generated game fit together. Also consult it when asked
  "how does X work" about this repo, even for seemingly small one-file edits:
  most features cut across the model → serialization → codegen → UI → viewport
  chain, and this skill tells you which files each kind of change must touch.
---

# TyraX development

> **A note on `PROGRESS 123` citations.** They appear throughout this file and
> point at numbered entries of `PROGRESS.md`, which was retired at ~15 800
> lines. They are still exact pointers — the file lives on in git history, and
> `docs/backlog.md` has the two-line recipe for reading it. New work records
> itself in its commit message and PR body instead.

## What this project is

An editor for the [Tyra](https://github.com/h4570/tyra) PlayStation 2 game engine.
The editor itself is a Windows desktop app (C++20, Dear ImGui docking + GLFW +
OpenGL 3.3). It edits a **data model** (scenes, objects, terrain heightmaps, flow
graphs, preferences) stored in a `<name>.tyra` manifest plus one `objects/<id>.json`
file per scene object (merge-friendly split), and on every build
**generates a complete PS2 game project** (C++ sources, Makefile, docker-compose.yml).
The game is compiled inside a Docker container (`h4570/tyra` image, PS2DEV
`mips64r5900el-ps2-elf-g++` toolchain) and launched in the PCSX2 emulator.

The one-line pipeline to keep in your head:

```
ImGui UI (app.cpp) ──edits──> Project model (project.hpp)
        │ commitChange()              │ save()/load()
        ▼                             ▼
  undo history (history.hpp)     <name>.tyra (manifest) + objects/<id>.json + <name>.history + terrain-*.heights
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
| `main.cpp` | ~560 | Entry point. GUI by default; headless `--new`, `--build [--run\|--run-ps2]`, `--resave`, `--refresh-gen`, and the AI-agent commands `--dump` / `--list-nodes` / `--dump-graph` / `--apply-graph` / `--ai-graph` / `--add-ai-support` (docs/ai-tools.md). |
| `aigen.cpp/.hpp` | ~700 | **AI flow-graph generation** (docs/ai-flow-graph.md). `systemPrompt()` builds the instruction text live from `flowAllNodeTypes()` + the project's referencable names (new node types self-document: pins/params from the registry flags, semantics from `FlowNodeType::desc` — every node entry carries its description, which is also the editor's add-menu/node-hover tooltip). `Generator` runs the backend (claude/codex/copilot CLI via stdin temp file, OpenAI via a curl config file) - each invoked as ONE completion with its own tools, session files and project instruction files off, from a neutral CWD, because the prompt here is the whole contract; the Claude and Codex CLIs are additionally asked for their JSON/JSONL event output so `Usage` carries the model's OWN token counts (and Claude's cost) instead of an estimate, with the raw text as the fallback when an envelope changes shape. **The Codex reply arrives in a FILE** (`-o`, because its stdout is progress chatter), so it must be read before `cleanup()` deletes the request's temp files - that ordering is a bug waiting for any future backend that answers into a file on a worker thread inside a kill-on-close Job Object (cancel kills the tree). `parseGraph()` extracts/validates the reply with the same link pin rules the editor prunes by + auto-layout; `appendGraph()` merges. Backend config = `aigen::Config` on `EditorConfig` (editor.ini `aiBackend`/`aiModel`/`aiThinking`); the modal is `App::drawAiGenerateModal`. |
| `aichat.cpp/.hpp` | ~700 | **The AI Assistant's brain** (docs/ai-chat.md) - the in-editor chat that answers questions about the editor AND performs operations in the project. Host-only (no ImGui, no GL, no `App`), so the prompt, the parser and every read tool run from a harness. Three single-source ideas hold it up. (1) **The skills are `docs/*.md` themselves**: every page is embedded by `cmake/embed_docs.cmake` into `docs_gen.hpp` and the prompt carries only an INDEX derived from the markdown (H1 + first sentence, `describeDoc`), which the model expands with the `read_doc` tool - so the assistant's knowledge IS the repo's documentation (the standing docs-in-the-same-commit rule keeps it current for nothing) and a 21 KB prompt does the work of 780 KB. A new page joins the index by existing; the only constraint is the `)TYRAXDOC"` raw-string delimiter. (2) **the write surface is the project's OWN serialization, not a second model**: `get_section`/`set_section` hand the assistant `project::sectionJson`/`applySectionJson` - the collaboration LWW unit - so all 18 sections became readable and writable without a tool (or a property table) per collection, and a section added to the model tomorrow is covered today; `set_object_json` does the same for one object through `objectJson`/`parseObject`. The price is that a blob is TOTAL, so a model sending back only what it was thinking about deletes the rest - hence `shrinkReport`, which refuses a write that makes any top-level ARRAY shorter unless it was confirmed. That guard is pure JSON and knows nothing about any section, which is exactly why it keeps working. (3) **`tools()` is the ONE tool table** - the prompt catalog, `validate()` and both executors read it, so documenting a tool for the model and implementing it are the same edit; `ToolKind` is what splits them, Read tools living here (`runReadTool`, a pure function of `const Project&`) and Edit/Command tools in chat_ui.cpp. `objectProps()` is the same trick for `set_object`'s property list. (4) **the search is tiered, and the middle tier is the one that earns its keep** (`searchDocs`): lines holding every term, PLUS - because prose rarely puts a whole question on one line - the lines of the PAGES that hold every term somewhere (capped per page so a common word cannot flood), and only if both come up empty, any line holding any term, labelled loose. A page whose name or title matches gets a large ranking bonus; without it "VRAM budget" answered with the one line in the docs index that happened to hold both words while gs-vram.md - the entire page on the subject - was never reached. (5) **`parseReply` never fails**: a reply that is not the `{say, calls}` envelope is taken as prose, because a backend answering in plain English is being useful - the envelope only has to be honoured to ACT. **Context accounting and compaction** are here as well, and carry the trap that cost the most in this batch: `overBudget` must be decided on the UNTRIMMED conversation, because `transcript()` trims to just UNDER its budget by construction - comparing what it returns against the budget is a test that can never fire, and compaction silently never happened. `contextStats` therefore renders the transcript twice (sent vs full) and `transcript` reports how many messages it dropped. The second trap is in `compactableCount`: a fold has to end at the start of a TURN, and the first version walked DOWN from the tail looking for a User message - in a conversation that is one enormous turn (six documentation reads) it walked to index 0 and answered "nothing to compact" exactly when folding was most needed. It picks the latest User index that leaves a tail, and falls back to the last one. Note the deliberate asymmetry: the CURRENT turn is never trimmed and never compacted, whatever it costs - a tool result the model just asked for is the most valuable text in the request. The **chat store** lives here too (`chatDir`/`saveChat`/`loadChat`/`listChats`/`pruneChats`): one JSON file per conversation under `<configDir>/chats/<projectId>/`, keyed by the project's stable id the way the session remote-cache keys its content - machine-global on purpose, because a conversation is the person's and putting it in the `.tyra` would send it through git and the collaboration wire. Two decisions worth keeping: the file carries **no timestamp** (its mtime is what `chatAge` renders, so there is no clock, timezone or format to get wrong - the `--debug-state` precedent), and it is written temp-file-then-rename because it is saved after EVERY turn and a half-written file is one `listChats` would skip forever. It stores a tool call's arguments as VALUES, which is the only reason `json::write` exists - those are the one piece of JSON in this editor nobody hand-builds, because the model authored it. Also `projectSummaryJson`, which is what `--dump` prints (one answer to "describe this project to a model"), and the value coercions (`numberOf`/`boolOf`/`stringOf`) - shared with the property executor after a model's `detail: "4"` was read there as "no value" and silently kept the old number. |
| `chat_ui.cpp` | ~600 | The AI Assistant **window and its acting half** - App:: methods declared in app.hpp, own TU (the assetbrowser.cpp precedent). Owns the step loop (`aiChatTick`, called every frame from `drawUI`: consume a reply, run its tool calls, hand the results back, up to `kChatMaxSteps` rounds per user turn) and every Edit/Command tool, because those need `project_`, `commitChange()`, the selection and the window flags. Rules: one tool call is ONE undo step and ends in `commitChange()`, never `saveAll()` (the editor does not autosave and the chat must not decide otherwise - `save_project` is the only writer, and only when asked); an edit goes through the editor's own verb where one exists (`addObject(type, false)` / `addModelObject(..., false)` for the placement snap and the naming, `deleteSelectedObjects` because it takes a procedural volume's baked chunks with it, the Project panel's own scene-switch sequence); and a tool that cannot do what was asked REPORTS WHY with the names that would have worked (`aichat::noSuchObject`) - the loop's next step is usually the fix. **A `std::filesystem` file time is NOT a system-clock time**, and that cost a full afternoon here: `file_clock`'s epoch is implementation-defined and on this libstdc++ it sits in the FUTURE, so every real file's `time_since_epoch()` is a large NEGATIVE number. The game-is-up wait used 0 for "the file is not there yet", which therefore compared as NEWER than anything the running game could write - the wait timed out every time while the game was demonstrably running, and the code read as obviously correct. Use `LLONG_MIN` (or an optional) for absent, never 0, anywhere file times are compared. **`build_game` PARKS the turn**, and that is the only reason the assistant can check its own work: `runChatTool` starts the Runner and sets `chatBuildWaiting_`, the tool loop then does NOT send the next request, and a branch at the top of `aiChatTick` polls the Runner every frame and appends the outcome (plus the tail of `logOut_.text` on failure) to the tool result that is already in the transcript before resuming. Any future long-running tool wants that same shape - a synchronous result string cannot wait, and blocking the UI thread is not an option. `refresh_generated` is the cheap half of the same idea and needs no gate at all, and `press_pad` is the third user of the shape - it hands a script to `livepad::parseScript` and the new `padScriptTick`, which is why `remotePadTick` now drives when a SCRIPT is running and not only when the panel is open (the state is cleared when it ends - a script that left a direction held is indistinguishable from a stuck pad). A `run` build parks TWICE: once for the build, then again until `bin/livedbg.bin` appears - deliberately that file and not `bin/log.txt`, whose first lines are the engine initialising, so a wait that ended there handed over while the game was still loading and every button went nowhere. The **cost readout** distinguishes two kinds of number and must keep doing so: `ctx` is an ESTIMATE of a request not yet sent (4 bytes/token), while the session totals are `aigen::Usage` - what the backend itself reported - and a backend that reports nothing (the Copilot CLI) shows "reports no usage" rather than an invented figure. Compaction is a request of its OWN (`chatCompacting_`), with its own small prompt, and its reply must never be appended as an assistant message; a failed one is non-fatal and the answer goes ahead uncompacted. History is `aiChatPersist()` after every turn (no Save button - the chat worth having tomorrow is the one nobody saved) plus the `##chathistory` popup; `chatFile_` is the file the CURRENT conversation owns, so later turns overwrite it instead of multiplying files, and both `aiChatReset` and `aiChatOpen` persist before they replace what is on screen. `chatAllowEdits_` (editor.ini) is the read-only switch: with it off every non-Read tool is refused and the model is told so. A request in flight when a project closes is harmless because every tool checks `hasProject_` - the tick is the only place chat data meets `project_`. |
| `aisupport.cpp/.hpp` | ~140 | Installs **AI support** files into projects (docs/ai-support.md): content authored in `ai-support/` (markdown, single source of truth), embedded at build by `cmake/embed_ai_support.cmake` into `ai_support_gen.hpp`, `{TYRAX_EXE}` substituted at install, rewrite gated by a "Generated by TyraX" marker in the file head (below SKILL.md frontmatter). Hooked into the New Project modal, Project Preferences, and `--add-ai-support`. **Codex is a second DESTINATION for the Claude files, not a second copy** - `.agents/skills/` + `AGENTS.md` with the self-references rewritten, exactly like this repo's own mirrored skills; a duplicate under `ai-support/` would drift the day one side was edited. The marker is searched in the first 4 KB, not 512 B: a skill's `description:` is what makes an assistant reach for it and can run long, and at 512 a new skill whose marker landed at byte 829 read as user-owned the moment it was installed and silently stopped refreshing. **When the generated-project format, flow-graph model, menu stylesheets or CLI change, update `ai-support/` in the same commit.** |
| `app.cpp/.hpp` | ~11700 (.cpp) | The ImGui **shell**: `run`/`drawUI`, menu bar + toolbar, the Viewport window (gizmo, sculpt, rubber-band, overlays), window layouts, undo/redo + clipboard + placement, the Project/Scene/Layers/Assets/Music/Sounds/Save-data/Scripts panels, asset import, sessions, the Terrain/Menus/Grading/Ambience/Disc-Layout windows, every Preferences dialog, and the wiring viewport ↔ project ↔ runner. The **Output and Debug log panels** live here too and share ONE body (`logSetText`/`logRefresh`/`drawLogPanel` over a `LogView`, classification in logview.cpp): a new log surface calls those three rather than growing a second answer to "how is a log drawn". Two traps that shape already paid for - `LogView::visible` holds INDICES into the text `LogView::text` owns, so anything that replaces the text mid-frame (Clear log, the source combo) must be followed by a `logRefresh` before the body draws (which is why `drawLogPanel` calls it itself), and the Debug window READS its file before submitting its widgets, because the buttons report on the classified lines - hence `debugReloadNow_` arming the next frame's read. **`app.hpp` still declares the whole `App` class** — the six files below only hold definitions that moved out of this TU, so a new member is declared here regardless of which file defines it. |
| `app_internal.hpp` | ~175 | Private header shared by app.cpp and the seven subsystem TUs: the `PickKind` pickers, `sanitizeAssetName`, `fileSizeOr0`, `readTextFileTail`, `prefHelp`, `walkSpeedDrag`. It exists ONLY for file-scope helpers more than one of those TUs calls — a helper used by one TU stays in that TU. Compiled seven times, so keep it small and its includes cheap. |
| `props_ui.cpp` | ~1900 | The object inspector: `drawPropertiesWindow`, `drawMultiProperties`, `drawLodOverrides`, the area/script pickers. |
| `flowgraph_ui.cpp` | ~1900 | The Flow Graph editor window (imnodes): add menu, pins, links, params, the debugger overlay. Two things a change here has to respect. **`flowNodeDoc()` is the ONE tooltip renderer** (title, `desc`, then a line per parameter and per exec pin), used by both the add-menu tooltip and the node hover - the procui.cpp `procNodeDoc` precedent, and the two editors must not grow two answers to "what does hovering a node tell me". And **`paramTip()` must be called AFTER every `IsItem*` query on the widget it documents**: a tooltip is a window and ImGui's "last item" is context-global, so a `paramTip` placed before the `IsItemDeactivatedAfterEdit()` that commits an edit silently stops the edit from being saved the moment the cursor rests on it. A drawn param tip also suppresses the node-level tooltip that frame (`paramTipShown`) - the cursor is inside the node either way, and two ImGui tooltips in one frame are drawn on top of each other. |
| `hud_ui.cpp` | ~3250 | The 2D-authoring windows: UI Editor, Loading Screen, Splash, Font Manager, button icons, Input Map, Animation Editor, plus the GI-bake and Tree-generator tool windows. |
| `procui.cpp` | ~1060 | The Procedural window (Tools > Procedural, docs/procedural-generation.md): the scatter-graph canvas (its OWN imnodes editor context - `procEditorCtx_`, never `EditorContextSet(nullptr)`), the live budget readout, per-instance overrides, `addScatterVolume`, `updateProcPreview` and the bake verbs (`bakeProcVolume`/`bakeStaleProcVolumes`/`projectForBuild`), and the **seed simulator** for runtime volumes (`runProcSeedSweep` + `procSeedPreview_`, docs/procedural-runtime.md - N evaluations at N seeds, so the author sees the SPREAD a "new world every run" volume will hand players rather than the one draw they happened to author with; the simulated seed rides `procgen::Options::seedOverride` and is a way of LOOKING at the graph, never an edit - it must stay out of `bakeHash`). Two traps this file has already paid for: a node's context menu must remember its target in `procCtxNode_` and NOT in the hover tracker `procDescNode_` (that one is reset to -1 on every frame the cursor is not over a node, which an open popup is - the menu closed the frame after it opened and right-click read as dead); and the sweep uses a SCRATCH `procgen::Cache` per trial so it cannot evict the live preview's memo. **A point carries an asset OR a prefab**, and the prefab half has no mesh of its own - `updateProcPreview` expands each such instance through `prefab::instantiate` (the same function Insert into scene and the runtime spawner use, so the preview cannot invent a placement) into `ScatterPreview::prefabObjects`; a consumer that only looks at `Instance::asset` silently shows/counts NOTHING for a prefab-scattering graph, which is how the cube example previewed as empty ground at 0 triangles. App:: methods declared in app.hpp, own TU (the droneui.cpp precedent). |
| `cutscene_ui.cpp` | ~2180 | The Cutscene Director (dopesheet, camera shots), the phone-camera link UI and camera-take import. |
| `credits_ui.cpp` | ~740 | **Credits Editor** (Tools > Credits Editor, docs/credits.md) - App:: methods declared in app.hpp, own TU (the assetbrowser.cpp precedent). Rolls are project-wide data, so they are outside undo like Loading Screens - but they are still edited with `commitChange()` like everything else (see rule 1); the file used to call `saveAll()` per widget, which is why the save icon never lit for a roll. The load-bearing decision: the roll's BAKE is the single source of its look - `menubake::bakeCreditsStripRGBA` (in menubake.cpp, next to every other text bake) lays the blocks out and rasterizes them into a strip of pow2 PAGE textures, and the preview here uploads THOSE pixels and positions them with the generated player's own arithmetic. So there is no second layout implementation to drift, and "what scrolls in the editor is what scrolls on the console" is a property, not a promise. `creditsPreviewRefresh()` re-bakes when the roll changes, compared with `CreditsRoll::operator==` plus the fonts' TTF paths (a hand-built signature string forgets a field the day someone adds one). Pages instead of one sprite per line is a VRAM decision (`menubake::kCreditsMaxPages` = 16): the GS pins every texture it draws, so the window reports pages / duration / VRAM and says *content clipped* rather than silently cutting a roll. The layout is settings-over-preview with a **height splitter** (`creditsSplit_`, the matEdSplit_ idiom: InvisibleButton + `MouseDelta` + `saveGlobalConfig()` on release, persisted in editor.ini because how much room a preview deserves is a property of the monitor); the space a 512x448 preview leaves in a wide window is the **Jump to** list, which inverts the roll's own timing arithmetic ("when is this block centred") so a click scrubs to a block instead of hunting on the slider. |
| `menustyle.{hpp,cpp}` | ~1500 | **Menu stylesheets** (docs/menu-styles.md): the model, the CSS-shaped parser, the cascade and the registry (`loadForProject`, called by `project::load` BEFORE menus are read - the flownode/screenfx arrangement). No GL, no ImGui, no layout, no rasterization, so the whole language is exercisable from a harness. **`propSpecs()` is the ONE property table** - parser, writer, the Style tab's widgets and its tooltips all read it, so a new property is one row there plus one `case` in `applyDecl`. Three decisions to keep: **the built-in `defaults()` ARE the Classic look** (the literals that used to sit in menubake.cpp), which is what makes "no sheet" and "the classic sheet" the same thing instead of two code paths; the **`classic` built-in sheet is deliberately EMPTY of rules**, because a rule restating a default freezes a value the MENU is supposed to supply (it re-froze every project's `accent` and the pixel diff against the old baker caught it); and `write(parse(t))` must be STABLE, since the Style tab saves through it and an unstable writer silently rewrites people's files. `stage()`/`unstage()` are the preview's door into the registry - one lookup path, so the editor cannot show a different sheet from the one the build bakes. |
| `menulayout.{hpp,cpp}` | ~470 | **The menu layout engine** - the single place a menu's geometry is decided, read by three consumers that may never re-derive it: menubake (rasterizes the boxes), the Menu Editor (previews those pixels + prints the cost), and templates.cpp (bakes the numbers into `menu_data.gen.hpp`). `compute(menu, project)` resolves the sheet over `baseFor()` - **the menu's own accent/sizes/font/panel width are the BASE of the cascade**, which is the whole compatibility story - then lays out the vertical flow, the rows (uniform pitch: the runtime's cursor arithmetic is `row0Y + i * rowH`), the state/description/list atlases and the VRAM estimate. Host-only, no GL, no fonts (text placement inside a box is the baker's job), so it is harness-testable like aobake. `kMaxRows` is 32 - it was 8 because a panel had to hold its own rows, and a scrolling list lifted it by moving them into their own windowed texture. |
| `menustyle_ui.cpp` | ~900 | The Menu Editor's **Style tab**, its raw *Stylesheet* tab and the shared **preview** (App:: methods declared in app.hpp, own TU - the credits_ui.cpp precedent). Style edits get their OWN undo stack, because a stylesheet is not project data and `commitChange()` must not see it (the Material Editor made the same call). The widget for a property is chosen from its `Kind`, so a new property appears in the GUI as soon as it is in `propSpecs()` - plus one entry in this file's `propsFor()` list, which is the only thing that decides WHERE it shows up. Two things to preserve: the override CHECKBOX seeds its declaration from what the value currently resolves to (turning an override on must never move anything), and the preview stages the unsaved sheet through `menustyle::stage` rather than reading the file - the editor showing the file on disk while the widgets say otherwise is the failure the whole arrangement exists to avoid. |
| `mateditor_ui.cpp` | ~3800 | The Material Editor: `.mtl` load/save, paint-layer stack + its own undo, the raytraced map bake, the UV validator. |
| `devkit_ui.cpp` | ~2130 | The devkit host side: `liveLinkTick`, `liveLogicTick`, `livedbgTick`, `livetimeTick`, the Debugger window, the time-machine panel, the game-error modal. |
| `assetbrowser.cpp` | ~1500 | **Asset Browser** (Tools > Asset Browser, docs/asset-browser.md) - App:: methods declared in app.hpp, in their own TU (the save_assets.cpp precedent) because they are a self-contained subsystem. `res/` IS the asset database, so everything is a view over the file system (`scanAssetTree`, throttled while the window is open) plus the two things a file manager cannot do: **`rebuildAssetUsage`** - ONE pass over the model recording every stored asset path (the census the grid's unused-ring, the inspector's user list and the delete warnings all read; keyed off `modelEditSerial_`) - and **the sibling invariant**: a Wavefront reference (`mtllib`/`map_Kd`/`refl`) is a bare name resolved next to the file that named it and the PS2 cannot walk `..`, so `moveAssets` moves a transitively closed dependency group (`assetWavefrontDeps`), COPIES a dependency the files left behind still need, and REFUSES rather than half-applying a move that would break a reference; `renameAsset` (same folder = safe) rewrites the siblings instead (`rewriteWavefrontRef`) and carries the `.mtl` a model exclusively owns. A new file type becomes a first-class asset by joining `assetKindOf`/`assetKindName`, the filter-chip counts, `matchesFilter`, `kindColor` (append - the cases are numeric, so inserting shifts every colour), `activateAsset` and the inspector switch - the `.drone` audio project (the drone batch's 213) is the worked example. **`retargetAssetPath` is the single list of every field that stores an asset path** - a new such field must join it or renaming its file silently breaks it. Sidecars (`.uvs`, `.aov`, the Drone Generator's `.drone` patch, `<tex>.layers/`) travel with their asset; the baked `.tmdl` is deleted for the next build to redo. Host-only apart from `Viewport::assetThumb` (thumbnails: one render per asset into a dedicated FBO, copied into its own texture, budgeted a few per frame) - so the whole non-UI half is exercisable from a harness (PROGRESS 105). |
| `project.cpp/.hpp` | ~1050 | **Data model + JSON (de)serialization + generated-file refresh.** `Project`, `SceneData`, `SceneObject`, `TerrainConfig`, `ProjectSettings`. `save()`/`load()`: the `<name>.tyra` **manifest** (project-wide data + per-scene ordered object-id list + editor state + the named window layouts) plus one `objects/<id>.json` body per object — `save()` writes every live object then prunes orphaned files; `load()`→`readSceneObjects()` dispatches split (id strings) vs legacy inline (object bodies). Both are **recomposed from per-section writer/reader pairs** (`Section` enum + `sectionJson()`/`applySectionJson()` — one manifest key group as a standalone JSON blob; apply is total-replace-with-defaults, the collaboration LWW unit). **A new manifest key must join a `write*Section`/`read*Section` pair** (or the scene table / editor tail) or it reaches the file but never the collaboration wire. **`Section` is looped by INDEX (`for s < kSectionCount`), so the count must equal the enum size or the LAST section silently stops being written** - with 17 sections and a count of 16 every save dropped `"prefabs"` outright, no error, and it survived several branches because nothing else reads that section. It is now `Section::Count` + a `static_assert`, i.e. maintained by the compiler; a new section still needs the number in the assert bumped deliberately, which is the point. `objectJson()`/`parseObject()` (public): one object ⇄ wire string. `manifestFiles()`: in-memory byte images of .tyra + objects/*.json + heights from the LIVE model. `Project::projectId` + `ensureProjectId()` (stable 16-hex project identity — the remote-cache key). `ensureObjectIds()` (stamps stable ids), `create()`, `seedBuiltinLayouts()` (the Default/Director/Material `WindowLayout` set, also used to migrate a legacy single `"layout"` dump), `saveHeights()/loadHeights()`, `saveHistory()/loadHistory()` (`<name>.history` undo stack — stays monolithic/inline, gitignored), `refreshGenerated()`. **Window layouts** are `std::vector<WindowLayout> windowLayouts` + `activeLayout`; a `WindowLayout` is `{name, ini, recipe, openWindows}` where an empty `ini` + a `LayoutRecipe` id is (re)built by `App::buildLayoutRecipe` (DockBuilder) the first time it's shown. The built-in set is Default / Director / Material / Debugger / Procedural /
**Menu Designer** (the Menu Editor + the standalone `Menu Preview` window).
**A new built-in layout is FOUR places**: the `LayoutRecipe` enum (project.hpp), a `case` in `buildLayoutRecipe` (app.cpp), a row in `seedBuiltinLayouts` - and, easiest to miss, the `hasRecipe` top-up in `project::load`, without which every EXISTING project keeps its saved layout list and never sees the new one. One thing a recipe cannot express: **which window is the ACTIVE tab of a dock
node it shares**. A fresh node activates whichever of its windows is submitted
first in the frame, and `drawUI`'s order is fixed - so the Menu Designer layout
gives the preview a node of its OWN rather than sharing it with Properties,
which sat on top whichever order the two were docked in. `pendingFocusWindow_`
only settles one window.

The Layout menu / switching / capture logic lives in app.cpp (`switchLayout`/`applyActiveLayout`/`captureActiveLayout`/`buildLayoutRecipe`, applied at a frame boundary). `openWindows` holds string keys, written/read by name, so their order is cosmetic — but **a new optional window must be registered in BOTH `App::showFlagForKey` AND `kLayoutWindowKeys` (app.cpp)**: the array is what capture and apply iterate, so a flag missing from it can neither be saved into a layout nor closed when a layout that omits it is applied — it leaks across every switch while the rest reset deterministically (that was the Input Map's bug, PROGRESS 221). Editor state, not undo. |
| `templates.cpp/.hpp` | 3522 | **All code generation.** `templates::generate(Project)` returns `vector<File>` (relativePath + content). Scene tables, terrain game sources, flow-graph compilation, Makefile/compose, VS Code IntelliSense config (`.vscode/c_cpp_properties.json` always-overwritten; `.vscode/extensions.json` written-if-missing — recommends the `tools/vscode-tyrax` extension), and `THIRD-PARTY-NOTICES.txt` (written-if-missing — the attribution a shipped game carries; see LICENSE-EXCEPTION.md). |
| `input.hpp` | ~250 | **Configurable input model** (docs/input-bindings.md), header-only. `InputAction` (name + label + `Role` + rebindable), `InputBinding` (pad name / USB HID key / mouse button - all three may fire one action), `InputPreset`, `InputMap` (on `Project::input`; `Section::Input`). Plus the shared tables every layer agrees on: `kPadButtonNames` (Tyra::PadButtons order - the index codegen stores), `inputKeyNames()` (the offered HID keys), and **`inputCodes()`** - the dense rebind code space whose index 0 means "the preset's binding" and whose numbers land in players' memory-card saves, so it is **append-only**; `INPUT_CODES` in the generated game is its twin. `project::ensureInputActions` seeds/backfills the built-in roles with exactly the bindings that used to be hardcoded in controls.hpp. |
| `flowgraph.hpp` | 216 | Flow-graph data model: `FlowNode`, `FlowLink` (exec / object-id / position / bool / text / **number** link kinds, plus `toPin` — which exec input an exec link fires), `FlowGraph`, the built-in `flowNodeTypes()` registry, the retired-type migration table (`flowLegacyNodes`), and the project-scoped custom-node registry (`CustomFlowNode`, `customFlowNodes()`). Per-object graphs, stored inside each object's `objects/<id>.json` body. |
| `flownode.cpp` | 230 | Loads project-defined **custom flow nodes** from `<project>/flow-nodes/*.flownode` text files into the global `customFlowNodes()` registry (`flownode::loadForProject`). Called by `project::load` *before* graphs are parsed. Parses the manifest (title/category/params, `in`/`out` pins, `exec_out`, `call`) into a `FlowNodeType`; the node's behavior is an inline C++ snippet or a `call = fn` into `inc/scripts/flow_nodes.hpp`. Also scaffolds the starter file (`writeExample`). See `docs/custom-flow-nodes.md`. **When you add/change a header key or `{placeholder}` here, mirror it in the VS Code extension's `SPEC` table (`tools/vscode-tyrax/extension.js`) and the grammar** — that is what colours/validates `.flownode` files (see `docs/vscode-extension.md`). |
| `screenfx.cpp` / `.hpp` | ~230 | Loads project-defined **custom screen effects** from `<project>/screen-effects/*.screenfx` text files into the global `customScreenEffects()` registry (`screenfx::loadForProject`, called by `project::load` before placements are read). The full-screen-post-effect analogue of custom flow nodes: a manifest (title + up to four numeric params) plus a raw low-level GS-blit C++ body. A `Project` references one only by placement (`ScreenFxPlacement` in project.hpp: key + stack `layer` + `enabled` + params). Placed/reordered in the *UI Editor* screen stack (like bloom/grain), codegen'd to `src/gen/screen_fx.gen.cpp` (`screenFxSource`/`screenFxHeader` in templates.cpp), run via `RendererCore::applyCustomPostFx` at the effect's slot in the frame loop. Unknown-key placements are dropped on load. See `docs/custom-screen-effects.md`. (Header keys/`{pN}` placeholders are also mirrored in the VS Code extension's `SPEC` — `tools/vscode-tyrax/extension.js`; keep them in sync.) |
| `dronegen.cpp/.hpp` | ~1600 | **Ambient / drone music generator** (docs/drone-generator.md, Tools > Drone Generator). Host-only, no GL, no `Project` - the treegen/matbake pattern, so the whole synthesizer is exercisable from a 40-line harness (link `dronegen.cpp` alone, render presets, measure them). Output is an ORDINARY asset: a 16-bit PCM WAV in `res/audio/` plus a `.drone` patch sidecar, so nothing downstream (serialization, codegen, engine) knows the generator exists - do not grow a project field for a patch. Three things to respect when changing it: (1) **the audition IS the renderer** - `Synth::render` is the only DSP, `LiveSynth` wraps it for the audio thread and the offline `render()` runs the same loop then adds the mastering; a knob that sounds different in the file than in the preview means someone duplicated DSP. (2) **`Params` determinism** - every random stream derives from `seed` through `mix32`, so a re-render is bit-identical; a new random source must derive the same way. (3) **`visitParams` is the ONE field list** the `.drone` writer and reader both walk (the project section-writer trick) - a parameter added to `Params` but not there is silently never saved. Seamless looping ADDS the rendered tail over the head (that is what a looping player hears) instead of crossfading, but the fold MUST be windowed to zero at its end (unity then raised cosine) - adding a tail and stopping leaves a step one loopTail into the file, an audible tick measured at 0.37 vs a 0.11 p99 neighbour jump (PROGRESS 212); the reverb is an 8-line FDN because comb banks ring metallic at the 30-second tails this genre wants. **The timeline** rides the same single-source idea: `paramTable()` is built by walking `visitParams`, so every saveable scalar is automatable by byte offset, and `Params` carries fixed-capacity `AutoLane`s (no heap - it is copied into the audio thread). `applyAutomation` runs once per control block INSIDE the synth's own copy, so modulation still layers on top; tempo/format/mastering fields are excluded on purpose (`autoExcluded`) - a tempo lane would look like it worked and do nothing, because `barSeconds` is read once per render() call. |
| `audiopreview.cpp/.hpp` | ~120 | The editor's **only audio output path**: a miniaudio (`vendor/miniaudio`, in deps.sh + deps.ps1) playback device pulling interleaved stereo floats from a callback. The ONLY TU that includes miniaudio, and it trims it to the raw device API (no decoders/encoders/engine). Backends are dlopen'd (WASAPI / ALSA / Pulse / JACK), so there is **no new system package and no link-time dependency**; a machine with no sound card is an expected state - `start()` returns false and the caller stays an offline renderer. `stop()` joins the audio thread (`ma_device_uninit`), which is what makes it safe to destroy whatever the callback captured - App::run does this before any teardown. |
| `droneui.cpp` | ~900 | The Drone Generator window - App:: methods declared in app.hpp, own TU (the assetbrowser.cpp precedent). Owns the widget vocabulary the rest of the editor does not have: the **rotary `knob()`** (vertical drag, Shift fine, double-click resets, `curve` for frequency/time knobs, symmetric ranges auto-drawn bipolar), `knobInt`, the arc-envelope editor, the scope/analyzer/meters strip. **A knob's label IS its ImGui ID**, so two knobs with the same caption in one panel (a delay *Mix* and a reverb *Mix*) are an ID conflict that breaks hover/drag, not just a warning - suffix BOTH with `##scope` (`Mix##delay` / `Mix##reverb`); the label is displayed only up to `##`. The same rule bites any list-shaped picker: a `Selectable`'s LABEL is its id, so a combo that shows TWO lists (the procedural Pick Prefab picker lists prefabs and then scene objects to capture) collides the moment one name appears in both - which capturing guarantees, since capturing "box-1" makes a prefab called "box-1" while the object is still there. Give every entry an explicit `##<prefix><index>`; and note automated runs cannot catch this, because `--ui-script` has no way to open a `##`-labelled combo (it has no name to target). PROGRESS 210 has the two ways to check that and why ImGui's own conflict detector cannot be driven with a synthetic cursor. Also the timeline: the position bar, the lane editors, and **`AutoWriteHook`** - the reason arming Write automates all ~137 parameters without a single knob call site mentioning automation (a knob binds straight to a `Params` field, so the pointer it was handed IS the parameter's address; the hook turns that into a lane). The transport has two modes - Generate (free-running past the end) and Record (stops itself at the end, `Rec` arms keyframe writing); BOTH play from the playhead, and the end-of-piece stop is POLLED on the UI thread, never fired from the audio callback. **A seek must SETTLE, not just move the clock** (`Synth::setTime`, PROGRESS 216): envelopes/glide/LFO phase/tails at time T are the product of the history 0..T, so a bare jump lands in the opening fade-in - it snaps notes to the chord and to an attack-since-chord-start level, jumps the periodic LFOs analytically, and pre-rolls 1-3 s of discarded audio for the delay/reverb lines. Dragging passes `settle=false`; the release settles once. **ImGui popup trap burned here (PROGRESS 215): `OpenPopup`/`IsPopupOpen`/`BeginPopupModal` must use the SAME string, and `ImHashStr` only resets on `###` - a `"Title##id"` modal paired with `OpenPopup("id")` is opened but never drawn, and an open-but-undrawn modal swallows every click in the window.** Rules: every parameter edit ends in `dronePushParams()` (that is what makes playback live), the render runs on `droneRenderThread_` and is only read after `droneRenderDone_` (Runner idiom), `droneTickRender()` is what writes the WAV + `.drone` and calls `saveAll`, `droneHeadSec_` is the ONE playhead truth, and all pixel literals go through `scaled()`. |
| `treegen.cpp/.hpp` | ~800 | **Procedural low-poly tree generator** (docs/tree-generator.md), EZ-Tree-inspired. Host-only, no GL, no `Project` dependency - the stochtile/matbake pattern: pure functions over a `Params` struct, so it is the one part of the editor you can **exercise from a 40-line host harness** (link `treegen.cpp` + define `STB_IMAGE_WRITE_IMPLEMENTATION`, dump the triangle soup, measure or rasterize it - far faster than clicking the GUI, see PROGRESS 104). `Params::height` is the tree's SIZE: `thickness` and `leafSize` are fractions of it, so the slider scales the tree instead of thinning it - a new world-space parameter should follow that rule. `Params::crown` picks the growth rule: 0 spread (the branching recursion) or 1 conical, where the trunk keeps its leader and `conicalWhorls()` hangs rings of boughs off it with a length PROFILE along the trunk - a conifer is a different rule, not a tuning of the spread one. `generate()` builds a recursive branch skeleton into tapered tubes + leaf quads; `bakeBarkTexture`/`bakeLeafTexture` bake tileable 128² procedural textures (the leaf card uses **hard 0/255 alpha** with opaque colors dilated into the margin - the tRNS→CLUT path loses a soft gradient, and bilinear sampling would otherwise fringe); `writeAssets()` emits `.obj` + `.mtl` + the PNGs into `res/models/trees/`. **Fully deterministic in `Params`** - each branch derives its RNG stream from (parent seed, child index), so editing one slider adjusts the tree instead of reshuffling it; keep that property when adding parameters. The tree reaches the scene through the ordinary `addModelObject()`, so NOTHING downstream (serialization, codegen, runtime) knows trees exist - do not grow a scene-object type for this. UI = `App::drawTreeGeneratorWindow`/`rebuildTreePreview`/`addTreeToScene` (app.cpp) + `Viewport::renderTreePreview` on its **own** framebuffer (`treeFbo_`, NOT the Material Editor's `prevFbo_` - both tools can be open at once and size their previews independently). |
| `stochtile.cpp/.hpp` | ~130 | **Stochastic tiling / texture bombing** for terrain textures (`docs/terrain-painting.md`). Host-only, no GL. `generate()` bakes a source tile into one larger, still-tileable "supertile" (≤512²) by scattering randomly rotated/flipped/offset feathered patches on the torus - breaks the tiled-grid repetition with zero runtime cost. Single source of truth: texbake writes it into `.res-baked/stoch` (regenerated wholesale, exempt from the vanished-source sweep), the viewport uploads the same pixels, codegen points the terrain texture table + tiling at it via `bakedBinPath`/`factorFor`. Deterministic (seeded from the source path). Toggled per base / per layer (`SceneData::terrainBaseStochastic`, `TerrainLayer::stochastic`). |
| `sequence.hpp` | ~330 | Cutscene Director data model + shared math: `Sequence` (object tracks + camera *shots* - free or bound to Camera entities - plus widescreen bars, fades, skippable), `seqEase`/`seqSample`/`seqBarsFractions`/`seqShakeOffset`/`seqCameraForward` (each mirrored in the generated PS2 player - keep in sync). **Camera roll** (the Dutch angle) lives here too: `seqCameraUp(fwd, rollDeg)` is a THREE-way twin (host bake, viewport `camView`, generated player's `upFor`) and `seqRollFromUp` inverts it; roll 0 reproduces the engine's old hardcoded `(0,1,0)` exactly. `SeqCameraKey::roll` is for FREE shots only - a bound shot takes its whole basis from the entity's Euler via `seqCameraUpFromEuler`, because **`rotation.z` is NOT a lens-axis roll** (`Rz*Ry*Rx` rotates about the world axis last: on a camera pitched 40 deg, `rotation.z = 90` swings the aim 54 deg). `seqEulerFromBasis` is the inverse used to bake a recorded roll into an entity's rotation track. Project-wide (like presets), persisted but not in undo. Compiled to `src/gen/sequences.gen.cpp` (a runtime player Script + the bars/fade `renderOverlay`); the dopesheet UI + viewport scrub live in `app.cpp`. Object renames remap track/shot name references - `App::renameObjectRefs` (app.cpp) is the ONE remap for every by-name reference in the project (here, mirror/scroller/portal/camera-feed lists, area lookups), called by the Properties name field and by the AI Assistant's `set_object`; `objRenameFrom_` only remembers what the name WAS while the field is being edited. A new by-name reference joins that function or it goes stale on the first rename. |
| `ambience.hpp/.cpp` | ~330 | **Ambience presets + the day/night cycle** (docs/day-night-cycle.md). `AmbiencePreset` is the sky/lighting/AO/fog "mood" bundle a scene picks; its fields mirror the matching `ProjectSettings` members exactly, and `project::resolvedSettings` overlays them so all downstream codegen/viewport keeps reading ONE set of fields. `DayCycle` + `DayKey` add time: sun/moon arcs plus a cyclic keyframe list, and **`ambience::evaluate` is the single answer** - `resolvedSettings` applies it (which is how one hook reaches the vertex bake, aobake, gibake + its probe grid, `SCENE_LIGHT_*` and from there the runtime projected shadows/flare/god rays), codegen bakes `SCENE_SUN_*`/`SCENE_MOON_*` from it, the viewport places its discs from it and the editor prints its readout from it. Host-only, no GL, no `Project` (`Resolved` exists precisely so `applyCycle` need not include project.hpp) - the placement/treegen shape, exercisable from a 40-line harness. Two rules the math must keep, both paid for: the resolved light is **clamped to +5 deg elevation** (a light at the horizon gives flat ground zero diffuse, one below it lights the world from underground - night gets dark from the key COLOURS), and the sun/moon handover **switches** the direction to whichever body dominates while `Resolved::shadowFade` takes the cast shadows to 0 across the swap - **never blend two near-opposite directions**, which cost two bugs: a weighted sum cancels to zero at the crossover (a 180-degree shadow flip between 17:59 and 18:01), and rescuing that with a zenith term (`4*w*(1-w)`) points the light straight up at the midpoint, so shadows collapse under their casters and the surviving horizontal component briefly points the wrong way - reported from the console as "at dusk the shadows vanish and come back from the wrong side". Blending the AZIMUTH instead sits on the +-180 seam and flips under noise. A direction cannot be crossfaded; the shadow it throws can, so the switch happens at 0.03% shadow opacity (measured: 167.97 deg of direction change at fade 0.0002; the worst step with any shadow visible is 0.24 deg/min). Only the DIRECTIONAL casters multiply their alpha by `g_shadowFade` - the projected silhouettes; a blob shadow sits under its caster and has no direction to swap, so fading it too (the first attempt) just left every object unmoored for the hour around twilight, and a BAKE ignores the fade entirely. Second thing the same report cost: `renderProjShadows` used to drop the sun as a candidate outright below ~14.5 deg of elevation (`syd < 0.25`) because the receiver patch is capped at 3.5x the caster radius and a longer shadow crops square. That cliff is fine under a steep arc and catastrophic under a shallow one - with the day/night example's 28-degree peak it swallowed **4.15 h** in one stretch ("shadows only turn up just before noon and just before midnight"). It is a smoothstep ramp from the light's own 5-deg floor to 16 deg now, folded into both the candidate score and the alpha, so the most cropped shadow is the faintest: measured on the console, shadows present 15.70 h/day -> 21.33 h, longest gap 4.15 h -> 1.33 h (the twilight handover itself, which is meant to be shadowless). The elevation is also capped BELOW the pole (`kMaxLightElevation` 88 deg): a steeply authored arc reaches 90.0000 deg at its own peak, and `M4x4::lookAt` builds its basis from a hardcoded world-up (`$vf6 = {0,1,0}`) with a double cross product, so a light straight up degenerates every basis derived from it - the projected shadows' light camera above all. **Anything new that builds a basis FROM the light direction inherits that trap.** A new cycle field joins `operator==` (History::push short-circuits on equality), the `"cycle"` object in `write/readAmbienceSection`, and `project::clampDayCycle`. **The RUNTIME half** (`DayCycle::runtime`) is a generated numeric TWIN: `templates::dayNightHeader` emits `inc/daynight.gen.hpp`, a header of `inline` functions (the live_debug.gen.hpp arrangement - both game-cpp templates need it and neither may carry a copy) reproducing `ambience::evaluate` + `driftGrade` from tables in scene_data.hpp. A harness comparing the two over all 1440 minutes measures 1.2e-7 on directions and exactly 0 on colours and the grade; keep it that way. Three traps this cost: the header had to be added to **`refreshGenerated`'s overwrite list** or it is written once by `project::create` and never refreshed (the live_pad.gen.cpp mistake again); `postFx.setGrading` takes FIXED POINT (`unsigned char` gain where 128 = 1x, `short` lift, `mixAmt` 0..128), not the float globals; and once the clock runs, `time` (where it starts) and the hour geometry bakes at are two different questions - **`ambience::bakedHour` is the one answer every bake-side consumer asks**, and `DAYCYCLE_STARTS` vs `DAYCYCLE_BAKEDS` is that split in codegen. |
| `starfield.cpp/.hpp` | ~150 | **Procedural night sky** (docs/day-night-cycle.md "Stars"). Host-only, no GL, no `Project` - the treegen/dronegen shape. `generate(Params)` returns a list of stars (unit direction, apparent size, RGB, magnitude tier); the editor viewport and the generated game's `buildStarField` each turn one into a quad, so codegen ships ~400 ROWS rather than ~2400 baked vertices and the preview cannot drift from the console. **The reason it is affordable at all is `StaPipInfoBag::additiveBlendFix`** - an additive 3D bag (`Cs*FIX + Cd`, the light beams' own trick): per-star brightness/colour ride the Gouraud vertex colours so a bright star ADDS light and blooms instead of being a grey pixel, one bag per `kTiers` magnitude tier makes the whole sky **three submits**, and both the dusk fade (`DayKey::stars`) and the twinkle are that bag's FIX - one byte per bag per frame, never a rebuild. Three things to keep: determinism through `mix32(seed, index, channel)` (never a running counter, so a slider adjusts the sky instead of reshuffling it), the Milky Way PULLS stars toward the band rather than rejecting samples (a reject loop silently thins the sky as the slider rises, so the requested count stops being the delivered count), and a star **must** be drawn through the soft corona sprite - an untextured quad is a hard little SQUARE, which is exactly the look the whole arrangement exists to avoid, so `projectStarCycle` forces the `flare-corona.png` bake and `STAR_COUNT > 0` forces its load even with no beams anywhere. |
| `animedit.cpp/.hpp` | ~180 | **Non-destructive animation-clip editing** (docs/animated-models.md, Tools > Animation Editor). Host-only, no GL - the decalproj/aobake pattern. `Project::animClipEdits` holds one `AnimClipEdit` per touched (model, SOURCE clip): rename, time scale, trim window, default loop; `ProjectSettings::animSourceFps`/`animPlayFps` give the project-wide speed ratio (`projectTimeScale`). `applyClipEdits` folds them into a parsed `glbparser::Skel` right before `writeTskl` in `bakeAnimAssets` (templates.cpp) - trim (interpolated boundary keys, rebased to 0) → scale times → rename - so the source .glb/.fbx is never touched and the console pays nothing. **The viewport is the twin**: `Viewport::setAnimEdits` (pushed each frame by the app, which owns the Project) makes `updateAnimPose` trim/retime the same way, and `renderAnimPreview` poses at an explicit time for the panel. Change the math in one place and the other must follow. Clip names in the model are SOURCE names; every reference (`SceneObject::animClip`, the Player locomotion clips, the Animation node's Clip param) stores the EFFECTIVE (post-rename) name - `effectiveName`/`sourceName` translate, `App::effectiveClips` is what every picker lists, and `App::renameAnimClipRefs` retargets on rename. |
| `camtake.cpp/.hpp` | ~580 | Phone-recorded 6DoF camera takes (ARKit) → Cutscene Director camera keys. Two strictly separated stages: *acquisition* (anything producing a `CamTake` — the CamTrackAR `.hfcs` loader via a minimal XML subset reader, the canonical CSV, and the live `phonecam` stream; spec + conventions in `docs/camera-takes.md`) and *bake* (`bakeCamTake`: scale/yaw/origin/time mapping + either time-parameterized RDP decimation or fixed-rate resampling → free `SeqCameraKey`s, pure and harness-testable). **`mapCamSample` is public because the live view must not have its own copy of the mapping** — the phone previews the camera through it and the bake places keys with it, so they cannot drift. Two `CamTakeMapping` fields exist only for the live path: `hasAnchor`/`anchor` (a stream has no meaningful "first sample" to pivot on, so Recentre pins one — without it the path jumps when a recording starts mid-stream) and `keyRate` (the Director's keyframe density; mutually exclusive with `tolerance` by design). The resampler derives key times from the STEP INDEX, never an accumulator — `t += step` drifts and then emits two keys at the same time. UI = the "Import take..." modal in `app.cpp` (`seqTake*_` members). |
| `phonecam.cpp/.hpp` | ~690 | **Live phone camera link** (docs/phone-camera.md): the phone as a viewfinder — it shows a JPEG stream of the editor's viewport while its ARKit pose drives that camera, and the Cutscene Director records the move into keys. Second acquisition source for camtake. `phonecam::Link` = Runner/Session idiom (worker thread owns the transport + does the JPEG encode; `drainEvents()`/`drainPoses()` on the UI thread once per frame in `App::phoneCamTick` — the ONLY place link data meets `project_`/ImGui). Never touches sockets: goes through `wire::makeWebSocketTransport()`, because WebSocket is what React Native and browsers have built in. Poses ride a bounded ring (`kMaxPendingPoses`) so a stalled UI thread drops old samples instead of falling behind; a preview frame already waiting is REPLACED, and `previewWanted()` gates on the send backlog — a weak link must cost frame rate, never latency. Also owns `testClientPage()`, the self-contained HTML client served to a plain GET on the same port (synthetic poses on purpose — it is the verification path, and a half-right DeviceOrientation conversion here would misdirect the ARKit one). The phone app is **NOT in this repo** — it is `github.com/doctorspider42/tyrax-cam` (public; Expo + a local Swift ARKit module, its own CI producing an unsigned sideloadable .ipa). Its `PROTOCOL.md` is the client-side twin of `docs/phone-camera.md`: **a protocol change has to land in both, and bump `phonecam::kProtoVersion`** so a stale app is denied at the handshake instead of misbehaving. Nothing reaches codegen — a recording ends as ordinary `SeqCameraKey`s. |
| `viewport.cpp/.hpp` | ~4860 | Offscreen GL 3.3 preview. **The orbit camera may tip both ways** (`kOrbitPitchLimit`, +/-85.9 deg - not 90, because `camView`'s up vector is world +Y and the basis degenerates when the two align). The camera passes THROUGH the terrain on purpose - an intermediate version lifted the pivot to keep the eye above ground and it read as the camera lying on the terrain and then jumping, so `orbit()` has no floor. To get the sky in frame the pivot is raised by hand: `pan`, or `dolly` (right+middle drag, down = forward), which moves the pivot along the FULL view direction so it climbs while you look up and leaves `distance_` alone - it is a pan, not a zoom. Its speed is `NavConfig::dollySensitivity` (own slider, wider 0.1..8 range: this gesture crosses whole scenes). **A new NavConfig field is four places** - the struct in app.hpp, the `match("nav...")` line and the `<<` line in load/saveGlobalConfig, and the widget in `drawNavigationPopup`; `orbitAroundSelection` additionally has a second door in the View menu, and both must reset `navFocusedIndex_` so the pivot re-snaps. **One camera, one place**: `camView()` resolves the projection (perspective / parallel / the six locked axis views - `Viewport::Projection`, docs/orthographic-views.md) plus the Cutscene/look-through override into an eye + orthonormal basis, and `camRay()` turns image coords into a world ray. `projectToImage()` is that ray's INVERSE (world point -> image coords of the last frame) - an app-side ImDrawList overlay that has to sit on world geometry (the measuring tape, `App::drawMeasureOverlay`) places itself with it instead of rebuilding a camera. `render()`, `pick()`, `terrainRaycast()` and `placementRaycast()` ALL go through them - they used to each rebuild a hardcoded 50-degree perspective ray, which silently disagreed with the image under any other projection. `orbit`/`pan`/`fly` read the same basis (orbiting a locked axis view seeds yaw/pitch from that axis and falls back to `orbitBase_` - the projection in use before that axis view was picked, which `setProjection` records, so an axis view is a glance and not a new home). The corner **axis gizmo** is app-side (`App::drawAxisGizmo`, ImDrawList over the image, axes read from the view matrix's columns): it does its OWN hit test and returns "cursor is over me", which every click-consuming branch (pick, rubber-band start, paste commit) must keep honoring - an overlay that skips that veto silently makes each click on it also clear the selection. Note the ortho depth range straddles the eye on purpose (a parallel view is a slab, so a Top view still draws what is above the camera). Also: unit-primitive meshes, terrain grid + heightmap, sky dome, selection outline (+ per-peer session-presence outlines via `setPeerSelections`, drawn under the local amber), live point-light shader, sculpt-brush raycast, orbit/pan camera. Also the Material Editor preview (a primitive, a project .obj, or an animated .glb/.fbx in bind pose via `buildMatPrevAnimated` - the assigned .mtl resolved name-matched like the console, with the selected entry's staged values), its paint raycast (`materialPreviewPick`) and the live painted-texture upload (`updateTexturePixels`, shared texCache_ id so the scene updates too). **Every preview window owns its OWN framebuffer** (`renderMaterialPreview`/`prevFbo_`, `renderAnimPreview`/`animFbo_`, `renderTreePreview`/`treeFbo_`, one per future preview) - several tool windows are routinely open at once and size their previews independently, so a shared target both thrashes its `glTexImage2D` size and makes each `ImGui::Image` show the other's draw; `ensurePreviewBackdrop` is the shared gradient+checker backdrop, the only thing worth sharing. Preview shading is BAKED into vertex colors (`shadeOf`, the `g*` light globals), so the tool windows' lighting override (`PreviewLight`, the panels' *Light* combo) is a re-bake of the preview's own meshes under a scoped global swap (`ScopedShade`, private unit-shape copies, the light folded into the `matPrevModel_` key) - never `setLighting`, which is the scene-wide setter and rebuilds every mesh incl. the terrain AO grid. `modelBounds()` is the **GL-free** model-AABB lookup (objparser + own cache) that the AO occluder pass uses instead of `modelDraw()` - see the aobake row. `grabPreviewRgb()` reads the LAST rendered image back as packed RGB for the phone camera link: it blits into its own small framebuffer and reads back *that*, because a straight `glReadPixels` of a 1600x900 viewport stalls the frame; `lastImageFbo_` is the source, tracked per render (with `lastImageW_/H_`, which is NOT `fbWidth_/fbHeight_` - see below) so a graded frame streams graded. **PS2 output mode** (`Ps2Output`/`setPs2Output`, docs/ps2-viewport.md) is the one thing that decouples the RENDER size from the panel size: `render()` reassigns its own `width`/`height` params to the GS framebuffer size, so the entire scene pass moves to 512x448 without any site knowing, and a final presentation pass (`PS2_FS`, sharing GRADE_VS/`gradeVao_`) point-scales `fbo_`/`gradeFbo_` into the panel-sized `outFbo_` with the display window's letterbox. Consequences to respect: `fbWidth_/fbHeight_` is the GS size in that mode (anything wanting the panel wants `outW_/outH_`), the letterbox lives in `CamView::boxSx/boxSy` so **camRay/projectToImage take and return PANEL coords** while the render pass gets none of it, `projMatrix()` is the box-scaled twin (ImGuizmo draws over the whole panel rect) while the matrix `render()` draws with is not, and `Viewport::ps2LetterBox` must keep agreeing with `App::drawSafeAreaOverlay`'s fit - they draw the same rectangle. The geometry itself is NOT computed here: `App::ps2ViewportOutput` resolves it from the project's display settings as the host twin of the engine's `RendererSettings::updateGeometry` + `RendererCoreGS::presentFrameBuffer`, so **a new engine display mode is one entry in each of those three places and nothing in the viewport**. |
| `vuir.{hpp,cpp}`, `vuasm.{hpp,cpp}`, `vusim.{hpp,cpp}`, `vugen.{hpp,cpp}` | ~2200 | **The VU framework** (docs/vu-framework.md). Host-only, no GL, no `project.hpp` - the aobake/livedbg shape, exercised entirely from `--vu-check` / `--vu-emit` / `--vu-list`. `vuir` is the shared instruction model: **VCL-level assembly with unlimited virtual registers** (pre-schedule - it says what a program computes, not what cycle each op lands in). `vuasm` parses the engine's handwritten `.vclpp` into it (the vclpp layer: `#include`, one-level `#define`, non-nesting `#macro`/`Name{ }`); `vusim` EXECUTES it on **either vector unit** (`Target::VU1`/`VU0` - 1024 vs 256 quadwords of data memory, 2048 vs 512 micro slots, `xgkick`/`xtop` warned about on VU0 which has neither, and `runKernel` for the `vcallms` contract: restart at the entry, data memory persists; masked fields, ACC, Q/I, clip flags, 16-bit VI wrapping); `vugen` is the C++ DSL plus the `.vclpp` and EE-side emitters. **The load-bearing property**: `vusim` ends a run with the same memory image `vucap::Capture::vuMem` carries and it is decoded by the SAME `vucap::scanGifPackets`, so a simulated run and a console capture are directly comparable - a second GIF decoder would quietly destroy that. The generator emits **VCL, not microcode**, so `vcl` keeps doing register allocation and dual-issue scheduling; that is why a generated program is as fast as a handwritten one, and why the framework must never try to schedule. One `Desc` yields the microprogram, the EE program class, the tag-block size and the GS register list together - the drift those used to have between `.vclpp` and `*_vu1_program.cpp` is what the module exists to remove. `equivalence()` is the proof obligation: change a described program on either side and `--vu-check` fails until both agree. Two traps the module had to learn the hard way. **`vusim` must not use host float semantics**: the VU FPU has no inf and no NaN, overflow saturates to `0x7F7FFFFF` and denormals are zero, so every float the machine writes goes through `vuFloat()` - and the MOVE FAMILY (`move`, `mr32`, `mfir`, `ftoi*`, `lq`/`sq`) deliberately bypasses it because those carry integer bit patterns and clamping one corrupts it. **`--vu-check` proves the microprogram, NOT the EE side**: it stages VU1 memory itself, so the emitted `addProgramQBufferDataToPacket` is never executed by it - which is how the emitted `c` came to put colours a block too far and the emitted `td` to unpack normals on top of the ST block while the check still said "bit-identical". The per-vertex block layout therefore lives in exactly one place, `attrBlocks()` in vugen.cpp, and the stream count plus the emitter both derive from it; never restate that layout. `--vu-replay` closes the loop the other way: it reconstructs the input from a REAL console capture (`bin/vucap.bin`), re-runs it here and diffs the staged GIF packet against the hardware's own - `examples/vu-lab` is the fixture and matches 36/36 GS vertices. That is what caught the last arithmetic gap: `vusim::run` now also switches the host ROUNDING MODE (the VU truncates toward zero, an x86 rounds to nearest-even), which is invisible on screen X/Y and showed up as one or two ULP in the 24-bit Z. Range (`vuFloat`) and rounding are separate fixes and both are needed. |
| `runner.cpp/.hpp` | 301 | Docker + PCSX2 pipeline on a worker thread. **Pre-flight, before Docker: `project::checkScriptNamespaces`** - a user-owned script in `src/scripts/` lives in the project's C++ namespace, which is derived from the project NAME, and renaming a project deliberately does not rewrite user-owned files. So a rename (or the common "copy an example, rename it" start) leaves every script registering into a namespace that no longer exists, and the PS2 toolchain's answer is forty lines about `no known conversion from 'Old::Thing*' to 'New::Script* const&'` that never mentions the rename. This is the one refresh-time failure the Runner treats as FATAL rather than a warning - the compile cannot succeed, so continuing only buries the real cause. `TYRA_SCRIPT`'s qualified argument is the check. (states Idle/Running/Success/Failed). `buildAndRun()`, `runEmulatorOnly()`, `exportIso()`. **Every step is incremental, and the incrementality is fragile in one specific way: anything that WRITES a file the compiler reads must not write it when nothing changed.** `cp` sets an mtime, PS2SDK headers reach the compiler through `-I` (so they are ordinary user headers in the `.d` files), and the audsrv overlay used to be re-applied on every build — which invalidated 16 of a game's 18 translation units, every time, silently. Both the overlay and the engine's `Makefile.base` copy are now stamped/compared; a new "just copy it, it's idempotent" step in here needs the same treatment or it quietly restores the full-rebuild behaviour. `buildAndRun(p, run, rebuild)` — the `rebuild` flag is the escape hatch (Build > Rebuild, `--build --rebuild`): recreate the container, drop `/src/obj`, `/src/bin` and the whole compiled engine, build everything from source. |
| `platform.cpp/.hpp` | ~800 | **The ONE place OS differences live** — the editor builds and runs on Windows AND Linux from this one source tree. Covers: `exePath`/`configDir` (the machine-global config root: `%LOCALAPPDATA%\tyra-editor` vs `$XDG_CONFIG_HOME/tyra-editor` — editor.ini, the session remote-cache, the exported PS2SDK headers)/`homeDir`/`userName`/`exeSuffix`/`processId`, `sleepMs`/`logTimeStamp`, the shell fragments (`quiet`, `killByName`, `envPrefix`, `commandExists`), **`Process`**, the pickers (`pickFile`/`pickFolder`/`errorBox`/`setDialogOwner`), `revealInFileManager`, `openInVSCode`, `installDesktopEntry`, and the font lookup (`systemFontPath`/`systemFonts`/`fallbackFontFiles`/`defaultFontLabel` for the game bakes, plus **`uiFontFiles`** — the EDITOR's own interface font chain, a different question and answered separately). **The rule: a feature that needs to know which OS it is on grows an entry HERE and the call site stays platform-blind** — that is what keeps ~40 sites free of `#ifdef`. `Process` is the load-bearing part: one shell command line (`cmd.exe /S /C` vs `/bin/sh -c`), optional stdout capture (`readLine`/`readAll`), optional stderr-to-file, `running()`/`wait()`/`startDetached()`, and a **`kill()` that takes down the whole TREE** — Job Object vs `setsid()` process group. Never "improve" that to kill just the child: the shell wrapper is never the process doing the work (docker, make, node, curl, ps2client), and killing it alone orphans a token-burning backend or a port-holding file server. Two subsystems deliberately stay outside and say so in a comment — the socket shims in `wire.cpp` (Winsock2 *is* BSD sockets with other spellings, mapped in place) and PCSX2/`PCSX2.ini` discovery in `runner.cpp`/`pcsx2_config.cpp` (genuinely different shapes per OS, and platform.cpp has no business knowing what PCSX2 is). Linux specifics worth knowing: file dialogs shell out to **zenity** (kdialog fallback) so a machine without either has no Open/Import; system fonts resolve through a lazily built filename→path index over the freedesktop roots; SIGPIPE is ignored process-wide from a static here (a dying child's pipe or a vanished session peer would otherwise kill the editor); and `installDesktopEntry` writes the `.desktop` + hicolor icon that give the window its icon — under **Wayland that is the only mechanism there is** (no icon protocol; the compositor matches the surface's app id to a desktop file, and `glfwSetWindowIcon` fails with `GLFW_FEATURE_UNAVAILABLE`), so the app id hinted in `App::run`, the desktop file's basename and the icon name are one `kAppId` constant and must stay that way. |
| `pcsx2_config.cpp` | ~170 | Finds PCSX2.ini (portable dir next to the exe first, then the Documents known folder on Windows — beware OneDrive redirection — or the XDG config dir / flatpak sandbox on Linux) and, before launch, force-enables `HostFs = true` plus — when `ProjectSettings::keyboardMouse` is on — points the emulated USB ports at the host devices (`ensureUsbKbdMouse`: `[USB1] Type=hidkbd` bound to `Keyboard`, `[USB2] Type=hidmouse` bound to `Pointer-0` + buttons). See `docs/keyboard-mouse.md`. |
| `iso9660.cpp`, `isoexport.cpp` | 379+264 | In-tree ISO9660 writer + disc layout planning (`Project > Export PS2 ISO`, Disc Layout window). |
| `json.cpp/.hpp` | 158 | Tiny standalone JSON parser used for reading the `.tyra` project file. |
| `session.cpp/.hpp` | ~900 | **Live collaboration session** (docs/collaboration.md). `Session` (Host/Client) owns one worker thread — Runner idiom (`std::atomic` state, mutex-guarded event + command queues); the UI thread drains `drainEvents()` once per frame in `App::sessionTick()`, the ONLY place session data touches `project_`/ImGui. Host scans+hashes the project (model files from `project::manifestFiles()`, everything else from disk minus bin/obj/.git/.res-baked/*.history), serves a content-hash `manifest`; the client diffs against its `remote-cache/<projectId>` cache, fetches only misses in 256 KiB chunks, opens the materialized project. Handshake: proto-version + 6-digit join code, `deny`/`bye`, ping/timeout keepalive, kick/close. `broadcastFrame`/`sendFrameToHost` + `AppEvent::Frame` are the hook the live-sync layer rides. Never touches sockets directly — goes through `wire::Transport`. |
| `elfsym.cpp/.hpp` | ~230 | **ELF32 reader + the release audit** (docs/devkit.md). Sections, symbols and section bytes out of a built PS2 ELF, and `auditRelease()` on top: the check that a shipped game carries NO devkit code. **The PS2 toolchain strips the symbol table**, so the audit leans on two designed signals instead - the `TXDEVKIT-<layer>` marker each generated devkit runtime plants (`__attribute__((used))`) and the channel file names - and reports text/data/bss so the cost is a number. `--audit-release` exits 0/1 for scripts; the Runner runs it after every release build and logs the verdict. Also the future foundation for named-memory reads (needs an unstripped ELF / map file first). |
| `livelogic.cpp/.hpp` | ~700 | **Live Logic host side** (docs/live-logic.md) - the flow-graph HOT PATCHER: the editor compiles a graph itself so editing one no longer needs a Docker rebuild. `livelogic.hpp` is the single source of truth for the IR (`BlockKind`/`OpCode`/`CondOp`/`PosKind`, `Block`/`Instr`/`Program`, the caps) - **templates.cpp GENERATES the interpreter's enums and dispatch switch from it**, so the numbering cannot be restated by hand and a missing interpreter body becomes a `#error` in the generated file. `compile()` mirrors `flowGraphScript`'s resolution (resolveTarget / posExpr / boolInputsOr) but writes INDICES instead of C++ literals, linearizes exec chains into blocks (a `Delay` owns the block it arms) and allocates per-node state slots; `capability()` is the honest gate - the supported node set is explicit and anything else is reported per graph. `graphHash()` deliberately EXCLUDES node positions (dragging a node must not read as a logic change), and `builtListText()`/`loadBuiltList()` are the "what did the ELF compile" record that decides which graphs need patching. |
| `livedbg.cpp/.hpp` | ~250 | **Live Debugger host side** (docs/live-debugger.md) - the flow-graph debugger's formats and history model. No GL, no ImGui, no project.hpp: the aobake/placement shape, harness-testable. Owns `Symbols` (`src/gen/livedbg.sym`: node key -> scene + object id + node id, the watch-variable list and the table hash), `Snapshot` (`bin/livedbg.bin`: cumulative hits per node, a ring of recent fires with their AGE in frames, watch values, halted flag, break key), `Command` (`bin/livedbg.cmd`: full breakpoint list, halt/step/step-until-fire, force-fire keys) and `Timeline`, the per-frame fire history the Debugger scrubs. **Every layout here has a twin in the generated runtime; the shared caps (`kMaxNodes`/`kMaxBreakpoints`/`kMaxForced`/`kMaxEvents`) are read by codegen from this header.** Torn writes are rejected by exact-size + footer-echo on both ends; commands apply only when `seq` changes (so a repeated Step must bump it). |
| `livetime.cpp/.hpp` | ~180 | **Time machine host side** (docs/time-machine.md) - the state-rewind channel: `Snapshot` (`bin/livetime.bin` written by the game / `bin/livetime.rst` written by the editor) plus `History`, the capture ring. Same harness-testable shape as livedbg, and the same torn-write guard (exact size + a footer echoing `seq`). **The editor deliberately does not understand the payload** - what is in a capture is a codegen detail, so this stores bytes and hands the right ones back; the `layout` hash in the header is what stops a capture landing in a differently built world. `History` is bounded by a BYTE budget (a count would mean a tiny scene wastes it and a huge one blows it), keeps at least the newest capture whatever the budget, and CLEARS itself when a capture's frame goes backwards - that is a restarted game, not a rewind (a restore leaves the frame counter running forward on purpose, because it is the history's ordering key). It lives in RAM by design: the only disk footprint is the two fixed-size channel files. |
| `logview.cpp/.hpp` | ~200 | **Log severity classification** (docs/log-panels.md) - what the *Output* and *Debug* panels split their lines into (error / warning / info / verbose). Pure function of text: no ImGui, no GL, no `Project`, so a real build log can be run through it from a 40-line harness (the treegen/placement pattern) instead of being eyeballed in a docked panel. Three decisions to keep. (1) **A diagnostic is a RUN of lines** - a gcc error carries its source snippet and notes, a TYRAX dump its `|` body - so continuation lines inherit their entry's level (`Line::cont` marks them) and a filtered view never strands an `error:` without the four lines that explain it; the same flag is why the chips count ENTRIES rather than lines. (2) **The earliest marker in a line wins**, which is the whole reason `[editor] Warning: texture bake failed` is a warning while `[editor] ISO export failed` is an error; the marker tables are heuristic for tool output and exact for the engine's own `LOG:`/`==WARN:`/`====ERR:` prefixes (vendor/tyra debug.hpp - a change there is a change here). A `> <command>` echo skips the scan entirely, which is what keeps `-Werror` and paths containing "error" out of the error bucket. (3) **The parse is incremental** (`parse(log, from, state, out)` + `appendPartial`): a build appends lines continuously and re-classifying a megabyte per appended line costs far more than a frame, so the panel keeps the resume offset and the carried `State`. A harness must assert that feeding a log one line at a time gives exactly what one-shot parsing does - the continuation state is the thing that breaks if it does not. |
| `livepad.cpp/.hpp` | ~330 | **Remote Pad host side** (docs/remote-pad.md) - the input direction of the host: channel, and the reason a pad-driven feature is testable at all without a human: the editor (or `--pad`) writes `bin/livepad.bin` and the game overlays it on the physical pad, so NOTHING needs the window focus. Same harness-testable shape as livedbg/livetime (no GL, no ImGui, no project.hpp) and the same torn-write guard. Three decisions to respect: the file is absolute STATE, not events (a dropped poll cannot swallow a press), which is why the game expires an overlay whose `seq` stopped moving for `kStaleFrames` and why every writer must keep refreshing at ~25 Hz; the button mask is indexed by `kPadButtonNames` (input.hpp), the same order codegen and the engine agree on; and `parseScript` resolves a pad script into a flat timeline of (state, seconds) `Step`s, so the language is checkable with no file system and no game - the CLI (`padFromCli` in main.cpp) and the panel (`App::remotePadTick`/`drawRemotePadWindow`, devkit_ui.cpp) share this one encoder. The game-side twin is `templates::livePadSource`. |
| `uiscript.cpp/.hpp` | ~430 | **UI scripting host side** (docs/ui-scripting.md) - the answer to "how do I click something in the editor without a human", and the reason a panel change can be verified rather than eyeballed. Two halves. (1) The **item registry**: ImGui declares four `extern` hook functions under `IMGUI_ENABLE_TEST_ENGINE` (`ImGuiTestEngineHook_ItemAdd`/`ItemInfo`/`Log`/`FindItemDebugLabel`) purely so a test engine can implement them - **we implement them**, which buys label + rect + checked/open/inputable state for every widget, with no imgui_test_engine dependency (its licence is not ours to take on). The define is `PUBLIC` on the imgui target because it changes `ImGuiContext`'s layout, and collection is gated on ImGui's own `TestEngineHookItems`, so a normal session pays one never-taken branch per widget. (2) The **script**: `parseScript` -> `Step`s that `App::uiScriptTick` (devkit_ui.cpp) executes one at a time by injecting into `io.AddMousePosEvent`/`AddMouseButtonEvent`/`AddKeyEvent` - so nothing reaches the OS and no window needs focus. Two rules if you touch it: the tick must stay BETWEEN the GLFW backend's NewFrame and `ImGui::NewFrame` (it reads the map the last frame built and its event must be the last one queued), and `find(target, clickable)` must keep excluding whole-window items for anything that clicks - a bare window name otherwise resolves to the window's own rect and the "click" lands on whatever widget sits in its middle (it pressed R3 when asked to open a panel). `rightclick` is the same three-phase shape as `click` on mouse index 1, and it is what makes a CONTEXT MENU assertable at all - before it, everything hanging off a right-click (both node canvases, the object list) could only be checked by a human, which is exactly how a procedural context menu that closed the frame after it opened shipped unnoticed. Note the tokenizer strips **double** quotes only, so a two-word target written with `'...'` silently arrives as two tokens. |
| `wire.cpp/.hpp` | ~700 | **The only place sockets live** (no project.hpp dependency — pure bytes). Frame codec `[u32 jsonLen][u32 binLen][json][bin]` LE with per-part caps + incremental `FrameDecoder`; `wire::Transport` interface (listen/connect/poll/send/kick, single-thread contract) with two impls, protocol code never seeing a socket: `makeTcpTransport()` (Winsock2 + WSAPoll) for LAN collaboration, and `makeWebSocketTransport()` — an RFC 6455 **server** (SHA-1 + base64 upgrade, unmasking, ping/pong, fragmentation) for the phone camera link, because WebSocket is what React Native and browsers have built in. The two share the accept/poll/send machinery: WebSocket is a per-connection `WsCodec` between the socket and the same `FrameDecoder`, one binary message = one `encodeFrame` image. Two contract differences to respect if you touch it: a WS peer is announced on **upgrade**, not accept (so an ordinary browser GET — which gets served an HTML page instead — never becomes a peer, and never produces an unmatched `Disconnected`), and a dying codec sets `closeAfterFlush` rather than dropping, or the served page is truncated. Also `fnv1a64`/`hashFile` (transfer-cache hashing) and `localIPv4()`. Binary payloads ride the raw trailer, never JSON (json.cpp collapses `\u`). |
| `objparser.cpp` | 109 | Wavefront .obj importer for custom models. Editor-side only: the GAME never reads .obj, it reads the baked `.tmdl` (below). |
| `theme.cpp/.hpp` | ~350 | **The editor's look** (docs/editor-theme.md): the four interface themes, the shared style metrics, and the semantic colours the hand-drawn chrome reads. ImGui only - no `Project`, no GL, no `App` - so the palette is a pure function of a theme id instead of being spread over the call sites that used to hardcode `IM_COL32`. A theme is **nine colours** (`Palette`) from which every one of the ~60 `ImGuiCol_` entries is DERIVED, which is what stops four themes from being four sixty-line tables that drift the day ImGui adds a colour; the METRICS (rounding, hairline frame borders, trackless scrollbars, accent tab overlines, tree lines) are shared by all of them **including the stock-ImGui one**, because a theme is a palette and not a second layout. **The rule: a widget asks for a MEANING, never a colour** - anything that cannot go through an `ImGuiCol_` (the toolbar's vector icons, the LIVE/DBG/LOGIC/SESSION chips, viewport overlays) reads `theme::semantics()` (accent/accentMuted/ok/warn/danger/text/textDim/surface/border), or it stays green in a violet editor. A new theme is one `Palette` literal + one `info()` row + one `paletteOf()` case; a new semantic colour is a `Semantics` field filled by `apply()` for EVERY theme, which is what stops a call site inventing one. `applyImNodes()` tints both node canvases (darker than a window; per-node/per-pin colours are left alone - they encode category and pin type, which is data). `hoverAnim()` is the 0..1 ramp the hand-drawn hover highlights fade over, state in the current window's `ImGuiStorage` keyed by `GetItemID()`. App side: **`App::applyTheme()` is colours + metrics + scale in that order and `baseStyle_` IS the themed style** - `applyUiScale()` resets to that reference on every zoom step, so a theme that only wrote `ImGui::GetStyle()` is undone by the next `Ctrl+=`; it must also run AFTER `ImNodes::CreateContext()`. And `ImGuiStyle`'s ctor leaves `FontSizeBase` at **0**, which the reference copy carries, so the scale path restores `uiFontSize_` (0 when no system face resolved = keep the built-in font's own size). |
| `tmdl.cpp/.hpp` | ~130 | **The binary static-model format the game ships** (docs/model-pipeline.md). Pure serialization, no project.hpp: `tmdl::Model{parts, min, max}` -> bytes, following the `.tskl` conventions (4-byte magic, `u32` version read as a range, packed little-endian, fixed NUL-padded strings, counts + inline arrays). Written by `templates::bakeStaticModels` (called from `refreshGenerated`, so `--refresh-gen` produces it without Docker), read by the engine's `TmdlLoader`, which returns the SAME `LeanObjMesh` the .obj loader does so the generated game keeps one geometry path. Everything the EE used to work out at load is resolved at bake: triangulation, flat normals, the V flip, material assignment incl. a per-object .mtl override, atlas UV rects folded into the UVs, bin-relative texture paths, and the LOD tiers. `texbake` then skips mirroring the source .obj and the Runner sweeps a superseded one out of `bin/`. **Both sides carry a "keep in sync" comment - the layout lives in two files.** |
| `savebake.cpp/.hpp` | ~600 | Memory card save appearance bake (Tools > Save Editor): `icon.sys` (964 B - browser title with `\|` line break; the PS2 browser renders ONLY full-width Shift-JIS, so ASCII is mapped to the 0x81/0x82-row full-width forms) and `list.icn` (a PS2 3D icon). Icon geometry sources (`iconInfo`/`iconIcn`): the flat image quad, a res/models `.obj` (+ map_Kd texture), or an animated `.glb` whose clip is sampled into ≤8 morph shapes via `glbparser::bake` - a real animated icon; static sources get a sine "sway". **Animation encoding**: each frame's keys are that shape's WEIGHT envelope over the timeline (tent peaking at its own tick, shape 0 closes the loop) - the browser lerps keys and blends shapes by weight; a single key per frame reads as weight≈0 and the icon renders INVISIBLE (expensively learned; semantics per mymcplus/ps2icon.py, key 0 sits where old docs saw "two unknown" u32s). Written to `res/save/` on every `refreshGenerated`, copied onto the card by the generated save system (`saveEnsureIcons`, buffer sized by codegen from `iconInfo().bytes`). **The save menu is a `GameMenu` with `saveMenu = true`** (one per project, seeded by `project::ensureSaveMenu`, found with `saveMenuIndex`), which is what buys it the Menu Editor, serialization and the panel bake for free - the same trick `titleScreen`/`pauseMenu` already used. Its `entries` are NOT authored: `menubake::asBaked()` swaps in `Project::saveSlotsPerPage` blank rows, so panel, editor preview and the generated row metrics all count rows identically. The labels are drawn at RUNTIME with `drawFontText` (`MenuData::row0Y/rowH/font`), because a baked label per slot cannot page - and that is why `Project::atlasFontIndices()` must list the save menu's font, or the rows render blank. `drawFontText` CENTRES on its x; left-aligning means adding half of `fontTextWidth`. **A flow node cannot call the game.** A graph writes only into `ScriptContext`, so a node parameter whose meaning is only known at RUNTIME travels as a SENTINEL that `TerrainGame` resolves - Commit Checkpoint's slot modes are the worked example (`SAVE_COMMIT_AUTOSAVE`/`SAVE_COMMIT_NEXT`, resolved by `resolveCommitSlot`). Emitting a call to a `TerrainGame` method from `flow_graph.gen.cpp` does not compile, and the mistake is easy to make because the two files read like one program. Note the sentinel values must dodge the field's existing idle value (`commitCheckpoint == -1` already meant "nothing requested"). **Async writes** (`SAVE_ASYNC`): every libmc call is already asynchronous - the blocking `saveWrite` just answers each with `mcSync(MC_WAIT)`. `saveWriteBegin`/`saveWritePoll` drive the same open/write/close chain one step per frame with `mcSync(MC_NOWAIT)`, whose contract is **0 = still executing, 1 = finished, -1 = nothing registered** (treat -1 as failure or the poll spins forever). The payload is COPIED into a static up front, so nothing the player does mid-transfer changes the bytes. Loads stay blocking on purpose. The spinner is a sprite SHEET walked with `Sprite::offset` (`MODE_REPEAT` - `MODE_STRETCH` derives the source rect from the texture size and ignores `size`), and **both sheet dimensions must be powers of two**: a 192x24 strip asserts `Texture width/height should be 8/16/32/64/128/256/512` and the game simply never leaves the TyraX splash, which reads as a hang rather than as a bad asset. `savebake::spinnerInfo()` is the single arbiter of which sheet ships - it validates a user-picked PNG (POT sides, width divisible by the cell count) and **falls back to the built-in rather than letting a bad one through**, so codegen, the panel's preview and its warning all read the same call and a mis-picked image cannot produce a game that halts. Any new "pick your own asset" setting on a PS2-side texture wants the same shape: validate host-side, fall back, and say why in the panel. **Idle motion** (`iconMotions()` / `applyMotion()`): a source with no animation of its own - the quad, an `.obj`, a `.glb` with no clips - gets one of six presets baked as displaced copies of shape 0. `iconMotions()` order IS the enum `applyMotion` switches on, so entries may be APPENDED but never reordered; the `.tyra` stores the string key and `iconMotionIndex("")` is sway, which is what every icon did before the setting existed. Amplitudes are derived from the model's own height so they mean the same thing whatever units the source used, and they stay small on purpose - the browser LERPS between shapes, so a big rotation between two of them cuts through the model instead of going around it. `iconPreviewFrames()` is the panel's picture: a small software rasterizer (z-buffer, barycentric, two-sided abs(N.L) shading) over the SAME `buildGeo` result, one image per animation shape so the panel can cycle them. It exists because the preview used to decode the .icn's texture segment alone, and a model with no `map_Kd` carries its colours in the VERTICES against a near-white `modelFallbackTexture` - so every such 3D icon previewed as a blank white square while shipping correctly. Preview and stats therefore come from one bake and cannot describe different icons. Also `busyText()` - the single source for the "checking memory card" overlay sprite + its codegen'd size constants. |
| `meshlod.cpp/.hpp` | ~230 | **Bake-time mesh decimation**, shared by the `.tskl` and `.tmdl` bakes (moved out of glbparser's anonymous namespace). Quadric-error half-edge collapse over a welded triangle list + the tier policy both bakes use (`kRatios` 0.5/0.25 of the welded count, `kMinCorners`, `kShrinkSlack`). **The one trap:** `weld(..., keyNormals)` must be FALSE for static meshes - they derive a flat normal per face, so keying on it makes every position a seam twin, the position-twin lock fires everywhere and nothing decimates (`generateTiers` welds by position+uv and calls `recomputeFaceNormals` after the collapse). Animated meshes keep `keyNormals` on: authored smooth normals are real data and a hard edge must stay a seam. |
| `fbxparser.cpp/.hpp` | ~650 | FBX importer for animated models, built on the vendored ufbx reader (`vendor/ufbx`, cloned by setup.ps1). Fills the SAME `glbparser::Baked`/`Skel` structures, so the whole downstream (.tskl, viewport preview, codegen) is format-agnostic; the `animimport::` namespace in its header is the extension dispatch every import site calls. FBX curves are resampled at 24 Hz + RDP-reduced; axes/units normalized to glTF conventions; external textures copied in at import. |
| `version.hpp` + `migrations.cpp/.hpp` | ~150 | **Editor/format versioning.** `version.hpp`: the editor semver (title bar + informational `"editorVersion"` in the manifest) and `kFormatVersion`, the on-disk contract (`"formatVersion"`; pre-versioning files = v0). `load()` refuses newer-format files; older ones open silently unless `migrations::stepsFor` returns registered steps — then the GUI (`App::openProjectAt`, the single funnel for every LOCAL open) prompts, backs up the format-bearing files into `_backup/` and migrates in memory (save only on success), and headless `--build`/`--resave`/`--refresh-gen`/`--apply-graph`/`--ai-graph` refuse (use `--migrate`). **Two invariants worth not breaking:** `migrations::backup` must copy everything the post-migration save writes (`save` + `saveHeights` + `saveSplat`) or the skipped file is unrecoverable, and `App::openRemoteProject` (a collaboration client) **refuses** rather than migrates — the project is the host's, and a migrated replica would diff against the host over fields it does not have. `migrations::validate()` guards the registry itself (ascending, unique, in range), called by `run` before the first step. Rules + step-authoring example: `docs/format-versioning.md`. |
| `primmesh.cpp/.hpp` | ~180 | Shared, GL-agnostic **unit-primitive tessellation** (box/sphere/cylinder/cone/plane → raw `pos+normal+uv`). The single host source: the viewport bakes shade on top of it, and `decalproj` uses it as receiver geometry, so a projected decal conforms to exactly the geometry the viewport draws. (templates.cpp keeps its own generated-string builders for the PS2 runtime — the pre-existing twin.) |
| `procgraph.hpp/.cpp` | ~600 | **Procedural scatter graph: data model + node registry** (docs/procedural-generation.md). `ProcNode` (keyed float/string params + a generic `rows` table used for asset pools and curve control points), `ProcLink` (typed pins), `ProcGraph` (nodes/links/seed/overrides/bakedHash), `ProcOverride` (a manual per-instance edit bound to a point's stable key) and `procNodeTypes()` - the 23-entry registry whose `.desc` is the node's documentation (add-menu tooltip + hover), plus `validate`/`linkError` (type mismatch, cycles, missing inputs). Also `procObjectProps()`: the list of properties the **Object Settings** node can put on every object a bake generates (mesh LOD distance, baked lighting, reflections) - a row stores its property by KEY, so that list is append-only, and its twin is `applySettings` in procbake.cpp (offer a property there and not here and the switch does nothing). Deliberately NOT in it are the four fields Output owns (draw distance, cast shadow, collision, layer). The graph lives on a `Scatter` scene object (`SceneObject::procGraph`) - **the UI calls that object a "Procedural volume"**; the enum and the serialized key stay `scatter` because they are file format, and naming the region after one of its source nodes is what made users read it as a choice of method. Per object, so undoable and collaboration-ready for free. Data only - no evaluation, no GL, no ImGui. |
| `procgen.cpp/.hpp` | ~1100 | **The evaluator** - host-only, the decalproj/aobake/navmesh pattern: one deterministic function of (project, scene, volume, graph). Built around three properties, and every change must preserve them: DETERMINISM (`rand01(seed, nodeId, pointKey, channel)`, never a running counter - so an unconnected node elsewhere cannot reshuffle the result), PREFIX STABILITY (generators emit a fixed Halton sequence and density picks a PREFIX, which is what makes progressive preview honest AND keeps manual overrides attached to their instances), CACHING (`Cache` = per-node memo keyed on params + input hashes + `Options::contextSerial`). `bakeHash` is the staleness key; **it quantizes floats to the SIX SIGNIFICANT DIGITS the `.tyra` stores** (`%.6g`) - hashing raw bits made every bake read as stale after a save/load round trip. Also `Mask`/`Curve`/`Instance` and `assetMesh` (cached .obj triangle soup). The **Repeat** nodes (Array / Radial Array) are the analytic half: they multiply their input, so each copy's identity is `copyKey(node, sourceKey, i)` (an override must stay attached to "copy 7 of that point"), they do NOT thin by `Options::fraction` - a preview that dropped copies would lie about an exact count - and they stop at `kMaxRepeatOut` with a warning rather than eating the frame. |
| `procbake.cpp/.hpp` | ~450 | **The bake**: instances -> ordinary static geometry. Merges the instances of one asset inside one world chunk into a single mesh written as `res/models/<dir>/procgen-<vol8>-<asset>-x<i>z<j>.obj` (in the SOURCE asset's folder, so its `mtllib` line resolves unchanged) and reconciles one Model scene object per chunk (matched by NAME so ids/live-link identity survive a re-bake; a fresh chunk gets `project::newObjectId()` - an object with an empty id is written to `objects/.json` and lost). `applySettings` puts the graph's Object Settings rows on every chunk object AFTER the fixed fields (those are defaults, the node is the explicit statement) - its twin is `procObjectProps()` in procgraph.cpp. `estimate` is the live budget readout, `anyStale`/`bakeAll` the build hook (`App::projectForBuild` + `bakeProcedural` in main.cpp - it MUTATES the model, which is why it cannot live in const `refreshGenerated`), `clearVolume` the cleanup on delete. Optional source decimation via meshlod (`Output`'s Instance detail). |
| `procrt.cpp/.hpp` | ~900 | **Runtime procedural generation** (docs/procedural-runtime.md) - the half of a Procedural volume that does NOT bake: `ProcGraph::runtime` compiles the graph into the game (`src/gen/procedural.gen.cpp`) and the EE evaluates it, so the world can differ every boot and no geometry ships. Host-only, no GL, no templates.cpp dependency - the livelogic.cpp arrangement, and for the same reason. **`kRuntimeNodes` is ONE table read by both `capability()` and the emitter**, so the window can never promise a node the compiler cannot produce; a graph with an unsupported node is named with its reason under the budget bar and codegen refuses to emit it. The emitted evaluator is a numeric TWIN of procgen.cpp (same mix64/Halton/channels) - that is what makes a runtime volume previewable at all, so a change to either side is a change to both. Two structural rules: points live in ONE growing buffer and every node returns the `[begin, end)` range it produced, always ending at `count` (which is what makes a filter's in-place compaction and a Merge's plain concatenation correct with no allocation); and a node feeding two consumers is EMITTED TWICE - that is the dataflow meaning, not a bug, hence `emitSeq` in the generated variable names. `volumeIndexOf` is the only correct way to name a volume from codegen (skipped volumes are not in the emitted table). |
| `prefab.cpp/.hpp` | ~200 | **Prefabs** (docs/prefabs.md): reusable groups of scene objects with their flow graphs. The `Prefab` STRUCT lives in project.hpp (a member is a `SceneObject` and nothing lighter - a prefab is a piece of scene); this is the verbs plus the one predicate the runtime story hangs on, **`memberMerges`** - plain static geometry folds into the instance's shared bag (one submit for the lot), anything with an identity something can address takes a clone-pool slot. That predicate is computed HERE and baked into `PREFAB_MERGE`, so the editor's cost readout and the console cannot disagree. `capture` puts the origin at the selection's footprint centre at its LOWEST point (placement is a ground click); `instantiate` is a yaw + a translation, ids left empty for `ensureObjectIds`. `referencedBy` is the single source for both the codegen asset scan and the window's "used by" list. `instantiate` also stamps `SceneObject::prefabSource` (the prefab's NAME) - editor bookkeeping nothing downstream reads, but it is what lets the outliner fold an instance into one node instead of twenty rows, so `capture` must CLEAR it and `rename` must retarget it like any other reference. Host-only, no GL - harness-testable like placement/decalproj. |
| `aobake.cpp/.hpp` | ~700 | **Baked ambient occlusion / contact shadows** (docs/ambient-occlusion.md). Host-only, no GL - the decalproj pattern. The occlusion ships as **per-pixel AO textures** drawn as extra alpha-blended passes (black RGBA32 texture + GS alpha-over = exact per-pixel multiply; **palettized alpha loses the gradient in the engine's tRNS→CLUT path — keep these RGBA32**, both capped 256²): `terrainAOMap` (heightmap horizon scan + occluder contact per texel → `.res-baked/aomap/`; its RGB carries the terrain's baked emissive light, drawn as a second, additive chunk pass — `SCENE_AO_MAP_OCC`/`SCENE_AO_MAP_LIT` say which channels exist) and `bakeSceneAoAtlas` (per-scene primitive lightmap atlas → `.res-baked/aoatlas/` + UV rects in `ao_data.gen.hpp`; regions mirror the builders' UV layouts - box 6/sphere 1/cylinder 3/cone 2/plane 2 - and the generated pushVert emits atlas STs via `g_aoRegion`). `collectOccluders` (solid `castShadow` objects → oriented-box/sphere shapes) is the single source for codegen AND the viewport shader uniforms; its Model-AABB callback must stay **GL-free** (the viewport passes `Viewport::modelBounds`, NOT `modelDraw` - reading bounds should never upload meshes/textures mid-frame); **casting is per object** (`SceneObject::castShadow`, Properties > Cast shadow, in liveLinkRecipeHash), receiving is automatic. **An object that does not exist at runtime must not bake anything** - `collectOccluders`, `collectEmitters` and the atlas loop all skip `scrollsim::memberTemplateFlags` (an endless scroller's member templates are deactivated in the game and replaced by sliding clones). Missing that put a permanent dark patch at the belt origin, a contact shadow cast by objects the player can never see - and it is invisible in the editor preview, because the viewport draws the templates. Objects whose atlas comes out fully lit are dropped (stay batchable); covered objects render solo - texbake, ao_data.gen.hpp and the scene-table batching bit all reuse the SAME deterministic bake. Spawned clones/physics receive via a per-vertex fallback (`aoShadeMul` twin: generated game ⇄ viewport `aoOcclusion` ⇄ host `occluderOcclusionAt` - change one, change all). **Model receive/self-AO is disabled** (g_aoOff for type 5; `modelAO` + the `.aov` sidecar + the LeanObjLoader reader stay parked for a future lightmap unwrap). Settings `aoEnabled/aoStrength/aoRadius` on ProjectSettings + AmbiencePreset (Ambience Editor). |
| `uvunwrap.cpp/.hpp` | ~380 | **Automatic UV unwrap** (Material Editor > UV check > "Unwrap UVs..."). Smart-project: normal-clustered charts (angle threshold, BFS over shared edges), per-chart planar projection + tightest-bbox rotation, ONE global scale (uniform texel density), shelf packing with a bleed margin. Deterministic; shared core + two fronts: `unwrapObjFile` rewrites a static .obj IN PLACE preserving every non-vt line byte-for-byte, `unwrapTriangles` unwraps a flat soup (position-welded) for ANIMATED models - the editor writes a `<model>.uvs` sidecar ("TXUV", per part: material name + corner UVs) that `animimport::bake` AND `animimport::parseSkel` fold in (fbxparser.cpp), so previews, matbake, and the shipped .tskl + its LODs all see the replacement; parts match by material name + vertex count (stale sidecars self-ignore), texbake treats .uvs as editor-only. The UI invalidates viewport/model/bake caches and auto-runs the validator after. |
| `texatlas.cpp/.hpp` | ~280 | **Texture atlasing plan** (docs/texture-atlasing.md): `ProjectSettings::textureAtlas` packs small (<=128) clamp-safe map_Kd textures into shared 256x256 pages. This module computes the DETERMINISTIC plan (eligibility scan with real model UV bounds via objparser, dir-grouped shelf packing, 2px gutters) consumed by BOTH texbake (composites pages, rewrites baked .mtl with `# tyra-uvrect`, skips members) and templates.cpp (TEXTURE_ATLAS_INFO boot line) - the aobake single-source pattern. Runtime: LeanObjLoader parses the hint (models remap at load; `LeanMtlMaterial::uvRect` -> GameMaterial -> `g_primUvRect` multiply in pushVert for primitives). Exclusions: terrain (tiling), emitters (VU1 UVs), decals/mirrors/portals, refl maps, textureQuality-pinned assets, cross-dir refs. Pages quantize as ONE image (shared 256-color CLUT when palettized). |
| `bvh.cpp/.hpp` | ~200 | **The flat binned-SAH BVH**, shared by the two raytracing bakers - matbake (one model's own surface detail) and gibake (a whole scene). It lived in matbake's anonymous namespace until the scene bake needed the identical traversal over a much bigger soup; a second copy would have meant two subtly different answers to one question. Host-only, no GL, no project.hpp. |
| `gibake.cpp/.hpp` | ~1000 | **Baked global illumination + light probes** (docs/global-illumination.md). Host-only, no GL - the aobake/decalproj pattern. Tessellates a scene into real triangles (primitives via primmesh, static .obj via objparser, terrain as a heightfield) with per-triangle albedo/emission, then runs ONE hemisphere gather over sky + sun + emissive area lights + baked point lights, iterated for bounces. **It owns almost nothing of the delivery path**: the lightmap atlas, its region packing, the codegen tables and texbake all already existed - GI plugs in through `aobake::LightFn` and REPLACES what the atlas's RGB channel means (from "baked emissive light" to "incoming light, all sources, all bounces"), because at 256^2 RGBA32 = 19% of GS VRAM the image cannot grow. Also bakes the **L1 SH probe grid** (12 B + 1 liveness byte per probe) that lights everything a lightmap cannot: models, textured surfaces, physics bodies, spawn clones, characters. Deterministic like matbake (per-element seeded spiral, thread-count independent - the only thing that makes A/B possible). The bake is **explicit and cached** in `.res-baked/gi/`, keyed by a signature over the scene + settings + the CONTENT of every file it reads (never mtimes: a bake takes minutes, and the example ships its cache). Codegen/texbake/viewport only READ that cache; a stale one falls the whole scene back to the pre-GI bake together. `Baker` = the matbake progressive-worker idiom; `--bake-gi` is the headless twin of the UI, which lives on the **Global illumination** tab of the Ambience Editor (`App::drawGiBakeSection`) - that window already owns a scene's light. `App::giBakerPoll` runs every frame from `drawUI` and NOT from any window body: a finished bake has to reach the viewport whether or not the tab is open. |
| `matbake.cpp/.hpp` | ~900 | **UV-space raytraced map baker** for the Material Editor (docs/material-baking.md). Host-only, no GL. Conservative UV rasterization → per-texel surface samples, flat binned-SAH BVH, cosine golden-spiral hemisphere rays (seeded per-texel rotation - deterministic, bit-identical at any core count); one pass yields AO + bent normal + thickness + curvature + position + OS-normal maps; optional high-poly cage projection along smoothed low normals; flood dilate. `matbake::Baker` = progressive worker-thread bake (growing rounds, snapshot()/version() polling, gbuffer+BVH cache keyed by MeshInput::signature so sampling-only slider drags re-bake nearly free). Distinct from `aobake` (scene/terrain occlusion): this bakes ONE model's own surface detail into its texture. |
| `decalproj.cpp/.hpp` | ~230 | **Projected-decal geometry** (host-only, no GL). `project(Project, SceneData, decal)` clips the receiver triangles (terrain + overlapping objects, auto) against the decal's oriented unit-cube projector, computes projected UVs and a surface-normal offset, and returns a world-space triangle list. Used by the viewport (live preview) AND codegen (`decalDataHeader` bakes it into `inc/decal_data.gen.hpp`); the game just draws it — **no projection/clipping on the PS2 EE**. See PROGRESS (99). |
| `placement.cpp/.hpp` | ~140 | **Collision-aware object placement** (docs/object-placement.md). Host-only, no GL - the decalproj/navmesh pattern. `worldAabb` (rotated+scaled unit primitive, or a model's own bounds via the shared `aobake::ModelAabbFn`), `isSupport` (which types are something to rest ON: solid primitives/save points/models with `collisionMode != 2`) and `restOffsetY`/`restOffsetYGroup` - the vertical offset that rests an object on the highest surface under its FOOTPRINT (terrain sampled at corners+center, plus overlapping objects' AABB tops). The `ceilingY` argument is the whole behavioral switch: `FLT_MAX` = insert/paste ("stack on whatever is under it"), the object's own underside = the `End` drop-to-floor ("nothing may lift it"). Deliberately NOT a collision solver - no sweep, no penetration resolve. Callers: `App::snapInsertedObject` (every add path), `App::movePasteStaged`, `App::dropSelectionToFloor`; the two callbacks come from the viewport (`Viewport::modelLocalBounds`, `Viewport::terrainHeight`), so the editor snaps against the same bilinear heightfield the game walks on. Harness-testable like treegen/stochtile (a 40-line host `main()` + `placement.cpp` covers every rule). |
| `navmesh.cpp/.hpp` | ~160 | **NavMesh bake** (host-only, no GL — the decalproj pattern). `bake(Project, SceneData)` rasterizes a scene into a walkable-cell grid (terrain slope on the game's own bilinear heightmap + `collidePlayer`-box-mode blockers inflated by the agent radius; capped 128×128). Used by codegen (`navDataHeader` → `inc/nav_data.gen.hpp`, gated on AI nodes existing) AND the viewport nav overlay (`App::updateNavOverlay`, signature-cached). The generated `src/gen/navigation.gen.cpp` runs A* over the bitmap on the EE and ticks all AI agents (Patrol/Chase/Flee flow nodes set agent state; one state per object). See `docs/navigation-ai.md` + PROGRESS (108). |
| `scrollsim.cpp/.hpp` | ~180 | **Endless-scroller belt math** (host-only, no GL), the single source of truth for `PrimitiveType::Scroller` (19). Given a scene's objects + a scroller object it computes the belt axis, per-segment length, pattern period, clone count and the recycle `wrapU`/placement of every segment instance at a given scroll distance. The viewport reads it for the animated ghost preview; `templates.cpp` reads it at build to bake clone rest positions + the `SCROLLERS`/`SCROLLER_CLONES`/`SCROLLER_HIDDEN` side tables. The generated `ScrollerDirector` (`scroller.gen.cpp`) is the per-frame twin — `sc_wrapU`, the cell arithmetic AND `sc_varyHash`/`cellAdjust` all mirror this file; keep in sync. **Per-cell variation** is the half that makes an endless belt stop repeating: `Placement::cell` is an index that counts along the INFINITE belt (the runtime folds its scroll accumulator for float precision, which would also make the layout eternally periodic — so it counts the folds and adds them back), and `memberVary`/`cellAdjust` hash it into each member's presence, yaw, lateral offset and scale. Two rules if you extend it: derive everything from that hash rather than from any running state (a clone resolves its look once per recycle, and the editor preview must predict the console exactly), and **anything that bakes the scene as authored must skip belt members** — `memberTemplateFlags` is that predicate, and aobake ignoring it burned the members' contact shadow into the terrain AO map, leaving a permanent dark patch at the belt origin cast by objects the player never sees. See `docs/endless-scroller.md`. |
| `history.hpp` | 59 | Undo/redo snapshot stack. |
| `gl_loader.h/.cpp` | 137 | Minimal hand-rolled GL 3.3 loader (only what the viewport needs). |

`examples/script-demo/` is a complete generated project checked into the repo.
Its generated files are only as fresh as the last time someone rebuilt it — if
codegen changed since, they drift silently. Regenerate (load + save +
`refreshGenerated`, or a `--build`) before trusting it as a reference for what
`templates.cpp` emits today.

## The rules that keep the system consistent

### 1. Editing model: mutate, then `commitChange()`
UI code mutates `project_` freely; one logical user action ends with a single
`commitChange()`, which pushes an undo snapshot and marks the project dirty.
**There is no autosave** — the bytes reach disk only on an explicit Save
(Ctrl+S / File > Save / the toolbar button). If you add an editable property
and skip the commit, undo/redo and the dirty flag silently break.
**Collaboration corollary:** the live-session sync detects edits through
`modelEditSerial_`, bumped only in `commitChange()`, `applySnapshot()` and
`setDirty(true)`. A mutation path that avoids all three (writes project state
but never dirties) will save fine locally and **silently never reach session
peers** — route new edit paths through commitChange/setDirty like everything
else.

**`commitChange()` is the one verb, project-wide data included.** The undo
snapshot only carries `project_.scenes`, so for a project-wide collection —
menus, credits, loading screens, splashes, the Input Map, fonts, button icons,
the HUD, save values, per-asset overrides — `history_.push()` returns false and
**no undo step appears**; the commit still dirties and still bumps the serial.
That is exactly what those panels need, and it is why committing per widget
costs nothing and cannot spam undo during a slider drag.

**Never reach for `saveAll()` from a widget.** It writes the whole project AND
the history file, then clears the dirty flag — so the toolbar save icon never
lights, the exit prompt never appears (the edit is quietly losable), every
slider release rewrites the project, the serial bump is skipped, and whatever
else the user had pending is silently persisted too. `saveAll()` is for an
explicit save **command** (Ctrl+S, File > Save, the toolbar button, the discard
modal, "Layout saved") or for an action whose file-system side effect the model
must match on disk (asset import). Nothing else. This was settled repo-wide
after the Save Editor, the Credits Editor, Loading Screens, the Animation
Editor, the insert-object presets and the per-asset LOD/quality popups had each
grown their own answer; `credits_ui.cpp` had a file header documenting the
opposite rule, which is how the two conventions survived side by side.

**A hand-set `bool changed` is not enough on its own.** It is the usual trigger
accumulated over a window body, but the next widget someone adds forgets to set
it — that is precisely how the Menu Editor left the icon dark for titles,
colours, sizes and images. A window that owns a `project::Section` pairs the
flag with a comparison of `project::sectionJson()` taken across the whole body:

```cpp
const std::string before = project::sectionJson(project_, project::Section::Credits);
... window body ...
if (changed || project::sectionJson(project_, project::Section::Credits) != before)
    commitChange();
```

A window with early returns wraps that in a local `commitIfEdited` lambda and
calls it at each exit (`drawLoadingScreenWindow`, `drawCreditsWindow`,
`drawGradingWindow`, `drawCutsceneWindow`); a window spanning two sections
concatenates both blobs. Take the `before` snapshot **after** any repair the
window does on entry (`if (fonts.empty()) push_back`), or merely opening the
panel reads as an edit. **Every panel that owns a project-wide section now
carries this guard** — Save Editor, Menu Editor, Credits, Loading Screens +
Splash, UI Editor, button icons, Font Manager, Input Map, Animation Editor,
Color Grading, Ambience, Cutscene Director, Prefabs — so a new one is expected
to, and a new panel should copy the nearest of them rather than invent a third
answer. After the sweep the ONLY `saveAll()` call sites left in `src/` are the
five save commands and the three asset imports (music, sfx, HUD image) plus the
Drone Generator's render, which writes a WAV and registers it — if you are
adding a ninth, you are almost certainly wrong.

**View state is not an edit.** The render mode, the projection and the active
scene are read off the viewport by `saveProject()` at save time — they neither
dirty the project nor write to disk (`setViewProjection` is the reference).
Editor state that IS stored in the `.tyra` but has no undo meaning — window
layouts, debugger breakpoints — marks dirty directly with `setDirty(true)`.

### 2. Generated-file ownership markers
`project::refreshGenerated()` (project.cpp:914) runs at the start of every build
and decides per file:
- **Always overwritten** (first line `// Generated by TyraX. Do not edit -
  regenerated on every build.`): `docker-compose.yml`,
  `inc/scene_data.hpp`, `inc/terrain_config.hpp`, all `*.gen.hpp` / `*.gen.cpp`.
- **User-ownable** (first line `// Generated by TyraX. Delete this line to
  take ownership of this file.`): `src/terrain_game.cpp`, `inc/terrain_game.hpp`,
  `inc/controls.hpp`, `inc/scripts/script.hpp`. Regenerated only while the marker
  line is intact; the user deletes the line to take over.
- **Written if missing** (no marker — the formats have no room for a comment
  line): `.vscode/extensions.json`, `THIRD-PARTY-NOTICES.txt`. Created once and
  then never touched, so an existing project picks the file up on its next build
  while anything the user added to it survives. This is the right category for
  content that is static, user-extendable, and wrong to clobber — the notices
  file is the attribution a shipped game carries (see LICENSE-EXCEPTION.md), and
  authors are expected to append their own credits to it.

**"Rewritten" means "rewritten when the bytes differ".** `writeFile` compares
the content first and skips an identical write, which is not a micro-optimization
but the thing that makes a game build incremental at all: this list is rewritten
at the start of EVERY build, and a fresh mtime on `scene_data.hpp` recompiles
most of the game. So do not "simplify" that check away, and be careful with any
new generator that embeds something volatile (a timestamp, a random id, an
unordered container's iteration order) — a file that differs on every run reads
as a real edit and puts the full rebuild back. **The same applies to the binary
bakes**, which are written by their own code in `refreshGenerated` rather than
through `templates::generate()`: `res/save/list.icn` was going out through a raw
`ofstream`, so the biggest asset the editor bakes (tens of KB) got a fresh mtime
on every build from unchanged project data. Route a new bake through `writeFile`
too — take the bytes as a `std::string` and the comparison comes for free.

**A baked asset folder needs a `res/.gitignore` rule AND a migration.** Adding
the rule to `TPL_RES_GITIGNORE` only covers projects created *afterwards*; every
existing project keeps its old file and starts tracking the derived bytes. The
end of `refreshGenerated` has the append-if-missing block that fixes that
(`/models/*.tmdl`, `/credits/pages/`, `/save/` all arrived this way) — add a
paragraph there in the same commit. Note the distinction it encodes: `res/save/`
is ignored because it is rebaked every build, while `res/hud/save-busy.png` is
written *only when missing* and therefore stays tracked and user-replaceable.

**"Always overwritten" is a hand-written LIST inside `refreshGenerated`, not a
rule about the suffix.** A new generated file added to `templates::generate()`
and to nothing else is written **once, by `project::create`**, and then never
refreshed again - so it keeps whatever the project's settings were at creation
while every file around it follows edits. That is very hard to see, because the
file looks perfectly generated: the Remote Pad's `live_pad.gen.cpp` shipped the
full devkit runtime into a **release** build for exactly this reason, and the
only thing that caught it was `--audit-release` (which is itself the argument for
running the negative test). Add the path to that list in the same commit as the
generator.

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

**A project-wide field that INDEXES THE SCENE LIST** (`Project::startScene` is
the worked example) has four sites beyond the usual chain, all of them about the
index going stale rather than about the value itself. (1) It travels in
`writeScenesTable`/`applyScenesLayout`, NOT a `Section` — the collaboration wire
sends the scene layout as one message, and an index that arrives without the
list it indexes is meaningless. (2) It needs a clamp helper called from BOTH
`project::load` and `applyScenesLayout` (`clampStartScene`), because a
hand-edited `.tyra` and a peer with a different scene list are the same bug. (3)
`scenes.erase` in app.cpp must shift it (`> deleted` decrements, `== deleted`
falls back), the way `activeScene` is already fixed up — scene indices are baked
into every generated table, so scenes are never REORDERED and delete is the only
motion to handle. (4) Codegen clamps again on the way out, because
`inc/scene_data.hpp` is read by C++ that will walk off the array rather than
show a wrong number. Omit it from the JSON at its default so existing projects
don't change shape.

And the part that cost the most: **the generated game's boot path had scene 0
baked into it in places that are not a `loadScene` call** - the built-in FPP
player was positioned from scene 0's spawn point in `init()` (which runs before
the deferred boot load) and the boot loading screen was scene 0's. When you make
something that was always 0 configurable, grep the generated template for the
OTHER uses of that 0, not just the obvious one - and remember the game .cpp is
assembled from an ORBIT or FPP head around a shared middle, so a loop-level fix
usually has two homes.

**An asset path the GAME will open must be `lexically_normal()`.** The PS2
cannot walk `..`, and a Wavefront reference is resolved relative to the file
that named it — so joining a `.mtl`'s folder with its `map_Kd` yields
`materials/../textures/x.png` unless you normalize. PCSX2's `host:` fs resolves
that through the OS, so the bug is **invisible in the emulator and black on
hardware** (PROGRESS 199, `project::resolveTerrainMaterial`). The bake copies
files to their normalized location, so normalizing is also what keeps codegen
and `bin/` agreeing.

**Any new field that stores an asset path** (a `res/...` file: a model, a
material, a texture, a WAV, a TTF) must join **`App::retargetAssetPath`**
(assetbrowser.cpp), or the Asset Browser's move/rename breaks it silently -
including a map KEYED by an asset path (`textureQuality`, `modelLods`,
`modelUnitMeters`, `musicBuild`) and even the editor's own staged paths. If the
field is a real *reference* (something uses the file) it also joins
**`App::rebuildAssetUsage`**; a per-asset *setting* (quality, recorded
real-world size, clip edits) deliberately does NOT, or no imported asset would
ever read as unused. Both are single flat walks over the model - one line each.
The "asset path" test is whether a *file* is named; a name-keyed reference (a
font entry, a menu, a sequence) is not one.

**Runtime-generated geometry** (`TerrainGame::ProcChunk`, docs/prefabs.md +
docs/procedural-runtime.md) is the third geometry path, next to solo objects
and static batches: world-space vertex bags the GAME built, from a runtime
procedural volume or a prefab instance. Three things to respect. (1) The merge
is not an optimization, it IS the feature - a PS2 submit costs ~1 ms flat, so
neither "500 scattered cubes" nor "27 prefab rooms" can exist as objects; a
prefab spawned BY A VOLUME merges into that volume's chunk grid (owner >= 0)
while a flow-node spawn keeps its own bags, because Despawn Prefab must be able
to remove one instance. (2) Merged geometry has no objects behind it, so
collision comes from `procColliders` - one conservative world AABB per merged
member with collision, tested in `collidePlayer` with a cheap distance reject
(without it the cube example measured 47 FPS instead of 50). (3) Generated
geometry gets NO lightmap region, no static-batch membership and no scene-table
entry - it is lit from the probe grid and nothing can address it. A new
per-object visual feature therefore has to be staged explicitly in
`procAddMergedObject`, which states the `g_*` globals rather than inheriting
whatever the last rebuild left set.

**Static batching invariants** (the generated game merges non-moving
primitives into combined bags — `staticBatchEligible`/`batchBlockedNames` in
templates.cpp, `buildStaticBatchList`/`rebuildStaticBatch` in the game
template): (1) any runtime code path that mutates a rendered object property
must set `RuntimeObject::dirty` — that flag is what demotes a batched member
back to its own bag, and a mutation without it silently doesn't render
(visibility flips are the one exception, caught by a snapshot); (2) a new
*reference kind* that can move/hide/re-submit objects at runtime (the way
sequences and mirror target lists do) must be added to `batchBlockedNames()`
— flow nodes are covered generically via `strKind == ObjectName`; (3) a new
exclusion-worthy per-object property (a new special draw path, a new
streaming mechanism) must be added to `staticBatchEligible()`.

**`dirty` is a re-bake, so per-frame motion must not go through a graph.**
Setting `RuntimeObject::dirty` makes `renderScene` rebuild that object's whole
**world-space** vertex array on the EE. That is correct for a one-shot (Move /
Rotate / Set Object Position, a recolor) and ruinous per frame per object — so
a feature that moves something *continuously* belongs in a game-loop pass with
the **matrix fast path**, not in a flow node fired every frame:
`rebuildObjectGeometry(i, /*localSpace=*/true)` ONCE (gated on
`physFastPathEligible`) bakes local-space vertices, and from then on
`updateObjMat` refreshes `objectGeometry[i].objMat` and VU1 applies the motion —
the object's entire per-frame render cost. Built for physics bodies (PROGRESS
116), now also `updateSpinners()` (the Spin Object node, PROGRESS 222): the
node writes only a RATE onto the RuntimeObject and the loop integrates it.
Ineligible objects (usable, reflective, animated models) must fall back to
`dirty`. The inherited trade-off: baked shading freezes at the pose the object
was promoted in — fine for something permanently in motion, wrong for a prop
that moves once.
**A generated SCRIPT asks for the same path through the RuntimeObject**, because
a script has no access to `objectGeometry`: it sets `wantsMatrixPath` and reads
`onMatrixPath` (the Script-visible mirror of `ObjectGeometry::matrixMode`, kept
in sync by `rebuildObjectGeometry`), and `renderScene` does the promotion — the
endless scroller's clones are the users. Two things that arrangement got wrong
first: the promotion must be checked BEFORE `dirty` (such an object dirties
itself on the frame it asks, and a world-space rebuild would clear the flag and
leave it asking forever), and the only thing that still needs a `dirty` re-bake
is a SCALE change, because scale is baked into the local vertices. Worth 16 →
50 FPS on examples/endless-runner.

**New object type** → `PrimitiveType` enum (0–19 used so far, `kPrimitiveTypeCount`
bounds "every type" loops; keep values stable, they're serialized) →
mesh/marker in viewport.cpp → insert menu in app.cpp →
codegen + runtime as above. If the type needs per-object variable-length data
(like Mirror's reflected-object list), don't grow the fixed `SceneObjectData`
POD — emit a flat side table into scene_data.hpp keyed by (scene, object), the
`OBJECT_SCRIPT_ATTACHES` / `MIRRORS` / `SCROLLERS` pattern. **A type with no geometry must be
added to every marker skip list**, and they are scattered by *number*, not by
enum: `collidePlayer`, the USE-target scan, the carry/throw sweep,
`physObstacle` and the geometry `switch` in templates.cpp (all `o.data.type ==
N` lists), `flowRaycast` in `flowGraphScript`, plus `blocksNavigation`
(navmesh.cpp) and `objectShape`/`regionCountFor` (aobake.cpp, whose `default`
already excludes unknown types). Miss one and an invisible marker blocks the
player or eats a raycast.

**Areas (`PrimitiveType::Area`, docs/areas.md)** are the reference point for
"replace a hand-typed distance with a placed volume". The pattern: the volume
is an ordinary `SceneObject` (transform = the box), references to it are BY
NAME (`SceneObject::catchArea`, `SceneLayer::streamArea`, a flow node's `str`
with `FlowParamKind::AreaName`), and the point test lives in exactly two
places — `project::areaContainsPoint` (host: editor previews AND codegen) and
`pointInArea` emitted into `scene_data.hpp` (both generated TUs: the game cpp's
layer zones and flow_graph.gen.cpp's In Area trigger). Putting the runtime
twin in the generated DATA header instead of a game-cpp template is what keeps
it a single definition; `project::areaCaughtObjects` is likewise the ONE
expansion used by the Properties preview, the viewport mirror preview, the
baked target tables and `batchBlockedNames`. If you add a consumer, call those
— do not re-derive the box math.

A catch area can also be **live** (`SceneObject::catchAreaLive`): the volume is
re-tested every frame instead of only at build. The rule that makes it cheap
and safe is worth reusing if you add another "re-submit these objects" feature:
only `project::areaLiveCandidates` — objects that can move — is re-tested, and
that predicate (`project::objectRuntimeMovable`, over
`project::runtimeRefNames`) is the exact complement of the immovability
`staticBatchEligible` relies on, so a live candidate always has the solo bag a
second submission needs. The immovable rest stays baked in the fixed list, and
movable objects are dropped FROM that list so nothing is submitted twice. The
candidates bake into a shared `CATCH_CANDIDATES` table sliced per owner
(`liveArea`/`firstCand`/`candCount` on `MirrorData`/`PortalData`/`CamFeedData`);
`TerrainGame::collectLiveCaught` walks a slice plus the spawn pool.

An area can also be a **reverb zone** (docs/reverb.md): the SPU2's hardware
reverb, authored as a room instead of as a number. It follows the same pattern
— a `REVERB_ZONES` side table keyed by (scene, object) with the BOX left
unbaked, so `TerrainGame::updateReverb` reads the live transform through
`pointInArea` like every other consumer. What differs, and what to know before
extending it: this is not a per-owner question but a single global decision made
once per frame — the listener is in exactly one room (highest `priority` inside
wins). The console has **two** reverb units, one per SPU2 core, and
`updateReverb` cross-fades rooms across them: the incoming room takes the free
unit while that unit is silent (switching the algorithm zeroes its work area in
SPU2 RAM), then both depths ramp. The per-sound control is a BIT
(`SceneObject::soundReverb`, the Play Sound node's `Dry` param) rather than an
amount, because the hardware has no per-voice wet level.
**The obligation that falls on ANY new code that plays a sound**: a unit is
reachable only by voices on its own core, so a voice is committed to a room the
moment it starts, and every play site must offset its channel by
`ScriptContext::reverbBusBase` (0 = core 1, 24 = core 0). The emitter path and
the Play Sound node do; a site that forgets is silently heard in the room the
listener just left. Anything caching per-channel state needs the bus in its key
for the same reason — `sndChBus` beside `sndChVol`/`sndChPan` is that fix.
`reverbPresets()` in flowgraph.hpp is the single preset table — read by the Area
combo, the Set Reverb node and codegen — and its ORDER IS THE WIRE FORMAT: it is
`Tyra::AudioReverb::Preset`, i.e. libsd's `SD_EFFECT_MODE_*`, so append only.
**A second obligation, and the same shape: the voices are FINITE** (24 per bus,
16 for Play Sound and 8 for the emitters — docs/sound.md). Who keeps one when
they are all busy is decided in exactly two places, and a new play site must go
through one of them rather than picking a channel itself: `pickSoundSlots` in
the emitter loop (a per-frame ranking, priority then loudness, with a steal
margin so near-equal ambiences do not trade a channel every frame and retrigger
each other) and `flowPickSfxChannel` in the generated flow-graph TU (an ended
voice, else the lowest priority strictly below, else the sound is DROPPED — and
a drop must stay unlogged, it is the feature working). The runtime table there
is per bus and resets when the room moves to the other core: the outgoing bus's
voices belong to the room the player just left and must not be stolen from.
**A type whose data drives OTHER baked objects** is the heaviest kind of new
type. `Scroller` (19) is the reference: codegen APPENDS clone objects to the
scene table (authored indices must never shift, or every flow graph / mirror /
player reference silently retargets) and emits a generated director script that
repositions them each frame. Mirror the `scrollsim.cpp` shared-host /
`scroller.gen.cpp` per-frame-twin split so the editor preview and the PS2
runtime compute the same layout from one source of truth — and keep any formula
duplicated in the generated twin flagged as such in both files.

**Object identity: `SceneObject::id`.** Every object carries an opaque, stable
`id` (first JSON key; part of `operator==`) — the merge/persistence key for the
multi-user file format. It is *not* a user field and never reaches codegen
(references still resolve by name). `project::ensureObjectIds()` stamps a fresh
unique id on any object that lacks one and reissues duplicates; it runs in
`create`, at the end of `load`, and in `commitChange()`. **When you add a code
path that clones an existing object** (like `pasteObject`), clear the copy's
`id` so it gets its own identity — otherwise two objects share one id. Freshly
default-constructed objects need nothing (empty id → `ensureObjectIds` fills
it). Migrate/round-trip `.tyra`-format changes headlessly with `--resave` (see
tyra-testing).

**Any change to what `project::save()` writes** (new field included) →
`version::kFormatVersion++` in version.hpp, so an older editor refuses the
newer file instead of silently dropping the field on its next save. Keep the
read tolerant (default it) as always; ADDITIONALLY register a step in
migrations.cpp only when existing files need active transformation (rename,
unit/semantic change, moved data) — that is what triggers the editor's
backup-and-migrate prompt. Bump the editor semver in the same PR (feature →
MINOR, fix → PATCH). See `docs/format-versioning.md`.

**New procedural node** (docs/procedural-generation.md) → an entry in
`procNodeTypes()` (procgraph.cpp: pins, params with UI ranges, `.rows` kind,
mandatory `.desc`, **and a `.tip` on every parameter** - the node's tooltip is
`.desc` followed by one line per control, so a knob without a tip is the half of
the documentation the reader is actually looking at) → a branch in `evalNode`
(procgen.cpp) → **decide whether it
can run at RUNTIME too** (docs/procedural-runtime.md): if it can, add it to
`kRuntimeNodes` AND write its `emit*` in procrt.cpp - the table is what the
capability check reads, so a node listed without an emitter is a promise the
compiler cannot keep; if it cannot, leave it out and it is reported honestly.
Otherwise nothing else: the window renders params from the registry, and the
bake only sees the Output.
Keep the three properties above intact - in particular derive randomness from
the point key, and make a generator's count a PREFIX of its sequence rather
than a reseed. A runtime emitter must reproduce those numbers EXACTLY (same
hash, same channel indices): the editor preview is the only way to author a
runtime volume, and it is only useful while it predicts the console. A node that reads the scene (objects, terrain) must have its
inputs covered by `bakeHash`, or a stale bake will not be noticed.

**New flow-graph node** → node kind in flowgraph.hpp (designated-initializer
entry; **`.category` is the add-menu submenu and the list is derived from the
registry by `flowNodeCategories()`, so a new category costs nothing but a
string** - `Procedural` is the home of the nodes that CREATE content while the
game runs (Spawn/Despawn Prefab, Generate Volume), pulled out of `Object`
because that one is the most crowded menu there is; **`.desc` is mandatory by
convention — and so, in spirit, is a tip on every parameter the node declares**
(`.numTips[i]`, `.strTip`, `.str2Tip`, and `.execInTips[i]` on a node with
several exec pins). All five are the node's documentation, read by the same
three consumers — add-menu tooltip, node-hover tooltip AND the AI generator's
catalog line (`nodeCatalogLine` in aigen.cpp) — so a node that fills them in is
documented everywhere at once, including for the AI. The split is what makes
the tooltips usable: **`.desc` says what the NODE does and why you would reach
for it; a tip says what that ONE knob does.** Parameter prose written into
`.desc` instead means hovering the node buries the answer in a paragraph and
hovering the parameter gives nothing, which is the state PROGRESS (247) fixed
across all 186 entries. A trap about one parameter belongs in that parameter's
tip; a trap about the node as a whole stays in `.desc`. Never restate a raw slot
name (`num[0]`, `str`) in prose — address a parameter by the label its widget
carries, which `flowStrLabel`/`numLabels` are the single source of) → node UI (pins, params)
in the flow-graph editor in app.cpp → codegen in `flowGraphScript()`
(templates.cpp), which compiles graphs to `src/gen/flow_graph.gen.cpp` — one
script class per object graph; object references resolve to indices at codegen;
bool logic folds into inline C++ expressions.

**A value a graph computes** rides the **number plane** (`FlowLinkNum`,
`numIn`/`numOut`): a wired number REPLACES the target's `num[0]`, one
convention for every consumer, mirroring `posIn` over X/Y/Z. Codegen resolves
it to a self-contained float C++ expression (`numExprImpl` / `numOperand` in
`flowGraphScript`, the bool-plane shape) so a value needs no runtime slot;
`flowNumFolds()` is the single predicate deciding whether an input folds over
every link or takes only the first, read by the editor's link pruning AND by
codegen — and it reads two **declared** flags rather than inferring anything:
`numFold` (n-ary: Add, Min, Modulo) and `numInExtra` ("the wire is an operand of
its own, num[0] is a separate param" — Clamp's value between its Min/Max, Number
At Least's value against its Threshold). Both exist because inference was wrong:
`pure && numIn && numOut` is true of a unary Sine too, and the editor's "num[0]
came from the link" notice was a lie on every At-Least node. Two things a new
number consumer must do: read `numOperand(n)` instead of `n.num[0]`, and accept
that **Live Logic cannot patch it** (`capability()` rejects any graph with a
number link — the IR carries num[] as compile-time constants).

**A value plane and the position plane can feed each other** (Get X reads a
position, With X writes one from a number), so `posExprImpl` and `numExprImpl`
are mutually recursive through the forward-declared `numInputVis`/`numOperandVis`
— and those take the **visited path**, not a fresh one. A cycle that hops planes
is invisible to either guard alone, and starting a fresh path at the boundary
recurses until the stack goes; conversely `numExprImpl` must drop **its own id**
from the path before calling `posExprImpl`, or the position walk reads the node
as visited and silently skips its own input link. Both bugs were hit building the
Vector nodes; `--refresh-gen` on a deliberate pos→num→pos cycle is the check.
One cost to know: a position is three independent C++ **expressions**, so a long
Vector chain is re-emitted once per component per consumer. Constant chains fold
away in the compiler; a chain with a wired angle really does pay its trig per
component (`PosRotateY` folds `sinf`/`cosf` at codegen time when the angle is a
typed-in constant for exactly this reason). Two things a new
*int* consumer must do: round (the plane is float, `flowInt` is not) and, if it
names variables, join BOTH copies of the collect list (`collectFlowVars` in
templates.cpp and the identical walk in livelogic.cpp) — a variable named only
by a getter still takes its index slot, and a missing entry shifts every index
after it.

**A node that decides where exec goes next** (the `Flow` category: Branch,
Sequence, Gate, Switch Number, Timer, Tween, For Loop): set `execOutCount` +
`execOutLabels` and emit each branch yourself. The branch a link LEAVES is
`FlowLink::fromPin` (serialized `"fpin": N`, omitted at 0); output 0 keeps the
original pin slot 1, outputs 1..7 take slots 18..24 (`kFlowMaxExecOut` = 8), and
**`flowExecOutCount(t)` is the one answer** to "how many outputs does this type
have" - read by the editor's pin submission, both link-validity checks (editor
AND `aigen.cpp`) and codegen. Inside `actionCode` the local `branch(outPin, pad)`
returns that output's whole chain as inline C++, so a Branch costs one `if`; each
branch walks with its OWN COPY of the visited path, because two outputs of one
Sequence may legitimately reach the same action (it then runs twice, which is
what the wiring says) while a link back into the path is still a cycle. Nodes
needing per-frame state (Timer, Tween, Cooldown) declare it through `addMember`
in the per-node state pass and tick in the `update()` prologue like Delay - never
straight into `members`, or the time machine cannot see it. **Live Logic cannot
patch a branching node**: a block is a straight instruction list, so
`capability()` rejects any graph where `flowExecOutCount > 1`, and
`livelogic.cpp`'s own exec walk filters `fromPin != 0` to stay honest with
codegen's.

**Several triggers on one node** (show/hide/toggle/add): set `execInCount` +
`execInLabels` on the `FlowNodeType` and switch on the `pin` argument in
`actionCode(n, pad, pin, visited)`. The pin a link fires is `FlowLink::toPin`
(serialized `"pin": N`, omitted at 0); pin ids come from `flowExecInPin` (slot 2
for the first, spare slots 10..15 for the rest — `kFlowMaxExecIn` = 7). Do NOT
add a Show*/Hide* *pair* of node types: that was the old convention and the five
surviving pairs were merged away (Set Object Visible / Set HUD Visible / Set
Text Visible / Set Layer Loaded / Animation). **Retiring a node type means
adding it to `flowLegacyNodes()`** — `readFlowGraph` drops unknown types
silently, so without an entry every existing project loses those nodes. Note the
merge only fits when both branches share the node's param: `Play/Stop Music` and
`Play/Stop Sequence` stayed separate because their Stop is global and would make
the field next to a "stop" pin a lie.

(If the node is project-specific
rather than a general editor feature, prefer a **custom node**: a
`flow-nodes/*.flownode` file, no C++ change — see `flownode.cpp` and
`docs/custom-flow-nodes.md`. Custom nodes plug into the same `flowNodeType()`
lookup, the add-menu via `flowAllNodeTypes()`, and a `flowCustomNode()` branch
in `actionCode()`. A `call = fn` custom node runs a user function in
`inc/scripts/flow_nodes.hpp` via the `FlowNodeIO` struct and can have any pins;
its **object output is a runtime value**, which is why `resolveTarget()` returns
a C++ int-*expression* (a literal index for built-in sources, `objOut<id>` for a
custom node's runtime output) — built-in object actions fed such a ref are
bounds-guarded by the wrapper `actionCode()` emits around the body
(`if (<dyn> >= 0 && <dyn> < ctx.objectCount) { ... }`). The built-in **Raycast** node uses the same
runtime-latch machinery: every `flowCustomNode(...)` check on that path also
accepts `type == "Raycast"` — a new built-in node with runtime outputs should
extend those same spots. The **AI nodes** (Patrol Waypoints / Chase Player /
Flee / Stop AI / On Player Seen) compile to calls into the generated
`navigation.gen.cpp` runtime — a new AI-family node usually only needs a new
`nav*` entry point there plus an `actionCode` branch; the shared per-object
agent state, movement and A* already exist. Anything that changes what blocks
walkability must update `navmesh::bake` (host) — there is no game-side twin,
the game only reads the baked bitmap.)

**Player / two-player work** (docs/multiplayer.md): the generated game's
walker state is a per-player `PlayerCtl` struct (`players[2]` in the game hpp
templates) and the walker is `updatePlayerWalker(PlayerCtl&, pi, Tyra::Pad&)`
in `TPL_GAME_CPP_SCENE` — NOT the old loose `entX/entYaw/...` members. The
scene tables come in pairs (`PLAYER_*` / `PLAYER2_*`, first/second Player
object per scene) selected via the `PP_*(pi)` macros in scene_data.hpp; a new
per-player Player-object property must be added to the paired table emitter
(one loop emits both prefixes) and read through a new `PP_` macro.
`ProjectSettings::multiplayer` ("off"/"shared"/"split") + `p2JoinOnStart`
gate everything; menu bind 7 = Player count (edge-triggered +
`syncPlayerCountMenuValue` write-back).

**Anything that reads a button.** Never emit `pad.getClicked().<Button>` for
gameplay: the generated game reads inputs through **named actions**
(docs/input-bindings.md) — `inputPressed(pad, IA_ROLE_JUMP)` /
`inputClicked(...)` from `inc/input_map.gen.hpp`, defined in
`src/gen/input_map.gen.cpp` (`inputMapHeader`/`inputMapSource` in
templates.cpp). A new built-in behavior that needs its own button adds an
`InputAction::Role` (input.hpp) → a `kSeeds` entry in
`project::ensureInputActions` (so existing projects get a default binding) →
a role slot in the `kRoles` table of `inputMapHeader` (emits `IA_ROLE_*`, -1
when the project has no such action) → the read site. The three layers the
runtime folds are preset → player override (an `inputCodes()` index persisted
in a save value by a `MenuEntry::RebindKey` row, applied by
`TerrainGame::applyInputBindings`) → a **user-owned** `controls.hpp`'s
`BTN_*`/`KEY_*` (which only wins when it disagrees with the default preset —
the generated copy is derived from that preset, so they normally agree).
`Pad::injectVirtual` folding of the USB keyboard/mouse is table-driven in the
same generated TU (`inputApplyKeyboardMouse`), so keys rebind too. The raw
`OnButton` flow node stays raw on purpose; `OnAction` is the configurable one.

**New project preference** (travels with the `.tyra`, part of the game) →
`ProjectSettings` → save/load in project.cpp → the *Project* Preferences dialog
(`drawPreferencesModal`) in app.cpp → usually a constant baked into
`inc/terrain_config.hpp` or `scene_data.hpp` by templates.cpp.

**The starting preset (`Project::gameTemplate`) is create-only.** The three
presets the *New Project* dialog offers — `fpp`, `thirdperson`, `orbit` (Empty)
— live in ONE table, `kNewPresets` in app.cpp, read by both the dialog and the
(disabled) Preferences row that displays the choice; `project::create` is the
only writer. It is deliberately not editable afterwards, because it picks which
**user-ownable** game-template sources are generated (`src/terrain_game.cpp`,
`inc/terrain_game.hpp`) — flipping it would either overwrite the user's work or
leave an owned file no longer matching what the project builds. The two player
presets generate the SAME sources (`Project::hasPlayerTemplate()` is the one
place they are treated as one thing) and differ only in the seeded Player
object's `playerMode`, which stays editable per object like any other property.
A new preset is a row in that table plus a branch in `project::create`; a new
*game template* (a genuine source fork) is that plus a `hasPlayerTemplate`-style
predicate at the `templates::generate` fork.

**A member initializer is NOT the new-project default.** Every `read*Section`
guards on `find("key")`, so the struct initializer is what a project saved
*before that key existed* loads as — changing it silently changes those
projects' behavior. When a fresh project should start somewhere else, the
struct keeps the legacy answer and **`project::create` assigns the new one**
(the AmbiencePreset `aoEnabled` precedent; `buildProfile = "debug"`,
`keyboardMouse = false` and the Empty preset's `orbitSpeed = 0` are there for
the same reason). Two corollaries:
`create`'s block is also the only place that may scale metric-by-definition
defaults by `ProjectSettings::unitsPerMeter` — the *New Project* dialog picks
the world scale, so the FPP preset is a 1.8 m player at any scale, while an
existing project's numbers are never touched — and the *New Project* modal's
per-field prose belongs in a `prefHelp("...")` `(?)` tooltip, not in
`TextDisabled` paragraphs that push the buttons off the screen.

**The terrain is optional** (`TerrainConfig::enabled`, docs/terrain.md) and it
is the reference point for "a subsystem a scene can be built without". The flag
lives in `TerrainConfig`, so it travels through `project::create`'s existing
`terrain` argument, the scene table's `"terrain"` object and `SceneData`'s
`operator==` (undo) with no new plumbing — but *reading* it is spread by design,
and the split is the thing to copy:
- **One decision, made once, in the height sampler.** `terrainHeightAtScene`
  returns `TERRAIN_VOID_Y` (a deep but FINITE -1e6) when the scene has no
  terrain, and ~30 call sites in the generated game — the walkers, the physics
  contact, the spring arm, the raycasts, `aoShadeMul`'s ground term, the blob
  shadows' fade — become correct with no branch of their own: "there is no
  floor" IS "the floor is unreachably low". Finite matters: every one of those
  sites subtracts or compares heights, and an infinity would produce NaN
  geometry rather than a skipped effect.
- **Explicit `TERRAIN_ENABLED` only where a site BUILDS something** rather than
  answering a question: `resetTerrainChunks` (no chunks at all, which is what
  makes `renderTerrain` and the streaming pass no-ops), `setupLightPools` (a
  ground pool needs a ground), the projected-shadow patch (skipped over the
  void, or it draws geometry a million units down), the rain particle's fall
  length, and the player spawn Y (the void would drop the player before the
  first collision could catch them — the spawn point's own Y is used instead).
- **The host bakes each read `sc.terrain.enabled` themselves** (`navmesh::bake`
  returns an empty grid, `decalproj` drops the terrain receiver, `gibake` skips
  the ground soup + the terrain lightmap and mixes the flag into its cache
  signature, `aobake`'s atlas leaves the ground-contact term out, texbake skips
  the ground textures/stochastic supertiles) — they already take
  `(Project, SceneData)`, so gating inside is what keeps codegen and the
  viewport agreeing for free. `collectTexturePaths` skipping a terrain-less
  scene is what makes `TERRAIN_TEXTURES` -1 and the ground textures not ship.
- **The editor's twin is `Viewport::terrain_.enabled`**: `buildTerrainMesh`
  builds nothing (world axes still do), `terrainRaycast` misses, and the AO
  ground-contact uniform is forced off — the shader's fallback is the y = 0
  plane, which would darken every object against a floor that isn't there.
  `App::placementHeight()` returns an EMPTY `placement::HeightFn` (placement.cpp
  already reads that as "no terrain under the footprint") instead of the
  viewport's 0.0 fallback.
So: a new consumer of the terrain asks `sc.terrain.enabled` on the host and
`TERRAIN_ENABLED` in a generated builder — but a new consumer of the terrain
*height* needs nothing at all.

**Inline text icons** (`{{cross}}` / `{{action:jump}}`, docs/text-icons.md).
`Project::textIcons` (`TextIcon`: name + PNG + scale, seeded per pad button by
`project::ensureTextIcons`, edited in *UI Editor > Button icons*). The parser is
**shared, header-only** — `parseTextIcons` in project.hpp returns `TextRun`s —
but the two renderers are TWINS and must stay in step: `textWidth`/`drawText` in
menubake.cpp composite icons into baked sprites (so menus, HUD texts, loading
screens and value strips all gained it at once through those two functions),
while the generated game blits them from `res/hud/icons.png` via
`resolveIconToken`/`drawFontText` with rects from `inc/icon_data.gen.hpp`. Both
sides take their geometry from `menubake::iconAtlasLayout`, and the advance
formulas (`iconAdvance` / `iconAdvanceFor`) are the pair to change together, or
the same string measures differently baked vs runtime. Icons are NOT tinted with
the text color (the face buttons carry the DualShock palette) and are skipped in
shadow passes. A `{{token}}` naming nothing stays literal on purpose.

**Anything that draws text.** `Project::fonts` (`GameFont`) is the single font
registry — *Tools > Font Manager* (`drawFontManagerWindow`). Text carries a font
**name**, never a path (`HudText::font`, `GameMenu::font`); only `GameFont::fontPath`
names a real file, resolved in one place (`menubake::resolveFontPath`, with a
Consolas Bold fallback chain). `fonts[0]` is the fallback for an empty/stale
name (`Project::findFont`) and the Font Manager refuses to delete the last
entry, so a reference always resolves. Pick a font with `App::fontCombo`;
`fontSourceCombo` (the TTF picker) belongs to the Font Manager alone. Renames
must follow into texts, menus AND `FontName` node params (`App::renameFont`).

Two very different paths hang off one entry:
- **Static text** (HUD texts, menus, loading screens) rasterizes from the TTF
  into a sprite at build (`menubake::bakeText*`). The font never reaches the PS2.
- **Runtime text** (the Display Text node) can't be pre-baked, so its font bakes
  a **glyph atlas** (`atlasLayout`/`bakeAtlasPNG` → `res/fonts/atlas-<name>.png`,
  metrics into `inc/font_data.gen.hpp` via `fontDataHeader`, both from the same
  `atlasLayout()` call — change the layout math and BOTH move together or every
  glyph misplaces). Only fonts `Project::atlasFontIndices()` returns get one.
  Atlases bake white and are tinted at runtime, so never bake a color in.
  One runtime slot per node: `dynTextSlots()` is walked identically by
  `fontDataHeader` and `flowGraphScript` — keep them in sync or slots misalign.

**VRAM rule for any new texture the game loads.** The engine uploads to GS VRAM
on a texture's FIRST RENDER (`RendererCoreTexture::useTexture`) and then pins it
forever — there is no LRU, only an all-or-nothing flush when the next texture
doesn't fit, on a ~1.33 MB budget (+8 KB per allocation). So: add lazily, never
call `useTexture()` eagerly unless you *want* it resident (the streamed model
textures do), and prefer 4-bit for anything the runtime tints. Do NOT "fix"
residency by freeing on hide: `RendererCoreGSVRam::free` is `pointer = address`
(a bump-pointer stack pop), so freeing anything but the newest allocation
rewinds past live textures.

**Menus & option blocks** (`GameMenu`/`MenuEntry` in project.hpp; project-wide,
not per scene). A menu is baked to a panel sprite at build (menubake.cpp — the
PS2 has no font), edited in the *Menu Editor* (`drawMenusWindow` in app.cpp),
and driven at runtime by `updateGameMenu`/`renderGameMenu` (generated in
`TPL_GAME_CPP_SCENE`, data in `menu_data.gen.hpp` via the `MenuEntryData`/
`MenuData` codegen). Stateful **Toggle/Choice** rows store an option index in a
named save value (cycled by the pad, label drawn from a baked value strip).
**Option blocks** build on that: `MenuEntry::settingBind` (a `Setting` enum,
0=none) marks a stateful row as driving a built-in engine setting; codegen emits
it as the `bind` column and `TerrainGame::applyMenuBindings()` (called each
frame in both loops before `applyVideoRequests`) maps the row's option index
onto the setting - music/sfx volume, deadzone (`g_deadzoneL/R`), stick curve
(`g_stickCurve`), display mode / widescreen (via `scriptCtx` video requests).
The **display-mode row** (bind 5) is special twice: each option carries an
explicit engine mode (`MenuEntry::optionModes` → `MenuEntryData::optModes`;
null/empty = the option index, the legacy positional map — the Menu Editor
edits these as a dropdown of the five `Tyra::DisplayMode`s + a "Default
(project)" sentinel (-1, resolved at runtime to `g_defaultDispMode`, the
boot mode latched at init — which main.cpp may have promoted from
Interlaced to Pal576i on a PAL region via `ProjectSettings::palFullHeight`,
the "PAL picture" preference) + free label; a new engine mode must bump
the -1..4 clamps in project.cpp load, the
templates.cpp emitter and the app.cpp lists/seeds), and with an **Apply video
mode** row (`MenuEntry::ApplyVideo`, action 9) anywhere in the project
(codegen'd `MENU_HAS_APPLY_VIDEO`) the row defers: cycling only stages the
save value, the APPLY row commits the switch (`updateGameMenu` case 9), and
outside a menu the row snaps back to the live mode each frame (also covers a
reverted keep-or-revert confirm). Without the APPLY row bind 5 keeps the
classic switch-on-change path.
A **`MenuEntry::RebindKey`** row (action 10) is the in-game key-assignment row
(docs/input-bindings.md): `bindAction` names the Input Map action, `param` the
save value holding the player's override as an `inputCodes()` index (0 = the
preset's binding). It is deliberately **pad-only** — `inputCapture` ignores
keyboard/mouse and `inputBindLabel` omits them (that path is experimental and
gets its own menu later), so an override replaces the action's `pad` slot alone
and the preset's key/mouse survive. Lifting that restriction means touching all
three of those spots together. It is the one row whose value cannot be baked into the
option strip — the binding name is only known at runtime — so it draws as
**runtime text** from the menu font's glyph atlas, which is why
`Project::atlasFontIndices()` bakes an atlas for any menu carrying such a row
(`MenuData::font` is that FONTS slot) and `updateGameMenu` grows a capture mode
(`menuRebindRow`, cleared on every menu transition). `bind` 8 =
`BindInputPreset` cycles the Input Map presets from a Choice row.
The *Menu Editor* "+ Option block" popup and "+ Options menu" scaffolder
(`addOptionBlock`/`addOptionsMenuPages`/`addRebindRows`, app.cpp) create
pre-configured rows + their backing save values (the scaffolded DISPLAY page
includes the APPLY row, CONTROLS the rebind rows). So a menu change can touch:
`MenuEntry` (+ `==`) →
menu JSON in project.cpp → `MenuEntryData` codegen + `applyMenuBindings` in
templates.cpp → the runtime setting site (audio call, `axis`/`axisValue`,
`applyVideoRequests`) → the Menu Editor UI.

**A menu's LOOK is a stylesheet, not code** (docs/menu-styles.md). The chain
differs from every other feature here, because most of it is data:
- a new **style property** is one row in `menustyle::propSpecs()` + one `case`
  in `applyDecl` + one entry in `menustyle_ui.cpp`'s `propsFor()` (where it
  shows up) + wherever `menulayout`/`menubake` reads it. Nothing else: the
  parser, the writer, the widget and its tooltip are all derived from that row.
- a property that only changes PIXELS never reaches codegen. One that changes
  where a sprite goes is exactly what `menu_data.gen.hpp` carries, and then
  `renderGameMenu` - which is a **compositor over that table and decides
  nothing about the look**. Adding a look to the runtime is the mistake this
  design exists to prevent.
- the bake side owes a **stale-file delete**: `refreshGenerated` writes the
  state/list/description textures only when the sheet needs them and REMOVES
  them when it stops, or a menu that no longer scrolls keeps shipping a strip
  the game loads into VRAM.
- a menu is authored in the logical 512x448 space and the runtime scales it by
  `(width/512, height/448)` per display mode, which is why `Tyra::Sprite` grew a
  per-axis `drawSize` (see tyra-engine-dev). `project::displayModes()` is the ONE
  geometry table - `App::ps2ViewportOutput` and the Menu Editor's
  per-resolution preview both read it, and it is the host twin of
  `RendererSettings::updateGeometry`.
- the sheet's editor support lives in `tools/vscode-tyrax`, and **a change there
  is not done until the `.vsix` is repackaged**: that committed package is what
  the editor installs, nothing rebuilds it, and a source-only change is invisible
  to every user with no error anywhere (it has fired twice - the VU language,
  then `.menustyle`). Bump `version` in package.json, run `python3
  tools/vscode-tyrax/package-vsix.py`, let it delete the old file, read the
  language list it prints.
- **motion is sprite properties, and only sprite properties** (`@transition`
  reacts, `@animate` loops). A baked gradient cannot slide, so the animated
  background is a LAYER whose sampling window moves - and a scroll bakes two
  copies of the tile along each scrolled axis so the window never leaves the
  texture (no wrap mode, the value-strip rule again). Anything drawn over the
  panel must be cropped by hand: a 2D sprite is clipped by nothing, which is how
  the sheen first appeared beside the menu instead of inside it.
- the compatibility rule that must not be broken: **an empty `GameMenu::style`
  bakes byte-identically to the pre-stylesheet editor.** It is checkable -
  `--refresh-gen` an example with both binaries and diff `res/menus/*.png` (that
  is how the double-composite and the missing save-menu hint were found).

**Project lifetime: `attachProject()` and `closeProject()` are a pair.** A
project reaches the editor through `openProjectAt`/`openRemoteProject`/the New
Project modal, all of which end in `attachProject()`; it leaves through
`closeProject()` (File > Close Project), which returns the editor to the state
it boots in — `hasProject_ = false`, an empty `Project`, and the Viewport
drawing `drawWelcomeScreen()` again. Most subsystems need nothing from either:
the per-frame channels (`liveLinkTick`/`livedbgTick`/`livetimeTick`/
`liveLogicTick`/`remotePadTick`/`pollGameError`) and every tool window already
open with `if (!hasProject_)` and stand down on their own — **that guard is what
a new window or channel owes the close path**, and it costs one line. What
`closeProject` handles explicitly is only the state that would otherwise
OUTLIVE the project: worker threads still writing into it (the phone camera
link, the GI baker, a running build, the Drone Generator's offline render) and
the disk-derived caches keyed by project-relative paths
(`viewport_.invalidateAssets()`, the layer-RAM / WAV / model-info / GLB-info
caches), which a *different* project must not inherit. So a new
subsystem with a worker thread or such a cache joins `closeProject`; one that
only reads `project_` per frame does not. The unsaved-edit guard is NOT in
there — `requestCloseProject()` owns it, like `requestOpen/New/Exit`, via
`PendingAction` + the discard modal.

Two traps that guard alone does NOT cover, both real bugs found on this path:
- **A tick called BEFORE its own `!hasProject_` return still runs after a
  close.** `droneTickRender()` sits above that guard in
  `drawDroneGeneratorWindow()` on purpose (a render must survive closing the
  window), and its completion path appends the track to `project_.music` and
  calls `saveAll()` — after a close that writes into an empty `Project`, or into
  whichever project is opened next. Such a worker must be cancelled+joined in
  `closeProject`, in the same order the shutdown path in `App::run` uses: audio
  device first (its callback holds the `LiveSynth`), then the render thread.
- **Audio does not stop by itself.** An audition whose Stop button lives in a
  window that hides on `!hasProject_` keeps playing over the welcome screen with
  no control left. `closeProject` calls `droneStop()` for exactly that reason.
Caches that are always invalidated together must be cleared together:
`modelInfoCache_` and `glbInfoCache_` are siblings at every other eviction site.

**Credits rolls** (`CreditsRoll`/`CreditsBlock` in project.hpp, docs/credits.md)
are the reference point for **"a whole screen the game hands over to"**, and the
three decisions worth reusing:
- **The roll owns the frame.** The loop hook (in BOTH game-cpp loop templates,
  right after the boot-splash block) ticks `credits::tick` and `return`s while
  `credits::playing()` — no walker, no scripts, no scene render behind it. That
  is why `playing()` is `inline` over one extern int rather than a function
  call: the loop asks it every frame, roll or no roll. Anything that owns the
  screen this way must ALSO own the pad, or the frame it ends the same press
  reaches gameplay.
- **A finish action, not a caller contract.** The roll carries where to go
  afterwards (`CreditsRoll::Finish`, resolved to indices in
  `credits_data.gen.hpp`), the player REPORTS it (`credits::Result`) and the
  loop turns it into requests the loop already serves — `scriptCtx.requestScene`
  / `openMenu` / `pendingEvent`. `pendingEvent` is the one addition:
  `updateGameMenu` is the single place that clears `menuEvent`, so an event
  queued earlier in the loop must be promoted there or it is wiped before any
  script sees it. A SKIP runs the same finish action - a skip that only stops
  the roll strands the player.
- **`On Credits Finished` cannot be a falling edge.** A roll freezes every
  graph, so between the frame that started it and the frame it ended, no node
  ran to latch "it was playing" (the trick `OnSequenceEnd` uses). The runtime
  counts finished rolls (`credits::endCount()`) and the node fires when its own
  copy falls behind; a scene reload re-syncs that copy to the LIVE count instead
  of zero. Any future "the world was frozen while it happened" trigger needs the
  same shape.
The look is baked, not drawn: see the `credits_ui.cpp` row above for why the
page strip is the single source of truth and where the VRAM cap comes from.

**A new AI Assistant tool or object property** (docs/ai-chat.md) is two edits,
never one: a tool is a row in `aichat::tools()` (name, `ToolKind`, prose, args -
the row IS what the model is told, so write the description for a reader who has
never seen the editor) plus a branch in the executor its kind selects -
`aichat::runReadTool` for Read, `App::runChatTool` (chat_ui.cpp) for Edit and
Command. A `set_object` property is a row in `aichat::objectProps()` plus a
branch in `App::applyChatObjectProp`; a row with no branch is reported to the
model as unhandled rather than silently ignored, which is the honest failure but
still a bug. Two things that need no maintenance at all, and should stay that
way: the **documentation index** (every `docs/*.md` page is embedded and
described by parsing its own H1 + first sentence) and the **object type list**
(`primitiveTypeName` over `kPrimitiveTypeCount`) - so a new doc page and a new
`PrimitiveType` reach the assistant by existing. Check the result with
`--chat-prompt`, which prints exactly what the assistant is told.

**New machine-global editor setting** (per-installation, NOT in the `.tyra` —
e.g. UI scale, viewport navigation, emulator path, dev-PS2 IP) → a field on
`EditorConfig` (app.cpp) with load/save lines in `loadEditorConfig`/
`saveEditorConfig` (key=value in `editor.ini` under `%LOCALAPPDATA%`) → an App
member seeded from it at startup → edited in the *Edit* Preferences dialog
(`drawEditorPreferencesModal`). Every save funnels through `App::saveGlobalConfig()`
so no field is dropped. If the Runner needs it, feed it into `project_` in
`attachProject` (as `emulatorPath`/`ps2LinkIp` do — those live on `Project` only
as the Runner's runtime transport, not as serialized game data). A machine-wide
setting doesn't have to live in that modal — the `errorPopup` toggle (below) is
edited from the *Debug* window and the error dialog instead, but it still goes
through `EditorConfig` / `saveGlobalConfig()` the same way.

**Game error catcher** (`App::pollGameError`, called each frame from `drawUI`):
the running game's fatal errors reach the editor through its log, not a return
code — a failed `TYRA_ASSERT` in the engine prints a dump (bracketed by the
stable `======= TYRA =======` … `================` delimiters) to
`bin/log.txt` (PCSX2, host: fs) or the `[ps2]` runner-log stream (network
deploy) and halts quietly (see tyra-engine-dev — the engine no longer takes over
the screen). `pollGameError` tails both (throttled), `extractLastTyraAssert`
pulls the last block, and a new one raises the copyable `drawErrorModal` (and
flashes/focuses the window via `glfwRequestWindowAttention`/`glfwFocusWindow` —
PCSX2 has the foreground when the game dies). The same block format covers both
a fatal assertion (game stopped) and a `TYRA_SOFT_ERROR` (recovered asset load,
game running — see tyra-engine-dev); `drawErrorModal` switches its wording on the
`Non-fatal` header marker. Dedupe is by block text
(`errorSeenSig_`), but the signature is **forgotten when a log source shrinks**
(tracked via `errorGameLogSize_`/`errorRunnerLogSize_`): the Runner deletes
`bin/log.txt` before each launch, so a new run drops the size and an *identical*
re-run error pops again instead of being deduped away. Both size and signature
are baselined in `attachProject` so a stale dump present at open neither pops nor
reads as a shrink. (Don't revert to text-only dedup re-baselined per build/run —
it silently misses the second identical run's error.) `EditorConfig::errorPopup`
(default on) gates the dialog; off = errors go only to the Debug window / console.

**The time machine** (`App::livetimeTick`, docs/time-machine.md) is the third
direction of the same host: channel and carries one invariant of its own: the
capture walk in `liveTimeSource` (templates.cpp) is the list of everything a
rewind puts back. **A new field that the RUNNING game mutates - on
`RuntimeObject`, on the flow-variable storage, on the save values - must join
that walk**, or rewinding silently loses it while the panel claims the world
came back. Capture and restore are twins in one function pair, in one file, in
that order; never two lists. Anything the walk cannot reach through
`ScriptContext` (the walker's fall speed, a graph class's own timers) is
deliberately out and is *named* in the panel and the doc rather than quietly
missing. Bumping the walk's shape means bumping the layout-hash mix in
`liveTimeSource` too, or an old capture will be accepted into a world that no
longer matches it.

**Live Link** (`App::liveLinkTick`, called each frame from `drawUI`; docs in
`docs/live-link.md`): with the **debug** build profile and the
`ProjectSettings::liveLink` preference on (default; toggled by the toolbar
LIVE chip, *Build > Live Link* and *Preferences > Build*), the editor streams
scene edits into the running game by rewriting `bin/livelink.bin` (atomic
tmp→rename; `TXLL` v2 header + one 64-byte record per object + seq-echo
footer), which the generated `src/gen/live_link.gen.cpp`
(`templates::liveLinkScript`; empty TU in release or with the preference off)
polls over host: — the same file channel both PCSX2 and ps2link already serve
assets through. Records address objects by `project::liveLinkIdHash` (baked
as `SCENE_*_OBJECT_ID_HASHES` in scene_data.hpp), so renames/reorders are
safe, newly added objects are **live-spawned** from an equal-recipe template
via the runtime spawn pool, and deleted ones are hidden. Consistency is
guarded by the as-built record `bin/livelink.sig`
(`project::liveLinkSigFile`: per-object id + `liveLinkRecipeHash` + a context
hash, stamped by the Runner at build start, which also deletes stale
`livelink.bin`); recipe drift / unspawnable new objects
(`liveLinkCanSpawnLive`: baked lights, projecting decals, mirrors, objects
with graphs/scripts) flip the chip from green LIVE to amber LIVE (rebuild)
and stop writes. **If you add an object property**, decide where it belongs:
build-time-baked or clone-relevant fields go into `liveLinkRecipeHash` (and
new unspawnable categories into `liveLinkCanSpawnLive`), or Live Link will
silently not show that edit while claiming LIVE. The snapshot seq is seeded
from the clock at attach — a restarted editor must never reuse a seq the
still-running game already applied.

**Live Logic** (`App::liveLogicTick` each frame from `drawUI`; docs in
`docs/live-logic.md`) - the third live channel, and the one that changes
BEHAVIOR: debug profile + `ProjectSettings::liveLogic`. Codegen emits
`src/gen/livelogic.built` (per graph: scene + object id + `livelogic::graphHash`)
at build start; the editor compares every live graph against it and compiles
only the ones that differ, so untouched graphs keep running their native C++.
The seam in the generated game is one line per script - `if
(livelogic::patched(scene, ownerIdx)) return;` - so a graph is EITHER
interpreted or compiled, never both. **Adding a node type to the interpreter is
adding a twin**: the opcode goes in `livelogic.hpp`, the runtime body in
`liveLogicOpBodies()` (templates.cpp) and the mapping in livelogic.cpp's
`actionMap`/`triggerMap`; the body must behave exactly like the C++ `actionCode`
emits for that node, and the capability check derives from the same tables so
the editor can never promise a node the interpreter lacks. Patched graphs share
EVERYTHING with compiled ones (the `flowInt/flowBool/flowPos` statics via
generated accessors, save values, RuntimeObject state, and the Live Debugger
node keys carried in each instruction) - that sharing is why a hot patch is
usable rather than a sandbox.

**The devkit's zero-cost rule** (docs/devkit.md) - the constraint every future
debugging feature must satisfy: a release build carries NOTHING. In practice that
means (1) the generated runtime becomes an empty TU, (2) the generated header
keeps the API as `inline` no-ops with predicates that are compile-time `false` so
call sites fold away, (3) the instrumentation in `flow_graph.gen.cpp` is not
emitted at all, and (4) no static arrays exist. Do not add a runtime `if
(debugEnabled)` - that is a branch and a table in a shipped game. The rule is
CHECKED: `elfsym::auditRelease` scans the built ELF for the `TXDEVKIT-` markers
and channel file names, `--audit-release` exits non-zero, and every release build
runs it. **A new devkit layer must plant its own marker** or the audit cannot see
it; a new instrumentation call must go through a generated header that no-ops.
Two more steps that are easy to miss and both silently break the promise: the
layer's file name goes in `kStringNeedles` (elfsym.cpp) next to the other
channels, and its generated `.cpp`/`.hpp` must join `refreshGenerated`'s
overwrite list (see rule 2) - the Remote Pad's runtime reached a RELEASE ELF
because it was generated once at project creation and never refreshed. So run the
audit in BOTH directions before believing it: `--audit-release` against the DEBUG
ELF must FAIL and name your layer, and against a release build must come back
clean. A layer the audit cannot see is indistinguishable from a layer that is
not there. And a new channel file must be **deleted before launch** in BOTH of
the Runner's clean-up blocks (`runner.cpp` has one for the PCSX2 path and one
for the ps2link deploy): a leftover from the last session is applied on the first
poll of the fresh boot, which for the Remote Pad meant a game that starts walking
before anyone touches anything.

**Live Debugger** (`App::livedbgTick` each frame from `drawUI`; docs in
`docs/live-debugger.md`) - Live Link's reverse channel, on the same host: files.
Debug profile + `ProjectSettings::liveDebug`. Codegen (`debugSymbols` in
templates.cpp) assigns ONE KEY per instrumented flow-graph node by walking
scenes -> objects -> nodes, and that enumeration is the single source of truth
for four consumers: the `livedbg::hit(key)` calls emitted into
`flow_graph.gen.cpp`, the runtime tables in `src/gen/live_debug.gen.cpp`, the
`src/gen/livedbg.sym` map the editor reads, and the hash baked into the ELF that
flips the chip to amber when the two disagree. So **a change that alters which
nodes exist changes the keys** - never hand-roll a second enumeration, call
`debugSymbols()`. Instrumented = triggers + actions (`!t->pure`); pure data
nodes are expressions with no moment to report. The generated header
(`inc/scripts/live_debug.gen.hpp`) is ALWAYS emitted and is the on/off seam:
with the debugger off every entry point is an inline no-op and `halted()` a
compile-time `false`, so the game loop's `|| livedbg::halted()` folds away -
that is why the loop hook needs no `{{...}}` gating. The halt itself is the
existing menu pause (`menuActive`/`menuOwnsPad`/`g_gameplayPaused`), so a
project that took ownership of `terrain_game.cpp` loses the world freeze but
keeps the reporting (a fallback global Script pumps it - `tickFromLoop` sets a
flag that permanently disables `tickFromScript`). Breakpoints live in
`Project::debugBreakpoints` as `"<objectId>:<nodeId>"` - editor state in the
`.tyra`, deliberately NOT a collaboration section. Anything new the Debugger
should watch goes into the ONE watch array: flow variables via
`flowDbgReadVar` (emitted next to the `flowInt/flowBool/flowPos` statics,
because that is the TU that owns them), then save values read straight off
`ScriptContext` - the sym file's per-entry `kind` is what tells the editor which
is which.

**Remote Pad** (`App::remotePadTick` each frame from `drawUI`, docs in
docs/remote-pad.md) - the fourth direction, and the only one carrying INPUT:
debug profile + `ProjectSettings::remotePad`. `bin/livepad.bin` is absolute pad
STATE, so both writers (the panel and `--pad`) must **keep rewriting it** - the
game expires an overlay whose `seq` stopped moving, which is what stops a killed
driver leaving a direction held. Two invariants if you touch it: the generated
`livepad::tick` must stay at the TOP of both game loops but **after** each
`Pad::update()` (update rebuilds the state from hardware and would discard an
earlier overlay), and it uses `injectVirtual` **slot 1** because the USB
keyboard/mouse fold owns slot 0 - the slot is what keeps the two sources' click
edges apart, and sharing one makes every held button re-click every frame. The
panel is deliberately the same encoder as the CLI (`livepad::write`), so a
scripted test and a human clicking buttons cannot drift apart.
### 4. Never hand pixels straight to `glTexImage2D`
Every RGBA texture upload in the editor goes through **`glUploadTexRgba(w, h,
pixels)`** (`gl_loader.h`), which allocates the level empty and then fills it
with `glTexSubImage2D`. The one-call form — `glTexImage2D(..., pixels)` — takes
an access violation **inside the AMD GL driver** (`atio6axx.dll`, `0xc0000005`,
same fault offset every time) with entirely valid arguments; the allocate-then-
fill path does not. This bit a user as an instant crash on opening a tool whose
preview uploads two textures, so it is not theoretical. A new upload site that
calls `glTexImage2D` with data re-arms that crash for whatever feature owns it
(PROGRESS 101). Framebuffer attachments allocate with `nullptr` and are fine;
non-RGBA formats (the R32F heightmap) do the same two steps inline.

### 4b. Paths and shell command lines are cross-platform hazards
The editor builds for Windows AND Linux, and four habits that were harmless
while it was Windows-only now break silently:

- **Never hand-join a path with `"\\"`.** Outside Windows a backslash is an
  ordinary FILENAME character, so `p.dir + "\\" + rel` names a file that does
  not exist and the asset simply fails to load - no error, no crash. Use
  **`Project::filePath(rel)`** for a project-relative asset path
  ("res/models/x.obj"), and **`templates::nativePath(rel)`** for a
  `templates::File::relativePath` (those stay `'\'`-separated because hundreds
  of literals compare against them; only the four places they meet the file
  system convert). This bit the first Linux run twice - a fresh project came
  out as ~30 files literally named `src\gen\flow_graph.gen.cpp`, and PCSX2
  was handed `.../name\bin\name.elf`.
- **Never hand-roll the join either, and never assume a path is normalized.**
  `std::filesystem::path(dir) / rel` CONCATENATES - it does not normalize - so
  on Windows a forward-slashed `rel` leaves a MIXED path
  (`C:\proj\bin/proj.elf`). The C++/CRT file APIs accept that, which is what
  makes it dangerous: every `fs::exists()` and every asset load passes, so the
  editor believes the path is good, while an **external program** may reject it.
  PCSX2 v2.6.3 does exactly that (`Requested boot ELF ... does not exist` for
  an ELF it boots under the all-backslash spelling), and `explorer.exe
  /select,"<mixed>"` silently opens the default folder instead. `filePath()`
  therefore ends in `make_preferred()`, `App::assetAbs` delegates to it instead
  of repeating the join, and `platform::revealInFileManager` normalizes at the
  OS boundary. So: join through `filePath()`, and normalize anything you build
  by hand before it leaves the process (PROGRESS 193).
- **Anything nested inside a command line goes through `platform::shellArg()`.**
  `cmd.exe` expands nothing inside double quotes, so the Runner's
  `docker ... sh -c "<script>"` used to reach the container verbatim. `/bin/sh`
  expands `$(...)`/`${...}` inside double quotes on the HOST, which emptied
  every variable in the in-container sfx loop and ran `$(nproc)` against the
  wrong machine. The same applies to any path argument that could contain `$`
  or a backtick.
- **The build container runs as root.** On a plain Linux Docker that means
  everything it writes into the bind-mounted project is root-owned and the user
  cannot delete their own `bin/`. The copy-back rsync passes
  `--chown=` + `platform::containerFileOwner()` (empty on Windows, where Docker
  Desktop maps ownership itself); a new container→host copy needs the same.

One PS2-side limit belongs here too: **PCSX2's `host:` loader silently refuses
an ELF path over ~145 characters** - it logs `ELF Loading: ...`, the EE never
reaches `is executing`, and you get a black window with no diagnostic.
`Runner::launchPCSX2` warns about it. A Linux home directory plus a deep
project tree passes that far sooner than a Windows `TyraProjects` path does.

### 4c. Platform parity: the files that exist twice
Some things in this repo cannot be written once, because a `.ps1` cannot run on
Linux and a `.sh` is not what a fresh Windows shell reaches for. Every such
file therefore has a **twin**, and the failure mode is always the same: someone
edits one side, the other side keeps working on their machine, and the bug
surfaces weeks later on the platform they don't use. **Editing one member of a
pair without its twin, in the same commit, is a bug — not a follow-up.**

| Windows | Linux/macOS | Must stay in step on |
|---|---|---|
| `deps.ps1` | `deps.sh` | every third-party dependency (`vendor/`, `tools/`) — the ONLY place a dependency is listed |
| `setup.ps1` | `setup.sh` | how the lists are fetched |
| `build.ps1` | `build.sh` | flags (`-Run`/`--run`, `-Clean`/`--clean`), the dep guard, the toolchain check |
| `build.cmd`, `setup.cmd` | — | **nothing**: they are thin wrappers that shell out to the `.ps1`. Keep them that way. |
| `CMakeLists.txt` `if(WIN32)` | its `else()` | link libraries, compile options |
| `platform.cpp` `#ifdef _WIN32` | its `#else` | every function in `platform.hpp` |

Two traps worth knowing by name:

- **`build` in PowerShell runs `build.cmd`, not `build.ps1`** — PATHEXT puts
  `.CMD` ahead of `.PS1`, so the wrapper is what actually executes on the
  common invocation. `build.cmd` and `setup.cmd` used to be *full cmd
  translations* with their own hardcoded four-entry dependency list; it froze
  while `deps.ps1` grew to seven, and the guard that was supposed to catch a
  missing dependency lived in the file nobody ran. Result: `fatal error:
  miniaudio.h: No such file or directory` on Windows for a tree that built
  cleanly on Linux (PROGRESS 214). They are now wrappers, and a wrapper cannot
  drift. Don't put logic back into them.
- **A dependency added to only one of `deps.ps1` / `deps.sh`** leaves that
  platform's build guard blind — the merge brings the CMake reference, not the
  clone, and cmake says `Cannot find source file: vendor/<x>` (see
  tyra-testing).

The same reasoning covers a new per-platform file: if you have to add one,
either add its twin in the same commit, or make it a wrapper over the existing
one. Two files that must agree are a maintenance cost; two files where one
simply delegates are not.

### 5. Conventions
- Files: `snake_case.cpp/.hpp`, paired header/impl, flat `src/`.
- One feature = one commit, and its **commit message** describes what was done
  and **how it was verified**, dead ends included. (This used to be a numbered
  entry in `PROGRESS.md`, retired at ~15 800 lines; `docs/backlog.md` keeps the
  forward-looking half and the git-history recipe.) A fact worth re-reading
  later goes in the relevant `docs/` page or skill, not only in the message.
- Comments explain constraints, not narration; match the existing density.
- The editor viewport and the PS2 game must agree: shading, terrain sampling,
  sky and the reflective-material matcap (sphere-map STs from the camera-space
  normal — `docs/reflective-materials.md`) are implemented twice (GLSL/C++ in
  viewport, codegen in templates). When you change one formula, grep for its
  twin. **Terrain splat painting** (`docs/terrain-painting.md`) is such a twin:
  the two-pass layer blend (per-vertex weights → Gouraud alpha over the tiled
  base) is implemented in `buildTerrainChunk` (templates.cpp, StaPip layer bags
  under a blending-enabled info bag) AND in `buildTerrainChunkMesh`
  (viewport.cpp, 9-float layer meshes drawn with the particle shader) - the
  weights themselves are per-vertex `SceneData::splat` on the heightmap grid,
  so both sides consume identical data with no resampling. The **macro ground
  variation** noise (`tintNoise2`) is another such twin: templates.cpp (above
  `buildTerrainChunk`) and viewport.cpp - change one, change both.
- **Analytic per-vertex lighting bakes** (AO occluders, emissive lights -
  `docs/ambient-occlusion.md`, `docs/emissive-materials.md`) all follow one
  shape: `aobake` extracts the analytic box/sphere per object (`objectShape`,
  shared by `collectOccluders`/`collectEmitters`), codegen emits the table into
  `ao_data.gen.hpp`, and the response is evaluated per vertex at scene load
  from ONE distance-to-shape query (`occShapeAt`, a THREE-way twin: host,
  generated game, viewport FS). **The load-bearing rule is pruning**: the local
  list is collected once per object / per terrain chunk, never scanned per
  vertex (an 1100-object scene cost ~170 ms per chunk when it was) - and every
  bake site needs its own collect call. `rebuildStaticBatch` is the easy miss:
  it bakes MANY objects in one call, so the collect belongs inside its member
  loop, not before it.
  **Per-vertex is not good enough for a strong gradient**: a plain box face is
  two triangles, so the diagonal split between them shows as a hard seam - and
  the terrain grid is coarser still (Terrain detail 32 over 64 units = one
  sample every 1.94 u). Both take such bakes through a **lightmap**: primitives
  through `aobake::bakeSceneLightAtlas`, the terrain through
  `aobake::terrainAOMap` - each ONE 256² RGBA32 image where `A` = occlusion and
  `RGB` = emissive light, read by two passes whose VERTEX COLOR selects the
  channels (texturing is MODULATE: black sees only the alpha multiply; the RGB
  add is modulated by white for objects, by the terrain's base tint for the
  ground). A new per-texel bake means claiming a free channel in those two
  images, not adding a third texture. Anything that stays on the vertex path
  (models, spawned clones, physics bodies, textured object receivers) needs a
  flag saying which route it took, or the term lands twice - `SCENE_AO_ATLAS_LIT`
  per object, `SCENE_AO_MAP_LIT` per scene for the terrain.
  **Baked global illumination is the same mechanism one step further**
  (docs/global-illumination.md): with a fresh GI bake that RGB channel stops
  meaning "emissive light" and starts meaning ALL the incoming light, so
  `SCENE_AO_ATLAS_GI` / `SCENE_AO_MAP_GI` say the vertex shade must go BLACK
  (`g_giLightmap`) - and every surface the lightmap cannot cover reads the probe
  grid instead (`g_giProbeShade`, `inc/probe_data.gen.hpp`). Never both, and in
  either case the ambient + directional term, the baked point lights and the
  emissive pools are ALL skipped, because the baked answer already holds them.
  A new geometry-baking site (a new builder, a new LOD path, a new batch kind)
  must STAGE those two globals explicitly - they persist between calls, so
  inheriting whatever the last object left set silently mis-shades a whole
  batch.
  Chunk/part passes that reuse one vertex buffer must also share ONE
  `bboxVersion`: the engine's package-bbox cache is keyed by the vertex pointer,
  so differing stamps make each pass recompute the boxes the previous one just
  built, every frame.
  **A bag lit by VU1 instead of baked** (the opt-in per-object dynamic
  lighting, `GeoPart::litBag`) inverts the usual arrangement, and three things
  go with it: the per-vertex NORMAL capture is per PART, not per object
  (`g_litNormals` staged inside each builder loop - point it at `parts[0]` for
  a whole model and every part's normals pile into part 0, the
  size-equals-vertices gate fails, and the model renders at the flat white
  albedo `pushVert` wrote); the light colours must be built in the same colour
  space `pushVert` uses for that part (255 untextured / 128 textured -
  `GeoPart::litScale`), because the untextured lit program never reads the
  colour bag at all and the albedo has to ride in the light; and the object
  must be excluded from `staticBatchEligible` or the batch rebuild - which
  knows nothing about lit bags - silently renders it with ordinary baked
  shading. Seed the colours where the bag is WIRED, not only in the per-frame
  pass: `renderObjects` rebuilds a dirty object from inside the draw loop,
  after that frame's update has already run.
- **Shadowing an analytic light** is a segment test against those same
  occluder shapes (`aobake::shapeBlocksRay` - slab for a box, quadratic for a
  sphere; twins in the generated game and the viewport FS). Three things go
  wrong if you skip them: the caster list must be pruned by the LIGHT's reach
  (not `SCENE_AO_RADIUS`, so it needs its own list), the receiver's own shape
  and the emitter's own must be excluded (the ray starts on one and ends on
  the other), and the ray needs a small bias off the surface or a prop resting
  on a floor shadows it with its contact face.
  Emitters are AREA sources, so ONE ray only ever answers lit-or-black and
  paints a hard edge: the host bake and the viewport cast
  `aobake::kEmisShadowSamples` rays (ray 0 to the nearest surface point, the
  rest over the silhouette via the fixed `kEmisShadowDisk` - no RNG, bakes must
  be reproducible) and use the unblocked fraction as a visibility multiplier.
  Rays aimed below the receiver's horizon are left OUT of the vote, not counted
  as blocked - otherwise a floor beside a big plate darkens with no occluder
  anywhere. The generated game's per-vertex path deliberately stays at one ray
  (measured: 8 rays = +200 ms of EE scene load on examples/glow, and the vertex
  grid cannot resolve a penumbra anyway) - the ONE place these three twins
  diverge, written down in docs/emissive-materials.md.
- **Atlas region sizing is importance-weighted, per AXIS**: a 6×6 probe grid
  per region gives both the peak signal (sets the region's AREA, density
  `sqrt(peak)`) and the signal's gradient along each of its two axes (splits
  that area between them - a long wall's height needs density its length does
  not). A bisection then raises the density until the image is full. The atlas
  DIMENSION still comes from the unweighted area on purpose - VRAM must not
  move when the weighting does. Measured dead end: simply RAISING the old flat
  128-texel per-axis cap made things worse (it packs badly and spends the win
  on the flat axis); the cap was not the problem, isotropic density was.
- **A lightmap texel's ALPHA MUST NEVER BE 0** (`aobake::kMinLightmapAlpha`).
  StaPip draws with the GS alpha test set to "pass only when alpha != 0" - the
  cutout rule that makes foliage and decals work
  (`stapip_qbuffer_renderer.cpp`). Both lightmap passes sample the SAME
  texture, so a texel whose occlusion is zero fails that test and takes the
  ADDITIVE LIGHT pass down with it: baked light silently clipped to wherever
  the AO happened to be non-zero, as hard texel-aligned holes. This looks
  exactly like "the lightmap is too low-res" and is not - if a bake looks
  cut off, dump the alpha channel before touching resolution. The engine's PNG
  loader scales alpha 0..255 -> 0..128 by integer division, so the floor has to
  be 2, not 1.
- **Per-texel bakes average over the texel footprint** (`kSuper`), because a
  fixture close to a wall throws a sub-texel-sharp penumbra and point-sampling
  it aliases into a staircase. Host-side cost only.
- **The terrain takes the same treatment through the terrain AO map**, whose
  RGB channels carry the light while the alpha keeps the occlusion
  (`SCENE_TERRAIN_LIT` gates the extra additive chunk pass AND tells the vertex
  bake to leave the light out - miss the second half and it lands twice). Two
  traps: the occlusion pass's vertex color MUST be black once RGB is populated
  (white drags the light into the multiply), and a TEXTURED terrain has to stay
  on the vertex path for the same reason textured receivers do (a flat add
  blows out dark texels).
- **Facing terms**: `max(0, N.L)` is wrong for anything standing in for an
  AREA source - it lights one face of a box fully and its neighbour not at all,
  seaming on the corner. The occluder bake uses a linear wrap
  (`0.35 + 0.65*N.L`); emissive light uses **half-Lambert squared**
  (`((1+N.L)/2)²`), because a linear wrap still reaches zero at a finite angle
  and in a dark scene that angle itself reads as a hard shading edge.
- **Anything that imports real-world measurements converts through
  `ProjectSettings::unitsPerMeter`** (docs/world-scale.md) - the project's
  world scale, host-side only, never generated into the game. A unit is
  whatever a project decided it is, but a camera take is metres and a model
  is metres, so a new importer (mocap, photogrammetry, a scan) reads that one
  number instead of inventing a second scale field with its own default. Two
  rules that go with it: don't bake the factor into the copied asset file (it
  then can't be corrected without re-importing - the per-asset size lives in
  `Project::modelUnitMeters` / the `"modelUnits"` section), and don't rescale
  content that is already placed as a side effect of a setting change.
- **Material features live in the `.mtl`, not in `project.json`.** A
  `SceneObject` only carries a `materialPath`; anything a material *is* rides
  in the Wavefront file as a standard-looking statement, parsed FOUR times and
  those four must stay in sync: `src/objparser.cpp` (host/viewport),
  `App::loadMaterialFile`/`saveMaterialFile` (the Material Editor's staged
  `MatEdEntry`), and `parseMtl` in the engine's
  `lean_obj_loader.cpp` (filling BOTH `LeanObjMaterial` for models and
  `LeanMtlMaterial` for primitive materials). Existing members: `refl`
  (`docs/reflective-materials.md`), `Ke` emission
  (`docs/emissive-materials.md`), `# tyra-uvrect` (atlasing), plus the
  editor-only `# tyra-brightness` / `# tyra-glow` / `# tyra-glow-light` /
  `# tyra-bake` hint lines that make a color x strength split round-trip.
  A hint that carries authored controls behind a resolved statement (`Ke`)
  must have ONE resolve function (`App::matEdKe`) used by the writer AND every
  preview, or the file and the viewport drift; and the reader must keep the
  raw statement out of the staged fields, or the hint and the statement clobber
  each other depending on line order. Runtime plumbing for a new
  field means a member on BOTH `GameModelPart` and `GameMaterial` **in both
  game-hpp templates** (TPL_GAME_HPP_ORBIT and TPL_GAME_HPP_FPP - they are
  duplicated), copies in `loadModelAsset`/`loadMaterialAsset`, and a staging
  global next to `g_primKd` set in BOTH `rebuildObjectGeometry` and
  `rebuildStaticBatch` (miss the second and batched props silently lose the
  feature). The asset-import and texbake `.mtl` rewriters pass unknown lines
  through verbatim, so they need no change. Animated `.glb`/`.fbx` models take
  a `.mtl` as an override through `objparser::applyMaterialOverride`, which
  only carries `Kd` + texture - anything else (refl, Ke) has no `.tskl`/VU1
  slot and is silently ignored there; say so in the docs rather than faking it.
- **DPI/zoom: wrap literal pixel sizes in `App::scaled(px)`.** `applyUiScale()`
  scales fonts (`FontScaleMain`) and style spacing (`ScaleAllSizes`) but NOT the
  pixel literals you pass to ImGui. So a hardcoded `SetNextItemWidth(180)`,
  `BeginChild(ImVec2(170,0))`, absolute `SameLine(190)`/`Indent(46)`, fixed
  button size, or hand-drawn preview stays literal and clips/misaligns at high
  scale (a 4K laptop runs ~250%). Route such sizes through `scaled()` (=
  `px * uiScaleApplied_`); negative/`-FLT_MIN`/fill widths and text-measured
  (`CalcTextSize`) sizes already track scale, leave those alone. Free functions
  that draw fixed-size widgets take a `scale` param (see `gradingWheel`).
  **`ScaleAllSizes` also does not reach a third-party style struct** -
  `ImNodesStyle` (grid spacing, node padding, pin radii, link thickness) is the
  editor's, so the Flow Graph scales it itself; a new vendored widget library
  with its own style struct owes the same.
- **A zoomable canvas scales its font with `PushFont`, never
  `SetWindowFontScale`.** The latter writes `window->FontWindowScale`, and since
  ImGui 1.92 `UpdateCurrentFontSize()` reads that field for the CURRENT window
  only - the `FontWindowScaleParents` it computes for children is dead code. So a
  per-window font scale does not reach anything drawn inside a `BeginChild`,
  which is where imnodes (and any canvas) puts its content: the call compiles,
  does not warn, and silently scales nothing. `PushFont(nullptr, sizePx)` sets the
  context-level `FontSizeBase` and children inherit it.
  Two consequences the Flow Graph's zoom is built around (`flowgraph_ui.cpp`,
  PROGRESS 233), worth copying for any future zoomable view: **derive every
  length from the rounded font pixel size, not from the zoom** (ImGui rounds font
  sizes, so text width is a staircase while a raw `zoom` multiplier is a straight
  line - snap the zoom to a whole font pixel and the view stays self-similar),
  and remember that a **stored node position is a distance between nodes**, so it
  carries the SAME factor the node contents do - the UI scale included, or a 250%
  editor draws grown nodes at un-grown spacing and they overlap.

## Building the editor

```powershell
./build.ps1          # configure (if needed) + build → build/tyrax-editor.exe
./build.ps1 -Run     # build and launch
./build.ps1 -Clean   # nuke build/ first
./build.ps1 -Dev     # -O1 iteration build into its OWN build-dev/
```

```bash
./setup.sh --deps    # one-time: toolchain + dev headers (apt/dnf/pacman/zypper)
./build.sh           # configure (if needed) + build → build/tyrax-editor
./build.sh --run     # build and launch
./build.sh --clean   # nuke build/ first
./build.sh --dev     # -O1 iteration build into its OWN build-dev/
```

`build.cmd` / `setup.cmd` exist for cmd.exe and are **thin wrappers** that call
the `.ps1` — which also means a bare `build` in PowerShell runs `build.cmd`
(PATHEXT: `.CMD` before `.PS1`). See "Platform parity" above before touching
them.

**Keep the translation units splittable.** `-O3` is about two thirds of this
project's compile time and it scales worse than linearly with TU size, so a
single huge source file becomes the whole build's critical path: app.cpp at
26k lines took ~48 s on its own (uncontended) while everything else finished
around it, and *any* edit anywhere in the UI paid that. It is now the shell
plus seven subsystem TUs (table above) and the same edit costs ~9 s. So: when a
window or subsystem grows past roughly two thousand lines, give it its own
`*_ui.cpp` — App:: members declared in app.hpp, definitions in the new file,
the assetbrowser.cpp/save_assets.cpp precedent — instead of appending to
app.cpp. `templates.cpp` (27k lines) is the one left and is now the tail of a
clean build. The `Dev` build type (`-O1`, own directory so switching costs
nothing) is for iterating on UI and model code; its host bakes (gibake,
matbake, aobake, pngquant) are genuinely slow, so never benchmark or ship one.
CMake also picks up **ccache/sccache** off `PATH` automatically — worth having,
since this repo is normally checked out in several worktrees at once
(`-DTYRAX_COMPILER_CACHE=OFF` opts out).

Missing `vendor/` deps are fetched by `setup.ps1` / `setup.sh` (imgui docking,
glfw 3.4, imguizmo, imnodes, stb, ufbx, miniaudio — all git-ignored;
`vendor/tyra` is versioned, see tyra-engine-dev), and the build script runs it
whenever something is absent. **The dependency list lives only in `deps.ps1` /
`deps.sh`**, which the setup and build scripts read — add a new third-party
library **to both** and nowhere else, or one platform's build guard won't know
about it (see tyra-testing for the failure that caused). Every entry pins an
exact `Commit` SHA and names a `Mirror` (our `doctorspider42/tyrax-vendor-*`
fork, used when the upstream fetch fails); `Ref` is the branch/tag that SHA came
from and is documentation only. **Never put a branch name where a SHA belongs** —
setup skips a vendor directory whose probe already exists, so a branch pin
freezes silently at whatever HEAD that machine happened to fetch, and two
checkouts drift apart with nothing to say so. To bump: new SHA in both lists
(`gh repo sync` the mirror first so the SHA exists there too), delete the vendor
directory, re-run setup, build. A new dependency also needs its license notice
in `THIRD-PARTY-LICENSES.md` — see the Dependency policy in the README.
Toolchain: Windows
`scoop install mingw cmake ninja` (build.ps1 finds scoop's mingw even
off-PATH); Linux `./setup.sh --deps`, which reads the per-family package lists
in deps.sh (`SYSTEM_PACKAGES_apt`/`_dnf`/`_pacman`/`_zypper`) and installs via
sudo, or pkexec when there is no tty. build.sh only DIAGNOSES - it checks the
tools and the pkg-config headers up front and prints that command. **A new
system dependency has to be added to all four lists**, or that distro's users
get a link error instead of a clear message; zenity is in them because the
file dialogs shell out to it.

One more ordering trap, because it cost 17 seconds of every clean build: a
generated header listed straight in `tyrax-editor`'s sources makes CMake attach
its `add_custom_command` to that target, which inherits order-only deps on
everything the target **links** — so two 0.1 s generators waited for
`libimgui.a`, and with them every editor `.obj`. They live in their own
`tyrax-generated` custom target for that reason; keep a new generator there
rather than in the executable's source list.

Single CMake target `tyrax-editor`. Windows: statically linked (MinGW
`-static`), console subsystem on purpose (logs stay visible), links
shell32/ole32/uuid/ws2_32 for the pickers and sockets. Elsewhere: `OpenGL::GL`
+ Threads + `${CMAKE_DL_LIBS}`, and the app icon `.rc` is skipped. **A new
source file must be added to `CMakeLists.txt`'s single source list** — there is
no glob.

For how to test what you built — headless CLI, codegen checks without Docker,
full PCSX2 e2e, screenshots — read **tyra-testing**.

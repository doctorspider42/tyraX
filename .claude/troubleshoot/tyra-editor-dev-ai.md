# tyra-editor-dev — the AI Assistant and AI generation

From the source map (`aigen`, `aichat`, `chat_ui`, `aisupport`) and `### 3`.
See `docs/ai-chat.md`, `docs/ai-flow-graph.md`, `docs/ai-support.md`,
`docs/ai-tools.md`.

## A new AI Assistant tool or object property does half its job
**Cause/rule:** a tool is a row in `aichat::tools()` (name, `ToolKind`, prose,
args — the row IS what the model is told) plus a branch in the executor its kind
selects (`aichat::runReadTool` for Read, `App::runChatTool` in chat_ui.cpp for
Edit and Command). A `set_object` property is a row in `aichat::objectProps()`
plus a branch in `App::applyChatObjectProp`.
**Fix:** a row with no branch is reported to the model as unhandled rather than
silently ignored — the honest failure, but still a bug. Two things need no
maintenance and should stay that way: the **documentation index** (every
`docs/*.md` page is embedded by `cmake/embed_docs.cmake` into `docs_gen.hpp` and
described by parsing its own H1 + first sentence, `describeDoc`) and the **object
type list** (`primitiveTypeName` over `kPrimitiveTypeCount`) — a new doc page and
a new `PrimitiveType` reach the assistant by existing. The two NODE catalogs
(`aigen::nodeCatalog`, `aichat::procNodeCatalog`) are the same trick over the flow
and procedural registries, which is why `.desc` and a `.tip` on every parameter
are mandatory in both. Check the result with `--chat-prompt`, which prints exactly
what the assistant is told. A tool that hands the model an EDITOR-side structure
wants the project's own serializer (`objectJson`, `sectionJson`, `procGraphJson`);
a tool that stands in for a menu item calls that item's own verb with
`commit = false`, so the whole call stays one undo step.

## The assistant's write surface deletes the rest of a section
**Cause/rule:** `get_section`/`set_section` hand the assistant
`project::sectionJson`/`applySectionJson` — the collaboration LWW unit — so all
sections are readable and writable without a per-collection tool; `set_object_json`
does the same through `objectJson`/`parseObject`. The price is that a blob is
TOTAL, so a model sending back only what it was thinking about deletes the rest.
**Fix:** `shrinkReport` refuses a write that makes any top-level ARRAY shorter
unless it was confirmed. The guard is pure JSON and knows nothing about any
section, which is why it keeps working. `tools()` is the ONE tool table — the
prompt catalog, `validate()` and both executors read it, so documenting a tool and
implementing it are the same edit; `ToolKind` splits them. `add_object` with type
`scatter` routes through `App::addScatterVolume(false)` (a plain `addObject`
makes a 1x1x1 box with no graph that looks exactly like a broken feature). The
procedural tools (`get_proc_graph`/`set_proc_graph`/`list_proc_nodes` +
`bake_volume`) derive `procNodeCatalog` from `procNodeTypes()` the way
`aigen::nodeCatalog` is derived from the flow registry; the wire format is
`project::procGraphJson`/`parseProcGraph`.

## Context compaction silently never happens
**Cause:** `transcript()` trims to just UNDER its budget by construction, so
comparing what it returns against the budget is a test that can never fire.
`compactableCount` walked DOWN from the tail looking for a User message, and in a
conversation that is one enormous turn it walked to index 0 and answered "nothing
to compact" exactly when folding was most needed.
**Fix:** `overBudget` must be decided on the UNTRIMMED conversation;
`contextStats` renders the transcript twice (sent vs full) and `transcript`
reports how many messages it dropped. `compactableCount` picks the latest User
index that leaves a tail, falling back to the last one. The CURRENT turn is never
trimmed and never compacted, whatever it costs. Compaction is a request of its OWN
(`chatCompacting_`) whose reply must never be appended as an assistant message; a
failed one is non-fatal. `ctx` is an ESTIMATE of a request not yet sent
(4 bytes/token); the session totals are `aigen::Usage` (what the backend
reported), and a backend that reports nothing (the Copilot CLI) shows "reports no
usage" rather than an invented figure.

## A model's prose breaks the reply envelope and pastes raw JSON into the window
**Cause:** a reply that is not the `{say, calls}` envelope is taken as prose, but a
reply that IS one and will not parse used to paste the raw envelope as if it were
the answer. A model writing prose leaves quotes in it (`the "main" scene`) and one
makes the whole document unparseable.
**Fix:** `aigen::repairJson` re-escapes any `"` not followed by `, } ] :` (what a
real terminator is ALWAYS followed by) plus lone backslashes, and the repair runs
on the WHOLE reply (the same stray quote also unbalances
`extractJsonObject`'s brace scan); `salvageSay` is the last resort that reads the
prose out by hand and drops the calls. The repair must stay pointed at text a
MODEL wrote — a project file that will not parse is a real problem to report.
`json::parse` did NOT reset its output value, so a retry appended its members to
the abandoned attempt's and `find("say")` answered with the stale partial copy
(fixed in json.cpp). The `searchDocs` is tiered: lines holding every term, PLUS
the lines of the PAGES that hold every term somewhere (capped per page), and only
if both are empty any line holding any term, labelled loose; a page whose name or
title matches gets a large ranking bonus.

## A std::filesystem file time compares as newer than anything
**Cause:** `file_clock`'s epoch is implementation-defined and on this libstdc++
sits in the FUTURE, so every real file's `time_since_epoch()` is a large NEGATIVE
number. The game-is-up wait used 0 for "the file is not there yet", which
therefore compared as NEWER than anything the running game could write — the wait
timed out every time while the game was demonstrably running.
**Fix:** use `LLONG_MIN` (or an optional) for absent, never 0, anywhere file
times are compared. Same trap in the Debugger's age read (use
`fs::file_time_type::clock::now()`, never the system clock).

## A long-running chat tool blocks or cannot report
**Cause/rule:** `build_game` PARKS the turn: `runChatTool` starts the Runner and
sets `chatBuildWaiting_`, the tool loop does NOT send the next request, and a
branch at the top of `aiChatTick` polls the Runner every frame and appends the
outcome (plus the tail of `logOut_.text` on failure) before resuming.
**Fix:** any future long-running tool wants that same shape — a synchronous result
string cannot wait, and blocking the UI thread is not an option. `refresh_generated`
is the cheap half and needs no gate; `press_pad` is the third user (hands a script
to `livepad::parseScript` + `padScriptTick`, which is why `remotePadTick` drives
while a SCRIPT is running, and the state is cleared when it ends). A `run` build
parks TWICE: once for the build, then again until `bin/livedbg.bin` appears —
deliberately that file, not `bin/log.txt`, whose first lines are the engine
initialising. One tool call is ONE undo step and ends in `commitChange()`, never
`saveAll()`; an edit goes through the editor's own verb where one exists; a tool
that cannot do what was asked REPORTS WHY with the names that would have worked
(`aichat::noSuchObject`). `chatAllowEdits_` (editor.ini) is the read-only switch.

## Clipboard copy, and a hover that cannot be screenshotted
**Cause/rule:** the copied text is `aichat::messageText`/`conversationText`, NOT
`renderMessage` (addressed to the model). The GESTURE is a click on the message:
`clickToCopy` draws the body inside a group, puts an `InvisibleButton` back over
it, and paints the toolbar's `theme::hoverAnim` highlight UNDER it through an
`ImDrawList` channel split.
**Fix:** the hit box is labelled `"Copy message"` rather than `"##copy"` (an
`InvisibleButton` draws nothing either way, and a label-less widget is one
`--ui-script` can never target). The acknowledgement is keyed by the hit box's
ImGui id, not message index. `--ui-script`'s `hover` does NOT hold a synthetic
cursor across later steps (the GLFW backend rewrites `io.MousePos` every frame),
so a DELAYED hover tooltip cannot be screenshotted that way; a tooltip shown
during a `click` step can. History is `aiChatPersist()` after every turn plus the
`##chathistory` popup; `chatFile_` is the file the current conversation owns.

## The chat store
**Cause/rule:** `chatDir`/`saveChat`/`loadChat`/`listChats`/`pruneChats` keep one
JSON file per conversation under `<configDir>/chats/<projectId>/`, keyed by the
project's stable id, machine-global on purpose (a conversation is the person's,
not project data that would go through git and the collaboration wire).
**Fix:** the file carries **no timestamp** (its mtime is what `chatAge` renders),
and is written temp-file-then-rename because it is saved after EVERY turn. It
stores a tool call's arguments as VALUES, which is the only reason `json::write`
exists. `projectSummaryJson` is what `--dump` prints; the value coercions
(`numberOf`/`boolOf`/`stringOf`) are shared with the property executor.

## AI generation (`aigen`)
**Cause/rule:** `systemPrompt()` builds the instruction text live from
`flowAllNodeTypes()` + the project's referencable names (new node types
self-document). `Generator` runs the backend (claude/codex/copilot CLI via stdin
temp file, OpenAI via a curl config file) as ONE completion with its own tools,
session files and project instruction files off, from a neutral CWD. The Claude
and Codex CLIs are asked for their JSON/JSONL event output so `Usage` carries the
model's OWN token counts (and Claude's cost).
**Fix:** **the Codex reply arrives in a FILE** (`-o`, because its stdout is
progress chatter), so it must be read before `cleanup()` deletes the request's
temp files — an ordering bug waiting for any backend that answers into a file on
a worker thread inside a kill-on-close Job Object. `parseGraph()` extracts and
validates the reply with the same link pin rules the editor prunes by +
auto-layout; `appendGraph()` merges. Backend config = `aigen::Config` on
`EditorConfig` (editor.ini `aiBackend`/`aiModel`/`aiThinking`); the modal is
`App::drawAiGenerateModal`.

## Installed AI support files stop refreshing
**Cause:** `aisupport` embeds content authored in `ai-support/` at build
(`cmake/embed_ai_support.cmake` → `ai_support_gen.hpp`), substitutes `{TYRAX_EXE}`
at install, and rewrites only when a "Generated by TyraX" marker is intact. The
marker is searched in the first 4 KB, not 512 B: a skill's `description:` can run
long, and at 512 a new skill whose marker landed at byte 829 read as user-owned
and silently stopped refreshing.
**Fix:** Codex is a second DESTINATION for the Claude files, not a second copy —
`.agents/skills/` + `AGENTS.md` with self-references rewritten; a duplicate under
`ai-support/` would drift. **When the generated-project format, flow-graph model,
menu stylesheets or CLI change, update `ai-support/` in the same commit.**

## Reference facts (upstream `ai-features.md`)
- Files/sizes: `aigen.cpp/.hpp` (~1.1k+160), `aichat.cpp/.hpp` (~2.1k+380), `chat_ui.cpp` (~1.8k), `aisupport.cpp/.hpp` (~130+35). Docs `docs/ai-flow-graph.md`, `docs/ai-chat.md`, `docs/ai-support.md`, `docs/ai-tools.md`. CLI flags: `--dump`, `--list-nodes`, `--dump-graph`, `--apply-graph`, `--ai-graph`, `--add-ai-support`, `--chat-prompt`, `--search-docs`.
- `systemPrompt()` builds from `flowAllNodeTypes()` + the project's referencable names; `nodeCatalog`/`nodeCatalogLine` are the catalog. `Generator` runs claude/codex/copilot CLI (stdin temp file) or OpenAI (curl config) as ONE completion; the Codex reply arrives in a FILE (`-o`, stdout is progress) so it must be read before `cleanup()`. `parseGraph()` extracts/validates with the editor's link pin rules (incl. `flowExecOutCount`) + auto-layout; `appendGraph()` merges; `aigen::repairJson` lives here. `aigen::Config` on `EditorConfig` (`aiBackend`/`aiModel`/`aiThinking`); modal `App::drawAiGenerateModal`.
- Skills are `docs/*.md`: embedded by `cmake/embed_docs.cmake` into `docs_gen.hpp`; the prompt carries an INDEX (`describeDoc`) expanded with `read_doc`; delimiter `)TYRAXDOC"`.
- Write surface = project serialization: `get_section`/`set_section` = `project::sectionJson`/`applySectionJson`; `set_object_json` uses `objectJson`/`parseObject`; `shrinkReport` refuses a write that shortens any top-level ARRAY unless confirmed. `tools()` is the ONE tool table; `ToolKind`; `runReadTool`; `objectProps()`. Tiered `searchDocs`. `parseReply` never fails: `aigen::repairJson` re-escapes any `"` not followed by `, } ] :`; `extractJsonObject`; `salvageSay`; `json::parse` did not reset its output (fixed).
- Procedural tools: `get_proc_graph`/`set_proc_graph`/`list_proc_nodes` + `bake_volume`; `procNodeCatalog` from `procNodeTypes()`; wire `project::procGraphJson`/`parseProcGraph`; `add_object` type `scatter` -> `App::addScatterVolume(false)`.
- Context: `overBudget` on the UNTRIMMED conversation; `transcript()`; `contextStats`; `compactableCount`. Chat store `chatDir`/`saveChat`/`loadChat`/`listChats`/`pruneChats` under `<configDir>/chats/<projectId>/`; `chatAge`; `json::write`. `projectSummaryJson` = `--dump`; `numberOf`/`boolOf`/`stringOf`.
- Acting (`chat_ui.cpp`): `aiChatTick` up to `kChatMaxSteps` (8) rounds; one tool call = one undo step ending in `commitChange()`; `deleteSelectedObjects`; `save_project` is the only writer; `runChatTool`; `build_game` parks via `chatBuildWaiting_`; `refresh_generated`; `press_pad` (via `livepad::parseScript` + `padScriptTick`); `aichat::noSuchObject`; `chatAllowEdits_`; `aiChatPersist()`; `##chathistory`; `chatFile_`; `aiChatReset`/`aiChatOpen`.
- Clipboard: `aichat::messageText`/`conversationText` (NOT `renderMessage`); `clickToCopy`; `"Copy message"`; `theme::hoverAnim`.
- AI support: content in `ai-support/`; `cmake/embed_ai_support.cmake` -> `ai_support_gen.hpp`; `{TYRAX_EXE}` at install; marker searched in the first 4 KB. Codex is a second DESTINATION (`.agents/skills/` + `AGENTS.md`).

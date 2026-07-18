# AI flow-graph generation

Describe the logic you want in plain language and the editor builds the flow
graph: `Flow Graph` window → **Generate with AI...** (or headlessly,
`tyrax-editor --ai-graph` — see [ai-tools.md](ai-tools.md)).

## Using it

1. Pick the backend once in `Edit > Preferences > AI assistant`:
   - **Claude CLI** — needs the `claude` command on PATH (Claude Code).
   - **GitHub Copilot CLI** — needs the `copilot` command on PATH.
   - **OpenAI API** — needs the `OPENAI_API_KEY` environment variable (calls
     the Chat Completions endpoint through `curl.exe`, which ships with
     Windows).
   The **Model** dropdown lists the backend's common models plus
   **Custom...** — pick Custom to type any model id by hand (new models work
   the day they ship); *Backend default* leaves the choice to the CLI/API.
   **Thinking** turns on extended reasoning where the backend supports it
   (slower, better on tricky logic; ignored by the Copilot CLI).
2. In the Flow Graph window pick the object whose graph you're editing and
   press **Generate with AI...**. Describe the behavior ("when the player
   uses the lever, open the door and play a clank sound"), press
   **Generate**. A spinner shows while the backend runs; **Cancel** kills the
   whole backend process tree.
3. The reply is validated (node types against the live registry, link pin
   rules — the same checks the graph editor enforces) and applied as **one
   undo step** — a bad generation is a `Ctrl+Z` away. With an existing graph
   the default is **Add to the existing graph**; untick it to replace.

## What the model knows

The system prompt is built fresh per request from the live editor state, so it
never drifts from the code:

- the full node catalog from `flowNodeTypes()` **including the project's
  custom `.flownode` nodes**, with pins, params and per-node semantics;
- the graph JSON schema and the link-validity rules;
- the project context: the owner object, scene objects (with usable/animated
  flags and positions), scenes, layers, music/sound tracks, save values and
  texts, menus and their flow events, on-screen texts, gradings, ambience
  presets and sequences — so generated `str` params reference real names.

## Failure modes

- Backend errors (CLI missing, API key unset, usage limits, unknown model)
  surface verbatim in the modal; nothing is applied.
- A reply whose JSON is malformed, or that invents node types, is rejected
  with the reason (retry, often with a more specific prompt).
- Invalid links (wrong pin kinds, dangling endpoints) are dropped with a
  warning; missing node positions get an automatic left-to-right layout.

## Internals (for editor developers)

`src/aigen.cpp`: `systemPrompt()` (registry + project context → instruction
text), `Generator` (worker thread; the prompt travels via a temp file + stdin
redirect — never the command line; the child runs in a kill-on-close Job
Object so Cancel takes down the whole tree; stderr goes to a temp file so it
cannot interleave with the reply), `parseGraph()` (fence/prose-tolerant JSON
extraction, registry validation, the editor's link rules, auto-layout),
`appendGraph()` (id/position-shifting merge). The GUI modal lives in
`App::drawAiGenerateModal` (app.cpp); the headless path in main.cpp
(`--ai-graph`) shares every stage. Backend/model/thinking persist in
`editor.ini` (`aiBackend`/`aiModel`/`aiThinking`), which the CLI reads as its
defaults too.

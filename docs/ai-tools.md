# AI-agent CLI tools

The editor executable doubles as a headless toolbox for AI coding assistants
(and scripts) working inside a generated project: inspect the project, read
and write flow graphs, regenerate the game sources and drive AI generation —
all without the GUI. Projects with [AI support](ai-support.md) installed ship
skill files that teach the assistant these commands.

```powershell
tyrax-editor.exe --dump <projectDir>
tyrax-editor.exe --list-nodes <projectDir>
tyrax-editor.exe --dump-graph <projectDir> <object> [scene]
tyrax-editor.exe --apply-graph <projectDir> <object> <graph.json> [scene] [--append]
tyrax-editor.exe --refresh-gen <projectDir>
tyrax-editor.exe --ai-graph <projectDir> <object> <prompt|prompt-file> [scene]
                 [--backend claude|copilot|openai] [--model <m>] [--thinking]
tyrax-editor.exe --add-ai-support <projectDir> [claude] [copilot]
tyrax-editor.exe --chat-prompt [projectDir]
tyrax-editor.exe --search-docs "<query>" [page]
```

- **`--dump`** — one JSON summary of the project: which scene is active, then
  scenes with their objects (name, type, position, usable/model/layer/scripts,
  graph size) and layers, plus every name a flow-graph parameter can reference
  (music, sounds, save values/texts, menus, HUD texts, gradings, ambience
  presets, sequences, credits, prefabs, fonts, input actions and presets). This
  is the same text the editor's own [AI Assistant](ai-chat.md) gets from its
  `project_summary` tool - one answer to "describe this project to a model".
- **`--list-nodes`** — the flow-node catalog (built-ins + the project's
  custom `.flownode` nodes) with pins, params and semantics, plus the graph
  JSON schema and link rules. This is literally the AI system prompt, so it
  is always in sync with the registry.
- **`--dump-graph` / `--apply-graph`** — read/write one object's graph as
  JSON. Apply validates node types and link pin rules (same checks as the
  editor), drops invalid links with a warning, auto-lays-out unpositioned
  nodes, and saves the project. `--append` merges instead of replacing.
  Accepts both the editor's stored format and the AI schema
  (`"kind": "exec"|"object"|"pos"|"bool"|"text"`), and tolerates markdown
  fences/prose around the JSON.
- **`--refresh-gen`** — regenerate every editor-owned game source from the
  project data without building (no Docker). The fast way to check what a
  data change does to the code; dangling references show up as
  `// node N: unknown ...` comments in `src/gen/flow_graph.gen.cpp`.
- **`--ai-graph`** — the whole [AI generation pipeline](ai-flow-graph.md)
  headlessly. The prompt argument is a file path if one exists, literal text
  otherwise. An existing graph goes into the prompt automatically and the
  model decides from the request whether to change, extend or rebuild it —
  the reply is always the complete resulting graph. Backend/model/thinking
  default to the editor's global preferences (`editor.ini`); the flags
  override per call.
- **`--add-ai-support`** — (re)install the [AI support](ai-support.md) skill
  files (defaults to `claude` when neither provider is named).
- **`--chat-prompt`** — print the system prompt of the editor's built-in
  [AI Assistant](ai-chat.md): its tool catalog, the documentation index derived
  from `docs/*.md`, the object type/property tables and the live project
  context. The counterpart of `--list-nodes` for the chat assistant - the way to
  see what it is told without a backend and without clicking. With no project
  argument it prints the no-project-open variant.
- **`--search-docs`** — full-text search over the documentation the editor
  carries (the assistant's own `search_docs` tool, from a shell). Prints every
  matching line with its page and the heading it sits under, most relevant page
  first; exits 1 when nothing matches. Handy on its own ("which page talks about
  VRAM residency?") and it needs no project.

`--new`, `--build`, `--resave` (see the README's CLI section) complete the
loop: an agent can create, edit, regenerate, build and run a project end to
end.

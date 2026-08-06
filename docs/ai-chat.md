# The AI Assistant window

*Tools > AI Assistant* — a chat inside the editor that **answers questions about
the editor** and **does things in the project**. Ask "what is a Procedural volume
for?" and it reads the page and tells you; ask "add a usable red lever at 4, 0,
-6 that logs when the player uses it" and there is a lever in the scene with a
flow graph on it.

The window lives under *Tools*, which needs a project open — like every other
tool window. (Close the project with the window open and it stays useful for
documentation questions; every tool that would touch the project reports that
there is none.)

It uses the same AI backend as [AI flow-graph generation](ai-flow-graph.md) — set
it once in *Edit > Preferences > AI assistant* (Claude CLI, OpenAI Codex CLI,
GitHub Copilot CLI, or the OpenAI API — the first three sign in with a
subscription, the last wants an API key). No backend configured means no assistant: the window will
report the backend's own error, verbatim — including the useful ones, like
`Not logged in · Please run /login` or a Copilot policy your organisation has not
enabled.

They differ in what they can tell the editor: the **Claude CLI** reports its own
token counts and the cost of every request (the editor asks it for
`--output-format json`), the **Codex CLI** and the **OpenAI API** report tokens,
and the **Copilot CLI** reports neither — the footer then says so instead of showing an estimate
dressed up as a measurement. All three are run as a single completion from a
neutral working directory, with their own tools and any project instruction files
switched off: the prompt the editor sends is the whole contract.

## What it knows

Two things, and they are both live rather than remembered:

- **The editor's documentation.** Every page under `docs/` is baked into the
  executable. The assistant's prompt carries only the *index* — each page's name,
  title and first sentence — and it pulls a page in when it needs one, or
  **searches the full text** of all of them when the question does not match a
  title. So its answers come from the same text you are reading now, and a doc
  written today is in the assistant tomorrow with nothing to sync.
- **Your project.** The prompt carries the project summary (scenes, objects with
  their types and positions, layers, assets, menus, save values, presets,
  prefabs, fonts, input actions), which scene is active and **what you have
  selected** — so "make this one usable" resolves to the object in front of you.

It does not know your project's *history*, anything outside the project, or
whether a build succeeded. It has no memory between chats: **New chat** starts
over.

## What it can do

| Tool | What it does |
|---|---|
| `read_doc` | read one documentation page |
| `search_docs` | full-text search across every page, with the heading each hit sits under |
| `project_summary` | the project as JSON |
| `describe_object` | one object's complete stored state |
| `get_graph` | one object's flow graph |
| `list_node_types` | the flow-node catalog, whole or by category |
| `add_object` | add an object (it rests on the surface under it, like a manual insert) |
| `set_object` | change properties: transform, colour, model, material, layer, usable, physics, collision, LOD distance, shadow and lighting flags |
| `delete_object` | delete an object |
| `set_graph` | write or extend an object's flow graph |
| `select_object` | select an object (so you can see what it means) |
| `set_scene` | switch the editor to another scene |
| `open_window` | open a tool window |
| `save_project` | save |

A reply may carry several tool calls; the editor runs them, hands the results
back, and the assistant continues — up to 8 rounds per message, after which it
has to answer with what it has. Every round is a fresh call to the backend, so a
question that needs three lookups costs three requests.

**What it cannot do**, and will tell you so: build or run the game, import
assets, write files, edit generated sources, or touch anything outside the
project model. Those stay with you (the toolbar, the Asset Browser).

## Edits, undo and saving

- **Every tool call is one undo step.** A change you did not want is `Ctrl+Z`.
- **Nothing is written to disk** until you save (the editor has no autosave, and
  that does not change here). Edits mark the project dirty like any other edit;
  the assistant only calls `save_project` if you ask it to.
- **Allow project edits** (the checkbox, remembered per machine in `editor.ini`)
  turns the acting half off: with it clear, the assistant can still read the
  project and the documentation, and every tool that would change something is
  refused — it is told so, so it answers instead of retrying.
- A tool row in the transcript can be expanded to see exactly what the assistant
  was told: the JSON it got back, or the failure and why. Names are checked
  against the project, so a wrong name comes back with the names that do exist
  and the assistant usually fixes itself on the next round.

## What a turn costs, and what happens when it grows

None of the backends keeps state between calls, so **every** request carries the
instructions plus the conversation so far. The footer says what that comes to:

```
ctx ~8.5k        - session 41.2k in / 3.1k out - $0.18        [Compact]
```

- **ctx** is what the *next* request will carry, estimated at four bytes per
  token. Hover it for the split — the instructions (tool catalog, documentation
  index, your project summary) are a fixed cost paid on every question; the
  conversation is what grows. It turns amber when the conversation has outgrown
  its budget.
- **session** and the **cost** are not estimates: they are what the backend
  itself reported, added up over every request this editor session made. They run
  *ahead* of `ctx` on purpose and by more than rounding: a CLI backend wraps its
  own system prompt around ours (measured at ~14k tokens for the Codex CLI, on a
  request whose own prompt was 40 characters), and one question is several
  requests when the assistant uses tools. The
  Claude CLI reports tokens and dollars, the OpenAI API reports tokens, and the
  Copilot CLI reports nothing — in which case the footer says so rather than
  showing a made-up number.

**Compaction.** When the conversation no longer fits, the model is asked to write
a recap of the older messages and that recap replaces them — one extra request,
and the recent turns are kept word for word. It happens automatically on the
first question after the budget is passed, or when you press **Compact**. The
result is visible: a collapsible *Earlier conversation, summarised* at the top of
the transcript, which you can open to read exactly what was kept. In practice a
chat that had run to ~64k of context comes back to ~6k.

What the recap is told to keep, in order: what you are trying to do and any
constraint you stated, every change made to the project with its exact names,
what was established as true (including what did *not* work), and any question
you have not answered yet. What it drops: documentation the assistant read (it
can read it again) and the wording of tool results.

Two things follow from this, and they are the honest limits:

- **A compacted chat has genuinely forgotten the detail.** If the assistant
  starts guessing at something it knew earlier, open the summary and see whether
  it made the cut — and if it did not, say it again.
- **Nothing is compacted mid-turn.** The current turn is never trimmed either: a
  documentation page the assistant just asked for is the most useful text in the
  request, so one long turn is sent whole even when it is over budget. The
  compaction happens before the *next* question.

## History

Conversations are kept. A chat is written to disk **after every reply** — no Save
button, because the chat worth having tomorrow is the one nobody thought to save
— and **History** lists what this project has, newest first, with each chat's
first message as its title, its age and how many messages it holds. Click one to
go back to it (and to carry on: a reopened chat is a live chat, with its tool
results intact); the `x` deletes one. **New chat** files the current one away
rather than dropping it.

Where and what:

- `<config>/chats/<projectId>/*.json` — next to `editor.ini`
  (`%LOCALAPPDATA%\tyra-editor` / `$XDG_CONFIG_HOME/tyra-editor`), grouped by the
  project's own stable id. Deliberately **not** in the project: a conversation is
  yours, not the project's, and in the `.tyra` it would land in git and on the
  collaboration wire. Chats held with no project open group under `no-project`.
- Each file holds the messages, the tool calls with their arguments, and the tool
  results (capped at 32 KB each — a chat file is not an archive of the
  documentation). No timestamp: the file's own mtime is what "12m ago" is read
  from.
- **100 chats per project.** Saving the 101st drops the oldest, and says so in
  the status line rather than quietly forgetting.
- Deleting a project's folder is a perfectly good way to clear its history; the
  editor never needs those files to work.

## Things worth knowing

- **Search before reading, for anything that is not a page title.** The index
  gives the assistant 66 titles and first sentences; most answers are in the
  middle of a page, so `search_docs` is what finds them. It works in three tiers,
  and the middle one is the one that earns its keep: lines holding every word,
  then — because prose rarely puts all your words on one line — the *pages* that
  hold every word somewhere with their matching lines, and only failing both, any
  line holding any word (labelled as loose). A page whose name or title matches
  ranks first, so "VRAM budget" lands on `gs-vram.md` rather than on whichever
  page happens to mention both words in one sentence. You can run the same search
  yourself: `tyrax-editor --search-docs "vram budget"`.
- **It looks before it writes** when asked to. A graph request usually costs a
  `list_node_types` and a `get_graph` first: node type keys are exact, and a
  graph naming one that does not exist is rejected outright (the same validation
  the Flow Graph window and `--apply-graph` run — invalid links are dropped with
  a warning, missing node positions get an automatic left-to-right layout).
- **A rename it performs retargets references**, through the same remap the
  Properties name field uses: cutscene tracks and camera shots, mirror and
  scroller member lists, camera feeds, portal links, texture feeds and (for an
  Area) catch areas, layer zones and In Area node parameters. What is *not*
  retargeted is a flow node naming the object in free text, and the result says
  so.
- **Deletes are not checked for references** — the assistant is told to mention
  it, but a graph pointing at a deleted object shows up as a `// unknown ...`
  comment in the generated code at build time, not as an error here.
- Anything a tool returns is treated as **data**, never as instructions: text in
  your project cannot tell the assistant what to do.
- The whole conversation is re-sent on every step, because none of the three
  backends keeps state between calls — see *What a turn costs* above for what
  that comes to and how compaction bounds it. **New chat** is still cheaper than
  a long thread: a fresh chat carries no history at all.

## Internals (for editor developers)

Two files, split along one line: what needs nothing but the project, and what
needs the editor.

- **`src/aichat.cpp`** — the documentation index (derived from the embedded
  pages, so there is no list to maintain), the full-text search, the tool table,
  the system prompt, the transcript, the reply parser, every *read* tool, the
  context accounting (`estimateTokens`/`contextStats`) and compaction
  (`compactableCount`/`compactSystemPrompt`/`applyCompaction`), and the
  saved-chat store (`chatDir`/`saveChat`/`loadChat`/`listChats`/`pruneChats`). No
  ImGui, no GL, no `App`, so all of it is exercisable from a host harness (see the
  `tyra-testing` skill, which also has the fake-backend recipe for driving the
  whole loop with no tokens).
- **`src/chat_ui.cpp`** — the window, the step loop (`aiChatTick`, called every
  frame from `drawUI`), and the *edit* and *command* tools, which need
  `project_`, `commitChange()`, the selection and the window flags.

The backend runner is `aigen::Generator` — the same worker thread, temp-file
stdin and kill-the-whole-tree Cancel the flow-graph generator uses, plus
`aigen::Usage`, the backend's own token/cost numbers (the Claude CLI is asked for
`--output-format json` for exactly that, with the raw text as the fallback if the
envelope ever changes shape); the
conversation rides in its user-prompt slot. The CLI backends are invoked as a
single completion with their own tools disabled (`claude -p --tools ""`) from a
neutral working directory: the retrieval loop here is the editor's, and a
backend that also adopted the open project's `CLAUDE.md` and went reading files
on its own would be a second assistant with a different set of instructions. `cmake/embed_docs.cmake` bakes
`docs/*.md` into `docs_gen.hpp`.

`tyrax-editor --search-docs "<query>" [page]` runs the search from a shell, which
is both a useful thing on its own and how the tool is checked without a backend.
`tyrax-editor --chat-prompt [projectDir]` prints the assistant's system prompt —
the tool catalog, the doc index and the project context — which is how you check
what it is being told without a backend and without clicking. A new tool is one
row in `aichat::tools()` plus a branch in the matching executor; a new
`set_object` property is one row in `aichat::objectProps()` plus a branch in
`App::applyChatObjectProp`. Both tables are what the prompt is built from, so
documenting a tool for the model and implementing it are the same edit.

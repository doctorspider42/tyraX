# AI Assistant

The AI Assistant answers questions about TyraX and can edit the open project.
Open **Tools > AI Assistant** (or the sparkle button at the foot of the left
rail). It opens as a tab of the Properties column - a conversation beside the
scene.

![The AI Assistant after an edit turn: the change card with Undo, the mode control and the composer.](img/ai-assistant.png)

The chip next to the title (`Claude CLI · default`) names the backend and
model and opens a small popover to switch them (backend, model preset,
extended thinking); a custom model id is set in **Edit > Preferences > AI
assistant**. The backends: Claude CLI,
OpenAI Codex CLI, GitHub Copilot CLI or the OpenAI API. CLI backends use their
normal sign-in; the API backend needs a key.

## What it knows

The assistant receives:

- an index of every page under `docs/`, with tools to read or search them;
- scenes, objects, layers, assets and project-wide settings;
- the active scene and current selection;
- results from any tools it calls during the turn.

It does not inherit your backend's normal project instructions or tool access.
The editor's prompt and tools are the whole contract.

## What it can do

Read-only tools cover documentation, project summaries, objects, flow graphs,
procedural graphs, project sections and a running debug game's state, log and
graph activity.

What it may do is the **mode** above the input - *Ask only* (read-only),
*Edit project*, or *+ Build & run* - with a one-line consequence under it, and
each reply is stamped with the mode it was asked in. In *Edit project* it can:

- add, duplicate, change and delete objects;
- write flow and procedural graphs;
- add or remove scenes;
- edit menus, ambience, loading screens, HUD, input, saves and other sections;
- create, insert or flatten prefabs;
- bake procedural volumes;
- select objects and open tool windows;
- regenerate sources and save when asked.

*+ Build & run* also lets it build the game, launch it and drive the Remote
Pad. It is a separate step because builds are slow and not undone by `Ctrl+Z`.

It cannot import files, sculpt or paint terrain, edit arbitrary source files or
touch anything outside the project model.

## Good requests

Be direct and name the result:

```text
Explain Procedural volumes in three sentences.
Add a usable red lever at 4, 0, -6.
On Used, play the switch sound and hide door-a.
Make a forest volume that avoids the path material, then bake it.
Build, press Cross and tell me which graph nodes fired.
```

“This”, “selected” and “here” use the editor's current selection and scene.

## Safe edits

- Every tool call that changes a scene is one undo step. Project-wide
  sections (menus, HUD, input...) are not in `Ctrl+Z`, and the card says so.
- Changes stay in memory until you save.
- A turn that changed something ends in a **change card**: one line per
  change in words (`Added lever - box · 4.00, 0.00, -6.00`, `Changed lever -
  color, usable`, `Graph lever - 6 node(s)`, `Checked refresh_generated`),
  *Select* / *Open* links to the object or its graph, and **Undo** - which
  steps the undo history back to before the turn (offered only while nothing
  else has changed since). *N tool calls* shows the raw calls with their exact
  input and result; the read tools collapse into one quiet line (`Read
  object-scripts.md, generator`).
- Wrong object and asset names are rejected with valid alternatives.
- Replacing a project section or procedural graph is a total replacement, not a
  patch. Shrinking a list requires explicit confirmation.
- Writing a procedural graph marks its bake stale; ask the assistant to bake it
  if you want generated geometry now.

Pick **Ask only** when you only want advice.

The composer is a card: the draft (**Enter** sends, **Ctrl+Enter** starts a
new line), a chip naming what "this" refers to (the selection and the active
scene), and the send button, which becomes **Stop** while a request runs.
Under it, **Context** is a meter of the next request's conversation against
the size at which it is compacted, then the session's reported token use and
cost, and **Compact** (fold the oldest messages into a summary now). Click any
message to copy it.

## Checking its work

The assistant can call **refresh generated** after graph edits. This catches
missing object, scene and asset references without Docker. In the **+ Build & run** mode
it can also wait for the build, read compiler errors, launch a debug
game, press buttons and inspect the game's log and graph counters.

Running-game tools need the matching [devkit](devkit.md) channels in a debug
build. A release build intentionally exposes nothing.

## Conversation controls

- Click an assistant message to copy it.
- **Copy chat** copies the full transcript and tool results.
- **New chat** starts with no conversation memory.
- **History** restores chats saved for this project.
- **Compact** summarizes an oversized transcript and continues from the summary.

Each tool round is another backend request. A turn that reads several pages,
edits a graph and builds the game therefore costs more than a simple answer.
The footer shows available token and cost data; backends that do not report it
are labelled as such rather than estimated.

## History and privacy

Chats are stored locally under the editor's configuration directory, grouped by
project. They are not part of the `.tyra` project and are not committed unless
you copy them into the repository yourself.

The chosen backend receives the prompt, relevant transcript, project summaries
and tool results. It does not receive arbitrary project files unless their
contents are represented by an editor tool.

## Related tools

- [AI flow-graph generation](ai-flow-graph.md) for a focused graph-only dialog.
- [AI support files](ai-support.md) for assistants working in generated source.
- [AI command-line tools](ai-tools.md) for scripted project changes.

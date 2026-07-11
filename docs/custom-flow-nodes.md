# Custom flow-graph nodes

The Flow Graph ships with a fixed set of built-in nodes (triggers, object
actions, audio, save data, logic gates…). When you need a node the editor
doesn't have — a project-specific action that pokes some `ctx` field, calls a
helper of yours, or wraps a snippet you keep re-typing into `Log Message` — you
can define your own **custom action node** as a small text file in the project.
No editor rebuild, no C++ toolchain: drop a `.flownode` file into the project's
`flow-nodes/` folder and it appears in the Flow Graph add-menu.

Custom nodes are **per project**, but a node is just one self-contained file, so
moving it to another project is a copy — see
[Moving a node to another project](#moving-a-node-to-another-project).

## Quick start

1. Open the **Flow Graph** tab, click **Custom nodes… ▸ New starter node**. This
   writes `flow-nodes/example.flownode` (a commented "Nudge Up" node) and
   reloads. *Or* click **Open flow-nodes folder** and create a file by hand.
2. Edit the file (see the format below), then **Custom nodes… ▸ Reload from
   folder**. The node shows up in the add-menu under its `category`.
3. Right-click the canvas → your category → your node. Wire a trigger's
   `then >` into the node's `> do` pin, set its params, build. Your C++ lands in
   `src/scripts/flow_graph.gen.cpp` exactly where the node runs.

## File format

A `.flownode` file has a small `key = value` header, a line that is exactly
`---`, and then the raw C++ body to the end of the file:

```
# Lines before --- starting with # are comments.
title = Nudge Up
category = Custom
string = object
num0 = Amount
---
ctx.objects[{obj}].data.position[1] += {num0};
ctx.objects[{obj}].dirty = true;
```

### Header keys

| Key         | Meaning                                                        | Default        |
|-------------|----------------------------------------------------------------|----------------|
| `title`     | Display name in the add-menu and the node's title bar          | the file name  |
| `category`  | Add-menu submenu the node is grouped under                     | `Custom`       |
| `string`    | The node's string param: `none`, `text`, or `object`           | `none`         |
| `num0`…`num3` | Labels for up to four numeric params (define them **in order**) | *(no params)* |

- `string = text` gives the node a free-text field; its value is available as
  `{str}`.
- `string = object` gives the node an **object** dropdown (and an object input
  pin) like the built-in object actions. The chosen object — or `self` (the
  object owning the graph) when left empty, or whatever is wired into the object
  pin — resolves to `{obj}`.
- Numeric params must be contiguous from `num0`: defining `num0` and `num2` but
  not `num1` stops at one param. Each renders as a drag field.

### The C++ body

Everything after `---` is emitted **verbatim** into the generated flow graph,
inside the block that runs when an incoming exec link fires the node (a custom
node is always an *action*: it has a `> do` exec input and no exec output). The
code runs in the generated `Script::update`, where `ctx` is a `ScriptContext&` —
the same object the built-in nodes and your object scripts use (see
[object-scripts.md](object-scripts.md) for the `ScriptContext` reference).

Placeholders substituted at build time:

| Placeholder      | Becomes                                                          |
|------------------|------------------------------------------------------------------|
| `{obj}`          | resolved target object **index** (the object dropdown / pin, or `self`) |
| `{self}`         | index of the object that owns this graph                         |
| `{num0}`…`{num3}`| the numeric params as float literals (e.g. `5.0F`)               |
| `{int0}`…`{int3}`| the numeric params as integer literals (e.g. `5`)                |
| `{str}`          | the string param as a quoted, escaped C string (e.g. `"hi"`)     |

`{obj}` and `{self}` are indices into `ctx.objects[...]`; when `string` is not
`object`, `{obj}` resolves to `{self}`. Example uses:

```cpp
// Move the target object up by the Amount param:
ctx.objects[{obj}].data.position[1] += {num0};
ctx.objects[{obj}].dirty = true;

// Log the Text param N (Times) times:
for (int i = 0; i < {int0}; ++i) TYRA_LOG({str});
```

Because the body is raw C++, it must **compile against the engine**. If it
doesn't, the game build fails with a normal compiler error pointing at
`flow_graph.gen.cpp` — read the emitted file to see exactly what your node
produced. Keep to the `ctx` API the built-in nodes use unless you know the
engine headers.

## Moving a node to another project

A custom node's **identity is its file name** (`nudge.flownode` →
`custom:nudge`), and graphs reference it by that identity. To reuse a node in
another project:

1. **Copy the `.flownode` file** into the other project's `flow-nodes/` folder
   (keep the file name identical), then **Reload from folder** there (or just
   reopen the project). The node is now available in that project's add-menu.
2. Any graph you build in the second project can use it immediately. If you also
   copied graphs that *already reference* the node (e.g. you copied an object
   between projects, or hand-merged `.tyra` data), those references resolve as
   long as the file name matches.

> ⚠️ **The file must travel with the project.** When the editor loads a project,
> a flow-graph node whose type is unknown is **dropped** (this is how stale
> node types get cleaned up). So if you copy a `.tyra` that uses `custom:nudge`
> but forget the `nudge.flownode` file, those nodes silently disappear on load
> and are gone on the next save. Copy the file *first*. This is why custom nodes
> live in plain files next to the project rather than baked into the `.tyra`.

There is no central "node library": each project owns its `flow-nodes/` folder.
Keep a folder of your favorite `.flownode` files somewhere and copy them into new
projects as needed. They are plain text — version them, share them, diff them.

## Reference

- The folder is `<project>/flow-nodes/`, scanned for `*.flownode` files in
  alphabetical order (that order is the add-menu order within a category).
- Loading happens on project open and on **Custom nodes… ▸ Reload from folder**.
  Reload picks up new files and edits to existing ones (labels, category, code)
  without reopening the project.
- Parse or duplicate-id problems are reported in the editor status bar on reload
  (e.g. `2 custom flow nodes loaded; foo.flownode: duplicate node id`).
- Implementation: `src/flownode.cpp` (loader/parser), `src/flowgraph.hpp`
  (registry + `CustomFlowNode`), code generation in
  `flowGraphScript()` (`src/templates.cpp`).

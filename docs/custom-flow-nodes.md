# Custom flow-graph nodes

The Flow Graph ships with a fixed set of built-in nodes (triggers, object
actions, audio, save data, logic gates…). When you need one it doesn't have,
define your own — **per project, no editor rebuild** — as a `.flownode` text
file in the project's `flow-nodes/` folder. It appears in the Flow Graph
add-menu and wires up like any built-in node.

Two flavors, from quick to fully general:

- **Inline snippet** — a few lines of C++ written straight into the `.flownode`
  file, with `{placeholders}` for the node's params/target. Great for
  one-liners ("nudge this object up by N").
- **C++-backed node (`call = fn`)** — the node calls a function you write in a
  real, user-owned C++ file (`inc/scripts/flow_nodes.hpp`), with full
  IntelliSense and no length limit. This flavor can have **input and output
  pins of any kind** (object / position / bool / text), so a node can, say,
  pick the object the player is looking at and hand it to a built-in *Set
  Object Visible*.

Both are self-contained files, so moving a node to another project is a copy —
see [Moving nodes to another project](#moving-nodes-to-another-project).

![The custom-nodes example graph in the Flow Graph tab: On Button (Cross) fires a custom "Nearest Visible" node whose object output is wired into a built-in "Set Object Visible"; On Button (Square) fires a custom "Spin By" node. Round pins carry execution, square pins carry object ids.](img/flow-graph.png)

*The [custom-nodes example](../examples/custom-nodes): two custom nodes
(`Nearest Visible`, `Spin By`) wired next to a built-in action.*

## Quick start

1. Open the **Flow Graph** tab → **Custom nodes… ▸ New starter node**. This
   writes `flow-nodes/example.flownode` (a working "Nearest Object" C++-backed
   node) and reloads.
2. Edit the file (format below), then **Custom nodes… ▸ Reload from folder**.
   The node appears in the add-menu under its `category`.
3. Right-click the canvas → your category → your node. Wire it up, build. Your
   code lands in `src/gen/flow_graph.gen.cpp` exactly where the node runs.

## File format

A `.flownode` file is a `key = value` header, a line that is exactly `---`,
then (for inline nodes) the C++ body:

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

| Key         | Meaning                                                         | Default        |
|-------------|-----------------------------------------------------------------|----------------|
| `title`     | Display name in the add-menu and node title bar                 | the file name  |
| `category`  | Add-menu submenu                                                 | `Custom`       |
| `desc`      | What the NODE does — shown as the node's tooltip in the editor and fed to the AI flow-graph generator's catalog, so the node documents itself | *(empty)*      |
| `string`    | The string param: `none`, `text`, or `object`                   | `none`         |
| `num0`…`num3` | Labels for up to four numeric params (define them in order)   | *(no params)*  |
| `tip0`…`tip3` | What each numeric param DOES — one line, shown when the cursor rests on that widget inside the node, and listed under `desc` in the node's own tooltip | *(empty)*   |
| `tip_string` | The same for the string param                                  | *(empty)*      |
| `in`        | Extra input pins: any of `object position bool text`            | *(none)*       |
| `out`       | Output pins: any of `object position bool text`                 | *(none)*       |
| `exec_out`  | `true` = a follow-up exec output that fires downstream after the node runs | `false` |
| `call`      | Name of a C++ function in `inc/scripts/flow_nodes.hpp` to run    | *(inline)*     |

Every custom node is an **action**: it has a `> do` exec input and runs when a
trigger (or another node's exec output) fires it. `string = object` and
`in = object` both give the node its object input (the "target"). Numeric
params must be contiguous from `num0`; their **tips are not** — `tip2` without
`tip0` is fine, and a `tipN` with no matching `numN` is simply dropped.

**Write the tips.** `desc` explains the node, a tip explains the knob —
someone hovering a drag labelled `Amount` wants to know what the number means
and what 0 does, not what the node is for. Custom params are one project's
idea, so a knob without a tip is the half of the help the reader actually
needs. The built-in registry holds itself to the same rule
(`FlowNodeType::numTips` in `src/flowgraph.hpp`).

### Inline C++ body

Without `call`, everything after `---` is emitted verbatim into
`flow_graph.gen.cpp` where the node runs. `ctx` is the `ScriptContext`.
Placeholders substituted at build:

| Placeholder      | Becomes                                                     |
|------------------|-------------------------------------------------------------|
| `{obj}`          | resolved target object index (the object pin/dropdown, or self) |
| `{self}`         | index of the object that owns this graph                    |
| `{num0}`…`{num3}`| numeric params as float literals (`5.0F`)                   |
| `{int0}`…`{int3}`| numeric params as integer literals (`5`)                    |
| `{str}`          | string param as a quoted, escaped C string (`"hi"`)         |

Inline nodes have no outputs — use `call` for those.

## C++-backed nodes (`call = fn`)

For real logic (multi-line, helper functions, `#include`s) and for
**outputs**, set `call = someFunction` and write that function in
`inc/scripts/flow_nodes.hpp` — a generated, user-ownable file (delete its
first `// Generated by…` line to stop the editor regenerating it, exactly
like `script.hpp` / `controls.hpp`). Signature:

```cpp
void someFunction(ScriptContext& ctx, FlowNodeIO& io);
```

`FlowNodeIO` (defined in `script.hpp`) carries the node's inputs and outputs:

```cpp
struct FlowNodeIO {
  int self;              // object that owns the graph
  // inputs (filled before the call):
  int object;            // the target object input (or self); -1 if invalid
  Tyra::Vec4 position;   // wired position input
  bool boolIn;           // OR of wired bool inputs
  const char* text;      // first wired text input ("" if none)
  const float* num;      // the node's num0..3 params
  const char* str;       // the string param (when string = text)
  // outputs (write the ones your node declares in `out`):
  int objectOut;         // an object index, e.g. a pick/raycast result (-1 = none)
  Tyra::Vec4 positionOut;
  bool boolOut;
  char* textOut; int textOutCap;  // write up to textOutCap bytes (NUL-terminated)
};
```

The node runs when an exec link reaches it: the game fills the inputs, calls
your function, then **latches** whatever you wrote into the outputs.
Downstream nodes read that latched value.

### Example: an object output driving a built-in node

The scaffolded `example.flownode` is a `Nearest Object` node — a stand-in for
a "what am I looking at" raycast:

```
title = Nearest Object
category = Custom
out = object
exec_out = true
call = flowExampleNearest
---
```

and in `flow_nodes.hpp`:

```cpp
inline void flowExampleNearest(ScriptContext& ctx, FlowNodeIO& io) {
  int best = -1; float bestDist = 0.0F;
  for (int i = 0; i < ctx.objectCount; ++i) {
    if (i == io.self || !ctx.objects[i].active) continue;
    const float* p = ctx.objects[i].data.position;
    const float dx = p[0]-ctx.playerPosition.x, dy = p[1]-ctx.playerPosition.y,
                dz = p[2]-ctx.playerPosition.z;
    const float d = dx*dx + dy*dy + dz*dz;
    if (best < 0 || d < bestDist) { best = i; bestDist = d; }
  }
  io.objectOut = best;  // -1 = nothing
}
```

Wire it up: **On Button (Cross)** → *Nearest Object*, then its **object
output** into a built-in **Set Object Visible**'s object input, and its **exec
output** into that same node's `> hide` pin. Pressing Cross now hides whatever
object was nearest, picked at runtime. The exec link sequences it: the node
runs (sets its output) first, then the built-in reads it.

### How outputs reach other nodes

- **object out** — the graph normally resolves object identity at *build* time
  (names → indices). A custom node's object output is instead a **runtime
  value** (`io.objectOut`), so it can be a pick/raycast result. Any consumer —
  built-in *Set Object Visible* / *Move Object*, another custom node, etc. —
  reads that runtime index. A built-in action fed such a ref is
  **bounds-guarded**: `-1` or out of range makes it a no-op, not a crash.
- **bool / text / position out** — read directly wherever the matching plane
  is consumed (a bool output into a logic gate or *On Condition*; a text
  output into *Log Message* / *Set Save Text*; a position output into *Set
  Object Position*).

Outputs hold their **last** value: they update only when the node runs, so
drive data-producing custom nodes from the exec flow (that's what `exec_out`
is for) before the consumer reads them.

## Moving nodes to another project

A custom node's **identity is its file name** (`nudge.flownode` →
`custom:nudge`), and graphs reference it by that identity. To reuse a node
elsewhere:

1. **Copy the `.flownode` file** into the other project's `flow-nodes/`
   folder (keep the file name identical), then **Reload from folder** (or
   reopen the project).
2. **For a `call = fn` node, also copy the function** into that project's
   `inc/scripts/flow_nodes.hpp` (un-marked — first line deleted — so the
   editor won't overwrite it). Inline nodes need no extra file.

> ⚠️ **The file must travel with the project.** When the editor loads a
> project, a flow-graph node whose type is unknown is **dropped** (that is how
> stale node types get cleaned up). Copy a `.tyra` that uses `custom:nudge`
> without the `nudge.flownode` file and those nodes silently disappear on load
> and are gone on the next save. Copy the file *first*. This is why custom
> nodes live in plain files next to the project rather than baked into the
> `.tyra`.

There is no central node library: each project owns its `flow-nodes/` folder
and its `flow_nodes.hpp`. Keep a folder of favorite `.flownode` files (and
their functions) and copy them into new projects. They are plain text —
version them, share them, diff them.

## Custom nodes vs. object scripts

For full C++ that is **not** wired into the flow graph, use an **object
script** instead ([object-scripts.md](object-scripts.md)): a class you attach
to an object, with `onStart`/`onUpdate`/`onUsed`. Custom flow nodes are the
right tool when the logic should be **triggered from and wired into the
graph** (a trigger fires it, pins carry data in and out).

## Reference

- Folder: `<project>/flow-nodes/`, scanned for `*.flownode` in alphabetical
  order (that is the add-menu order within a category).
- Loaded on project open and on **Custom nodes… ▸ Reload from folder** (picks
  up new files and edits without reopening).
- **Custom nodes… ▸ Open in VS Code** opens the project (whole-project
  context, so IntelliSense resolves) and jumps to `flow_nodes.hpp`; **Jump to
  node file** opens a specific `.flownode`. Opening also installs the TyraX
  VS Code extension (**Custom nodes… ▸ Install VS Code extension** does it on
  demand): syntax highlighting, snippets and validation for `.flownode` files
  — see [the VS Code extension](vscode-extension.md).
- C++ bodies live in `inc/scripts/flow_nodes.hpp` (marker-owned; delete the
  first line to keep your edits). `FlowNodeIO` is defined in
  `inc/scripts/script.hpp`. As a normal project header, `call = fn` bodies
  get **full C++ IntelliSense** in VS Code (via the generated
  `.vscode/c_cpp_properties.json`). Keep the `.flownode` file a thin manifest
  — real logic goes in `flow_nodes.hpp` — but the extension still colours and
  checks its header and inline body.
- Parse / duplicate-id problems are reported in the editor status bar on
  reload.
- Implementation: `src/flownode.cpp` (loader/parser), `src/flowgraph.hpp`
  (registry + `CustomFlowNode`), code generation in `flowGraphScript()`
  (`src/templates.cpp`).

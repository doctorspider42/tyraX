# custom-nodes example

A tiny FPP scene showing **project-defined custom flow-graph nodes** — Flow
Graph nodes you add in a text file, no editor rebuild. Full guide:
[docs/custom-flow-nodes.md](../../docs/custom-flow-nodes.md).

Open `custom-nodes.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn facing three crates — **red** (nearest), **green**, **blue**.

- **Press Cross** → the nearest still-visible crate disappears. Press again and
  the next one goes, then the last. This runs a **C++-backed** custom node whose
  **object output** feeds a built-in **Hide Object**.
- **Press Square** → the green crate spins 45° each press. This runs an
  **inline-snippet** custom node.

## How it is wired

The player object carries the graph (open the **Flow Graph** tab). Two custom
nodes live in `flow-nodes/`:

- **`nearest-visible.flownode`** — a `call = fn` node with an **object output**
  and `exec_out`. Its C++ lives in `inc/scripts/flow_nodes.hpp`
  (`flowNearestVisible`): it scans the scene for the closest visible object and
  writes it to `io.objectOut`. That is a *runtime* object reference (known only
  in-game), and the graph wires it straight into a built-in **Hide Object** —
  the whole point of C++-backed nodes with pins. `exec_out` fires the Hide right
  after the pick, sequencing the two. The built-in action is bounds-guarded, so
  once every crate is hidden the last press is a harmless no-op.

  ```
  On Button (Cross) ──exec──▶ Nearest Visible ──exec──▶ Hide Object
                                    └────── object ──────┘
  ```

- **`spin.flownode`** — an **inline** node: a two-line C++ snippet with
  `{obj}` / `{num0}` placeholders, no separate function. It nudges the target
  object's yaw by *Degrees*. Here it targets `crate-2` (the green one).

  ```
  On Button (Square) ──exec──▶ Spin By (crate-2, 45°)
  ```

  It is kept here as the smallest possible inline-node example. For real work
  the editor now has built-ins that do this and more: **Rotate Object By** (the
  same nudge, any axis) and **Spin Object** (continuous rotation the game
  integrates itself). Reach for a custom node when the editor has no built-in,
  not to re-implement one.

## Reusing these nodes

A node's identity is its **file name**. To use `Spin By` in another project,
copy `flow-nodes/spin.flownode` into that project's `flow-nodes/` folder. For
`Nearest Visible` also copy the `flowNearestVisible` function into that
project's `inc/scripts/flow_nodes.hpp` (an inline node needs no extra file).

`inc/scripts/flow_nodes.hpp` here is **owned** (its editor marker line was
removed) so the editor keeps the hand-written function on every build.

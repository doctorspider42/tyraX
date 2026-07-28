# Live Logic — edit a flow graph, no rebuild

Live Link streams object state into the running game. The Live Debugger streams
back what ran. **Live Logic streams the program itself**: change a flow graph in
the editor and the game's behavior changes on the PlayStation 2 within a fraction
of a second — no Docker, no `make`, no reboot.

This is the last thing in the pipeline that always needed a rebuild.

## Using it

1. Build profile **debug** (*Project > Preferences > Build*) and **Live Logic**
   on (same page, or *Build > Live Logic*; on by default).
2. Build & run once — **F5** (PCSX2) or **F6** (a console over ps2link).
3. Edit any graph: change a timer, a color, a radius, rewire the exec chain, add
   a trigger. The running game picks it up on its next poll (~6 frames).
4. The toolbar shows **LOGIC (n)** while *n* graphs are running from the
   editor's patch. Click it (or *Tools > Debugger* > **Logic**) for the details.

The next real build folds the edits back into native C++ and the patch is
dropped automatically.

## What can be hot-patched

The interpreter implements the node families you actually iterate on:

| Family | Nodes |
|---|---|
| Triggers | On Start, On Update, Every N Seconds, On Button, Near Object, On Condition |
| Object | Set Object Visible, Move Object By, Move Object To, Set Object Position, Rotate Object By, Set Object Rotation, Spin Object, Set Object Color, Spawn Player At |
| Scene | Set Sky Color, Switch Scene, Set Fog, Set Bloom, Set Grain, Set Particles |
| HUD | Set HUD Visible, Set Text Visible |
| Variables | Set Int (both pins: set / add), Set Bool (set / toggle), Set Position (+ Get Bool / Int At Least / Get Position as sources) |
| Save | Set Save Value, Add To Save Value (+ Value At Least) |
| Logic | AND, OR, NOT, NAND, XOR, XNOR, Is Visible |
| Time / Debug | Delay, Log Message |
| Data | Self, Get Position (unwired) |

Everything else still needs a build: audio, AI (Patrol/Chase/Flee), animation,
Spawn/Despawn Object, sequences, menus, runtime text (Display Text), save texts,
layers, grading/ambience, custom `.flownode` nodes, Raycast — plus any graph
using a **runtime object reference** (a spawned clone, a raycast hit), the
**text plane**, or the **number plane** — an instruction carries its `num[4]` as
constants resolved when the editor compiles, so a *wired* value has nowhere to
live in the IR. A graph with a number link is reported rather than run with the
node's typed-in param, which would be the one failure worse than needing a
build. The editor tells you which graph and why, per graph, in the
Debugger's **Logic** tab, and the chip turns amber **LOGIC (rebuild)**.

A graph that did not exist at build time is also a rebuild case: Live Link can
spawn a new *object*, but it cannot give one logic.

## How it works

The editor compiles, the game interprets.

1. **Codegen writes what the build knows.** `src/gen/livelogic.built` lists every
   graph the ELF compiled natively, with a hash of its content (node positions
   excluded — dragging a node is not a logic change).
2. **The editor compiles the difference.** `App::liveLogicTick` (~7 Hz) hashes
   every live graph, and for each one that differs runs `livelogic::compile`
   ([`src/livelogic.hpp`](../src/livelogic.hpp)): a **pre-resolved instruction
   list**. Object references become runtime indices, variables / save values /
   HUD texts / scenes become table indices, positions become literal / variable /
   object operands, and bool conditions become a tiny RPN program. Exec chains
   are linearized into blocks the same way codegen linearizes them, so a `Delay`
   owns the block it arms.
3. **It lands as `bin/livelogic.bin`** — same host: filesystem as Live Link and
   the Live Debugger, so PCSX2 and a real console are the same code path. Written
   atomically; only rewritten when the compiled bytes change (a rewrite resets
   the patched graphs' state).
4. **The game runs it.** `src/gen/live_logic.gen.cpp` (generated) polls the file,
   resolves each program's owner by the stable object-id hash baked into
   `scene_data.hpp`, and runs the instructions. The natively compiled script for
   that object starts with `if (livelogic::patched(scene, index)) return;` — so
   exactly one of the two runs, never both.
5. **Delete the patch and native logic resumes** (the editor removes it as soon
   as the graphs match the build again; the game logs both events).

Because the opcode numbering, the block kinds and the condition ops all come
from `src/livelogic.hpp`, the interpreter's enums and dispatch switch are
**generated from that header** — and a missing interpreter case is a `#error` in
the generated file, not a silently dead opcode.

### It shares everything with the compiled code

A patched graph writes the same `flowInt`/`flowBool`/`flowPos` arrays (through
accessors emitted next to them), the same save values, the same `RuntimeObject`
state — so a patched graph and a native one can drive each other, and Live
Link's object patching keeps working. A patched graph also still reports to the
**Live Debugger** (its instructions carry the same node keys), so node
highlighting, hit counters, breakpoints and the timeline work on hot-patched
logic exactly as they do on compiled logic.

### Cost

In a debug build with the preference on: one `fopen` every 6 frames (25 under
ps2link), a switch per instruction that runs, and ~55 KB of static arrays for
the loaded program. Nothing on the GS. In a release build — or with the
preference off, or in a project with no graphs — the generated runtime is an
empty translation unit, `patched()` is a compile-time `false`, and the check in
each compiled script folds away.

## Limits

- **The node subset above.** This is deliberate: an interpreter case is a second
  implementation of a node's semantics, and every one of them is a twin that can
  drift from `flowGraphScript`. The families that matter for iteration are in;
  the rest report honestly instead of silently doing nothing.
- **Structure, not identity.** Patching addresses graphs by their owner object's
  stable id, so renames and reorders are non-events — but an object added since
  the build has no logic in the game at all.
- **State resets on each new patch.** A rewritten program starts with fresh
  timers/edges (`On Start` fires again). Unchanged graphs are never rewritten, so
  editing graph A does not restart graph B.
- **A patched graph is interpreted**, so it is slower than compiled C++ — matters
  only for something running hundreds of instructions per frame.
- ps2link (real hardware) uses the same code path on a 25-frame poll; the
  verified target is PCSX2 (see PROGRESS 192).

## Related

- [live-link.md](live-link.md) — object state into the running game.
- [live-debugger.md](live-debugger.md) — what the graphs are doing, breakpoints,
  step, the rewindable timeline. It is also the instrument that proves a patch
  took effect (hit counters change rate the moment the patch lands).
- [custom-flow-nodes.md](custom-flow-nodes.md) — project-defined nodes; these are
  C++ bodies, so they are outside the interpreter's reach by construction.

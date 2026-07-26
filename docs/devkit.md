# The devkit — and the promise that a shipped game pays nothing for it

TyraX has grown a development kit for the PlayStation 2: the editor talks to a
game running on the console, and the game talks back.

| Layer | Direction | What it does |
|---|---|---|
| [Live Link](live-link.md) | editor → game | object transforms/colors, adds/deletes, texture hot reload |
| [Live Debugger](live-debugger.md) | game → editor **+** commands back | what the graphs run, breakpoints, pause/step, watches, timers |
| [Live Logic](live-logic.md) | editor → game | the flow-graph **program** itself, no rebuild |

All three ride one channel: the host filesystem the game already loads assets
from (PCSX2's *Host Filesystem*, the ps2link file server on real hardware). No
extra transport, no engine debug stub, no devkit hardware.

## The promise: release builds carry nothing

The devkit exists only in **debug** builds, and "only" means *literally* — not
"disabled at runtime", not "a few dead branches", not "some tables nobody
reads":

- each generated runtime (`live_link.gen.cpp`, `live_debug.gen.cpp`,
  `live_logic.gen.cpp`, `live_tex.gen.cpp`) becomes an **empty translation unit**;
- the generated headers keep the API but every entry point is an `inline` no-op,
  and the predicates (`livedbg::halted()`, `livelogic::patched()`) become
  compile-time `false`, so the calls in the game loop and in every compiled flow
  graph **fold away** rather than branch;
- the instrumentation inside `flow_graph.gen.cpp` (`livedbg::hit`,
  `livedbg::timer`, the force-fire duplicate of each trigger branch) is not
  emitted at all;
- nothing polls, nothing writes, and the static arrays the debugger and the
  interpreter need (~145 KiB of `.bss`) do not exist.

### And the promise is checked, not asserted

The PS2 toolchain strips the symbol table, so each devkit runtime plants a
deliberate marker string (`TXDEVKIT-<layer>`) and the audit looks for that plus
the channel file names — signals that cannot survive if the code was not
compiled in.

```bash
tyrax-editor --audit-release <projectDir>
```

Exit code 0 = clean, 1 = something leaked (so a script can gate a release on it).
**Every release build runs the same audit automatically** and prints the verdict
into the build log:

```
[editor] Release audit: clean - ELF 2641 KiB (text 1830 KiB, data 205 KiB, bss 139 KiB), stripped (marker + string scan)
```

Measured on the same project, same assets, same code — only the profile changed:

| | text | data | bss | verdict |
|---|---|---|---|---|
| debug (devkit on) | 1848 KiB | 205 KiB | **284 KiB** | 4 devkit findings |
| release | 1830 KiB | 205 KiB | **139 KiB** | clean |

So the devkit costs ~18 KiB of code and ~145 KiB of RAM **while you are working**,
and exactly nothing in what you ship. The reader behind this
([`src/elfsym.hpp`](../src/elfsym.hpp)) is a small ELF32 parser; it is also the
foundation for reading named memory off a running game later.

## Turning it off while still in debug

Each layer is its own project preference (*Project > Preferences > Build*, or the
*Build* menu): **Live Link**, **Live Debugger**, **Live Logic**. Turning one off
compiles it out of the debug build too — the same empty-TU path — which is the
honest way to measure "what does my game do without the devkit" without
switching profiles.

## What the debugger gives you today

Beyond breakpoints and stepping (see [live-debugger.md](live-debugger.md)):

- **Armed timers.** Every `Delay` counting down reports itself, so the panel
  shows *"⏱ 1 armed timer, next in 1.2 s"* instead of leaving you wondering why
  the branch after it never came. A `Delay` only advances on frames that **run** —
  which is exactly why a single-frame *Fire* looks like it did nothing.
- **Fire and continue.** *Fire now* runs a trigger's branch for one frame;
  *Fire and continue* resumes the game afterwards, so whatever the branch armed
  (a `Delay`, a `Move Object To` glide) actually gets frames to finish.
  Shift-click the Fire button for the same thing.
- **Object watch.** Name up to 8 runtime objects and the game samples them
  **every frame** (position, rotation, scale, color, visible/active/dirty) into a
  ring it flushes whole — so the editor draws a real 50 Hz curve per axis, not
  one point per flush. The path is also drawn **in the viewport** as a trail with
  the head marking where the object is right now. This is the "where is it
  actually, and what did it do a second ago" tool.

## What is not here yet

- **Named memory.** The ELF reader can already map names to addresses, but the
  shipped ELF is stripped, so a symbol-driven memory watch needs the build to
  keep a map file first. The channel for it (a read request + a response block)
  is a small addition once that exists.
- **Live perf graphs.** The engine already computes per-phase EE times, free RAM
  and GS VRAM residency, but prints them as text (see
  [profiling.md](profiling.md), [gs-vram.md](gs-vram.md)); streaming them over
  this channel would make them curves next to the object watches.
- **EE/VU instruction-level debugging.** Deliberately out of scope: PCSX2 already
  has a debugger, and on real hardware the sane path is a GDB stub over ps2link
  rather than a hand-rolled disassembler. What this devkit does instead is show
  the *game's own* state — objects, graphs, variables, timers — which is where
  the bugs of a project like this actually live.

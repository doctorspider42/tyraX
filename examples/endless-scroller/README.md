# endless-scroller example

An infinite tunnel built from **one** authored chunk. A **Scroller object**
tiles a segment — a floor slab, two walls and a bright arch beam — along its
axis forever and slides the copies toward you, so the corridor streams past to
the horizon without a single object placed past the first ring. The "level
generator in a train window": author one piece, watch it repeat to infinity.

Open `endless-scroller.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn (first-person) in the mouth of the tunnel looking down its length.
The walls, floor and cyan arch beams recede to a vanishing point and **scroll
toward you** — each arch sweeps overhead and vanishes behind as the next
appears far ahead. Walk forward (left stick) and the belt keeps generating
corridor ahead of you; you can never reach the end. Try turning around: the
belt fills behind you too, out to the *keep-behind* distance.

## How it's wired

- **tunnel-belt** is the Scroller (Insert > World > Scroller (endless)),
  invisible in the game, its +Z pointing down the corridor. Speed is **−7**
  units/s (negative = the content flows toward the player), the window is
  **45 ahead / 24 behind**, and it runs at start.
- Its single segment **tunnel-ring** (length **3**) lists four member objects —
  `floor`, `wall-left`, `wall-right`, `arch-beam` — authored once around the
  origin. The floor and walls are 3 units deep so they tile into a seamless
  corridor; the arch beam is thin, so a distinct arch punctuates every 3 units
  and reads as the motion.
- The floor and walls are **Plane primitives** (the walls rotated 90°), not
  boxes, and sit slightly above the terrain — planes have no end caps, so the
  joints between consecutive copies show no flickering seam line; the belt's
  *Seam overlap* (0.05 here) makes neighbors interpenetrate slightly so no
  crack ever opens. The freestanding arch stays a box. (See "Seams" in
  [docs/endless-scroller.md](../../docs/endless-scroller.md).)
- At build the editor bakes enough clones of those four objects to fill the
  window (see `SCROLLERS` / `SCROLLER_CLONES` in `inc/scene_data.hpp`) and the
  generated `src/gen/scroller.gen.cpp` slides them each frame. The authored
  members are hidden templates — edit them and the whole tunnel changes.

Change the look with almost no work: retint or resize the four members, add a
second segment (e.g. a wider "cavern" ring) so the tunnel alternates, or drive
**Set Scroller Speed** from the flow graph to accelerate as the game ramps up.

Full guide: [docs/endless-scroller.md](../../docs/endless-scroller.md).

Generated object values live in `src/gen/scene_objects.gen.cpp`;
`inc/scene_data.hpp` keeps stable declarations. Counts and object IDs live in
the same data file, so ordinary moves, color edits, additions and removals can
rebuild it alone. Changes to features or derived tables can still rebuild consumers.

Generated game methods are split between `src/terrain_game.cpp` and the
`src/gen/game_*.gen.cpp` subsystems, with shared inline helpers/state in
`inc/game_runtime.gen.hpp`. Header changes can compile these units in parallel.
The main file remains user-ownable; generated subsystem files refresh on build.


Hardware timeline instrumentation matches the 1.170 coarse/detail emitter.
Use the [capture and observer-control guide](../../docs/hardware-profiler.md)
when comparing performance; traces add measurable work.

This example explicitly requests `framePipeline: true` (TyraX2). Its authored interlaced output preserves the existing scan mode and quality; ordinary interlaced output is eligible, unlike true field rendering. True field rendering, BLSS, unlimited triple buffering and unavailable queue memory fall back safely. This request does not guarantee 60 FPS. See [TyraX2](../../docs/tyrax2.md).

Generated runtime snapshot refreshed for [dynamic light receivers](../../docs/dynamic-light-receivers.md). This example retains the default **All objects** policy; Players and driven vehicle is an optional project or scene setting.

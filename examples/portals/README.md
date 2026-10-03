# portals — linked Portal surfaces with a live through-view and seamless teleport

A minimal demo of the **Portal object** ([docs/portals.md](../../docs/portals.md)):
two linked portals across the map, a live view through the surface, and a
walk-through teleport that carries position, view angle and vertical velocity.

Open `portals.tyra` in the editor and Build & Run (or `run.ps1`). Walk into
the orange surface: you arrive at the cyan portal facing the tower — the same
image the surface was showing — and both portals link back at each other, so
you can walk straight back. The demo is pad-driven; in the editor viewport,
the bright arrow out of each surface marks the entry side.

What's in the scene:

- **portal-a** (orange, upright) in front of the player spawn, two-way linked
  with **portal-b** (cyan) 25 world units away — both mounted flush on dark
  wall boxes, so each reads as a doorway in a wall. Looking at portal-a you
  see portal-b's surroundings — the red tower — rendered live through the
  opening at full resolution (in-place, z-carved: no render-to-texture, no
  seam), with correct parallax as you move. The wall behind the far portal
  never shows its backside in the opening: the dead-zone test projects each
  object's exact OBB extent onto the exit plane, so flush-mounted walls
  count as behind.
- **box-red** — the landmark tower at the far end, with **fire-b**, a fire
  emitter burning at its base. Both show live through portal-a — the fire's
  billboards re-expand on VU1 to face the portal viewer, so particles come
  through the opening, not just solid geometry. portal-a runs the
  experimental **All objects in view** switch (its view list is empty —
  everything on the far side shows up), and so does portal-b: the switch
  overrides its leftover explicit list (`box-green`), the classic render
  budget that works like a Mirror's reflected-object list. Every portal in
  the scene runs the switch.
- **The infinite fall**, in plain view to the right of the spawn:
  **portal-floor** (purple, lying flat ON the ground — the floor-portal
  swallow rule lets bodies sink into it instead of resting on the terrain)
  linked up to **portal-ceiling** (green, hovering ~8 units above, facing
  down), with **Teleport physics objects** on. **box-drop**, a physics cube,
  spawns in the air between them, falls into the floor portal, pops out of
  the ceiling portal with its speed carried through, and falls again —
  forever. Both surfaces run **All objects in view**, and exactly one portal
  view is live per frame — the nearest linked portal the camera faces, every
  other portal showing its tinted quad — so walk under the ceiling portal and
  look up to watch the cube approaching *inside* it before it drops out. You
  can jump into the floor portal yourself (the feet probe catches the drop)
  and join the loop — strafe out to escape. Object physics clamps falls at a
  30 u/s terminal velocity, so the loop stays readable instead of accelerating
  into a blur.

The two dark overhead frame bars use an unrotated horizontal box: their
X/Y scales express the width and thickness directly. The standing-player
box collision path applies yaw only, so rolling a tall box by 90 degrees
would leave an invisible vertical blocker across the doorway. This authored
representation preserves the plain-color rectangular geometry and keeps
both side pylons collidable; it does not disable collision or change the
renderer. Use Square to pick up the nearby sphere, carry it through both
upright portals and use Circle to throw it after returning.

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

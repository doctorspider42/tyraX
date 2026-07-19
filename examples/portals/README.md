# portals — linked Portal surfaces with a live through-view and seamless teleport

A minimal demo of the **Portal object** ([docs/portals.md](../../docs/portals.md)):
two linked portals across the map, a live view through the surface, and a
walk-through teleport that carries position, view angle and vertical velocity.

What's in the scene:

- **portal-a** (orange, upright) in front of the player spawn, two-way linked
  with **portal-b** (cyan) 25 world units away. Looking at portal-a you see
  portal-b's surroundings — the red tower — rendered live through the
  opening at full resolution (in-place, z-carved: no render-to-texture, no
  seam), with correct parallax as you move.
- **box-red** — the landmark tower at the far end. portal-a runs the
  experimental **All objects in view** switch (its view list is empty —
  the tower shows up anyway because everything does); portal-b uses the
  classic explicit list (the render budget, like a Mirror's
  reflected-object list) so the demo shows both modes.
- **The infinite fall**: **portal-floor** (purple, lying flat on the ground)
  linked up to **portal-ceiling** (green, hovering 7 units above, facing
  down), with **Teleport physics objects** on — and **box-drop**, a physics
  cube that spawns in the air between them. It falls into the floor portal,
  pops out of the ceiling portal with its speed carried through, and falls
  again — forever, in plain view to the right of the spawn. Both surfaces
  run **All objects in view**, and up to four portal views are live per
  frame — walk under the ceiling portal and look up to watch the cube
  approaching *inside* it before it drops out. You can jump into the floor
  portal yourself (the feet probe catches the drop) and join the loop —
  strafe out to escape. (Object physics clamps falls at a 50 u/s terminal
  velocity, so the loop stays readable instead of accelerating into a
  blur.)
- **anchor-cross** — an Empty carrying a small flow graph
  (On Start → Delay 6 s → Spawn Player At) that walks the player through
  portal-a unattended ~6 s after the scene starts, so the whole loop —
  see through → cross → arrive exactly where the view promised — plays out
  with no pad input. Delete this object (or its graph) to explore manually.

Open `portals.tyra` in the editor and Build & Run (or `run.ps1`). Walk into
the orange surface: you arrive at the cyan portal facing the tower — the
same image the surface was showing. Both portals link back at each other,
so you can walk straight back.

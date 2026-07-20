# nav-ai example

NPCs with a mind of their own, wired entirely in the flow graph — the
[NavMesh + NPC AI](../../docs/navigation-ai.md) feature: a **guard** that
patrols waypoints around a wall and chases you on sight, and a **rabbit**
that bolts the moment it spots you.

Open `nav-ai.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn in the south-east corner. Ahead of you:

- The **red box (guard)** walks its round: `wp1` → `wp2` → `wp3` → repeat.
  The leg from `wp2` to `wp3` is blocked by the long wall — watch it detour
  around the wall's edge on its own (that's A* over the baked nav grid, on
  the PS2's EE).
- Walk toward the guard's route. The moment you enter its **vision cone**
  (12 units, 100°, terrain line-of-sight) it abandons the patrol and
  **chases** you, stopping at arm's length and staring you down. Outrun it
  past 25 units and it gives up (idles — it has no order to resume; see the
  wiring note below).
- The **yellow box (rabbit)** grazes near your spawn. Get within 10 units of
  its field of view and it **flees** until it feels safe (22 units), then
  stops and waits for you to scare it again.

In the editor, toggle **View > Nav mesh overlay** to see the walkable grid
the NPCs reason over — note the hole the wall punches into it (inflated by
the agent radius) and the blocked ring at the map edge. Tuning lives in
*Project > Preferences > AI navigation*.

## How it is wired

- **guard** (Patrol + Chase, 4 nodes):
  - `On Start` → `Patrol Waypoints` (Prefix `wp`, Speed 2.5, Pause 0.6 s,
    Once off) — the route is every object named `wp1`, `wp2`, … in order.
  - `On Player Seen` (Range 12, FOV 100°, LOS on) → `Chase Player`
    (Speed 3, Stop Dist 1.5, Give Up 25).
- **rabbit** (2 nodes): `On Player Seen` (Range 10, FOV 180°) →
  `Flee From Player` (Speed 4, Safe Dist 22).
- `wp1`–`wp3` are plain **Empty** objects; the wall is an ordinary box —
  anything with box collision blocks the nav grid automatically.

One AI state per object: the Chase **replaces** the Patrol, so after giving
up the guard just stands there. To make it resume, wire the classic loop —
`On Player Seen` bool output → `NOT` → `On Condition` → `Delay` →
`Patrol Waypoints` — everything stays in the graph.

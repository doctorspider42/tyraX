# NavMesh + NPC AI

NPCs that find their own way: a navigation grid baked from the scene on the
**host** at build time, A* pathfinding on the PS2's EE, and four flow-graph
nodes — **Patrol Waypoints**, **Chase Player**, **Flee From Player**, **On
Player Seen** — that turn any scene object into a guard, a monster or a
skittish animal. No scripting required; in 2002 this was the part every
studio wrote by hand from scratch.

Working demo: [examples/nav-ai](../examples/nav-ai) — a guard patrolling
around a wall that chases you on sight, and a rabbit that runs away.

## The nav grid (baked on the host)

At build time the editor rasterizes each scene into a walkable-cell bitmap
(`src/navmesh.cpp` → `inc/nav_data.gen.hpp`). A cell is walkable when:

- its center is inside the playable bounds (the game clamps walkers to one
  unit from the map edge),
- the terrain slope under it is at most **Max walkable slope** (sampled from
  the exact bilinear heightmap the game walks on),
- no blocking object covers it. Blockers mirror the player collision's box
  mode: collidable geometry types with *Collision* ≠ *none*, as an
  axis-aligned box (`.obj` models use their real mesh AABB; animated `.glb`
  objects fall back to their unit scale box), **inflated by Agent radius**
  in XZ. An object low enough to step onto (top ≤ ground + 0.5) or high
  enough to walk under (bottom ≥ ground + 1.8) does not block. Objects with
  *mesh* collision (ramps, stairs) never block — that mode exists to be
  walked on.

Because walkability *is* the terrain surface here, a scene whose terrain was
removed ([docs/terrain.md](terrain.md)) has **no walkable cells at all** and its
agents hold still. A placed floor is something a player can stand on, not
something the nav bake understands - pathfinding over one is not supported yet.

Tuning lives in *Project > Preferences > AI navigation*: **Nav cell size**
(default 1 world unit), **Max walkable slope** (default 40°) and **Agent
radius** (default 0.4). The grid is capped at **128×128 cells** — larger maps
get proportionally bigger cells — which keeps the PS2-side A* working arrays
small and static.

Preview the bake with **View > Nav mesh overlay**: translucent green quads
over every walkable cell, recomputed live as you edit the scene.

**Scenes whose flow graphs use no AI nodes carry zero nav data and zero nav
code** — the generated `nav_data.gen.hpp` / `navigation.gen.cpp` collapse to
stubs, exactly like the other pay-for-what-you-use systems.

## The runtime (A* on the EE)

`src/gen/navigation.gen.cpp` (generated) owns one **AI agent state per
runtime object** — authored objects *and* spawn-pool clones — and a global
script that ticks every active agent each frame:

- **Pathfinding**: A* over the baked bitmap, 8-connected with no corner
  cutting, octile heuristic, integer costs. At most **one pathfind per frame**
  (round-robin across agents), with a hard expansion cap, so a crowd of
  agents can never blow the frame budget. Unreachable goals path to the
  closest reachable cell instead, so a chase pressed against a wall still
  closes in.
- **Path smoothing**: string pulling over the raw cell path — agents walk
  straight lines between a few waypoints instead of stair-stepping the grid.
- **Movement**: agents move at their node's speed (units/s, wall-clock
  normalized), snap to the terrain height plus their authored height offset,
  and **turn to face their motion** with the same shortest-arc yaw lerp the
  third-person avatar uses (models face +Z; `rotation[1]` is the live yaw,
  so a walk animation triggered from the same graph lines up).
- Agents pause with the game (menus), stop when their object is despawned or
  its streaming layer unloads, and reset on scene (re)load.

## The flow nodes (category "AI")

One shared AI state per object — starting a new behavior **replaces** the
current one (a Chase interrupts a Patrol), and **Stop AI Movement** returns
the NPC to idle.

- **Patrol Waypoints** *(Speed, Pause s, Once)* — the target (object link >
  self) walks the scene objects whose names start with the **Prefix** in
  natural order (`wp1`, `wp2`, … `wp10`; use *Empty* objects as waypoints),
  pausing at each. *Once* off (default) cycles forever. The route is resolved
  at codegen — renaming waypoints re-routes on the next build.
- **Chase Player** *(Speed, Stop Dist, Give Up)* — pursue the player,
  repathing twice a second as they move. Within *Stop Dist* the NPC stands
  and keeps facing the player (your melee/attack trigger range); *Give Up* >
  0 drops to idle once the player escapes farther than that.
- **Flee From Player** *(Speed, Safe Dist)* — run away, fanning out sideways
  when the straight-away direction is blocked, until *Safe Dist* from the
  player, then idle.
- **On Player Seen** *(Range, FOV deg, LOS)* — a trigger that fires (rising
  edge, like *Near Object*) when the watched object **sees** the player:
  within *Range*, inside a vision cone of *FOV* degrees around the object's
  facing, and — with *LOS* on — with terrain line-of-sight (a hill hides
  you; other objects do not block sight). Its **bool output** is the live
  "seen right now" condition for the logic gates (e.g. NOT → *On Condition*
  = "player escaped my sight").

The classic wiring, entirely in the graph:

```
On Start ──────────────► Patrol Waypoints  (prefix "wp")
On Player Seen ────────► Chase Player
On Player Seen ─(bool)─► NOT ─► On Condition ─► Delay 3s ─► Patrol Waypoints
```

guard patrols → sees you → chases → loses you for 3 s → resumes the patrol.

## Limitations (era-appropriate on purpose)

- The grid is **static** — baked at build time. Moving/spawned objects do
  not re-carve it at runtime (agents still avoid them only if the author
  keeps routes clear). Live Link edits of blockers need a rebuild to change
  the baked grid.
- Box-mode obstacle footprints ignore rotation (so does the game's box
  collision — the two stay consistent). A long thin wall rotated 45° blocks
  its whole axis-aligned bounding box; prefer axis-aligned walls or mesh
  collision.
- Agents don't avoid **each other** — two NPCs can overlap.
- *LOS* checks terrain only, not objects.
- NPCs walk the terrain surface; there is no flying/jumping navigation.

## Files

| Piece | File |
|---|---|
| Host bake | `src/navmesh.cpp` (shared by codegen and the viewport overlay) |
| Baked grid | `inc/nav_data.gen.hpp` (per scene, bit-packed) |
| A* + agent tick | `src/gen/navigation.gen.cpp` + `inc/scripts/navigation.gen.hpp` |
| Node compilation | `flowGraphScript()` in `src/templates.cpp` |

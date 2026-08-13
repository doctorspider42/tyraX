# Collision boxes

What stops the player, and what the third-person camera's spring arm stops
against, is **a box** — not the mesh you see. This page says which box, where
it comes from, and how to look at it, in the editor and in the running game.

Mesh-accurate collision exists and is opt-in per object (*Collision: mesh*, a
static `.obj` feature — see the Physics section of the README); everything
else collides as the box described here.

## Which box

| Object | Box |
|---|---|
| Box, sphere, cylinder, cone, plane, save point, mirror, portal | the unit cube under the object's position/rotation/**scale** |
| Static model (`.obj` → `.tmdl`) | the **mesh's own bounds**, scaled |
| Animated model (`.glb`/`.fbx` → `.tskl`) | the **baked all-clips pose bounds**, scaled |
| Spawn point, player marker, emitter, sound emitter, point light, empty, decal, camera, area, procedural volume, scroller belt | none — they block nothing |
| Anything with *Collision: none* | none |

Two consequences worth having in your head:

- **A model's box is not centred on the object.** A character or tree
  authored standing on its own origin has its bounds entirely *above* that
  origin, so the box sits above it too. That is what makes it right — the box
  covers the model — and why the box moves when you scale the object rather
  than growing symmetrically.
- **The box is oriented, not axis-aligned.** It is tested in the object's own
  frame, so a wall rotated 30° blocks along its real faces instead of a
  bigger axis-aligned stand-in that juts into the room at the corners. One
  deliberate simplification survives in the walker: the *footprint* uses the
  object's **yaw only**, so a pitched or rolled box still collides upright
  (physics bodies tumble, and projecting a pitched frame onto the ground
  plane is not an isometry — doing it anyway used to teleport the player
  inside thrown crates). Mesh collision is the escape hatch for geometry that
  has to block while tilted. The camera boom does test the full 3D
  orientation.

**Model yaw offset rides along.** An animated model authored X-forward
carries a content-forward correction (*Properties > Model yaw offset*, ±90)
that turns the mesh without touching the facing logic. It turns the **box**
with it. Before, it did not: an X-forward character collided and blocked the
camera across its own body, at 90° to what was on screen — invisible unless
you drew the box, which is half the reason this page and the two overlays
exist.

## Seeing them

**In the editor** — *View > Collision boxes*. Every collider gets a red
wireframe of exactly the box above. Session state, not project data, and it
costs one wireframe draw per object.

**In the game** — *Project > Preferences > Build > Show collision boxes*
(**debug** build profile only, exactly like *Show areas*). Red wireframes
drawn in the running game, following anything that moves — a tumbling physics
body, a prop a flow node slides. It is a **look, not a census**: the nearest
24 colliders within 60 units of the camera are drawn (`COLLISION_BOX_LIMIT` /
`COLLISION_BOX_RANGE` in the generated `terrain_config.hpp`), because each
box is 144 vertices the EE rebuilds every frame. A release build emits none
of it — the flag is a `constexpr`, so the whole pass folds away.

Both overlays and every collision consumer read **one builder**:
`placement::collisionBox` on the host, `TerrainGame::objectCollisionBox` in
the generated game, which are twins. The editor cannot show you a box the
console does not use.

## What has no box

**Runtime-generated geometry** — a procedural volume's merged chunks, a
spawned prefab's static members (docs/procedural-runtime.md,
docs/prefabs.md) — has no scene object behind it, so it has no object box and
neither overlay draws it. It collides through its own conservative world
AABBs (`procColliders`) and a block world through its solid field. Same for
the **terrain**, which is a heightfield and not a box.

**A model still streaming in** collides as a unit cube for the frame or two
before its bounds arrive (assets load one per frame at scene start). It
settles by itself; a wrong box for a moment right after a load is that.

## Where it lives

| Layer | Code |
|---|---|
| Host (editor overlay, placement snapping, the drag/paste raycast) | `placement::collisionBox` / `placement::collides` (`src/placement.cpp`) |
| Generated game (walker, spring arm, split-screen cull, the overlay) | `TerrainGame::objectCollisionBox` / `objectCollides` (`templates.cpp`) |
| Editor overlay draw | `Viewport::setCollisionOverlay` (`src/viewport.cpp`) |
| Game overlay draw | `TerrainGame::renderCollisionBoxes` |

The two builders are a twin pair: change one and the other must follow, or
the editor starts drawing a box the console does not collide with.
`src/placement.cpp` is host-only and links on its own, so the box is
checkable from a ~40-line harness rather than by eye.

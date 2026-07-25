# Areas (invisible volumes)

An **Area** is a scene object with no geometry: an oriented box you place with
the gizmo, drawn in the editor as a wireframe and completely absent from the
game. Nothing collides with it, nothing renders it, it costs no triangles and
no VRAM.

Its job is to replace numbers you would otherwise type by hand. Several editor
features ask "how far?" or "which objects?" and, before areas, answered that
with a radius or a list you maintained one name at a time:

| Feature | Without an area | With an area |
|---|---|---|
| Streaming-layer auto zone | circle: center X/Z + radius | the area's box (bounds height too) |
| Mirror reflected objects | a hand-built list of names | everything the box holds |
| Portal through-view objects | a hand-built list of names | everything the box holds |
| Camera feed (CCTV) view list | a hand-built list of names | everything the box holds |
| A "player is here" trigger | Near Object + a radius | the **In Area** trigger |

Add one with **Add object > Gameplay > Area**. It starts as a room-sized green
box on the ground; Position / Rotation / **Size** in the Properties panel are
the volume, and the color tints its wireframe (nothing else — an area has no
material, no physics, no collision, no draw distance).

The Properties panel of an area lists **what references it** and how many
objects it currently catches, so resizing or deleting one is never a guess.

## Point tests: what "inside" means

An area's box is the unit cube under its transform: rotation order X, then Y,
then Z, half extents = `Size / 2`. A point is inside when its offset from the
area's center, projected onto each of the three rotated axes, stays within that
axis' half extent.

That test exists in exactly two places and they are checked against each other:
`project::areaContainsPoint` (the editor, `project.cpp`) and `pointInArea`
(generated into `inc/scene_data.hpp`, so both the game's `terrain_game.cpp` and
`src/gen/flow_graph.gen.cpp` call the same function). Both were compared over
200k random oriented boxes — no disagreement.

Areas are tested **live** at runtime: move an area with a flow node and its
trigger volume / streaming zone moves with it. An area sitting on a streaming
layer that is not resident is inactive, and then it catches nobody.

## Streaming-layer zones

*Project panel > Layers*, tick **auto-stream**, then pick an area in the combo
next to it. The layer loads while a player stands inside the box and unloads
once both players leave it (plus a hysteresis band, same as the circle's). Pick
`<none>` to go back to the center + radius circle.

The difference that matters: **the box bounds Y**, so one floor of a building
can stream on its own, while a circle always covers the whole column of space.
The initial residency at scene load is decided the same way — from the spawn
point — so `Start loaded` stays greyed out for auto-streamed layers.

See [streaming layers](streaming-layers.md) for the rest of the mechanism.

## Catch areas (mirror / portal / camera feed)

Mirrors, portals and feed cameras all carry an explicit list of objects they
re-draw a second time. That list is deliberately explicit: on the PS2 the cost
of a second submission is real, and a radius would let it grow silently. A
**catch area** keeps that honesty while removing the bookkeeping — set
*Catch area* in the Properties panel and everything the box holds joins the
list.

- Resolved **at build time** (`project::areaCaughtObjects`), not per frame, so
  the count the editor shows next to the picker is exactly what the game will
  re-draw. Add or move a prop inside the area and rebuild to pick it up.
- An object is caught when its bounding sphere (half its largest scale axis)
  touches the box, so a wide crate half inside still counts.
- Only types the game draws as geometry are caught: box, sphere, cylinder,
  cone, plane, save point, model, decal. Markers, lights, cameras, mirrors,
  portals and other areas are never picked up.
- The explicit list still works alongside it and adds objects from outside the
  area; duplicates collapse.
- Caught objects are excluded from static batching, exactly like listed ones —
  a batched member has no bag of its own and would simply vanish from the
  reflection.

The editor viewport previews mirror reflections through the same call, so what
you see in the viewport is what the console re-draws.

## The In Area trigger

*Flow Graph > add node > Triggers > In Area*. Its **Area** param names the
area; **Who** selects whose position is tested (0 = either player, 1 = player 1,
2 = player 2).

- The exec output fires on the **rising edge** — the frame someone enters. It
  does not re-fire while they stay inside.
- Its **bool output** is the live "inside right now" condition. Wire it through
  **NOT** into **On Condition** for an exit trigger, or into the logic gates for
  "inside AND holding the key".
- Its object output is the area itself (useful with Get Position).

Compared with **Near Object** + Radius: the area is a real volume (height
included) and it is shaped and placed visually instead of being a number that
means nothing until you run the game.

## What still uses a distance, on purpose

Sound emitters (`Range`) and point lights (`Radius`) keep their radius: both
describe a *gradient* — volume and brightness fall off with distance — not a
boundary, and a box has no falloff. Per-object draw distance and the terrain
view distance are camera-relative, not places in the world, so an area cannot
express them either.

## Under the hood

- `PrimitiveType::Area = 17`; the object rides in the ordinary scene table
  (type 17 sits in every marker skip list: collision, the USE picker, the
  physics sweep, the Raycast node, navmesh baking, the geometry builder).
- References are by name: `SceneObject::catchArea`, `SceneLayer::streamArea`,
  and the In Area node's `str`. Renames remap all three.
- `SCENE_LAYER_STREAM_AREAS[scene][layer]` is the zone's object index
  (-1 = use the circle).
- Live Link: an area's transform is part of its recipe hash, because catch
  areas expand into baked tables at build. Moving an area during a live session
  therefore flips the toolbar chip to **LIVE (rebuild)** instead of showing you
  half the edit.

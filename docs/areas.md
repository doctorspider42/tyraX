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
| Mirror reflected objects | a hand-built list of names | everything the box holds (optionally per frame) |
| Portal through-view objects | a hand-built list of names | everything the box holds (optionally per frame) |
| Camera feed (CCTV) view list | a hand-built list of names | everything the box holds (optionally per frame) |
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

The generated side splits the work in two so a caller testing many points
against one area pays for the trig once: `areaBasis` builds the box's center,
rotated axes and half extents (and short-circuits the six trig calls when the
area is unrotated, which is the usual case), `areaDistSq` is the squared
distance from a point to that box, and `pointInArea` is `areaDistSq(...) <= 0`.

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
  re-draw. Add or move a prop inside the area and rebuild to pick it up —
  unless you tick *Update every frame* (below).
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

### Update every frame (live catch areas)

A build-time list cannot notice a crate rolling into the room, or the player
walking in front of the glass. Tick **Update every frame** under the picker and
the volume is re-tested every frame instead: an object joins the reflection /
through-view / feed the moment it crosses the boundary and drops out when it
leaves.

The interesting part is what it does *not* cost:

- **Only movable objects are re-tested.** The candidate set is
  `project::areaLiveCandidates`: physics bodies, pickables, usables, save-state
  objects, layer members, anything with its own flow graph or script, and
  anything a flow node / cutscene track / target list names. A static room full
  of props contributes nothing to the per-frame work — whatever the box holds
  that *cannot* move is still resolved at build and baked into the fixed list.
  The Properties panel prints the split: `3 fixed + 1 of 6 movable inside now`.
- **The per-frame test is a handful of dot products.** The area's rotated basis
  is built once per pass (`areaBasis`), and an unrotated area skips the trig
  entirely; each candidate is then three dots and a compare against its
  bounding sphere. The candidate list is baked into `CATCH_CANDIDATES`, sliced
  per owner, so nothing is searched at runtime.
- **Static batching needs no special handling.** "Can move at runtime" is
  exactly the set static batching already refuses, so a live candidate always
  has the solo bag a second submission needs.
- **Runtime spawns are covered** by a scan of the spawn pool (they exist in no
  build-time table at all), so a spawned object flying through the volume
  reflects too.

What it *does* cost is the second submission itself, and that number is now
variable: every object inside is one more full re-draw of its geometry. Twelve
crates rolling into a mirror's area is twelve extra submissions. Keep the
volume tight around what the glass actually shows and watch the movable count
in the panel.

Details worth knowing:

- **The player** follows the *Reflect player* checkbox as before, but with a
  live area it also has to be inside: the checkbox says the avatar *may*
  reflect, the volume says whether it does right now. An FPP player still has
  no body and reflects nothing.
- **A moving area over an immovable prop does not catch it.** The prop is not a
  candidate (it is in the fixed list, or in a static batch). Move the *objects*,
  not the area, when you want things to pop in and out — or make the prop
  movable in any of the ways above.
- **Raytraced mirrors ignore the flag.** Their VU0 proxies are decimated meshes
  baked per mirror at build; the traced set cannot change while the game runs.
  A portal with *All objects in view* ignores it too — it already shows
  everything.
- **Portals: what a live area shows, it also lets through.** `portalCanCross`
  and `portalShowsObject` run the same test, so the owner's rule (a portal you
  can see through is a portal you can walk through) still holds for objects the
  volume picked up this frame.

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
  `SceneObject::catchAreaLive` is the per-frame switch.
- `SCENE_LAYER_STREAM_AREAS[scene][layer]` is the zone's object index
  (-1 = use the circle).
- A live catch area bakes three more fields into its owner's side-table row
  (`MirrorData` / `PortalData` / `CamFeedData`): `liveArea` — the area's own
  scene index, so the game reads its *live* transform — plus `firstCand` and
  `candCount`, a slice of the shared `CATCH_CANDIDATES` table.
  `TerrainGame::collectLiveCaught` walks that slice (and the spawn pool) into
  a reused `liveCaught` buffer once per owner per frame.
- Live Link: an area's transform is part of its recipe hash, because catch
  areas expand into baked tables at build. Moving an area during a live session
  therefore flips the toolbar chip to **LIVE (rebuild)** instead of showing you
  half the edit. (That holds for live catch areas too — the *candidate list* is
  baked even though the test is not.)

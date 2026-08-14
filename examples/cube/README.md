# cube example

A 3 × 3 × 3 lattice of identical-shaped rooms with hatches through every wall,
floor and ceiling, in four flavours, **assembled on the PlayStation 2 at boot**.
You spawn in the middle room. Beyond the doorways: black.

Open `cube.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`.

**TRIANGLE reshuffles the whole cube** — same 27 cells, different rooms in them.

## What it demonstrates

Two features meeting: [prefabs](../../docs/prefabs.md) and
[runtime procedural generation](../../docs/procedural-runtime.md).

Each room is a **prefab**: 20 wall/floor/ceiling slabs around a doorway or a
hatch. Four of them are in the pool the map draws from — one of those also
contains a slowly turning cube — next to an empty fifth prefab, literally named
`prefab`, that nothing references. A **runtime Procedural volume** does the
rest, and it is three nodes long:

```
Scatter on Grid (spacing 14, Levels 3)  ─  Pick Prefab (4 rooms in the pool, weighted)  ─  Output
```

`Scatter on Grid` with *Levels* is a full 3D lattice, so 27 cells fall out of
one node. `Pick Prefab` gives each cell a room. That is the whole map.

## Why it is affordable

| | |
|---|---|
| rooms | 27 |
| merged slabs | 540 |
| draw calls for all of it | **~4** |
| spawn-pool slots used | one per red room (the turning cube) |
| PS2 frame rate (PCSX2, software renderer) | **50 FPS** — the PAL cap |

A PS2 static submit costs ~1 ms of fixed EE time whatever it contains, so 540
slabs as 540 objects would be half a second per frame. Instead:

- every member that is **plain static geometry** merges into the volume's chunk
  grid — not into the *instance*, into the **volume** — which is why 27 rooms
  cost the grid's handful of bags rather than 27;
- the turning cube **cannot** merge (it carries a flow graph, so something has
  to be able to address it) and takes a clone-pool slot and a submit of its own.

The Prefabs window states that split per prefab before you ever build.

You can still walk into the walls: merged geometry contributes conservative
world AABBs to a static collider list the walker tests, so the rooms are solid
even though they are four draw calls.

## The files

- `build-scene.py` — authors the four prefabs, the volume and the shuffle
  button into the `.tyra`. 20 slabs × 4 variants is not something anyone should
  read as JSON; this script is its readable form. Re-run it after
  `tyrax-editor --new` to rebuild the example from scratch.

## Things worth trying

- Open *Tools > Prefabs*, select **room-red** and read the cost line: 20 merged
  into 1 draw call, 1 spawned object. In the member table the `-core` row says
  *own object* and the rest say *merged*.
- Change the weights in the volume's *Pick Prefab* pool and press TRIANGLE.
- Set *Levels* to 5 on the `Scatter on Grid` node: 45 rooms, still the same
  handful of draw calls.
- Drop a room prefab into a scene by hand: *Prefabs > Insert into scene*, then
  click the ground. It stamps ordinary objects you can edit individually.

## What it is not

Every cell gets the same room shape, so the outer shell has doorways to nowhere
— hence the black sky. A sealed cube would need an edge-room prefab and a filter
that places it, which is a graph, not a feature: `Filter by Attribute` on the
grid's own coordinates would do it.

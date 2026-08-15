# deep-forest example

A 2048×2048 map in daylight with **2800 scattered spruces**, walkable at **50
FPS** — this is the example for the two level-of-detail systems working
together: [terrain distance detail](../../docs/terrain-lod.md) for the ground
and mesh LOD + draw distance for the trees.

Open `deep-forest.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`. It ships in the **debug**
profile, so the FPS / MEM / VRAM readouts are on screen. Walk with the left
stick, look with the right.

## What it shows

- **A big map that draws.** 2048 units square at terrain detail 512 (a 4-unit
  grid), **View distance 320** so only the ring of tiles around you is in
  memory, and **Detail distance 55**: tiles past 55 units are built from every
  2nd heightmap sample, past 121 from every 4th. Edges are stitched to the
  neighbouring tile's stride, so nothing cracks — and in daylight you can
  actually watch for the bands and not find them.

  Set Detail distance to 0 in *Project > Preferences > World* and rebuild to
  measure what full-detail tiles cost — on this project's night-time
  predecessor that was the difference between 25 and 50 FPS, and the mechanism
  is identical here.

- **A forest that draws.** One
  [procedural volume](../../docs/procedural-generation.md) scatters 2800
  spruces and rocks over 560×560 units, filtered by slope and by a noise mask
  so there are clearings, then bakes them into 72 merged chunk meshes —
  ~62k triangles total, none authored by hand. Three separate throttles keep
  that walkable, and each one is visible if you look for it:

  - **Chunk draw distance 210** — a whole 105-unit chunk of forest stops being
    submitted past 210 units. The fog (ends at 290) is tuned to make the
    pop-in a fade-in.
  - **Mesh LOD at 70 units** (*Preferences > Models > Mesh LOD distance*) —
    past 70 units each chunk swaps to its decimated twin. On a stepped-tier
    spruce the swap is honest: the far tiers were built from the same
    silhouette.
  - **Terrain detail distance 55** — the ground under the far forest is a
    quarter of the vertices of the ground under you.

- **Fog doing its job.** Fog end (290) sits inside the view distance (320) and
  past the chunk draw distance (210), so neither the streaming ring's edge nor
  the forest's edge is ever the thing you notice.

## Things worth trying

- *Preferences > World > Detail distance* — drag it down to 20 and walk: the
  bands come close enough to watch them settle behind you.
- *Preferences > Models > Mesh LOD distance* — 0 disables the decimated twins;
  watch the triangle counter climb.
- The scatter volume's graph (select **pines**, open the graph): the `Limit`
  node's 2800 and the `Output` node's draw/cell numbers are the whole
  performance story, one click away.

## Assets

`pine.obj` is a generated 32-triangle spruce: a square trunk and four stacked
hexagonal tiers. Tiers rather than one cone because the props' shading is baked
per vertex, so a stepped silhouette is what gives it shape. `rock.obj` and
`props.mtl` come from the [procedural](../procedural) example. The heightmap is
generated value noise with a flat clearing at the spawn.

# procedural example

Six **Procedural volumes** in one 140x140 map, between them using every node in
the [procedural generation](../../docs/procedural-generation.md) library — from
"scatter a forest with clearings" to "put this pillar twelve times around a
circle, exactly".

Open `procedural.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`. The project opens on its
**Procedural** window layout, so the graph editor is already there; the volume
dropdown at the top switches between the six.

Nothing procedural reaches the PS2: every build bakes the volumes into ordinary
static chunk meshes (`res/models/procgen-*.obj`) plus one Model object per
chunk. The console loads finished geometry and never learns a graph existed.

## What is in the map

You spawn on the stone **plaza** in the middle. Turn around once and you can
see five of the six volumes.

| Volume | What it shows | Result |
|---|---|---|
| **forest** | the full stochastic chain: noise clearings, a slope limit, breathing room, a weighted pool | 98 pines + 35 rocks, 8 chunks, 2 226 tris |
| **colonnade** | `Single Point` -> `Radial Array`: 12 pillars on an exact circle around the plaza | 12 pillars, 4 chunks, 864 tris |
| **cairn** | `Single Point` -> `Array`: a stack up the Y axis, each rock yawed 34 deg and 16 % smaller | 6 rocks, 1 chunk, 48 tris |
| **fence** | a hand-placed curve, posts every 2.6 units, then `Array` in POINT space for the second row | 94 posts, 2 chunks, 2 256 tris |
| **orchard** | the grid source, a mask filter, and `Set Attribute` driving per-tree size | 30 trees, 1 chunk, 660 tris |
| **crystals** | `Scatter in Volume` (no surface snapping) merged with a surface scatter, then limited | 70 crystals, 1 chunk, 840 tris |

Totals: **345 instances -> 17 chunk meshes, 6 894 triangles**. Seventeen draw
calls is the number that matters on a PS2, not the instance count — see
"Reading the budget" below.

## The map itself

The terrain gives every terrain-reading node something to read: a flat plaza
inside a 20-unit radius, rolling ground around it, and a genuinely steep ridge
along the north edge. That ridge is what the forest's slope filter cuts
against — walk to it and watch the trees stop partway up.

The **plaza** is an ordinary Box object, and the forest's *Keep Away From*
points at it **by name**: a procedural rule referencing hand-placed geometry,
so moving the plaza in the editor moves the clearing with it.

## Node coverage

Every node in the library appears at least once:

- **Sources** — Scatter on Surface (forest), Scatter on Grid (orchard),
  Scatter in Volume (crystals), Scatter along Curve (fence), Curve (fence),
  Single Point (colonnade, cairn).
- **Masks** — Noise Mask (forest, orchard), Terrain Mask (forest: slope),
  Combine Masks (forest: noise AND slope), Remap Mask (forest).
- **Filters** — Filter by Attribute (forest: slope band with a soft edge),
  Filter by Mask (orchard), Minimum Distance (forest), Keep Away From (forest:
  the plaza), Merge Points (crystals), Limit Count (forest, crystals).
- **Repeat** — Array (cairn: world step; fence: step in point space),
  Radial Array (colonnade).
- **Attributes** — Pick Asset (all six), Vary Transform (forest, orchard,
  crystals), Set Attribute (orchard).
- **Output** — Output (all six), Object Settings (forest, colonnade,
  crystals).

## Things worth trying

- **Reseed** (top of the window): the forest, orchard and crystals reshuffle
  while the colonnade, cairn and fence do not move a millimetre — the repeat
  nodes are analytic, nothing about them is random.
- Raise the forest's **Density**: existing trees stay put, new ones appear
  *between* them. That is prefix stability, and it is why the coarse preview
  while you drag never lies.
- Right-click any node > **Preview** to see what that node alone produced
  (a mask drapes over the terrain, a curve draws as a line).
- Turn on **Edit instances** and drag one tree somewhere else, or ctrl-click
  to delete it. Change the density again — your edit is still attached to
  that instance, because overrides bind to a point's identity, not its index.
- Open the **colonnade** and change *Count* from 12 to 9: an exact nonagon,
  one edit. Then set *Sweep* to 180 for a half-circle spread evenly end to
  end.
- Open the **cairn** and set *Step Y* to 0 and *Step X* to 3 — the stack
  becomes a row. The fence shows the other mode: its Array steps in POINT
  space, so its second row follows the curve instead of running off along
  world X.

## Reading the budget

The header line is the honest PS2 readout: instances, **chunks**, triangles
and the estimated vertex bytes. Chunks are draw calls, and on real hardware
each submit costs about a millisecond of fixed EE overhead regardless of size
— so "how many chunks" is usually the number to tune, not "how many trees".
The `Chunk size` on the Output node is that lever: bigger chunks mean fewer
draws and coarser culling.

Two details this map makes visible:

- The chunk grid is **world-aligned**, so the colonnade (a ring centred on the
  origin) lands in 4 chunks even though a 34-unit ring would fit in one.
  Volumes that straddle 0 pay for it; it is the price of chunk positions
  staying stable when a volume moves.
- The forest sets **Instance detail = Half**, which decimates each source mesh
  once before merging. Measured on this scene: 2 226 triangles as shipped
  against 6 160 for the same 133 instances at full detail — and at the
  distance a scattered tree is read from, nothing about it looks different.

Its **Object Settings** node then gives every generated chunk a *Mesh LOD
distance* of 55 units, which is also what makes the build bake the decimated
tiers for those chunk meshes (the project-wide mesh LOD preference stays off).

## Measured

PCSX2 (software renderer, PAL): **50 FPS**, i.e. the vsync cap, with EE at
44 %. Not measured on real hardware — the 17 draw calls would be the first
thing to watch there.

## How it was built

By hand, the way you would: add a Procedural volume, fill its Pick Asset pool,
then build the chain outward. The five assets (`res/models/pine.obj`,
`rock.obj`, `pillar.obj`, `post.obj`, `crystal.obj`, 8 to 72 triangles each,
sharing one `props.mtl`) are deliberately tiny and untextured so the example
is about the graphs, not the art.

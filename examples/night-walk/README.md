# night-walk example

A 2048×2048 map at night with a torch and 800 scattered spruces, holding **50
FPS** — the two features that make that possible are
[terrain distance detail](../../docs/terrain-lod.md) and the
[flashlight's projected pool](../../docs/flashlight.md).

Open `night-walk.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`. It ships in the **debug**
profile, so the FPS / MEM / VRAM readouts are on screen. Walk with the left
stick, look with the right, and **Circle** switches the torch off and on.

Point the torch at the ground near your feet. That pool of light is a
**projected texture**, not the terrain's vertex lighting — which is the whole
point of the example.

## What it shows

- **A big map that draws.** 2048 units square at terrain detail 512 (a 4-unit
  grid), **View distance 320** so only the ring of tiles around you is in
  memory, and **Detail distance 55**: tiles past 55 units are built from every
  2nd heightmap sample, past 121 from every 4th. Edges are stitched to the
  neighbouring tile's stride, so nothing cracks. Measured on this project,
  walking, PCSX2, same build otherwise:

  | Detail distance | Frame rate |
  |---|---|
  | 0 (off — every tile full detail) | **25.0 fps** |
  | 55 | **50.0 fps** (the PAL cap) |

  Exactly half, because that is what missing a 20 ms field costs on a PAL
  console: the frame is shown one field late, every time.

  Set it to 0 in *Project > Preferences > World* and rebuild to see that for
  yourself.

- **A flashlight worth looking at.** The pool under the beam takes its texture
  coordinates from the light's own frustum, so its shape is the
  `res/hud/flashlight-gobo.png` image — per pixel, an ellipse that stretches as
  you lower the beam, following the ground's relief. The terrain deliberately
  takes **no** per-vertex light from the torch: that term is one value per
  vertex, and a terrain vertex is four units from the next, so it draws a blocky
  wedge that moves in cell-sized steps. Props and trees still get it — they are
  small enough for it to look like light.

  Three stone walls stand by the spawn - one of them turned 34 degrees to the
  world, which is the interesting one: shine the torch at it and the pool
  appears ON the wall, in its plane, clipped to its edges. An axis-aligned box
  test would have put that light where the wall is not.

  The walls never take the per-vertex cone at all, aimed at or not, so what you
  see on them is the gobo and the moon and nothing else - sweep the beam from
  the grass up a wall and the light crosses the join without blinking. The tree
  trunks in front of them are small enough to keep the cone, and do.

  The torch's colour is deliberately about 0.6, not white. The per-vertex cone
  has no N·L, so a white one adds the same amount to every face of a tree
  whichever way it points and flattens it into a cutout; at 0.6 the baked
  moonlight still shows through and the tiers keep their shape.

- **800 spruces and rocks, from a graph.** One
  [procedural volume](../../docs/procedural-generation.md) scatters them over
  420×420 units around the spawn, filtered by slope and by a noise mask so there
  are clearings, then bakes them into 34 merged chunk meshes with a 210-unit
  draw distance. ~19k triangles total, none of them authored by hand.

- **Two sheds, one of them pre-lit.** They are the same textured model in the
  same light. The left one had the scene's light
  [baked into its texture](../../docs/prelit-models.md)
  (--bake-object-light night-walk shed-lit), so its planks read per pixel and
  the torch's pool lands on them; the right one takes the ordinary per-vertex
  route and the cone floods it to white at close range. That is the whole
  argument for pre-lighting a textured model, in one screenshot.

- **Fog doing its job.** Fog end (185) sits well inside the view distance (320),
  so the streaming ring's edge is never the thing you notice.

## Things worth trying

- *Preferences > World > Detail distance* — drag it down to 20 and walk: the
  bands come close enough to watch them settle behind you.
- The torch's **Reach** and **Cone half-angle** (Properties on the player) size
  the pool as well as the light.
- Replace `res/hud/flashlight-gobo.png` with your own gobo — a cross, a grille,
  a cracked lens. Shape in RGB, black border, power-of-two sides.

## Assets

`pine.obj` is a generated 32-triangle spruce: a square trunk and four stacked
hexagonal tiers. Tiers rather than one cone because the props' shading is baked
per vertex, so a stepped silhouette is what gives it any shape at all under a
torch that has no N·L term. `rock.obj` and `props.mtl` come from the
[procedural](../procedural) example. The heightmap is generated value noise with
a flat clearing at the spawn.

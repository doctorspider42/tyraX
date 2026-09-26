# Conservative occlusion culling

TyraX can reject whole objects, static batches and procedural chunks that are
completely hidden behind solid scene geometry. Enable it in
**Project > Preferences > Rendering > Conservative occlusion culling**. It is
off by default: the result depends on the map's layout, while every enabled
project pays a small EE cost to build the visibility buffer each camera pass.

This is an additional test after frustum and distance culling. It does not
change meshes, materials or draw order. It tests solo objects, static batches,
terrain chunks, road chunks and procedural chunks, and occluders themselves.

## Build-time proxies

Code generation creates `inc/occlusion_data.gen.hpp`. A static, opaque box gets
an inset box proxy. A static OBJ is voxelised on the host and reduced to at most
32 non-overlapping **inner** boxes. The boxes are deliberately inside the real
mesh: an uncertain cell is discarded instead of being allowed to hide a visible
object.

An object is refused as an occluder when it moves or participates in gameplay,
uses an animated model, has an alpha/unknown-opacity texture, is not closed, or
cannot produce a useful inner volume. UV seams and geometric T-junctions are
accepted when three-axis parity tests still prove that the surface is closed.
Repeated model assets share the host-side proxy calculation.

Select an object and enable **Never occlude other objects** to opt it out. This
is the correct setting for authored holes, windows, foliage and any custom
shader or material whose opacity the importer cannot prove. **Can be occlusion
culled** independently controls whether that object may disappear behind a
proved occluder; disable it for important objects or unusual bounds.

## Runtime test

The generated game projects each proxy into a 48x42 CPU depth buffer, built
once per main view before the terrain is drawn. It then tests the box of every
draw unit that survived frustum and distance culling: solo objects, static
batches, terrain chunks, road chunks and procedural chunks. A unit is skipped
only when every buffer cell its box touches is covered by a proxy that is
nearer than the box, by a 0.35-unit bias.

The rules that keep this conservative:

- **Coverage is exact per proxy.** A proxy box is rasterised as its convex
  screen hull, and a cell is written only when that one hull contains all four
  of its corners. The hull's x-span is found on every integer row line; a cell
  is inside when it fits within the spans of both its lines. There is no
  erosion pass. The old rule sampled cell centres against the union of
  all proxies and eroded that union by one cell. That can bridge a slit
  narrower than a cell between two proxies, such as an alley between two
  buildings. It can also give a cell one proxy's depth while a different,
  farther proxy supplies its coverage. Both hide geometry that is on screen.
- **Depth is per piece.** A long box seen end-on would otherwise let its far
  end set the depth of its near end. Proxies and candidate boxes are therefore
  cut into up to 8 pieces, each at most 4 units long, along their longer
  horizontal edge. Each piece has its own projected rectangle and its own w.
  An occluder cell takes the deepest piece that touches it, and a candidate
  cell needs the nearest piece that touches it. The pieces partition the box,
  so any ray through a cell meets the box only inside a piece that touches
  that cell. A cell of a candidate's rectangle that no piece touches is not
  part of its footprint, so it needs no coverage.
- **The candidate box is the drawn box.** A solo object is tested with the
  vertex AABB that `coarseObjectOutside` already uses, built from the vertices
  it draws. Objects whose drawn vertices can leave that box are never tested:
  an impostor card turning to face the camera, a matrix-path body, or a VU
  program that moves geometry. A batch, terrain chunk or road chunk uses its
  own exact world box.
- **Occluders are tested too.** An object cannot hide itself: its proxies lie
  inside its own box, and w is affine, so a proxy's farthest w is at least the
  box's nearest w. Two occluders cannot hide each other, because each would
  need its proxy to be nearer than the other's box. A batch holding occluders
  is safe by the same argument. It was exempt before, and that is why a city
  of buildings hid nothing.
- **A proxy only stands for geometry this pass draws, whole and opaque.** At
  runtime an occluder writes nothing in these cases:
  - it is past its draw distance;
  - it is an impostor card, or within 90% of its impostor distance (the lower
    end of the switch hysteresis);
  - it is on a decimated mesh-LOD tier;
  - a part is translucent, LOD-hidden or may blend;
  - it is outside the split-screen band;
  - the eye is inside its mesh box, where back faces are culled and the wall
    is not drawn.
- **Off-screen parts need no cover.** The buffer is the raster (x/w spans
  +-W/4096 there, the same edge as the frustum's side planes). The part of a
  candidate past the buffer's edge is outside the picture, so a box that is
  half off screen can still be hidden by what covers its on-screen half. A
  proxy or candidate with any corner closer than 0.15 to the eye is never
  used.
- **One view.** The buffer answers only for the pass that built it. The
  reflection probe, portals, mirrors and camera feeds call the same terrain
  and road functions with another view-projection. They get "visible", both
  from a flag that closes after the object loop and from a check of the
  matrix itself.

A candidate whose projected centre falls on an uncovered cell stops before the
corner work, which keeps the common open-view path cheap. Occluder corners and
their mesh boxes are cached per object and recomputed only when the transform
changes.

### Roads and terrain are tested again

Road chunks skipped this test after 1.122.2, on the theory that the
software-depth test had caused asphalt gaps. It had not: the gaps were the
retained-command EMIT_STATE flag ([roads.md](roads.md), "Holes in the road",
1.127.1). The Motor District also had occlusion switched off, so the test
returned before looking at anything. With exact coverage and per-piece depth,
a road chunk is hidden only when every cell its whole box touches is covered
nearer than the piece of the chunk in that cell. A near arm of the same road
is inside that box and is not covered, so it keeps the chunk visible.

Terrain chunks are tested with their real minY/maxY box, and for that the
buffer is now built before the terrain draws. It reads only the view and the
static proxies.

### The OCC line

With the in-game profiler on (`DEBUG_SHOW_PROFILER`), the runtime logs a line
every 120 frames:

```text
OCC proxies=26 hidden=23/48 obj=3/8 batch=6/15 terrain=3/9 road=11/16 proc=0/0 why=4,3,0,9,9 skip=30,0,0,85,7
```

- `proxies` counts the boxes that wrote at least one cell.
- `hidden/tested` counts whole draw units, then splits them by kind.
- `why` says why the visible candidates stayed visible, in this order:
  1. near plane;
  2. centre cell uncovered;
  3. entirely off the buffer;
  4. a cell uncovered;
  5. a cell not nearer.
- `skip` says why occluders wrote nothing, in this order:
  1. draw distance;
  2. impostor, LOD, blend or split band;
  3. eye inside;
  4. outside the view;
  5. every box too near or too small.

## Build-time proxy quality (2026-09-26)

The runtime rewrite alone took the dense scene from 0 to a few hidden units per
street pose. After that, the limit was the proxies. The host voxel grid was 18
cells on the longest axis, and the one-cell erosion plus the corner test make a
proxy a whole cell smaller than its mesh on every side. That left a 17-unit
tower 0.96 units short at every wall and 1.02 units short at its base. At
street level, that base band is exactly what everything behind a building has
to cross. Three changes:

- **A finer grid for low-poly meshes:** 48 cells for 256 triangles or fewer,
  32 for 2048 or fewer, and 18 above that. The district's kit pieces have 22
  faces each. The tower proxy went from +-3.04 x 1.02..15.04 to +-3.62 x
  0.38..15.56, and the workshop from +-5.28 x 0.65..2.35 to +-5.73 x
  0.27..2.94.
- **Thin slabs are dropped.** The finer grid steps a pitched roof into a stack
  of slabs, which took the district from 373 to 1 061 boxes. A box under 5% of
  the object's largest box volume is now dropped. The district is back to one
  box per occluder (171 in total). Dropping a box can only turn an answer into
  "visible".
- **Box primitives are inset to 0.49 instead of 0.46.** Their mesh is exactly
  the box, so the inset only has to absorb float noise. 0.46 cost a 16-unit
  pavement 0.64 units on every side.

## Motor District results

### PCSX2 (counts and pixels, 2026-09-26)

The fixture is a `benchmark-district.py` copy with parked traffic and
`startScene` 1 (`dense`), with occlusion on and the profiler overlay off. The
camera is held by a pose file. The ELF is the same in both arms; the boot mode
(`bin/stapipexp.txt`) switches occlusion off (70) or leaves it on. Each arm
takes three `--capture-frame` shots per pose, for 20 day poses and 4 night
poses at street level (eye height 1.8) along every street of the dense scene.

Hidden draw units per pose (the `OCC` line in the on arm), and the pixel result
against the off arm:

| pose | eye -> look at | hidden / tested (obj, batch, terrain, road) | px > 8 levels |
|---|---|---|---:|
| 1 | (0,1.8,-45) -> (0,1.5,0) | 12/70 (2, 1, 2, 7) | 0 |
| 2 | (0,1.8,-45) -> (30,1.5,-35) | 5/53 (0, 0, 0, 5) | 0 |
| 3 | (0,1.8,-10) -> (40,1.5,-20) | 10/44 (4, 1, 1, 4) | 0 |
| 4 | (0,1.8,20) -> (40,1.5,30) | 23/48 (3, 6, 3, 11) | 0 |
| 5 | (0,1.8,20) -> (-40,1.5,30) | 9/50 (1, 1, 1, 6) | 0 |
| 6 | (-40,1.8,0) -> (-40,1.5,-40) | 8/46 (2, 2, 1, 3) | 0 |
| 7 | (30,1.8,0) -> (100,1.5,0) | 2/38 | 0 |
| 8 | (-30,1.8,0) -> (-120,1.5,0) | 7/39 (1, 1, 2, 3) | 0 |
| 9 | (0,1.8,58) -> (60,1.5,58) | 0/43 | 0 |
| 10 | (0,1.8,-56) -> (-60,1.5,-58) | 5/38 | 0 |
| 11 | (0,1.8,-74) -> (0,1.5,-140) | 0/12 | 0 |
| 12 | (10,1.8,-5) -> (20,1.5,13) | 11/50 (2, 5, 1, 3) | 0 |
| 13 | (0,1.8,58) -> (-60,1.5,58) | 4/45 | 1 (see below) |
| 14 | (-62,1.8,-30) -> (-65,1.5,30) | 7/48 | 0 |
| 15 | (70,1.8,0) -> (80,1.5,-30) | 0/35 | 0 |
| 16 | (0,1.8,-100) -> (0,1.5,-40) | 8/102 | 0 |
| 17 | (-94,1.8,103) -> (0,1.5,112) | 12/49 | 0 |
| 18 | (120,4,-64) -> (122,3,44) | 3/39 | 0 |
| 19 | (-122,1.8,0) -> (-122,1.5,64) | 5/24 | 0 |
| 20 | (0,1.8,0) -> (0,1.5,40) | 15/47 (2, 5, 1, 7) | 0 |
| 21-24 | poses 3, 4, 6, 12 at night | 10, 23, 8, 11 hidden | 0 |

There are 24 poses, 3 shots per arm, and the traffic is parked. Across the
whole sweep, the only pixel more than 8 levels off is at pose 13. It is one
horizon pixel far down the avenue, and it lies outside every hidden unit's
rectangle (checked with the `OCCH` log of the hidden boxes). It flips between
74 and 37 across shots of the SAME on arm, and it stays too when the terrain
and road tests are switched off (mode 73). The 1 400-3 600 pixel "differences"
at a few poses are 1-3 levels each (at most 7 on the night poses, which also
flicker within an arm).

**How the pixels were judged.** Two traps cost this round a false alarm each:

- The FPS/MEM/VRAM text differs between arms, because MEM moves. It is masked.
- Interlace parity (and, at junctions, the order in which coplanar road layers
  are submitted) makes single shots of one arm differ by up to ~1 400 pixels
  of 1-2 levels.

So for each pose the minimum difference over all nine A/B shot pairs is taken,
and a pixel counts only when it differs by **more than 8 levels**. A hidden
unit that was really visible would show as a whole missing surface: tens of
levels over whole cells. What remains after this is a spread of 1-3 levels in
the silhouettes of culled units. A surface in front evidently takes about 1/128
of whatever was drawn behind it, which is consistent with a base pass blending
at alpha 127 rather than 128. That is worth its own look and is noted in the
backlog. It is not occlusion.

### Physical PS2

Not yet measured. The timing arm and the procedure are in the backlog
("Occlusion culling: the hardware verdict"). Until that series says otherwise,
the Motor District keeps `occlusionCulling` off.

## What it cost before, and why a city of buildings did not win (1.125.2)

Measured on a physical PS2 (Motor District, devkit off, `FTOCC` with
`TYRA_FRAME_PROFILE`), before the rewrite above:

| scene | proxies drawn | hidden / tested | `work` off -> on |
|---|---:|---:|---:|
| `main` (14 buildings) | 10 | 0 / 71 | 18.26 -> 19.29 ms |
| `dense` (121 buildings) | 44 | 0 / 71 | 19.49 -> 21.97 ms |

1.125.2 made the buffer cheaper, which took the `main` cost from +1.58 to
+1.03 ms:

- one span per row instead of every edge per cell;
- occluder corners and candidate boxes cached;
- whole occluders outside the view skipped.

It still hid nothing, for three structural reasons, all fixed above:

- occluders were never culled;
- batches holding occluders were never tested;
- terrain and road chunks were never tested.

(FTOCC's `erode` lap now reads about 0, because there is no erosion pass. The
column is kept so the line keeps its shape.)

## Limits

- Only the main view (and each split-screen half) builds a buffer. Reflection
  probes, portals, mirrors and camera feeds draw without occlusion.
- A static batch is hidden or drawn as a unit, and its box spans its grouping
  cell. Split large facade batches spatially if that prevents useful
  rejection behind them.
- The system is intentionally pessimistic. A false visible result costs time;
  a false hidden result would be a hole in the frame, so it is not accepted.
- This complements, rather than replaces, spatial batching and frustum culling.

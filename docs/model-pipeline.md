# Static models: the .tmdl pipeline and mesh LOD

![Browsing model assets before placing them](img/asset-browser.png)

You author static geometry as `.obj` and nothing about that changes. What
changed is what the **game** reads: the build compiles every `.obj` a scene
uses into a binary `.tmdl` next to it, and that is what ships on the disc.

(Animated `.glb`/`.fbx` models have had their own binary format, `.tskl`, all
along - see [animated-models.md](animated-models.md).)

## How big is this model, really?

Selecting a static model shows three numbers in *Properties*:

```
4281 triangles, 12843 vertices (3351 unique positions)
```

The gap between them is the interesting part:

- **unique positions** is the `v` count - what the modelling tool told you;
- **vertices** is what actually reaches VU1: three per triangle, with a corner
  split wherever its normal, UV or material differs. This is the number the
  pipeline cuts into VU1-sized chunks (*Debugger > Stats* reports how big a
  chunk is - it is the VU1 buffer's capacity for that vertex layout, e.g. 108),
  and the number a frame's vertex budget is spent in.

A vertex count far above `3 x positions` is paying for split corners - hard
edges and material boundaries everywhere. The cottage above is 1.28x, which is
ordinary; a smooth-shaded sphere would be near 1.0x.

Animated `.glb` models report their own vertex count in the same place.

## Why the game stopped reading .obj

An `.obj` is text. Reading one on the PS2 meant parsing ASCII on a 300 MHz
CPU every single time the model loaded: a string stream per line, float
parsing per number, a normal computed per face, a material lookup per
`usemtl` group. On a 9 216-vertex model that measured **286 ms** - and with
[streaming layers](streaming-layers.md), where a layer's assets load one per
frame, a third of a second lands inside one frame as a visible hitch.

The build now does all of that once. Reading the same model as `.tmdl` takes
**39 ms**, and the whole load (textures included) went from 306 ms to 59 ms.
The PS2 side is a sequential read plus a memory copy per material.

The `.tmdl` carries the triangulated mesh, flat face normals, the resolved
material assignment (including a per-object `.mtl` override), texture-atlas
UV rectangles already folded into the UVs, texture paths as the game will
open them, the LOD levels below - and, when the project has *Flashlight shadow
volumes* on and the model is over the volumes' 1200-triangle budget, a
positions-only **shadow proxy** decimated under that budget, which is what the
torch extrudes the model's shadow from (docs/flashlight.md, "The shadow").

## Whole-model frustum rejection

A static model with at least three material parts gets one conservative AABB
over all of its baked vertices. The generated game tests that box before it
submits the individual parts in the main view and in portal through-views. A
fully off-screen district therefore costs one six-plane AABB classification,
not one pipeline entry and a set of package-bound tests per material. Objects
that touch the view retain the existing per-part and per-package culling, so a
large building crossing a screen edge does not turn into one oversized draw.

The box is rebuilt only when the object's geometry is rebuilt. It remains
conservative across mesh-LOD tiers, which only remove vertices, and follows the
matrix fast path in object space. A custom VU program that moves geometry skips
this early rejection because its displaced vertices may leave the baked box.

Consecutive material bags that point at the same model matrix also reuse their
MVP and object-space frustum planes within the frame. The cache key includes the
model and view-projection values, not only their addresses: physics may mutate a
matrix in place, while portal and split-screen passes replace the camera. Frame
end clears the cache, and projection-only (`TyraMP`) submissions bypass it.

## Compact static-model batching

Static batching also accepts compact, immutable imported models when they do
not use distance LOD, impostors, reflections, dynamic lighting or another
per-object runtime path. Each material part joins the batch for its actual
loaded texture and coarse world cell; atlas-backed materials therefore merge
even when their source material names differ. Singleton groups are discarded,
and a model whose horizontal footprint exceeds half a cell stays solo. Runtime
mutation demotes every part of that object from its batches before drawing it
through the normal path.

Those spatial limits are intentional. An earlier Aster experiment grouped
model parts by material across districts: it destroyed culling, and even
per-district groups widened four bags enough to cost roughly 3.8-4.5 ms in
PCSX2. The retained path targets repeated props rather than architecture and
keeps the whole-model reject above for large or LOD-switched meshes.

## Triangle strips

Every static model now ships **twice**: as the flat triangle list the build has
always produced, and as the same surface written out as **triangle strips**. The
render bag draws the strip; everything else - the collider, the shadow proxy,
the decal projector, the flashlight's receiver passes - keeps reading the list.

The reason is not the GS and it is not the DMA. On a measured Motor District
garage frame ([vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md)) render
submission is 40.2 ms, of which VIF1 wait is 5.8 and the rest is the EE getting
work ready: bounding boxes 4.8, per-bag preparation 4.6, packet construction
3.0, the `send_packet2` bracket 2.4 and about 8 ms of package creation and
classification. **Every one of those scales with the number of VU1 packages,
which scales with the vertex count.** An unindexed triangle list packages,
transfers and transforms a shared corner once per triangle that uses it; a
strip submits it once and the GS takes one vertex per triangle after the first
two. So the lever is fewer vertices, and it pays on the EE before it pays
anywhere else.

**Measured on the eleven baked district models: 13 176 list vertices become
9 648, a 0.732x count.** Per part it ranges from 0.417x (a ground quad grid) to
1.000x (one part that did not strip at all - see below). It is not the 0.35x a
terrain grid reaches, and the reason is visible in the numbers: these models are
flat-shaded, so each face has its OWN normals and a strip cannot cross a face
boundary. A quad is six list vertices and four strip ones - 0.75 - and most of
the district's walls, beams and posts are quads. Smooth-normal geometry does far
better.

### What it costs on the console

A strip changes nothing about the microprograms. The per-vertex ADC judgement
the cull programs already write is `fcand 0x3FFFF` over the last three `clipw`
results, which for a strip is exactly the triangle that vertex kicks - so the
whole feature is **zero VU1 instructions** and micro memory is unchanged at
1862 of 2042 words. What changes is one field of the GIF tag
(`PRIM_TRIANGLE_STRIP` instead of `PRIM_TRIANGLE`) and the order of the
vertices in the array.

Measured in PCSX2 on the district's garage-day pose, the same fixture built
twice with one knob moved (the `.tmdl` baked with strips or without; one
engine, one editor):

| per 50-frame window | list | strip |
| --- | ---: | ---: |
| VU1 packages (cull route) | 56 625 | **50 525** |
| submitted vertices per frame | 76 951 | **68 235** |
| packet flushes | 6 450 | 6 400 |
| clip-routed packages | 1 425 | 1 425 |

**11.3% of the frame's vertices and 10.8% of its VU1 packages, gone.** Less than
the models' own 26.8% because the models are about two fifths of this view's
geometry - the roads and the terrain are grids, they strip far better than
26.8%, and neither of them is stripped yet.

Packet flushes barely move, and that is not a disappointment: a flush happens
per BAG (`flushBuffers` at the end of each `render`), not per package, so it
counts objects rather than vertices.

### The rules the format keeps

The strip is chopped at build time into independent **runs** of exactly
`meshstrip::kRun` = 72 vertices, and the game pins `StaPipBag::packageSize` to
that number. That is the whole trick: a VU1 package is a contiguous slice of the
bag's array, so making the runs BE the packages means no package boundary can
ever splice two unrelated vertices into one triangle, and no overlap has to be
repeated at runtime.

- 72 is the smallest package any static program class derives, so one baked
  number is legal for every pass an object can take. The game checks that
  against the engine at runtime and keeps the list if it ever stops being true.
- Every run length is a multiple of 3 - the VU1 vertex loops step by three, and
  a count that is not runs off into VU1 memory. The padding repeats the last
  vertex, which makes a degenerate triangle the GS rasterises to nothing.
- Separate strips inside one run are joined by repeating a vertex either side
  of the seam. Winding parity is NOT preserved and does not need to be, because
  nothing in this engine backface-culls.
- A part whose strips come out no smaller than its list keeps the list and is
  not marked stripped. A cube is the honest example: 36 list vertices against
  24 unique ones plus 10 of join is 34, and 34 is not worth a second copy.

### Clipping

`clip_*` and the EE clipper both loop by whole triangles over a triangle LIST,
so a stripped package that genuinely crosses a VU1 clip plane is **expanded back
into a list on the EE** and clipped exactly as before. The expansion goes into
the qbuffer copy pool - the one whose double-buffered lifetime the DMA already
relies on - in chunks carrying the same triangle budget a list subpackage does,
and the buffer it produces emits `PRIM_TRIANGLE`, so one bag can mix the two.

It is rarer than it sounds, because the guard band sends most screen-straddling
packages down the cull path whole ([vu1-clipping.md](vu1-clipping.md)). At the
garage-day pose **not one** stripped package needed it; from inside a building,
where walls cross the near plane in every direction, 29.5 per frame did. The
cost is a 3x vertex expansion for exactly those packages - which is what the
list path would have sent for the same triangles anyway. The strip is a saving
on the packages that need no cutting, and that is nearly all of them.

### What the picture does

A geometry change cannot be argued correct from the instruction stream, so it
was compared pixel for pixel: PCSX2 software renderer, frozen camera, the
game's own `--capture-frame` (the GS raster, not a window grab). **Three
captures of one arm are byte-identical**, which is what makes the comparison
mean anything.

| pose | pixels differing | of those, by more than 8/255 | mean abs difference |
| --- | ---: | ---: | ---: |
| garage day (no clipping) | 1 756 of 200 704 (0.875%) | 9 (0.004%) | 0.015/255 |
| inside a tower, high (1 625 expansions) | 1 992 (0.993%) | 953 (0.475%) | 0.235/255 |
| inside a tower, floor in view | 12 860 (6.407%) | 12 493 | 3.243/255 |

The first two are what a topology change looks like: isolated pixels along
silhouettes and window frames, where a shared edge is claimed by a different
triangle and Gouraud interpolation from a different corner rounds differently.
Nothing is missing and nothing is corrupt.

**The third row is a pre-existing artefact being MOVED, not a new one**, and it
is worth knowing the shape of. At that pose the building's floor is very nearly
coplanar with the ground, and both arms draw a dithered hatch along the
crossover - the double-surface fight described under `StaPipBag::packageSize`.
The strip changes which VU1 route some of those triangles take (cull with the
divide on VU1, or clip), the last bits of z move with it, and the crossover
line lands ~15 pixels further along. Read it as: this change does not create
z-fighting, and it will re-shuffle any that a scene already has.

## What this means for your project

- **You keep working with `.obj`.** Import it, replace it, re-export from
  Blender over the top of it - the build re-compiles it on every build.
- **The `.obj` no longer ships.** Only the `.tmdl` goes into `bin/` and into
  an exported ISO, so the disc never carries both copies. The `.mtl` still
  ships (a material library can also be assigned to primitives).
- **The binary is bigger than the text**, usually around 1.7x: an `.obj`
  shares vertices between faces through indices, while the game needs a flat
  triangle list with a normal per corner - which is what it always built in
  RAM anyway. Memory use is unchanged; you trade disc space for load time.
- **Nothing to configure.** There is no switch: a static model referenced by
  a scene object is compiled.
- **An `.obj` carries no unit**, so the importer asks for one (the **Model
  size** dialog, and the **Size...** button next to the model in the Assets
  list afterwards). It records how many meters one unit of the file measures;
  combined with the project's world scale that is the scale objects made from
  the model are inserted at. The file itself is never rewritten - see
  docs/world-scale.md.
- A model that cannot be parsed is reported in the build log
  (`[model bake] ...`) and simply renders nothing, exactly like a missing
  animated model. The Asset Browser flags the same problem earlier.

## Mesh LOD: fewer triangles far away

**Project > Preferences > Rendering > Mesh LOD distance** turns distance
levels on for the whole project (`off` = no levels at all). An object farther
from the camera than the distance renders a reduced mesh, and past twice the
distance a further reduced one. This used to apply only to animated models,
because the `.tskl` had somewhere to put the reduced meshes and an `.obj` did
not; static models get it now too.

The saving is real because vertex count is what static geometry costs the
PS2: every vertex is transformed, classified against the frustum, clipped if
it crosses the screen edge, and pushed through VU1. A test scene with 12
copies of a 9 216-vertex model went from a hard 25 FPS to a steady 50 with
levels on (27.1 ms of scene time down to 11.5 ms).

**Per object:** any static model object can override the project value in its
Properties - **Override mesh LOD**, next to the material summary. Unchecked,
the project preference applies; checked, that object uses its own distance,
and dragging the value to `0` turns levels off for it (the hero prop next to
a crowd of scenery that always decimates). An override above zero also makes
the build produce the levels for that model even when the project preference
is `off`.

**Cost:** each level is more data in the `.tmdl` (the test model grew from
295 KB to 497 KB with two levels) and more RAM per object that actually gets
far enough away to use it. Levels are shaded and kept per object the first
time they are needed, so an object that never leaves the near band pays
nothing. This is why `off` bakes nothing at all.

**Tuning:** pick a distance at which the model is already small on screen.
If you can see the switch from a normal gameplay camera, the distance is too
short. The editor viewport always shows the full mesh.

## Your own LOD meshes

Automatic decimation is a quadric-error collapse: it protects UV seams and
silhouette borders, and it refuses to touch meshes too small to gain
anything - which also means some models barely shrink. When you want control,
model the levels yourself.

In the **Asset Browser** (*Tools > Asset Browser*), a selected model has a
**LOD...** button in the inspector:

- **Level 1** shows past the mesh LOD distance, **Level 2** past twice it.
- Pick any other `.obj` in the project (the list shows each candidate's
  triangle count), or leave a level on **(auto - decimate)**.
- Clearing a level also clears the coarser one - a chain with a hole in it
  has no meaning.
- A model with hand-authored levels shows `[N custom LOD]` next to it.

Requirements the build checks, per level:

1. **The same materials** - the same `usemtl` names in the same order as the
   full mesh. The levels share the model's materials and textures, so the
   material list has to line up.
2. **Fewer vertices** than the level before it.

If a level fails either check, the build logs why and **decimates that model
automatically instead** - the whole custom chain is dropped rather than
shipping something half-broken. Watch the build output after assigning
levels:

```
[model bake] res/models/tree.obj: custom LOD res/models/tree_lod1.obj has a
different material set than the model (same usemtl names in the same order
are required) - decimating instead
```

The level files live in `res/models` like any other model. They do not ship
separately - their geometry is folded into the model's `.tmdl` - and they do
not need their own scene objects. A practical setup is `tree.obj`,
`tree_lod1.obj`, `tree_lod2.obj` exported from the same source with the
material names kept intact.

## The shadow proxy

Distinct from the LOD levels and baked whether or not mesh LOD is on: with
**Flashlight shadow volumes** enabled, a model past 1200 triangles gets one
extra, positions-only mesh under that budget, from which the torch extrudes
its shadow volume (a per-frame EE classification of every triangle is what
the budget protects). It is welded across all parts by position and decimated
with open borders unlocked, so it keeps the outline rather than the surface -
which is all a shadow needs. About 40 KB per model; the game tries the real
mesh first and reads the proxy only when the real one is over budget. A model
the decimator cannot bring under budget prints
`[model bake] <model>: N triangles and the shadow proxy could not be decimated
under 1200` and casts its bounding sub-boxes, as every big model used to.

## What LOD never touches

- **Collision.** The collider and the model's bounding box always come from
  the full mesh, so what you walk into never changes with camera distance.
- **Physics bodies in flight** and objects with a **camera/mirror texture
  feed** keep the full mesh - both depend on the exact vertex buffers the
  game builds for them.
- **Mirrors, portals, camera feeds and reflection probes** re-use whichever
  level the main camera picked for that frame (the same approximation
  animated models make).

## Troubleshooting

**A model disappeared after a build.** Check the build log for a
`[model bake]` line: the `.obj` failed to parse, so no `.tmdl` was written.
The Asset Browser shows the same models it can read.

**A level never seems to show.** Distances are measured from the camera to
the object's center, in world units - the same units as object positions. A
level also needs to have been produced: with the project preference `off`,
only objects whose own override is above zero get levels.

**The decimated mesh looks too rough.** Push the distance out, or author the
level yourself (above). Small parts of a model may keep their full detail:
the decimator skips meshes that are already small, and levels are decimated
per material, so material borders never move.

**An exported ISO still contains the .obj.** `bin/` accumulates - the build
deletes a superseded `.obj` from it, but if you see stale files, a
*Project > Clean* followed by a build gives you an exact `bin/`.

# Static models: the .tmdl pipeline and mesh LOD

You author static geometry as `.obj` and nothing about that changes. What
changed is what the **game** reads: the build compiles every `.obj` a scene
uses into a binary `.tmdl` next to it, and that is what ships on the disc.
This page explains why, what it means for your files, and how to use the
distance LOD levels the format carries.

(Animated `.glb`/`.fbx` models have had their own binary format, `.tskl`, all
along - see [animated-models.md](animated-models.md).)

## Why the game stopped reading .obj

An `.obj` is text. Reading one on the PS2 meant parsing ASCII on a 300 MHz
CPU every single time the model loaded: a string stream per line, float
parsing per number, a normal computed per face, a material lookup per
`usemtl` group. On a 9 216-vertex model that measured **286 ms** - and with
[streaming layers](streaming-layers.md), where a layer's assets load one per
frame, a third of a second lands inside one frame as a visible hitch.

Everything that parse worked out is decided at build time, so the build now
does it once. Reading the same model as `.tmdl` takes **39 ms**, and the
whole load (textures included) went from 306 ms to 59 ms. The PS2 side is a
sequential read plus a memory copy per material.

The `.tmdl` carries the triangulated mesh, flat face normals, the resolved
material assignment (including a per-object `.mtl` override), texture-atlas
UV rectangles already folded into the UVs, texture paths as the game will
open them, and the LOD levels below.

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
  list afterwards). What it records is how many meters one unit of the file
  measures; combined with the project's world scale that is the scale objects
  made from the model are inserted at. The file itself is never rewritten -
  see docs/world-scale.md.
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

# Conservative occlusion culling

TyraX can reject whole objects, static batches, procedural chunks and road
chunks that are completely hidden behind solid scene geometry. Enable it in
**Project > Preferences > Rendering > Conservative occlusion culling**. It is
off by default: the result depends on the map's layout, while every enabled
project pays a small EE cost to build the visibility buffer each camera pass.

This is an additional test after frustum and distance culling. It does not
change meshes, materials or draw order, and it does not cull the terrain.

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

The generated game projects each proxy into a 48x42 CPU depth buffer. A box is
rasterised as one convex screen-space hull, using its farthest depth. Coverage
is eroded by one cell; a candidate's projected AABB is expanded by one cell and
must be covered at every cell with an additional depth bias. Near-plane and
screen-edge uncertainty always resolves to visible. Occluders themselves stay
visible, which also prevents self-occlusion and keeps mixed static batches safe.
Candidates whose projected centre is not covered stop before the eight-corner
test, keeping the common open-view failure path cheap. A static occluder's
rotated basis is also evaluated once per object rather than once per box corner.

With the in-game profiler enabled, the runtime reports a line like:

```text
OCC proxies=31 hidden=8/94
```

`proxies` counts useful projected boxes, and `hidden/tested` shows the whole
draw units rejected from those considered. Compare the same camera pose with
the project setting off. A street with long uninterrupted walls is a strong
case; an open field can only add the visibility-buffer cost.

## Limits

- Occlusion is recomputed for every camera pass. Reflection probes, split view
  and portals therefore pay separately, but also receive the correct view.
- A static batch containing an occluder is kept visible as a unit. Split large
  facade batches spatially if that prevents useful rejection behind them.
- The system is intentionally pessimistic. A false visible result costs time;
  a false hidden result would be a hole in the frame, so it is not accepted.
- This complements, rather than replaces, spatial batching and frustum culling.

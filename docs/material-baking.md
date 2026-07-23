# Material map baking (matbake)

`src/matbake.cpp/.hpp` is the Material Editor's UV-space raytraced baker:
it turns a mesh (a preview primitive or one of the project's `.obj` models)
into a set of texture-space maps - ambient occlusion, bent normals,
thickness, curvature, position and object-space normals - in one pass.
Host-only, no GL (the decalproj pattern); this is a different animal from
`aobake.cpp`, which bakes *scene* occlusion (terrain grids + analytic
occluder atlases). matbake bakes *one model's own* surface detail.

## How the bake works

1. **UV rasterization.** Every paintable triangle (the parts using the
   selected material entry) is rasterized in UV space at the target texture
   size with an N×N subsample grid per texel; each covered subsample
   interpolates the triangle's 3D position and normal through its
   barycentrics. The rasterization is conservative: a texel grazed by just
   a triangle corner still receives a sample at the nearest interior point,
   so island borders have no coverage gaps. UVs wrap (the GS repeats
   textures).
2. **BVH.** All triangles (paintable or not - everything occludes) go into
   a flat binned-SAH BVH; rays traverse it in logarithmic time
   (~11 M rays/s against a 100k-triangle mesh on a desktop core count).
3. **Hemisphere sampling.** Each texel fires K cosine-weighted rays
   (golden-angle spiral, rotated per texel by a seeded hash - clean at low
   ray counts, no banding, no noise hiss, and any prefix of the sequence is
   itself well distributed, which is what makes the progressive preview
   honest). Ray origins are epsilon-offset along the normal, so there is no
   self-hit acne. Hits within the max distance occlude with a linear
   falloff (a hit right next to the surface darkens more than one at the
   edge of the radius).
4. **One pass, many maps.** The same rays also produce the bent-normal map
   (average unoccluded direction) and - mirrored below the surface - the
   thickness map. Curvature comes from the geometry (discrete mean
   curvature from edge normal deltas, normalized to the 90th percentile),
   position and object-space normals fall out of the rasterizer for free.
5. **High-poly projection (cage).** With a high-poly mesh assigned, every
   texel point is first relocated onto the dense mesh: a ray from the
   *smoothed*-normal cage offset shoots back toward the low-poly surface
   and the first high-poly hit becomes the point the maps are computed at -
   the low-poly texture then carries the high mesh's occlusion, normals and
   curvature. Rays occlude against the high mesh too.
6. **Dilate.** Every map is flood-dilated `padding` texels outward from the
   islands (each ring averages its filled neighbors), so bilinear filtering
   and mipmaps never bleed background into the seams - on 128²/256² PS2
   textures this matters a lot.

## Determinism

The sample spiral is fixed, the per-texel rotation is a seeded hash, and
worker threads own fixed texel ranges - the same inputs give bit-identical
maps regardless of core count or how the progressive rounds were sliced.
`Params::seed` is part of the bake parameters.

## Progressive preview

`matbake::Baker` runs the bake on a worker thread in growing rounds
(8, 16, 32... rays per texel) and publishes a complete map snapshot after
each round; the UI polls `version()`/`snapshot()` and shows a usable AO
preview within tens of milliseconds, refining in place. The rasterized
geometry buffer + BVH are cached across `start()` calls while the mesh
signature and raster params hold, so dragging a sampling-only slider (max
distance, rays, seed) restarts almost for free.

## Map conventions

| Map | Encoding |
|---|---|
| `ao` | gray, white = fully open |
| `bent` | RGB object-space direction, 128/128/255 = flat +Z |
| `thickness` | gray, white = thick (nothing close below the surface) |
| `curvature` | gray, 128 = flat, >128 convex edges, <128 cavities |
| `position` | RGB, position normalized to the occluder AABB |
| `normal` | RGB object-space normal |

Texels outside the dilated islands hold each map's neutral value (AO white,
so a multiply layer built from it is a no-op off-island).

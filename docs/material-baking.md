# Material map baking (matbake)

![Material Editor](img/material-editor.png)

`src/matbake.cpp/.hpp` is the Material Editor's UV-space raytraced baker: it
turns a mesh (a preview primitive or one of the project's `.obj` models)
into a set of texture-space maps - ambient occlusion, bent normals,
thickness, curvature, position and object-space normals - in one pass.
Host-only, no GL (the decalproj pattern). Don't confuse it with
`aobake.cpp`, which bakes *scene* occlusion (terrain grids + analytic
occluder atlases); matbake bakes *one model's own* surface detail.

## Using it (Material Editor > Bake maps)

The property column of the Material Editor ends in a **Bake maps** block:

- **Preview** - `AO on material` runs the bake continuously and multiplies
  the occlusion over the textured preview mesh (upload-time only: nothing
  is written, painting keeps working); `Map view` shows the selected raw
  map (AO / curvature / thickness / bent / OS normal / position) instead of
  the material. Any parameter change re-bakes; the first rough result lands
  within a round (a few rays per texel) and refines in place.
- **High-poly** - a dense `.obj` from `res/models` whose detail is
  projected into the maps (cage projection; the **Cage** field controls the
  search distance, auto = 2% of the model size).
- **Resolution / Rays / Max distance / Anti-alias / Backface hits /
  Padding / Seed** - the bake parameters. *Max distance* is the main
  artistic knob (small = tight contact shadows, large = broad soft
  shading). They persist per material file as a `# tyra-bake` comment, so a
  re-open reproduces the exact same bake (same seed = bit-identical).
- **Bake & add AO layer** - bakes at full quality and drops the result
  onto the entry's texture as a **"Baked AO" multiply layer** (paint
  layers, docs/material-painting.md). Re-bakes overwrite that layer in
  place; Ctrl+Z undoes it like any layer edit. The entry needs a texture -
  create one via *Texture > New paintable texture...* first.
- **Save all maps** - writes the whole map set as PNGs next to the `.mtl`
  (`<file>-<entry>-ao.png`, `-curvature`, `-thickness`, `-bent`, `-normal`,
  `-position`) - mask sources for wear/dirt or exports for external tools.

## The automatic path

Everything above is the **hand** bake: one material, once, by choice. The same
occlusion is also available for **every** `.obj` model in the project without
anybody asking — *Tools > Baked Lighting > Model AO*
([ambient-occlusion.md](ambient-occlusion.md#model-ao)). It runs this same
raytracer per model asset, caches the result by content hash, and multiplies it
into the model's shipped texture at build; the source in `res/` is untouched,
so the hand bake and the automatic one do not collide.

Reach for the hand bake when you want the maps themselves (masks, exports, a
cage projection from a high-poly mesh) or a per-material result you can paint
over. Reach for the automatic one when you just want every model to occlude
itself.

## Smart masks (procedural wear & dirt)

With the paint tool open, **+ Mask** (next to the layer buttons) adds a
**smart mask layer**: a fill color drawn through a procedural mask driven by
the baked map set. Select the layer and tune the generator that appears
under the list:

- **Sources**: *Edge wear* / *Cavity grime* (baked curvature), *Occlusion
  dirt* (1−AO), *Thin rims* (thickness), *Height (Y)* and *Facing up*
  (position/normals), *Perlin noise*, *Worley cells* — the noises sample 3D
  noise **at the baked surface position**, so patterns continue across UV
  island seams instead of restarting at them (a free triplanar effect) —
  and *Bricks* (UV-space running bond with a mortar width).
- **Range** remaps the source through a smoothstep window (narrow = hard
  edge, wide = soft ramp), **Invert** flips it, **Breakup** multiplies by
  world-space Perlin for organic, uneven wear.
- Masks **regenerate live as the bake refines** and whenever a parameter
  changes; hand strokes on a mask layer are overwritten by the next
  regeneration (paint on a normal layer above instead). Parameters persist
  in the `.layers` sidecar; a layer marked `*` in the list is generated.

**Presets** saves the current mask stack's *parameters* (never pixels) as
`material-presets/<name>.matpreset` in the project root (never ships) —
apply one to another material and the same wear recipe regenerates from
*that* model's own bake.

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

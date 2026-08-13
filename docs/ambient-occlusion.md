# Baked ambient occlusion (contact shadows)

Baked ambient occlusion puts soft contact shadows where geometry meets:
terrain self-shadowing (ravines, the foot of hills) and darkening where
objects touch the ground and each other. The occlusion is baked at build into
**per-pixel AO textures** and drawn as extra alpha-blended passes — smooth
shadows with no visible triangle edges, and no per-vertex math on the PS2 at
runtime.

Two knobs, two places:

- **Scene-wide** (*Tools > Ambience Editor*, "Ambient occlusion" block, per
  ambience preset): the enable toggle, **AO strength** (how dark full
  occlusion gets) and **AO radius** (world units the contact darkening
  reaches; terrain self-shadowing scans 3× this). New projects have it
  enabled on their Default preset; pre-AO projects keep their look.
- **Per object** (*Properties > Cast shadow*, default on): whether this
  object darkens nearby terrain and objects. Off = it casts nothing but
  still receives shadows from others.

The editor viewport previews the shadows live (per fragment, the same
formulas), and a `Cast shadow` toggle takes effect immediately.

**The light direction the bake uses may come from a day/night cycle**
([day-night-cycle.md](day-night-cycle.md)). A cycle overwrites the preset's
`lightDir` inside `project::resolvedSettings`, which is where this bake reads
it from — so moving the time-of-day slider swings every contact shadow in the
scene, and nothing in `aobake` needs to know why.

## How it ships

| What | Where it is computed | How it ships |
|---|---|---|
| Terrain lightmap | Host, at build (`aobake::terrainAOMap`: an 8-direction horizon scan over the heightmap + the occluder contact term, per texel) | `.res-baked/aomap/scene<N>.png` (≤256×256), drawn as one extra alpha-blended terrain pass per chunk |
| Primitive lightmap atlas | Host, at build (`aobake::bakeSceneAoAtlas`: per-scene shelf-packed regions mirroring the builders' UV layouts - box 6 faces, sphere 1, cylinder 3, cone 2, plane 2) | `.res-baked/aoatlas/scene<N>.png` (≤256×256) + the matching UV rects in `inc/ao_data.gen.hpp`; each covered object draws one extra blended pass |
| Occluder shapes | Host (`aobake::collectOccluders`: every solid `Cast shadow` object reduced to an oriented box / sphere; model AABBs from a fast `v`-line scan) | `inc/ao_data.gen.hpp` occluder tables - also consumed per vertex on the EE by the fallback below |

**Alpha is floored at 2, never 0** (`aobake::kMinLightmapAlpha`). StaPip's GS
alpha test passes only where alpha != 0 — the cutout rule for foliage and
decals — and the emissive-light pass reads the SAME texture, so a texel with
zero occlusion used to discard the light pass with it. See
docs/emissive-materials.md.

Both images are **RGBA PNGs whose alpha is the occlusion** — the GS alpha-over
blend `(Cs-Cd)*As/128 + Cd` with a BLACK vertex color collapses to
`Cd*(1-As/128)`, an exact per-pixel multiply. They ship as full RGBA32 on
purpose: the engine's palettized (tRNS→CLUT) path loses the smooth alpha
gradient (verified in PCSX2 — a quantized bake renders as nothing), hence the
256×256 cap (~320 KB of the ~1.33 MB GS texture budget together).

Their **RGB carries the baked emissive light** (docs/emissive-materials.md) —
one image, read twice, the vertex color of each pass picking the channels it
sees. So neither image is gated on the AO preference: a scene with glowing
lamps and no baked occlusion still ships them, with an empty alpha channel and
only the additive pass drawn. The occlusion pass, in turn, is drawn only when
the alpha channel has content.

Codegen and `texbake` call the **same deterministic bake**, so the pixels and
the emitted UV rects cannot drift.

Regions are sized by **importance**, not by world area alone: a 4×4 probe grid
per region estimates the occlusion (and emissive light) it will carry, texel
density scales with the square root of that peak, and a bisection then raises
the density globally until the image is full. Faces that receive nothing shrink
to a floor size instead of eating the budget. The atlas *dimension* still comes
from the unweighted area, so this never changes VRAM — it only moves texels to
where the gradient is. See docs/emissive-materials.md for the numbers.

## Who casts, who receives

- **Casts** (with *Cast shadow* on): boxes, spheres, cylinders, cones,
  planes (thin slabs — rotated planes make walls), save points, and static
  `.obj` models (their AABB).
- **Receives**: the terrain (map) and static primitives (atlas). An object
  whose atlas regions come out fully lit (an isolated prop in the open) is
  dropped from the atlas and **stays eligible for static batching**; covered
  objects carry the extra pass and render solo.
- **Spawned clones and physics bodies** receive through a per-vertex
  fallback (`aoShadeMul` at geometry rebuild — they re-shade when they move
  or wake), not the atlas.
- **Imported models neither receive nor self-occlude** for now: per-vertex
  occlusion on authored low-poly meshes reads as triangulated shading. The
  whole self-AO pipeline (`aobake::modelAO`, the `.aov` sidecar, the
  `LeanObjLoader` reader) stays in the tree, disabled, for a future
  per-model lightmap-unwrap approach. Animated models relight dynamically
  and are unaffected, like with baked point lights.

## Static by design

AO is a bake. A runtime-moved object's *cast* shadow stays where the scene was
built (terrain map + atlases are textures); the *received* shading of
clones/physics re-bakes on rebuild. Live Link edits of `Cast shadow` (or of
anything the bake reads) need a rebuild — the LIVE chip flips amber.

## Costs

- **VRAM**: ≤256 KB terrain map + ≤256 KB atlas per scene (RGBA32) — unchanged
  by the light channel, which rides in the same two images.
- **Fill rate / EE**: one extra blended terrain pass + one extra pass per
  atlas-covered object; those objects also leave static batching (a possible
  future optimization: merge the AO passes into the batch bags — they share
  one texture). A scene with emissive lamps pays one more terrain pass and one
  more pass per lit object for the additive light.
- **Package size**: a lightmap pass draws the SAME vertex array as the base
  pass it darkens, and both must split it into the same VU1 packages — or the
  two coplanar passes can classify a triangle differently against the frustum
  and disagree about depth, which is what made baked shadows fight z-index
  with the ground. The generated game pins every pass of an object (and of a
  terrain chunk) to one package size; see
  [docs/reflective-materials.md](reflective-materials.md) for the mechanism
  and the measured cost.
- **Build time**: a few hundred ms per scene, re-run on every build
  (deterministic, no caching needed).

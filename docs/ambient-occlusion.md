# Baked ambient occlusion (contact shadows)

![Ambient occlusion and global illumination controls](img/ambience-editor.png)

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
- **Imported models do not RECEIVE** the scene's contact shadows: per-vertex
  occlusion on authored low-poly meshes reads as triangulated shading. The
  per-vertex pipeline (`aobake::modelAO`, the `.aov` sidecar, the
  `LeanObjLoader` reader) stays in the tree, disabled. They **do self-occlude**
  — see [Model AO](#model-ao) below, which is per texel and free. Animated
  models relight dynamically and are unaffected, like with baked point lights.

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

## Model AO

*Tools > Baked Lighting > Model AO. Headless:
`tyrax-editor --bake-model-ao <projectDir> [--texbake]`.*

Everything above is about the **scene**: how objects darken each other and the
ground. Model AO is the other half — a model darkening **itself**, in the
crease where its own two surfaces meet, under its own eave, inside its own
doorway. In a real game most objects are textured `.obj` models, and until this
existed they had no automatic occlusion at all: the lightmap atlas refuses a
textured surface (it is additive, and an additive term over a texture blows out
its dark texels) and baked GI reaches them only as flat per-vertex probe light.

It is the [Material Editor's map bake](material-baking.md) — the same
`matbake` raytracer, the same UV-space rasterization — run **per model asset**,
automatically, and multiplied into the texture that model already ships.

**Why that is affordable comes entirely from what is being baked:**

- A model's own surface occlusion is **transform-invariant**. It does not
  change when you move, rotate or scale the object, so every instance of the
  asset shares one map. Twenty crates cost one bake.
- The pixels ride in an **existing** texture, so it costs **zero extra GS
  VRAM** ([gs-vram.md](gs-vram.md)). That is the difference between this and
  [pre-lit models](prelit-models.md), which buy per-pixel *scene* light at one
  unique texture per object.

### The knobs

| Control | What it does |
|---|---|
| **Bake model AO into textures** | The project default. New projects have it on; a project saved before it existed keeps its look until you tick it. |
| **Strength** | `ao' = 1 − strength × (1 − ao)`. An *apply-time* remap — moving it re-multiplies, it never re-bakes. |
| **Rays per texel** | Hemisphere rays. More = less noise, linearly more bake time. |
| **Distance** | How far the occlusion reaches, in world units. 0 = a quarter of the model's own bounding-box diagonal, which darkens where surfaces *meet* rather than shading the whole prop. |
| **Per-asset column** | *Default* / *On* / *Off*, keyed by the model's asset path (`Project::modelAoMode`). |

These are project-wide bake quality, deliberately **not** part of the ambience
preset overlay — a preset changes what the light looks like, this does not.

### What it refuses to bake, and why

The panel names every one of these on its own row, and so does the build log.
An AO map that silently is not there is indistinguishable from a broken
feature.

| Skipped | Why |
|---|---|
| animated `.glb`/`.fbx` | They relight dynamically, and their textures go down the `animBakedTextureRel` path. |
| an untextured material | It already has the scene lightmap route, which costs no texture at all. |
| **a texture used by more than one model asset** | Two UV layouts over one image: a single multiply would be wrong for both. This is what makes a procedurally-scattered project (whose chunks all share the source asset's texture) skip cleanly. |
| **a pre-lit material** (`res/materials/*-lit.png`) | Its gather already contains occlusion; multiplying AO in again would darken the same shadow twice. |

### The cache

`.res-baked/modelao/<pair>-<signature>.png`, one grayscale map per (model,
texture) pair, never shipped. The signature is a **content** hash — the `.obj`
bytes, every `.mtl` it resolves, the texture's *dimensions*, the ray/distance
knobs and the module version — following the same rule
[GI](global-illumination.md) uses: never mtimes, because a checkout or a copy
is not an edit.

Deliberately **not** in it: the texture's **pixels**. AO is a function of
geometry and UVs alone, so repainting a texture does not throw the bake away.
Nor is *Strength*, which is applied at multiply time.

### Where the multiply happens

In exactly one function (`modelao::applyToRgba`), called from three places, so
they cannot drift:

1. **`texbake`**, at build — into the mirrored PNG's RGB, before the PS2-valid
   resize and the palette quantization, so the CLUT is computed from the pixels
   the console will display. Atlas members get it as they are composited into
   their page. **Sources in `res/` are never written.** A headless `--build`
   is therefore correct with no editor running.
2. **The editor viewport**, as a texture is uploaded — so the preview shows
   what the console will.
3. **[litbake](prelit-models.md)**, into the albedo it reads — otherwise an
   object would lose its self-AO the moment it went pre-lit.

**Alpha is never touched.** StaPip's GS alpha test discards `alpha == 0`
texels, so writing occlusion into alpha would delete a pass — the same rule the
lightmap alpha floor above exists for.

The editor bakes missing maps on a worker thread whenever the settings or the
per-asset overrides change; a build bakes whatever is still missing itself.
Bake time is seconds at these resolutions (the map is 64–256 px square,
derived from the texture it multiplies into).

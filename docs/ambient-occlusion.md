# Baked ambient occlusion

Soft contact shadows where geometry meets: terrain self-shadowing (ravines,
the foot of hills), darkening where objects touch the ground and each other,
and raycast self-occlusion inside imported `.obj` models. Everything is baked
into the same per-vertex colors the directional light already lives in, so the
PS2 pays **zero per-frame cost** — the game only pays a little extra work at
scene load, where it bakes its vertex buffers anyway.

Authored per **ambience preset** (*Tools > Ambience Editor*, "Ambient
occlusion" block): an enable toggle, an **AO quality** switch (see below),
**AO strength** (how dark full occlusion gets) and **AO radius** (world units
the contact darkening reaches; terrain self-shadowing scans 3× this). New
projects have it enabled on their default preset; projects saved before the
feature keep their look until it is turned on. The viewport previews the
result live and matches the console output.

## AO quality: Vertex colors vs Textured (experimental)

**Vertex colors** (default) folds the occlusion into the per-vertex shade -
completely free at runtime and static-batching friendly, but Gouraud
interpolation across big triangles shows its edges: on the sparse terrain
grid and on low-tessellation primitive faces a contact shadow becomes a
diamond-shaped gradient.

**Textured** bakes the same occlusion into **per-pixel AO textures** instead:

- a **terrain AO map** covering the whole terrain (heightmap self-occlusion +
  occluder contact, ≤256×256), drawn as one extra alpha-blended terrain pass;
- a per-scene **primitive lightmap atlas**: every static box / sphere /
  cylinder / cone / plane / save point gets atlas regions (box 6 faces,
  sphere 1, cylinder 3, cone 2, plane 2 - `aobake::bakeSceneAoAtlas`, the
  builders' UV layouts inverted), rasterized on the host with the same
  occluder + ground formulas and drawn as a per-object extra pass.

The textures are black RGBA PNGs whose **alpha is the occlusion** - the GS
alpha-over blend `(Cs-Cd)*As/128 + Cd` with a black source collapses to
`Cd*(1-As/128)`, an exact per-pixel multiply. They ship as full RGBA32 on
purpose: the engine's palettized (tRNS→CLUT) path loses the smooth alpha
gradient (verified in PCSX2 - a quantized bake renders as nothing), which is
why both images are capped at 256×256 (~320 KB of the ~1.33 MB GS texture
budget together).

Costs of Textured, beyond VRAM: one extra blended terrain pass + one extra
pass per covered object (fill rate + EE submit), and the covered objects
**leave static batching** (the merge cannot carry the extra pass). Imported
`.obj` models, physics/pickable/save-state objects and spawned clones keep
the vertex bake in this mode (their occlusion still re-bakes on rebuild);
`texbake` writes the images into `.res-baked/aomap/` + `.res-baked/aoatlas/`
and codegen emits the matching atlas rects from the same deterministic bake,
so pixels and UVs cannot drift. The editor viewport computes its AO preview
per fragment either way, so it approximates the Textured look in both modes.

## The three bakes

| What | Where it is computed | How it ships |
|---|---|---|
| Terrain self-occlusion | Host, at build (`aobake::terrainAO`: an 8-direction horizon scan over the heightmap) | `TERRAIN_AO_TABLES` in `inc/terrain_heights.gen.hpp` — one visibility byte per heightmap vertex, multiplied into the terrain vertex colors at chunk build |
| Contact darkening (object ↔ ground, object ↔ object) | PS2 EE, at scene load: analytic response to the occluder shapes in `inc/ao_data.gen.hpp` (`aobake::collectOccluders` reduces every solid authored object to an oriented box or sphere on the host) | Baked into the vertex colors by `pushVert`/`shadeAt` in the generated game cpp (`aoOccluderAt`/`aoShadeMul`), before point lights add on top |
| Model self-occlusion | Host, at build (`aobake::modelAO`: 24 deterministic cosine-weighted rays per obj position against the model's own triangles) | A `"<model>.aov"` sidecar written by the texture bake into `.res-baked/models/` ("TXAO" + u32 LE count + one byte per obj `v` entry), read by the engine's `LeanObjLoader` and folded in by `pushVert` |

The occlusion **formula is implemented twice** — the generated game
(`aoOccluderAt`/`aoShadeMul`, per vertex at load) and the editor viewport
fragment shader (`aoOcclusion`, per fragment, live) — keep them in sync. The
occluder *shapes* and both grid/model bakes have a single host implementation
in `src/aobake.cpp`, shared by codegen, texbake and the viewport, so those
cannot drift.

## What casts and what receives

- **Casts**: Boxes, Spheres, Cylinders, Cones, Planes (as thin slabs — rotated
  planes make walls), Save Points and static `.obj` Models (their AABB).
  Markers, lights, decals, mirrors, portals and animated models cast nothing.
- **Receives**: everything that goes through the static vertex bake — terrain,
  primitives, static `.obj` models (which additionally receive their own
  raycast self-AO).
- **Animated models** (`.glb`/`.fbx`) neither cast nor receive: they relight
  dynamically every frame, exactly like they already ignore baked point
  lights.

## Static by design

AO is a bake. Runtime moves re-bake the **moved object's own shading** (its
geometry rebuilds and re-reads the occluder table at the new position — the
same rule as physics objects), but the shadow it *casts on others* stays where
the scene was built: terrain chunks and neighbouring objects keep their baked
colors. The same applies to Live Link edits — the moved object streams and
re-shades, its cast shadow updates on the next build. Changing the AO
settings themselves (or anything in the ambience preset) is a rebuild, like
all baked lighting.

## Costs

- **ELF size**: one byte per heightmap vertex per AO-enabled scene (a 128×128
  map ≈ 16 KB) + ~5 floats per solid object; model sidecars are one byte per
  obj position.
- **Scene load**: per vertex, a loop over the few occluders in range (the
  table is pruned per object/chunk, the point-light dcache lesson) + one
  bilinear heightmap sample. Negligible next to the existing shade bake.
- **Runtime**: zero — it is just different vertex colors.

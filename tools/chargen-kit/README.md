# chargen-kit

Builds `resources/chargen-kit.bin`, the one file the editor's Character
Generator reads (docs/character-generator.md). **You do not need any of this to
build or run the editor** - the kit is committed and linked into the binary.
This is for changing what is IN the kit: the body, a slider, a garment, a clip.

```
python make_kit.py <work_dir>          # everything, ~30 min (+ ~1.3 GB download once)
```

Needs Python 3 with numpy and Pillow, and Blender 4.x/5.x (found on PATH, in
`$BLENDER`, or in the usual install folders; `--blender <exe>` overrides).

## The stages

| Script | Runs in | Does |
|---|---|---|
| `fetch_sources.py` | Python | downloads the CC0 sources: MakeHuman `makehuman/data` (reference mesh, rig, weights, targets), the MakeHuman *system assets* pack and the `*_cc0` community packs named in `catalog.py`, Quaternius' Universal Animation Library 1 + 2 |
| `kit_body.py` | Blender | the `female1605` body + low-poly eyes, the re-packed PS2 atlas, and every layer baked into it: 18 skins, eyes, 12 brows, 4 lashes, AO and the face-paint masks |
| `kit_wear.py` | Blender | each `catalog.py` entry into a SHELL (body vertices pushed out + texture in the atlas) or a MESH (remeshed, unwrapped, baked, bound to the body surface) |
| `anim_retarget.py` | Blender | the 87 library clips onto the rig, as local rotations + a hips track |
| `build_kit.py` | Python | targets projected onto the body's vertices, skin weights, rig, sliders, layers, wardrobe and clips into the binary |

Data-only modules: `catalog.py` (the wardrobe), `rig.py` (the 35 bones and which
MakeHuman bones fold into each), `kit_body_masks.py` (face-paint regions),
`build_kit.SLIDERS` (the detail sliders). Helpers: `mhkit.py` (MakeHuman file
readers, numpy only), `bmh.py` (Blender object builders).

## Licences: CC0 only

Everything the kit is built from is CC0 1.0, and that is a rule, not an
accident - the kit ships inside the editor and inside every generated game.
MakeHuman's community packs come in CC0 and CC-BY builds (`hair02`, `shirts02`,
...); `fetch_sources.py` only ever requests `<pack>_cc0.zip`. MakeHuman's DATA
is CC0 and its PROGRAM is AGPL-3.0: nothing here reads, links or ports the
program. Check a new source's licence file by file before adding it to
`catalog.py`.

## Adding things

- **A garment or hairstyle**: one line in `catalog.py` (slot, shell or mesh,
  `sex`, `cutout` for hair), then `kit_wear.py ... <id>` for just that item and
  `build_kit.py`. Look at it with `tyrax-editor --chargen` + Blender before
  committing - a shell's coverage and a mesh's remesh both fail visibly.
- **A slider**: one line in `build_kit.SLIDERS` naming MakeHuman targets; the
  fetch picks the targets up from there.
- **A clip**: `anim_retarget.py` takes every action in the two libraries;
  `chargen::defaultClipSet()` decides which are in the standard set.

## Traps (each cost a broken bake)

- Baking anything onto the HIGH-res reference body with the low-poly body in
  the scene: the low-poly lies within millimetres of it, partly outside, and
  occludes it in patches. Hide it (`hide_render`).
- Selected-to-active between the reference body and the low-poly one: a cage
  loose enough to catch every texel starts rays inside the next limb in the
  armpits and crotch (black streaks); a tight one misses the fingers. AO and
  masks are baked in MakeHuman's own UV layout instead and re-sampled.
- Voxel-remeshing a sheet thinner than the voxel: crumbs. Solidify by >= 2x.
- A quadric collapse to 2% of a voxel mesh: shards. QuadriFlow.
- `loop_triangles` after applying modifiers: stale until `calc_loop_triangles()`.
- MakeHuman `.mhclo` files may put `material` between `verts 0` and the first
  vertex, and may give a vertex as a single index (an exact base vertex).
- A macro target can be EMPTY (the average body is the base itself).
- The triangulation is shared: garments bind to body triangles BY INDEX, so
  `build_kit.py` and `kit_wear.py` both use `mhkit.triangulate`.

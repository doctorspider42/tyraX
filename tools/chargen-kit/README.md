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
| `kit_body.py` | Blender | one game body (`female1605`, then `male1591`) + low-poly eyes, its re-packed PS2 atlas, and every layer baked into it: 18 skins, eyes, 12 brows, 4 lashes, AO, the face-paint masks and the island mask |
| `kit_wear.py` | Blender | each `catalog.py` entry into a SHELL (body vertices pushed out + texture in the atlas) or a MESH (remeshed, unwrapped, baked, bound to the body surface); for the second body `--reuse-mesh <first stage>` binds the first body's meshes instead of remeshing |
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
- Re-running `kit_body.py` must not move the atlas (shells are baked into it):
  the UV packer is deterministic - check `uv_new` before/after if in doubt.
- `--reuse-mesh` takes the first body's mesh from its FILE: a mesh rebuilt in
  Blender loses degenerate triangles to `validate()` and no longer matches.
- Bound to the nearest point, a skirt's hem levers off the thigh: skirts and
  dresses bind only above the crotch. And a `.mhclo`'s hidden-vertex list is for
  ITS garment: a body triangle is hidden only if it lies inside our stand-in.
- A shell is decided per body QUAD, and the low-poly rows are 5-6 cm tall: a
  cloth edge crossing mid-row either drops the row or keeps it. Demanding all
  four corners cut a square bib out of the man's crew neck; `make_shell` takes
  a quad with three covered corners, or two plus two within 4.5 cm of the cloth.
- A mesh vertex's offset from the body point it rides is a VECTOR in that
  triangle's frame for skirts and dresses: along the normal alone, a long skirt
  bound to the hips folded up into a mini skirt. The tangents are the bisector
  and difference of the unit edges (`tangent_frame`): a plain AB/AC basis is
  near-singular on slivers and its cancelling coefficients spiked shoes after a
  morph, and with no cross product the corner swap in `build_kit.py` only
  negates the last one.
- A recolour dyes only texels near the garment's KEY colour (`key_colour` in
  `build_kit.py`, the fullest chromaticity x luma bin - not the mean, which a
  white shirt pulls halfway to grey). A uniform dye painted a trouser suit's
  blouse and scarf navy along with the suit.
- Face bones (`rig.FACE`) are appended to the rig, never inserted: clips are
  indexed by bone. Mesh items fold the face bones' weights into the head -
  glasses bound by the eyes blinked with the lids.
- The proxy's mouth tube is open at its inner end; with the jaw down the game
  looked through the head. `build_kit.cap_mouth` closes it without new vertices.
- Spring bones (`rig.HAIR_SPRINGS`, `SKIRT_SPRINGS`) are appended after the
  face. A rig head can be an affine mix of joints (`rig.heads`). Only catalog
  items that hang take their weights (`build_kit.spring_weights`).
- Expression bones (brows, mouth corners) are in `rig.FACE` too, so mesh items
  never take their weights; they pivot deep in the head on purpose.

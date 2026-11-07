# Character Generator kit

The editor embeds `resources/chargen-kit.bin`. To rebuild it from source,
run `python make_kit.py <work_dir>` with Python 3, NumPy, Pillow and Blender.
Set `BLENDER` to the executable if the build cannot find it. Rebuild the
editor after writing the kit.

The fetch stage retrieves MakeHuman reference geometry, skin layers, targets,
rig data and Quaternius motion clips. The starter wardrobe and portrait
sources live in [studio](studio). The project artwork includes ImageGen
diffuse sheets and geometry adapted from the Blender-authored Night Warden.
Third-party source attribution is in [the notices](../../THIRD-PARTY-LICENSES.md).

## Body and wardrobe

1. `kit_body.py` prepares the five neutral bodies, UVs, skin and makeup layers.
2. `anim_retarget.py` prepares the shared 50-joint rig's animation library.
3. `build_kit.py` projects the macro and detail targets. `studio_topology.py`
   refines long edges around the jaw, cheek, throat and shoulder. It transfers
   weights and every sparse morph to the new vertices using the same stencil.
4. `studio_bodies.py` sculpts the neutral bodies and joint positions and
   projects the portraits, eye detail and fabric panels into each body's UVs.
   Shell cuts define the sleeve, cuff, neckline, waist and hem on the neutral
   surface. They follow the same vertices as the body at runtime.
5. `studio_gear.py` builds the separate garments, headwear and hairstyles.
   It binds each item to every refined body using triangle IDs, barycentric
   coordinates and offsets in the local surface frame. The NPZ sources store
   Y-up positions in metres, triangle indices and per-corner UVs with V up.

Each haircut is lofted from three silhouettes. Raised locks curve over the crown and taper beyond the hairline. The mesh uses fewer cross sections and locks at lower detail levels. The atlas contains front, profile and rear projections and cutout tips. Preview
new hair from the front, side and back, including beneath both hats. For a
new garment, test the actual generator export on both sexes, a broad and a
narrow build, and each detail level. Existing recipe IDs are translated by
`studio/legacy_names.json`. A retired full outfit can expand to two pieces.

## Export

Use `tyrax-editor --chargen-reference <dir>` for clean base meshes in metres.
Use `tyrax-editor --chargen <recipe.json> <out.glb>` for a dressed, animated
character. Pass `--variants N` to regenerate the matching palette sidecars.

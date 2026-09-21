# CC96 strip study — additional vehicle asset

This is an independently authored panel-grid body variant for the existing
CC96 coupe. It is the model selected by `vehicle-playground.tyra` for the
canonical one-car optimization scene. Its authoring scripts only regenerate
the source asset and reports; refreshing the project runs the ordinary vehicle
importer and rebuilds its game resources.

## Files

* `cc96-strip-study.glb`: complete indexed vehicle, embedded opaque 256x256
  atlas, body and four separately named wheel nodes. Metres, +Y up, +Z forward.
* `cc96-strip-study-body.obj` and `.mtl`: editable body panel quads (triangles at
  pole caps), explicit normals and UVs. The OBJ intentionally excludes wheels;
  the complete car is in the GLB. Keep the MTL and atlas beside the OBJ.
* `cc96-strip-study-atlas.png`: base-color artwork; no normal map, transparency
  or PBR-only surface detail is required. Live reflections are not baked in.
* `cc96-strip-study-preview.png`: orthographic host inspection views, including
  topology. These are not editor, emulator or console captures. The simple
  preview has no live reflection pass.
* `geometry.json` and `strip-verification.json`: source inventory and offline
  verification against the repository's existing `meshstrip.cpp`.

## Authored shape and topology

The body retains the coupe's long dark bonnet, compact cabin, four round
headlights, chrome bumper strips, rear lamps and separate mirrors. Wheel
openings are cut into the side silhouette with inner returns. The body is new
geometry, not a decimated copy of the original or the rejected 550-triangle
experiment. Wheels retain the efficient variant's 76-triangle construction,
node names, axle positions and dimensions.

Longitudinal panel grids share exact position/normal/UV attributes inside each
surface. Normal seams separate the shoulder, cabin panels, bumper and trim.
UV coordinates remain continuous within a panel; small details use the atlas.
The GLB uses indexed triangle primitives, not GLTF strip primitives: the
importer generates PS2-sized strip runs from the ordinary triangles while
retaining ordered lamp ranges as lists.

The body has **3,782 triangles**, including 96 light triangles. Four wheels add
304 triangles, giving **4,086 triangles for the complete source vehicle**.
This is a packing experiment that preserves geometry detail, not a promise of
Burnout-equivalent final appearance or a measured console performance result.

## Offline strip result

The existing stripifier is run unchanged with `Weld::kFull` (position, normal,
UV) and 75-vertex runs, including joins and padding:

| Source part | Triangle-list vertices | Strip vertices | 75-vertex packages, list → strip |
| --- | ---: | ---: | ---: |
| Main body, glass and trim | 11,058 | 4,212 | 148 → 57 |
| Front lamps | 144 | 144 (list retained) | 2 → 2 |
| Rear lamps | 144 | 63 | 2 → 1 |

The main body uses **61.9% fewer submitted vertices** without losing or adding
any nondegenerate triangle or changing its normal/UV attributes. This compares
two representations of THIS model, not this model against the old vehicle's
runtime inventory. Project refresh confirms the main body at 4,212 submitted
strip vertices / 57 packages and the complete near vehicle at three submits
(body, lamps and the shared wheel batch). Lamp parts remain lists because
rear/front ranges depend on corner order. Reflection, shadow and LOD behavior
use the normal vehicle runtime; physical-console performance still has to be
measured per scene.

## Reproduce

From the example directory, with Python, NumPy, Pillow and g++ on PATH:

```
python authoring/build-cc96-strip-study.py
python authoring/verify-cc96-strip-study.py
```

The builder reads `res/models/cc96-efficient.glb` only for wheel bounds, names
and translations; its wheel construction/artwork helpers come from
`authoring/prepare-efficient-vehicles.py`. The checker compiles the existing
host-only stripifier in a temporary directory and compares the complete
triangle multiset after expanding every independent strip run. It also checks
finite attributes, unit normals, nondegenerate geometry, valid UVs/indices and
unchanged wheel anchors/bounds. No game rebuild or hardware run is performed.

Do not lower `bodyTris` below 3,782 when evaluating the near source, or importer
decimation would invalidate this comparison.

The reference vehicle's attribution remains in `../car1-CC0-licence.txt`.
No geometry or textures from Burnout are included.

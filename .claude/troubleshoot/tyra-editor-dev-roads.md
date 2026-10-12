# tyra-editor-dev — roads

From `## Motor District and flat-road spans (1.85.0)`, `## Road and terrain
triangle strips (1.96.0)`, the road-intersection notes and `## Road editing
(1.151.0)`. See `docs/roads.md`, `docs/model-pipeline.md`.

## Motor District and flat-road spans (1.85.0)
**Cause/rule:** the roadgen.cpp / templates.cpp `buildRoads` twins sample every
lateral height, then retain the established horizontal collapse, or merge a
non-flat span's lateral cells into MAXIMAL RUNS whose every dense sample lies
within `kSpanFlatness` of an affine 3-D quad whose parallelogram defect is within
`kSpanShear` (roadgen.hpp; 1e-5 and 0.05).
**Fix:** do not infer planarity from the shoulders — an interior crown or saddle
must retain its samples. Run
`examples/vehicle-playground/authoring/verify-road-twins.py` for a compiled
comparison of both actual implementations, then build/drive the example. ROADS now
logs emitted vertices as well as chunks. The district's seven-road network is an
EE memory stress case, not just a screenshot fixture. **The reduction used to be
all-or-nothing** (docs/roads.md, "The lateral budget"): a road is sampled far more
finely across than the heightfield under it (the district's terrain cell is 4
world units against a 0.5-unit lateral sample), so the FULL width is almost never
one plane — test sub-spans, and measure which of your gates (the UV budget, not
the surface one) is firing before relaxing either.

## Road and terrain triangle strips (1.96.0)
**Cause/rule:** since 1.119 each road carries optional `roadSampleStep` (1..2,
default 1). `roadgen::tessellate`, viewport drawing/picking and generated
`RoadDefRt`/`buildRoads` must receive the same value. `roadgen::tessellateStrips`
is a THIRD twin of the same surface, beside `tessellate` (the triangle list, source
of truth for the editor viewport, picking and the align pass) and the generated
`buildRoads`. Both emitters share `buildRows` and `spanCuts`, so the sampling, the
height queries, the texture arc length, the lift and the exact planar-span
reduction happen in ONE place. `spanCuts` contains the greedy extension
`while (j1 < crossSteps && spanIsExact(rows, i, j0, j1 + 1)) ++j1;` that
`verify-road-twins.py` pins by text — moving it is a deliberate act, and the
oracle builds its dense reference by DELETING that one line.
**Fix:** geometry may be coarse, but texture V still integrates the spline at the
original 1 m cadence, or lane marks slide when detail changes
(`road-budget-sweep.py` compares candidates against the dense surface). Motor
District keeps Ring road and East crest at 1 m and uses 2 m on the other five
roads; do not promote 2 m to a universal default without crest/bend tests.
`templates.cpp` carries both budgets as literals (the generated `buildRoads` is a
raw string and cannot read a constant); a `static_assert` against roadgen.hpp at
the top of templates.cpp is what stops them drifting. Rules that cost real
defects: **which way the strip runs depends on the span's own reduction** (dense
spans strip ACROSS the road, collapsed full-width spans ALONG it — taken laterally
a collapsed span is exactly break-even and triples the GS primitives); **the
interleaving decides which DIAGONAL splits each quad** (only one of the two legal
orders reproduces the list stitch's own cut); **chunk boundaries are now
load-bearing** (a run may not straddle a chunk, which is why `kChunkSpans` and
`kChunkBudget` moved into `roadgen.hpp`); and the terrain builder in templates.cpp
is the same shape and strips **only with a terrain material** (the untextured
checker is a per-QUAD colour). `verify-road-twins.py` now compares the strip output
vertex for vertex AND chunk for chunk, and asserts an exact triangle-SET equality
against the list on top of the preserved-surface baseline. `ROADSTRIP` /
`TERRAINSTRIP` in `bin/log.txt` carry each producer's own surface triangle count —
the pipeline's counters cannot answer "same geometry?" across representations.
Since 1.117, both road emitters and generated `buildRoads` rebase longitudinal V
by `floor(V at chunk start)` (texture-identical under REPEAT, keeps physical-GS ST
values bounded); the oracle canonicalizes only whole V repeats. Since 1.117.2,
`TYRA_STRIP_ROADS` defaults to 1 again; 0 is the diagnostic list arm. The physical
road-only district fell from 63,966 vertices / 880 packages to 24,576 / 347 with
the same 21,322 surface triangles.

## Road intersections and junctions
**Cause/rule:** automatic road intersections are host decisions. The Properties
picker stores a material path in the legacy-named `roadTexture` /
`roadIntersectionTexture` fields; `project::resolveRoadTexture` resolves the first
`map_Kd`, while direct PNG values remain a backwards-compatible path. Two crossing
roads' authored intersection references must match; `roadgen::findJunctions`
samples the same Catmull-Rom centre line, codegen uses the centre plus four
strip-overlap corners as the footprint.
**Fix:** since 1.151.2 `roadgen::tessellateJunctionSurface` fits an adaptive,
conforming mesh against actual road triangles and proves 0.02-unit clearance at
triangle intersection corners. Codegen bakes XYZUV in `ROAD_JUNCTION_VERTS`;
`buildRoads` only uploads it. Flat patches retain four triangles. Use the rendered
terrain triangle sampler (`roadgen::terrainHeight`) when generating the source
roads, not `project::heightAtWorld`'s bilinear interpolation; never move road
pairing or surface fitting onto the EE. **Every crossing decision is
`roadgen::planCrossings` (1.145.0, docs/roads.md "Junction overrides")**: patch or
not, who runs through, which spill survives, the per-junction overrides
(`SceneData::roadJunctions`, matched by road-id pair + nearest position) and the
winner OVERLAYS all come out of that one call; codegen, `Viewport::syncRoadDraws`
and the test drive (`roadgen::addCrossingsToSurface`) only read its result, over
`project::crossingRoads`. Two rules: spills are filtered PER CROSSING (a triangle
belongs to the nearest crossing of its pair), and overlay decals precede the
spills (the console reads all spill Y before adding any chunk, so the overlay's
height reaches the spill through the row's `lift`, not `roadSurfaceAt`). The
junction UI is `src/junction_ui.cpp`; a junction is selected by identity
(`App::junctionSel_`), not by object index.

## Road editing (1.151.0)
**Cause/rule:** road loops repeat the first XZ pair at the end of `roadPoints`;
`roadgen::isClosed` and `controlCount` hide the seam sentinel from editing. The
host and generated `buildRoads` wrap Catmull-Rom neighbours and final tangents
together. No format field was added. `moveControl`/`removeControl` preserve
closure and minimum counts.
**Fix:** Properties has collapsible Crossings/Points and a reversible Closed loop
toggle; viewport Shift-click deletes, endpoint snap closes at release, one undo
each. Outlines reuse `Viewport::RoadDraw`; full-width soft borders retain a
separate outline. During viewport drag only, both crossing caches defer work until
release. Verify loop seams and open-road parity with the vehicle-playground road
twin oracle; UI scripts can target `Road point N` handles and use `shiftclick`.
Road-handle overlays must clip both pixels AND ImGui item bounds to the scene
canvas (1.151.1): an off-canvas InvisibleButton calls ItemSize and grows the
window's scroll extent; restoring cursor position alone does not undo that. The
scene Viewport uses NoScrollbar/NoScrollWithMouse plus zero offsets, while the
welcome project list preserves its scrolling. Test wheel zoom with road controls
outside the frame and compare Viewport canvas rectangles before/after.

## Reference facts (upstream `vehicles-and-roads.md`, road half)
- Files: `roadgen.cpp/.hpp` (~1.2k+420) host spline tessellator, single source for viewport and the generated boot-time `buildRoads` raw-string twin in templates.cpp; `junction_ui.cpp` (~370, selected by `App::junctionSel_`).
- Lateral span reduction: keep the horizontal collapse or merge a non-flat span's cells into MAXIMAL RUNS within `kSpanFlatness` and `kSpanShear` (1e-5, 0.05, roadgen.hpp); never infer planarity from the shoulders.
- Strips: `roadgen::tessellateStrips` is a THIRD twin beside `tessellate` (source of truth) and `buildRoads`; shared `buildRows`/`spanCuts` (greedy `while (j1 < crossSteps && spanIsExact(rows, i, j0, j1 + 1)) ++j1;`); `kChunkSpans` (36), `kChunkBudget` (1800); `TYRA_STRIP_ROADS` (default 1); oracle `verify-road-twins.py`; `ROADSTRIP`/`TERRAINSTRIP` in `bin/log.txt`.
- Bounded V: rebase longitudinal V by `floor(V at chunk start)`. Sample step `roadSampleStep` (1..2) reaches `roadgen::tessellate`, viewport draw/pick, `RoadDefRt`/`buildRoads`; texture V at 1 m cadence.
- Intersections: `roadTexture`/`roadIntersectionTexture` + `project::resolveRoadTexture`; `roadgen::findJunctions`; `tessellateJunctionSurface` -> `ROAD_JUNCTION_VERTS`; `roadgen::terrainHeight` not `project::heightAtWorld`; `roadgen::planCrossings`; `SceneData::roadJunctions`; `Viewport::syncRoadDraws`; `roadgen::addCrossingsToSurface`; `project::crossingRoads`.
- Road editing: loops repeat the first XZ pair; `roadgen::isClosed`/`controlCount`; `moveControl`/`removeControl`; `Viewport::RoadDraw`; `Road point N` handles; `shiftclick`. Road chunks opt out of occlusion culling/frustum pre-test; receive baked shadows via `decalproj::Receivers::roads`.

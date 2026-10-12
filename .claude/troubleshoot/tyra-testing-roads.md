# troubleshoot/tyra-testing-roads.md

Roads, junctions and proxies. Symptom first.

## Motor District and flat-road spans (1.85.0)

**Cause:** the roadgen.cpp / templates.cpp `buildRoads` twins sample every lateral height, then retain the established horizontal collapse, or merge a non-flat span's lateral cells into maximal runs within the two budgets in roadgen.hpp (`kSpanFlatness`, the surface and the seam; `kSpanShear`, the UV).
**Fix:** do not infer planarity from the shoulders: an interior crown or saddle must retain its samples. Run `examples/vehicle-playground/authoring/verify-road-twins.py` for a compiled comparison of both actual implementations, then build/drive the example. ROADS now logs emitted vertices as well as chunks. The district's seven-road network is an EE memory stress case, not just a screenshot fixture.

## Road editing (1.151.0)

**Cause:** road loops repeat the first XZ pair at the end of `roadPoints`, and the seam sentinel must stay hidden from editing.
**Fix:** `roadgen::isClosed` and `controlCount` hide the seam sentinel. The host and generated `buildRoads` wrap Catmull-Rom neighbours and final tangents together. No format field was added. `moveControl`/`removeControl` preserve closure and minimum counts. Properties has collapsible Crossings/Points and a reversible Closed loop toggle; viewport Shift-click deletes, endpoint snap closes at release, one undo each. Outlines reuse `Viewport::RoadDraw`; full-width soft borders retain a separate outline. During viewport drag only, both crossing caches defer work until release. Verify loop seams and open-road parity with the vehicle-playground road twin oracle; UI scripts can target `Road point N` handles and use `shiftclick`.

## Hull and reflection proxy acceptance (1.111.0)

**Cause:** a proxy must preserve collision/picking identity, and material consolidation must not lose ordered parts.
**Fix:** for a hull proxy, bake a concave multi-material OBJ through the Properties UI, resave, refresh generated assets, and inspect the emitted OBJ: one `usemtl`, `4n-4` faces for its convex XZ ring, and a non-billboard far assignment. Boot near/far poses and verify collision/picking identity from generated scene data. For material consolidation, use a fixture with repeated `usemtl` groups that resolve to identical state and inspect `.tmdl` part counts; include a captured 4/8/16-view impostor as the negative control whose ordered parts must survive. For a reflection box proxy, compare the per-producer frame inventory and a console self-capture with only `reflectionProxy` changed. The env-probe object row should fall to one bag / one package while the main-view rows and generated collision fields remain identical.

## Automatic road junctions

**Cause:** junction detection is texture- and topology-driven.
**Fix:** cross two roads with the same intersection texture, one road with no value and one pair with different values. The first pair alone must produce a `ROAD_JUNCTIONS` row, with 12 vertices / four triangles on flat ground. Since 1.151.2, uneven junctions refine conformingly on the host (up to 256 triangles), then ship baked XYZUV in `ROAD_JUNCTION_VERTS`. The viewport and PCSX2 patch must match. `verify-road-twins.py` also exercises terrain folds and both Market endpoints, proves the old fan regression is triggered, sweeps clearance and checks nonempty runtime junction uploads. Move one spline off the crossing, resave and refresh: the row must disappear. Always rerun `verify-road-twins.py`.

## The physical-PS2 stretched-road regression

**Cause:** road V is rebased per chunk, and PCSX2 never reproduced the original smear.
**Fix:** inspect the generated road STs first: each chunk's V must begin in `[0,1)` and stay bounded by roughly that chunk's nine repeats. Then park the same camera on hardware and confirm lane markings survive; PCSX2 is only the topology/image regression arm. Road strips ship on since 1.117.2: `ROADSTRIP ... strips 1` is expected; use `TYRA_STRIP_ROADS=0` only as the list control. Across an A/B, compare the producer's surface-triangle count (it must be identical), then packages, vertices, synchronized frame costs and several physical-console road views.

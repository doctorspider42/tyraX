# troubleshoot/tyra-testing-procedural.md

Procedural placement, frozen procedural bakes, and scene object data iteration. Symptom first.

## Procedural placement validation (1.154.0)

**Cause:** the spatial hash and the road path filter each need an independent oracle.
**Fix:** use a host harness to compare the spatial hash with an independent all-pairs AABB rejection oracle, including rotated/off-centre assets, differently sized models, clearance, cell boundaries and the giant-bound overflow. Check painted islands inside a footprint, upper-layer occlusion, absent terrain, spline bends, closed roads and road width/point edits invalidating `bakeHash`. Repeat evaluation and baking, and verify serialization and the runtime capability rejection. Drive Validate Placement's checkboxes and material picker through `--ui-script`. Use vehicle-playground's procedural scene for `--refresh-gen` and a game boot; benchmark many candidates only in the host filter, not as a PS2 timing claim.

## Road path filtering (1.155.0)

**Cause:** origins must be tested against the road ribbons independently.
**Fix:** compare origins against an independent point-in-triangle oracle over straight, curved and closed road ribbons. Check legacy `roads=0/1`, both road targets, no roads and missing/non-road targets; Only must ignore canopy width but retain independent collision/material checks. Procedural parameter combos now register their scoped labels for UI scripts: open Roads/Road/Material by name and assert the popup options; no coordinate workaround is needed for these node controls.

## Frozen procedural bake acceptance (1.156.0)

**Cause:** a frozen (stale/unbaked) volume must be ignored by `anyStale` and refused by an explicit `bakeVolume`.
**Fix:** freeze a stale/unbaked scratch volume, then move roads, edit terrain/materials, seed and graph parameters. Compare generated object JSON and mesh hashes before and after both ordinary and forced `bakeAll` and `refresh-gen`. Save/reload and repeat; `anyStale` must ignore it and explicit `bakeVolume` must refuse. Unfreeze, assert staleness and rebake; output must change. Verify missing frozen defaults off and equality notices the flag. Runtime freezing must fail without mutation. Drive Frozen with `--ui-script`, save/reopen, inspect the checkbox and disabled bake/clear/mode/instance controls. Capture the viewport too: frozen chunks must remain visible without live instances, and Show preview must hide them.

## Scene object data iteration regression

**Cause:** a value-only move should compile only the scene-object source when other bakes stay unchanged.
**Fix:** after warming a scratch native game, move an authored object and refresh generation. Compare the header and script timestamps: for a value-only move, `scene_objects.gen.cpp` should be the only compiled source when other bakes stay unchanged. Also test ordinary object additions/removals (which should keep the header stable), scene-count changes, color edits, empty scenes, baked scroller clones, both camera templates and a custom script that reads `SCENE_OBJECT_TABLES`. Check old/new initializer rows for exact equivalence and boot the resulting game. Test culling both enabled and disabled: static-box additions must update its proxy data without changing the declaration header. Feature and other derived-table edits may rebuild more. Profile a remaining large compile with GCC `-ftime-report` before assuming parsing/I/O is its cost.

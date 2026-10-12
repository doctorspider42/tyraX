# troubleshoot/tyra-testing-strips.md

Triangle strips: a host property test, then one knob in PCSX2; and the roads/terrain twins. Symptom first.

## A stripifier renders *almost* right

**Cause:** a stripifier is the kind of code that renders almost right, so check it where checking is free.
**Fix:** `src/meshstrip.cpp` has no GL, no ImGui and no `Project`, so a harness links in seconds:

```bash
g++ -std=gnu++20 -O1 -I src -o stripcheck harness.cpp src/meshstrip.cpp
```

The property that matters is not "it produced a strip" — it is that the strip draws the SAME TRIANGLES. Expand the output back (triangle i of a run is `v[i], v[i+1], v[i+2]`, never across a run boundary), canonicalise each triangle so winding does not matter and drop degenerates, then compare the multisets. Assert the structural invariants in the same pass: every run but the last exactly `kRun` vertices, every run length a multiple of 3, and the result smaller than the list it replaces. Feed it grids in BOTH orientations — a 200-cell row and a 200-cell column behaved completely differently and that is how two real defects were found (seed order, and the three-versus-six seed orientations) — plus a cube, ten cubes, and a pile of triangles sharing nothing, which must be REFUSED.

## An all-eight-floats comparison FAILS on a correct stripper

**Cause:** `meshstrip::Weld::kNoNormal` welds on position and UV, so a strip vertex may legally carry a different normal from the corner a given triangle had.
**Fix:** take the multiset over the attributes the BAG receives, not over all eight floats. A worked instance is `examples/vehicle-playground/authoring/wheel-strip-2026-09-17/stripcheck-wheels.cpp`, which runs the property against the district's three REAL baked wheels:

```bash
g++ -std=gnu++20 -O1 -I src -o stripcheck     examples/vehicle-playground/authoring/wheel-strip-2026-09-17/stripcheck-wheels.cpp     src/meshstrip.cpp
./stripcheck examples/vehicle-playground/.res-baked/vehicles
```

## A hand-written grid strip needs a PICTURE

**Cause:** no count can see its one failure mode: the quad diagonal flips unless each column pair is emitted far-corner-first, which on a terrain-following surface is a different surface — 54 pixels of one 512x512 self-capture.
**Fix:** `meshstrip` output is immune (the multiset test covers it); roads, terrain and the projected-shadow receiver patch are not, so capture a picture.

## The PCSX2 arm is one knob, not two builds of different trees

**Cause:** two builds of different trees differ in more than the strip call.
**Fix:** build two editor binaries from the SAME worktree differing only in whether the bake calls `meshstrip::build`, and run both against one project directory. Freeze the camera from a global script (`TYRA_SCRIPT`, no attachment), set `displayMode: progressive`, and capture with the game's OWN `--capture-frame` rather than a window grab: on the Motor District that gave **three byte-identical captures per arm**. Wheel strips have their own generated-game switch, `TYRA_STRIP_WHEELS` (default 1). Aim a pose at the clip path deliberately — the guard band sends most screen-straddling packages down the cull route, so an ordinary outdoor vantage exercises the strip-to-list expansion ZERO times (`sexp=0` in the `FTCLIP` line); park the camera inside a building and it runs thousands of times. Read `verts` from that line, not the triangle counts.

## Roads and terrain (1.96.0)

**Cause:** roads and terrain are grids, they are stripped too, and neither goes through `meshstrip`, so the harness above does not cover them.
**Fix:** their oracle is `examples/vehicle-playground/authoring/verify-road-twins.py` (Python 3 + g++, no emulator): it compiles the real host tessellator AND the actual generated `buildRoads` body with storage stubs, then checks the strip output vertex for vertex and chunk for chunk between the two, asserts the same run invariants (interior runs exactly `roadgen::kStripRun`, every run a multiple of 3), and compares the expanded triangle SET against the list emitter's. Run it before anything else — it is seconds and it is the only check that sees both twins.

## Road V is rebased per chunk (since 1.117)

**Cause:** to keep physical-GS ST values bounded. The list and strip chunk boundaries can differ.
**Fix:** the oracle compares their V modulo integers while keeping positions, U, fractional V and the host/runtime strip stream exact. Do not replace that with raw V equality: it would reject texture-identical output and tempt the hardware fix back out. A stationary physical-console capture is still the acceptance gate; PCSX2 did not reproduce the stretched-one-texel failure.

## Road HEIGHT queries (1.123, docs/roads.md)

**Cause:** `roadSurfaceAt` is sampled 17 times per blob shadow, per light pool and per glow patch, and answered from a uniform XZ grid built once after the roads. The grid must be a NO-OP.
**Fix:** the check is `TYRA_ROAD_INDEX_VERIFY` in the generated `terrain_game.cpp` — default 0 (it costs more than the work it checks). Flip it to 1 AFTER `--refresh-gen`, build with `native-build` directly, drive, and read `ROADINDEXVERIFY checked N bad M` out of the game's log; `ROADINDEX cells NxN entries M` at scene load says the grid was built. Measured on the district: 140 000 queries, 0 mismatches, day and night.

## Two traps that make a green road-index run worthless

**Cause:** both hide the check behind a trivial agreement.
**Fix:** **An off-road query agrees trivially** — both sides answer "no road" — so a run that never walks onto asphalt proves nothing (the deliberately broken control arm stayed green for 40 000 queries). And **falsify it before believing it**: shifting the query one cell in x must make it report a first mismatch, with the point that produced it.

## The static-batch grouping oracle

**Cause:** `TerrainGame::buildStaticBatchList` is generated code that runs on the EE; `src/staticbatch.cpp` is its host twin.
**Fix:** their oracle is `examples/vehicle-playground/authoring/verify-batch-twins.py` (Python 3 + g++, no emulator, seconds): it lifts `buildStaticBatchList` VERBATIM out of templates.cpp by string index, compiles it beside the twin against small stubs, and diffs the batch assignment member for member over eleven fixtures — texture and cell and lamp and strip-run keys, the singleton drop, the oversized-model guard, batching off, and a deliberate MISSING-TEXTURE case. It asserts the shape of what it LIFTED (the key names must still appear in the body) and it was checked that it can FAIL. The ELIGIBILITY half needs no harness: `batchStatic` in a generated `inc/scene_data.hpp` IS the twin's stage-1 verdict (87 of 142 on examples/vehicle-playground); `--batch-report`'s totals can be read against the game's own `Static batching: eligible 87, solo 22` / `65 objects in 48 batches`.

## Since 1.105 the oracle also prints the road's lateral budget

**Cause:** the three numbers behind it (docs/roads.md).
**Fix:** it prints the **worst surface error** against the dense reference, the **worst UV drift in texels**, and the **worst T-vertex seam**. The seam check is an exact T-VERTEX test (a vertex lying strictly inside another triangle's edge in XZ and off it in Y) because SAMPLING a surface at its own vertices reads barycentric noise (measured 5e-5) larger than the seams worth finding. Its three `heightfield ...` fixtures lay a road over a triangulated 4-unit grid — the district's real ground — and are the only ones where the merge fires at all.

## The PCSX2 knob for these is one expression

**Cause:** `buildRoads` and `buildTerrainChunk` gate the strip on the generated game's `#ifndef` switch `TYRA_STRIP_ROADS` (default 1) beyond `minPackageSize() >= stripRun` (`stripRun = 75u`); the old "flip `minPackageSize() >= 72` in templates.cpp" recipe is stale. Editing the generated `src/terrain_game.cpp` does NOT work (`--build` regenerates it first).
**Fix:** flip the switch with `-D`/native-build or the editor (not the generated `terrain_game.cpp`; `src/game_templates.inc` holds the gated body), build, and **keep a COPY of each binary** (`build-dev\tyrax-editor-strips.exe` / `build-dev\tyrax-editor-lists.exe`), then build one fixture with each. The copies must live INSIDE the checkout (the editor finds `tools/toolchain` and `vendor/tyra` relative to its own executable) and you must confirm the two really differ (`grep -c 'minPackageSize() >= 4096U'` reads 2 and 0) — the binaries come out the same SIZE. Terrain additionally needs a terrain MATERIAL to strip at all (`TERRAINSTRIP ... strips 0` is that case and not a failure).

## The render-cost row `Roads`

**Cause:** it is ready road/junction cull and submission, not generation; roads are built once at scene load.
**Fix:** `Procedural` explicitly skips owner `-3` road chunks and now means only procedural volumes/prefabs. Old CSVs from before 1.117.6 reported both together as `Procedural`, so do not compare those labels directly without summing the new rows.

## Do not use `--build --run` for this

**Cause:** it kills every running PCSX2, and with parallel worktree sessions that is somebody else's game.
**Fix:** launch `pcsx2-qt.exe -elf <project>\bin\vehicle-playground.elf` yourself and talk to it through that project's own `bin/livedbg.cmd` (`--capture-frame <project>`). Run the editor through a normal `--build --run` once beforehand so `HostFs` and `Renderer = 13` are already in `PCSX2.ini`.

## The district's NIGHT poses are not frozen

**Cause:** at night the lamps flicker, so three captures of ONE arm differ from each other at 6 s of settle and still differ at 30 s.
**Fix:** base a pixel A/B on the two DAY poses (byte-identical across three captures in both arms) and quote the night poses' own within-arm spread as the noise floor if you use them at all. Check the repeats PER POSE before reading any difference. For "do both arms draw the same thing", read `ROADSTRIP scene N strips S packages P triangles T` and `TERRAINSTRIP scene N chunk cx,cz ...`: those triangle counts are surface counts with degenerates dropped and MUST match across arms; `FTCLIP`'s cull/clip/guard triangles are GS primitives and legitimately differ by about 1.5x between representations.

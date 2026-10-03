# Big City

A procedurally generated city, about a kilometre across, built to find out how
TyraX and the PS2 behave on a map this large. Open `big-city.tyra` in TyraX and
build the project. You start in the Ravager next to the central plaza; drive
anywhere inside the ring road (Square gets you out).

![Big City running in PCSX2: the Ravager on a downtown avenue](../../docs/img/big-city-pcsx2.png)

## What is in it

Everything is written by [`authoring/generate-city.py`](authoring/generate-city.py)
from one seed. With the shipped knobs:

- **78 roads, 31 km of centre line, 214 nodes**: a perturbed grid of ten avenues
  and streets per axis (4-lane straight avenues, 2-lane gently wobbling streets),
  a closed superellipse **ring road** (6 lanes), a **diagonal boulevard** whose
  every grid node is a six-armed node and which carries a **double tram line**,
  a shorter second diagonal ending in a five-armed node, a sinuous **river
  boulevard** crossing the grid at oblique angles, mid-block **lanes** (T's at
  both ends, or dead ends with one T), four **spurs** that leave the ring and
  split into **Y forks** at the map edge, closed cobble **park paths**, and a
  **double-track railway** arc outside the ring: Spur 2 crosses it at a level
  crossing and Spur 1 flies over it on a **bridge** with piers.
- **Kerbs, pavements, road details** (manholes, gullies, patches, cracks) and
  zebra crossings on the *core* roads (the grid lines nearest the centre, the
  tram diagonal and the downtown lanes); stop/edge-line paint downtown only.
- **1 283 objects**: 623 buildings (lean Kenney-derived shells plus generated
  stacked towers up to 80 m downtown, tapering to lofts and workshops outside),
  71 block plinths, 350 trees (parks, plaza, core avenues), 104 street lamps,
  17 parked cars, 21 benches, the plaza, the player and the car.

All the road features come from [docs/roads.md](../../docs/roads.md). The
buildings, trees and benches are the Motor District's CC0 Kenney assets
([`res/models/urban/LICENSE.txt`](res/models/urban/LICENSE.txt)); the stacked
towers, the 14-triangle trees and lamp and the 36-triangle parked car are
written by the generator. The Ravager is copied from
[vehicle-playground](../vehicle-playground/README.md).

## Regenerating

```sh
python authoring/generate-city.py --editor ../../build/tyrax-editor.exe
tyrax-editor --build examples/big-city
```

`--editor` also makes the 6-lane ring texture with `--road-texture` and runs
`--resave`, so every file is exactly what the editor would write. The script is
deterministic: same seed and knobs, same files. It only rewrites the `main`
scene's objects, its terrain and layer list and a handful of settings (fog,
view distance, batching, lighting); vehicles, input, menus and fonts in the
`.tyra` are left alone. Every knob is documented at the top of the script and
can be overridden for one run, e.g. `--set GRID_LINES=12 --set KERBS=False`.
`road_object()` is the one place a road's fields are written.

## Optimisation, up front

- **Fog and draw distances** are the LOD here. Fog 90 to 220 units; buildings
  drawn to 230 (downtown towers 300, for the skyline), trees 110, lamps 70,
  parked cars 80, benches 55. Kerb, rail and detail chunks keep their own short
  draw distances.
- **Static batching** merges 1 117 of the objects into 149 batches.
- **Terrain streaming**: a 240-unit chunk ring, terrain LOD from 110, and a
  20-unit grid (the city is flat, so more cells would only cost RAM).
- **Cheap props**: every instanced vertex costs EE RAM (below), so the street
  trees and lamps are 14 triangles each instead of the 152/192 of the Kenney
  ones (`LOWPOLY_PROPS`), and street furniture follows the core roads only.
- **Off**: conservative occlusion culling (measured below: 3.6 ms to build the
  visibility buffer, nothing saved, because the occluders, the buildings, are
  themselves never culled) and baked GI/AO/shadows (bake time on 1 300 objects).

## Measured (PCSX2, 2026-10-03)

Debug build, NTSC progressive, frozen walker (`POSE`), HUD numbers after the
scene settled, `--profile-frame` totals over three captures (the profile drains
the pipeline, so its Total is attribution, not frame time).

| pose | HUD FPS | HUD FRAME / SCENE ms | profile Total ms | MEM |
|---|---:|---:|---:|---:|
| car at spawn, downtown avenue | 55.9 | 17.35 / 7.45 | 12.7 | 27.9 / 32 MB |
| downtown, eye 1.8, along the plaza avenue (-42, -100) | 59.9 | 16.69 / 6.65 | 13.9-16.3 | 27.1 |
| midtown (-226, -100) | 59.9 | 16.69 / 6.60 | 12.0-15.5 | 27.1 |
| ring road, city edge (-497, -100) | 59.9 | 16.68 / 3.62 | 7.7-8.4 | 26.4 |
| raised, eye 40 over downtown, 12 degrees down | 59.9 | 16.68 / 3.99 | 8.2-9.2 | 26.3 |

`Roads` is a steady 0.8-1.0 ms of the profile at every pose and `Objects` the
rest (4-11 ms). VRAM 3.2-3.3 / 4 MB. With occlusion culling on, the downtown and
midtown profiles read 15.3-16.3 ms with an `Occlusion` row of 3.4-3.7 ms.

**Boot**: about 15 s from launching PCSX2 to the first frame: 10 s to engine
init (the 6.0 MB ELF over `host:`), then 1.5 s of objects and batching, 1.2 s of
roads and 0.7 s for the road height index. The build: `scene_data.hpp` is a
4.9 MB generated source (the baked road tables); a warm incremental build is
about 70 s.

The acceptance lines from `bin/log.txt`:

```
Static batching: 1117 objects in 149 batches
ROADKERB scene 0 chunks 285 vertices 21597 packages 424 triangles 14212   (kerbs + rails)
ROADBRIDGE scene 0 chunks 8 vertices 726
ROADBRIDGE scene 0 walls 30
ROADDETAIL scene 0 chunks 122 vertices 2460 triangles 820
ROADS scene 0 chunks 1134 vertices 89043
ROADSTRIP scene 0 strips 1 packages 1667 triangles 50186
ROADINDEX cells 128x128 entries 175775
```

## Limits found

**EE RAM is the wall, long before frame time.** Every larger version that was
tried ends in `std::bad_alloc` during scene load: the game hangs on a black
screen after `VEH controls card`, and only the EE console says why (run a PCSX2
of your own with `-datapath <dir> -logfile <dir>\emulog.txt`, so you do not
switch logging on for every other emulator on the machine). The emulator's `MEM`
readout, by configuration:

| configuration | MEM |
|---|---:|
| roads off, 288 objects (128 of them 152-triangle Kenney trees) | 15.3 MB |
| + 962 buildings | about 21 MB |
| roads off, 1 394 trees of 28 triangles | 27.1 MB |
| 1.4 km city (12 grid lines), roads only, no pavements/rails | 24.2 MB |
| 1 km city, all road features, pavements on the core grid lines too | 26.1 MB |
| 1 km city, all road features, no pavements | about 22 MB |
| **shipped: 1 km city, everything** | **27.1-27.9 MB** |
| 1.4 km city, all road features, roads only | out of memory |
| 1.4 km city, 3 562 objects (first version) | out of memory |
| **1.173** (instance sharing, trimmed batches): shipped city, batching on | 27.3 MB |
| 1.173: shipped city, batching off (every model shared) | 25.3 MB |
| 1.173: furniture on every road (`FURNITURE_CORE_ONLY=False`, 2 557 objects), batching on | out of memory (as in 1.172) |
| **1.173: the same 2 557 objects, batching off** | **30.0 MB, runs** |

The 1.173 rows are at the frozen walker (-42, -100) and read from the HUD 60 s
after boot; the 1.172 shipped city reads 29.1 MB there. See
[docs/instance-sharing.md](../../docs/instance-sharing.md) for the
breakdown and the frame-time side (batching off costs 1.6-2.4 ms of HUD SCENE
in PCSX2).

What that works out to:

- **In 1.172 a static object cost roughly 70-110 bytes of EE RAM per vertex
  it draws**, whatever the object was: the 28-triangle tree about 9 KB, a
  22-triangle building about 6 KB. Each instance kept its own expanded vertex,
  colour and UV arrays, and a batch made a merged copy that kept up to as
  much slack again as it held. Instances of one model shared nothing, and
  turning batching off did not rescue a 1 km configuration with 1 809 objects
  either (out of memory both ways). **Since 1.173** batches trim that slack
  (-1.6 MB here) and objects that do not batch draw one shared model-space
  mesh per model, keeping only their colours (pooled: 897 arrays for 1 656
  parts here). Batched, a member still costs 48 bytes a vertex; unbatched and
  shared, about 650 bytes of bookkeeping per part plus its share of the
  colours, and about 1 KB per object whatever it is. That bookkeeping, not the
  triangle count on screen, is now what limits how much city fits.
- **Road surfaces cost about 60 bytes per vertex**, plus the baked tables in the
  ELF (`ROAD_JUNCTION_VERTS`, `ROAD_KERB_VERTS`: 3.3 MB of `.rodata` on the
  first version). Pavements on the core grid lines added about 4 MB on their
  own (the `ROADS` vertex count went from 79 539 to 122 427), which is why the
  shipped city puts them only on the tram diagonal and the downtown lanes.
- **A `.glb` placed as a plain model object loads as an animated DynamicMesh,
  once per instance** (`Frames count should be greater than 1 for DynamicMesh`
  in the log). 143 parked cars using the vehicles' far `.glb` models hung the
  first boot after 77 of them; the parked car is a 36-triangle OBJ now.
- **The road height index held only 1 024 proc chunks** (10 bits of chunk index
  in `buildRoadHeightIndex`). The first version had 1 210 kerb and 1 029 road
  chunks; this city has 1 134 road chunks alone. Every road chunk past the
  1 024th was missing from the index, so wheels and walkers there stood on the
  terrain under the asphalt. Fixed in this change: 13 bits of chunk (8 192) and
  19 of vertex, and anything still out of range is logged (`ROADINDEX skipped`)
  instead of dropped silently. See [docs/roads.md](../../docs/roads.md#kerb-collision).
- **Kerb chunks are small**: at 32-unit cells, kerbs on every street were 1 210
  chunks of about 80 vertices each, and every chunk is a separate submit and
  allocation.
- **Streaming layers did not rescue the 1.4 km city**: with
  `STREAM_LAYERS=True` (200-unit districts) the resident set at the central
  spawn still ran out of memory. Not re-tried on the 1 km city.

## Not verified

- Driving the whole city with the pad, and the frame rate while moving: every
  number above is a parked camera.
- That a car on a road chunk past the old 1 024th now rides on the asphalt: the
  fix is by construction and the log shows nothing skipped, but no capture
  compares the two.
- A physical PS2 (emulator only).

## Files

| File | What it is |
|---|---|
| `authoring/generate-city.py` | The generator and every knob |
| `objects/*.json`, `big-city.tyra` | Its output (plus the vehicle, input and menu setup in the `.tyra`) |
| `res/models/urban/city-*.obj` | Generated towers, trees, lamp and parked car |
| `res/materials/roads/` | Seeded road, pavement and rail textures, plus the generated `road-6lane` |

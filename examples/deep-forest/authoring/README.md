# Broadleaf asset authoring and physical-console density sweep

The three forest oaks use the Tree Generator's Forest oak preset, height 5.5,
and seeds 1234, 2231 and 3228. Each near model has 503 triangles. Their 4-triangle
far models are two perpendicular orthographic captures, not camera billboards.
Each far mesh has one material; the ordinary instanced-model path can draw it.
The leaf source was modelled and rendered by `tools/bake-tree-foliage.py`.

From the repository root, compile the host asset exporter:

```sh
g++ -std=c++20 -O2 -Isrc -Ivendor/stb examples/deep-forest/authoring/export-oaks.cpp src/treegen.cpp src/treeimpostor.cpp -o /tmp/export-oaks
/tmp/export-oaks examples/deep-forest
```

On Windows use MinGW, add `-static`, and choose an `.exe` output in a temporary
directory. The exporter also leaves the original four-view impostor assets
for inspection. These are not the model selected by the instance pool.

## Hardware measurement recipe

1. Copy this example to a short scratch path. For the maximum-density fixture,
   use ScatterSurface density 40 / max 100000, FilterDistance radius 1,
   Limit 20000; keep seed 7, scale, masks and Vary unchanged. Set PAL, keep
   interlaced output, enable the frame profiler, and disable Live Link, Live
   Debugger, Live Logic, Time Machine, Remote Pad, input recording and mouse.
   Set the Player's walk/look speeds to zero. Keep draw distance 210 and the
   authored terrain settings. Build with the editor once to bake ALL assets.
2. Run `python examples/deep-forest/authoring/instrument-density.py SCRATCH`.
   Optional `--counts 6000,7000,8000,9000` narrows the sweep; `--lod 35` changes
   the near/far switch in this measurement only. `--route-radius 140` checks
   deeper parts of the authored forest. Invoke `native-build.ps1`
   (or `.sh`) directly afterward. Before returning to gameplay, restore
   `src/terrain_game.cpp` and regenerate the instrumented renderer source
   (`src/gen/game_collision.gen.cpp` in the current generator):
   the editor may preserve an instrumented user-owned main source on rebuild.
   Give restored sources fresh modification times before rebuilding (a copy
   retaining older times can reuse instrumented object files). Verify the
   playable ELF no longer contains `FOREST_BEGIN` before deploying it.
   For a same-ELF LOD comparison use `--counts 7000,8000 --lods 35,30,25`:
   each distance runs all counts and poses, only oak LOD changes, and rocks
   retain their authored 35-unit switch. Logs/CSVs include the `lod` column.
   Disable the profiler HUD in all arms to match the normal gameplay HUD.
   `--preserve-order` tests prefixes of the authored graph instead of shuffled
   subsets; use it when validating a new default Limit against its actual layout.
   It restores `PROC_INST_ROWS` source order first: the runtime array has already
   been sorted into spatial cells and is NOT in graph order. `--cell 52.5`
   varies spatial-culling granularity while keeping draw distance 210.
3. Deploy the resulting ELF through ps2link, with the absolute `bin/ps2link.run`
   marker and the file server's working directory set to that `bin` folder.
   Save the complete console log and ELF SHA256. Wait for `FOREST_DONE`.
4. `python examples/deep-forest/authoring/summarize-density.py console.log`
   emits `console.csv` and per-density FPS ranges.

Every density uses a nested, deterministically shuffled subset of the SAME
20,000 placements. Cells are rebuilt only between trials; the draw loop does
not pay for filtering 20,000 instances every frame. Three fixed headings
(0/90/180 degrees) at the spawn and a radius-35 walking loop use 300 frames
each, with 61 transition/warm-up frames discarded and 239 measured periods.
The route is driven by frame index and follows terrain height + 1.8.

`fps` is reciprocal mean frame period, `p95` is the 95th-percentile period,
and `misses` counts periods above 25 ms on PAL. `scene` times beginFrame through
scene/HUD work before the debug HUD (not GPU busy time). `submitted`, `near`
and `far` count cell/distance candidates before the pipeline's frustum reject;
they are NOT the number of visible trees. `near` includes rocks; the original
sweeps used the old rock without a far model. `trees` counts oak model paths,
not assets with LODs. The regular `PROFILE` and `INSTANCES` lines remain in the log.

The probe retains a full placement table as well as the current subset and
adds three integer diagnostic counters per candidate. Its memory readout is
therefore not the shipping example's memory footprint. Validate the chosen
authored layout separately: the graph's original prefix is not the sweep's
shuffled subset. Take pictures in a separate Live Debugger-enabled run, and
compare normal profiler-off output as an observer control.

## Same-view oak LOD pictures

Use a separate scratch copy of the maximum-density fixture. Restore clean
sources, enable Live Debugger, run a full editor build, then run
`python examples/deep-forest/authoring/instrument-lod-visual.py SCRATCH` and
native-build. This selects the same deterministically ranked 8,000 placements.
Write four numbers (`LOD X Z HEADING_DEGREES`, e.g. `30 0 0 90`) to
`bin/forest-visual.txt`; the camera follows terrain + 1.8. Only oak LOD changes.
Wait for the `FOREST_VISUAL` acknowledgement before `--capture-frame`.
Use identical coordinates/headings across 35/30/25, and repeat an unchanged
capture to establish pixel noise. Test several headings and near the switch.
The file poller and Live Debugger make this a visual fixture, NOT a timing run.
An optional fifth number selects spatial cell size, e.g. `25 0 0 0 52.5`.
With an authored 8,000-placement fixture all placements are retained; with the
20,000 stress fixture the visual probe selects the ranked 8,000 subset.

## Blender fieldstone

Run `blender -b --factory-startup --python examples/deep-forest/authoring/build-rock.py`
from the repository root (use your local Blender executable). Blender 5.1
produces `rock.obj` (56 triangles), `rock-far.obj` (12), `rock.mtl` and a shared
128×128 `rock.png` in `res/models`. Cycles bakes original procedural mineral
colour and ambient occlusion. No downloaded textures are required. Both meshes
are triangulated, Y-up, and retain the original approximately 1.5-unit footprint.
The graph switches to the far rock at 35 units. `rock.blend` is editable;
`rock-preview.png` is a studio render, not a console screenshot. Both exports
passed the editor importer with zero missing resources and a full native game
build. See the measurement report for which asset revision each run used.

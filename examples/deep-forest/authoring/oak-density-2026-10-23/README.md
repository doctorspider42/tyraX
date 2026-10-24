# Physical PS2 oak density measurements — 2026-10-23

Console transcripts retain their contents with normalized line endings and trailing whitespace.

These are real-console COP0 frame-period measurements, served through ps2link
at 192.168.100.150, not PCSX2 performance estimates. PAL interlaced output,
frame pipeline enabled, tree LOD 35, draw distance 210, terrain detail distance
55. Live devkit services were off. See [the recipe](../README.md) for sampling,
camera paths and observer overhead. FPS is reciprocal mean frame period.

## Density sweep (new oaks, original eight-triangle rock)

Each row combines four camera poses, 239 samples per pose. Counts include rocks.
All rows use nested subsets of the same 20,000 placements and a radius-35 route.

| Total placements | Oaks | FPS range | Worst pose p95 period |
|---:|---:|---:|---:|
| 1,000 | 836 | 49.97–50.00 | 20.21 ms |
| 2,000 | 1,669 | 49.97–50.00 | 20.22 ms |
| 4,000 | 3,335 | 49.97–50.00 | 20.22 ms |
| 6,000 | 5,040 | 49.97–50.00 | 20.23 ms |
| 7,000 | 5,891 | 49.97–50.00 | 20.23 ms |
| 8,000 | 6,741 | 24.99–50.00 | 40.04 ms |
| 9,000 | 7,591 | 24.99–49.97 | 40.04 ms |
| 12,000 | 10,111 | 24.99–49.97 | 40.06 ms |
| 20,000 | 16,840 | 16.66–24.99 | 60.04 ms |

`sweep.txt/.csv`: broad sweep, ELF SHA256
`6B6EC44606CA575A154A73225D646E5D29E92E12B7528ED0E68396C8B4EFDC47`.
`narrow.txt/.csv`: 6,000–9,000 refinement, ELF SHA256
`28A5188DE2C0E5B3E81C8EF4D38C88A09C9B1759140006DD3EDEF889E951F119`.
The profiler HUD was enabled in these sweeps. The 7k/8k boundary describes
these sampled views with the OLD rock, not a universal engine limit.

## Authored layout: 5,061 oaks + 939 rocks

The shipping graph's original 6,000 placements were tested separately, with
the profiler HUD off and a larger radius-140 terrain-following route. The
three static headings plus this route each contain 239 measured periods.

| Rock asset | FPS range | Worst p95 | Periods above 25 ms |
|---|---:|---:|---:|
| Original, 8 triangles | 49.97–50.00 | 20.071 ms | 0 / 956 |
| New Blender rock, 56 near / 12 far | 49.79–50.00 | 20.087 ms | 1 / 956 |

`authored-old-rock.txt/.csv`, ELF SHA256
`A86D7615A786E6764FD3F2B290ED85A616CBFFA0D6BCB5DF65115CAEA834A432`.
`authored-new-rock.txt/.csv`, ELF SHA256
`A465D2936C1F2C394E2F875CED17D9CAE89F799B50BD53BD1AD2BC29C9C2B093`.

The new rock passed the importer (56 / 12 triangles, zero missing resources),
full native asset bake/build and this hardware run. The final conservative
default at this stage was 6,000 total placements. The follow-up below tests
the new rock at higher densities and shorter oak LOD distances.
This is a short repeatable route, not an exhaustive traversal of every clearing.
The benchmark's extra placement copy/counters remain overhead; no network
capture or remote-pad polling ran during timing.

A planned 25-unit LOD diagnostic produced no valid boot or samples after a
console reset and is deliberately excluded. A physical restart restored
ps2link before the successful final new-rock run.

## Follow-up: new rock, 7k/8k, oak LOD 35/30/25

`new-rock-lods.txt/.csv` contains all 24 trials from ONE ELF, SHA256
`B171D2384E90CD7E769B83E389DE6DCE74DFE79401B6165D1C40EBFAF9A307B3`.
Same nested subsets as the broad sweep: 7,000 = 5,891 oaks + 1,109 rocks;
8,000 = 6,741 oaks + 1,259 rocks. All rock instances now use the new 56/12
triangle asset with their switch held at 35. Only the oak switch changes.
The profiler HUD and live services are OFF. Unlike the first broad sweep,
the moving pose follows the deeper radius-140 route; this exposes slow views
that the original radius-35 loop missed. Each arm has three stationary views
and one moving route, 239 measured periods per view.

| Placements | Oak LOD | FPS range across four views | Worst p95 period |
|---:|---:|---:|---:|
| 7,000 | 35 | 46.68–50.00 | 39.98 ms |
| 8,000 | 35 | 24.99–50.00 | 40.04 ms |
| 7,000 | 30 | 49.38–50.00 | 20.30 ms |
| 8,000 | 30 | 46.68–50.00 | 39.98 ms |
| 7,000 | 25 | 49.97–50.00 | 20.29 ms |
| 8,000 | 25 | 49.38–50.00 | 20.28 ms |

### Visual comparison on physical PS2

The separate visual fixture uses the same ranked 8,000 placements. Its Live
Debugger and control-file polling are enabled, so FPS printed IN THE PICTURES
is not a benchmark result. `visual.txt` records switch acknowledgements before
each framebuffer capture. Three views were inspected:

- `lod35/30/25.png`: spawn, heading 90 (a foreground rock partly occludes the view).
- `north35/30/25.png`: spawn, heading 0, clear view of the crowns.
- `deep35/25.png`: x=140, z=0, heading 270, looking through the forest.

Excluding the top 72 HUD rows, repeated `north25-control.png` is pixel-identical
to `north25.png`. Switching 35 to 25 changes 2.70% of pixels facing north,
7.87% facing east and 4.46% in the deep view. These are image differences, NOT
quality scores. The near trunks/crowns retain volume; the distant crown
silhouettes and small gaps change. 25 units looks usable in these views, but
the fixed crossed planes and discrete LOD switch remain visible compromises.
No claim of seamless transitions or exhaustive angle coverage is made.

### Actual 8,000-placement layout

`cell-prefix-lods.txt/.csv`, ELF SHA256
`2F856A79A49B81BA1E5AE4BFF3BBA6BBA350A3B26E541A0932E971EABA430DAE`,
tests the graph's original 8,000 placements (6,742 oaks + 1,258 rocks).
At cell size 105, oak LOD 30 gives 46.86–50.00 FPS, 16 periods above 25 ms;
LOD 25 gives 49.58–50.00 FPS, two periods above 25 ms (both on the moving
route; 956 total samples per arm). Rock LOD remains 35.

The 7,000 rows in THIS file are a prefix of the runtime's CELL-SORTED table,
not the authored graph prefix; they must not be used as validation of Limit
7000. This was detected because spawn candidate counts stayed identical.
`--preserve-order` now restores source order using `PROC_INST_ROWS` before
selecting a prefix. The full 8,000 rows are unaffected by this ordering issue.

### Selected default: 7,000, oak LOD 25, cells 52.5

`cell52.txt/.csv`, same-ELF comparison SHA256
`C039285362A46CD80648D977A2D129E1B4F618F828106FE4B8FB282A0B5C4C88`,
uses restored source order, new rocks and a 52.5-unit spatial cell. No engine
code, draw distance, terrain settings, asset geometry or texture changes.

| Authored placements | Oak LOD | FPS range | Worst p95 | Periods >25 ms / 956 |
|---:|---:|---:|---:|---:|
| 7,000 (5,914 oaks) | 30 | 49.79–50.00 | 20.08 ms | 1 |
| 8,000 (6,742 oaks) | 30 | 46.86–50.00 | 39.99 ms | 16 |
| 7,000 (5,914 oaks) | 25 | 49.97–50.00 | 20.28 ms | 0 |
| 8,000 (6,742 oaks) | 25 | 49.38–50.00 | 20.27 ms | 3 |

The checked-in graph now selects **7,000 total placements, oak LOD 25,
rock LOD 35, cell 52.5, draw distance 210**. After regeneration, all 7,000
placement rows were compared against the measured source prefix; only the
intended oak LOD value differs. The moving view's scene cost is 12.391 ms
mean / 15.570 ms p95. Eight thousand remains an optional stress setting,
not a claim of sustained 50 FPS. These short tests are not a whole-map guarantee.

`cell105.png` versus `cell52.png` compares the SAME authored 8,000 layout,
camera and oak LOD 25 on physical PS2. Below the HUD 705 pixels (0.31%) differ,
with mean absolute RGB channel delta 0.029/255. This is not pixel-identical,
but no obvious loss of silhouette detail was visible in the inspected view.
The repeated `cell105-control.png` is pixel-identical below the HUD.
`cell-visual.txt` confirms both cell switches before capture; visual ELF SHA256
`936205D5F1237EF79650ABA2A780968A3150848E8FA13F369DB6465C7FA21BEF`.
Visual-fixture FPS is excluded from all timing tables.

All timing ELFs above used the existing engine baseline. Concurrent unrelated
engine edits began during the later picture build (a transient warp declaration
compile error); they were not mixed into the benchmark. Final picture/playable
builds used a private snapshot of the already-built baseline engine archive,
SHA256 `101dec38bf65be4a1947e0fd030ee9d9e894f04b49bc9e891b8930c320b0e7cd`.

After pictures, the console was returned to the normal 7,000-placement game
with physical pad control, Live Debugger off and no benchmark/visual probe.
Playable ELF SHA256:
`7A0E77B71128C0EA306BB98B9A6151172975FC1689B55973CD3ED586CD852EBA`.

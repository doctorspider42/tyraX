# Terrain distance detail (LOD)

*Project > Preferences > World > **Detail distance***.

A big map is expensive twice over: it has to **fit** in the PS2's 32 MB, and it
has to be **drawn** every frame. [View distance](terrain.md) answers the first —
only the tiles around the player exist at all. Detail distance answers the
second: the tiles you can see far away are built from fewer heightmap samples.

- Inside the distance: every sample. Full detail.
- Out to 2.2x it: every 2nd sample — **a quarter** of the triangles.
- Beyond: every 4th — **a sixteenth**.

`0` (the default, and what every project did before the setting existed) builds
every tile at full detail.

## What it does not touch

**Gameplay.** Collision, the walkers, physics, the AI's ground height and every
`terrainHeightAt` query read the heightmap directly, never the mesh. A coarse
tile is a coarse *picture*; the ground you stand on is the same ground at any
distance.

**Painting and lighting** keep working — a coarse tile samples the painted
layer weights and the baked shade at its own corners, so both lose detail with
the relief rather than disappearing. The terrain
[lightmap](ambient-occlusion.md) is a texture and is unaffected either way.

## No cracks

Where two tiles at different detail meet, the coarser one has fewer vertices
along the shared edge, and the two surfaces disagree between them — the classic
geomipmap crack, which on the PS2 shows as a hairline of background through the
ground.

The finer tile **interpolates its edge vertices onto the coarser tile's
segment**, so the two meshes agree exactly. It costs no extra geometry (skirts,
the usual cure, add about a quarter as much again to the tiles that can least
afford it), and it works because a tile's detail is a pure function of its
position and the view focus: a tile can work out what its neighbours are doing
without asking whether they exist yet. The vertex *shade* is interpolated the
same way — matching only the height closes the hole and leaves a colour seam
where it was.

## When detail changes

The bands are measured from the player (both players in split screen, whichever
is nearer), snapped to half a tile. Snapping is what keeps this quiet: detail is
a step function, so its input may as well be one, and without it a tile sitting
on a band boundary would rebuild itself for ever.

When a band moves, the tiles it crossed are rebuilt — one per frame, after any
tile that is missing outright, so a hole in the ground is never waiting behind a
cosmetic rebuild. A tile whose *neighbour* changed detail is rebuilt too: that
is the same edge, seen from the other side.

## Choosing a distance

Start at about the range where a person stops reading the ground as ground —
often somewhere near the fog's start. Two sanity checks:

- **Watch the horizon while walking, not while standing.** A band change is
  visible as a shape settling, and it is visible only in motion.
- **Pair it with fog** the way the view distance is paired with it. If the
  fourth-detail band is thickly fogged, nothing about it is legible anyway and
  you can bring the distance in further.

The Preferences line under the slider states the two thresholds in units and how
big one tile is, so a distance can be read against the tile grid it actually
switches on.

## What it costs to build

A rebuild is a real cost on the EE — the same one the streamer already pays per
tile — so detail distance trades a smaller per-frame bill for occasional
rebuild work while the player moves. On a map big enough to want this, that is
the trade you want; on a small map, leave it off.

The peak *memory* is unchanged: a tile's buffers keep their capacity when a slot
is recycled, so LOD buys transform and draw cost, not RAM. Memory is the view
distance's job.

## What it is worth, and what bounds it — measured on a PS2 (1.105)

The Motor District is the worked example, and the two numbers below are the two
halves of the choice. Physical console, `--profile quiet-debug`, four parked
poses, repeatability floor 0.016 ms of frame `work`
([evidence](../examples/vehicle-playground/authoring/road-lod-2026-09-16/README.md)):

| detail distance | garage day | garage night | garage triangles |
| ---: | ---: | ---: | ---: |
| 96 | **−0.437 ms** | −0.530 ms | −2 350 |
| 160 | −0.380 ms | −0.454 ms | −1 102 |

Nothing gets worse in the outer-road poses at either setting, and **the saving
is entirely EE-side**: `bounds` and `prepare` fall while the VU1 wait actually
rises 0.01–0.05 ms. That is the shape to expect — a coarser tile is fewer VU1
packages, and almost every per-frame term of static submission is per-package.

**The constraint is the second band, and it is what picks the distance.** A road
is a decal lifted 0.12 world units above the *dense* heightfield
([roads.md](roads.md)); where a coarse tile rises above it, the ground shows
through the asphalt. Sampled at all 49 149 of the district's seven roads' own
dense sample positions:

| coarsening | worst rise above the road | positions where the ground wins |
| --- | ---: | ---: |
| every 2nd sample | **0.0175** | 22 of 49 149 |
| every 4th sample | **0.3807** | 3 023 of 49 149 |

The first band is safe with a 6.9x margin on the lift. The second buries the
road over about 6% of its area by more than three times the lift. Since the
second band starts at **2.2x** the authored distance, a map with terrain-glued
roads wants a distance large enough that the second band never reaches its far
corner: 320 units of terrain needs about 160, and 96 does not do it.

The transition itself is not the problem, and that is measurable too. The worst
vertical disagreement between the dense mesh and the every-2nd-sample mesh
anywhere on this heightfield is 0.2363 world units, and at 160 units away it
subtends **0.57 pixels** on the PAL 512x448 raster — the settling is sub-pixel
by construction rather than by opinion.

`terrain-lod-burial.py` and `terrain-lod-step.py` in the evidence directory
compute both tables for any project; neither needs a console or an emulator.

## See also

- [The terrain](terrain.md) — view distance, streaming, and building without one.
- [Terrain painting](terrain-painting.md) — the layer weights a coarse tile samples.
- [The flashlight](flashlight.md) — why a finer grid is *not* how you get a
  better-looking torch.
- [Profiling](profiling.md) — how to measure whether any of this helped.

## The Motor District turns it on (1.123.4)

`examples/vehicle-playground` shipped with `terrainLodDistance` at 0 — the
feature off — while its terrain was the frame's largest geometry producer. It
is 70 now, and the value is not a guess: it is the largest band arrangement the
district's own quality oracles accept.

**Why 70 and not more or less.** Two bands exist: every 2nd sample out to 2.2x
the distance, every 4th beyond. Run against the district's real heightfield and
its seven roads' own sample positions
(`authoring/road-lod-2026-09-16/terrain-lod-burial.py`):

| band | worst rise above the dense surface | road lift | verdict |
|---|---:|---:|---|
| every 2nd sample | 0.0175 units | 0.12 | safe |
| every 4th sample | **0.3807 units** | 0.12 | **buries the asphalt** |

So the every-4th band must never engage. It starts at 2.2 x the distance, and
the terrain view distance is 150, so any distance at or above 68.2 keeps it
permanently out of range. 70 does, with room. `terrain-lod-step.py` prices what
is left: the every-2nd band's worst vertical disagreement is 0.2363 units and
at 70 units it subtends **1.3 pixels**.

**What it is worth**, measured in PCSX2 on the example itself (counts are exact
there; milliseconds are not) at a parked street vantage, day, one knob:

| | LOD off | LOD 70 | delta |
|---|---:|---:|---:|
| triangles / frame | 1 308 100 | 1 066 150 | **-18.5%** |
| VU1 packages / frame | 22 550 | 19 550 | **-13.3%** |
| submitted vertices / frame | 24 090 | 21 171 | **-12.1%** |
| packet flushes | 2 950 | 3 000 | +1.7% |

The garage poses gain much more and the outer-road poses almost nothing — on
the outer road the camera sits near the map edge and the visible ground is
inside the full-detail radius anyway. A per-producer inventory of the four
benchmark poses put terrain at 5 416 triangles a frame before and 1 780 after,
a **67% cut of the terrain itself**.

**The picture**, from two `benchmark-district.py` fixtures differing in that
constant alone, three captures each, both arms byte-identical within themselves:
**1 743 pixels of 180 224 (0.97%)** differ, all of them in a 32-row band at the
horizon, mean absolute difference 0.9 of 255. That is the far ground's
silhouette settling by about a pixel, which is what the step oracle predicted.

**A caveat about the inventory percentages.** Those fixtures build from the
example's committed sources without a texture bake, so they render the district
with placeholder boxes instead of its models (docs/performance-hardware-recheck.md,
the full-asset gate). Terrain and road geometry is generated and therefore
correct in them, but the SHARE of the frame each producer holds is not - the
models are missing from the denominator. The per-frame counts quoted above come
from the real example, with its real assets, and those are the ones to quote.

### The hardware number (2026-09-23)

Physical PS2, the same parked street vantage, six `--profile-frame` samples per
arm, paired against the stored baseline. This arm carries the terrain LOD AND
the two frustum rejects of 1.123.5/6, so the two ground rows are their joint
result:

| phase | day before | day after | night before | night after |
|---|---:|---:|---:|---:|
| `Terrain` | 2.909 | **2.650** | 2.916 | **2.675** |
| `Roads` | 3.407 | **3.182** | 3.458 | **3.294** |
| renderScene `Total` | 17.341 | **16.938** | 20.819 | **19.908** |

That is -0.40 ms of a day frame and -0.91 ms of a night one. The street vantage
is the WEAK case for the LOD by construction - the garage poses cut 40% of the
frame's triangles against this one's 18.5% - so read it as a floor.


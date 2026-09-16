# Raw evidence: the triangle budget, on hardware

Measured on a **physical PS2** (192.168.100.150, ps2link), 2026-09-16, against
[docs/ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md),
"Order of work" item 5 — the triangle budget, promoted out of last place because
the EE rearchitecture plus the cheap wins lands near 21 ms against a 20 ms rung.

Three attacks were named. All three were measured. **One of them ships, one is a
quality-bounded recommendation, one was refuted a second time, and the largest
number of the round belongs to a fourth thing nobody ranked first.**

Garage day, frame `work` (update + submit + finish), against a **repeatability
floor of 0.016 ms** from two boots of each ELF:

| attack | garage day | garage night | outer day | outer night | verdict |
| --- | ---: | ---: | ---: | ---: | --- |
| road lateral budget | **−0.358** | −0.367 | **−0.591** | −0.547 | **ships** |
| + terrain LOD 160 | **−0.738** | −0.821 | −0.607 | −0.609 | recommended, one check outstanding |
| + mesh LOD 64 | −0.166 | −0.235 | −0.444 | −0.362 | **refuted again** — it COSTS 0.19 |
| + reflection probe every 4th frame | **−1.391** | −1.645 | −1.133 | −1.087 | a bounding probe, not a candidate |

## The finding that reorders the plan

**The garage barely contains any road.** The lateral budget removes 31.6% of the
district's road triangles — 31 050 to 21 252 map-wide — and the garage-day frame
loses **614** of them, 1.5% of its 40 961. The outer-road poses lose 2 817, 17.6%.

The plan promoted the triangle budget on the strength of "the road surface alone
is 31 050 triangles in the map". That is true, and it is not the garage's
problem. Whoever picks this up next should aim at what the garage actually
draws, and this round says what that is: the shared reflection probe is worth
**2.07 ms of garage day and 2.56 ms of garage night**, twice what the road and
the terrain LOD together are worth there.

## Method

- **Both editors built from the measuring worktree.** The arms here are two
  EDITOR binaries, not two engine builds: the change is in codegen
  (`src/roadgen.cpp` and its `buildRoads` twin). An editor sitting in `build/`
  is not "the editor at the tree's commit", which is what produced round one's
  72-run control ([ee-probes-2026-09-16](../ee-probes-2026-09-16/README.md),
  "Fixture identity"). `build-arm.ps1` takes the editor as a parameter and
  records its sha256 in `ARM.json`.
- **Fixture identity, archived**: `console/fixture-identity.txt`. `stripRun` is
  **75u** in the generated source of every arm, matching the branch tip.
  `ROADSTRIP` legitimately MOVES between the arms and is quoted before and
  after, from the host oracle below rather than from a capture hash — a capture
  hash is a picture check and never a fixture check.
- **`--profile quiet-debug`**, because the live tools' `host:` pollers cost
  6.44 ms of `work` and produce `update` outliers no arm's code can explain.
- Fresh boot over ps2link, **120 warm-up then 240 recorded rows** in each of
  four parked poses (960 rows, all collected in every run), PAL 512x512 native,
  parked traffic, `reuploads` 0.000 in all four poses of all nine runs.
- **Every ELF hashed and distinct** (`console/*/run.json`), control alternated
  with candidate, and both the control and the road candidate booted twice.
- The baked asset tree (`.res-baked`, `bin/aoatlas`, `bin/aomap`) is baked once
  and copied into every arm, so the only thing that differs is generated code.

### The repeatability floor

Two boots of one ELF, per-pose means over 240 rows (`console/summary-repeatability.txt`):

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work`, control | −0.001 | +0.001 | −0.008 | +0.006 |
| `work`, candidate | +0.016 | −0.013 | −0.009 | +0.009 |
| `triangles`, `flushes` | identical | identical | identical | identical |

**Read every delta against 0.016 ms.** That is eight times tighter than round
one's 0.135, because these arms run `quiet-debug` and round one's ran `debug`.

## Attack 1 — the road lateral budget. SHIPS.

The reduction and its two budgets are in [docs/roads.md](../../../../docs/roads.md),
"The lateral budget". The short version: it was all-or-nothing — one full-width
quad or all 26 lateral cells — and a road is a decal sampled eight times more
finely across than the 4-unit terrain cell it is projected onto, so the full
width is almost never one plane and nothing ever merged. The surface budget
stays at the float noise floor and the UV budget was relaxed to 0.05.

Map-wide, from the host oracle (`road-budget-sweep.py`, which reproduces the
console's own producer line exactly at the reference budget):

| | branch tip | shipped | |
| --- | ---: | ---: | --- |
| `ROADSTRIP … triangles` | 31 050 | **21 252** | −31.6% |
| `ROADSTRIP … packages` | 470 | **337** | −28.3% |
| strip vertices | 34 485 | 24 417 | −29.2% |
| worst surface error | — | **0** | exact |
| worst T-vertex seam | — | **0** | exact |
| worst UV drift | — | 0.36 texel | on the 128-pixel road texture |

On the console, against the control:

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **−0.358** | −0.367 | **−0.591** | −0.547 |
| `submit` ms | −0.174 | −0.200 | −0.438 | −0.373 |
| `vif_wait` ms | −0.055 | −0.104 | −0.274 | −0.286 |
| `triangles` | −614 | −614 | −2 817 | −2 817 |
| `flushes` | −1 | −1 | −2 | −2 |

### Checking the conversion against the hardware, which is the point

[docs/vu1-and-dma-cache-cost.md](../../../../docs/vu1-and-dma-cache-cost.md)
prices a colour-program triangle at 73 VU1 cycles unlit and 133 lit. The removed
triangles therefore predict, at 294.912 MHz:

| | triangles removed | predicted VU1, unlit | predicted VU1, lit | measured `vif_wait` |
| --- | ---: | ---: | ---: | ---: |
| garage day | 614 | 0.152 ms | 0.277 ms | **−0.055** |
| outer day | 2 817 | 0.697 ms | 1.273 ms | **−0.274** |

**36–39% of the unlit prediction arrives.** That is not a failure of the model;
it is the asymmetry the same page already measured and explained — "removing
arithmetic only helps a packet until VU1 drops below the EE's preparation, after
which further cycles are free and invisible", where removing 60 cycles bought
**41%** of theory. Two independent reductions now land on the same fraction, so
the rule to carry forward is that a triangle REMOVED from this frame is worth
about 0.4 of its cycle count, while a triangle ADDED costs about 0.9 of it
(the +33% payload probe measured 87%).

The rest of the frame saving is EE-side: garage day `dispatch` −0.197 and
`bounds` −0.067 against `vif_wait` −0.055.

### One measured effect with no attribution

`update` falls **0.178 ms in every pose**, in every arm built with the candidate
editor, against a 0.003 ms floor. It is reproducible and it is not noise, and
there is no per-frame road code — `buildRoads` runs once at scene load.

Two candidate mechanisms, neither tested. The road chunk count moves 61 → 59, so
something in `update` may walk `procChunks`. Or it is **layout**: the road
arrays shrink by 10 068 vertices at scene load, so every allocation after them
moves, and this repository has already been bitten by exactly that — quantizing
the car body measured 0.51–0.74 ms SLOWER on every pose with two of the arms
byte-identical, and the surviving explanation was that the texture shrank and
every later VRAM allocation moved
([vu1-and-dma-cache-cost.md](../../../../docs/vu1-and-dma-cache-cost.md)). If
that is what this is, it is a windfall rather than a saving, and it would not
survive an unrelated change to the same scene. It is counted in the `work`
deltas above because it is real and measured, and flagged here because it is not
understood; **the `submit` column is the one to quote for the road reduction
itself** (−0.174 / −0.200 / −0.438 / −0.373).

## Attack 2 — the authored LOD distances. SPLIT.

The earlier trials were rejected because model 64 and model 64 / terrain 96 both
repeated 25 / 25 / 50 / 50 displayed FPS. That measurement could not have shown
anything: the frame was pinned to a vsync rung. Re-run against `work_ms`, the
two halves of that pair turn out to have **opposite signs**.

### Mesh LOD 64 is refuted a second time, and now for a reason

| vs the road candidate | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **+0.192** | +0.131 | +0.146 | +0.185 |
| `dispatch` ms | −0.316 | −0.279 | −0.101 | −0.079 |
| `vif_wait` ms | −0.311 | −0.343 | −0.076 | −0.058 |
| `triangles` | −592 | −592 | −592 | −592 |

It removes 592 triangles and takes 0.32 ms out of `dispatch` and 0.31 ms out of
the VU1 wait — and `work` goes **up**, because `submit` outside `StaPipCore::
render` rises more than that. The per-object tier selection costs more than the
geometry it saves at this object count. `prepare` +0.269 and `packet` +0.153 in
garage day say where. **This is no longer a null result; it is a negative one.**

### Terrain LOD is the useful half, and its ceiling is a quality constraint

Terrain LOD 96 alone, against the road candidate: garage day **−0.437**, garage
night **−0.530**, outer day −0.024, outer night −0.073, for −2 350 garage
triangles. Nothing gets worse anywhere. The saving is entirely EE-side
(`bounds`, `prepare`), and `vif_wait` actually rises 0.01–0.05 ms.

But 96 is not safe, and `terrain-lod-burial.py` says so in world units. The road
is a decal lifted **0.12** above the DENSE heightfield; a coarse tile that rises
above it shows grass through the asphalt. Sampled at all 49 149 of the seven
roads' own dense sample positions:

| coarsening | worst rise above the road | positions where the ground wins |
| --- | ---: | ---: |
| every 2nd sample | **0.0175** | 22 of 49 149 |
| every 4th sample | **0.3807** | 3 023 of 49 149 |

The first band is safe with a **6.9x margin** on the lift. The second buries the
road over about 6% of its area, by more than three times the lift. Detail
distance switches to every 4th sample beyond **2.2x** the authored distance, so
the setting has to keep the whole map inside the first band: at 96 the second
band starts at 211 units and the district's far corners are past it; at **160**
it starts at 352 and nothing in a 320-unit map reaches it.

Terrain LOD **160** keeps 87% of the win and is inside the constraint:

| vs the road candidate | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **−0.380** | −0.454 | −0.017 | −0.062 |
| `triangles` | −1 102 | −1 102 | 0 | 0 |

`terrain-lod-step.py` answers the other acceptance criterion — "no distracting
LOD transitions" — with a number rather than a screenshot. The worst vertical
disagreement between the dense mesh and the every-2nd-sample mesh anywhere on
this heightfield is 0.2363 world units, and the band where it can appear starts
at 160 units, so the settling subtends **0.57 pixels** on the PAL 512x448
raster. Collision is unaffected by construction: every `terrainHeightAt` query
reads the heightmap, never the mesh ([docs/terrain-lod.md](../../../../docs/terrain-lod.md)).

**What is still outstanding, and why the setting is not authored into the
example yet.** A parked fixture cannot see the one cost terrain LOD actually
has: the tile REBUILD when a band moves with the player. That is bounded by
design — one tile per frame, bands snapped to half a tile — but it is not
measured here, and "reject a setting that only improves the parked-camera
benchmark" is the plan's own rule. One drive across the 160-unit band in both
directions is what it needs.

## Attack 3 — the shared reflection probe. THE BIGGEST NUMBER OF THE ROUND.

`build-probe-cadence.ps1` is a **bounding probe, not a candidate**: it refreshes
the shared 128x128 target every fourth frame instead of every second, by forcing
the constant of the cadence that is already adaptive. Nothing else is touched —
in particular the retained CAPTURE BASIS is untouched, because Task 5 of the
Motor District plan is correctness work and a cadence experiment must not undo
it.

| vs the road candidate | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **−1.033** | **−1.278** | −0.543 | −0.539 |
| `dispatch` ms | −0.667 | −0.777 | −0.252 | −0.231 |
| `vif_wait` ms | −0.266 | −0.276 | −0.051 | −0.031 |
| `triangles` | −2 643 | −2 676 | −892 | −892 |
| `flushes` | −6.5 | −9.25 | −3 | −3 |

Halving the cadence removes half the probe, so **the whole shared probe costs
about 2.07 ms of garage day and 2.56 ms of garage night**. The plan's S4
predicted "roughly 2 ms averaged" from counts alone; that prediction is now
measured and it was right.

This is not a recommendation to ship the longer cadence — a 12.5 Hz reflection
on a car the player is looking at is a visible thing, and nobody has looked.
What it establishes is that **S4 is worth more in the garage than attacks 1 and
2 together**, and that its three options (fewer objects, a coarser LOD for the
probe pass, a longer cadence) are worth designing properly rather than
sequencing behind the triangle budget.

## Reproducing

```powershell
# the two editors, from the worktree under test
./build.ps1;  cp build/tyrax-editor.exe <tmp>/cand.exe
git checkout HEAD~1 -- src/roadgen.cpp src/roadgen.hpp src/templates.cpp
./build.ps1;  cp build/tyrax-editor.exe <tmp>/ctl.exe
git checkout HEAD -- src/roadgen.cpp src/roadgen.hpp src/templates.cpp

# the baked asset tree, once, then restore the example's generated sources
<tmp>/cand.exe --build examples/vehicle-playground
git checkout -- examples/vehicle-playground/inc examples/vehicle-playground/res examples/vehicle-playground/src

# one arm, then one physical run
./build-arm.ps1 -Arm cand -Editor <tmp>/cand.exe
./build-arm.ps1 -Arm lod-t160 -TerrainLod 160 -Editor <tmp>/cand.exe
./build-probe-cadence.ps1
./run-console-arm.ps1 -Arm cand -Label boot1
python summarize_cost.py ctl=<results>/ctl-boot1 cand=<results>/cand-boot1
```

Counts and error budgets need no console and no emulator:

```
python road-budget-sweep.py --reference HEAD~1 0.00001,0.00001 0.00001,0.05
python terrain-lod-burial.py ../..
python terrain-lod-step.py ../.. 160
```

**THE CONSOLE IS A SHARED RESOURCE.** A listening tcp/18193 does not mean free —
check `Get-CimInstance Win32_Process -Filter "name like '%ps2%'"` for a live
`ps2client` command line before resetting anything.

## What this does not establish

The traffic is **parked**, so nothing here prices a change that skips work when
an input did not change. The camera is **parked** too, in four poses, which is
what leaves the terrain LOD rebuild cost unmeasured and the LOD transitions
unwitnessed. No picture comparison was run: the road reduction's surface is
exact by construction and checked by the oracle, but that is an argument about
geometry and not a screenshot, and the terrain LOD and probe cadence arms
deliberately change what is drawn. Linux was not involved. The `kSpanFlatness`
half of the road budget — 7 428 triangles for a 0.028-unit seam — is priced in
docs/roads.md and was not taken.

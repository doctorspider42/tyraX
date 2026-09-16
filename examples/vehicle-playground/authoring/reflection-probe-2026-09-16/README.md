# Raw evidence: what the garage-day frame is MADE OF

Measured in **PCSX2** (software renderer, PAL 512x512 native), 2026-09-16,
against [docs/ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md),
"Order of work" item 3 — S4, the shared reflection probe.

**Nobody had ever inventoried this frame.** The plan promoted a whole front on
a map-wide road count and the garage turned out to contain 614 of those
triangles, 1.5% of its 40 961. That mistake was only possible while the frame's
triangles had never been broken down by what produces them. This round breaks
them down.

Everything here is a **COUNT**. PCSX2 emulates no EE data cache, so its
milliseconds are not admissible; its counters are exact and they are what this
round quotes. No console was used and none was needed.

## The instrument

`authoring/inventory-frame.py`, applied to a `benchmark-district.py` fixture
INSTEAD of `instrument-frame-cost.py`. `StaPipCore::takeTelemetry()` clears as
it reads, so a drain at every producer boundary is an exclusive split of the
frame — **no engine counters were added and no engine file was touched.** Bags
are counted game-side (every `stapip.core.render()` call), which needs no
`TYRA_STAPIP_ATTRIB`. Every bracket opens with a drain into `rest`, so
unbracketed work cannot leak into the producer that follows it.

The triangle column is `trianglesCull + trianglesClip`: the primitives the GS
is asked to rasterise, degenerate strip joins and run padding included. That is
the same quantity `frame-cost.csv` reports as `triangles`, which is what lets
the rows be checked against the published four-pose table.

## Fixture identity, and the check that the instrument is exact

`pcsx2/inventory/log.txt`, `pcsx2/inventory/ARM.json`:

| | |
| --- | ---: |
| editor sha256 (built from this worktree) | `bd73d0f723f7c3dbfc4327fc396ca6b9408b7cfd4630c61bcc28a29528b8f45f` |
| `stripRun` in the generated source | **75** |
| `ROADSTRIP scene 0 … packages` | **337** |
| `ROADSTRIP scene 0 … triangles` | **21 252** |
| `TERRAINSTRIP … vertices / packages` | **588 / 8** |
| `Static batching` | 65 objects in 48 batches, eligible 87, solo 22 |

Those are the branch tip's numbers with the road lateral budget shipped, not
the 470/31 050 the plan's older tables quote. **A matching capture hash would
have proved none of this** — the strip run changes how a surface is cut into
runs, not which pixels it covers.

**And the rows add up to the published frame, in all four poses.** The
four-pose hardware table in the plan predates the road reduction; subtract the
road round's measured −614 / −614 / −2 817 / −2 817 and it must equal what this
instrument totals:

| pose | plan's table | − road round | this inventory | diff |
| --- | ---: | ---: | ---: | ---: |
| garage day | 40 961 | 40 347 | **40 347** | 0 |
| garage night | 41 629 | 41 015 | **41 015** | 0 |
| outer day | 16 053 | 13 236 | **13 236.5** | 0 |
| outer night | 16 385 | 13 568 | **13 568.5** | 0 |

Exact in every pose. The halves are the probe's every-second-frame cadence
averaged over 240 frames.

## The garage-day frame, by producer

240 recorded frames after 120 of warm-up, parked pose, parked traffic.
`tri/frame` is the per-frame average; `tri/hit` divides by the frames the
producer actually ran, which is what the every-second-frame probe needs.

| producer | tri/frame | % | pkg | bags | flush | pkgOut | tri/pkg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `object_submit` (solo objects) | **18 087** | **44.8%** | 354.0 | 62.0 | 38.0 | 121.0 | 51.1 |
| `terrain` | 5 416 | 13.4% | 76.0 | 25.0 | 12.0 | 124.0 | 71.3 |
| **`env_probe_objs`** | **5 206.5** | **12.9%** | 78.0 | 17.5 | 12.0 | 50.5 | 66.7 |
| `static_batches` | 5 159 | 12.8% | 89.0 | 45.0 | 18.0 | 185.0 | 58.0 |
| `roads` | 3 276 | 8.1% | 48.0 | 59.0 | 22.0 | 289.0 | 68.3 |
| `proj_shadows` | 1 544 | 3.8% | 69.0 | 6.0 | 7.0 | 0.0 | **22.4** |
| `wheels` | 1 506 | 3.7% | 61.0 | 3.0 | 6.0 | 18.0 | **24.7** |
| `env_probe_sky` | 79.5 | 0.2% | 14.5 | 1.0 | 1.0 | 2.0 | 5.5 |
| `sky` | 67 | 0.2% | 12.0 | 2.0 | 1.0 | 6.0 | 5.6 |
| `particles` | 4 | 0.0% | 1.0 | 1.0 | 1.0 | 0.0 | |
| `blob_shadows` | 2 | 0.0% | 1.0 | 3.0 | 1.0 | 2.0 | |
| **TOTAL** | **40 347** | | **803.5** | **224.5** | **119.0** | **797.5** | |

Light pools, light beams, shadow decals, mirrors, portal surfaces, animated
models, procedural chunks other than roads, the camera feed and the highlight
pass all submit **nothing at all** in this pose. Garage NIGHT differs by 668
triangles in total: `sky` rises 67 → 387 (the star field), `light_pools` adds
158 and `light_beams` 30, and the probe carries the eleven lit window boxes.

Five things in that table are worth more than the ranking:

- **The garage is a pile of solo static models, and three of them are a
  quarter of the frame.** Grouped by model, `object_submit` is three
  `district-tower` instances at **10 491 triangles (26.0% of the whole frame)**,
  two `district-loft` at 2 748, the three car bodies at 3 446 and five
  `detail-light-double` at 798. Nothing else reaches 600.
- **The shared reflection probe is 13.1% of garage day and 13.5% of outer
  day** — it is very nearly pose-independent, because it is a 110-degree
  LEVEL-FORWARD view of the same buildings whatever the camera is doing. In the
  outer poses it is the same absolute cost against a frame a third the size.
- **The road front's true shape, in one row.** Roads are **8.1%** of garage day
  and **47.4%** of outer day. The previous round measured that as a delta; this
  says it as an inventory.
- **`proj_shadows` and `wheels` are the frame's worst packing.** 130 packages
  for 3 050 triangles — 16% of the frame's VU1 packages for 7.5% of its
  triangles — because they are triangle LISTS (75/3 = 25 per package) where
  terrain, roads and the probe are strips (up to 73). The EE pays per package
  ([docs/ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md)),
  so those two are worth about twice their triangle share.
- **Half the classified packages are thrown away**: 797.5 rejected against
  803.5 drawn. `roads` alone offers 289 and draws 48; `static_batches` offers
  185 and draws 89. Probe A already measured that rejection pays for itself
  (2.60 ms bought for a 1.79 ms test), so this is not a defect — but it is the
  population a world-visibility scheme (plan item 5) would work on, and the
  garage is where it lives.

## The outer-road poses, for contrast

| producer | outer day tri/frame | % |
| --- | ---: | ---: |
| `roads` | 6 271 | 47.4% |
| `terrain` | 2 848 | 21.5% |
| `static_batches` | 1 776 | 13.4% |
| `env_probe_objs` | 1 709.5 | 12.9% |
| `object_submit` | 502 | 3.8% |
| `env_probe_sky` | 75 | 0.6% |
| `sky` | 55 | 0.4% |
| **TOTAL** | **13 236.5** | |

Two poses, two completely different frames. Any future front should say which
one it is aimed at before it is ranked.

## What the probe pass actually draws

The rows that decide S4. `env_probe_objs` runs on every SECOND frame, so the
per-hit column is the one to design against.

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| triangles per HIT (objects) | 10 413 | 10 545 | 3 419 | 3 419 |
| triangles per HIT (sky + discs) | 159 | 159 | 150 | 150 |
| **probe triangles per hit** | **10 572** | **10 704** | **3 569** | **3 569** |
| packages per hit | 185 | 196 | 80 | 80 |
| packet flushes per hit | 26 | 37 | 12 | 12 |
| bags per hit | 37 | 48 | 37 | 48 |

**26 flushes and ~10 400 triangles on every second frame is exactly what S4
predicted from counts** (`docs/ee-submission-rearchitecture.md`, supporting
change S4: "+26 flushes and +10 444 triangles on every second frame"). The
prediction is now an attributed measurement.

Per reflected object, garage day (`summary.txt`, pose 0):

| object | model | tri/HIT | pkg/hit | bags/hit |
| --- | --- | ---: | ---: | ---: |
| 34 | `district-tower` | 3 497 | 50 | 5 |
| 36 | `district-tower` | 3 497 | 50 | 5 |
| 38 | `district-loft` | 2 237 | 32 | 5 |
| 30 | `district-workshop` | 1 182 | 24 | 5 |
| 32 | `district-workshop` | **0** | 0 | 5 |
| 50 | `district-loft` | **0** | 0 | 5 |
| 52 | `district-tower` | **0** | 0 | 5 |

Eighteen objects carry `reflected` in this scene; eleven of them are the
night-time window and trim boxes, which are invisible in the day pose and add
132 triangles at night. **Seven are submitted, four draw anything, and three
draw nothing at all while still costing 15 bags and about 100 classified-and-
rejected packages a hit** — because the probe's object loop has **no frustum
test, no draw-distance test and no screen-size test of any kind**. The only
thing that rejects anything there is the engine's own per-bag classify, which
runs after the EE has already paid `bounds` and `prepare` for the bag.

And the geometry it does draw is the **full main-view mesh**: object 34 submits
3 497 triangles into the main frame and the same 3 497 into a **128x128**
target. Object 38 submits 511 into the main frame and **2 237** into the probe,
because the probe's 110-degree level-forward view sees far more of the district
than the chase camera does.

## Reproducing

```powershell
# the editor, from the worktree under test (NOT the one sitting in build/)
./build.ps1

# the example's baked asset tree, once
build/tyrax-editor.exe --build examples/vehicle-playground
git checkout -- examples/vehicle-playground/inc examples/vehicle-playground/res `
                examples/vehicle-playground/src

# the fixture, instrumented and compiled with the native toolchain directly
./build-inventory.ps1 -Editor <abs path>/build/tyrax-editor.exe
./run-pcsx2-arm.ps1 -Fixture D:/tyra-probe-0916/arms/inventory `
                    -Out D:/tyra-probe-0916/results/inventory

python ../summarize_inventory.py D:/tyra-probe-0916/arms/inventory --phase 0
```

**THE EMULATOR AND THE CONSOLE ARE SHARED RESOURCES.** `run-pcsx2-arm.ps1`
launches PCSX2 itself on this arm's ELF and stops only that process; never
`--build --run`, which reaps other worktrees' emulators. No console was touched
by this round at all.

## What this does not establish

Not one millisecond. The instrument drains telemetry dozens of times a frame,
so its own frame times are meaningless, and PCSX2's would be inadmissible
anyway. The traffic and the camera are both **parked**, so nothing here prices
a producer whose cost depends on motion — the terrain chunk rebuild and the
wheel rebake both look cheaper here than they are while driving. The four poses
are the fixture's four; a fifth vantage would have a fifth inventory.

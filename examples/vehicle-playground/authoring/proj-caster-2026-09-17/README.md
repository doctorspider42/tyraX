# The projected silhouette stops paying for attributes it does not read

Measured in **PCSX2** (software renderer, PAL 512x512 native), 2026-09-17.
**Counts only — no console this round**, which is the right instrument for this
question: PCSX2's counters are exact even though its milliseconds are not, and
every number on this page is a package, a vertex, a bag or a pixel.

Commit measured from: **`133d2bce`** (the merge of this work into `vehicles`
tip `0d90799c`, which contains `ae616f8e`). Every arm re-built and re-run at
that commit after the `BagArray` / `contentVersion` contract landed underneath
this change; the earlier arms taken at `2c6a12a8` are kept only where they
document a trap, and each `ARM.json` records its own commit.

## The lever

From the garage-day inventory in
[wheel-strip-2026-09-17](../wheel-strip-2026-09-17/README.md): **87% of the
`proj_shadows` bracket is the caster's own model bags re-submitted from the
light** — 60 of 69 packages, 4 440 of 4 632 vertices, in 4 bags, decomposing
exactly as the CC96 body's three parts plus the ggbot body's one.

The round was set up to ask whether the `.tmdl` v3 **shadow proxy** could be
submitted instead. **It cannot, for three independent reasons**, each sufficient
on its own, and that refusal is recorded here because it is cheap to re-derive
wrongly:

1. The vehicle bodies are baked by `src/vehbake.cpp`, a separate path from
   `bakeStaticModels`. It writes its own `veh-<id>-body.tmdl` and **never calls
   `meshlod::generateShadowProxy`**.
2. Proxy generation is gated on `ProjectSettings::flashShadowVolumes`, which
   defaults to false and is absent from `vehicle-playground.tyra`.
3. Even with 1 and 2 fixed the size gate is `tris > meshlod::kShadowProxyMaxTris`
   = **1200**, and the CC96 body is **1 116** triangles, the ggbot body **364**.
   CC96's authored `bodyTris` budget is *also* 1200, so `vehbake` decimates the
   body to exactly the ceiling below which the proxy never generates. Any
   vehicle with `bodyTriBudget <= 1200` can never qualify.

**The vertices were never the lever. How they are packaged is.**

## What shipped

`TYRA_CHEAP_PROJ_CASTER` (generated, **default 0**). The silhouette is submitted
through a bag that shares the part's own vertices but carries **no texture bag
and one colour**, so it goes through the colour VU1 class instead of the
texture+colour one. `getMaxVertCount` is
`(dbuffer - 9) / (colorElementsPerVertex + reglistCount)` rounded down to a
multiple of 3; the double buffer is `(944 - 22) / 2 = 461`, `cull_c` is built
`elementsPerVertex 2, reglistCount 2`:

| silhouette bag | verts per package |
| --- | ---: |
| textured + per-vertex colour (what the pass submitted) | 75 |
| untextured + per-vertex colour | 111 |
| untextured + **single** colour | **150** |

No geometry change, no second vertex array, no bake and no format change.

## PACKAGES AND VERTICES, per frame, all four poses

`pcsx2/control` vs `pcsx2/cheap`, parked. Packages lead the table because the
console has priced a VU1 package at ~19.5 us in garage day and 31.8 at night,
and the triangle column can point the wrong way.

| pose | silhouette pkgs ctl -> cheap | delta | vertices | bags |
| --- | ---: | ---: | ---: | ---: |
| 0 garage day | 60 -> **32** | **-28** | 4 440 -> 4 440 (0) | 4 -> 4 (0) |
| 1 garage night | 60 -> **32** | **-28** | 4 440 -> 4 440 (0) | 4 -> 4 (0) |
| 2 outer day | 0 -> 0 | 0 | 0 | 0 |
| 3 outer night | 0 -> 0 | 0 | 0 | 0 |

The whole `proj_shadows` bracket moves 62 -> 34 in both garage poses; the
receiver patch (2 packages, 96 vertices) is untouched. **Outer day and outer
night hold no projected caster at all** — they are structurally zero in both
arms, not an unmeasured pose.

### Driving

`pcsx2/mv-control` vs `pcsx2/mv-cheap`. The shipped district contains **no
moving projected caster**: its only two `shadowMode 3` objects are parked, and
its only two routed vehicles are `shadowMode 2` (blob). `--keep-routes` alone
therefore changes nothing — `pcsx2/drive-*` is byte-identical to the parked run,
which is how this was found. `-MovingCaster` promotes the two routed rivals to
projected casters and aims the pose at the circuit they drive.

| garage day, caster DRIVING | ctl | cheap | delta |
| --- | ---: | ---: | ---: |
| silhouette packages | 1.8 | 1.0 | **-0.9** |
| silhouette **vertices** | 132.7 | 132.7 | **0.0** |
| silhouette bags | 0.1 | 0.1 | 0.0 |

Averaged over 240 frames of real motion, so the casters are in the cone about 3%
of the time and the absolute numbers are small. The load-bearing column is the
middle one: **the vertices match to the digit**, so the bag followed a moving,
turning caster's geometry exactly. A silhouette that lagged or went stale in
motion could not produce that.

## THE PICTURE, and the gate that had to be built first

**The shipped garage pose cannot see this shadow at all.** A known-bad arm that
removes the caster submit entirely (`-NoSilhouette`) returns a capture that is
**byte-identical** to the control, under the stock camera (`pcsx2/picture-*`)
and again under a camera aimed at the ground (`pcsx2/sc-*`). `PROJDBG`
(`pcsx2/dbg`) shows why it is not a fade problem: both slots are held at
`fade 1 reach 1` with the sun. The receiver patch is depth-tested and lands
under the road the casters are parked on. See docs/backlog.md.

So the picture gate is read from a fixture where the shadow IS visible:
`-PatchLift 1.2` lifts the patch clear of the road in **all three arms equally**.

| comparison | pixels differing | max channel delta | mean delta |
| --- | ---: | ---: | ---: |
| **known-bad**: control vs silhouette removed | 5 141 (1.96%) | **113** | 39.54 |
| **the arm**: control vs cheap | 1 708 (0.65%) | **2** | 1.10 |

The gate separates **113 levels** for "the shadow is gone" from **2** for this
change — the arm's peak is 56x smaller than the thing the gate exists to catch.
Both arms are deterministic across repeats (identical hashes), and the two ELFs
differ, so this is a real comparison and not two copies of one build.

**The edge, inspected against the real body.** `lift-diff.png` puts every
differing pixel inside the shadow's own footprint (bbox 228x223, which is the
patch), and they outline the caster's silhouette edge rather than filling it.
Crops of the shadow at 5x in both arms are visually indistinguishable: the
shadow keeps the body's outline — roof line, A-pillar, the gap under the sill —
at the same place and the same extent. A max delta of 2/255 over a 64x64 map
magnified onto ~228 screen pixels is edge-texel quantization, not a shape
change.

**What was NOT obtained: an in-motion picture.** The camera that keeps a moving
caster in the cone looks straight down from 40 units and renders empty sky at
capture time, and `benchmark-district.py` states that motion costs pixel
repeatability by design, so a frame-to-frame A/B is unavailable there anyway.
The in-motion evidence is the counts table above.

## The two traps, both of which cost an arm

- **Do not inherit `packageSize`.** `pinPackageSize` gives the base bag the
  MINIMUM size over itself and its coplanar companions, and a car body is
  reflective, so the first arm copied the env pass's pin and moved **literally
  nothing** — 60 -> 60, a perfect null result from a change that was working.
  Kept as `pcsx2/*-inherited-pin`. The silhouette is coplanar with nothing: it
  owns its slot target and its own z-buffer, so it derives its own size. A
  stripped array still keeps its run.
- **A matching capture hash is not a gate.** Three separate arms produced
  byte-identical captures before any of it meant anything, because the pose
  showed no shadow. Build the known-bad arm first.

## Reproducing

    # once: the baked asset tree, on a C: root (a WSL make in-tree eats the
    # bin/obj junctions)
    tyrax-editor --build C:\tyra-probe-0917-caster\example

    # counts, both arms
    ./build-arm.ps1 -Editor <exe> -Arm control -Caster 0
    ./build-arm.ps1 -Editor <exe> -Arm cheap   -Caster 1
    ../reflection-probe-2026-09-16/run-pcsx2-arm.ps1 -Fixture ... -Out pcsx2/...
    python compare_counts.py pcsx2/control/frame-inventory.csv \
                             pcsx2/cheap/frame-inventory.csv

    # the picture gate: THREE arms, and read the known-bad one first
    ./build-arm.ps1 ... -Arm lift-blind   -Caster 0 -Profile debug -NoInstrument \
                        -ShadowCam -PatchLift 1.2 -NoSilhouette
    ./build-arm.ps1 ... -Arm lift-control -Caster 0 -Profile debug -NoInstrument \
                        -ShadowCam -PatchLift 1.2
    ./build-arm.ps1 ... -Arm lift-cheap   -Caster 1 -Profile debug -NoInstrument \
                        -ShadowCam -PatchLift 1.2
    python diff_capture.py <a>.png <b>.png out.png

`-ShadowCam`, `-PatchLift`, `-NoSilhouette`, `-ProjDebug`, `-KeepRoutes` and
`-MovingCaster` are DIAGNOSTIC switches. Only `-Caster` may differ between two
arms whose numbers are compared.

## What is left open

The knob defaults to 0 for one reason only: an **alpha-tested caster** would
lose its silhouette holes with the texture bag gone. That needs a per-material
gate fed from the bake, which is not built — see docs/backlog.md. Nothing in
this district is such a caster.

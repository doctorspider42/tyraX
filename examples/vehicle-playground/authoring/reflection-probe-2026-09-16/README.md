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

PCSX2 v2.6.3, software renderer (`Renderer = 13`), `HostFs = true`. The
emulator's own log is not archived: it is BIOS noise plus the owner's absolute
Documents path, and the only thing in it worth keeping is the version above.
The GAME's log (`log.txt`) is archived for every arm, because that is where the
fixture identity lives.

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

**The inventory is the frame as the branch tip drew it, before this round's
own change.** The reuse budget below moves the two `env_probe_*` rows and
nothing else; every other row in the tables that follow still holds.

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

## S4: the option taken, and why the other two lost

The plan named three: fewer objects in the probe pass, a coarser LOD for it, or
a longer cadence. The inventory above is what settled it, because it says what
the probe actually draws in the pose that is slow.

| option | what it is worth in garage day | verdict |
| --- | ---: | --- |
| fewer objects | **~0 triangles** | four near buildings are the whole probe there. The three objects that draw nothing already cost only 15 bags and ~100 rejected packages a hit, and a size or distance gate that removed the four that DO draw would remove the reflection |
| a coarser LOD for the probe pass | up to ~70% of 10 413 triangles a hit | **priced and not taken** — see below |
| **reuse when nothing changed** | **everything, when nothing is moving** | **shipped** |

**Why the LOD option is not this round's change, with the mechanism.** The
district bakes **no LOD tiers at all**: `meshLodDistance` is 0 and the model
bake gates on it (`lodWanted`), so `.tmdl` carries tier 0 only. Turning it on
turns MAIN-VIEW mesh LOD on with it, which the previous round refuted at
**+0.19 ms**. And the tiers cannot simply be swapped for the probe pass:
`applyGeoLod` swaps the LIVE bag's vertex pointer and bumps `bboxVersion`, so
switching to a coarse tier for the probe and back for the main view would
invalidate the bbox cache and the retained-command cache for every reflected
part, twice a frame — the plan's own "per-bag cost is the term that does not
shrink with the triangle count", in its sharpest form. It needs a second
RESIDENT bag set per reflected part, and the RAM for it in a 32 MB machine.
That is a real feature and it is on the backlog; it is not a cadence decision.

**What shipped** is Task 5 step 2 verbatim — "detect conditions permitting
reuse: unchanged capture pose and unchanged relevant scene/lighting" — with the
staleness stated as a number in **pixels of the probe's own 128-pixel target**
rather than as a frame count. `docs/reflective-materials.md`, "The reuse
budget", has the design; the arms below are the measurement.

### Method for the arms

Both arms of every pair come from **one editor binary**
(`0A8938B013D84D8DD8DF194196E3F75909181058D97278484393D8FE6B9B4D21`, recorded in
every `ARM.json`) and differ only in the project setting, so unlike a codegen
A/B there is no second baker to go stale. `REFLECTION_REUSE_BUDGET` is read back
out of each arm's generated `inc/terrain_config.hpp` and recorded in `ARM.json`
beside the ELF hash, so an arm cannot silently run the default while claiming a
budget.

**The editor at the final commit generates the same game, checked rather than
assumed.** Documentation and Preferences-help edits landed after the arms were
built, so the editor's own hash moved to
`9BB403B29E40F70294960E249508EF22B7E380A26EA292DC59CEC249DA2691A2`. Rebuilding
`parked-cand` with it produces the ELF
`494EC844C46E72C62F61AB192C15BC8923AA1C5C03A572629D861F8AAFD5AEA6` — **byte for
byte the arm that was measured**. That is the check an editor-hash mismatch
otherwise leaves open, and it is cheap: one arm, four minutes.

**And this round hit the stale-editor trap itself, which is worth recording
because it hit from the one direction the previous round's warning does not
cover.** The first five arms were built from an editor compiled *before* a
one-line fix that makes budget 0 mean OFF. Every fixture was regenerated
faithfully, every hash was distinct, every identity check passed — and the
CONTROL captured **zero** times in garage day, because at budget 0 the unguarded
comparison `drift <= 0.0F` is true whenever the drift is exactly zero, which on
a parked camera under a still sky it is. A control that quietly becomes a
candidate produces a clean-looking table with no delta in it. The rule that
catches it is the one already written down — *rebuild the editor from the tree
under test, every time* — and the reason to repeat it is that here the tree had
moved by **one line** since the build.

### The parked fixture, and why its number is the best case rather than the answer

`pcsx2/parked-ctl`, `pcsx2/parked-cand`. 240 recorded frames per pose.

| pose | captures/frame, budget 0 | budget 1 | reuse rate | worst staleness permitted |
| --- | ---: | ---: | ---: | ---: |
| garage day | 0.5000 | **0.0000** | 100% | **0.000 px** |
| garage night | 0.5000 | **0.0000** | 100% | **0.000 px** |
| outer day | 0.5000 | **0.0000** | 100% | **0.000 px** |
| outer night | 0.5000 | **0.0000** | 100% | **0.000 px** |

Every skipped capture was skipped at **zero** drift — not "close enough", the
same image. That is the whole probe: −5 286 triangles and −13 packet flushes a
frame in garage day, which against the road round's hardware anchor of 4.14 ms
per capture is **−2.07 ms**, the figure S4 has been carrying.

**And it is the flattered case, exactly as `benchmark-district.py` warns.** The
camera and the traffic are both parked, and this is a skip-when-unchanged
change, so the parked fixture scores it at 100% by construction. The two
fixtures below are the honest ones.

### The motion fixture: what it buys while the camera moves

`motion-sampler.py` replaces the four poses with four camera regimes in the
garage in daylight, so the only variable is motion.
`pcsx2/motion-ctl`, `pcsx2/motion-cand`, `pcsx2/motion-cand4`.

| regime | drift per cadence beat | budget 1: reuse | derived ms | budget 4: reuse | derived ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| idle | **0.000 px** | 100% | **−2.070** | 100% | −2.070 |
| straight, 6 units/s | 0.909 px | **50%** | **−1.035** | 84.2% | −1.742 |
| turn, 20 deg/s | 0.931 px | **50%** | **−1.035** | 80.0% | −1.656 |
| turn, 90 deg/s | 4.189 px | **0%** | **0.000** | 0% | 0.000 |

**The shape is the finding.** The faster the camera turns, the less the budget
buys — and at the default it buys *nothing at all* in a 90 deg/s turn, which is
precisely the case a cadence change is rejected for. Nobody has to decide
whether a hard turn can tolerate a stale reflection: at 1 pixel it does not get
one.

The drift figures are arithmetic made visible: 20 deg/s at 50 Hz is 0.4 degrees
a frame, two frames is 0.8 degrees, and 0.8 degrees at 128 pixels over a
110-degree field is 0.93 pixels. The instrument reports 0.931.

**The staleness bound is confirmed empirically as well as by construction.** The
claim is "the budget, plus one cadence beat's motion". At budget 1 driving
straight, the worst staleness the gate permitted is 0.909 px and the worst it
ever saw is **1.810 px** — against a predicted 1.0 + 0.909 = 1.909.

### The invalidation fixture: does it ever hold an image it should have dropped?

This is the one that matters, and the camera is parked in all four regimes so
that pose drift is zero by construction and the SCENE is the only thing that
can force a capture. `content-sampler.py`; `pcsx2/content-ctl`,
`pcsx2/content-cand`.

| regime | what changes | captures, budget 0 | budget 1 | expected |
| --- | --- | ---: | ---: | --- |
| static | nothing | 120 | **0** | 0 — the control for the other three |
| hide-show | a reflected object hidden/shown every 50 frames | 120 | **5** | ~5, one per toggle in 240 frames |
| move | a reflected object slides every frame | 120 | **120** | 120 — every beat, i.e. no reuse at all |
| day-night | the night value flips every 60 frames | 120 | **4** | 4, one per flip |

**Every number is the predicted one.** One capture per visibility change, one
per day/night flip, and an object that moves every frame disables the reuse
completely rather than being smoothed over. The `move` regime also reports
**zero beats consulted**: the content key never matched, so the pose budget was
not even reached — the invalidation is a hard gate in front of the budget, not
a term inside it.

Task 5's remaining cases are answered by construction rather than by a fixture,
and each is checkable by reading the generated source: **first use and scene
reload** clear `sharedEnvBasisValid`, and the gate's inner test requires it, so
either forces a capture; **teleports** arrive as a very large translation term;
**split screen** and **reflected-ray probe mode** skip the shared pass entirely
and are untouched; **reflection mode** is compile-time.

### The picture

The gate's own arithmetic says the skipped captures were identical. This is the
independent check that the arithmetic corresponds to pixels, and it is the
game's OWN `--capture-frame` (`docs/devkit.md`) rather than a window grab: no
emulator chrome, no letterbox, no crop to argue about. It needs the `debug`
profile, because `quiet-debug` switches the Live Debugger's command channel off
— the right trade, since a capture is a picture question and the live tools
cost milliseconds rather than pixels. `pcsx2/picture-ctl`, `pcsx2/picture-cand`,
`pcsx2/picture-compare.txt`.

**Within each arm first, because a between-arm number means nothing until the
arm is repeatable.** Three captures per pose per arm, 512x512:

| | garage day | outer day |
| --- | ---: | ---: |
| `picture-ctl`, repeats 2 and 3 against 1 | **0 px** | **0 px** |
| `picture-cand`, repeats 2 and 3 against 1 | **0 px** | **0 px** |

Then between them:

| comparison | pose | size | differing pixels | worst channel |
| --- | --- | ---: | ---: | ---: |
| budget 0 vs budget 1 | garage day | 512x512 | **0** | **0** |
| budget 0 vs budget 1 | outer day | 512x512 | **0** | **0** |

**Byte-identical.** `pcsx2/picture-cand/garage-day.png` is the frame of record:
the three car paints in the garage, sampling a target the candidate captured
once and then reused for the whole run while the control re-captured it 120
times. All three materials reflect the same scenery in both arms, which is
Task 5's "verify that all three car materials still reflect actual scenery" —
and the probe-object rows above say what that scenery IS (two towers, a loft
and a workshop), so "the reflection is present" is a count as well as a look.

The night poses are excluded by the fixture, not by choice: authored lamp
flicker and twinkling stars mean three captures of ONE arm differ from each
other there, so no between-arm number can be read from them.

## Reproducing

```powershell
# the editor, from the worktree under test (NOT the one sitting in build/)
./build.ps1

# the example's baked asset tree, once
build/tyrax-editor.exe --build examples/vehicle-playground
git checkout -- examples/vehicle-playground/inc examples/vehicle-playground/res `
                examples/vehicle-playground/src

# the frame inventory: one arm, instrumented and compiled with the native
# toolchain directly (an editor --build would regenerate the instrument away)
./build-inventory.ps1 -Editor <abs path>/build/tyrax-editor.exe
./run-pcsx2-arm.ps1 -Fixture D:/tyra-probe-0916/arms/inventory `
                    -Out D:/tyra-probe-0916/results/inventory
python ../summarize_inventory.py D:/tyra-probe-0916/arms/inventory --phase 0

# the reuse budget: one pair per fixture, both arms from ONE editor
./build-inventory.ps1 -Editor <exe> -Arm parked-ctl  -Budget 0
./build-inventory.ps1 -Editor <exe> -Arm parked-cand -Budget 1
./build-inventory.ps1 -Editor <exe> -Arm motion-ctl  -Budget 0 -Motion
./build-inventory.ps1 -Editor <exe> -Arm motion-cand -Budget 1 -Motion
./build-inventory.ps1 -Editor <exe> -Arm content-ctl  -Budget 0 -Content
./build-inventory.ps1 -Editor <exe> -Arm content-cand -Budget 1 -Content
python compare_probe.py ctl=<results>/motion-ctl cand=<results>/motion-cand `
       --labels "idle,straight,turn 20,turn 90"

# the picture, which needs `debug` (quiet-debug switches the Live Debugger off
# and the game photographs itself through its command channel)
./build-inventory.ps1 -Editor <exe> -Arm picture-ctl  -Budget 0 -Profile debug
./capture-arm.ps1 -Fixture D:/tyra-probe-0916/arms/picture-ctl `
                  -Out <results>/picture-ctl -Editor <exe>
python compare_captures.py ctl=<results>/picture-ctl cand=<results>/picture-cand
```

**THE EMULATOR AND THE CONSOLE ARE SHARED RESOURCES.** `run-pcsx2-arm.ps1`
launches PCSX2 itself on this arm's ELF and stops only that process; never
`--build --run`, which reaps other worktrees' emulators. No console was touched
by this round at all.

## What this does not establish

**Not one measured millisecond.** Every `ms` figure on this page is a capture
rate multiplied by the road round's hardware anchor of 4.14 ms per capture, and
it is a projection until the arms are run on the console. The instrument drains
telemetry dozens of times a frame, so its own frame times are meaningless, and
PCSX2's would be inadmissible anyway (it emulates no EE data cache). **The
reuse budget owes exactly one thing: a four-pose hardware A/B of `parked-ctl`
against `parked-cand`, and of `motion-ctl` against `motion-cand`.** What it
does not owe is a design decision — the counts and the staleness settle that.

The inventory's four poses are all **parked**, so nothing in it prices a
producer whose cost depends on motion: the terrain chunk rebuild and the wheel
rebake both look cheaper there than they are while driving. The motion fixture
fixes that for the reflection probe and for nothing else. The four poses are
the fixture's four; a fifth vantage would have a fifth inventory.

**No MOVING picture was compared.** The capture path freezes the game for a
fixed spell, so two arms' `--capture-frame` calls do not land on the same frame
of a moving route, and a pixel diff between them would report the sampler's own
phase rather than the change. The moving evidence here is numeric — the capture
rate and the worst staleness in target pixels — and the pixel evidence is
parked. A motion-gate run
(`.claude/skills/tyra-testing/scripts/motion-gate.ps1`) over the same two arms
would close that, and it is the natural next check if the hardware A/B turns up
anything surprising. The night poses are not pixel-comparable on this fixture
at all (authored lamp flicker, twinkling stars), which is why the picture check
uses the two DAY poses.

**The traffic is parked in all three fixtures**, the invalidation one included.
The district's five vehicles are not `reflected` objects, so they cannot change
the probe's content — but a scene that DID reflect a moving car would behave
like the `move` regime, i.e. get no reuse at all, and nothing here says how
common that is in a real district.

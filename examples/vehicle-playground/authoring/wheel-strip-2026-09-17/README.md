# Raw evidence: the frame's two worst-packed producers

Measured in **PCSX2** (software renderer, PAL 512x512 native), 2026-09-17,
against the garage-day frame inventory in
[reflection-probe-2026-09-16](../reflection-probe-2026-09-16/README.md), which
found the target:

> `proj_shadows` and `wheels` are the frame's worst packing. 130 packages for
> 3 050 triangles — 16% of the frame's VU1 packages for 7.5% of its
> triangles — because they are triangle LISTS (75/3 = 25 per package) where
> terrain, roads and the probe are strips (up to 73).

That reading is correct. **The reason it gave for it is not**, and the reason
is the whole of this round's value.

Everything here is a **COUNT**. PCSX2 emulates no EE data cache, so its
milliseconds are not admissible; its counters are exact. No console was used,
none was needed, and the console was not touched.

## The headline: it was not two producers, it was one missing bake call

The plan assumed both producers "are generated at runtime and never got a
strip". Splitting the `proj_shadows` bracket three ways says otherwise.

| garage day, per frame | VU1 packages | vertices | bags |
| --- | ---: | ---: | ---: |
| `proj_shadows` → **caster silhouette** | **60** | **4 440** | 4 |
| `proj_shadows` → receiver patches | 9 | 192 | 2 |
| `proj_shadows` → torch wall copy | 0 | 0 | 0 |
| `proj_shadows` → everything else | 0 | 0 | 0 |
| **`proj_shadows` total** | **69** | **4 632** | **6** |
| `wheels` (drawn / rejected) | **61 / 18** | 4 518 | 3 |

**87% of the projected-shadow row is the CASTER's own model bags**, re-submitted
from the light's point of view — geometry `renderProjShadows` neither builds nor
owns, packed exactly as well as the object loop packs it. The only array the
feature generates is the receiver patch: **192 vertices of 4 632**.

The 4 440 silhouette vertices are an exact decomposition, which is what makes
this a measurement rather than a guess: the CC96 body's three parts
(2 280 + 888 + 180 = 3 348) plus the ggbot body's one part (1 092) = **4 440**,
in **4 bags**. Two cars cast; both are triangle lists.

And they are lists because **`vehbake` had never called `meshstrip` at all** —
no vehicle model in the district carried a strip. Calling it is not enough
either. An imported car is **flat-shaded**, so under the ordinary weld:

| baked model | list verts | unique corners | strip on the full key |
| --- | ---: | ---: | ---: |
| `veh-cc96playground01-body`, part 0 | 2 280 | **2 242** | 3 762 (**1.650x**) |
| `veh-cc96playground01-body`, part 1 | 888 | 886 | 1 479 (1.666x) |
| `veh-cc96playground01-wheel` | 942 | 878 | 1 512 (1.605x) |

Every triangle is its own island, so the strip is *bigger* than the list and
`meshstrip::build` refuses it. **That refusal is the right answer** — welding a
hard normal crease is what makes a model look melted.

## What made the wheel possible: the weld key belongs to the BAG

The wheel batch has **no lighting bag and one flat modulate-identity colour**,
so the attributes the GS receives for a wheel vertex are position and UV and
nothing else. On that key the same three refused meshes strip cleanly.
`meshstrip::Weld::kNoNormal` says it out loud; the emitted vertex still carries
its source corner's normal, so the array stays a well-formed 8-float mesh — it
is simply not the array to shade.

| wheel | list | strip | ratio | padded to runs | pkgs per wheel |
| --- | ---: | ---: | ---: | ---: | ---: |
| `veh-cc96playground01` | 942 | 720 | **0.764x** | 750 | **10** vs 13 |
| `veh-ggbotrally0001` | 84 | 54 | **0.643x** | 75 | **1** vs 2 |
| `veh-tristarplay01` | 417 | 297 | **0.712x** | 300 | **4** vs 6 |

**The BODY reaches 0.757x on the same key and still cannot use it**, because it
is lit. That is the backlog item this round leaves behind, not a thing it
shipped.

The per-wheel block is rounded up to a whole number of runs because the batch
concatenates four wheels a car: a block ending mid-run would put a package
boundary inside the next wheel's run and splice two wheels into one triangle.
It costs 30 vertices of 750 on the largest wheel.

## The arms

**One editor, one generated source, one engine.** The strip is baked and
generated unconditionally; an arm sets the two `#define`s the generated game
guards the CONSUMERS with, patched into that arm's own `terrain_game.cpp` after
`--refresh-gen`. So the arms differ in two tokens and there is no second baker
to go stale between them — the trap the reflection round hit from the other
direction.

editor `88549C21551F6BFB5836CF4EACA9916B7AD29CF8820AE1FF5F32238DCA290528`,
built from this worktree. PCSX2 v2.6.3, software renderer, `HostFs = true`.

| arm | `TYRA_STRIP_WHEELS` | `TYRA_STRIP_PROJ_PATCH` | profile | ELF sha256 |
| --- | ---: | ---: | --- | --- |
| `ctl` | 0 | 0 | quiet-debug | `0641BBF4…0A92C0B3` |
| `wheels` | **1** | 0 | quiet-debug | `E41B7F8E…F80B2BD0` |
| `patch` | 0 | **1** | quiet-debug | `4F3929AB…BA7B5D91` |
| `both` | **1** | **1** | quiet-debug | `5204F45A…10BEC55F` |
| `picture-ctl` | 0 | 0 | debug | `07EAA9CA…B2E779EF` |
| `picture-wheels` | **1** | 0 | debug | `ED95265E…966D55C1` |
| `picture-cand` | **1** | **1** | debug | `20A07E6F…3BFFBE2E` |

Each `ARM.json` also records what the baked wheel `.tmdl` carries, because half
of this change lives in an asset and no generated-source grep can see it.

**The editor at the final commit generates the same game, checked rather than
assumed.** Comment edits landed after the arms were built, so the editor's own
hash moved to
`ACE6B5B16E08D1B7D88206213CD55678BD408E96DD4C11502038B73649907128`. Rebuilding
`ctl` with it produces the ELF
`0641BBF4A4DE36AF2D781E89B09EA8D3682B03210EA0E9046F12115A0A92C0B3` — **byte for
byte the arm that was measured**. That is the check an editor-hash mismatch
otherwise leaves open, and it costs one arm.

### Fixture identity

`pcsx2/*/log.txt`, and the same three lines the inventory round archived — a
matching capture hash is a PICTURE check and never a fixture check.

| | this round | the inventory round |
| --- | ---: | ---: |
| `stripRun` in the generated source | **75** | 75 |
| `ROADSTRIP scene 0 … packages / triangles` | **337 / 21 252** | 337 / 21 252 |
| `TERRAINSTRIP … vertices / packages` | **588 / 8** | 588 / 8 |
| `Static batching` | 65 objects in 48 batches, eligible 87, solo 22 | same |

A fourth line was added for this round, because **half of this change lives in
an asset and no generated-source grep can see it**:
`wheel-strip-report.py` reads the baked `.tmdl` directly and every `ARM.json`
carries its output.

## The result

240 recorded frames after 120 of warm-up, per pose. `ctl` is the baseline
column; `dPkg` is against it.

### Garage day

| producer | ctl | wheels | patch | both |
| --- | ---: | ---: | ---: | ---: |
| `wheels` packages drawn | 61 | **45** | 61 | **45** |
| `wheels` packages rejected | 18 | **15** | 18 | **15** |
| `wheels` vertices | 4 518 | **3 375** | 4 518 | **3 375** |
| `proj_patch` packages | 9 | 9 | **2** | **2** |
| `proj_patch` vertices | 192 | 192 | **96** | **96** |
| `proj_silhouette` packages | 60 | 60 | 60 | 60 |
| **frame packages** | **711** | **695** | **704** | **688** |
| frame vertices | 49 587 | 48 444 | 49 491 | **48 348** |

**−16 and −7, and together exactly −23.** The two levers are independent and
they add, which is the check that neither is quietly paying for the other.

- **The wheel batch: 79 packages become 60** (61 + 18 → 45 + 15), and 4 518
  drawn vertices become 3 375. That is the predicted number to the vertex:
  3 000 + 300 + 1 200 padded strip vertices across three definitions.
- **The receiver patch: 9 packages become 2**, which is more than halving 96
  vertices to 48 can explain. A 96-vertex LIST patch is one package that the
  partial-frustum route then **sub-splits into thirds**; a stripped package is
  never sub-split (`StaPipCore::renderStrippedPkgs` — a 1/3 subpackage of a
  strip is not a strip). So the strip removes the split as well as the
  vertices.

### The other three poses

| pose | ctl packages | both | delta |
| --- | ---: | ---: | ---: |
| garage day | 711 | 688 | **−23** |
| garage night | 763 | 741 | **−22** |
| outer day | 175 | 175 | 0 |
| outer night | 192 | 192 | 0 |

The outer poses are a control the fixture gives for free: neither producer
submits **anything at all** there — no car is in range and no caster holds a
slot — so every counter is identical in every column, which is what a change
confined to these two producers must look like.

Garage night differs from day by one package because the control's patch had a
rejected package there.

### `triangles` RISES, and that is not a regression

1 506 → **3 285** for the wheels. `StaPipTelemetry` counts a package's GS
primitives as `size - 2`, so a strip reports its degenerate seams and its run
padding — see [model-pipeline.md](../../../../docs/model-pipeline.md), "What the
triangle counters count". 45 packages x 73 = 3 285, exactly.

**So `triangles` cannot be the acceptance gate for this change**, and the
acceptance criterion this round was handed ("unchanged triangle counts") is not
one a strip can ever meet. The two that work are the **vertex count**, which
falls, and the **picture**, which is below.

## The quad diagonal: a real defect this fixture CANNOT see

Found by reading rather than by measuring, and recorded here because the
measurement that looked like it confirmed it did not.

A strip's shared edge is its **trailing pair**, so walking a row of cells
near-corner-first — `(ix,iz) (ix,iz+1) (ix+1,iz) (ix+1,iz+1) …` — splits every
cell along the `p01`–`p10` diagonal, while the triangle list it replaces writes
`p00 p10 p11` + `p00 p11 p01`, the **other** one. On a flat quad the two are the
same picture; over terrain, where the four corners sit at four heights, they are
two different surfaces with two different ST interpolations. Emitting the FAR
corner of each pair first restores the list's diagonal, cell for cell, and that
is what ships.

**And it changes nothing this fixture can photograph.** The patch goes FLAT the
moment it lands on geometry (`patchY` returns the centre's height there), and in
the garage it does. The pre-fix and post-fix candidates differ from the control
in **exactly the same 54 pixels** — the same set, pixel for pixel — so the
diagonal contributed **zero** here and the 54 were never it. The pose that would
show it is a caster on open, sloping ground, and this fixture has none.

It is fixed anyway, and written up for the next person in
[model-pipeline.md](../../../../docs/model-pipeline.md), "A hand-written grid
strip flips the quad diagonal", and flagged in
[roads.md](../../../../docs/roads.md), because roads and terrain lay their
strips out by hand too (they are safe — `verify-road-twins.py` compares them
vertex for vertex against the host emitter).

**The wrong lesson to take from this is "the diagonal does not matter".** The
right one is that a fixture can be silent about a defect it does not exercise,
and that attributing a difference to the thing you just changed — which is what
the first pass here did — is a guess until a one-knob arm says so.

## The picture

The game's own `--capture-frame` ([devkit.md](../../../../docs/devkit.md))
rather than a window grab: no emulator chrome, no letterbox, no crop to argue
about. It needs the `debug` profile, because `quiet-debug` switches the Live
Debugger's command channel off. `pcsx2/picture-ctl`, `pcsx2/picture-cand`,
`pcsx2/picture-compare.txt`.

Within each arm first, because a between-arm number means nothing until the arm
is repeatable. Three captures per pose per arm, 512x512. **All three arms are
byte-identical to themselves, in both day poses, over three repeats** — so every
number below is attributable.

`pcsx2/picture-compare.txt` has the run. A third arm, `picture-wheels`, carries
the wheel strip alone, which is what separates the two levers:

| comparison | pose | differing pixels | worst channel |
| --- | --- | ---: | ---: |
| control vs **the PATCH strip alone** | garage day | **0** | **0** |
| control vs the WHEEL strip alone | garage day | **54** | **1** |
| control vs both | garage day | 54 | 1 |
| control vs both | outer day | **0** | **0** |

**The receiver patch is byte-identical** — 0 pixels, exactly the gate this round
was set. The 54 are entirely the wheels, and they are not a defect:

- they sit in two clusters, `x` 20–30 and `x` 480–500 at `y` 324–340, which is
  the two side cars' **tyres** — the surface the wheel batch draws, and nothing
  else in the frame;
- the geometry is provably unchanged: the host property test expands each strip
  back and finds the identical 314, 28 and 139 surface triangles, none lost and
  none invented;
- the worst difference is **one channel step**, on 54 of 262 144 pixels
  (0.02%), in one of two poses.

**A re-triangulation cannot be bit-exact on the GS, and this is what that looks
like.** The GS derives each triangle's ST gradients from its three vertices in
fixed point and breaks an equal-`z` tie in favour of whatever was drawn last;
both are functions of the triangle ORDER, and a strip is a different triangle
order over the same vertices. On a **palettized** wheel texture one texel is a
whole colour index, so a coordinate landing on the other side of a texel
boundary shows up as exactly this: a handful of ±1 pixels on the tyre.

So **"byte-identical" is achievable for a re-ORDERING (the patch) and not for a
re-TRIANGULATION of a textured surface (the wheels)** — see
[model-pipeline.md](../../../../docs/model-pipeline.md), "A re-triangulation is
not bit-exact on the GS". This round's honest gate is therefore: the patch at
zero, the wheels at a budget stated in advance and met, and the triangle
multiset proved on the host.

Only the DAY poses are comparable on this fixture: the night ones have authored
lamp flicker and twinkling stars, so three captures of ONE arm differ from each
other there.

## Reproducing

```powershell
# the editor, from the worktree under test (NOT the one sitting in build/)
./build.ps1

# the example's baked asset tree, once - this is where the wheel STRIP is baked
build/tyrax-editor.exe --build examples/vehicle-playground
git checkout -- examples/vehicle-playground/inc examples/vehicle-playground/res `
                examples/vehicle-playground/src

# the four counting arms. -Root defaults to C: on purpose; see the note below
$ed = (Resolve-Path build/tyrax-editor.exe).Path
./build-arm.ps1 -Editor $ed -Arm ctl    -StripWheels 0 -StripPatch 0
./build-arm.ps1 -Editor $ed -Arm wheels -StripWheels 1 -StripPatch 0
./build-arm.ps1 -Editor $ed -Arm patch  -StripWheels 0 -StripPatch 1
./build-arm.ps1 -Editor $ed -Arm both   -StripWheels 1 -StripPatch 1
foreach ($a in 'ctl','wheels','patch','both') {
  ../reflection-probe-2026-09-16/run-pcsx2-arm.ps1 `
     -Fixture C:/tyra-probe-0917-strip/arms/$a -Out C:/tyra-probe-0917-strip/results/$a
}
python compare_packing.py ctl=<results>/ctl wheels=<results>/wheels `
       patch=<results>/patch both=<results>/both --phase 0

# the picture, which needs `debug` and no instrument
./build-arm.ps1 -Editor $ed -Arm picture-ctl  -StripWheels 0 -StripPatch 0 `
                -Profile debug -NoInstrument
./build-arm.ps1 -Editor $ed -Arm picture-cand -StripWheels 1 -StripPatch 1 `
                -Profile debug -NoInstrument
../reflection-probe-2026-09-16/capture-arm.ps1 -Fixture <arms>/picture-ctl `
       -Out <results>/picture-ctl -Editor $ed
python ../reflection-probe-2026-09-16/compare_captures.py `
       ctl=<results>/picture-ctl cand=<results>/picture-cand
```

**THE EMULATOR AND THE CONSOLE ARE SHARED RESOURCES.** `run-pcsx2-arm.ps1`
launches PCSX2 on this arm's ELF and stops only that process; never
`--build --run`, which reaps other worktrees' emulators.

**Keep the probe root and the build cache on `C:`.** `D:` was at **zero free
bytes** while this round ran, and a full disk does not announce itself: WSL's
`make` silently replaced the in-tree `bin`/`obj` junctions with real
directories and the linker then failed with "Input/output error", and a `host:`
write can land as a **torn CSV** that reads as a corrupt file rather than as a
disk error. If a capture or a CSV comes back malformed, check free space before
suspecting the change.

## What this does not establish

**Not one measured millisecond.** Every number here is a count. What 23
packages are worth depends on which per-package term they carry, and this page
cannot say which:

- if only **packet construction** follows, the measured rate is **2.362 µs a
  package** ([ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md)),
  so 23 packages is **0.054 ms**;
- if the whole `dispatch` bracket follows — 15.0 ms over 803.5 packages in the
  round that measured it, i.e. 18.7 µs a package — it is **0.43 ms**.

**That factor of eight is the open question, and only hardware can close it.**
The arms are built and paired for it: `ctl` against `both`, four poses, on
`192.168.100.150`. Note also that a removed package is not a removed triangle,
and this repo's own rule — a removed triangle is worth about 0.4 of its cycle
count — has no counterpart for packages yet.

**The frame denominator is 711, not the 803.5 the inventory quotes.** The
shipped reflection reuse budget zeroes both `env_probe_*` rows at a parked
pose, so this round measures a smaller frame than the round that named its
target. Quoting −23 against 803.5 would understate it by 12%.

**The torch's wall copy is unpriced.** It is still a triangle list, built per
frame from arbitrary receiver geometry, and no sunlit pose reaches it — all
four of this fixture's poses report 0. Pricing it needs a flashlight fixture.

**The moving fixture was not run.** The wheel batch already skips a rig that
did not move ([wheel-rebake-skip.md](../../../../docs/wheel-rebake-skip.md)),
and a parked fixture flatters that mechanism — but this round changes the
REPRESENTATION rather than the amount of work, so the package count is a
function of which cars are drawn and not of whether they moved. `--keep-routes`
would confirm that rather than discover anything, and the two levers do not
interact: the skip is addressed by slot, and the block length is fixed per
definition either way.

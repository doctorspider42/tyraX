# Not re-baking wheels that did not move

`renderVehicleWheels` rebuilt every wheel vertex of every visible car in world
space on the EE, every frame, and then told the renderer the vertex buffer had
changed whether it had or not. The render-submission attribution priced that at
**2.962 ms, a fifth of the Motor District's render submission, of which 1.970 ms
is the rebake itself** — the largest single named item left in the frame
([render-submission-attribution.md](render-submission-attribution.md)). This
page is what was done about it, what it is worth on a fixture where the traffic
moves, and the two things about the measurement that matter more than the
number.

## What the old shape did

One bag per vehicle definition, and every car sharing that definition
concatenated into it. Per frame, per car that survived the visibility, draw
distance, LOD-tier and frustum tests:

```
batch.verts.clear();
for each wheel w in 0..3:
    vehBodyRotation(pitch, yaw, roll, bodyRot)      # 6 trig, 3 atan2, 1 sqrt
    hard = rotated({lx[w], 0, lz[w]}, bodyRot)      # 6 trig
    up   = rotated({0, 1, 0}, bodyRot)              # 6 trig
    bx   = rotated(..., bodyRot)                    # 6 trig
    by   = rotated(..., bodyRot)                    # 6 trig
    bz   = rotated(..., bodyRot)                    # 6 trig
    for each source vertex:
        batch.verts.push_back(...)                  # 9 mul, 9 add, a push_back
bag->bboxVersion = ++g_bboxStamp;                   # unconditional
```

Three separate kinds of waste are visible in that, and only one of them is the
one the backlog entry named.

**The trigonometry was recomputed per wheel.** `bodyRot` and its six sines and
cosines, the local `up`, and the spin angle are identical for all four wheels;
the loop computed each of them four times. Worse, `rotated()` takes *degrees*
and does its own `cosf`/`sinf` on every call, so twenty vector rotations per car
meant twenty full sets. Counted rather than estimated, with every `cosf`,
`sinf`, `atan2f` and `sqrtf` instrumented:

| per car, four wheels | sin/cos | atan2 | sqrt | total |
| --- | ---: | ---: | ---: | ---: |
| old | 160 | 12 | 4 | **176** |
| new | 18 | 3 | 1 | **22** |
| new, one wheel re-baked | 14 | 3 | 1 | **18** |

That is an **8x** reduction in transcendental calls per car, and it is paid on
every car that is re-baked — a moving one exactly as much as a parked one.
`cosf` and `sinf` are software routines on the EE, and with `errno` enabled they
are not `const`-attributed, so the compiler is not permitted to hoist them out
of the loop by itself. This is not an optimisation the `-O3` was going to find.

**The vertices were `push_back`ed into a cleared vector.** The length was known
before the loop started; clearing and re-pushing paid a capacity test and a size
increment per vertex for it, and it made the buffer's contents a function of
this frame's iteration order rather than of anything addressable.

**`bboxVersion` was bumped unconditionally**, which costs twice. The
package-bbox cache is keyed by (vertex pointer, version), so a bump discards the
bag's package bounding boxes and `bounds` recomputes them; and `bboxVersion` is
part of the retained-command key, so the same bump throws away every retained
command block for the bag's packages and `dispatch` rebuilds them
([retained-static-commands.md](retained-static-commands.md)). For a car that did
not move, all three were done to produce a byte-identical result.

## What it does now

**The batch is addressed by slot.** Car *k* owns
`[k*vertsPerCar, (k+1)*vertsPerCar)` and wheel *w* of it owns the *w*-th
quarter of that. Nothing is cleared; a car whose inputs did not move keeps the
vertices already sitting in its slot, and the frame does no work for it at all.

Since 1.107.0 the array those slots hold is the wheel's **triangle strip**
rather than its list, and each wheel's quarter is rounded up to a whole number
of VU1 runs ([vehicles.md](vehicles.md), "The wheel batch is a strip"). That is
deliberately arranged not to disturb anything on this page: the quarter is still
a fixed length per definition, so a slot still answers "these vertices are
already right" the same way, and the two mechanisms do not interact. What
changed is only how many packages the finished buffer becomes — 79 to 60 in the
Motor District garage.

**A signature decides, and it is compared exactly.** Nine floats — position,
body attitude including the weight-transfer lean, steer, spin, instance scale —
plus the source part's address and the vehicle index. They are the complete set
of inputs the vertex arithmetic reads; the definition's own constants (track,
wheel base, wheel radius, suspension travel) are excluded because `VEHICLE_DEFS`
is immutable static data. **There is no hash.** A hash collision here is a wheel
frozen one frame behind its car, which is precisely the failure this change
invites, and 36 bytes of exact comparison is cheaper than the bug. The compare
is `!=`, so a NaN input always rebuilds rather than always skipping.

**The per-wheel split exists, and it is worth less than it looks.** Everything
in the signature is shared by all four wheels; the one genuinely per-wheel input
is `wheelY[w]`, that wheel's own sampled ground. So a car that *moved* re-bakes
all four, and the split only saves work for a car standing still while one
corner settles under it. It is kept because it costs four float compares and the
steer-pair basis is built lazily anyway, not because it is a significant term —
see the counts below, where it fires on a small minority of frames.

**The stamp is sticky.** `bboxVersion` is only bumped when a slot was actually
rewritten, the car count changed, or the vector reallocated. As the fill logic
stands the last two already imply the first — a new slot is always rebuilt, and
a trim sets the flag itself — so the address and length checks are belt and
braces rather than load-bearing today. They are written out explicitly anyway,
because the thing that would break is silent: `StapipBagBBoxesCacher` keys on
(vertex pointer, version) and **stores no count**, so a stamp carried across a
resize hands back package boxes for the wrong number of packages, and nothing
anywhere reports it.

**The slot table is trimmed with the vertex vector, and that one IS
load-bearing.** A slot that outlives the vertices it describes will claim a
match for a span that was destroyed and then value-initialised, which is the
frozen-wheel bug exactly. Removing that single line and replaying a list that
oscillates between three cars and one turns 0 stale frames into **999 of
1000** — see below.

**The steer pair, not the wheel, owns the basis.** `bx`/`by`/`bz` are a function
of steer, spin, scale and attitude and never of which side of the car the wheel
is on, so the front two share one basis and the rear two (steer pinned at zero)
share another. Two are built instead of four, lazily, so a car needing one
corner re-baked builds one.

### Why the bag pointer may still be shared

`wheelBag_` is one `StaPipBag` reused across every definition in the loop, which
looks unsafe the moment the version stamp stops changing on every submit. It is
not: **neither downstream cache is keyed by the bag.** `StapipBagBBoxesCacher`'s
`id` is `reinterpret_cast<u32>(bag->vertices)`, and the retained-command key is
`{vertices, sts, colors, normals, program, count, ...}` with no bag pointer in
it. Every `WheelBatch` owns its own vertex vector, so the definitions are
already distinct keys. What the old unconditional bump was really buying was
that the caches never hit at all, which is not safety.

### What the change does NOT alter

**The buffer's lifetime.** The vectors are the same vectors, still one set per
definition, still never freed between frames, and their capacity still only
grows. A `resize` that reallocates copies the slots already written and is
caught by the address check, and it happens while the car count climbs to its
high-water mark and then stops — exactly as `push_back` growth did before. The
PATH1-DMA-may-still-be-reading constraint the batch was built around is
untouched, and no new exposure is added.

The same reasoning is what makes a split-screen frame correct. `renderScene()`
runs twice and so does this function; if both halves see the same cars in the
same order the second submit reuses the first's blocks, which is right, because
a retained block holds DMA tags and counts and the MVP and frustum
classification stay per-call. That is already how every ordinary static bag
behaves across the two halves.

## Bit-identity, proved rather than argued

The hoisting only counts if the vertices do not move, and "mathematically
equivalent" is not good enough — a reassociated rotation differs in the last
place and that is a moved pixel. So `rotatedBy()` performs `rotated()`'s three
stages **in `rotated()`'s order on `rotated()`'s operands**, with only the
trigonometry lifted out, and `rotated()` itself is now `rotatedBy(v,
rotTrigOf(rotDeg))` so there is no second copy of the arithmetic to drift. The
PS2 build is `-O3` with no `-ffast-math`, so the compiler may not reassociate it
either.

A native harness transcribes both bakes and compares the raw bytes:

- **200 000 random rigs**, positions to ±500, full ±180° attitude, scales 0.2 to
  4, including the degenerate `c < 1e-5` branch of `vehBodyRotation` and the
  `up.y <= 0.2` travel cutoff: **0 mismatching quadwords**, at `-O2` and `-O3`.
- **4 000 scripted frames** driving a rig through parked, rolling, steering-while-
  parked, spinning-while-parked, one-corner-settling, weight-transfer and scale
  changes, comparing the live slot buffer against a full old-code bake every
  frame: **0 stale frames**, 3 049 rebuilt, 951 skipped.

The second is the frozen-wheel test stated directly: after a skip, the buffer
must hold exactly what a full re-bake would have written.

A third harness models the **bookkeeping** rather than the arithmetic — cars
entering and leaving the batch, slots being reassigned, the vertex vector grown
and trimmed, LOD source swaps — against the same every-frame oracle, and checks
a second invariant besides staleness: a stamp may only be reused when the bytes,
the length and the address were all unchanged. 60 000 frames of random churn:
**0 stale, 0 stamps reused over changed bytes**.

**And it was mutation-tested, because a green test that cannot go red proves
nothing.** Deleting the slot-table trim and re-running the random churn passed
anyway — the coincidence it needs (the same car back at the same slot index,
with the same signature, after the buffer had been trimmed past it) simply never
came up in 60 000 random frames. So the schedule was replaced with one built to
produce it: a participant list oscillating between three cars and one, with
nothing ever moving. There the shipped logic is still **0 stale in 2 000
frames**, and the mutant is **999 stale in the 1 000 frames where the slots
regrow** — one frozen car per frame, every frame. That is the failure this page
keeps warning about, produced on demand, and it is the reason the trim is one
line with a comment on it rather than an afterthought.

## THE MEASUREMENT HAZARD, which is half of this page

`benchmark-district.py` **strips every vehicle's route** so the fixture is
repeatable. That is right for almost every change and disastrous for this one:
a skip-when-unchanged test scores 100% on a fixture where every car is still,
every frame, forever. **A number from the parked fixture alone is not evidence
for this change.** It is the best case and nothing else.

So the script grew `--keep-routes`, which leaves the AI drivers driving while
the camera stays frozen and the player stays pinned, and both fixtures were
measured. The cost of the second one is determinism: with traffic moving the
frame is no longer repeatable, so the pixel A/B has to be run on the parked
fixture and the moving fixture only yields timings and counts.

### The two levers have opposite dependence on traffic, so read them apart

This matters more than any single number, and it is a property of the change
rather than of a fixture:

- **The skip pays only for cars that are standing still.** Everything in the
  signature is shared by all four wheels, so a car that moved re-bakes all four
  and saves nothing. On parked traffic it saves everything; on a car driving
  down a street it saves nothing at all.
- **The trigonometry hoist pays for every car that IS re-baked**, which is to
  say for exactly the population the skip cannot help. 176 transcendental calls
  per car become 22 whether the car is parked or flat out.

Quoting one aggregate millisecond blends the two and lets a parked fixture
credit the hoist to the skip. The per-car counters are what separate them:
`WHEELBAKE cars=N rebuilt=M` says how much of the frame the skip could even
apply to, and whatever is left is the hoist's.

### What is measured here, and what is not

**Milliseconds are not on this page, and deliberately.** They are being taken on
the physical console against the twenty-odd arms already on this branch; PCSX2
models no EE data cache and the emulator's figure for a change that trades
computation for comparison is an upper bound, not an estimate. What the emulator
*can* settle, and what is recorded below, is the two things a millisecond cannot:
that the picture did not move, and how often the skip actually fires.

### The picture does not move

Frozen camera, the parked fixture, the two **day** poses only (the night poses'
lamp flicker never settles, so three captures of one arm differ from each other
there whatever the settle time). `--capture-frame` reads the finished frame out
of GS VRAM over the Live Debugger channel, so it needs no desktop and no focus
and carries no emulator chrome to crop.

**Twelve images — three per pose, two poses, two arms — hash to exactly two
values, one per pose.**

| pose | SHA-256 (first 16) | images |
| --- | --- | ---: |
| 0, garage day | `2BBD2F3398802BDE` | 6 (3 control + 3 change) |
| 2, outer road day | `9839B5B665A856ED` | 6 (3 control + 3 change) |

The within-arm repeats were confirmed identical before the arms were compared,
which is the check that makes the cross-arm result mean anything; and the
control arm was captured on two separate boots, which returned the same two
hashes again. A `512x512` garage-day frame holds the hero coupe and two parked
cars with their wheels plainly in view, so this is a comparison of the thing
under test and not of an empty sky.

### Nothing is added to submission — no counter moves at all

A reasonable person reading "fixed-size per-car spans" expects the spans to be
padded with degenerate triangles. **They are not.** The buffer is resized to
exactly `cars * vertsPerCar` at the end of every frame, which is the same length
the old clear-and-refill produced, so the bag's `count` is identical car for
car. That is the argument; here is the measurement.

(1.107.0 does introduce padding, and for an unrelated reason: a strip's
per-wheel block has to end on a run boundary, which costs 30 vertices of 750 on
the district's largest wheel. It is not this mechanism's, and the arithmetic
below is unaffected — `vertsPerCar` is still one number per definition and the
buffer is still exactly `cars * vertsPerCar`.)

The A/B is **one project directory, release profile, two compiles**, swapping
only the generated `terrain_game.cpp`/`.hpp` — which removes the fixture, the
baked assets, the scene and the route data as variables in one move. The two
source sets were checked to differ in nothing but `struct V3`, `rotated` and
`renderVehicleWheels`. **The two ELFs hash differently** (`7ADCF03D…` against
`9A6786F9…`), the two runs differ in frame time, and `FTCLIP` was armed in both
so every routing counter could be read apart rather than as one total:

| pose | triangles | vertices submitted | flushes | pkg cull / clip / guard / out |
| --- | ---: | ---: | ---: | --- |
| garage day | 40 502 | 55 602 | 120 | all identical |
| garage night | 41 176 | 57 624 | 142.5 | all identical |
| outer day | 16 386 | 18 057 | 44 | all identical |
| outer night | 16 720 | 19 059 | 48 | all identical |

**Both columns. Every pose. Identical to the digit** — the 29 pose-aligned
`FTCLIP` lines match the two arms line for line. Reading them apart matters
because `trianglesCull` counts a strip package as `size - 2` and its guard-band
subset as `size / 3` (a defect recorded in `stapip_telemetry.hpp`), so a package
merely **changing route** would move the triangle total with nothing drawn
differently. It did not: `verticesSubmitted`, which is representation-independent
and is the counter that cannot lie that way, is identical too.

The same two runs give the emulator's view of the saving, which is the shape the
console reports and not its size: **−2.20 ms garage day, −2.21 garage night,
−0.07 and −0.07 on the outer road.** The outer poses barely move for a reason
the counters name outright — `WHEELBAKE` reports `cars=0` there, so the pose
draws no wheel bag at all and there is nothing to save.

### The trap that nearly made all of this a lie

The first run of that A/B returned a **perfect** null: every counter 0.0 delta
in all four poses. It was wrong, and only hashing the ELFs caught it.
**`Copy-Item` carries the source file's timestamp across**, so the swapped-in
generated source landed *older* than the object `make` had already built from
the other arm, `make` did nothing, and both arms ran the same ELF —
byte-identical, `7ADCF03D…` twice, from sources differing by 332 lines. A null
produced that way is indistinguishable from a real one.

So: after swapping a generated file, stamp it, delete the matching `.o` in both
the project and the build cache, and **hash the two ELFs before reading
anything**. A null A/B is only evidence if the two arms were two builds. The
`instrument-frame-cost.py` flow happens to rewrite the file and bump its mtime,
which is why it never hits this. It is now in `tyra-testing`.

### How often the skip actually fires

`WHEELBAKE` counts car-submits, not cars: a car drawn for 300 frames is 300.

**Parked fixture** — the best case, and the one that must not be quoted alone:

| window | cars | rebuilt | wheels | wheels rebuilt | stamped |
| --- | ---: | ---: | ---: | ---: | ---: |
| garage, settling | 900 | 43 | 3 600 | 172 | 43 |
| garage, settled | 900 | **0** | 3 600 | **0** | **0** |
| garage → outer | 360 | 0 | 1 440 | 0 | 0 |
| outer road | **0** | 0 | 0 | 0 | 0 |

Once the suspension settles, **every car-submit is skipped and no submit stamps
`bboxVersion`** — the whole point, and a 100% score that means nothing on its
own. Two things worth reading off it. The settling window rebuilds 172 wheels
for 43 cars, exactly 4.0 per car, so **the per-wheel split never fired**: what
moved was the shared signature, not one corner. And the outer-road pose draws
**no wheel bag at all**, which is why the saving there is thin — there is
nothing to save.

**The `--keep-routes` fixture did not deliver the moving traffic it was built
for, and that is a fixture finding rather than a result.** Its counters are
within one car of the parked fixture's, because the benchmark's four frozen
camera poses never look at the two routed rivals while they are close enough to
draw a wheel bag; a rival that does come into view sits at LOD tier 2 with its
wheels baked into the body. The flag is still right and still necessary — it
just needs a camera pose aimed at a route, which this fixture does not have.

### Driven, with the oracle running inside the game

So the moving case was measured where the cars actually move: the plain example,
player free, driven through the Remote Pad — enter the car, accelerate, steer
both ways, come to rest — 166 units of travel down the street, with parked cars
dropping out of the batch behind it (2 car-submits per frame on foot, 1 while
driving), which is the slot-reassignment path running for real.

`TYRA_WHEEL_REBUILD_VERIFY` re-derives every drawn car's four wheels from
scratch **with the original per-wheel arithmetic** and byte-compares them
against whatever is in the slot, every frame. It costs more than the work it
checks and must never be on in anything measured or shipped; what it buys is the
one answer a screenshot cannot give.

| phase | cars | rebuilt | wheels rebuilt | checked | **STALE** |
| --- | ---: | ---: | ---: | ---: | ---: |
| on foot, cars settling | 600 | 30 | 120 | 600 | **0** |
| on foot, settled | 585 | 0 | 0 | 585 | **0** |
| pulling away | 300 | 198 | 792 | 300 | **0** |
| driving (×10 windows) | 300 | **300** | **1 200** | 300 | **0** |
| coming to rest | 300 | 40 | 160 | 300 | **0** |
| stopped | 300 | **0** | 0 | 300 | **0** |

**5 085 car-checks over 4 500 frames of real gameplay, 0 stale wheels.** And the
lifecycle is exactly the design: parked → skip everything, moving → skip
nothing (300 of 300 frames, all four wheels, every frame), stopped → skip
everything again. The skip fires when it should and never when it should not,
which a frame-time table would have rewarded either way.

### What the retained-command cache did

The second lever, read directly. `STAPIPRET retained=N rebuilt=M per frame`,
parked fixture, same 300-frame windows:

| window | control | change | hit rate |
| --- | --- | --- | --- |
| garage 1 | 590 / 279 | **646 / 223** | 67.9% → **74.3%** |
| garage 2 | 614 / 295 | **677 / 232** | 67.6% → **74.5%** |
| garage → outer | 390 / 200 | **415 / 175** | 66.1% → **70.3%** |
| outer road | 243 / 134 | 243 / 134 | 64.5% → 64.5% |

**56 to 63 package command blocks a frame stop being rebuilt and start being
replayed**, and the totals are identical, so this is the same work moving from
one column to the other rather than work appearing. The outer row does not move
by a single package, for the same reason its saving is thin: no wheel bag is
submitted there at all.

## The outer road regresses, and the wheel bag is not even drawn there

The console's corrected reading is **−1.838 ms garage day and −1.810 garage
night**, against **+0.408 and +0.549 on the outer road** — and the regression is
entirely in `bounds` (+0.422 and +0.545), with `prepare` at +0.02 and
`dispatch`, `vif_wait`, triangles and flushes flat. At the attribution's price
of 58.8 µs for a forced `recalculate()`, +0.42 ms is about **seven extra box
recomputations a frame**, which is a countable thing rather than a diffuse
slowdown.

**It cannot be the wheel bag's own bounds work.** `WHEELBAKE` reports
`cars=0 batches=0` in both outer poses, confirmed under the **release** profile
as well as debug — the bag is never handed to `StaPipCore::render` there, in
either arm, which is also why triangles and flushes are identical. So whatever
grew, grew on *other* bags, and the only state this change carries across a pose
boundary is the package-bbox cacher's: one entry whose version now stays put
instead of changing every frame, and a `g_bboxStamp` that advances about three
times a frame more slowly.

**The mechanism that fits, and that PCSX2 structurally cannot see.** A *fresh*
stamp makes `StapipBagBBoxesCacher` call `recalculate()` on a vertex array the
EE has just written and still holds hot; a *sticky* one makes it read a
`StaPipBagPackagesBBox` that may be hundreds of frames old and stone cold. That
is the same trade the retained-command work measured at −3.36 ms in the emulator
and −0.605 on hardware, in the same direction: the emulator sees the removed
computation and none of the added misses. This page's own emulator A/B reads the
outer poses at **−0.07 ms**, a small *win*, where the console reads **+0.42**, a
loss — the two disagree in **sign**, which is exactly what a data-cache effect
looks like and exactly why no millisecond here is quoted as a hardware claim.

That is a hypothesis, not a finding: it has not been instrumented, because
counting the recomputations means touching `stapip_core` and the cacher, which
another round owns.

### The knob that prices the stamp lever on its own

The three levers do not share a cost profile, so they need to be separable, and
`TYRA_WHEEL_STICKY_BBOX` (default 1) makes them so. At 0 it keeps the slot skip
and the trigonometry hoist and restores the unconditional `++g_bboxStamp`. Three
arms then answer it outright:

| arm | skip | hoist | sticky stamp |
| --- | --- | --- | --- |
| pre-change | no | no | n/a (always bumped) |
| shipped | yes | yes | yes |
| `TYRA_WHEEL_STICKY_BBOX=0` | yes | yes | **no** |

If the outer regression disappears in the third arm, the sticky stamp is the
cost and the honest shipping question is whether the garage's retained-command
gain (67.9% → 74.3%) is worth it there; if the regression survives, the stamp is
innocent and the slot bookkeeping is the thing to look at. One knob, one
project directory, the way `TYRA_STAPIP_RETAINED_COMMANDS` is run.

The knob was checked to do what it says rather than assumed to, using the
counters: at 1 the garage windows read `batches=900 stamped=53` and then
`stamped=2`; at 0 the same windows read `batches=900 stamped=900`, so every
submit bumps the stamp again, while `rebuilt` stays at 53 and then 2 — the skip
and the hoist untouched, the stamp lever alone reverted.

## Limits

- **No console, and no milliseconds from here.** Everything below the native
  harnesses is PCSX2. The one structural claim that does not depend on the cache
  is the transcendental count, which is an instruction count and travels.
- **The district has five vehicles and two of them are routed.** Even the
  `--keep-routes` fixture is a mixed population rather than a worst case, so the
  skip rate it reports is specific to this scene and must not be read as a
  general one. A scene of moving traffic would report a lower one, and the
  hoist's saving would be unchanged.
- **The fixture is the debug profile**, because `--capture-frame` needs the Live
  Debugger. That is fine for a pixel comparison and for counts, and it is not a
  timing baseline.
- **Split screen is not exercised.** The reasoning for why a shared bag stays
  correct across two `renderScene()` calls is in the section above; the benchmark
  fixture has one view, so it is an argument and not a measurement.
- **No hardware DMA-lifetime stress.** The buffers, their lifetime and their
  growth pattern are unchanged by this work, so there is nothing new to expose —
  but that is a structural argument of the same kind the qbuffer slot-pool race
  had before a console disproved it.

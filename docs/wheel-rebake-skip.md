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

PIXEL_AB_PLACEHOLDER

COUNTS_PLACEHOLDER

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

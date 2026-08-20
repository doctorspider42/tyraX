# Vehicles

Driveable cars: one authored model in, a vehicle definition you place in as many
scenes as you like out. The player walks up to one, presses USE and drives it.

This page is the contract between the four pieces: the import bake, the drive
model, the editor's Vehicle Editor, and the generated PS2 runtime.

## The constraint everything here is shaped by

A PS2 StaPip submit costs **~0.7–1.5 ms of fixed EE time whatever it holds**, and
a PAL frame is 20 ms. So the question "how many draw calls is a car" decides
whether vehicles are a feature or a demo. The answer this design reaches is
**two per vehicle**:

| | submits |
|---|---|
| body | 1 — a `.tmdl` drawn through `objMat`, so VU1 applies the motion and the EE does no per-vertex work at all |
| wheels | 1 — all four merged into ONE bag, rebuilt in world space each frame |

Rebuilding four wheels' worth of vertices per frame on the EE sounds expensive
and is not: a decimated wheel is a few hundred vertices, and the transform is
VU0 macro-mode work measured in microseconds against the millisecond a second
submit would cost. This is the static-batching trade-off run in the opposite
direction — batching merges to avoid submits, and so does this; it just does it
every frame because the members move.

Distant vehicles drop to one submit by baking the wheels into the body mesh.

## Importing a model

**One file.** A car arrives from Blender, Sketchfab or a kitbash pack as a single
`.glb`/`.fbx` with the wheels as separate nodes inside it, and that is what the
importer takes. Asking an author to export the body and each wheel separately,
with the origin in the hub, is 20 minutes of work per vehicle and the first
thing anybody gets wrong.

### How the wheels are found

Not by name. The reference asset this was built against
(`CC96/car1.fbx`, CC0) names its nodes `Cube`, `Cylinder`, `Cylinder.001`,
`Cylinder.002`, `Cylinder.003` — Blender defaults — so a name-matching importer
would have failed on the very first real model. Geometry decides, and the text
is only ever a bonus:

1. **Cluster mesh nodes by shape.** Nodes whose AABB extents agree within 10%
   are candidates for being the same part repeated. Grouping on size rather than
   on vertex count is deliberate: a car whose front and rear rims are different
   meshes is still a car.
2. **Score each cluster of 2, 4 or 6.** Round (the two non-axle extents match),
   thin (narrower across the axle than tall), low in the model, small relative
   to it, all the same mesh — plus a bonus if a name or material says *wheel*,
   *tyre*, *rim*. Below a threshold nothing is reported rather than four wheels
   being invented.
3. **Derive the vehicle's frame from the cluster itself.** The axle is the axis a
   wheel is *thinnest* along. Of the two remaining axes, the one the wheel
   centres barely spread along is up (they all sit on the ground) and the other
   is forward. **No exporter axis metadata is read at any point** — Blender, Maya
   and Max disagree about it, and four wheels do not.

That yields the wheelbase, the track and the wheel radius as measurements, so
the author types no numbers to get a working vehicle.

**The body is re-origined to the axle centre at hub height** — the mean of the
detected wheel centres in the canonical frame. The sim places its wheel anchors
at ±wheelBase/2 and ±track/2 around the *chassis origin*, so a body that kept
the exporter's own pivot put every wheel wherever that pivot happened to be:
the reference car's origin sat 0.25 behind the axle midpoint and all four
wheels rode visibly forward of their arches. With the origin at hub height,
`rideHeight = wheelRadius` puts the tyres exactly on the ground.

**What it cannot decide is which end is the nose.** If a node or material says
`front`/`rear` that wins; otherwise the shorter body overhang past an axle is
assumed to be the front, the assumption is stated with both overhang figures,
and the Vehicle Editor offers a flip. A car driving backwards is the most likely
wrong answer the importer can produce and it is never silent.

### Why the bake merges materials

The reference car has **40 materials**, 36 of which become mesh parts — and a
`.tmdl` part is one bag, so the model as authored is **36 submits, nearly two PAL
frames for one parked car**.

The fix rests on a property of the baked lighting path: `pushVert` folds a
material's `kd` into the *vertex colours*, so colour does not have to be a
per-bag state. Untextured materials can therefore be merged losslessly — the
bake collects every untextured material of a model into ONE part, writes each
distinct colour into a generated **palette texture**, and points that material's
vertices at its own cell.

The palette is a **one-dimensional strip**: each colour owns a full-height
column 8 px wide, and every UV sits at `v = 0.5`. It began as a 2-D grid of
blocks and that was a mistake worth recording, because a grid makes the colour
depend on the V coordinate — and V's origin is a *convention*, top-left in the
image file, bottom-left in GL, flipped again somewhere in the console path. A
strip has no row to be off by, so no V convention can select the wrong colour.
The columns are 8 px rather than 1 because the GS quantises texture coordinates
to 12.4 fixed point and filters bilinearly, which would bleed a one-pixel cell's
neighbours into the model at some camera distances. Cells are keyed on the
**colour**, not the material name, so twelve materials sharing six colours cost
six columns; 16 colours still fit in a 128×8 strip, i.e. 4 KB.

Textured materials always keep their own part — they have real UVs that cannot
be rewritten.

Measured on the reference car: **36 parts → 2, and 40 materials → 10 palette
colours in a 128×8 strip.**

### Triangle budgets

PS2-era cars are 1–3k triangles; the reference asset is 8780. The bake decimates
through `meshlod` (the same quadric-error collapse both model bakes already
use) toward a per-vehicle budget — body 1500, wheel 700 by default.

The wheel budget is much higher than a PS2 wheel would suggest because a wheel is
several materials, and meshlod **locks material seams**, so the collapse cannot
thin the rim without eating its roundness. Measured: at a 200-triangle budget the
silhouette lost **21% of its radius**; at 700 it loses 2%.

**The wheel radius the simulation uses is measured from the BAKED wheel, never
from the source.** A collapse pulls a round silhouette inward, and a car whose
physics rides on a 0.240 radius while a 0.190 wheel is drawn floats above the
road with its wheels spinning at the wrong rate. The bake corrects the figure and
says so in the panel whenever the shrink exceeds 5%.

## The drive model

A kinematic chassis on four height samples, not a rigid-body solver — the
era-correct arrangement and the only one that fits the EE budget beside
everything else a scene does.

- **Steering** is the bicycle model: `yawRate = speed / wheelBase * tan(steer)`.
  The turn radius therefore follows from the wheelbase, so a long vehicle turns
  wide with no second knob. Verified against its own closed form by measuring
  path length per radian of yaw off the trajectory: **5.244 m measured vs 5.115 m
  predicted, 2.5% apart**, the remainder being real lateral slip.
- **The steering lock shrinks with speed.** Without it a full-lock flick at top
  speed spins the car on the spot, and a d-pad — which always reads full
  deflection — makes the vehicle undriveable rather than merely twitchy.
- **Grip is one number.** Yawing the body does not yaw the velocity; the
  difference *is* the sideways slip, and grip is the cap on how fast the tyres
  kill it. Low slides, high is on rails, and `handbrakeGrip` replaces it while
  the handbrake is held — that is the entire drift knob.
- **Walls are eight sample points** — the four corners of the BODY rectangle
  (the wheelbase plus `bodyOverhang`, the bumpers' reach past the axles,
  measured off the baked body — the axle rectangle alone let the bonnet clip
  a bumper's length into any wall) plus the edge midpoints — through the
  runtime's own collider set (object boxes, generated
  prefab boxes, and mesh props), axis-separated so a glancing hit grinds and
  only a head-on stops. Corners alone let anything narrower than the corner
  spacing — a pillar, a post, a thin wall hit end-on — pass *between* the
  samples and sit inside the car. Two rules keep the overlapped case honest:
  a car already overlapping (a swept corner, an old save) may only move AWAY
  from the centroid of its blocked points (count comparisons fail here in
  both flavours — "no deeper" tunnels thin walls, "strictly fewer" deadlocks
  the escape), and an OBJECT floor rising more than half a unit over the
  car's feet blocks even where the on-foot walker would climb it, because a
  car reads its height from the terrain alone and a "walkable" mesh face was
  a door into the prop's inside. The colliders are gathered ONCE per vehicle
  per frame (the trig lives in the gather, the eight points cost multiplies),
  and the host twin holds all of it as `--vehicle-check` properties: pillar,
  overlapped, thin wall.
- **Car vs car is MOMENTUM, not a wall** (runtime-only, the
  solid()/collidePlayer split again — the editor's test drive has one car).
  Each body is two discs on its forward axis (a capsule: long like a car,
  cheap like a circle); the deepest overlapping pair defines the contact,
  and the response has two modes. A closing hit exchanges velocity along the
  contact normal with the authored `mass`es and 0.35 restitution — a thump
  with a bit of bounce, both cars keep moving ("nieklimatyczne jeb i oba
  stoją" is the wall answer this replaces). A *resting* contact instead
  velocity-matches the pair along the normal (momentum-conserving, e = 0),
  because the bouncy impulse plus full per-frame separation acted as glue
  and stalled a pusher nose-to-tail with the gas held — bumper against
  bumper now bulldozes traffic, the era's shove. Separation resolves 60% of
  the penetration per frame, inverse-mass weighted, and both bodies' matrix
  paths are told they moved.
- **The AI driver un-sticks itself.** Pure pursuit has no obstacle avoidance,
  so a pillar on the racing line parks the rival against itself forever the
  moment walls actually hold. Throttle held for over a second with no motion
  reads as wedged: the car backs out for a second, advances its waypoint so
  it aims past the obstacle, and resumes.
- **Ground contact is four height samples** under the wheel anchors — each
  the MAX of the terrain and any object floor there (a box top within half a
  unit of the car's feet, a mesh prop's walkable face), so a car drives ONTO
  platforms and ramp props instead of nosing into their sides. Per wheel, so
  a car half on a platform tilts; drive off the edge and the ordinary
  airborne drop takes over. The editor's test drive stays terrain-only — the
  twins share the formulas, the height *source* is each side's own, exactly
  like the wall test's solid()/collidePlayer split. They give
  the ride height, the pitch and the roll from one query each, which is what
  makes a heightfield vehicle affordable at all. A scene with no terrain answers
  `TERRAIN_VOID_Y`, so "there is no floor" needs no branch of its own.
- **The body is a SPRUNG RIG.** Height, pitch and roll are damped
  second-order springs pulled toward the terrain-derived targets (heave
  wn 14 rad/s at 0.9 of critical with plane-velocity feed-forward so a climb
  tracks with no droop; attitude wn 11 at 0.8, softly overshooting a crest;
  airborne both glide level at wn 4). The body used to SNAP to the plane
  while a rate-limited attitude hung mid-swing over every ridge — the mean
  of four samples jumps across a crest, so the body teleported vertically
  while the wheels rode their own samples — and every "car breaks apart on
  a bump" report was that one seam. A landing now compresses and rebounds
  once (the fall speed is kept and absorbed, never zeroed in a frame), a
  cliff-base slam bottoms out on a hard floor one travel below the plane,
  and **grounded has slack** (a third of the ride height), because the
  binary test flickered over every bump and each flicker dropped the
  steering and the tyres for a frame. Held as a `--vehicle-check` property:
  full throttle across a washboard of sharp ridges keeps the per-frame
  height step under 0.3, the attitude sane and the pace up.
- **Suspension compression is presentation**, derived from each wheel's ground
  height against the *tilted* chassis plane — the residual the pitch and roll do
  not already express. On the console the wheel bag actually DRAWS it: each hub
  rides one radius above its own wheel's sampled ground, clamped
  **asymmetrically** — 65% of `suspensionTravel` in compression, 45% in droop.
  Both ends are tighter than the sim's travel on purpose: this clamp is the
  wheel against the ARCH, not the spring — at a full travel up the wheel rode
  visibly through the bodywork.
  A kerb still shoves a wheel up into the arch and a crest still shows daylight
  under a tyre, but a wheel hanging a whole travel below the body read as
  falling off the car, which is exactly how it was reported. The
  **weight-transfer lean moves the wheels WITH the body**: squat, dive and
  corner roll are cosmetic — no ground caused them — so a leaning body over
  ground-stuck wheels opened daylight at the arches on flat ground (4° of squat
  over the front overhang is ~0.11 units of gap). Each hub adds the body
  plane's lean offset at its own anchor; the terrain-derived pitch and roll
  stay out of it, because those the wheels answer with their own ground
  sampling — which is the suspension look. (The editor's
  preview keeps the wheels at ride height — a known, stated divergence.) Measured against the mean instead, a constant slope reads
  as fully compressed at one axle and fully extended at the other while the body
  is in fact riding it level. Driving one wheel over a kerb gives
  `[0.40 0.60 0.60 0.40]`: the diagonal racking four springs actually produce.

Two traps this cost, both worth not repeating:

**The chassis rides the contact plane with no rate limit.** Smoothing the
vertical position looks like suspension and is not: a car climbing a 25% grade at
17 units/s needs 4.3 units/s of vertical travel and an authored suspension rate
supplies about 1.4, so the chassis sinks below the terrain and stays there for
the whole climb (measured: y = 4.68 where the ground was 10.16). The ride belongs
in the compression, which is free.

**`SkelPart::positions` are LOCAL to their node**, not model space — a rigid mesh
node gets a palette slot with an identity inverse bind matrix, so the skin
evaluates to `nodeGlobal * p`. Reading positions straight out of a parsed
skeleton and expecting model space makes all four wheels report the same
unit-cube AABB at the origin, which is what happened before the node transforms
were composed in.

### The gearbox is presentation until you ask it not to be

A car has five gears, an engine speed and a redline. **All of it is DERIVED from
the speed the model above already produces** — the gear is resolved from how fast
the car is going and feeds nothing back — which is the decision the whole feature
rests on: `accel` means exactly what it meant before the gearbox existed, so
every vehicle authored without one accelerates identically with one. Checked
rather than asserted: a harness reproduces the pre-powertrain arithmetic
independently and reads a worst-case difference of **0.000000000** over 14 s of
full throttle.

What that buys for free is everything that needs to *know* the engine speed — the
engine sound's pitch, a tacho, and the shift the player hears.

The ratios are **geometric**, because that is what a real gearbox is: the top gear
reaches `topSpeed` and each one below it reaches that divided by `gearSpread`. At
the defaults (5 gears, spread 1.52, top speed 22) that is 4.12 / 6.26 / 9.52 /
14.47 / 22.00.

Two knobs let the gearbox bite, and **both default to off**:

- **`shiftTime`** cuts the throttle for that many seconds per change — the audible
  gap between gears.
- **`gearTorque`** lets the ratio shape acceleration. It is geometric and
  **centred on the middle gear**, so it changes a car's *character* rather than
  its performance: at 1.0 the five multipliers are 2.310 / 1.520 / 1.000 / 0.658 /
  0.433 and their geometric mean is 1.0000. First gear pulls hard and top gear
  runs out of breath, which is what a gearbox is *for*.

**The down-shift threshold is computed, not validated.** An author can dial
shift-up and shift-down into a contradiction, and `safeShiftDownFrac` (its runtime
twin is `vehShiftDownFrac`) holds the point below where an up-shift *lands* so the
box cannot change up and immediately back down for ever. Measured with
deliberately contradictory thresholds — up at 0.55, down at 0.90 — a 14 s launch
makes **4** gear changes, which is a clean climb. A slider that silently
misbehaves at one end of its range is worse than one that quietly refuses to.

Reverse is gear **-1**: its own ratio off `reverseTopSpeed`, and it never shifts.

**Kickdown.** Flat out (throttle > 0.8) with the engine under 72% of the redline
drops a gear immediately instead of wallowing down to the passive threshold —
the automatic gearbox's answer to a hill. Without it a car cresting a dune in
top gear (torque multiplier 0.43 at `gearTorque` 1) decelerated through two
whole gears before the passive 50% point ever fired. The landing guard keeps
the post-kickdown rpm under the up-shift threshold, so it cannot hunt.

### Wheelspin, and why it needs no new knob

The engine follows the **driven wheels**, not the car (`DriveState::wheelSpeed`),
and the wheels are the car's speed plus whatever drive the tyres could not lay
down. `grip` is already this model's one tyre number, so the comparison is drive
against grip — no second knob — and the consequence falls out on its own: a stock
car never spins its wheels (accel 9 against grip 26) and one on nitrous does.

That is also what makes the engine *flare* on a launch instead of rising smoothly
with the car, and what makes the wheels visibly turn faster than the ground.

`DriveState::slip` (0..1) folds the sideways slide and the wheelspin into **one**
number, so the tyre smoke and the screech cannot disagree about when a tyre has
let go.

### The visual pack

Three fx surfaces past the smoke, all slip/shift/def-driven and all costing
nothing when idle:

- **Skid marks**: a ring of 96 terrain-flat dark quads under the slipping
  rear wheels (slip's fifth consumer), distance-paced (one per half unit of
  travel), fading over six seconds. Colors-only decay: `bboxVersion` bumps
  only when a quad SPAWNS. One alpha-over submit, skipped when empty.
- **Backfire**: an upshift pops a vertical additive quad at the exhaust for
  a tenth of a second — the shift sound's visual twin.
- **The lamps follow the MATERIALS.** The import pools the canonical AABBs
  of body parts whose material name says lamp (`lamp/light/brake/tail/stop/
  head/front`, plus a vertex-end split when the name does not say which end)
  into a rear and a front cluster, stored on the definition (`lampRear`,
  `lampFront`: |x| offset, y, z, half-size). The tail-lamp glow draws AT the
  measured spots and the headlight beam starts from the measured front — so
  the glow fits every body shape, because the material is the one thing
  that knows where the lamps are on THIS model. No lamp-named material =
  the shape-blind heuristic (trim-shouldering sizes pushed past
  `bodyOverhang`) stays the fallback, and a model authored before this
  existed changes nothing.
- **Headlights** (`headlights` on the definition, off by default): an
  additive beam trapezoid painted on the terrain ahead of the nose, bright
  at the bumper and gone at the far end (gouraud does the falloff) — the
  scene lights' ground-pool trick. Sells a night map; subtle by day.

Backfire and headlights share ONE additive submit (the glow bag). Both new
bags ride `PipelineInfoBagFrustumCulling_Precise` with full clip checks —
the engine asserts on the None combination, and a world quad the camera
drives over without clip checks is the giant smeared polygon of legend.

### The sound pack

Past the base loop, three optional companions (all per definition, in the
Vehicle Editor's **Sounds** tab; every one silent until authored):

- **High-rev loop** (`engineHighSound`, `*-loop.wav`): the era's two-sample
  engine — the base loop fades out toward the redline as this fades in, both
  riding the same authored pitch curve against their own natural rates.
  Volumes quantise to 8 steps and write on change, the pitch discipline's
  twin: a steady cruise is zero RPCs.
- **Tyre squeal** (`screechSound`, `*-loop.wav`): volume rides
  `DriveState::slip` — the one number the smoke and telemetry already read,
  so all three agree when a tyre lets go. Silent under slip 0.3, squared
  above it.
- **Gear shift** (`shiftSound`, any one-shot): played on every gear change
  while driving.

A vehicle project reserves **four** voices per core for the drive
(idle/single loop at `base+23`, high at `+22`, squeal at `+21`, the shift
one-shot at `+20`), so the emitter bank runs four slots short there
(`{{SND_SLOTS}}`). The shift deliberately does not borrow a script voice:
`flowPickSfxChannel` exists only in projects whose flow graph plays sounds.
`tools/veh-sound-pack.py` generates the example's deterministic set.

### Nitrous

`nosCapacity` is **seconds of boost, and it is the switch**: it defaults to 0, so a
vehicle has no nitrous until somebody gives it a tank, and there is no second flag
that could disagree with the first. While held it multiplies acceleration by
`1 + nosBoost` and the top speed by `nosTopSpeed`; released, the tank refills at
`nosRefill` per second.

It only burns **on the throttle**. Holding the button against a wall used to empty
the tank with the car stationary (measured on the console: `nos10` fell 7 to 5 at
`spd10 0`), which is a way to lose a resource without ever seeing it do anything.

### The HUD

*Vehicle Editor > Driver > Show a driver's HUD.* Speed, gear and — only when the
vehicle has a tank — the nitrous percentage, drawn while driving and nowhere else.

Three things are worth knowing before moving it:

- **It is runtime text**, so its font needs a glyph atlas. A vehicle with the HUD
  on therefore joins `Project::atlasFontIndices()`; without that the font ships no
  atlas and the readout draws *nothing*, which reads as a broken feature rather
  than as a missing asset.
- **Every horizontal position carries the widescreen squeeze** (the
  4:3-over-window-aspect factor the menus call `uiAspectFix`). Anamorphic
  widescreen keeps the framebuffer's shape and lets the television stretch it, so
  a readout that skips the factor is a third too wide on exactly the displays
  people play on.
- **Keep it inside the title-safe area** ([safe areas](safe-areas.md)). The first
  version put the nitrous line at 0.945 of the height, where the *emulator's own
  frame* already cut it in half — on a CRT it would not have been there at all.
  The bottom-most row is the one to re-check whenever this layout moves.

`hudSpeedScale` is what a world unit per second should READ as, because a unit is
whatever the project decided it is and no code here can guess: 3.6 turns metres
per second into km/h. Measured on the console at top speed under nitrous, the
readout shows **88** with the gear beside it and **NOS 3** below.

### Tyre smoke

`DriveState::slip` finally has its consumer: past 0.35 the rear anchors feed a
48-puff ring at a rate proportional to the slip, so burnouts, handbrake slides
and wall grinds all smoke — because they all *are* slip, and one number feeding
both the smoke and the telemetry is what keeps them from ever disagreeing. The
puffs are camera-facing billboards in **one submit** (the particle system's
exact shape: VU1 expands centre + 2×2 basis weights into a quad, the EE never
touches a corner), untextured grey with per-puff alpha over standard blending,
swirling and swelling as they fade — the fog puff's own recipe. The submit
rides at the frame's translucent tail with the emitters' particles and never
writes Z (the engine's `PipelineZTest_TestOnly`): drawn before the car with
depth writes on, a puff's quad z-rejected the body pixels behind it and read
on screen as a HOLE through the car. A vehicle 70+ units from the camera
spawns no puffs at all — invisible smoke was spending the shared pool. A dead puff is
a degenerate quad, and the bag is skipped outright when the pool is empty, so a
clean drive pays nothing.

### Engine sound

A looping sample whose **SPU2 pitch register** follows the engine speed. Set it in
*Vehicle Editor > Driver*: a sound, a pitch multiplier at idle and one at the
redline, and a volume.

**The loop lives in the encoded sample, not in the play call.** The build runs
`adpenc -L` over any `res/sfx/*-loop.wav`, which sets the SPU2 block loop flags;
nothing at runtime can make a one-shot repeat, so a definition pointing at an
ordinary WAV plays for a fifth of a second and stops. That is why the picker only
offers `*-loop.wav` files — offering the rest would be offering a broken choice.
The convention is in the *file name* because `adpenc` runs over `res/sfx` as a
directory and has no access to the project model; it is the `*-lit.png`
arrangement.

The pitch itself needed no new plumbing: `SD_VPARAM_PITCH` is an ordinary libsd
register, the engine already links libsd, and `logVoiceState` already *reads* it.
The fork gains one function, `AudioAdpcm::setPitch`. Two costs shape the caller:

- **Writing the pitch is a blocking IOP RPC** (`sceSdSetParam` → `SifCallRpc`
  with no callback — the same cost that makes reading those registers debug-only
  and once per channel). So the register is quantised to 32 steps and written
  **only when it moves**: no calls at all at a steady cruise, a handful per second
  under hard acceleration, instead of fifty.
- **A looping voice cannot be stopped** (`AudioAdpcm`'s own doc comment says as
  much). Getting out sets the volume to zero rather than stopping anything, and
  forgets the channel so getting back in restarts the loop instead of inheriting a
  stale pitch. The pause menu mutes the same way (`muteVehicleEngines` — the
  update is gated on `!menuActive` and is the only volume writer, so without the
  mute an open menu held the note at its last pitch), and so does a reverb bus
  flip mid-drive, whose old-core voice would otherwise keep looping for ever.

**The voice is reserved, not borrowed.** All 24 voices of a bus were spoken for
(16 Play Sound + 8 emitters), so in a project with vehicles the emitter bank is
generated one slot short (`{{SND_SLOTS}}` → 7) and voice `base+23` belongs to
the engine note outright. An emitter slot rather than a Play Sound one because
emitters are auto-ranked and degrade gracefully, while a pinned Play Sound
channel is an authored reference; a Play Sound *pinned to 23* is remapped to 22
at codegen for the same reason. And the `adpenc` staleness test reads the
encoded header's own loop byte back, because a `-loop.adpcm` built before `-L`
existed is newer than its WAV and mtime alone would skip it for ever.

The register is the sample's **own** encoded rate times the multiplier — not
`0x1000`. A 22 kHz sample reports 1881, because the SPU2's reference is 48 kHz.

Measured on the console, and the two halves of "it works" need two different
instruments. The telemetry proves the **tracking**:

```
rpm  800 → pitch 1408      (1881 × 0.75, idle)
rpm 6585 → pitch 4192
rpm 5126 → pitch 3488      ← the upshift, and the note drops with it
```

and PCSX2's own audio output, captured and analysed, proves it is **audible** —
the spectral centroid runs **194 Hz at idle → 417 Hz at the first-gear redline →
243 Hz** once it has changed up. That is the RPM sawtooth, heard.

`tools/engine-loop-wav.py` generates the example's sample. It is a script rather
than a committed opaque asset so the waveform is arguable: every partial is an
exact integer number of cycles in the loop, so the join carries no discontinuity
by construction (measured at 7e-14), and it is deliberately dull and quiet because
the runtime plays it at up to 2.4x its encoded rate.

### A shiny body

*Vehicle Editor > Model > Body shine* plus *Reflection map.* The paint gets a
reflection pass baked into the body's `.tmdl` parts — fields the format already
carried. What it mirrors is authored: a **static sphere map** (a `res/` image),
or the dynamic `"@sky"` env map when the field is empty.

**Prefer the static map.** The user's verdict on `"@sky"` was "I honestly don't
see anything reflecting", and they were right for a structural reason: a smooth
sky gradient has no features you can see move, so its reflection reads as a
faint tint. The era knew this — Underground's wet lacquer is **vertical light
streaks in a static texture** — and `tools/nfs-streak-map.py` generates exactly
that (deterministic, no RNG, byte-identical re-runs). With it the paint carries
faceted highlights that sweep as the car yaws, which IS the look.

**Rubber and trim stay matte, and the engine allows it because `.tmdl`
reflection is per PART.** The untextured merge splits into `merged` (paint) and
`merged-matte` — by name first (*rubber/tyre/tire/guma/trim* force matte,
*glass/window/chrome* force shiny, because a deep-blue window would otherwise
fall to the luminance test), then by luminance under 0.12 of full scale. The
reflection attaches to the paint alone; the wheels never shine. The split costs
**one more submit** (3 per car with shine on — the Cost tab reports it), and the
body triangle budget covers the whole body split proportionally across parts —
a per-part budget let a 2-part body carry 2002 triangles against an authored
1500 before that was caught.

The viewport preview reads the same `.tmdl` fields (bin-relative texture paths
mapped back through `res/`), so the editor shows the shine the console draws.

**Fresnel rim + white specular — the wet-lacquer pass, in the SAME submit.**
Per frame, per vertex, on the EE (the wheel-bag precedent, ~1100 vertices of a
few flops each): a fresnel term `0.3 + 0.7·(1 − |N·V|)` rides the env pass's
vertex **RGB**, and a Blinn-Phong `(N·H)⁸` white specular rides the vertex
**ALPHA** — drawn with the GS's **HIGHLIGHT2** texture function
(`RGB = Tex·Cv≫7 + Av`), so the silhouette gets the silver rim, panels facing
the key light get the burned-out white hot spot, camera-facing paint goes deep
and dark, and the additive FIX blend still carries the authored *Body shine*.
No new submit, no VU1 change — HIGHLIGHT2 was always in the GS, one line of
TEX0 state away (`StaPipTextureBag::textureFunction`, per-bag because TEX0 is
re-emitted per bag).

Vehicles only: `vehiclePaintFor` gates it, so a chrome sphere or mirror ball
elsewhere keeps its exact look. Three rules the loop lives by, each from a
field underneath: write through `envColorBag->many` (the LOD tiers re-aim it),
never bump `bboxVersion` (the env bag shares the base pass's frustum-box cache
entry), and keep alpha ≥ 1 — the GS alpha test is NOTEQUAL 0, and a zero
specular would erase the reflection with it. The editor's per-pixel program
mirrors both terms; the PS2-shading GS variant keeps the plain reflection — a
stated divergence.

What made this possible is an engine-side change worth knowing about:
**reflective parts used to be banned from the matrix fast path**, because their
env normals were baked in world space and froze the reflection at the promotion
pose. The local-space bake captures LOCAL normals now, and the per-frame env
pass folds the object's rotation into the env camera basis instead of touching
a vertex — `dot(R·n, e) = dot(n, Rᵀ·e)`, a constant per mesh per frame. A car
yaws every frame; without this it would have had to choose between its two
submits and a correct reflection. The lift applies to every mover with a `refl`
material, not just vehicles.

One consequence for codegen: `projectNeedsEnvMap` scans `res/` for `"@sky"`,
and a vehicle's `.tmdl` lives under `.res-baked/vehicles/` — so the check asks
`Project::vehicles` directly, or the engine boots with *"Env map target
disabled"* and the paint silently stays matte.

### Weight transfer

The body squats under power, dives under braking and leans OUT of a corner —
`DriveState::leanPitch`/`leanRoll`, clamped at ±4°/±6° and rate-limited. Two
rules hold it together: it is **presentation on top of the terrain-derived
pitch/roll, never folded into them** (slope gravity reads `sin(pitch)`, and a
cosmetic lean in there would make the car accelerate downhill *because* it is
accelerating), and the pitch target reads the frame's own longitudinal
acceleration, so a wall hit dips the nose with no code of its own. The
telemetry's `lean10` proves it on-console: 50 (5.0°) through a sustained
top-speed turn, 0 on the straight. `leanAmount` (*Driving > Body lean*) scales
the whole response — 0 is a kart on rails, 2 an American sofa — and the follow
rate is 35°/s, stiffened from 25 after the softer version read as a boat from
the driver's seat.

### AI drivers

`vehiclesim::step` **never reads a pad**. Its input is a `DriveInput` — throttle,
brake, steer, handbrake, nitrous — that a caller fills in, and the AI is the
payoff of that day-one bet: **~25 lines that fill the identical four numbers**,
after which the gearbox, the kickdown, the wall grind, the tyre smoke and the
weight transfer all come along for free, because the AI is just another caller
of the same sim.

Authoring is a **name prefix** (*Properties > AI route prefix* on a placed
vehicle): codegen collects every object in the scene whose name starts with it,
sorted by name, and bakes their positions as the instance's waypoint loop — an
**Area per corner** is the natural marker (invisible at runtime, no collider),
and the baked `VEH_WAYPOINTS` table means no runtime name matching at all. The
controller is pure pursuit: steer from the heading error, throttle backed off in
tight corners, waypoint advanced within a radius.

A player can **hijack** a patrolling car — the pad branch simply outranks the AI
branch while they drive it, and getting out resumes the patrol where it stood.
The acceptance line is `VEHAI` telemetry every ~2 s (position, waypoint, speed):
`grep VEHAI bin/log.txt` proves a patrol advanced its loop with no pad attached,
which is the backlog's own "done when", machine-checked. The example ships a
`rival` on a four-Area `circuit-` loop.

## The Vehicle Editor

*Tools > Vehicle Editor.* A definition list on the left, four tabs on the right.

A definition is **project-wide data** (`Project::vehicles`, `Section::Vehicles`)
and an instance names it. That is the same shape as an `AmbiencePreset` or a
`Prefab`, and it is data in the `.tyra` rather than a file in `res/` because the
file route (`.mtl`, `.flownode`, `.screenfx`, `.drone`) is for things that honour
someone else's format or carry C++. Being a Section buys the collaboration wire,
the AI Assistant's `get_section`/`set_section` and the `sectionJson` edit guard
with no code of its own.

- **Model** — the asset, then every line the importer decided, verbatim. The
  wheel table lists what was found; **Steered** and **Driven** are per wheel, so
  a rear-steer forklift and a 4WD are the same asset with different boxes
  ticked. When the front end was assumed rather than read, the tab says so and
  offers the flip.
- **Driving** — the tunables. The widgets are **derived from
  `vehiclesim::specFields()`**, so a tunable added to `DriveSpec` becomes
  editable, saveable, loadable and documented by appearing in that one list.
- **Driver** — the camera rig while driving, and the exit offset (the driver's
  door).
- **Cost** — the number that decides whether a scene can afford this vehicle:
  submits per vehicle, triangles, what the source was, and what the placed
  instances would total if they were all on screen. Measured on the reference
  car: *submits 2 (~2.0 ms), body 1072 + 4 wheels 1664 = 2736 triangles, source
  was 18 parts and 5312 triangles.*

Two things the window does deliberately:

**It keeps its own undo stack.** Definitions are project-wide, so
`commitChange()` dirties and syncs to session peers but pushes no undo step —
`History` carries the scenes alone. A window that is mostly sliders needs an
undo, and the Material Editor and the Menu Editor's Style tab already made that
call.

**Deleting a definition does not touch the scenes.** Instances keep their name
reference and their Properties row reports it in red. A delete that silently
edited every scene that used the thing would be far worse than a dangling name
somebody can see.

Renaming, on the other hand, **does** follow into every instance in every scene
and every prefab (`App::renameVehicleDef`, the `renameFont` rule) — a reference
stores the name, so it has to.

## Driving it

| Button | What it does |
|---|---|
| Square | the USE action — get in, and get out at the driver's door |
| R2 | throttle — **analog**: the DualShock 2 button pressure, so a squeeze is a crawl |
| L2 | brake |
| D-pad | drives too (steer + throttle/reverse). A keyboard emulating a stick — PCSX2 in a VM above all — can drop chorded key events, and full-lock-plus-throttle is exactly a chord; the d-pad is independent booleans end to end, so it cannot ghost |
| Circle | handbrake, i.e. `handbrakeGrip` instead of `grip` — the drift |
| Cross | nitrous, when the definition has a tank |
| Triangle | cycle the camera |
| left stick X | steer |
| right stick | glance around the car (X, up to ±60°) and lift the boom (Y); springs back on release |
| R3 | held: instant rear view — the look-back mirror |

Every BUTTON of the set is an Input Map role (`veh-throttle`, `veh-brake`,
`veh-handbrake`, `veh-nitrous`, `veh-camera`, `veh-rearview` —
docs/input-bindings.md), so a project can rebind the throttle; the table above
shows the seeded defaults. The runtime falls back to those exact buttons when a
project's map lost an action (they are deletable), and the ANALOG reads — the
steering stick, the stick's own throttle and the d-pad ghosting fallback —
stay hardwired: an axis is not an action, and the ergonomic fallbacks exist
precisely for pads that cannot chord. The throttle role reads the DualShock
2's **button pressure** (`inputAnalog`), so the default R2 squeezes from a
crawl to flat out, and any digital source (a keyboard, an emulator without
pressure mapping) reads as a clean 1.

### The three cameras

`vehCamMode_`, cycled with Triangle and kept across cars:

- **0 chase** — the boom yaw *lags* the body through an exponential, so in a slide
  the car visibly rotates under the camera.
- **1 bumper** — at the nose, low, looking where the **car** points. This one takes
  the *body* yaw on purpose: the opposite choice from the chase cam, so a drift
  throws the whole view sideways and the car feels like it has let go. The same rig
  with the two opposite decisions is the reason to have both.
- **2 far** — the chase rig at 1.9x the distance and 1.6x the height.

The **right stick glances around the rig** — X walks around the car up to
**±60°**, Y lifts or sinks the boom — and both offsets **spring back to zero
on release**: the stick is a glance at a rival or an apex, never a re-aim.
The cap is a frame-rate decision as much as a feel one: the first cut allowed
a full orbit, and swinging the view broadside puts the whole map in the
frustum at once (terrain fill plus every prop), which is exactly where
"koszmarnie klatki spadają" was reported. The one thing the full orbit
bought — looking straight back — is **R3's job: held, it cuts to the rear
view instantly** (the era's look-back mirror), taking the *body* yaw rather
than the lagging boom, because "what is behind the car" mid-slide is a
question about the car, not the camera. The car stays the look-at, so the
glance cannot lose it, and the bumper cam ignores the stick on purpose — its
whole point is being bolted to the body. Signs follow the steering stick's
convention (stick right looks around the right side; stick up climbs and
looks down).

See also **docs/roads.md** — spline streets glued to the terrain, the thing
these cars drive on.

## Where the code lives

| File | What it is |
|---|---|
| [`src/vehiclesim.hpp/.cpp`](../src/vehiclesim.cpp) | Wheel detection + the drive model. Host-only — no GL, no ImGui, no `App`, no `project.hpp` — the `scrollsim`/`placement` shape, so the whole thing runs from a 40-line harness against a real `.fbx`. It is the single source of truth for **two** consumers that must never disagree: the editor's test drive and the generated runtime, which is its per-frame twin. |
| [`src/vehbake.hpp/.cpp`](../src/vehbake.cpp) | The import bake: one `.glb`/`.fbx` in, a body `.tmdl`, a wheel `.tmdl` and a palette PNG out. Deliberately a *vehicle* importer rather than a general static-`.glb` one — a vehicle has to be cut up, re-framed and re-materialised regardless, and none of those steps mean anything for an ordinary prop. What it emits is an ordinary `.tmdl`, so it touches neither model classification, texbake nor codegen. |
| [`src/vehicle_ui.cpp`](../src/vehicle_ui.cpp) | The Vehicle Editor window. `App::` methods declared in `app.hpp`, own TU (the `prefab_ui.cpp` precedent). The import bake is cached per definition and keyed on everything it depends on — it parses a `.glb`/`.fbx` and decimates it, which cannot happen per frame. |

## In the viewport

A placed vehicle draws as **two things**: the body under the object's own
matrix, and one wheel mesh repeated at the four anchors — the same split, with
the same numbers, that the console will use. The wheels ride in the vehicle's
own frame (the object matrix times a local offset), never a second world-space
computation that could disagree with it. A vehicle whose definition has not
been imported yet falls through to the placeholder box, so it is visible and
selectable rather than an invisible hole in the scene.

The import bake is cached per definition and refreshed by `App::vehicleTick`,
which runs every frame from `drawUI` and does **at most one bake per frame** —
a placed vehicle has to draw whether or not the Vehicle Editor is open, and
baking a project's worth of cars in one frame would stall the editor for as
long as parsing that many `.fbx` files takes.

Everything the bake produces (`-body.tmdl`, `-wheel.tmdl`, `-palette.png`) is
written under `.res-baked/vehicles/` with the other derived artifacts, content-
compared before writing so a settled slider does not hand the build a fresh
mtime. The viewport is handed the **in-memory** bake rather than re-reading
those files: one bake, and no host-side `.tmdl` reader that would have to agree
with it.

**Never cache a GL texture id in a draw structure.** `invalidateAssets()` wipes
`texCache_` *and deletes the texture objects in it*, and the asset scan calls it
whenever anything on disk moves — so a stored id goes dangling and every sampler
reading it returns black. That cost a long hunt: the car rendered pure black
against a palette that decoded correctly, UVs that pointed at exactly the right
cells and a `.tmdl` that was provably right, and disabling the texture brought
the body back grey. The palette is resolved from its **path** at draw time.

## Test drive

*Vehicle Editor > Test drive > Drive it.* Runs `vehiclesim::step` — the same
function the console's runtime will be a twin of — on a placed vehicle, in the
real scene, against the terrain sampler the editor already draws with. W/S
throttle, A/D steer, Shift brake, Space handbrake, plus a **Hold throttle**
toggle and a **Steer** slider so the car keeps going while both hands are on
the tuning sliders.

This is the whole reason `vehiclesim` is host-only. Grip and acceleration get
tuned in a *slider → feel → slider* loop instead of *slider → four minutes of
Docker → PCSX2*, and the readout states speed, slip, steering angle, pitch,
roll and the sideways fraction of travel — the last of which is what says
whether the grip setting is doing anything at all.

**A test drive is a way of LOOKING at a vehicle, never an edit.** The object is
moved in place and put back exactly where the author left it when the drive
stops; nothing enters undo and nothing is committed (the procedural
seed-sweep rule).

Two things worth knowing if you touch it. The input is gated on
`io.WantTextInput`, **not** `WantCaptureKeyboard` — the latter is true whenever
any window has focus, which is always while this window is open, and it gated
the throttle off entirely (the car sat at 0.00 with the key held). And
`--ui-script` cannot HOLD a key (its `Key` step is a chord press and `hold` is a
mouse button), so the keyboard path is not machine-verifiable; the panel
controls are, which is how the chain was checked end to end.

## How the geometry reaches the console

The bake writes into `.res-baked/vehicles/`, and the generated Makefile's
`RESDIR := .res-baked` copies that whole tree into `bin/` — so the game opens
`vehicles/veh-<id>-body.tmdl` and its siblings with no new copy step. Verified
end to end: after a build the three files are in `bin/vehicles/`.

**`vehicles/` is exempt from texbake's vanished-source sweep.** That sweep drops
anything under `.res-baked` whose `res/` source is gone — and a vehicle bake has
no `res/` source, so without the exemption a build silently deletes the geometry
the game is about to load. Same reason `stoch/`, `aomap/`, `aoatlas/`, `gi/` and
`modelao/` are exempt.

**The bake runs in the BUILD**, from `vehbake::bakeProject` — the `texbake::bake`
shape, called from the Runner right before texbake (which owns the `.res-baked`
sweep). The editor's per-frame tick calls the same import for its preview, but
the build no longer depends on the editor having done so: it used to, and a
headless `--build` therefore shipped a game with no vehicle geometry at all
(measured — the directory came back empty). One function called by both is what
stops the console and the preview from being able to disagree about what a car
is. A build logs what it produced:

```
[vehicle] CC96: body 1072 tris / 1 part(s), wheel 416 tris, 2 submit(s) per vehicle
```

## What reaches the console today

A vehicle's row is emitted as an **ordinary type-5 Model** pointing at its baked
body `.tmdl`, and its body and wheel slots are **appended after every ordinary
model** so no existing index moves (the scroller's baked-clone rule, applied to
the model table). That single decision buys the body its entire rendering for
free — loading, the GeoPart build, the LOD tiers, and the one that matters, the
**matrix fast path**: `physFastPathEligible` accepts type 5, so VU1 applies the
car's motion and the EE touches not one vertex. What makes it a *vehicle* is the
side table, not its type — the same way a Mirror's reflected set lives outside
`SceneObjectData`.

Verified on real hardware emulation: the game builds, boots and runs at 37 FPS,
and `emulog` shows it opening
`host:.../bin/vehicles/veh-<id>-body.tmdl` and its palette `.png` with no
assert — so the whole chain, `.fbx` → import bake → `.res-baked` → `bin/` →
console loader, is closed.

One trap this cost: the model table's four parallel arrays emit a placeholder
`""` row when the model list is empty, and with no ordinary models but one
vehicle that placeholder pushed `MODEL_COUNT` one short of the rows actually
written. The emptiness test has to consider the appended vehicle slots too.

## Verifying a drive without eyes

**`tyrax-editor --vehicle-check`** runs the drive model's property tests -
host-only, no project, no Docker, exit 0 when every property holds - so a CI
job or a pre-commit hook can gate on the sim. Fifteen assertions, each one a
failure that actually happened: the pre-powertrain regression (a default spec
must be bit-for-bit the old model), gear-ratio geometry, the anti-hunt under
contradictory thresholds, the wall grind and the head-on (including the
phantom grind-in-place), weight-transfer bounds and `leanAmount 0`, and the
hill kickdown with its landing margin. What it cannot check is twin parity
with the generated runtime - the VEH telemetry below is that check.



While driving, the game prints one `VEH` line every half second to
`bin/log.txt`, plus one on enter/exit — position, speed×10 and whether the body
is on the matrix path. That turns a `--pad` script into a machine-checkable
drive:

```
tyrax-editor --pad examples/vehicle-playground \
    "press square 0.3; wait 1.5; hold cross; wait 5; release all"
grep VEH examples/vehicle-playground/bin/log.txt
```

A real run reads like a story, and this one is the feature's acceptance test:

```
VEH enter 0
VEH pos 0 -8 spd10 0 mtx 1        ← in, matrix path on
VEH pos 0 -3 spd10 84 mtx 1       ← accelerating
VEH pos 0 29 spd10 219 mtx 1      ← top speed (22 u/s)
VEH pos 0 32 spd10 0 mtx 1        ← the wall at z=34, minus half a car
VEH exit at -2 32                 ← out at the scaled driver's door
```

And steering has its own acceptance line — hold throttle, then push the stick
left, and the story continues:

```
VEH pos 0 12   spd10 188 yaw 5    mtx 1   ← stick goes left
VEH pos 4 21   spd10 219 yaw 40   mtx 1   ← carving at top speed
VEH pos 24 28  spd10 219 yaw 104  mtx 1   ← 120° of arc across the map
VEH pos 27 26  spd10 9   yaw 121  mtx 1   ← a side wall, mid-turn
```

(Yaw signs POSITIVE for a left turn since the steering-inversion fix: screen X
runs opposite world X, so +X is screen LEFT, and `DriveInput.steer`'s "positive
= right" is negated into the yaw math whose positive angle turns toward +X. The
original acceptance run only proved yaw MOVED under stick input - which way the
car went on screen took a human driver to notice.)

And the drift has one too. The same left turn twice — once on grip (26), once
with the handbrake's grip (6) — is the whole story of the one knob:

```
grip 26:       VEH pos 4 21  spd10 219 lat10 0   yaw 40    ← on rails
handbrake 6:   VEH pos 2 23  spd10 170 lat10 130 yaw 55    ← 13 u/s sideways
```

**The gearbox has its own, and the RPM sawtooth IS the acceptance test** — one
`hold cross` from a standstill on `examples/vehicle-playground` (which ships
`gearTorque` 1 and `shiftTime` 0.18), measured on the emulator:

```
VEH ... spd10 0    gear 0 rpm  800   ← idle
VEH ... spd10 41   gear 0 rpm 7200   ← first gear, on the redline
VEH ... spd10 56   gear 2 rpm 5017   ← changed up, and the engine DROPPED
VEH ... spd10 86   gear 3 rpm 6064
VEH ... spd10 129  gear 3 rpm 6539
VEH ... spd10 135  gear 4 rpm 4748   ← into top
VEH ... spd10 170  gear 4 rpm 5770
VEH ... spd10 0    gear 2 rpm  800   ← the wall: it downshifts on the way to a stop
```

Reverse, nitrous and the camera are the same one line. Reverse is `gear -1` on its
own ratio; `nos10` drains while R1 is held and refills when it is not; `cam` is the
Triangle cycle; and `slip10` reads non-zero exactly where the tyres let go:

```
VEH ... spd10 -59 gear -1 rpm 7198 nos10 10 cam 0   ← reverse, its own gear
VEH ... spd10 -37 gear -1 rpm 4800 nos10 10 cam 1   ← Triangle: bumper cam
VEH ... spd10 96  gear  3 rpm 5078 nos10 9  cam 1   ← R1 held, the tank draining
VEH ... spd10 7   gear  3 rpm 3373 nos10 7  cam 1 slip10 9   ← the wall, tyres gone
VEH ... spd10 0   gear  0 rpm  800 nos10 6  cam 1   ← refilling
```

A screenshot cannot say who moved; this can. It caught three of the four bugs
below inside one session.

## Bugs the telemetry and one screenshot found

Worth recording, because each looked like a different feature failing:

**The wheels drove off and left the body behind.** The transform was only handed
on with `updateObjMat` *when the object was already on the matrix path* - but
that promotion happens in `renderScene` and only once the object is eligible, so
on the early frames the write went nowhere. The wheels are built straight from
the sim's position and moved regardless. A `dirty` fallback makes the car one
object again.

**You could not get in.** The block that parks the player on the boom and points
the camera never reached the generated code at all: it was lost when the method
bodies were moved out of the game header into the `.cpp`. USE set the driver and
nothing else happened, which reads exactly like "it teleports me a little and I
still walk".

**The car ignored its own scale.** The body is an ordinary model row, so scaling
the object scaled the body - and nothing else. Every geometric term the sim and
the wheel bag use now carries the instance's uniform scale, or a scaled car grows
a body around wheels that stayed where they were.

**A third-person player's avatar is hidden while driving.** The walker is gated, so
it would otherwise stay parked at the camera boom, visibly floating along behind
the car. The condition is the driver state itself (`vehicleDriver_ < 0`, ANDed into
the line that already applies a cutscene's *Hide player*), so getting out restores
the avatar with no second writer and no flag anybody has to remember to clear. FPP
needs nothing — there is no body to see, which is exactly why the example project
never showed the bug.

## Not built yet

Honest state, so nobody looks for these. There is **no HUD** — no speedometer and
no tacho, though the powertrain now supplies both their inputs, and note that a PS2
sprite is axis-aligned, so a swinging needle is not a sprite rotation. **No AI
drivers**, though `DriveInput` is the seam and nothing else has to move. **No tyre
smoke**, though `DriveState::slip` is the one number it would read. A vehicle does
not trade momentum with physics crates or with another car. And the controls other
than USE are **raw pad reads** rather than Input Map actions, so they cannot be
rebound. Every one of those has an entry in docs/backlog.md.

The canonical vehicle frame is **forward +Z, up +Y, right +X**, and the bake is
the one place an exporter's frame is discarded. Everything downstream — the sim,
the viewport preview, the generated runtime — works in that frame and never sees
an axis convention again.

## Attribution

The reference vehicle used to develop and verify this is the **CC96** car by its
author, released under CC0 (see the model pack's own `licence.txt`). It is
included in `examples/` and listed in the generated project's
`THIRD-PARTY-NOTICES.txt`.

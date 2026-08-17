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

Cells are 8×8-pixel blocks, not single texels, because the GS quantises texture
coordinates to 12.4 fixed point and filters bilinearly: a one-pixel cell would
bleed its neighbours' colours into the model at some camera distances. A block
makes that impossible rather than unlikely. Cells are keyed on the **colour**,
not the material name, so twelve materials sharing six colours cost six cells.

Textured materials always keep their own part — they have real UVs that cannot
be rewritten.

Measured on the reference car: **36 parts → 2, and 40 materials → 10 palette
colours in a 32×32 texture.**

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
- **Ground contact is four height samples** under the wheel anchors. They give
  the ride height, the pitch and the roll from one query each, which is what
  makes a heightfield vehicle affordable at all. A scene with no terrain answers
  `TERRAIN_VOID_Y`, so "there is no floor" needs no branch of its own.
- **Suspension compression is presentation**, derived from each wheel's ground
  height against the *tilted* chassis plane — the residual the pitch and roll do
  not already express. Measured against the mean instead, a constant slope reads
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

### AI later, player now

`vehiclesim::step` **never reads a pad**. Its input is a `DriveInput` — throttle,
brake, steer, handbrake — that a caller fills in. The player controller fills it
from the Input Map today; a nav-driven AI controller fills the identical struct
later. The boundary costs nothing now and would be a rewrite of every control
path if it were added afterwards.

## Where the code lives

| File | What it is |
|---|---|
| [`src/vehiclesim.hpp/.cpp`](../src/vehiclesim.cpp) | Wheel detection + the drive model. Host-only — no GL, no ImGui, no `App`, no `project.hpp` — the `scrollsim`/`placement` shape, so the whole thing runs from a 40-line harness against a real `.fbx`. It is the single source of truth for **two** consumers that must never disagree: the editor's test drive and the generated runtime, which is its per-frame twin. |
| [`src/vehbake.hpp/.cpp`](../src/vehbake.cpp) | The import bake: one `.glb`/`.fbx` in, a body `.tmdl`, a wheel `.tmdl` and a palette PNG out. Deliberately a *vehicle* importer rather than a general static-`.glb` one — a vehicle has to be cut up, re-framed and re-materialised regardless, and none of those steps mean anything for an ordinary prop. What it emits is an ordinary `.tmdl`, so it touches neither model classification, texbake nor codegen. |

The canonical vehicle frame is **forward +Z, up +Y, right +X**, and the bake is
the one place an exporter's frame is discarded. Everything downstream — the sim,
the viewport preview, the generated runtime — works in that frame and never sees
an axis convention again.

## Attribution

The reference vehicle used to develop and verify this is the **CC96** car by its
author, released under CC0 (see the model pack's own `licence.txt`). It is
included in `examples/` and listed in the generated project's
`THIRD-PARTY-NOTICES.txt`.

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

## Not built yet

Honest state, so nobody looks for these: **nothing reaches the PS2** — there is no codegen and no runtime, so a project
with vehicles builds and runs exactly as it did without them. Collision against
world objects is also still outside the drive model, which samples terrain
height only.

The canonical vehicle frame is **forward +Z, up +Y, right +X**, and the bake is
the one place an exporter's frame is discarded. Everything downstream — the sim,
the viewport preview, the generated runtime — works in that frame and never sees
an axis convention again.

## Attribution

The reference vehicle used to develop and verify this is the **CC96** car by its
author, released under CC0 (see the model pack's own `licence.txt`). It is
included in `examples/` and listed in the generated project's
`THIRD-PARTY-NOTICES.txt`.

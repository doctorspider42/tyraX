# Cloth and soft bodies

A **cloth** is a sheet the game simulates: a curtain in a doorway, a banner on
two hooks, a flag on a pole. You place a rectangle, and the game hangs a grid of
particles in it and integrates them every frame, with the player's own body as a
collider — so walking through a curtain lifts it, and it keeps swinging after
you have gone.

It is the one *surface that moves* in this engine. Everything else static — the
lightmaps, the baked shadows, the static batcher, the navmesh, the collision
boxes — describes geometry that stays where it was put. A cloth opts out of all
of it, on purpose, and what it buys for that is motion nothing else can fake.

Add one with **Add object > Object > Cloth**.

![Three cloths in the editor viewport: a curtain in a doorway, a banner on two
hooks and a flag on a pole](img/cloth.png)

The viewport simulates them live, with the same solver the console runs — so
what hangs, sags and flaps while you author is what the game will do.

## What you place

The object's transform IS the sheet at rest: the unit XY quad under its
position, rotation and scale, exactly the rectangle the editor draws — the same
convention a Decal, a Mirror or a Portal uses. Rotate it and the sheet hangs in
that plane; scale it and the sheet is that big.

Nothing else about a cloth is a size. The grid below decides how *finely* that
rectangle is discretized, never how large it is.

## The controls

| | |
|---|---|
| **Columns / Rows** | particles across and down. The readout under them prints the particle count, the triangle count and the resulting spacing in world units. |
| **Pinned** | which particles are nailed to the rest rectangle: nothing, the top edge (a rail), the two top corners (hooks), top *and* bottom (laced), the left edge (a flagpole), or all four corners. This is also what the sheet is **attached to** — everything else hangs off it. |
| **Stiffness** | relaxation sweeps per step, 1–4. Two is a curtain; four is a tarpaulin. Linear cost. |
| **Damping** | fraction of the velocity lost per step. 0 swings forever; high values settle at once. |
| **Gravity** | units/s² down. Zero is a sheet in free fall — useful with wind. |
| **Wind** | gust acceleration. The gust *breathes* but never reverses, and neighbouring rows ripple out of phase. **Wind from** is its bearing in degrees around world Y (0 blows along +Z). |
| **Player radius** | how fat the player is to this cloth. Default **0.9** — deliberately more than a person, because the sheet is what you are looking at. |

The **material** is picked like any other primitive's: its `map_Kd` is the
fabric's texture, its `Kd` and the object's Color tint it. UVs run 0–1 across the
whole sheet.

## What the player is

**A capsule.** The walker in a generated game has no body a cloth could ask
about — it is a position, a height and a radius used for wall collision — so the
cloth models it as a capsule: the segment from the player's ankles (8 % of their
eye height) to just over their head (100 %), swept by *Player radius*.

Two earlier cuts are worth knowing about, because they are the obvious ones:

- **One sphere at waist height** pushes a *bulge* through a curtain while the
  hem and the top stay where they were. It reads as a hole, not as a person.
- **Three stacked spheres** fix the look and introduce a new fault: pushing a
  particle out of one sphere can push it *into* the next, so a single pass
  leaves particles inside the body (measured at 0.077 of a 0.9 radius).

The capsule is one test instead of three, has no seam to be pushed into, and
leaves **zero** free particles inside the body.

- Wide enough and the curtain billows off both shoulders as you pass.
- Narrow and you slip through a slit in it.
- **0 and the player walks straight through** with the cloth unmoved, which is
  what to set for a curtain that is only there to look at.

The default is **0.9 units** — deliberately larger than a person, because the
sheet is what you are looking at and a generous bubble is what makes the pass
read. With it, a 2.9-unit curtain's hem comes up **1.3 units** as you walk
through.

A pushed particle's *previous* position is deliberately left where it was, so
the displacement becomes velocity on the next step. That is why a curtain
**lifts** and swings rather than dragging a rigid hole through itself.

![Looking back at the doorway a moment after walking through it: the curtain is
up over the lintel and the way is open](img/cloth-walkthrough.png)

*Looking back through the doorway a moment after walking through it, on the PS2
(PCSX2 software renderer). The curtain is still up over the lintel; a second
later it is hanging flat again.*

In a project built from the Empty (orbit) template there is no Player object at
all, and the camera stands in for one — so the effect is visible there too.

Player two is not a collider yet: a split-screen game simulates the sheet
against player one.

## It moves with its object

The pinned particles are re-placed on the object's **live** transform before
every step. Move the object — with the gizmo, from a flow graph, through Live
Link — and the rail goes with it while the fabric swings behind. That is what
makes a curtain on a cart, or a banner on a moving platform, work with no extra
machinery.

## What a cloth is not

It is skipped by every static system, and each of those is a deliberate answer
rather than an omission:

- **No collision.** You walk through it; it does not push you. A box around a
  sheet would be wrong the moment it swung, and a curtain you cannot walk
  through is not a curtain.
- **Not a surface to stand on or drop props onto.** The editor's placement snap
  and the drop-to-floor both skip it.
- **No baked light and no baked shadow.** Its normals change every frame, so a
  lightmap texel or a shadow decal would describe a pose it left. It is shaded
  per vertex from the scene's directional light, **two-sided** — nothing in this
  engine backface-culls and a curtain is looked at from both sides, so a normal
  pointing away from the light is flipped rather than clamped to black.
- **Never batched and never atlased.** Its vertices are written by the
  simulation, not by the geometry builder every other object goes through.
- **Invisible to navigation, physics, raycasts and the USE picker.**

`Hide Object` takes the sheet with it; the simulation freezes with the rest of
the world while a menu is open.

## How it is built, and why that shape

The solver is `src/cloth.cpp` in the editor, with a numeric twin in the
generated game (`updateCloths`, emitted by `templates.cpp`). The editor's
viewport preview runs the *same* file the console's twin reproduces, which is
what makes authoring a curtain by eye work at all: what swings in the viewport
is what swings on the console.

Three properties hold the whole thing up, and each is load-bearing rather than
tidy.

### One particle is one quadword

A particle is `(x, y, z, w)` — 16 bytes, a VU quadword, a `Tyra::Vec4`. Every
step of the solver is whole-vector arithmetic on it and nothing in the hot path
touches a component by name, so on the console each line is one COP2 instruction
pair issued to **VU0**, not three scalar FPU ops. The solver's line count and its
instruction count are the same order of magnitude by construction.

The integration is Verlet, which is what makes that possible: the velocity *is*
`pos - prev`, so damping is a scale on a vector and the acceleration lands as one
fused multiply-add. Three vector operations per particle, no trig, no branches.

Gravity and this step's gust fold into **one constant vector per row** before
the inner loop. The gust itself costs exactly one `sinf`/`cosf` for the whole
sheet: each row's phase offset is baked as a sin/cos pair at build time and
combined by angle addition, so rows ripple out of phase for two multiplies each
instead of a trig call apiece.

### Constraints run in four independent batches

A distance constraint writes *both* of its endpoints, so a naive relaxation pass
over a grid is a serial dependency chain — the one shape a vector unit is worst
at. The grid is split red/black per axis instead:

1. horizontal constraints whose left column is **even**
2. horizontal constraints whose left column is **odd**
3. vertical constraints whose top row is **even**
4. vertical constraints whose top row is **odd**

No particle appears twice inside a batch. A batch therefore has **no internal
ordering at all**: it can be evaluated in any order, on any number of lanes, by
any unit, and the result is identical. That is the property that matters, and
it is why the solver is written this way rather than in the obvious loop.

Each constraint is one subtract, one dot product, one square root (the EE's
single-instruction `sqrt.s` through `Math::sqrtNonNegative`), one divide and one
scaled add. There is no second normalize: the correction is a *fraction* of the
separation vector, which is already in a register.

A pinned endpoint takes none of the correction and its partner takes all of it.
That is what makes a pinned top row behave as a rail rather than as a heavy hem.

### The step is fixed

Verlet with a varying `dt` changes its own effective stiffness every frame and
blows up on the first loading hitch. The simulation runs at a fixed **60 Hz**
and real time accumulates into whole steps, at most **four** per frame, so a
stall costs a bounded amount of catch-up instead of teleporting the sheet.

## Frame rate and region

The step rate is a constant, and 60 Hz is chosen rather than inherited: it is
the fastest display this engine runs at, which makes it the only value at which
**no displayed frame goes unsimulated in either region**.

The accumulator keeps the *speed* right whatever the step is — 60 frames of
1/60 s is exactly 50 steps of 1/50 s, so a curtain swings at the same rate on a
PAL and an NTSC console either way. What the step rate decides is the
**cadence**, and that is what a 1/50 step got wrong:

| step | 50 Hz display | 60 Hz display |
|---|---|---|
| 1/50 (was) | `1 1 1 1 1 1` — one per frame | `0 1 1 1 1 1` — **one frame in six runs no step** |
| 1/60 (is) | `1 1 1 1 2` — never none | `1 1 1 1 1 1` — one per frame |

A frame that runs no step rebuilds no mesh, so it displays the previous pose
again: at 1/50 the cloth animated at 50 Hz on a 60 Hz screen while everything
around it moved at 60. That is measurable from the outside — the `CLOTH`
profiler phase read **1.41 ms** per frame in PAL against **1.24 ms** in NTSC,
which is the five-steps-in-six showing up as five-sixths of the work.

The price of 1/60 is 20 % more solver steps per second in PAL, which measured
as **`CLOTH` 1.41 -> 1.58 ms** on the three-sheet example — less than the step
count suggests, because the mesh rebuild happens once per frame either way. It
also buys a slightly better simulation as well as the smoothness: a shorter
step stretches less, and the same curtain's worst edge went from 6.1 % over its
rest length to **4.2 %**.

It is deliberately **not** `1 / refreshRate`. Damping is applied per step and
stiffness is sweeps per second, so a step derived from the display would make
the same curtain settle faster on an NTSC console than on a PAL one — the
authored look would stop being portable. A constant costs one region 20 % and
keeps the sheet identical on both.

## Moving it onto VU0

What runs on VU0 today is the *arithmetic*: `Tyra::Vec4`'s operators are COP2
macro-mode instructions, so every add, subtract, scale and dot product in the
solver is issued to the vector unit. What still costs is the shape of the calls
around them — each operator is a load, an operation and a store, because macro
mode has no way to keep a value in a VF register across a C++ statement.

The obvious next step is a **VU0 microprogram** (`docs/vu-authoring.md` — the
`src/vu0/` kernel path), and the batching above is exactly what it needs: a
batch is order-independent, so it can be uploaded as a block of quadwords,
chewed through in one `vcallms`, and read back. Before anyone spends the time,
the numbers that decide it:

- VU0 has **256 quadwords** of data memory and **512** micro-memory slots. A
  9×7 curtain is 63 particles — positions *and* previous positions fit with room
  to spare — but a 16×16 sheet (256 particles) does not fit both blocks, so a
  kernel either works in strips or handles one array at a time.
- `run()` **blocks the EE** and clobbers the COP2 register file it shares with
  every `Vec4` and `M4x4` expression in the engine. That is the same deal the
  raytracer takes; it is a real cost, not a footnote.
- A kernel element is one quadword and there is **no cross-element access**, so
  the constraint batches cannot be expressed as one element each — the natural
  split is *integration and collision* as a kernel (perfectly per-particle) with
  the constraints staying on the EE, or a hand-written microprogram that walks
  the grid itself.

None of that is a blocker; it is what the port has to answer. It has not been
measured on hardware, and this page will not pretend otherwise.

## Cost

Per simulated step, per sheet: `particles` integrations, roughly
`2 × particles × stiffness` constraint solves, `particles` collision tests, and
`particles` normals plus `triangles × 3` vertex writes when a step ran. The
mesh is **one StaPip submit**, like any other object.

A 9×7 curtain is 63 particles and 96 triangles; the 11×13 curtain in the
example below is 143 particles and 240 triangles. Keep the grid as coarse as the
look allows — a curtain does not need the resolution a trampoline does, and the
constraint pass is what the cost is in.

The editor's Properties panel prints the particle and triangle counts as you
drag the sliders.

## Measured

**On the console side** (PCSX2, software renderer, two stiffness sweeps). The
generated game has a **`CLOTH` phase on the frame profiler HUD** —
debug profile plus *Preferences > Build > Show frame profiler* — so the
solver's EE cost is a number rather than the gap between `FRAME` and the other
phases:

`examples/cloth-curtain` — three sheets, 305 particles — in both regions, at
the shipped 1/60 step and at the 1/50 one it replaced:

| | PAL, 50 Hz | NTSC, 60 Hz |
|---|---|---|
| **step 1/60** (ships) | `FRAME` 20.01, `CLOTH` **1.58 ms** | `FRAME` ~16.9, `CLOTH` **1.43 ms** |
| step 1/50 (was) | `FRAME` 19.97, `CLOTH` **1.41 ms** | `CLOTH` **1.24 ms** — and one frame in six ran no step |
| | locked 50 FPS | locked 60 FPS |

That NTSC 1.24 is the defect showing up as a number: five sixths of the work,
because five frames in six did the work. Other measurements, on a two-sheet
fixture:

| | |
|---|---|
| 242 particles | `CLOTH` **1.13 ms** (three samples, ±0.02) |
| the same two sheets, shading per VERTEX instead of per particle | `CLOTH` **1.51 ms** — the six-fold duplicate shade was 25 % of the bill, and is gone |

Read the last row twice before optimising anything here: shading was a
*quarter*, so what is left is the solver's own arithmetic — which is exactly
the half a VU0 microprogram would fuse. And PCSX2 is not transferable per
function; **take this on hardware before quoting it as a console cost.**

On the host solver (`src/cloth.cpp`, a 9×7 sheet at 0.25-unit spacing, two
stiffness sweeps, 600 steps):

| | |
|---|---|
| rest spacing held | max edge error **0.0106** of 0.25 units (4.2 %) |
| pinned row drift | **0.000000000** over 600 steps |
| determinism | two runs bit-identical (sum of per-particle distance **0**) |
| hang | bottom row at y 0.478 against an ideal 0.500 — the stretch, visible as it should be |
| the player capsule (radius 0.9) walked through it | hem lifted **1.321** units, **0** free particles left inside the body |
| gust | with wind 14 along +Z, the sheet reaches z **0.84** |
| catch-up | a 5-second stall runs the bounded maximum, not five seconds of catch-up |
| step cadence (the harness in "Frame rate and region") | 50 Hz display `1 1 1 1 2`, 60 Hz display `1 1 1 1 1 1` — **0** frames with no step in either |

## Example

`examples/cloth-curtain` is the proof of concept: a stone doorway with a red
curtain hanging in it, a blue banner on two hooks beside it and a yellow flag
on a pole, both in a gust, and a Player parked six units back looking at the
door. Three pinning modes in one scene, because *what a sheet is attached to*
is the setting that changes its whole character. Walk forward and the curtain
lifts around you.

## Where it lives in the code

| | |
|---|---|
| `src/cloth.hpp` / `src/cloth.cpp` | the solver — host-only, no GL, no `project.hpp`, exercisable from a 40-line harness |
| `src/project.hpp` | `PrimitiveType::Cloth` (21) and the nine `cloth*` fields on `SceneObject` |
| `src/props_ui.cpp` | the Cloth section of Properties |
| `src/viewport.cpp` | `updateClothPreviews` — the editor preview, calling the solver above |
| `src/templates.cpp` | the `CLOTHS` side table in `scene_data.hpp`, and `buildCloths`/`updateCloths` — the runtime twin |

The serialized form is a `cloth` object inside the object's JSON; the pin values
are baked into the generated table, so **that list is append-only**.

# foot-ik-stairs example

A **Foot IK course**: a third-person character and two flights of stairs, one
coarse and one fine, plus a curb and a raised landing
([docs/foot-ik.md](../../docs/foot-ik.md)). Walking up and down them is the whole
test — a flat walk clip has no idea any of this geometry is there, and the
solver is what makes the shoes land on the treads instead of inside them or in
the air above them.

Open `foot-ik-stairs.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

The player starts on flat ground facing the geometry. Walk **west** (left on the
stick) and you meet, in order:

- **ik-curb** — an 18 cm curb across the path. One step up, one step down; the
  toe clearance is what keeps the shoe from clipping the edge on the way up.
- **ik-step-1..5** — a five-step flight, **22 cm per step**, up to a landing at
  1.10. Coarse enough that each tread is a distinct plant.
- **ik-step-b-1..10** — a second flight offset to the north, **11 cm per step**
  over ten steps. Half the rise, twice the cadence: this is the flight that
  catches a solver which only plants once per stride.
- **ik-landing** — the flat top, for standing at an edge with one foot on the
  step below (that is the *rest contact* path, not the walking one).

**Walking down is the interesting direction.** Turn around at the top and come
back. Watch the trailing foot: with the solver working it reaches down onto the
next tread and takes weight there; the failure it exists to prevent is the foot
finishing its stride at the height of the step above and standing in mid-air. The
22 cm flight is where that is easiest to see, because 22 cm is nearly twice the
plant distance — no level-ground tolerance can cover it, which is why the descent
reach is its own mechanism.

Compare by turning it off: *Tools > Foot IK > Instances* and untick `player-1`,
or in the Properties panel with the player selected. Rebuild and walk the same
route — the feet now follow the clip and ignore the stairs entirely.

## How it is wired

- **Tools > Foot IK**, model `res/models/UAL1_Standard.fbx` — the rig. *Rig* tab:
  the six leg bones, filled by **Auto-detect leg bones** (this skeleton uses the
  numbered `Leg_Left_1..3` convention) and confirmed by hand. *Tuning* tab: the
  defaults, with **Descent reach** carrying the 22 cm flight.
- **Instances** tab — `player-1` ticked. That is the per-object switch; the rig
  above is shared by every instance of this model, which is what a crowd of NPCs
  would use later.
- **Clips** tab — `Walk_Loop` solves; a jump or sit clip would be the row to turn
  off here.
- The player is a **third-person** Player object (`Idle_Loop` / `Walk_Loop`), with
  the orbit camera style so the feet stay in frame while you walk.
- The stairs are ordinary **box** objects with box collision. Nothing about them
  is special: the solver raycasts real collision geometry, so a staircase built
  out of boxes, a mesh-collision model or sculpted terrain all work the same way.
- **Terrain** is flat 100 × 100 — the flights are geometry standing on it, which
  is also the interesting case (the ground under a step is not the step).

## Seeing the raycasts

*Preferences > Build > Show Foot IK probes*, then rebuild. Every ground ray the
solver casts is drawn where it was cast, with a mark where it stopped and the
leg's target as a cross - green when that foot takes weight, orange when it does
not. On this course that is the fastest way to tell a ray that missed from a
surface a gate refused from a shoe the solver lifted on purpose. The colour
vocabulary is in [docs/foot-ik.md](../../docs/foot-ik.md).

## Measuring it

`TYRAX_FOOT_IK_TRACE=1` builds emit `PLAYERIK` / `FOOTIK` / `FOOTTRAIN` rows;
the Foot IK regression runner
(`.claude/skills/tyra-testing/scripts/foot-ik-regression.py`) builds with it,
drives this course over Remote Pad, captures a frame sequence and writes the CSVs
plus labelled neural-training samples. This project is the fixture that recipe
assumes.

## Credits

`res/models/UAL1_Standard.fbx` is from Quaternius's **Universal Animation
Library**, released under **CC0 1.0** (public domain dedication) —
<https://quaternius.com>. See
[THIRD-PARTY-LICENSES.md](../../THIRD-PARTY-LICENSES.md).

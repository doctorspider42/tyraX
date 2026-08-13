# endless-runner example

An endless track that **never repeats** — and a roadside that was **generated
procedurally, once, then tiled forever**. Two [Scroller](../../docs/endless-scroller.md)
belts stream a deck and its scenery past a first-person camera; what changes
from chunk to chunk is not authored anywhere, it is drawn from the belt's own
per-cell variation.

Open `endless-runner.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor --build <this folder> --run`.

## What to do

You stand on a deck looking down its length. The floor, rails, obstacles and
overhead gates rush toward you and past you, out to the horizon — forever, and
**never in the same order twice**. Watch one lane for a while: every deck chunk
carries exactly one of three obstacles (a low block, a full-width barrier, a
tall pillar), each at a slightly different angle and lane offset; lamp posts
show up on roughly half the chunks and gates on roughly two thirds. Nothing is
scripted — each chunk resolves its own look from a hash the moment it recycles
to the front.

The belt also **accelerates**: 12 units/s ramping to 30 over the first 80
seconds. Press **Cross** to freeze both belts where they are and **Circle** to
set them going again — a stopped belt costs nothing per frame, which is a good
way to see how much of a runner's frame time is the world moving.

## How it's wired

- **track-belt** (the deck) has two segments that cycle, `deck` and `span`, six
  units each. Both list `deck-slab`, `rail-left` and `rail-right` so the floor
  and rails tile continuously; the difference is what rides on top.
- The three obstacles share **variant group 1**, so exactly one of them appears
  per `deck` chunk — the group always fills, and which member fills it is a
  property of the chunk, not of the pattern. `lamp-post` uses **Appears in
  0.45** and `gate-beam` **0.60** instead: independent coin flips per chunk.
  The two moving obstacles also carry a small **Yaw jitter** and **Side
  jitter**, so even a repeated obstacle is never in quite the same pose.
- **scenery-belt** is a second, independent belt with a longer period (16 units
  against the deck's 12), so the roadside and the deck drift out of phase
  instead of marching in lockstep. Its members are the **baked chunk meshes of
  two Procedural volumes** — `rocks-left` and `rocks-right` scatter rocks and
  pines over a 20 x 16 strip of ground each, and the bake merges 18 instances
  into 4 chunk meshes of 508 triangles total. Those meshes are ordinary `Model`
  objects, so the belt tiles them like any other member; each gets a **Side
  jitter** and a **Scale jitter** so consecutive copies of the same strip do
  not read as copies.
- **director** is an `empty` carrying the flow graph: `Scene Time` through
  `Remap Range` (0..80 s onto -12..-30) feeds **Set Scroller Speed** on both
  belts, pushed once a second by `Every N Seconds`; `On Button` Cross/Circle
  drive **Stop Scroller** / **Start Scroller**.
- The terrain is **160 x 160** on purpose: a belt populates 70 units ahead, so
  a smaller world lets the roadside tile straight off the edge and hang in the
  sky. The fog ends at 74 — inside the belt window and well inside the terrain
  edge — so props fade up out of the haze instead of popping in, and the edge
  of the world is never visible.
- The whole thing holds **50 FPS** (full PAL) with 116 clone objects in flight.
  That is the per-object matrix path doing the work: a belt clone moves every
  single frame, so it is baked once in local space and moved by refreshing its
  matrix instead of re-baking its world vertices. Without it this same scene
  measured **16 FPS**.

## Things worth trying

- Change **Variation seed** on either belt (Properties > Endless scroller) and
  rebuild: the same pieces deal a completely different infinite level.
- Set every member's *Appears in* back to 1 and delete the variant group: the
  belt goes back to plain tiling and you can watch it start repeating every 96
  units.
- Re-seed a Procedural volume in *Tools > Procedural* and rebuild — the
  roadside is regenerated, and the belt tiles whatever came out.

Full guide: [docs/endless-scroller.md](../../docs/endless-scroller.md), and
[docs/procedural-generation.md](../../docs/procedural-generation.md) for the
volumes that made the scenery.

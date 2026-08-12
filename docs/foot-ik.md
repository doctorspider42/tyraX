# Foot IK

Feet that stop at the floor. A walk clip is authored on a flat plane, so played
on a staircase it puts one shoe inside a step and the other in the air. Foot IK
post-processes the sampled pose in the generated game — probing the real terrain
and collision geometry, planting the feet that are standing on something, and
solving each leg analytically — so the same clip follows stairs, curbs and
slopes.

It lives in **Tools > Foot IK**. The example project is
[`examples/foot-ik-stairs`](../examples/foot-ik-stairs/README.md).

## The two switches, and why there are two

A **rig** — which bones are legs, and how far a shoe may reach — belongs to the
**model asset**. Every instance of a character has the same skeleton, so the
binding is authored once and shared; duplicating it onto each object only bought
a way for two copies of one character to disagree about which bone is a knee.

Whether to **run** the solver is per **scene object** (*Properties > Foot
placement > Foot IK*, or the *Instances* tab of the tool). An instance that
solves needs its own corrected pose and cannot share a skinned mesh with
lockstep copies, so a distant extra — or an NPC nobody is looking at — is
cheaper without it. That split is what lets a future NPC opt in without
re-authoring anything.

Both have to be on. An instance switched on whose model was never bound animates
exactly as authored, and the Properties row says so rather than looking broken.

> Projects saved before v18 stored the whole binding on each object. They are
> lifted into per-model rigs on load, automatically and once — see
> [format-versioning.md](format-versioning.md).

## Binding a rig

Pick the model at the top of the window, then **Rig**:

**Auto-detect leg bones** guesses the six bones from their names and recognises
the conventions that actually turn up — Mixamo (`LeftUpLeg` / `LeftLeg` /
`LeftFoot`), Blender Rigify (`thigh.L` / `shin.L` / `foot.L`), Unreal (`thigh_l`
/ `calf_l` / `foot_l`), the plain English spellings and the numbered
`Leg_Left_1..3` style. Names are compared with case, punctuation and separators
removed, so `thigh.L`, `Thigh_L` and `thighl` are one spelling. It reports how
many of the six slots it filled and leaves the master switch **off**: a guess is
a starting point you confirm, and turning on a solver nobody looked at is how a
character ends up with a broken knee in a build.

Otherwise pick the bones by hand. Each leg must be a real `hip > knee > ankle`
descendant chain in the imported hierarchy; the window names every problem it
finds (a bone that is not in the model, a name the file carries twice, a chain
that is not a chain, the same bone in two slots) and the game leaves the
animation untouched if a mapping still cannot be resolved on the console.

Bones are bound **by name**, never by index: re-exporting a model with one extra
bone shifts every index, and a rig that silently retargets to the wrong leg is
far worse than one that reports a bone it can no longer find. A *renamed* bone
does have to be re-picked.

## Tuning

Distances are project world units, except the sole offset, which is in the
model's own units and therefore follows each instance's scale.

| Field | Meaning |
|---|---|
| **Sole below ankle** | Ankle-to-floor gap. **Measure from model** reads it out of the file - how high the ankle stands in the bind pose above the model's own floor - and the line beside it says whether the current value agrees, so "is my sole offset right" is answerable by looking instead of by experimenting on a console. It is a CONSTANT offset under every planted foot, so a wrong one is a shoe sunk or floating everywhere, on flat ground as much as on stairs. |
| **Probe above / below** | Vertical search range around the animated sole. A descent adds its own reach on top (below), so these only have to cover level ground and slopes. |
| **Plant distance** | How close the animated sole must come to the ground before the foot may lock to it. |
| **Release distance** | How far the animation may pull away from a planted foot before it unlocks. |
| **Max pelvis correction** | How far the hips may sink so the lower leg can reach its contact. A reach safeguard, not a second ground magnet: only a planted foot that needs more reach lowers it. |
| **Max foot tilt** | Caps the pitch/roll a planted ankle takes from the surface normal. `0°` keeps the authored orientation; `35°` follows ordinary slopes while refusing extreme triangle normals. |
| **Toe clearance** | Extra gap requested above a higher surface detected ahead of an airborne shoe. `0` disables swing clearance. |
| **Descent reach** | Extra downward reach a descending character may add to the contact bands — see below. `0` restores the level-ground behaviour exactly. |

The tab also reports the **model's own height**, because that is what decides
whether the rest of the numbers fit at all: every default is authored for a
~1.8-unit human, so a character twice that tall has a plant band, a probe window
and a pelvis allowance half of what it needs - and the symptom is feet that stop
reaching the ground exactly when a step gets interesting, which reads as a solver
bug rather than as a scale one.

## Per-clip rules

A rig is a property of the skeleton; whether a shoe should stop at the floor is a
property of the **motion**. The **Clips** tab lists the model's clips and lets
each one say something different:

- **Solve** off — this clip plays exactly as authored. That is what a jump, a
  sit, a ladder climb or a death animation wants; the solver drops its contacts
  when such a clip starts, so a jump cannot land the feet back on the stair it
  left.
- **Contact ×** multiplies plant, release and descent reach. A run covers more
  ground per frame than a walk, so its contact bands want to be wider.
- **Clearance ×** multiplies toe clearance; `0` leaves that clip's own foot lift
  alone.

Rules are stored under the clip's **source** name, like the Animation Editor's
clip edits, so renaming a clip there cannot orphan a rule. A row that says
nothing is dropped on save.

## How the solver works

It runs after clip evaluation and crossfade, before VU0 skinning, on the
engine's generic `SkelPoseAdjust` hook. It reads the previous frame's
**unadjusted clip** ankle positions for its probes — feeding the already-locked
result back into the contact detector would make a foot report that it never
moved and stretch the leg behind the walking character.

A sole must stay inside the contact band at stance-like horizontal speed for a
short confirmation window before it may plant; a fast descending swing is never
turned into a contact merely because it passed within probe range. A new contact
fades in over a 0.14 s smoothstep instead of applying a large correction on its
first frame. A plant releases as soon as the authored foot begins a measured
toe-off or pulls far enough away, and keeps its old target while its weight
smoothsteps to zero over 0.08 s — target height and influence therefore cannot
jump on the same frame when a probe crosses a stair edge.

The pelvis may also come down - but only by the reach the **leg cannot make**, and
that quantity is measured rather than guessed: a leg spans `|hip-knee| +
|knee-ankle|` (rigid bone lengths, so readable from any pose), and if the target
sits farther from the hip than that, the difference is exactly what the hips owe.
Below it the leg does the work and the pelvis stays where the animation put it.

Getting that quantity wrong fails in **both** directions, and both were seen while
this was written. Handing the pelvis the FULL correction makes the hips an echo of
the animation: 17 cm of hip drop measured on flat ground with both feet already
exactly on their targets - a crouch that bought nothing, read as the avatar buried
to the shins, and pulled planted feet off their contacts (29 such frames in one
route). Allowing the leg a flat *fraction* of Max pelvis correction instead fixes
that and breaks the other end: a target beyond the leg is then never reached - the
ray finds the ground, the target cross sits on it, and the shoe hangs half a metre
above, because the analytic solve has nothing left to give. The geometric answer
avoids both, at 4.7 cm of hip drop at most on the measured route and only on the
17% of frames where the leg really is at full extension. The price is a knee that
straightens fully at the extreme, which is the right trade against a body that
sinks or a foot that never arrives. Then an
analytic two-bone solve rotates the hip and knee. A filtered authored knee pole
guides that solve and takes over near the straight-leg singularity where a raw
cross product can flip sides. The authored ankle yaw is preserved while an
optional pitch/roll follows the supporting surface normal.

Planted-foot weights also define a small support polygon: the runtime shifts the
pelvis by at most 4.5 cm toward its weighted centre and leans the pelvis subtree
gently toward the average support normal. Those body changes are applied first
and both legs re-solve afterwards, so balance motion cannot drag locked shoes
off their contacts.

**Going up** is a swing problem. Two short probes look ahead in the travel
direction; the current support normal predicts the continuous uphill rise, so
only an abrupt excess above that plane counts as a lip. When either probe finds
one, the solver releases a stale lower-step contact and adds a filtered vertical
clearance to carry the shoe over the riser. The same swing sweeps four
approximate shoe corners at two points between the previous and current sole
poses, which closes the gap where a fast toe crosses a thin riser between
vertical raycasts. A footprint sample contributes clearance only when it finds
support more than 2.5 cm above the current support plane — equal-height flat
ground must never manufacture toe clearance or prevent the next plant.

Standing at an edge uses a separate **rest contact**: after the whole object has
been stationary for 0.1 s, an ankle that is itself nearly still may reach down
through the normal band by up to *Plant distance + Max pelvis correction*, so one
leg can stay on a step while the other finds the lower floor. It releases the
instant the object moves, so it cannot turn a walking swing into a ground magnet.

Deterministic procedural IK throughout: no model inference and no allocation in
the loop.

**A standing character gets no swing clearance.** The clearance probes need a
direction of travel, and when the object is not moving they used to fall back to
the ANKLE's own speed - which an idle shuffle, or the turn the avatar makes when
the stick is released, pushes past the threshold. "Ahead" then means whatever
that shuffle happened to point at, so standing beside a step the probes find its
riser, decide it is a lip and lift the free foot over it. Nothing resolves it
while the character stands, so the lift is permanent: a foot held a few
centimetres above ground it is standing on, indefinitely - which reads as the
solver failing to plant when in fact it was lifting the foot on purpose. The
ankle-speed fallback is still there for an in-place animation (a model that walks
without its object moving), but it is switched off once `objectStillTime` says
the character is standing - the same signal rest contact uses, so the two halves
of the policy cannot disagree. Measured beside a 22 cm step, one binary, one
line: the lift went from 6 consecutive frames (target raised 8 cm above the
foot's own sole) to none.

## Going down

Walking **down** is the hard direction, and it needs its own mechanism. The
contact bands above are tuned for level ground and are deliberately narrow — a
wide band turns every airborne swing into a ground magnet. On a descent the
swing foot has to cover the whole step height *plus* whatever the avatar's
visual root still owes its collision height, which on a 22 cm stair is already
twice the plant distance. No level-ground number stretches to cover that without
ruining flat ground.

So the reach becomes conditional on the character actually going down. The
runtime classifies its own **locomotion grade** — level, up or down — from three
independent signals, because each one alone is wrong somewhere:

- the visual root's own vertical speed against its horizontal travel (says
  nothing while the root filter is still catching up, and says "down" during a
  jump's descent),
- the ground drop the walker publishes (exact, but only a walker has it — see
  below),
- a probe under the path ahead, its lead proportional to speed (works for any
  object, including an NPC or a scripted mover, and sees the step before it is
  stepped on).

The classification is filtered and **soft**: every gate scales with it, so
nothing pops on the frame it flips. Ascending and descending are made mutually
exclusive, and geometry outranks velocity.

What the descent buys is a **budget**, capped by *Descent reach* and spent in
four places: the probe window (a tread has to be visible before any gate can
accept it), the descending-plant band, the down-reach envelope, and the pelvis's
downward limit (a fraction of it — the leg does most of the work, and a pelvis
allowed to sink a whole step reads as a crouch). Two gates additionally accept
the body's grade in place of the swing foot's own vertical velocity: in a flat
walk clip played by a falling root, the ankle can be rising in model space while
the shoe drops toward the next tread.

Every one of those is still **raycast-gated**. A budget widens where a foot may
*look*; it never changes what it may believe. Nothing plants without a ray that
found the surface.

Two supporting details, both of which were bugs first:

- **The player's visual root eases down twice as fast as it eases up.** Collision
  moves onto a walkable step in one frame — that safety property is kept — while
  the avatar root and camera follow through a low-pass filter, which removes the
  one-frame whole-body hop. Filtering a *fall* as slowly as a *climb* left the
  whole avatar hanging above the tread for several frames, which is most of the
  gap the feet were then asked to cover. A rising step still uses the slow
  filter; the alternative is the camera hopping.
- **The residual is published, not guessed.** `RuntimeObject::ikGroundDrop` is
  how far the ground under an object sits below the position it is being
  rendered at; the walker writes it and Foot IK adds it to the budget. Anything
  else leaves it 0 and behaves as before.
- **"A lower tread" no longer requires the other foot to be planted.** On a
  staircase that was a deadlock: with both feet hovering, neither could be the
  support that lets the other reach. A confident descent is now a second way to
  answer it.

Airborne motion follows physics directly and the filter resumes on contact, so a
landing onto a raised surface cannot reintroduce the one-frame jump.

## Seeing the raycasts

Every correction the solver makes comes from a ray, and a ray is the one thing
the screen cannot show. From outside, a foot left in the air looks identical
whether the ray **missed**, found a surface the **gates then refused**, or found
one the solver deliberately **lifted the shoe over** - three different bugs with
three different fixes. *Preferences > Build > Show Foot IK probes* (debug profile)
draws them, in the game, where they were cast:

| What you see | What it is |
|---|---|
| dim cyan segment | the search window a ray covered, ending where it stopped |
| whole window in **red** | the ray came back with nothing - there was no surface in range |
| bright cyan mark | a sole probe's hit: the surface the foot is being placed on |
| yellow mark | an ahead / lip probe's hit - what the swing reacts to |
| magenta mark | a swept shoe-corner hit (the between-frames tunnelling guard) |
| white mark | a candidate of the learned landing fan |
| **green** cross | the leg's target, and this foot is taking weight |
| **orange** cross | the leg's target, and this foot is NOT taking weight |

The crosses are the answer and the rays are the evidence. An orange cross sitting
on a surface a cyan mark already found means the geometry was there and a gate
refused it; an orange cross ABOVE its own probe's hit means the shoe is being
lifted on purpose (toe clearance); red means there was nothing to stand on.

The overlay records as the solver runs and draws at the end of the frame, capped
at 192 probes per frame - a walking biped records about thirty, so the cap is
there for a crowd, and it warns in the game log rather than silently drawing half
the story. With the preference off the recording and the overlay both fold away
at compile time, exactly like the collision-box overlay next to it, and a release
build carries neither.

## Switching it at runtime

The authored per-object switch is the capability; the live gate is
`RuntimeObject::footIkEnabled`. The **Set Foot IK** flow node exposes `enable`,
`disable` and `toggle` exec inputs for any named animated object (or the graph's
owner when its Object field is empty), and scripts can write the same flag.
Disabling immediately restores the raw animation pose and clears planted
targets, filters and landing history, so a later enable starts from the current
pose and terrain instead of snapping back to a contact remembered on an old
stair. Enabling an object whose authored checkbox is off does nothing.

Live Logic can patch a graph that flips the switch (`OP_SetFootIk`). Retuning a
**rig**, however, is baked into the scene tables, so it flips the LIVE chip to
"rebuild" rather than pretending to stream.

## The experimental neural assist

**Neural landing prediction (VU0)** is an optional learned layer *inside* the
solver, not a replacement for the stair logic. A deterministic
20-input, 16-hidden-ReLU, 6-output network consumes:

- the object's planar velocity and the current and previous ankle velocity,
- the sole-to-ground gap, a ground sample 80 ms ahead and the support normal,
- the two slope-removed ahead-probe residuals,
- a filtered gap and vertical velocity, decaying lip memory and swing phase,
- and the **locomotion grade**: the descend and ascend confidences, the filtered
  drop ahead and the reach budget already earned.

It predicts a small horizontal landing residual, contact confidence, extra
stair-clearance intent, early-release intent and how much of the proven descent
reach to spend. **Neural influence** scales every output from 0 to 1; 0 still
evaluates the network for telemetry but applies nothing, which is the honest way
to A/B it against the procedural solver.

The multiply-accumulate work runs in VU0 **macro mode**: it occupies no VU0
micro memory, so it coexists with project VU0 kernels and the experimental
raytraced mirror. There is no model file and no allocator in the loop.

The network cannot create contact or an obstacle:

- the landing residual only re-centres a five-point fan whose candidates, toe
  and heel are all raycast against real collision and scored for support, and
  the chosen point may guide an airborne ankle by at most 16 cm;
- learned clearance may add at most 35% of the authored *Toe clearance*, and
  only after ordinary probes or the swept footprint proved an abrupt lip;
- learned release may only loosen a nearly-rising foot still stuck to a lower
  tread;
- learned reach may firm up a down-reach envelope by at most a quarter, and only
  one a ray already opened.

Step heights, pelvis correction, reach limits and the analytic leg solve stay
fully procedural.

### Retraining

`tools/train-foot-neural.py` (seed 2002, dependency-free) reproduces the
committed weights from domain-randomized synthetic slopes, lips and descents plus
the labelled PCSX2 trajectories in `tools/data/foot-neural-real.csv`. Its
docstring carries the feature and target order.

New real trajectories come from the Foot IK regression runner
(`.claude/skills/tyra-testing/scripts/foot-ik-regression.py`), which builds with
`TYRAX_FOOT_IK_TRACE=1`, drives the game over Remote Pad and converts the
`PLAYERIK` / `FOOTIK` / `FOOTTRAIN` log rows into CSV plus labelled samples. The
landing label is the next verified plant within an 18-frame causal window; the
clearance, release and reach labels **copy the procedural solver's own
raycast-proven decisions**, so a head can only learn to agree with geometry.
Already-planted frames are omitted unless they carry an emergency stair release,
which stops long static contacts from drowning the short useful events.

**Runtime normalization, the runner's labels and the trainer's feature order are
exact twins.** A divisor changed on one side alone silently feeds the network a
different world than it was trained on. A recording made before a feature
existed still loads: missing columns read as 0, which for the grade lanes means
level ground — true of the routes those rows were walked on.

Two generator-only switches, both compiled out of normal builds:
`TYRAX_FOOT_IK_TRACE=1` emits the CSV-shaped trace rows, and
`TYRAX_FOOT_NEURAL_EE=1` swaps in the scalar twin of the inference for parity
runs (a trace build also evaluates both and reports the maximum divergence).

## Costs and limits

- Foot IK is opt-in per instance. Disabled objects keep pose sharing and the
  animation cost they always had.
- The solver runs in the generated game. The editor viewport deliberately keeps
  showing the authored clip, so the raw animation stays easy to inspect.
- Two legs. The data model names six bones; a quadruped would need the leg count
  to become a list, which nothing in the format prevents but nothing implements.

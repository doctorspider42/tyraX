# Foot IK

A walk cycle is authored on a flat floor. Play it on stairs and the feet go
through the concrete on the way up and hang in the air on the way down — the
single most obvious tell that a character is a looping animation rather than
something standing on a world.

Foot IK fixes that at the last possible moment: after the clip has been
sampled, before the mesh is skinned. Each foot traces the world for the
surface actually under it, the pelvis drops far enough that the legs can
reach, and hip/knee/ankle are re-solved analytically onto the result.

On level ground the correction is exactly zero and the clip plays untouched —
that property is what makes the feature invisible when it should be.

## Where it runs

The pose pipeline gained one seam. `SkelInstance::evalPose` used to walk the
hierarchy and build the matrix palette in one breath; now anything installed
through `setPoseHook` runs in between, on the finished model-space skeleton,
before a single vertex has been skinned:

```
sample clips  →  blend crossfade  →  hierarchy walk  →  [POSE HOOKS]  →  palette  →  VU0 skinning
```

Hooks chain (`SkelPoseHook::nextHook`) because a character wants more than
one, and the order matters: the [learned pose corrector](neural-gait.md)
reshapes the stride first, foot IK then plants whatever it produced.

Editing a pose from a hook is two calls. `setPoseGlobal(node, m)` overwrites
one joint's model-space transform and marks its **descendants** as stale;
`refreshPose()` settles every marked subtree from the joints' own local
transforms in one parents-first pass. An edited child overrides its parent's
motion rather than being re-derived from it — which is what lets the solver
move a pelvis and a knee in the same batch and mean both.

## What it costs

Per joint, not per vertex: two traces and a few dozen flops for a biped,
against roughly 0.9 ms of skinning for the same 1092-vertex instance. The
raycast goes through `CollisionMesh`, whose XZ grid keeps the cost
proportional to the triangles near the foot rather than to the model.

The real price is **pose sharing**. `SkelInstance::poseEquals` lets a renderer
skin one instance and draw it for every other instance striking the same pose
— which is how a crowd of NPCs on one clip costs one skin. Two characters
standing on different ground are not in the same pose, so an instance
carrying a hook never joins or lends such a group. That is charged inside
`poseEquals` itself, once, rather than left for each renderer to remember: a
crowd with IK on pays a skin each.

## The solver

`FootIk` (`renderer/3d/mesh/dynamic/skel_foot_ik.hpp`), one per instance,
driven by a `FootIkRig` describing which nodes are legs.

**Trace.** From the clip's own sole position (the ankle, lowered by
`soleOffset` along the object's up), the game is asked what is underneath
within `[traceUp, traceDown]`. A miss means "nothing to stand on" and the
foot keeps the clip's placement — the correct answer over a ledge or a pit,
and the reason walking off a roof does not stretch a leg to the ground below.

**Smooth.** Each foot's correction goes through a critically damped spring
(`smoothing`, 1/s). The ground under a walker is a step function; without
this the foot pops on every tread edge. Critically damped rather than merely
damped because an overshoot on a step input reads as a bounce.

**Drop the pelvis.** The deepest of the per-foot corrections lowers the
pelvis, clamped to `maxDrop`. Without it a character descending stairs does
the splits: the trailing foot reaches for a tread the leg is not long enough
to touch.

**Solve.** Law of cosines on hip/knee/ankle. Two details worth knowing:

- The **bend plane comes from the pose**, not from an authored pole target.
  The knee keeps hinging the way the animator hinged it, so any rig
  convention works and there is nothing to configure. A straight leg has no
  plane of its own and falls back to a hip axis.
- Both bend directions are computed and the one closer to the current thigh
  wins. That is cheaper than reasoning about the cross-product winding of an
  arbitrary rig, and it cannot pick the knee that bends backwards.

A target beyond the leg's reach is pulled onto the reachable sphere rather
than refused: the leg points straight at it, fully extended. The pelvis drop
above exists to make that rare.

**Roll the foot.** The ankle tilts onto the surface normal, scaled by
`normalBlend` and clamped to `maxRollDeg`. The tilt is measured against the
object's own up, so a flat floor is exactly identity and the clip's authored
foot angle survives untouched.

`CollisionMesh::raycast` gained an optional out-normal for this. It is free —
the flat normal is already packed next to the triangle — but note it comes
back with the source mesh's winding, and a triangle soup has no reliable one.
The solver flips it against world up itself.

## What it never does

The solver does not move the character. Its output is what the mesh looks
like; where the body *is* stays the game's own collision answer. So a flow
graph, a trigger volume and the camera all see the same world whether IK is
on, off or half faded — and `setWeight(0)` is a true zero-cost bypass, not a
solver running with small numbers.

## Binding a rig (Tools > Foot IK)

The panel is per **model asset**, because a rig is a property of the skeleton:
every instance of a character shares it. A scene object only carries the
on/off switch (*Properties > Foot IK*), so a rig can be bound once and used on
some instances only — a distant extra is cheaper without it, for the pose
sharing reason above.

**Detect legs from bone names** guesses the whole thing. It recognises Mixamo
(`LeftUpLeg`/`LeftLeg`/`LeftFoot`/`LeftToeBase`), Blender Rigify
(`thigh`/`shin`/`foot`), Unreal (`thigh_l`/`calf_l`/`foot_l`/`ball_l`) and the
plain `thigh`/`knee`/`ankle` spellings, pairs sides by the left/right marker,
and picks the pelvis as the **deepest common ancestor of the two hips** —
whatever it happens to be called.

One naming trap is handled by the hierarchy rather than by the name: a bare
`…Leg` bone is ambiguous, and Mixamo uses `LeftLeg` for the *shin*. It becomes
the knee when it descends from an already-found hip, and the hip otherwise.

A detection never switches the solver on. A guess is a starting point you
confirm, and a solver nobody looked at is how a character ships with a knee
bending backwards.

**measure** reads the sole offset off the model instead of guessing it: the
lowest vertex actually weighted to the foot in the bind pose, against the
ankle joint. A boot and a bare foot differ by centimetres, and centimetres are
exactly what a sunk heel is.

The panel refuses to enable a rig it cannot resolve, and says why — a bone
name that is not in the model, a chain the hierarchy does not connect (the
ankle must descend from the knee, the knee from the hip), or a pelvis that is
not an ancestor of every hip and would therefore not move the legs. The build
skips such a rig too: the character animates exactly as it does today.

From a shell, the same answer without the GUI:

```bash
tyrax-editor --rig-detect <projectDir>
```

It prints, per animated model, the detected (or stored) chains, the pelvis,
the measured sole offset as a percentage of the model's own height, and every
resolution problem — exiting non-zero if any model fails. The percentage is
the quick sanity check: a human ankle sits at roughly 4–6% of standing
height, so a number far outside that means the "foot" bone is not the foot.

## Rig fields

| Field | What it decides |
|---|---|
| `legs[].hip/knee/ankle` | the chain. `toe` only rides along |
| `pelvis` | the node lowered so a leg can reach |
| `soleOffset` | ankle height above the sole, model units |
| `maxLift` / `maxDrop` | how far a foot may rise / the pelvis may sink |
| `normalBlend`, `maxRollDeg` | how much the foot follows the surface |
| `smoothing` | spring rate, 1/s |
| `traceUp` / `traceDown` | the trace window, world units |

Lengths are **model** units — the space the `.tskl`'s vertices live in, the
only scale the rig itself knows. The solver converts to and from world space
through the instance's own matrix, including per-axis scale, so a rotated or
squashed object still plants correctly.

## See also

- [docs/animated-models.md](animated-models.md) — the skeletal runtime this
  hangs off
- [docs/neural-gait.md](neural-gait.md) — the stage that runs before it
- [docs/collision-boxes.md](collision-boxes.md) — what the traces hit

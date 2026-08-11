# foot-ik-stairs

A character that puts its feet where the ground actually is —
[foot IK](../../docs/foot-ik.md) plus a trained [gait net](../../docs/neural-gait.md)
running on VU0.

Open it in the editor and press F5, or from a shell:

```bash
tyrax-editor --build examples/foot-ik-stairs --run
```

## What to look at

The scene is deliberately two shapes and nothing else, because both of them
are cases a walker **cannot** express — a walker has one ground height, and
these need two.

**The kerb.** A slab covering half the world. Stand astride its edge and one
foot is on the slab while the other is on the grass, 0.22 lower. The legs end
up at different heights, the knees at different angles, and the pelvis tips
toward the low side. Turn *Properties > Foot IK* off on the player and the
same pose puts both feet at kerb height, with the outer one hanging in the
air.

**The staircase.** Eight box treads, 0.18 rise. Walk up and each foot lands on
the tread under it rather than on the one the body is standing over — the
walker reads one step ahead of itself, because its footprint test pads by the
player radius and the treads are shallower than that pad.

It is a stack of **Box primitives on purpose**: that is how anybody builds
stairs, and the foot trace handles box colliders precisely so it works. Mesh
colliders work too, and give the foot a real surface normal to roll onto.

## The rig

Bound once for the model in *Tools > Foot IK*. It was not typed in — the
chains came from **Detect legs from bone names** (this is an Unreal-style rig:
`thigh_l` / `calf_l` / `foot_l` / `ball_l`), the pelvis is the deepest common
ancestor of the two hips, and the sole offset was **measured** off the bind
pose rather than guessed.

From a shell, the same answer:

```bash
tyrax-editor --rig-detect examples/foot-ik-stairs
```

Note the numbers are scaled to this character: it is 3.39 units tall, not the
~1.8 the defaults assume, so the trace window and the lift/drop limits are
almost twice the struct defaults. Getting that wrong is what puts feet in the
air on stairs.

## The gait net

`res/models/UAL1_Standard.tnet` (30 KB, 2 hidden layers of 64) reads a 3×3
grid of ground heights *ahead* of the character and rewrites the stride before
the solver plants it — including the playback rate, which is the shortened
step going up a flight that no solver can produce. About **7 300
multiply-accumulates a frame**, ~50 µs on VU0, against roughly 900 µs of
skinning for the same character.

It was trained from a dataset this editor generated itself:

```bash
tyrax-editor --gait-dataset examples/foot-ik-stairs res/models/UAL1_Standard.fbx gait.csv --clip Walk_Loop --frames 30000
python ../../tools/motion-net/train.py gait.csv --out weights.json
tyrax-editor --gait-bake examples/foot-ik-stairs res/models/UAL1_Standard.fbx weights.json
```

To compare the solver alone against solver-plus-net, set `netEnabled` to
`false` in the `animRigs` section of `foot-ik-stairs.tyra` and rebuild.

## Things worth knowing about this scene

- **Walk speed is 0.035**, not the 0.1 default. At the default this character
  covers about 1.8 body-heights a second — a sprint under a walk cycle, and
  the foot sliding drowns out everything the IK is doing.
- **The follow camera is low** (height 0.55, pitch 12) so the legs are in
  shot. It cannot be pointed sideways: `camYaw` is not honoured by the
  third-person follow camera, and `camShoulder` swings the camera out without
  re-aiming it. A proper side view needs a Camera entity driven by the
  Cutscene Director.

## Assets

`res/models/UAL1_Standard.fbx` — Universal Animation Library 2 by
[@Quaternius](https://www.patreon.com/quaternius), released under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) (public domain).
43 clips; this scene uses `Idle_Loop` and `Walk_Loop`.

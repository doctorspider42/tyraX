# vehicle-playground example

A car you can get into and drive, and some things to hit with it. This is the
worked example for [vehicles](../../docs/vehicles.md).

## What is in it

- **A CC96 coupe** imported from a single `res/models/car1.fbx`. Nothing about
  that file was prepared for this: its nodes are called `Cube` and
  `Cylinder.001`–`.003` — Blender defaults — and the importer finds the four
  wheels by **geometry** rather than by name.
- **A walled box** to steer inside, so the wall collision has something to do.
- **A slalom of pillars**, which are cylinders and therefore collide as boxes —
  a useful reminder that the collider is not the mesh.
- **A ramp**, for the suspension and the airborne branch of the drive model.

## Driving it

Walk up to the car and press **USE** (Square by default). The camera moves to a
boom behind the car — there is no second camera rig; the walker's own camera
becomes the chase cam. Press USE again to get out at the driver's door.

| | |
|---|---|
| left stick | steer, and throttle forward/back |
| Cross | throttle |
| L1 | brake |
| Circle | handbrake — this is the drift button |

L1 rather than Square for the brake is deliberate: Square is USE, and a brake
there would throw the driver out on the same press.

## What it costs

The build says so on every run:

```
[vehicle] CC96: body 1072 tris / 1 part(s), wheel 416 tris, 2 submit(s) per vehicle
```

**Two submits per car** is the whole point. The source model is 40 materials and
8780 triangles — 36 mesh parts, and a `.tmdl` part is one bag at roughly 1 ms of
fixed EE time, so the car as authored would be nearly two PAL frames of submit
overhead standing still. The body is one bag on the matrix fast path (VU1 moves
it, the EE touches no vertex) and all four wheels share a second bag rebuilt in
world space each frame.

Tune any of it in *Tools > Vehicle Editor*, and use its **Test drive** tab to
feel a grip change immediately instead of waiting for a Docker build.

## Attribution

The car is the **CC96** model by its author, released under CC0 — see
`res/models/car1-CC0-licence.txt`.

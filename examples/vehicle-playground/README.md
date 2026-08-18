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

## Driving it

Walk up to the car and press **USE** (Square by default). The camera moves to a
lagged boom behind the car;
while driving, the on-foot controls are fully off (no jumping from the driver's
seat). Press USE again to get out at the driver's door.

| | |
|---|---|
| left stick | steer, and throttle forward/back |
| Cross | throttle |
| L1 | brake |
| Circle | handbrake — this is the drift button |
| R1 | nitrous — this car carries three seconds of it |
| Triangle | cycle the camera: chase → bumper → far |

L1 rather than Square for the brake is deliberate: Square is USE, and a brake
there would throw the driver out on the same press.

**Try the bumper cam in a handbrake turn.** The chase camera's boom lags the car,
so a slide reads as the body rotating underneath you; the bumper cam takes the
car's own heading instead, so the same slide throws the whole view sideways. That
contrast is why both exist.

## The gearbox

This car is set up to show the powertrain off rather than to stay neutral:
`gearTorque` 1 (the ratio fully shapes acceleration — first gear pulls, top gear
runs out of breath), `shiftTime` 0.18 (an audible throttle cut between gears) and a
three-second nitrous tank. Both of the first two default to **off** on a fresh
vehicle, precisely so an existing car drives exactly as it did before gears
existed; this example turns them on.

Hold Cross from a standstill and watch `bin/log.txt`:

```
VEH ... spd10 41   gear 0 rpm 7200   ← first gear, on the redline
VEH ... spd10 56   gear 2 rpm 5017   ← changed up, and the engine dropped
VEH ... spd10 170  gear 4 rpm 5770   ← top gear
```

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

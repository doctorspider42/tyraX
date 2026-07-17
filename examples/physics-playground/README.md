# physics-playground

The rigid-body physics stress scene: **28 high-bounce bodies** (boxes +
spheres, restitution 0.88, friction 0.08) rain onto a terraced slope, bounce
around for a good while, roll downhill and finally settle and fall asleep.
This is the exact scene the VU1 fast-path optimization was benchmarked on
(14-16 → 156 FPS mid-flight on the PCSX2 software renderer).

## What's in the scene

- **28 `b*` bodies** dropped from 8-20 units on a stepped slope (high in the
  east). Watch them kick sideways off the terrace edges (real heightfield
  normals), tumble (spin from ground contact) and go still one by one - a
  sleeping body costs one branch per frame, so the settled scene runs at
  full speed again.
- **`kicked-ball`** (yellow): a flow graph kicks it every 3 s with **Apply
  Impulse** (-4, 7, 1.5 units/s) and logs its position to `bin/log.txt`
  (`Get Position → Position To Text → Log Message`) - numeric telemetry of
  the bounces.
- Walk into anything to shove it (push scales with 1/mass); jump on X.

The project ships in the **debug profile with the FPS overlay and vsync
disabled** (`showFps`, `disableVsync`) and **VU1 clipping** (`"clipping":
"vu1"`), so the FPS counter shows real headroom instead of pinning at 50 -
handy for perf work. Timers stay wall-clock true at any frame rate
(`everyFrames` divides by the measured frame time).

## Things to try

- Select a body in TyraX and play with **Mass / Bounciness / Friction /
  Tumble** (Properties, visible while *Physics (rigid body)* is checked).
- Drop the bounciness to 0 on a few bodies and watch them thud dead while
  the rest keep bouncing.
- Open `kicked-ball`'s Flow Graph and retarget the *Apply Impulse* node.
- Watch `bin/log.txt` while the game runs for the kicked ball's positions.

## Build & run

Open the folder in TyraX and press F5, or headlessly:

```powershell
build\tyrax-editor.exe --build examples\physics-playground --run
```

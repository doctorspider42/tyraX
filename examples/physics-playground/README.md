# physics-playground

A minimal demo of the rigid-body object physics: drop, bounce, slide, tumble,
stack, shove.

## What's in the scene

The terrain is a stepped slope (high in the east). Walk around with the left
stick, look with the right, jump on X.

| Object | Material | What it demonstrates |
|---|---|---|
| `bouncy-ball` (red sphere) | bounce 0.85, friction 0.1, mass 0.5 | Drops from 12 units and keeps bouncing, then rolls downhill. |
| `dead-box` (gray box) | bounce 0, friction 0.9, mass 2 | Drops and thuds dead - no bounce, barely slides. |
| `medium-box` (blue box) | bounce 0.5, friction 0.4 | The middle ground: a couple of bounces with tumbling. |
| `crate-1` + `crate-2` (brown boxes) | bounce 0.15, friction 0.7 | Stacking: they land, settle and **sleep**. Shove the lower one and the top crate wakes up and topples. |
| `kicked-ball` (yellow sphere) | bounce 0.6, friction 0.2, mass 0.8 | A flow graph kicks it every 3 s with **Apply Impulse** (-4, 7, 1.5 units/s) and logs its position (`Get Position → Position To Text → Log Message`). |

Walk into any body to push it - the shove impulse scales with 1/mass, so the
heavy `dead-box` barely moves while the balls fly.

## Things to try

- Open the project in TyraX, select a body, and play with **Mass /
  Bounciness / Friction / Tumble** in the Properties panel (visible while
  *Physics (rigid body)* is checked).
- Open `kicked-ball`'s Flow Graph to see the *Apply Impulse* node; retarget it
  at another object with an object link.
- Watch `bin/log.txt` while the game runs: the graph logs the kicked ball's
  position, so you can see the bounces numerically.

## Build & run

Open the folder in TyraX and press F5, or headlessly:

```powershell
build\tyrax-editor.exe --build examples\physics-playground --run
```

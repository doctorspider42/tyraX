# probe-aim example

A crystal ball that shows what's behind you. This scene demonstrates the
**Reflection probe: aim along the reflected ray** preference
(*Preferences > Rendering*; see "Probe aim" in
[docs/reflective-materials.md](../../docs/reflective-materials.md)): the
`@sky` dynamic env map's camera no longer looks level along the player's
forward — it renders **from the point your view ray hits the reflective
surface, along the reflected direction**, smoothed.

Open `probe-aim.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn facing a big chrome ball on a pedestal, a chrome monolith to the
side — a red crate, a yellow ball and a blue pillar standing **behind
you**, and a smaller red ball a few steps **in front**, between you and the
pedestal. Look at the chrome: the props behind your back reflect in the middle
of the ball, like a real mirror. **Each reflective object carries its own
probe** — the ball and the monolith mirror *different* prop subsets in the
same frame, from their own vantage points — and the probe pose depends only
on positions, never on where you point the camera: look hard to the side
and the reflections stay put. Walk and strafe: they slide with correct-ish
parallax.

For contrast, flip the preference off and rebuild — the classic GT3
level-forward aim shows the same props as small distant smudges, because
the probe then renders from YOUR eye, not the surface's.

## How it is wired

- **`chrome-ball` / `chrome-monolith`** — the `chrome-dyn` material
  (`refl ... -rounded @sky`): the dynamic env map sampled by camera-space
  normals on VU1. Each gets its own probe render per frame (the eye→center
  reflection: a sphere's probe looks straight back at you — the
  crystal-ball look-back; a box's reflects off the hit face, which is why
  the monolith is rotated so its reflected cone actually contains the
  props — at its first rotation it honestly mirrored empty sky).
- **`crate-red` / `ball-sun` / `pillar-blue` / `ball-sun-copy`** — plain
  primitives with **Show in reflections** checked: they render into the env
  map every refresh (base passes, z-tested in the map).
- **`envProbeReflected: true`** in the project settings — the whole feature
  is this one preference; the raycast targets are found automatically (any
  object bound to the dynamic map).

## Honest limits

Every reflective object costs **one full probe render per frame** (PATH1
drain + 128² sky + the reflected list) — two objects here, so two probes;
put ten in a scene and it will crawl. The env map contains sky + listed
objects, not the terrain, so ground-facing reflections show the horizon
color, and a 110° probe cannot cover the full reflected hemisphere at
grazing angles.

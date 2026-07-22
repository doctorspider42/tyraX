# probe-aim example

A crystal ball that shows what's behind you. This scene demonstrates the
**Reflection probe: aim along the reflected ray** preference
(*Preferences > Rendering*; see the "Probe aim" section of
[docs/reflective-materials.md](../../docs/reflective-materials.md)): the
`@sky` dynamic env map's camera no longer looks level along the player's
forward — it renders **from the point your view ray hits the reflective
surface, along the reflected direction**, smoothed.

Open `probe-aim.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn facing a big chrome ball on a pedestal, with a chrome monolith
to the side — and a red crate, a yellow ball and a blue pillar standing
**behind you**. Look at the chrome: the props behind your back reflect in
the middle of the ball, like in a real mirror — because the probe traces
your view ray to the surface, reflects it, and renders the scene from
there. Walk and strafe: the reflections slide across the chrome with
correct-ish parallax, and the smoothing keeps them from snapping as your
crosshair moves between the ball and the monolith.

For contrast, flip the preference off (*Preferences > Rendering*) and
rebuild — the classic GT3 level-forward aim shows the same props as small
distant smudges, because the probe then renders from YOUR eye, not the
surface's.

## How it is wired

- **`chrome-ball` / `chrome-monolith`** — the `chrome-dyn` material
  (`refl ... -rounded @sky`): the dynamic env map sampled by camera-space
  normals on VU1. The ball's equator sits at eye height on purpose — a
  level view ray reflects straight back, so the demo shot mirrors what's
  behind the player.
- **`crate-red` / `ball-sun` / `pillar-blue`** — plain primitives with
  **Show in reflections** checked: they render into the env map every
  refresh (base passes, z-tested in the map).
- **`envProbeReflected: true`** in the project settings — the whole
  feature is this one preference; the raycast targets are found
  automatically (any object bound to the dynamic map).

## Honest limits

One probe serves every reflective surface — the one under your crosshair
gets the accurate aim, the rest inherit it (watch the monolith while
staring at the ball). The env map contains sky + listed objects, not the
terrain, so ground-facing reflections show the horizon color. Cost is
unchanged versus the classic aim: same env render, plus a handful of
ray-primitive tests per refresh.

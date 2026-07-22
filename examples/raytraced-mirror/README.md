# raytraced-mirror example

Yes, actual ray tracing on the PS2. The big glass wall in this scene is a
**raytraced Mirror**: its reflection is traced per pixel, every frame, on a
**VU0 microprogram** — sphere proxies of the listed objects against the
scene's sky gradient, re-uploaded to the GS as a texture the glass samples.
See [docs/raytraced-reflections.md](../../docs/raytraced-reflections.md)
for how (and why) this works.

Open `raytraced-mirror.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You stand on a gray slab facing the mirror wall between two pillars. Four
colored balls float in front of it — walk around (left stick / WASD-bound
pad) and watch the glass: every ball reflects as a real traced sphere with
its own lambert shading, **your reflection follows you** (the *Reflect
player* switch — the beige blob is you), and the whole image re-traces from
your actual eye position every frame, so the parallax is correct as you
move. The yellow ball floats high — you can see both the ball itself
against the glass and its traced reflection below it.

## How it is wired

- **`mirror`** — a Mirror object with **Raytraced (VU0, experimental)**
  checked, *Reflection resolution* **128 × 128** and *Reflect player* on.
  Its reflected-objects list carries the four balls. The glass draws the
  traced texture opaque — in RT mode the opacity slider is ignored.
- **`ball-red` / `ball-green` / `ball-blue` / `ball-sun`** — plain sphere
  primitives; sphere proxies match sphere objects exactly, so these reflect
  true to shape. (Try adding a pillar to the list to see the PoC trade:
  every proxy is a sphere, so a cylinder reflects as a ball.)
- **`floor`** — a thin box slab, NOT in the mirror list: the traced scene
  has no synthetic ground, misses shade from the sky gradient, and real
  floor geometry is simply scene dressing.
- **`pillar-left` / `pillar-right`** — framing only, also unlisted.

## Things to try

- *Properties > Mirror > Reflection resolution*: **32** is the honest
  PS1-flashback tier, **128** (this scene) holds a solid frame rate,
  **256/512** are photo modes — the cost scales with the square of the
  edge, 512 traces a quarter million rays per frame and crawls, but the
  512×512 reflection is glorious.
- Move a ball in the editor and rebuild — reflections read live positions,
  so anything that moves at runtime (physics, flow graphs) moves in the
  glass too.

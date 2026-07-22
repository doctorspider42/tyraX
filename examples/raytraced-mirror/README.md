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
  Its reflected-objects list carries the four balls and the floor. The
  glass draws the traced texture opaque — in RT mode the opacity slider is
  ignored.
- **`ball-red` / `ball-green` / `ball-blue` / `ball-sun`** — plain sphere
  primitives; sphere proxies match sphere objects exactly, so these reflect
  true to shape. (Try adding a pillar to the list to see the PoC trade:
  curved shapes proxy as spheres, so a cylinder reflects as a ball.)
- **`crate`** — a textured `.obj` model (`res/models/crate.obj`), also
  listed: static models trace as REAL TRIANGLE MESHES with their texture —
  the kernel returns barycentric hits and the EE samples `crate.png` per
  texel. The crate is 12 triangles, under the proxy budget, so its
  reflection is the exact mesh — rotated 25° like the object itself
  (triangle proxies honor rotation; slabs and spheres don't).
- **`wobbler`** — an ANIMATED `.glb` (the Twist clip, autoplaying), also
  listed: skeletal models reflect as a coarse 18-triangle medoid mesh
  whose corners read the LIVE skinned vertices every frame — watch the
  glass, the reflection twists along with the model. Untextured, so it
  shades with its material color under the tracer's lambert.
- **`floor`** — a thin box slab, listed too: flat objects (boxes, planes,
  decals) trace as **axis-aligned slab proxies**, so the floor reflects as
  an actual floor under the balls. (A flat object as a bounding *sphere*
  would engulf the glass and never show — that asymmetry is why the kernel
  has two proxy types.)
- **`pillar-left` / `pillar-right`** — framing only, unlisted.
- **`cctv-cam` + `cctv-screen`** — a Camera entity with **Render to
  texture (CCTV feed)** watching the scene from up high, and the billboard
  above the mirror showing its view live (*Properties > Texture feed >
  camera: cctv-cam*) — walk around and watch yourself on the big screen.
  See [docs/texture-feeds.md](../../docs/texture-feeds.md).
- **`mirror-screen`** — the floating monitor on the left streams the
  raytraced mirror's traced image as a texture (*Texture feed >
  mirror: mirror*): the VU0 raytracer's output re-used as a live material.

## Things to try

- *Properties > Mirror > Reflection resolution*: **32** is the honest
  PS1-flashback tier, **128** (this scene) holds a solid frame rate,
  **256/512** are photo modes — the cost scales with the square of the
  edge, 512 traces a quarter million rays per frame and crawls, but the
  512×512 reflection is glorious.
- Move a ball in the editor and rebuild — reflections read live positions,
  so anything that moves at runtime (physics, flow graphs) moves in the
  glass too.

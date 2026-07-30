# texture-feeds example

Two ways to stream a live render onto an ordinary surface — the 2002
security-monitor set piece. Both monitors on the far wall are plain boxes
with a **Texture feed** (*Properties > Texture feed*); see
[docs/texture-feeds.md](../../docs/texture-feeds.md).

Open `texture-feeds.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You face a crate and three balls, with a big **raytraced mirror wall**
behind them and two floating monitors above it. Both monitors show a live
render every frame:

- the **left** monitor is a **camera feed** — a Camera entity tucked in the
  corner renders the scene (sky + terrain + the listed props) into a
  128×128 texture; walk around and the little screen tracks the whole
  stage from its own angle, terrain and all;
- the **right** monitor **streams the raytraced mirror's image** — the same
  VU0-traced reflection you see on the glass wall, re-used as a texture.
  Note it shows the props as proxies on a **plain sky** (no terrain) — that
  is the tell-tale of the raytracer versus the camera feed.

## How it is wired

- **`feed-cam`** — a Camera with **Render to texture (CCTV feed)** checked,
  `feedObjects` = the crate + three balls, terrain on. One feed camera per
  scene.
- **`monitor-cam`** — a thin box, *Texture feed → camera: feed-cam*. Draws
  the camera's view emissive over its face.
- **`rt-mirror`** — a raytraced Mirror wall (the same object type as the
  [raytraced-mirror](../raytraced-mirror) example), reflecting the props.
  It is both a visible mirror AND the source for the second monitor.
- **`monitor-mirror`** — a thin box, *Texture feed → mirror: rt-mirror*:
  the raytracer's output as a live material, for free (the traced texture
  already exists).
- **`crate` / `ball-*`** — the props, shared by the camera's view list and
  the mirror's reflection list.

## Honest limits

The camera feed is a real second scene render (bounded by its object list)
at 128², every frame — comparable to the `@sky` dynamic env-map pass;
one feed camera per scene, any number of surfaces may show it. The mirror
stream is free (it reuses the raytracer's texture). Feed surfaces are
excluded from static batching, and the target samples emissive (screens
glow rather than shade). PS2-only — the editor viewport shows the base
material.

The two sibling examples: [raytraced-mirror](../raytraced-mirror) (the VU0
raytracer on its own) and [probe-aim](../probe-aim) (the reflected-ray
env-map probe).

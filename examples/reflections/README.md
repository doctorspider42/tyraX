# reflections — sphere-mapped chrome, both flavors

The PS2-era reflective-material trick (NFS car paint, GT chrome), shown in
its two modes side by side:

| Object | Material | Mode |
| --- | --- | --- |
| `chrome-static` (left sphere) | `res/materials/chrome.mtl` | **Static sphere map** — `chrome-sky.png`, a hand-made sky gradient with a hot horizon band |
| `chrome-live-sky` (middle sphere) | `res/materials/chrome-dyn.mtl` | **Dynamic (`@sky`)** — the game re-renders the scene's sky dome into a 128×128 VRAM texture every frame |
| `car-paint-live` (right sphere) | `res/materials/paint-dyn.mtl` | Dynamic at lower strength over a dark blue base — the "car paint" look |
| `chrome-box` | `res/materials/chrome.mtl` | Static map on flat faces (per-face reflection) |
| `matte-control` | *(none)* | Control object — no reflection pass |

## What to look at

- Orbit the camera (right stick): the highlight **slides across the spheres**
  as the view changes — that's the matcap ST computed per vertex on VU1 from
  the camera basis.
- The **static** sphere always reflects its PNG (white band, warm ground).
  The **dynamic** spheres reflect the scene's *live* sky — retint the sky
  (a *Set Sky Color* flow node or `ctx.skyColor` from a script) and watch
  the reflections follow while the static sphere doesn't care.
- The matte box proves the pass is per-material.

## How it's authored

Everything is plain Material Editor work (*Tools > Material Editor*): the
**Reflection** section picks a sphere-map PNG or `<dynamic - live sky>` plus
a strength. The `.mtl` files store it as the standard `refl` statement:

```
refl -type sphere -mm 0 0.8 chrome-sky.png   # static
refl -type sphere -mm 0 0.9 @sky             # dynamic
```

Full guide: [docs/reflective-materials.md](../../docs/reflective-materials.md).

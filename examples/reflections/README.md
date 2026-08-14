# reflections — a chrome showroom you walk through

First-person scene demonstrating the PS2-era reflective-material trick
(NFS car paint, GT chrome) in both modes — and proving the dynamic mode
really reflects the *live* scene: a **sky cycler** flips the sky to a sunset
every 14 seconds (and back after 7), and only the dynamic surfaces follow.

## The tour (walk forward from the spawn)

| What | Material | Mode |
| --- | --- | --- |
| Avenue, **right** pedestals | `chrome.mtl` | **Static sphere map** — `sunset-sky.png` (indigo→violet→blazing horizon streak); never changes |
| Avenue, **left** pedestals | `chrome-dyn.mtl` | **Dynamic `@sky`** — the game re-renders the scene's sky dome into a 128×128 VRAM texture every frame |
| The tall **mirror monolith** | `chrome-dyn.mtl` | Dynamic, on a big flat slab |
| Three big spheres at the end | `paint-red/blue/black.mtl` | Dynamic at lower strength over colored bases — the "car paint" look |
| Red box at the spawn | *(none)* | Matte control — no reflection pass |

## What to look at

- **Wait ~14 s**: the sky turns sunset-orange — every `@sky` surface follows
  instantly (the reflections are re-rendered each frame), while the static
  sunset spheres don't care. 7 s later the day sky returns. That's the whole
  point of the dynamic mode in one glance.
- **Walk and look around**: the highlight slides across every sphere — the
  matcap ST is computed per vertex on VU1 from the camera basis.
- **Find the red box in the chrome**: the matte control box and the red/blue
  paint spheres are marked *Show in reflections* (Properties), so the
  dynamic surfaces mirror them — walk between them and watch the reflections
  move.
- The sky cycler is a plain flow graph on the `sky-cycler` Empty:
  *Every N Seconds (14)* → *Set Sky Color (sunset)*, and in parallel
  → *Delay (7)* → *Set Sky Color (day)*. (Built-in action nodes don't chain
  exec onward — wire the Delay directly to the trigger.)

## How it's authored

Plain Material Editor work (*Tools > Material Editor*): the **Reflection**
section picks a sphere-map PNG or `<dynamic - live sky>` plus a strength.
The `.mtl` files store the standard `refl` statement:

```
refl -type sphere -mm 0 0.9 sunset-sky.png     # static
refl -type sphere -mm 0 0.9 -rounded @sky      # dynamic
```

Full guide: [docs/reflective-materials.md](../../docs/reflective-materials.md).

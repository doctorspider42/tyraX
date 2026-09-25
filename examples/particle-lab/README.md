# particle-lab

The particle library's reference scene ([docs/particles.md](../../docs/particles.md)):
every particle in it comes from six effects made once in *Tools > Particle
Editor*, each with a texture the editor generated.

![particle-lab on the PS2 renderer](../../docs/img/particles-lab.png)

## What's in the scene

| effect | used by | what it shows |
|---|---|---|
| **Campfire** | `campfire` | Fire motion, **additive**, a generated *Flame* **flipbook** (4 frames of 64x64 at 10 fps) |
| **Embers** | `campfire-embers` | tiny additive glows rising out of the fire and dying |
| **Fire glow** | `campfire-glow` | a few big, faint additive halos that make the fire warm the air around it |
| **Wood smoke** | `campfire-smoke`, `chimney-smoke` | one Custom effect (rising, growing, low opacity) used by TWO emitters - edit it once, both change |
| **Torch sparks** | `torch-sparks` | Sparks motion, additive, a generated *Glow* texture (32x32) |
| **Magic motes** | `magic-motes` | a slow buoyant Custom cloud, additive cyan glow |

The emitters are linked by name (*Properties > Particle emitter > Library*);
their own knobs are greyed out and show the effect's values. The generated
textures live in `res/materials/particles/`.

## Things to try

- Open *Tools > Particle Editor*, pick **Wood smoke** and raise *Grow* - the
  campfire's smoke and the chimney's both thicken.
- Change **Campfire**'s *Generate* recipe (turbulence, heat, seed) and release
  the slider: the flame texture is re-baked and the viewport updates.
- Untick *Additive* on Campfire to see why fire wants it.
- **New effect... > Smoke**, then **Place in scene**.

## Build & run

```bash
tyrax-editor --bake-particles examples/particle-lab   # re-bake the textures (optional)
tyrax-editor --build examples/particle-lab --run
```

The player spawns facing the fire; nothing needs a pad.

# day-night — the same place four times, and the light does all the work

![Four scenes of one arch and a row of pillars: dawn under a mauve sky lit from
the east, noon under blue with short shadows, dusk under a red sky lit from the
west, and night in cold moonlight under stars.](../../docs/img/day-night-example.png)

Four scenes. **Identical geometry in every one** — the same arch, the same five
pillars, the same ball and drum, the same wall, the same terrain, down to the
object positions. The only difference between them is one number: the hour on the
[day/night cycle](../../docs/day-night-cycle.md).

Press **Triangle** to step to the next moment: dawn → noon → dusk → night → dawn.

## What to look at

| Where | What it shows |
| --- | --- |
| The **arch pillars** at dawn vs dusk | Lit on *opposite faces*. The light's compass bearing goes from 83° to −113° — a 196° swing — and nothing in the scene moved. |
| The **shadows on the ground** | 3.68× the caster's height at dawn and at dusk, 0.54× at noon. That is `1/tan(elevation)` for a light at 15.2° and at 61.8°, which is where the cycle's arc puts the sun at those hours. |
| The **ground colour** | Dark green at dawn, bright green at noon, dark again at dusk, cold blue-grey at night — the *baked* vertex shading, not a filter over the frame. |
| The **sky** | Mauve, blue, red, near-black. Horizon and zenith are separate colours and both come from the keyframe list. |
| **Look up at night** (right stick) | The moon, textured from NASA's LRO map at phase 0.62, and ~520 stars in real colours — blue-white, white, and a warm orange one or two. |
| The **fog** | Tinted per moment to match its sky, so the far edge of the terrain never disagrees with the horizon behind it. |
| **Noon vs night** | The moon disc is not drawn at noon and the sun is not drawn at night. A body more than 10° below the horizon is skipped. |

## Where the four moments come from

Four **ambience presets** — `dawn`, `noon`, `dusk`, `night` — carrying the *same*
cycle: the same sun and moon arcs, the same nine colour keyframes, the same
starfield seed. They differ only in `cycle.time` (7.2 / 12.0 / 17.3 / 22.5). Each
scene names one in *Scene > Preferences*.

That is the whole authoring model: a preset is a scene's mood bundle and a scene
already picks one, so **"which preset" doubles as "which time of day"**. There is
no per-scene lighting set by hand anywhere in this project.

`project::resolvedSettings` evaluates the cycle at that hour, and every consumer
downstream reads the result without knowing a cycle exists — the baked vertex
shading, the ambient-occlusion bake, the codegen'd `SCENE_LIGHT_*`, and from
there the projected silhouette shadows, the lens flare and the god rays.

## Why the shadows swing at all

Two different shadow systems are on, and they answer to the same light:

- **Baked contact shadows** (*Properties > Cast shadow*, on by default) — the
  per-pixel AO lightmap. This is what darkens the ground where a pillar meets it
  and the inside corners of the arch.
- **Projected silhouette shadows** (*Properties > Projected shadow*, **off** by
  default, on for every caster here) — the long directional shadow rendered from
  a light camera each frame. This is the one that visibly swings.

Turning `projShadow` on is the difference between "the pillars are grounded" and
"it is clearly morning". If you copy this example as a starting point, that is
the flag to remember.

## What it costs

**50 FPS / 100 % speed** in every moment, looking at the horizon and looking
straight up, with the 520-star field on. Four resident textures (the sun disc,
the moon disc, the shared star/beam corona, the AO atlas), no eviction and no
re-upload over thousands of frames.

The night sky is three submits: one additive bag per magnitude tier. The stars
fade in at dusk through those bags' additive FIX, so the transition costs three
byte writes a frame rather than any geometry work.

## Things you can try

- **Move a moment.** Open *Tools > Ambience Editor > Day / night*, pick a preset
  and drag the time slider. The viewport re-lights live, and the next build bakes
  it. Try dusk at 18:20 instead of 17:18 and watch the shadow stretch.
- **Change the moon.** Its phase is a slider; the disc is re-baked from NASA's
  colour map with a real elliptical terminator. Or import your own texture.
- **Reroll the sky.** A new star seed is a completely different sky; the same
  seed with a nudged *Milky Way* or *Magnitude spread* slider is the *same* sky,
  adjusted.
- **Turn bloom up.** The star cores and the low sun are sized to feed the bloom
  bright-pass — *Preferences > Post effects*, `Threshold` around 0.6.
- **Bake global illumination** (*Ambience Editor > Global illumination*). It is
  off here, and each hour caches separately because the light direction is part
  of the bake signature — so four scenes means four bakes, which is exactly the
  trade-off the "moving cycle" memo in the docs works through.

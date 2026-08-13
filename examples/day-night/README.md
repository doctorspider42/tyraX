# day-night — the same place five times, and the light does all the work

Five scenes with **identical geometry in every one** — the same arch, the same
five pillars, the same ball and drum, the same wall, the same terrain, down to
the object positions. The only difference between them is the hour on the
[day/night cycle](../../docs/day-night-cycle.md) — and, in the last one,
whether that hour stands still.

![Four scenes of one arch and a row of pillars: dawn under a mauve sky lit from
the east, noon under blue with short shadows, dusk under a red sky lit from the
west, and night in cold moonlight under stars.](../../docs/img/day-night-example.png)

Press **Triangle** to step to the next moment: dawn → noon → dusk → night →
**live** → dawn.

The fifth scene, `live`, is the other half of the
[hybrid](../../docs/day-night-cycle.md): same geometry again, but the **game
advances the clock** — **one real second per in-game hour**, so a whole day
takes 24 seconds, and it opens at 17:00, a minute and a half before sunset. Its
lighting is baked at noon and never re-baked; what carries it from a blue
afternoon through a red sunset into a moonlit night is the sun and moon moving,
the sky and fog retinting, the shadows sweeping, and a per-frame drift grade.
Stand still and watch days go by.

Its sun and moon also **cross the frame you are already looking at**. The first
four scenes lean their arc over the camera's shoulder — fine for the shadows
they exist to show, but the bodies themselves are never on screen. `live` leans
the arc toward the spawn's view instead (`azimuth` 315, peaking 28 degrees up),
so the sun rises into frame on the left, crosses, and leaves to the right, and
the moon retraces the same arc half a day later. Measured against the spawn's
frustum (±30 degrees vertical, ±37.6 horizontal at the engine's 60-degree FOV):
each body is on screen for **5.2 of the 12.5 hours it is up — 42 %** — the sun
from 6.6 to 11.8, the moon from 18.6 to 23.8. With the arcs the other scenes
use, both were on screen for **0 %** of their time up.

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
| The **`live`** scene | The arch is baked at noon with neutral light and still goes warm at sunset and cold blue at night — measured 120,119,114 → 106,75,59 → 24,26,36 while its vertex colours never change. That is the drift grade. |
| The **sunset in `live`**, about 1.5 s after it starts | The cast shadows **dissolve** as the sun reaches the horizon, and the moon's fade in behind them. They never jump to the other side of the caster: the light direction swaps bodies at the middle of twilight, and the shadow it throws is at 0.03 % opacity when it does. Console-logged: 167.97 degrees of direction change at a shadow fade of 0.0002. See [the handover](../../docs/day-night-cycle.md#sun-and-moon-arcs). |

## Where the moments come from

Five **ambience presets** — `dawn`, `noon`, `dusk`, `night`, `live` — sharing
the same nine colour keyframes and the same starfield seed. The first four also
share one sun and moon arc and differ *only* in `cycle.time` (7.2 / 12.0 /
17.3 / 22.5), which is the point they exist to make. `live` starts at 17.0,
bakes at 12.0, lets the clock run at a second per hour, and is the one preset
with its own arc — the one that puts the two bodies in front of the camera.
Each scene names one in *Scene > Preferences*.

That is the whole authoring model: a preset is a scene's mood bundle and a
scene already picks one, so **"which preset" doubles as "which time of day"**.
No per-scene lighting is set by hand anywhere in this project.

`project::resolvedSettings` evaluates the cycle at that hour, and every
consumer downstream reads the result without knowing a cycle exists — the
baked vertex shading, the ambient-occlusion bake, the codegen'd
`SCENE_LIGHT_*`, and from there the projected silhouette shadows, the lens
flare and the god rays.

## Why the shadows swing at all

Two shadow systems are on, answering to the same light:

- **Baked contact shadows** (*Properties > Cast shadow*, on by default) — the
  per-pixel AO lightmap. This darkens the ground where a pillar meets it and
  the inside corners of the arch.
- **Projected silhouette shadows** (*Properties > Projected shadow*, **off** by
  default, on for every caster here) — the long directional shadow rendered
  from a light camera each frame. This is the one that visibly swings.

Turning `projShadow` on is the difference between "the pillars are grounded"
and "it is clearly morning". If you copy this example as a starting point, that
is the flag to remember.

The silhouettes also **fade with how low the light is**, because the patch they
land on can only be so big (3.5x the caster) and a shadow from a light near the
horizon is far longer than that. Full strength above 16 degrees of elevation,
ramping to nothing at the light's 5-degree floor — so in `live` they are strong
from mid-morning to late afternoon, thin out as the sun drops, and the same
again under the moon. They used to be switched off outright below ~14.5
degrees, which on this scene's deliberately shallow arc meant four hours at a
time with nothing on the ground.

## What it costs

**50 FPS / 100 % speed** in every moment — including a whole running day in
`live` — looking at the horizon and looking straight up, with the 520-star
field on. Four resident textures (the sun disc, the moon disc, the shared
star/beam corona, the AO atlas), no eviction and no re-upload over thousands of
frames.

The night sky is three submits: one additive bag per magnitude tier. The stars
fade in at dusk through those bags' additive FIX, so the transition costs three
byte writes a frame rather than any geometry work.

## Things you can try

- **Move a moment.** Open *Tools > Ambience Editor > Day / night*, pick a
  preset and drag the time slider. The viewport re-lights live, and the next
  build bakes it. Try dusk at 18:20 instead of 17:18 and watch the shadow
  stretch.
- **Change the moon.** Its phase is a slider; the disc is re-baked from NASA's
  colour map with a real elliptical terminator. Or import your own texture.
- **Reroll the sky.** A new star seed is a completely different sky; the same
  seed with a nudged *Milky Way* or *Magnitude spread* slider is the *same*
  sky, adjusted.
- **Turn bloom up.** The star cores and the low sun are sized to feed the bloom
  bright-pass — *Preferences > Post effects*, `Threshold` around 0.6.
- **Change the day length.** `live` runs a 24-second day (a second per in-game
  hour) — fast enough to watch every part of the cycle without waiting, fast
  enough that the sun visibly moves. Slow it to a few minutes for a screenshot
  series, or to ten if it should read as ambience rather than a demo — *Day
  length* on the Day / night tab.
- **Turn the drift grade off** on the `live` preset and look at midnight. That
  is what the grade is for, and the clearest way to see it.
- **Bake global illumination** (*Ambience Editor > Global illumination*). It is
  off here, and each hour caches separately because the light direction is part
  of the bake signature — four scenes means four bakes, exactly the trade-off
  the "moving cycle" memo in the docs works through.

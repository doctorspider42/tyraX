# Day / night cycle

*Tools > Ambience Editor > **Day / night***

A time-of-day slider from midnight to midnight. The sun and the moon travel
authored arcs across the sky, a list of keyframes says what the light and the
sky look like at each hour, and the hour you leave the slider on is the hour the
scene is **built** at.

It is not a screen tint. The resolved light direction is the one the vertex
shading, the ambient-occlusion bake, the global-illumination bake and its probe
grid, the runtime projected shadows, the blob shadows, the lens flare and the
god rays are all built from. Move the slider from noon to five in the afternoon
and every contact shadow in the scene leans east.

The cycle can be **static or live**. Static, the authored `time` is baked and the
game does not advance it - which is the whole of "which preset = which time of
day". Live (*Let the clock run*), the game advances the hour and everything that
costs nothing per frame follows: see "The hybrid" below.

## Where it lives

A cycle belongs to an **ambience preset** (`DayCycle` on `AmbiencePreset`,
[src/ambience.hpp](../src/ambience.hpp)). A preset is already a scene's mood
bundle and a scene already picks one, so "which preset" doubles as "which time
of day" - two presets of the same place at different hours is the natural way to
author a morning level and an evening one.

## The one hook

Everything above follows from a single overlay in
`project::resolvedSettings` ([src/project.cpp](../src/project.cpp)), the funnel
every consumer of scene-visual settings already reads:

```cpp
if (a.cycle.enabled) {
    const ambience::Resolved d = ambience::evaluate(a.cycle, a.cycle.time);
    // ... writes skyColor, skyTopColor, lightDir, lightColor,
    //     ambient, diffuse, brightness, fogColor
}
```

Nothing downstream knows a cycle exists. `aobake`, `gibake`, the
`SCENE_LIGHT_*` codegen and the viewport keep reading the same
`ProjectSettings` fields they always did.

One consequence worth knowing: **moving the time stales the GI bake.** That is
correct, not a bug - `lightDir` is part of the bake's cache signature
(`gibake.cpp`), so each authored hour caches separately and re-baking is what
gives you that hour's bounce light.

### Looking up in the editor

The viewport is an orbit camera and its pivot normally sits on the ground, so
until recently the pitch was clamped to keep the eye *above* the pivot - the sky,
and with it the sun, the moon and the whole night sky, was simply unreachable
while the game could look wherever it liked.

Dragging up now tilts past the horizon, and the camera **passes through the
terrain** like any other DCC camera. An intermediate version lifted the pivot to
keep the eye above ground; it was worse than the problem, because the camera
appeared to lie on the terrain and then jump upward. To put the sky in frame,
raise the pivot yourself — pan, or the **right+middle drag** which pans forward
and back along the view direction (`Viewport::dolly`) and climbs while you are
looking up.

The bound is +/-85.9 degrees rather than 90: `camView`'s up vector is world +Y,
so at exactly 90 it aligns with the view axis, the basis degenerates and yaw
stops meaning anything.

### The preview previews a PRESET, not the scene

The viewport shows whichever preset is selected in the Ambience Editor while the
window is open, and goes back to the **scene's own** preset when it closes. If
those two differ, closing the window looks exactly like the edit being thrown
away — it is not: the value is in the model and the title bar shows the project
unsaved until you save it.

The Day / night tab says so directly: next to the preset name it prints
**"(not this scene's)"** with a *Use for this scene* button when the active scene
resolves to a different preset. Editing the preset the scene actually uses shows
no such warning, and closing the window changes nothing.

## The arcs

Each body rides a great circle. It crosses the horizon at compass bearing
`azimuth` (0 = +Z, 90 = +X) and the circle leans `tilt` degrees off the zenith,
so the peak elevation is `90 - tilt`. Between `sunrise` and `sunset` the sun is
above the horizon; the rest of the day it continues the same circle underneath.
The moon uses its own bearing and tilt, shifted `moonOffset` hours behind the
sun - 12 puts it up exactly while the sun is down.

`ambience::arcDirection` is that formula, and it is the only copy: codegen bakes
`SCENE_SUN_*` / `SCENE_MOON_*` from it, the viewport places its discs from it,
and the editor's readout prints it.

### Two rules the math keeps, and why

**The baked light never dips below +5 degrees** (`kMinLightElevation`). A light
exactly at the horizon gives flat ground zero diffuse, and one below it lights
the world from underground - both read as a broken bake rather than as night.
Darkness comes from the authored key colours instead.

**The handover sweeps over the zenith.** At the crossover the sun and the moon
are near-opposite (a full moon rises as the sun sets), so blending the two
direction vectors cancels to zero and the normalized result snaps: measured as a
180-degree flip of every shadow between 17:59 and 18:01. `evaluate` adds a
zenith term weighted `4·w·(1-w)`, which is 1 exactly where the cancellation
would be and 0 at both ends, so the light walks from one horizon up over the top
and down to the other. That is also the honest answer physically - at twilight
the dominant light really is the sky overhead. Measured effect on the default
cycle: worst light-direction step per minute fell from 1.99 to 0.078.

## Keyframes

A free list of `DayKey`s: hour, sky horizon and zenith, light colour, ambient,
diffuse, brightness, fog colour, and star brightness. They interpolate
**cyclically**, so the last key of the day carries through midnight into the
first.

The trap the default seed exists to avoid: keys interpolate *linearly*, so a
lone midnight key ramping to a 06:00 dawn makes 03:00 read as half-sunrise - a
pink sky in the dead of night. Hold a colour with **two** keys (00:00 and 04:30)
and the warm ramp stays in the hour either side of the horizon. *Seed a default
day* writes a nine-key set built that way.

## The sun and the moon in the sky

Two textured quads on the sky dome, one submit each, placed in the baked
directions and sized by `tan(size/2)` of the dome radius. A body more than 10
degrees below the horizon is skipped, so a setting sun slides out of view rather
than vanishing whole.

They blend differently, and it is not a style choice:

- The **sun is additive** (`Cs*FIX + Cd`), which is what makes it read as a
  light source and what gives the bloom pass a bright spot to flare. An additive
  bag never reads texture alpha, so `res/hud/sun-disc.png` carries its shape in
  RGB - the same rule as `flare-corona.png`. It is tinted by the scene's light
  colour, so a red sunset sun is red with nothing extra authored.
- The **moon is alpha-blended**. It is a lit rock; adding it to the sky would
  make the night glow through it.

Both are drawn per view - the main camera, mirrors, portals and camera feeds
each get them oriented for their own eye (`renderSkyBodies(eye, look)`).

### The moon texture

`res/hud/moon-disc.png` is baked at 128x128 by `menubake::bakeMoonRGBA`: the
near side of the sphere projected orthographically out of an equirectangular
albedo map, with the phase applied as a terminator and the limb antialiased.

The terminator is modelled as the illumination of a real sphere, so it comes out
as an **ellipse** rather than a straight cut - a straight cut is the tell-tale of
a fake crescent. A trace of earthshine keeps a thin crescent showing a faint full
disc instead of an amputated shape.

The default source is **NASA's Lunar Reconnaissance Orbiter colour map**
(`resources/moon-lroc-color-1k.jpg`, 1024x512, public domain - see
[THIRD-PARTY-LICENSES.md](../THIRD-PARTY-LICENSES.md)), embedded into the editor
binary by `cmake/embed_binary.cmake`. **The map never ships to the PS2** - only
the small baked disc does. Import your own and a 2:1 image is projected the same
way; anything else is used as the disc face directly.

The bake is the single source: `refreshGenerated` PNG-encodes it for the
console, and the editor viewport uploads the *same pixels* while the phase
slider moves. There is no separate preview-quality moon.

**Opacity** (`moonOpacity`) is applied when the disc is *drawn*, never baked into
the texture — so it is a slider you can drag with no re-bake, and one texture
serves every value. The console gets it for free: the moon is an alpha-blended
bag, so its **vertex colour alpha** is its opacity. Below 1 the sky shows
through, which is what a moon behind thin cloud or a daytime moon looks like; at
0 the quad is skipped entirely rather than submitted invisible.

The editor had a bug here worth recording, because it was **editor-only** and so
invisible to every console test: the viewport's shader emits a flat alpha of 1.0
unless `uAlpha` is set, and `drawSkyBodies` was not setting it. The moon's
transparent margin therefore blended as *opaque black* — a rotated black square
around the disc, rotated because `moonRoll` rotates the quad. The PS2 side never
had the problem because it blends through the GS alpha test and has no
flat-alpha path. Measured after the fix: the ring of quad outside the disc reads
14..16 against a sky of 14, i.e. gone, at every opacity.

### VRAM

64x64 + 128x128 at 32bpp = 6 144 + 18 432 words, about **8.7 % of the ~1.08 MB
texture heap** ([gs-vram.md](gs-vram.md)). Paid only by projects with a cycle:
`DAYCYCLE_USED` gates the texture load and matches the `projectUsesDayCycle`
predicate that bakes the PNGs, the way `FLARE_USED` matches `projectUsesFlare`.

The moon disc is baked **per project**, not per scene - it is one texture in a
1.08 MB heap, so the first scene that resolves to a cycle decides the phase.

## Stars

*Ambience Editor > Day / night > **Stars***

Real glowing points, not a sky texture - and it is affordable, because the
engine already has the exact primitive for it.

`StaPipInfoBag::additiveBlendFix` gives an **additive 3D geometry bag**: the GS
computes `Cv = Cs*FIX/128 + Cd`, so whatever it draws ADDS light to the frame
instead of being pasted over it. The visible light beams and the ground light
pools already ride it. A starfield is the same trick, and it buys four things
at once:

- **One bag per magnitude tier** (`starfield::kTiers` = 3). 400 stars is 2 400
  vertices in **three submits** - the sky dome next to it is 504 vertices in
  one.
- **Per-star brightness and colour live in the Gouraud vertex colours**, so a
  magnitude-2 blue-white star and a faint orange one genuinely differ, and the
  bright ones bloom into the sky behind them. That is what "additive" buys: a
  bright star is *bright*, not a pale grey pixel.
- **Fading the field in at dusk is one byte per bag per frame** - the brightness
  is the bag's additive FIX, driven by the interpolated `DayKey::stars`. No
  geometry is touched when the sun sets.
- **Twinkle is free**, for the same reason: each tier's FIX gets a slow sine at
  its own rate. Three multiplies a frame for the whole sky, not per-star work.

The quads are built once in world space and the bag is re-centred on the camera
each frame like the dome, so nothing is rebuilt per frame either.

### The generator

`src/starfield.{hpp,cpp}` - host-only, no GL, no `Project` (the
treegen/dronegen shape, exercisable from a small harness). Deterministic in its
`Params`: the same seed is the same sky, always, so nudging a slider adjusts the
sky rather than reshuffling it.

- **Magnitude distribution.** A uniform sample raised to a power, which gives
  the real shape: many faint stars under a handful of bright ones. Brightness
  drives the quad size too - a bright star reads as bigger because it saturates
  more pixels, and one texel is all a faint one deserves.
- **Colour from a black-body temperature** (2600 K to 12000 K), so the field has
  real blue-white / yellow / faint-red variation. A uniform-white field is the
  thing that reads as "pixels" rather than as stars.
- **A Milky Way band**: a share of the stars is pulled toward a tilted great
  circle. Note it PULLS rather than rejecting samples - a reject loop would
  quietly thin the sky as the slider rises, so the count you ask for would stop
  being the count you get.

Codegen emits the star LIST (`STARS[]`, ~400 rows of direction + size + colour +
tier), not baked quads, and the game's `buildStarField` makes the geometry at
scene load the way `buildSkyDome` does. Both sides call the same
`starfield::generate`, so the preview and the console draw the same sky.

`starfield::kMaxStars` (800) is the cap, so a slider cannot cost the EE its
frame.

### Two things PCSX2 taught this feature

**The field must NOT use the sky dome's clipping settings.** The dome is ~500
vertices of huge triangles that genuinely cross the screen edge, so it wants
`PipelineInfoBagFrustumCulling_Precise` + `fullClipChecks`. The field is ~3000
vertices of tiny quads scattered over the whole sky, and those settings put the
EE clipper on nearly every package: **a sky view measured 26 FPS with the field
on against 50 with it off.** With `_None` (the field is centred on the camera,
so per-package classification can only ever answer "keep") and no full clip
checks, the same view holds **50 FPS with the field on**. A star is a few
pixels - dropping one whose package straddles the border is invisible, and
clipping it is not worth a millisecond.

**Size the stars in pixels, not in taste.** The frame is 512x448 at a ~90-degree
horizontal FOV, so one degree is about six pixels, and the corona sprite's
bright core is only about a third of its quad. The first pass gave bright stars
a 3-pixel core: present on screen, but reading as dirt rather than as a star.
The generator now puts a bright one at ~3 degrees, i.e. a ~6-pixel core with a
halo around it. Turn the **bloom** post-effect on and those cores flare, which
is the look this is aiming at.

## The hybrid: letting the clock run

*Ambience Editor > Day / night > **Let the clock run***

![The same arch through a running cycle: blue day, red sunset, dusk, and cold
moonlit night - the geometry baked once, at noon.](img/day-night-runtime.png)

Turn it on and the **game** advances the hour. What follows it live is everything
that was already being computed per frame:

| Follows the clock | How |
| --- | --- |
| the sun and moon discs | placed from the live directions, skipped below the horizon |
| the sky gradient | horizon *and* zenith - the dome rebuild already existed for the horizon |
| the fog colour | a register write |
| projected silhouette shadows | they read the light vector every frame anyway, so they sweep |
| the lens flare and god rays | they track the live sun |
| the stars | the interpolated `DayKey::stars` on the bags' additive FIX |

**What does NOT follow it is the baked half** - the vertex shading, the AO
lightmap, any GI. Re-baking those is ~170 ms of EE work (the numbers are below),
so they stay frozen at one hour.

### Two hours, not one

Once the clock moves, "the time" splits in two and the editor asks for both:

- **Time of day** - where the clock *starts*, and what the viewport previews.
- **Bake lighting at** (`bakeHour`) - the hour shadows, AO and GI are baked at.

Noon is usually the right bake hour: it is the most neutral light for a lightmap,
and it is what every other hour is measured against. `ambience::bakedHour` is the
one function every bake-side consumer asks, so the two can never drift apart.

### The drift grade

The gap between those two hours is what `runtimeGrade` closes. Without it,
geometry baked at noon sits brightly lit under a midnight sky. With it, the cycle
computes a colour grade every frame — per-channel gain from how much dimmer the
hour is than the baked one, a small lift toward the sky (real darkness is the sky
reflected off everything, and pure gain crushes to mud), and a mix toward the sky
colour, which is what aerial perspective does.

It is **one `postFx.setGrading` call**, cheaper than anything it stands in for,
and it is **identity at the baked hour** — which is the property that makes
leaving it on safe.

#### The grade must not darken the sky

A full-screen pass hits the sky, the sun and moon discs and the stars too — and
those **already follow the hour**, so grading them is darkening them twice.
Measured on the example's night before this was fixed: the sky went from
(9, 11, 28) to (2, 3, 10) and the moon from a 110 grey to 26..38. It read exactly
like "everything is mega dark", because it was.

So everything that is already hour-correct is **pre-multiplied by 1/gain** before
it is drawn (`ambience::driftCompensation`), and the grade brings it back where it
was authored. That is why `kMinDriftGain` (0.5) exists: the colour bags carry 2x
headroom over their nominal 128, so a floor of 0.5 keeps the compensation exactly
reachable instead of clipping. Verified end to end — sky authored (9, 11, 28) →
drawn (18, 23, 56) → on screen (9, 11, 28); moon 110 → 220 → 110; and at the
baked hour every factor is 1.

The floor has a second effect worth knowing: the world is now darkened by at most
2x rather than 4x, so a night with the grade on reads as night without going to
mud.

Measured on the console over a two-minute day, sampling one frame every 5.5 s
(the arch is baked at noon with neutral light):

| in-game | sky RGB | ground RGB | arch RGB |
| --- | --- | --- | --- |
| 00:00 | 62, 129, 210 | 78, 132, 57 | 120, 119, 114 |
| 04:24 | 104, 102, 120 | 73, 109, 48 | 110, 98, 85 |
| 05:30 | 145, 65, 51 | 73, 82, 34 | 106, 75, 59 |
| 07:42 | 17, 14, 31 | 29, 41, 31 | 39, 37, 48 |
| 09:54 | 3, 4, 13 | 16, 30, 22 | 24, 26, 36 |

The arch never changes its baked vertex colours and still goes warm at sunset and
cold blue at night. **50 FPS / 100 % speed throughout.**

### The generated twin

`inc/daynight.gen.hpp` is a generated header of `inline` functions - the
`live_debug.gen.hpp` arrangement, because both game-cpp templates need it and
neither should carry a copy. It is a **numeric twin** of `ambience::evaluate` +
`ambience::driftGrade`: same arc formula, same cyclic key interpolation, same
twilight zenith pull, same +5 degree clamp, same grade. Change one and change the
other.

That claim is checked rather than asserted. A harness runs both over all 1440
minutes of a day and compares: worst disagreement **1.2e-7** on the sun, moon and
light directions (float epsilon) and **exactly zero** on the sky, zenith, fog,
star level and every grade term.

The key list is emitted as **one flat table sliced per scene**
(`DAYCYCLE_KEY_FIRSTS` / `_COUNTS`, the `CATCH_CANDIDATES` shape) and identical
lists are shared, so four scenes on four presets with the same nine keys ship
nine rows, not thirty-six.

### What is still missing

The third leg of the hybrid: **several baked slots, snapped under a fade**. The
drift grade covers the level and the colour, but a shadow baked at noon is still
a noon shadow at midnight. The costs that decide the shape of that work are
immediately below.

## What a moving cycle would cost

Asked and answered rather than built, because the answer decides the shape of
any future work.

### Free today, or nearly

The sky dome already retints itself per frame when a script changes
`scriptCtx.skyColor` - 504 vertices, nothing. Fog colour is a register write.
The two discs are six vertices each. Projected shadows, blob shadows, the flare
and the god rays all read `SCENE_LIGHT_*` **per frame already**, so making that a
variable instead of a constant would give a genuinely moving shadow at no extra
cost. The zenith colour is the one constant that would have to become a variable
(`SKY_TOP_R` is baked into `buildSkyDome`).

### The wall

Static geometry is lit by **baked vertex colours**. Relighting it per frame means
either re-baking - a full scene re-bake is on the order of 170 ms, impossible at
50 Hz - or moving it onto VU1's directional lighting, which the engine supports
(`PipelineDirLightsBag`, opt-in per object today). The catch is structural, not
arithmetic: a dyn-lit object is excluded from static batching, and a PS2 submit
costs about 1 ms flat. A 50-prop scene is 50 ms of submits before a triangle is
drawn. Baked AO and baked GI lightmaps are static by construction and have no
dynamic equivalent at all.

### Baking several moments instead

| baked thing | where it lives | cost of N slots |
|---|---|---|
| SH probe grid | main RAM, 13 B/probe | cheap - N grids fit easily |
| per-vertex baked shading | rebuilt on the EE | ~170 ms for a full re-bake |
| lightmap atlas + terrain AO map | 256² RGBA32 in GS VRAM | 24 % of the texture heap **each** |
| static batches | rebuilt with the vertices | folded into the re-bake |

So the honest shape is **2-4 slots, probes and vertex colours only**, lightmap
frozen at one slot. The 170 ms re-bake wants to hide behind a transition that
already exists (a scene load, a fade, a cutscene) or be amortised over a few
dozen frames - `RuntimeObject::dirty` already re-bakes objects one at a time.
Crossfading two baked colour arrays is O(verts) per frame and is not worth it;
snapping under a fade is.

### The recommendation

A hybrid, not a choice. Always dynamic: sun/moon position, sky, fog, projected
and blob shadows, flare, god rays - cheap, and it is the half the eye reads as
time passing. The baked half handled by a handful of slots snapped under a fade.
Optionally a per-frame colour grade ([grading.hpp](../src/grading.hpp) is
already a full-screen GS pass) to carry the continuous drift between slots: it
cannot move a shadow, but it makes 14:00 and 16:00 differ for free.

A literally dynamic cycle - every surface relit every frame - is not something
the PS2 can be talked into, and the limit is submit count, not maths.

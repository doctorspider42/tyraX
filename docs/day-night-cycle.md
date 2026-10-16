# Day and night cycle

The day/night cycle controls the sky, sun, moon, stars, fog and scene lighting
from one time-of-day track. Open **Tools > Ambience Editor > Sky & time**.

![A running day/night cycle moving from daylight through sunset and dusk into night.](img/day-night-runtime.png)

The example project shows the same scene at four authored times:
[examples/day-night](../examples/day-night).

![The day-night example at dawn, noon, sunset and night.](img/day-night-example.png)

## The Sky & time tab

The Ambience Editor opens docked beside the viewport. Its **Sky & time** tab is
one split: the **presets** (each with a thumbnail of its sky at its own hour,
the hour, *static* or *clock runs*, and *default* / *live* badges), a drawn
**sky** in the middle, and an **inspector** on the right. In a narrow dock the
preset list folds into a picker in the sky's header and the inspector moves
under the sky. The top line says which preset the current scene uses.

The sky preview draws the preset at the preview hour: the gradient, the sun and
moon on their arcs with their elevations ("Sun +10°", "Moon -7°, below the
horizon") and who lights the scene. **Play** runs the day in the preview at game
speed (the clock's day length, or 24 seconds a day) - only the preview moves,
not the preset's hour or the viewport.

Under it, the **24-hour track**: the horizon colour through the day, one diamond
per key (click to select it, drag to move it in time), the sunrise-sunset bar,
the preview hour as a white playhead and - while the clock runs - the **bake
hour** as a dashed marker. Drag on the track to scrub the hour; double-click it
to add a key there (continuing the curve). **Seed a default day** writes a
nine-key starting track. When the clock runs and the preview hour is two hours
or more from the bake hour, a warning says the baked bounce light will disagree
with the moving sun, with **Bake at HH:MM** to move the bake hour there.

## Set it up

A cycle belongs to an ambience preset. Select the preset used by the current
scene and turn on **Day/night cycle** (the *Cycle* section of the inspector's
first tab). The inspector then has four tabs:

- **Key HH:MM** - the selected key's hour and colours (sky horizon and zenith,
  light, fog), ambient, diffuse, brightness and stars, plus the *Sky dome*
  settings and the *Cycle* switches;
- **Sun & moon** - sunrise and sunset, the sun's bearing and tilt, the moon's
  bearing, tilt and hours behind the sun, disc sizes, phase, opacity and the
  moon texture;
- **Fog & stars** - distance fog (its colour comes from the keys) and the
  night sky;
- **Clock** - the time of day, and with the clock running its day length and
  bake hour.

With the cycle off the first tab is **Look** - the static sky, light and fog -
because those values only apply then: with a cycle the keys own the sky, the
light and the fog colour, so the static fields are not shown.

The viewport previews the selected preset while the Ambience Editor is open
(*Preview in viewport*). If it is not the scene's preset, the sky's header
offers **Use for scene <name>**. Closing the window returns the viewport to the
scene's preset; it does not discard your edit. The "..." menu holds the
preset's name, *Default at game start*, Duplicate and Delete.

## Keyframes

Each key stores the hour plus sky horizon/zenith colour, light colour, ambient,
diffuse, brightness, fog and star brightness. Keys interpolate around the full
24-hour loop, including across midnight.

Use two keys to hold a value. For example, keeping the same night colour at
00:00 and 04:30 prevents it from slowly blending toward dawn all night. **Seed a
default day** creates a useful nine-key starting track.

## Sun and moon arcs

Azimuth is the compass bearing where a body crosses the horizon (`0 = +Z`,
`90 = +X`). Tilt lowers the peak from the zenith: peak elevation is
`90 - tilt`. A moon offset of 12 hours puts it opposite the sun.

The active light never goes below 5 degrees or exactly through the zenith,
because those directions produce broken or degenerate shadows. Near sunrise
and sunset, directional shadows fade out, the light switches body, then fade
back in. Blob shadows stay visible because they have no direction to switch.

The sun uses additive blending; the moon uses alpha blending and a desktop-
generated phase texture. Both discs can bloom and flare.

The lens flare and the god rays are aimed from the sun's projected screen
position, and that projection is not the textbook one. Tyra's perspective
matrix is built for the VU1 pipeline's fixed 2048 scale, so after the
homogeneous divide the screen edge sits at `w * raster / 4096` rather than at
`w`, and the matrix already carries the GS's downward y. Until 1.65.1 the sun
was projected as if `x/w` were normalised device coordinates and its y was
flipped a second time, which pulled it 8x closer to the middle of the screen
(at a 512 px raster) and mirrored it across the centre: the shafts radiated
from roughly nowhere, the ghost sprites walked the wrong axis, and the edge
fade decided the sun had left the screen at the wrong angles. If a sun effect
looks misaimed, that is the first thing to check. The ghost sprites also take
one extra correction, because the 2D renderer authors sprites in the stock
512x448 layout and letterboxes it into taller rasters - so in **Full-height
PAL** or **1080i** a world-anchored sprite has to come back up by half the
difference, or the flare hangs below its own sun.

## Stars

Stars are a deterministic point field on the sky dome. Keyframes control their
brightness. They fade near the horizon and around the moon so the sky does not
look like a flat texture pasted behind it.

The star seed and density are project settings. Changing them changes the field
but adds no runtime asset files.

A [painted sky](sky-texture.md) follows the cycle as a tint: shown as
painted at the authored hour, darkened toward night. Its painted sun does not
move.

## Static or live

With **Clock runs in game** off, the chosen hour is baked and stays fixed. This
is the cheapest and most accurate mode.

With it on, the game advances time. Cheap values update continuously:

- sky, fog and brightness;
- sun, moon and stars;
- directional light and live shadows;
- lens flare and god rays.

Expensive lighting stays baked at its authored hour:

- ambient occlusion;
- global-illumination lightmaps;
- light probes;
- baked point and emissive light.

The editor shows both the live hour and the bake hour on the track, and warns
when they are far apart. Keep them reasonably
close or the moving light will disagree with baked shadows and bounce colour.
For a full-day game, bake several ambience presets and switch scenes or lighting
sets at planned times instead of stretching one bake across 24 hours.

## Rebuild rules

Changing time or any resolved light setting invalidates GI and AO caches. Re-run
their bakes before judging the result. The normal project build uses the latest
available cache; it does not run those desktop raytracers automatically.

## Useful starting values

- Start with the seeded day and adjust colours before changing arc math.
- Keep the moon about 12 hours behind the sun for a readable night.
- Use a shallow star brightness ramp around dusk, not an instant switch.
- Preview sunrise and sunset in motion; static noon/night shots will not expose
  a bad handover.
- Use the runtime example as a reference before building a custom track.

## A fixed mood selected in a game menu

[Motor District](../examples/vehicle-playground) uses a pause-menu toggle backed
by a save value. Its project-owned `district_mood.cpp` pins the generated
`daynight::g_hour` to noon or midnight before rendering and switches eight live
spotlights plus emissive dressing on resume. This exercises the existing hybrid
cycle without duplicating scene geometry; it is not a Set Ambience sky-only
swap or a replacement for separate GI bakes. The script also reapplies the
light/visibility state on scene-generation changes.

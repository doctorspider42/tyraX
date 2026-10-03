# Weather and lit street lamps

Weather is a per-scene setting, plus a flow node, that makes it rain: drops fall
around the camera, the asphalt turns dark and wet, and at night every street lamp
reflects in the road as a long streak. The street lamps that
[road furniture](roads.md#street-furniture-format-101) places light the street at
night: each one throws a pool of warm light onto the road under it and glows with
a halo. Both are built the cheap PS2 way. The pools are decals baked on the host,
and the halos and reflections are a few sprites rebuilt each frame. The wet
asphalt is one colour per frame shared by every road chunk.

![PCSX2, Motor District at night: left, Garage boulevard from street level - the pools of the lamps on both pavements and the halos down the street; right, the same street from 8 units up](img/road-night-lamps-pcsx2.png)

![PCSX2, the same two views in the rain: dark wet asphalt, rain streaks, and every lamp's reflection stretched toward the camera](img/road-rain-pcsx2.png)

## Authoring

**Scene > Scene Preferences > Weather and street lamps**:

- **Weather**: *Dry* or *Rain*. This is what the scene starts with on every load.
  A scene that starts in the rain also starts with wet streets.
- **Rain intensity** (0-100%) sets how many drops fall and how wet the roads
  get.
- **Street lamps**:
  - *Auto* (the default) follows the sun of the scene's
    [day/night cycle](day-night-cycle.md). Lamps come on in the dusk, fully lit
    at 3 degrees below the horizon and off at 6 above. A cycle that does not run
    uses the hour it is baked at. A scene without a cycle is daytime, so its
    lamps stay off.
  - *Always on* keeps the lamps lit.
  - *Off* bakes no pools for the scene at all.

The three are scene fields (`weather`, `weatherIntensity`, `streetLamps`, format
107), each written only when it differs from its default, so an untouched scene
saves byte-identical.

**Set Weather** (flow node, *Scene* category) changes the weather at runtime. It
takes three settings:

- *Weather*: Dry or Rain.
- *Intensity*: 0..100%.
- *Seconds*: how long the rain takes to reach the new intensity.

The roads follow at their own pace. They get wet over about 6 s of rain
(`kWeatherSoakSeconds`), and dry over about 40 s once it stops
(`kWeatherDrySeconds`). They never get wetter than the rain makes them. A
0-second transition switches at once, wet roads included, which is the way to set
a scene up from On Start. The node writes straight into the game's weather state,
not through ScriptContext. Live Logic cannot hot-patch it, so a graph that gains
one needs a rebuild.

The lamps that light are the road furniture's lamp lines. A lamp placed as an
ordinary scene object (Big City's are) is still only a model. To light those,
place them as furniture, or use point lights with ground pools
([flashlight.md](flashlight.md), "light pools").

## What runs where

### The pools (host-baked, streamed with the furniture)

`roadlight::bakePools` (`src/roadlight.cpp`) lays one pool under each lamp. The
head is the built-in lamp's unshaded lens, found by its colour among the
instance's own triangles; for an `.obj` lamp it is the furthest reach of the
model's top quarter. The pool is a disc of radius 1.1 x the head's height above
the surface (2..9 units), laid out to 85% of that radius, because the corona
sprite it is drawn with is black beyond.

The disc lies 0.05 above the drawn surface, the highest of the road, junction
patches, pavements and terrain under it. That puts it over the node paint (0.02)
and the road details (0.03). It starts as one quad and is split, up to twice
(4 x 4 cells), only where it cannot follow the surface. The budget is
asymmetric:

- a pool may float up to 0.12 over the street, because the light then lands a
  hair off in screen space;
- it may never dip more than 0.03 under its lift, because hidden light is a
  hole in the pool.

A vertex takes the highest surface within 0.6 of it, so the triangle across a
kerb floats over the road edge instead of diving under the pavement. A cell at
the depth cap that still dips (a kerb runs through it) is raised by its worst
dip. A lamp over flat road is one quad (6 vertices).

The pools ride in the furniture's own table:

- They are extra **`ROAD_FURN` rows** with a `light` column. A light row's
  colour word is its texture coordinate: 12 bits of U and 12 of V
  (`roadlight::packUv`).
- They are chunked by **96-unit cells** (`kPoolCell`). A pool chunk is a handful
  of quads, and every chunk drawn is one more submit.
- They upload through the furniture's own upload block, with one changed line
  set (`roadfurn::uploadSource(true)`). So a streamed project builds them as
  **ordinary furniture items** (`RS_FURN`), and with
  [tables on disk](roads.md#tables-on-disk-format-104) they are pages of
  `bin/roadfile/roads.bin` like any other furniture row.
- On the console a pool chunk is owner -7 with `ProcChunk::lampLight` set. Its
  bag is additive (FIX 128), z-tested, never z-written and unfogged, textured
  with `hud/flare-corona.png` (the beams' and stars' sprite, so no new
  texture). It carries **no per-vertex colour**. Its colour bag points at one
  `roadLampPoolColor_`, so the night level is one colour per frame.
- `renderProcChunks` skips the pools, and `renderRoadLamps` draws them after the
  whole scene. The asphalt under them is then already in the frame, whichever
  order the [interleaved passes](interleaved-passes.md) chose. A pool chunk has
  a frustum reject and a 90-unit draw distance.

`ROAD_LAMPS` (scene_data.hpp, kept in the ELF even with tables on disk) holds
one 10-float row per lamp:

- the scene;
- the head;
- the surface point under the head;
- that surface's least-squares slope;
- the pool radius.

`// scene N: L lit lamps, pools V vertices in C chunks` in `scene_data.hpp` is
the bake's count.

### Halos and wet reflections (per frame, one bag)

`renderRoadLamps` walks the scene's run of `ROAD_LAMPS` and builds one additive
bag through the corona sprite:

- **a halo** for each lamp within 160 units in front of the camera. It is a
  camera-facing quad of half size 0.55 + 0.011 x distance, so a far lamp stays a
  point of light. It is pulled 0.7 toward the camera so its own head does not cut
  it, and fades out past 110 units. At most 64 a frame.
- **a reflection streak** for each lamp within 55 units, when the road is wet.
  It lies on the lamp's surface plane (the baked slope, so no height query
  per frame). It runs along the line from the point under the lamp toward the
  camera and is centred where a mirror would show the lamp: the foot plus
  `d x h / (h + eye)`. It is the corona sprite stretched along that line. On
  screen it reads as the vertical streak under every light on a wet street, the
  classic trick. At most 32 a frame, brightness x wetness.

Car lights get no streaks yet (see the backlog).

### Wet asphalt (one colour)

With weather in the project, `procFinishChunks` points the colour bag of every
plain asphalt chunk at one `roadWetTint_`. That covers owner -3, textured, not a
spill or a soft edge, every vertex the uniform 128 grey. `updateWeather` sets
the tint once a frame: `128 - wet x (58, 55, 46)`, darker and a little cooler.
Dry is 128, exactly the old picture. Nothing is rewritten per vertex, ever. The
single colour also takes the colour stream off those bags.

Node paint whose colour is that same grey darkens with the asphalt. Kerbs, rails,
untextured pavements, details and furniture keep their own colours.

### Rain

`updateRain` keeps 240 drops in a 22 x 22 x 14-unit box round the camera. A drop
falls at 17 units/s and drifts a little. Each drop wraps round the camera in all
three axes, so a car at 26 units/s drives through the rain rather than out of it.
Nothing is respawned, there is no RNG per frame and no height query. The drops
are drawn by the particles' VU1 billboard program as one bag, the rain emitter's
streak shape: 3.2 cm wide, 0.75-1.2 long, hanging from world-up, alpha-blended.
The bag's count is `240 x intensity`.

### The state machine (one source, two homes)

`src/weather_core.inl` holds `WeatherState` (the rain ramp and the wetness lag)
and `weatherLampLevel` (the lamps' level from the sun). The editor compiles it
(`roadlight::WeatherSim`, `roadlight::lampLevelFromSun`, used by the viewport and
the check). The codegen pastes the same bytes into the generated
`inc/daynight.gen.hpp`, inside `namespace weather` (embedded by CMake, the
`roadstream_core.inl` arrangement). Next to it:

- `SCENE_WEATHERS`, `SCENE_WEATHER_INTENSITIES`, `SCENE_LAMP_MODES` and
  `SCENE_LAMP_STATIC` (the level at a non-running cycle's baked hour);
- `weather::g_state`, `reset(scene)` (called by `loadScene` next to
  `daynight::reset`) and `request()` (Set Weather).

Once a frame, `updateWeather` runs in the game loop after the particles. It:

- ticks the state, with dt 0 while paused;
- finds the scene's lamps;
- computes the lamp level, multiplied by the night grade's compensation
  (`daynight::g_comp`) so emissive light is not darkened twice;
- sets the pool colour and the wet tint;
- moves the rain.

### Zero cost when unused

Everything above is gated:

- a project with **furniture lamps** (in a scene whose lamps are not Off) gets the
  pool rows, `ROAD_LAMPS`, the corona texture load and the lamp runtime;
- a project with **weather** (a scene that rains or a Set Weather node) gets the
  wet tint and the rain.

A project with neither keeps its exact generated source. That was checked by
`--refresh-gen` of every example: only the Motor District (it has furniture
lamps) changed, plus the format number in showcase's replay header. The hooks are text patches in
`roadlight::patchTemplate`, the details-and-furniture pattern.

## The editor

The viewport previews the same lamps from the same bake (`syncRoadDraws` calls
`roadlight::lampsOf` and `bakePools` over the same surface):

- pools, halos and, on a wet scene, streaks are drawn when the previewed hour is
  night. That is the scene's cycle at its slider time, or the Ambience Editor's
  previewed preset, or the scene's lamps set to *Always on*;
- a wet scene's asphalt and junction patches are drawn with the console's tint.

Two things the preview does not show. It does not reproduce the grade's
compensation, because the editor has no grade to cancel. It also does not show
rain, which only exists at runtime. The preview was compiled and wired, not
looked at: the editor GUI was not launched for this change (see the backlog).

## What it costs

Measured in PCSX2 (emulated EE timing, so rough). The scene is the Motor District
main scene at night (the pause-menu night, the district's own spotlights on), with
a frozen walker on Garage boulevard at (-3, -35) looking north (eye 2.2, pitch 8)
and interleaving pinned off. Each arm is three `--profile-frame` captures:

| arm | `Road_lamps` | `Rain` | `Total` | HUD MEM | HUD FPS |
|---|---:|---:|---:|---:|---:|
| lamps Off (no pools, no lamp runtime) | - | - | 10.6-12.0 ms | 21.2 MB | 59.9 |
| lamps on, dry | 0.52-0.53 ms | 0.03 ms | 12.0-12.6 ms | 21.5 MB | 59.9 |
| lamps on, rain | 0.72-0.78 ms | 0.12-0.13 ms | 12.1-13.7 ms | 21.3 MB | 59.9 |

- **The bake**: 50 lamps, 3 342 pool vertices in 7 chunks (67 a lamp on the
  district's rolling streets). EE RAM grows about 0.3 MB.
- **The frame**: the lamp row is about +0.5 ms, and +0.25 ms more with the wet
  streaks. Rain is about +0.1 ms. `Total` moves within its own noise and the
  frame rate does not move. The row's bracket drains the pipeline (`costEnd`), so
  it includes the GS filling the additive quads near the camera. How much of it
  is EE work was not split.
- **Big City** (1.4 km, streamed roads, tables on disk) is tested with lamps added
  as furniture every 25 units (alternating sides) on its 110 ordinary roads and a
  night cycle at a baked hour, in the Ravager at spawn:

  | | HUD MEM |
  |---|---:|
  | no furniture lamps | 18.5 MB |
  | lamp posts with lamps Off | 19.7 MB |
  | lit | 20.2 MB |

  So the pools and `ROAD_LAMPS` cost +0.5 MB. The bake is 1 404 lamps and
  58 248 pool vertices (41 a lamp on the flat city), and the pools add 8 300 of
  the 93 063 resident road vertices at spawn. `Road_lamps` reads 0.60-0.63 ms
  (it walks all 1 404 lamps for the halos), the HUD reads 59.9 FPS, and
  `roads.bin` grows from 9.4 to 10.4 MB.

Nothing here was measured on a physical PS2.

## Limits

- Puddles are not built (a road-details decal kind shown only when wet).
- Car lights cast no wet streaks.
- The halo/streak pass walks every lamp of the scene each frame. That is fine at
  hundreds of lamps; a city of many thousands wants the lamps bucketed by cell.
- Pools do not land on objects and do not light the models. The lamps are not
  dynamic lights.
- Reflection views (the env probe) do not draw pools, halos or rain.
- Rain falls through bridges and roofs. There is no occlusion test and no splash
  on the ground.
- The drawn height of a pool beside a kerb floats up to the kerb's height over
  the road edge (see "The pools").
- A Set Weather node is not hot-patchable by Live Logic.

## Verification

`--vehicle-check` **"wet roads and lamps"** checks the following:

- **Lamps and pools:**
  - every furniture lamp becomes a light, its head about 5.2 above the surface;
  - every pool vertex lies kPoolLift over the drawn surface and never under it;
  - no sample of a pool sinks under its lift;
  - every pool is centred under its lamp, with UVs spanning it;
  - a lamp over flat ground is one quad;
  - chunks hold whole pools, within budget, one 96-unit cell each;
  - the bake is deterministic bit for bit, and the packed UV round-trips.
- **The weather state machine:** the rain ramps linearly and the roads soak
  after it, then dry slowly; 0 seconds switches at once; the intensity is
  clamped; dt 0 holds; the lamp level is off by day, on at night and monotone
  through the dusk.
- **The codegen:**
  - lamps generate the pools, the table and the runtime;
  - a streamed project builds the pools with its furniture items, and every
    streaming anchor holds;
  - with tables on disk, every furniture row, pools included, is a `roads.bin`
    item;
  - rain generates the tint and the drops;
  - Set Weather compiles to a request;
  - no lamps and no weather generates none of it, and lamps Off bakes no pools;
  - the game header carries the core with no block comments.

In PCSX2 the Motor District was checked at night, at night in the rain and with a
Set Weather (rain, 100%, 8 s) on On Start, which rained and soaked the road after
25 s. Big City was checked as above.

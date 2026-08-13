# Terrain splat painting

Terrain used to wear a single tiled material (the scene's terrain `.mtl`,
repeated across the surface). **Splat painting** blends several terrain
**layers** — grass here, a dirt path there, rock on the cliffs — by painting
their weights with a brush. The PlayStation 2 draws the blend the way era
games did: **two-pass splatting on the GS** — the base terrain draws as usual,
then each painted layer draws once more over it with its own tiled texture and
the painted weight as Gouraud vertex alpha. Layer textures stay **tiled at
full resolution** at any distance and on any map size; there is no baked
composite to run out of texels.

It lives in the **Terrain Editor** (*Tools > Terrain Editor*) — the one window
for both terrain brushes: **Sculpt** (the heightmap brush) and **Paint** (this
feature), switched by the two tool buttons at the top. Both need a terrain: a
scene whose terrain was removed shows only the *Terrain in this scene*
checkbox that brings it back — see [the terrain](terrain.md). The same toggles
sit in the viewport toolbar as *Sculpt (4)* and *Paint (6)*; grabbing either
tool opens the window with its options. One brush is in hand at a time.

## The brush

Both tools share one brush: **Radius** (with **`[` / `]`** to resize from the
keyboard, mid-stroke) and a per-tool **Strength**. The ranges **scale with the
map** — radius up to half the map's size, sculpt strength growing with it too
— so a 2000x2000 world sculpts and paints as comfortably as a 64x64 garden
(the sliders are logarithmic, so small values keep their precision on any
map). Compact copies of the brush controls float in the viewport corner while
a tool is active; they edit the same values as the window.

## Layers

A scene's terrain always has a **base**: its terrain material (Scene
Preferences > Terrain material), or a flat green when none is assigned. On top
of it you add **layers**, each one an existing Material Editor `.mtl` — so a
layer inherits that material's texture (`map_Kd`), color tint (`Kd`) and
tiling (`map_Kd -s`) exactly like the base does. Grass, dirt, rock and path
are just ordinary materials you already know how to author. Oversized imports
are safe: the PS2 caps textures at 512x512, and the build bake resizes any
bigger (or non-power-of-two) material texture down automatically — the
full-resolution source stays in `res/` for the editor.

The Terrain Editor shows the layers as a **Photoshop-style stack**: the top
row paints over everything below it, and the **base** sits at the bottom —
picked from a combo right here (it sets the scene's own material if the scene
overrides the project default, otherwise the project default), with its own
**Stochastic tiling** toggle. Everything about the terrain surface lives in
this one window; you no longer have to open Scene Preferences to set the base
material. `+ Add layer` lives at the top and drops the new layer **on top of
the stack** — and puts the paint brush straight in hand, since a fresh layer
is there to be painted. The reorder arrows raise/lower a layer in that
hierarchy.

Each layer row has an **active** radio (the layer the brush paints), a name, a
material picker, the reorder arrows and a remove button, plus a **Size** below
it — how large that layer's texture pattern appears on the ground (a
multiplier on the material's own tiling: `2.00x` makes the pattern twice as
big / repeat half as often) — and a **Stochastic** toggle (see below). Size
lets you tune the look without editing the `.mtl`; it has no effect on a flat,
textureless layer. A scene with **no layers behaves exactly as before** — the
single terrain material, tiled, one draw pass.

## Stochastic tiling (breaking the grid)

A tiled texture repeats on a regular grid, and the eye latches onto that
"checkerboard" the moment the camera pulls back. **Stochastic tiling** (a.k.a.
texture bombing) breaks it. Tick **Stochastic** on the base or any layer and
the build bakes that texture into one larger, **still-perfectly-tileable
"supertile"** (up to 512x512) whose interior scatters randomly rotated,
flipped and offset patches of the source, feathered so the seams disappear.
The game tiles the supertile like any texture — **the same single pass, zero
runtime cost** — but the repetition period is 2-8x longer (depending on the
source size), so the tell-tale grid leaves the visible range.

- It shines on **organic** textures — grass, sand, dirt, rock. Leave it
  **off** for anything with fixed seams that must line up (brick, tile,
  planks): bombing rotates and offsets the pattern, which would scramble
  those.
- The supertile is generated deterministically at build (into
  `.res-baked/stoch`, never your `res/`), and the editor viewport previews
  the exact same pixels.
- The runtime tiling is automatically divided by the supertile factor, so the
  texture keeps its on-ground size — toggling Stochastic doesn't change the
  scale, only the variety.
- Cost is VRAM (one ≤512² texture per stochastic terrain texture) and a
  little detail on very large source textures (a source bigger than 256px is
  sampled down so at least a 2x2 arrangement fits the 512 cap).

## How the ground tiles at all (and what it looked like when it didn't)

Terrain texture coordinates are **world position x the material's tiling
factor**, so they run far outside 0..1 — a 192-unit map at `map_Kd -s 0.125`
covers 24 tiles per axis, i.e. STs from -12 to +12. Everything on this page
depends on the GS repeating the texture over that range.

Until TyraX 1.9.1 it did not. `GS_REG_CLAMP` (the GS's wrap mode) is one
global register, no 3D pipeline writes it per mesh, and ps2sdk's
`draw_setup_environment` leaves it at CLAMP at boot — so the ground texture
was drawn **once**, in the 8x8-unit patch around the world origin, and its
edge texels were stretched along the world axes over the entire rest of the
map. On screen: long continuous streaks converging on the vanishing point
with flat, washed ground between them, worst when you look along the ground
toward the horizon. It is now asserted as REPEAT at the top of every frame,
which is what every layer, supertile and macro variation on this page
assumes.

Two things follow if you are chasing something similar:

- A tiling problem is diagnosed in one boot with a **deliberately
  unmistakable texture** — four saturated quadrant colours with a contrasting
  border. Correct tiling repeats the quadrants across the map; a clamped one
  shows a single tile at the origin and the border colour everywhere else.
  Soft, organic ground textures make both failure modes look like "the ground
  is smeared", which is how this survived so long.
- **Repeat is the only wrap mode 3D geometry gets.** A surface that must not
  repeat (a projected decal, a shadow receiver patch) clamps its own texture
  coordinates where they are built; only the engine's render-target textures
  (camera feeds, the raytraced mirror) get a real per-bag clamp, and that
  costs a pipeline drain.

## Macro variation (breaking uniformity at the group-of-tiles scale)

The supertile itself still repeats every 2-8 tiles — the GS texture cap is a
hard 512. The **Variation** section adds a third, *unbounded* scale on top:
large soft patches of lighter and darker ground, driven by deterministic
world-position value noise **multiplied into the terrain vertex shade while
chunks bake**. Because it rides the vertex colors:

- **zero runtime cost** (the colors are computed at chunk build anyway) and
  an **infinite period** — it never repeats, breaking even the supertile's
  second-order grid;
- it tints the base and every painted layer **together** (they all shade
  through the same per-vertex value), so a dark patch dims grass and the dirt
  path across it coherently — it reads as ground lighting, not an overlay;
- Gouraud interpolation keeps the patch edges perfectly smooth.

**Amount** (0 = off, up to ±50% brightness at 1.0) and **Patch size** (world
units) live in the Terrain Editor. The editor viewport and the game compute
the identical noise (a twin — `tintNoise2` in viewport.cpp and the generated
`terrain_game.cpp`). The stochastic supertile generator additionally
sprinkles a few large, low-amplitude brightness blotches *inside* the
supertile, so the three scales compose: micro (bombing), mid (supertile
blotches), macro (vertex noise).

## Painting

Grab the **Paint** tool (the button at the top of the Terrain Editor, the
viewport toolbar, or the **6** key), then **drag on the terrain** in the 3D
viewport — same brush raycast and ring as sculpting:

- **Radius / Strength** — the brush footprint and how hard each stroke pushes
  the active layer's weight (cosine falloff from the center).
- Painting a layer **pushes the other layers back** where you paint, and the
  base fills whatever weight is left — so a stroke reads as "replace with
  this".
- **Erase** (the toggle, or hold **Shift**) takes the active layer's weight
  back off, revealing the layers/base underneath.

The viewport draws the **same two passes the PS2 does** — what you see while
painting is what ships. Each finished stroke is one undo step.

## What else reads the painting

The layers are not only a look: a **procedural scatter volume** can read one
as a mask, which is how "trees only on the grass, never on the rock painted
over it" is authored — *Terrain Mask* with *Source* = **Terrain material**,
see [procedural generation](procedural-generation.md#scattering-on-one-terrain-material).
It reads a material's **visible** coverage (a layer painted on top hides the
one below), and it is build-time only: the splat map never ships, so a
runtime volume cannot use it.

## Blend resolution

The painted weights live **on the terrain vertices** (the same grid as the
heightmap), because that is exactly what the hardware interpolates: the GS
shades the blend per pixel from the vertex alphas (Gouraud), so the blend
gradient is as fine as the terrain grid. Texture detail is unaffected — it
comes from the tiled layer textures. For crisper blend *edges* on a big map,
raise **Terrain detail** (Project Preferences); the weights resample
automatically, like the heightmap does.

## How it is stored

- The **layer list** travels in the `.tyra` project manifest (per scene: a
  name, an `.mtl` and the Size multiplier each).
- The painted **weights** live in a per-scene binary sidecar
  `terrain-<scene>.splat` (like the heightmap's `terrain-<scene>.heights`): a
  `splatW x splatD` vertex grid, one 0..255 byte per layer per vertex (the
  base weight is the remainder). It is tracked in git and rides the editor
  undo snapshot.

## How it ships (build time)

Codegen bakes the per-vertex weights into `inc/terrain_heights.gen.hpp`
(`SPLAT_<scene>_WEIGHTS`, next to the heightmap tables — same grid) and the
layer descriptors (texture index, tiling incl. Size, tint) into
`inc/texture_data.gen.hpp`. At runtime `buildTerrainChunk` builds, for each
16x16-cell chunk, the base bag plus **one extra StaPip bag per layer that has
any weight on that chunk** — same vertices, the layer's tiled STs, shade-lit
colors whose alpha is the weight — and `renderTerrain` submits them right
after the chunk's base bag. The layer bags share a blending-enabled info bag
(GS PRIM ABE with the default alpha-over equation carried in-band per mesh).
Unpainted chunks cost exactly what they did before; layer textures ride the
normal scene texture streaming and are tiny (they are ordinary tiled material
textures).

Measured on the 64x64 demo scene (PCSX2 software renderer, PAL): 50 FPS with
two painted layers vs 50 FPS unpainted — EE 36% in both, VU 3% vs 2%. The
extra passes only exist where you painted.

### Why not a baked composite texture

The first version of this feature baked the blend into one whole-terrain
texture at build time (zero runtime cost). It hit a hard wall: the PS2 caps
textures at 512x512, so a whole map's surface detail had to fit one 512-texel
square — embarrassingly blurry up close, and unfixable (per-chunk baked
textures would thrash the ~1 MB GS texture budget through the mid-frame PATH3
upload hazard). Two-pass vertex-alpha splatting is how era games solved
exactly this, and it keeps the full tiled texture detail for a small,
painted-area-only runtime cost.

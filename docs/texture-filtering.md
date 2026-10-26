# Texture filtering: smooth, pixel and pixel close-up

Texture filtering decides how the PS2's GS samples a texture when a texel
covers more or less than one screen pixel: **Smooth** blends neighbouring
texels (bilinear, the classic PS2 look), **Pixel** draws every texel as a crisp
square (nearest, "point sampled", the PS1 look), and **Pixel close-up** is
crisp when the camera is close and smooth in the distance.

| Mode | GS `TEX1` MMAG / MMIN | Looks like |
|---|---|---|
| Smooth (default) | linear / linear | soft texels up close |
| Pixel | nearest / nearest | crisp texels everywhere; far walls shimmer |
| Pixel close-up | nearest / linear | crisp up close, softer and calmer far away |

There are no mipmaps in the game's pipeline, so a far wall made of many texels
per pixel flickers ("shimmers") as the camera moves with plain Pixel. Close-up
keeps the crisp look where you see it and lets the GS blend where texels are
smaller than a pixel. It costs exactly the same: the GS samples either way.

[`examples/texture-atlas-filtering`](../examples/texture-atlas-filtering) shows
all three on one atlas page: crates 00-09 Smooth, 10-19 Pixel, 20-29 Pixel
close-up.

## Where it is set

- **Project default** - *Project > Preferences > Graphics > Texture filtering*
  (Smooth / Pixel / Close-up). Smooth is the default, so a project that never
  touches it draws exactly what it drew before.
- **Per texture** - select a PNG in the **Asset Browser**; its *Build settings*
  have a *Filtering* row: *Project default*, Smooth, Pixel or Pixel close-up.
  The setting belongs to the **texture**, not to a material or an object, so
  every model and material using that image follows. Renaming or moving the
  texture in the Asset Browser carries its setting along.
- **Viewport preview** - the viewport's view menu (and the gear popup) has
  *Texture filtering*: *Match PS2* shows each texture as the game will sample
  it; *All smooth* and *All pixel* only change the editor's view (handy for
  modelling, or for checking texel density) and are remembered per machine.

A filter change is a **bake-time** property: it reaches the game on the next
build (Live Link does not push it).

## What it applies to

Colour textures of static and animated models, primitives with a material,
static batches, procedural chunks and instanced scatter, and billboard
impostors.

Always smooth, whatever the setting: lightmaps, AO maps, light probes,
terrain base and layer textures, the sky, particles, decals, mirrors and
camera feeds, HUD and menus, and every post-effect. Those are lighting data or
screen-space images, not pixel art. Animated models' textures are images
extracted from the `.glb`/`.fbx`, so there is no per-texture row for them:
they follow the project default.

## Texture atlasing: pages can mix filters

The filter is part of the **draw**, not of the image in VRAM: the GS takes it
from `TEX1`, which the engine sends with every mesh. So an atlas page
([texture atlasing](texture-atlasing.md)) can hold a pixel-art crate and a
smooth poster side by side - pages are never split by filter and the feature
costs no VRAM. The 2-texel gutter around each member stays (smooth and
close-up members need it; pixel members simply never reach it).

## How it works

- **Bake.** texbake writes a `# tyra-filter pixel` (or `closeup`) line right
  after every `map_Kd` whose **source** texture is not smooth, in the baked
  `.mtl` (next to the atlas `# tyra-uvrect` hint). The model bake writes the
  same code into each `.tmdl` part's flags (bits 1-2; older files read 0 =
  smooth, so the format version did not change). Parts with different filters
  are never merged at bake time.
- **Engine.** `LeanObjLoader` and `TmdlLoader` expose it as
  `LeanObjMaterial::textureFilter` / `LeanMtlMaterial::textureFilter`. A
  `StaPipTextureBag::filter` other than 0 overrides the info bag's filter when
  the static pipeline writes the bag's `TEX1` qword.
- **Game.** `GameModelPart` / `GameMaterial::textureFilter` carry it to every
  texture bag built from a part or material. Anything that merges parts by
  texture (static batches, instanced scatter groups) keys by texture **and**
  filter, because one bag has one `TEX1` - with atlasing, two members on the
  same page are the same `Texture*`.
- **Editor.** `ProjectSettings::textureFilter` and `Project::textureFilters`
  (keyed by the texture's res path, stored beside `textureQuality`; project
  format 123). The viewport applies `GL_NEAREST`/`GL_LINEAR` the same way to
  material and model colour textures only.

## Troubleshooting

- **"I set Pixel but the game is still blurry."** Rebuild - the filter is
  baked. Then check the texture is one the list above covers (terrain and the
  sky stay smooth).
- **"Far walls flicker."** That is Pixel without mipmaps. Use Pixel close-up.
- **"The viewport looks pixelated but the game does not."** The viewport's
  preview may be on *All pixel*; set it back to *Match PS2*.

# Painted sky

A sky dome can carry a painted panorama instead of only the two-colour
gradient: clouds, a sunset, a skyline. It is set per **ambience preset**, so a
scene gets it through the preset it uses, the same way it gets its sky colours.

## Using it

1. *Ambience Editor > Sky > **Sky texture (PNG)...***, with *Gradient sky
   dome* on. Pick a **360-degree equirectangular** panorama (2:1, horizon
   across the middle). The file is copied into `res/sky/`.
2. Turn **Sky rotation** until the painted sun sits where the scene's light
   comes from. The picture does not move the light and the light does not move
   the picture. Line them up yourself, or leave them apart if the look is what
   matters.
3. Build. The panorama reaches the game and the car paint through the ordinary
   dome, so nothing else needs switching on.

A free source that fits: the 2K panoramas from freestylized.com ("360" format),
royalty-free for commercial use.

## What the build makes of it

- **Only the upper part ships.** The dome ends a few degrees below the
  horizon, so the build keeps the top 56% of the panorama (`skytex::kVMax`),
  averages it down to **256x128** and palettizes it to **8 bits**, whatever the
  project's texture default. 16 colours band a sky gradient into stripes.
  That is 32 KB of GS VRAM plus its palette, resident while the scene is
  loaded.
- **`res/sky/` never ships.** The source panoramas stay on your machine; the
  game loads only `sky/scene<N>.png`, the baked crop. A panorama picked from
  anywhere else in `res/` ships its full-size file too (disc space, not VRAM).
- **The gradient colours become a tint.** At the authored hour the vertex
  colour is 128, so the panorama shows exactly as painted. After that it is the
  ratio of the live sky colour to the authored one, so the day/night cycle and
  *Set Sky Color* still darken or warm it. At midnight the clouds are still
  there, only dark.
- **The dome gets more segments** with a texture (8x24 instead of 6x14). Its
  faces are flat chords, the UVs are linear in longitude and latitude, and 14
  slices visibly bend a straight cloud edge.
- **It reflects.** The shared reflection probe draws the dome, so the painted
  sky appears in chrome and car paint without any extra setting and at no extra
  cost beyond the texture.

## Runtime color updates

Day/night and Set Sky Color updates change only the dome vertex colors. The
color version invalidates cached color packets; positions, UVs and bounds stay
valid. Scene loading and texture/shape changes still rebuild the full dome.
This applies to both painted and gradient skies and both renderer backends.
The color calculation and triangle order are unchanged. A missing or inconsistent
binding falls back to the full builder.

## What it will not do

- **Light the scene.** Baked GI, the probes and the ambient term still read the
  gradient colours. Match them to the picture in the preset if the bounce
  light should agree with it.
- **Move its sun.** A painted sun is part of the picture. With the day/night
  cycle running, the real sun sweeps and the painted one stays put. Pick a
  sunless panorama for a scene whose clock runs, or accept the mismatch.
- **Show the tint in the editor.** The viewport shows the panorama at the
  authored hour only.

## How it is built

`src/skytex.cpp` is the one crop (`skytex::crop`) and the one UV rule
(`skytex::domeUv`). texbake ships its pixels, the viewport uploads the same
pixels, and codegen emits `SKY_TEXTURE_PATHS` / `SKY_TEXTURE_YAWS` /
`SKY_TEXTURE_VMAX` into `inc/scene_data.hpp` only when some scene has a sky
texture, so every other project regenerates byte for byte. The generated
`buildSkyDome` compiles its textured half against `SKY_TEXTURES_ON`; its UV
formula is the third copy of `domeUv`, so change all three together.

The texture wraps REPEAT across (the panorama closes on itself) and CLAMP down.
Each quad carries its own corners, so the last slice ends at u = 1 instead of
wrapping back to 0 inside a triangle.

Verified in PCSX2 with a test panorama carrying text around the horizon: the
text reads correctly, so the mapping is not mirrored.

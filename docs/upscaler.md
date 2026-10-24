# The upscaler (BLSS)

BLSS (Bieda-Level Super Sampling) draws part of a PS2 frame into a reduced GS
render target and blows it back up with one bilinear pass, trading GS fill for
picture sharpness. It is meant for scenes whose frame is limited by **fill** -
overdraw from smoke, haze, fire, rain and other layered transparency - not as a
universal speed switch: most TyraX frames are limited by the EE, and halving
the pixels of an EE-bound frame saves nothing.

It has two shapes, and the second is the one to reach for first:

| Draw reduced | What renders at the reduced raster | What it costs | What it cannot be combined with |
|---|---|---|---|
| **Particles only** | the particle emitters (smoke, haze, fire, rain) | two quarter-area copies + one full-screen blend on the GS; a reduced colour **and** z target in VRAM, the full-size z stays | nothing |
| **Whole 3D scene** | everything 3D | one full-screen pass; the z buffer shrinks with the raster, so it hands back VRAM at 2x2 | depth of field, linked portals, split screen, frame extrapolation (the build refuses the pair, naming the scene) |

HUD, menus, text and post effects always draw at full resolution.

`examples/upscaler-lab` is the reference project: a haze-filled yard with a
shadow-casting lamp.

## Settings

*Project > Preferences > Graphics > Advanced*:

| Setting | Meaning |
|---|---|
| Use the upscaler | the project default; a scene may override it (*Scene > Scene Preferences > Upscaler (BLSS)*) |
| Draw reduced | **Whole 3D scene** or **Particles only** - project-wide, because it decides what VRAM is reserved at boot |
| Adapt resolution to frame budget | starts every scene native; after four frames over 1.35 display fields it switches to the reduced raster, and after 150 frames within 1.10 fields it probes native again |
| Render scale | **2x2** (a quarter of the pixels) or **1x2** (half height only, keeps horizontal detail) |

Preferences prints the exact VRAM cost for this project's display mode.

## Particles only

The opaque scene renders exactly as it would without the upscaler, at full
resolution and against the full z buffer. Then, right before the first emitter
of the main view:

1. the scene depth is copied, point-sampled, into a quarter-area z target - the
   z buffer is read as a PSMZ texture and written into a frame of the same
   format, so no bit changes;
2. the scene colour is box-filtered into the quarter-area colour target, with
   alpha 0;
3. the emitters draw into that target, depth-tested against the copy, with the
   GS's FBA bit marking every pixel they touch;
4. the mark becomes an exact 0 / 0x80 alpha (a destination-alpha pass at 32-bit
   colour; the 16-bit A bit is read through TEXA for free), and the target is
   blended over the display buffer by that alpha. Pixels no particle touched
   keep their full-resolution value, and the edge of the particle layer fades
   over one reduced texel instead of stepping.

The emitters draw with the **native projection**: only the VU1 screen scale
shrinks (`RendererCoreDepth::screenScaleX`, 2048 / scale). The billboard
program drops a quad whose corner leaves the GS's ±2048 raster window - that
is what removes a huge puff right in front of the camera - and that test reads
clip space, so it keeps exactly the native threshold. A half-size projection
would have kept puffs up to twice as close.

The trade: whatever sits **under** a particle is seen at the reduced
resolution, so geometry behind dense smoke softens. Geometry, textures and the
HUD everywhere else stay sharp. Split screen draws its particles at full
resolution (one full-screen copy cannot serve two scissored halves), and a
frame with no live emitter skips the copies entirely.

## At runtime

The **Set Upscaler (BLSS)** flow node switches it while the game runs - for
example *On* when the player walks into the burning street, *Off* on the way
out:

| Mode | Effect |
|---|---|
| Scene default | what *Scene Preferences* says for the current scene |
| Off (native) | full resolution |
| On (reduced) | the reduced raster, in the project's *Draw reduced* shape |
| Automatic | the frame-budget switch above, for this scene only |

A scene load goes back to *Scene default*. A switch is free - having the node
anywhere in the project makes the build reserve both rasters at boot, so it
moves no VRAM and evicts no texture. Scripts reach the same switch through
`ctx.upscaler` (-1 leave, 0 scene default, 1 off, 2 on, 3 automatic); a project
that switches only from a script needs the node somewhere (or *Adapt
resolution*) so the build reserves the rasters. In *Whole 3D scene* mode a
scene with depth of field, a portal or split screen must not be switched on -
use *Particles only* there.

`examples/upscaler-lab` maps it to the pad: Triangle = native, R1 = reduced.

## Measured

Real-PS2 numbers, PAL 512x448, 2x2:

| arm | 50-frame windows | `work` mean | per-arm means |
|---|---|---|---|
| native, lamp on | 27 | **34.79 ms** | 35.18 / 34.63 |
| native, lamp off | 18 | 35.38 ms | 35.91 / 34.84 |
| particles only 2x2, lamp on | 56 | **13.83 ms** | 13.88 / 13.75 / 13.88 |
| particles only 2x2, lamp off | 18 | 13.64 ms | 13.92 / 13.37 |

**2.5x less frame work** (-21.0 ms) with the particles reduced, the same in
every alternating arm (window sd under 1.3 ms); the bracket itself - two
quarter-area copies, the mask and the composite - reads about 1 ms of it. The
spot lamp's shadow volumes cost nothing measurable from this vantage (the
casters are ~25 units away and small on screen). An earlier run with a linked
portal pair in the scene measured 38.62 -> 19.30 ms (2.0x): the portal submits
the scene a second time, EE work the upscaler cannot touch. Measured
2026-10-23, frozen camera at the player's start in `examples/upscaler-lab`.

PCSX2's software renderer under-reports GS fill about 76x
([profiling.md](profiling.md), "The calibration gate"), so no PCSX2 timing of
this feature is admissible - judge pictures there and frame times on hardware.

## The neural reconstruction that used to be here

Until 2.0.0 BLSS also had a *Neural* mode: a 6-12-3 network chose, per 32x32
tile, how much nearest, temporal and sharpen reconstruction to blend in, fed by
screen boxes the EE described every submitted bag with. It was removed. On
held-out scenes it scored +0.29 to +0.42 dB over plain bilinear (within one
fold's standard deviation), the shipped net chose plain bilinear on most frames
anyway, and it cost about 5 ms of EE a frame on top of the composite - so its
break-even was 13 full-screen coverages against 2.6 for plain. The editor still
opens projects that carry its settings and drops them on the next save (format
118).

## Files

- Engine: `vendor/tyra/engine/{inc,src}/renderer/core/blss/`
- Editor settings UI: `src/blss_ui.cpp`
- Project fields: `blssEnabled`, `blssScale`, `blssAdaptive`, `blssParticles`;
  per scene `"blss": { "enabled" }`
- Codegen: `blssInit` / `blssSceneSetup` / `blssSceneRender` in
  `src/templates.cpp`, the particle bracket in `renderScene()` and the runtime
  switch in `updateAdaptiveResolution()` (`src/game_templates.inc`)

For GS memory pressure see [GS VRAM](gs-vram.md).

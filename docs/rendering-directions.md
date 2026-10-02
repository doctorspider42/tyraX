# Rendering directions

These are candidate improvements for a visually convincing PS2 showcase, ranked
by visible benefit and integration cost rather than unmeasured hardware claims.
This is a direction document, not a promise to implement every technique.

## Completed foundation

Offline impostors now cover static OBJ models and generated trees, with CPU/GPU
baking and 4/8/16 camera-selected captures. The grove example includes corrected
floor tiling and model-bounds selection. See [impostors](impostors.md).

## Next priorities

1. **Wind and local foliage interaction.** Add height-weighted, phase-varied wind
   to nearby vegetation and a matching inexpensive card deformation at distance.
   Then bend nearby shrubs away from the player within a bounded interaction
   radius. Verify that switching representations does not reset motion. This is
   the preferred next visible step for the grove; ropes and cloth can follow.
2. **Better use of existing light probes.** Preserve more of the RGB L1 field on
   animated models. Test a moving character passing from an open courtyard into
   shade. Explore authored lighting-state blends only with consistent static
   surfaces. Reserve PRT experiments for rigid self-shadowed geometry.
3. **Better source assets.** Improve silhouettes, authored variation, texture/AO
   baking and palette quality. A normal-map bake is useful only when the renderer
   evaluates it; captured source lighting must not fight scene lighting.

Impostor batching and background GPU baking are follow-ups when profiling shows
CPU draw submission or authoring stalls; adding more angles everywhere is not a
substitute for measuring atlas residency and overdraw.

## Conditional projects

- **Crowds:** shared poses, existing animation/mesh LOD, flow fields for common
  destinations and local avoidance. PCA reconstruction is an experiment, not an
  assumed improvement over skeletal skinning.
- **Texture pages:** first prove geometry-aligned pages, residency and fallback
  mips with ordinary textures. IPU decoding is a separate investigation after
  profiling. Page boundaries can require geometry splits; a UV offset alone
  cannot map a triangle across independently placed pages.
- **SDF signs/icons:** alpha-tested distance fields are promising for magnified
  graphics, without promising perfect small text or arbitrary magnification.
- **Audio occlusion:** build on rooms/reverb and sparse source-listener tests;
  camera-space visibility is not acoustic visibility.
- **Streaming and VU optimisation:** extend the existing terrain LOD, layer
  streaming and ISO load-group ordering only against a measured bottleneck.
  Scheduling/search and compression need console measurements, not assumed gains.

## Existing foundations and limits

TyraX already has baked multi-bounce GI, quantized L1 probes, terrain LOD,
streaming layers, ISO ordering, animation LOD, BLSS and camera frame extrapolation.
Half-height rendering reduces pixel work, not the number of transformed vertices.
Camera translation needs depth for reprojection; camera rotation does not.
GS render targets still cost drawing, VRAM and synchronization, and GS cannot
perform a general dependent texture read for a motion-vector composite.

The proposed visual fixture is a walkable grove around weathered ruins: distant
canopies, detailed nearby trunks, coherent lighting and a few responsive details.
Each new system needs a reproducible camera path, an off/on comparison and an
honest distinction between emulator verification and console measurements.

## Sources behind the assessment

- [Sloan, Kautz and Snyder: PRT](https://www.microsoft.com/en-us/research/wp-content/uploads/2017/01/prt.pdf).
- [Sloan: Stupid SH Tricks](https://www.ppsloan.org/publications/StupidSH36.pdf).
- [Valve: distance-field magnification](https://cdn.fastly.steamstatic.com/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf).
- [Losasso and Hoppe: geometry clipmaps](https://hhoppe.com/geomclipmap.pdf).

Historical claims that no PS2 game used a technique, and fixed percentage speedups
without measurements, are deliberately not used as project requirements.

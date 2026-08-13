# Terrain painting

Terrain painting blends material textures directly on the scene's ground. Open
the **Terrain Editor** and choose **Paint (6)**.

![The Terrain Editor in Paint mode beside the blocks-terrain example.](img/terrain-painting.png)

## Brush

Drag in the viewport to paint the selected layer.

- **Radius** sets the brush size.
- **Strength** sets how quickly the layer builds up.
- **Falloff** softens the edge.
- **Erase** removes the selected layer.

Painting is non-destructive: TyraX stores blend weights, not a flattened ground
texture. Sculpting and painting can be mixed freely.

## Layers

The base layer covers the whole terrain. Add more layers above it and assign a
material texture to each one. Layers higher in the list win where their painted
weights overlap.

Each layer has:

- texture and tile size;
- tint and blend strength;
- stochastic tiling controls;
- macro variation controls.

Keep the stack short. Every extra painted layer adds another GS pass over the
affected terrain patches.

## Break up repetition

**Stochastic tiling** rotates and mirrors neighbouring samples so a texture does
not form an obvious grid. **Amount** controls the variation; **Patch size** sets
the size of each repeated group. It costs no extra texture memory.

**Macro variation** adds slower colour change over many tiles. Use it lightly:
it is meant to break large uniform areas, not recolour the material.

## Blend resolution

Layer weights follow the terrain grid. Raise **Terrain detail** in Project
Preferences for sharper borders; this also increases terrain geometry and blend
data. A small brush cannot create detail finer than that grid.

The editor stores weights in `terrain-<scene>.splat`. Builds quantize them into
compact masks and draw only the layers used by each terrain patch. The same data
is read by procedural material filters and terrain-aware effects.

## Practical limits

- Prefer a few reusable materials over many near-duplicates.
- Paint broad regions first, then add detail.
- If a border looks blocky, raise terrain detail before repainting it.
- If distant ground shimmers, increase tile size or reduce high-frequency
  texture detail.

See [Terrain](terrain.md) for sculpting and terrain-free scenes, and
[Materials](material-painting.md) for editing the textures themselves.

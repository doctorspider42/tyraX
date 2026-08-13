# Materials and texture painting

The Material Editor previews `.mtl` files on their real model, edits their PS2
settings and paints layered textures. Open it with **Tools > Material Editor**.

![The Material Editor showing an altar material, its paint layers, smart mask controls and live model preview.](img/material-editor.png)

## Edit a material

Choose a material on the left. The centre column controls its colour, brightness,
texture, tiling, reflection and glow. The preview on the right can use a sphere,
the model that owns the material or any other project model.

Useful preview switches:

- **Spin** rotates the model.
- **Solid / Wireframe / UV** changes the view.
- **Light** uses scene ambience or a neutral studio light.
- **PS2 preview** shows the quantized texture that will actually ship.

The texture readout includes dimensions, bit depth and estimated GS memory.
Use 4-bit or 8-bit textures where the colour loss is acceptable; reserve
32-bit textures for gradients or alpha that truly need it.

## Duplicate or delete

**Duplicate** copies the `.mtl` and, when useful, its textures. References are
not changed until you assign the new material. **Delete** shows references first
and is not undoable.

## Paint on the model

Enable **Paint**, choose or create a paintable texture, then drag over the model.
The brush projects through the model's UVs, so paint lands in texture space and
survives camera changes.

- Radius, strength and falloff control the brush.
- Paint and erase work on the selected layer.
- **UV** exposes seams and overlap problems.
- Undo covers brush strokes and layer edits.

Models need usable UVs. The editor can generate replacement UVs without touching
the source model; they live in `<model>.uvs` and follow the model when it moves.

## Layers and masks

Layers are composited top to bottom. Each has visibility, opacity and a blend
mode. The stack is saved next to the texture in `<texture>.layers/`; the normal
PNG is the flattened result used by the game.

**+ Mask** limits a layer with a baked or procedural mask. Smart masks include
edge wear, cavities, ambient occlusion, thickness and noise. Their range,
breakup and inversion stay editable, so you can tune wear without repainting.

Presets save a reusable layer stack. They keep material logic, not model-specific
pixels, and are best for recurring looks such as painted metal or dusty stone.

## Create a paintable texture

Click **New paintable texture**, choose a resolution and base colour, then assign
it to the material. Power-of-two sizes are the safest choice. Start small:
256x256 is often enough on a PS2, especially after palette quantization.

For raytraced AO, curvature, thickness and high-poly projection, see
[Material map baking](material-baking.md).

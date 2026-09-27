# Prepare a vehicle in Blender

Start with the editable [Ravager scene](../examples/vehicle-playground/authoring/ravager.blend). It uses the same authoring workflow as the [driveable Ravager](../examples/vehicle-playground/README.md), and its export passes the vehicle import. Open it in Blender, save a copy under your own car's name, and work through these five checks.

![The Ravager body and four separate wheels, viewed from the front quarter](img/blender-vehicle-shape.png)

1. **Shape and axes.** Build at real-world size in metres. Point the nose along Blender **+X**, left along **+Y**, and up along **+Z**. Keep the front overhang shorter than the rear so the importer can recognize the nose. Apply object scale before export. The example body is a single editable mesh; its broad surfaces carry most of the silhouette. Spend triangles on the wheel arches, roofline and profile rather than tiny trim.

2. **Four wheels.** Keep `wheel front left`, `wheel front right`, `wheel rear left`, and `wheel rear right` as separate objects. Put each origin at its hub. They may share one mesh, as the Ravager does. Make both faces of the wheel look complete: the game reuses one unmirrored wheel mesh on both sides. Check their spacing in side and top views.

![Side view showing the axle positions and car profile](img/blender-vehicle-side.png)

3. **Materials and UVs.** Give opaque body surfaces one textured `body paint` material and pack their UVs into a small atlas. The Ravager uses a packed 256×256 image, so the `.blend` opens with its paint intact. Separate materials named `headlights` and `rear lights` let the game control lamp colour. A material with `glass` or `window` in its name marks the glazing. A simple dark interior is enough if the glass will be translucent.

![The Ravager's compact colour and ambient-occlusion atlas](img/blender-vehicle-atlas.png)

4. **Count and export.** In Blender, select the body and all four wheels. Choose **File → Export → glTF 2.0**, set **Format: glTF Binary (.glb)** and **Include: Selected Objects**, then export. Keep the body within your vehicle definition's triangle budget (2400 by default); aim for roughly 160 triangles per wheel. This source scene has 2072 body triangles and 160 per wheel. Put the `.glb` in your project's `res/models/` directory. The `.blend` is the editable source, not the game asset.

5. **Check in TyraX.** Import the `.glb` in the Vehicle Editor. Confirm it detects four wheels and reports plausible wheelbase, track and radius. Check the preview for correct orientation, material grouping and transparency. Set the model's texture depth to **8 bit** if a shaded 256×256 paint atlas bands at 4 bit; watch the project's VRAM budget. Then test the car in the game, including from behind and at driving distance. If the car has an authored far model, rebuild it against the same atlas when changing the main model; otherwise the importer may discard the far tier. See [Vehicles: importing a model](vehicles.md#importing-a-model) for the full bake and runtime details.

To rebuild this exact source scene and the reference images from its authoring script, run from the repository root:

```sh
blender -b --factory-startup --python examples/vehicle-playground/authoring/save-ravager-blend.py -- --preview <output-directory>
```

You can also edit the `.blend` by hand and export its five selected meshes directly; the generator is only a reproducible starting point.

The bundled Motor District `.glb` and far model are a matched older build. Keep them together when playing the example; this `.blend` is for authoring and learning.

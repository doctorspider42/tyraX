"""Save the editable Ravager scene and optional Blender preview renders.

Run inside Blender from the repository root:
  blender -b --factory-startup --python examples/vehicle-playground/authoring/save-ravager-blend.py -- --preview <directory>
"""

import argparse
import importlib.util
from pathlib import Path
import sys
import tempfile

import bpy
from mathutils import Vector


HERE = Path(__file__).resolve().parent


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blend", type=Path, default=HERE / "ravager.blend")
    parser.add_argument("--preview", type=Path, help="Render reference views and atlas here")
    opts = parser.parse_args(args)

    spec = importlib.util.spec_from_file_location("make_ravager", HERE / "make-ravager.py")
    ravager = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = ravager
    sys.path.insert(0, str(HERE))
    spec.loader.exec_module(ravager)

    # The existing authoring script builds the editable meshes, bakes and packs
    # their atlas, then verifies the same GLB export path used for vehicle imports.
    with tempfile.TemporaryDirectory(prefix="tyrax-ravager-") as tmp:
        sys.argv = [sys.argv[0], "--", "--out", str(Path(tmp) / "ravager.glb")]
        if opts.preview:
            sys.argv += ["--preview", str(opts.preview.resolve())]
        ravager.main()

    if opts.preview:
        camera = bpy.context.scene.camera
        camera.data.type = "ORTHO"
        camera.data.ortho_scale = 6.5
        camera.location = (0, 9, 1.05)
        camera.rotation_euler = (Vector((0, 0, 0.7)) - camera.location).to_track_quat("-Z", "Y").to_euler()
        bpy.context.scene.render.filepath = str(opts.preview.resolve() / "ravager-side-full.png")
        bpy.ops.render.render(write_still=True)

    # Preview lights, camera and ground are useful for renders but not part of
    # the five-mesh vehicle source file.
    for obj in list(bpy.data.objects):
        if obj.name not in {"body", "wheel front left", "wheel front right",
                            "wheel rear left", "wheel rear right"}:
            bpy.data.objects.remove(obj, do_unlink=True)
    bpy.context.scene.camera = None
    bpy.context.scene.unit_settings.system = "METRIC"
    for obj in bpy.data.objects:
        obj.select_set(False)
    body = bpy.data.objects["body"]
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.shading.type = "MATERIAL"
                space.region_3d.view_distance = 7
    blend = opts.blend.resolve()
    blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    print("RAVAGER saved editable source", blend)


if __name__ == "__main__":
    main()

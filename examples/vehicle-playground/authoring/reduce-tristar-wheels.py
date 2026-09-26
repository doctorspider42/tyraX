"""Blender: reduce the prepared Tristar's wheels, preserving their bounds.

blender -b --python reduce-tristar-wheels.py -- tristar-racer.glb tristar-lean.glb
The body, material assignments, UVs and node transforms remain authored.
Keep the source and output distinct; the game uses the derived GLB.
"""
from pathlib import Path
import sys
import bpy

source, output = [Path(p).resolve() for p in sys.argv[sys.argv.index('--') + 1:]]
assert source != output, 'Keep the original prepared model'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(source))
wheels = [o for o in bpy.context.scene.objects
          if o.type == 'MESH' and 'wheel' in o.name.lower()]
assert len(wheels) == 4
for obj in wheels:
    obj.data = obj.data.copy()
    bpy.context.view_layer.objects.active = obj
    def bounds():
        return [(min(v.co[a] for v in obj.data.vertices),
                 max(v.co[a] for v in obj.data.vertices)) for a in range(3)]
    before = bounds()
    modifier = obj.modifiers.new('PS2 wheel reduction', 'DECIMATE')
    modifier.ratio = .12
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    after = bounds()
    # Decimation must not change suspension anchors, tyre radius or width.
    for v in obj.data.vertices:
        for a in range(3):
            assert after[a][1] > after[a][0]
            v.co[a] = before[a][0] + (v.co[a] - after[a][0]) * (
                before[a][1] - before[a][0]) / (after[a][1] - after[a][0])
    obj.data.calc_loop_triangles()
    print('Reduced wheel:', obj.name, len(obj.data.loop_triangles), 'triangles')
bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLB')

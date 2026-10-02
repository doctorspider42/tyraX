"""Optional Blender step: --background --python prepare-keeper.py -- C:/Assets.

Convert Quaternius's CC0 EyeDrone to a self-contained, PS2-sized animated GLB.
The armature and actions survive; only the mesh is decimated. The result is
checked in, so ordinary builds need neither Blender nor the original pack.
"""
import sys
from pathlib import Path
import bpy

root=Path(sys.argv[sys.argv.index('--')+1])/'Sci-Fi Essentials Kit[Standard]'
out=Path(__file__).resolve().parent/'res'/'aster'
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(root/'glTF'/'Enemy_EyeDrone.gltf'))
for o in bpy.context.scene.objects:
    if o.type!='MESH':continue
    triangles=sum(len(p.vertices)-2 for p in o.data.polygons)
    if triangles>1000:
        bpy.context.view_layer.objects.active=o
        modifier=o.modifiers.new('PS2 triangle budget','DECIMATE')
        modifier.ratio=1000/triangles
        bpy.ops.object.modifier_apply(modifier=modifier.name)
for im in bpy.data.images:
    if im.size[0]>128 or im.size[1]>128:
        im.scale(128,128)
        im.pack()
bpy.ops.export_scene.gltf(filepath=str(out/'keeper.glb'),export_format='GLB',export_animations=True,export_yup=True)
license_text=(root/'License_Standard.txt').read_text()
(out/'CC0-Keeper.txt').write_text('\n'.join(line.rstrip() for line in license_text.splitlines())+'\n',encoding='utf-8')
print('Keeper exported with rig, actions and embedded 128px textures.')

"""Blender: prepare GGBotNet's CC0 Car 04 as a five-node vehicle.

blender -b Car4.blend --python prepare-ggbot.py -- OUTPUT.glb TEXTURE.png
The source mesh already has four disconnected wheels; no geometry is invented.
"""
import sys
from pathlib import Path
import bpy

output, texture = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.separate(type='LOOSE')
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
parts = [o for o in bpy.context.scene.objects if o.type == 'MESH']
wheels = [o for o in parts if .3 < o.dimensions.x < .5
          and .9 < o.dimensions.y < 1.2 and .9 < o.dimensions.z < 1.2]
assert len(wheels) == 4, 'Source changed: expected four disconnected wheel islands'
bpy.ops.object.select_all(action='DESELECT')
body = [o for o in parts if o not in wheels]
for o in body:
    o.select_set(True)
bpy.context.view_layer.objects.active = body[0]
bpy.ops.object.join()
body[0].name = 'GGBot Rally body'
for i, wheel in enumerate(wheels):
    wheel.name = f'Wheel {i + 1}'
image = bpy.data.images.load(str(Path(texture).resolve()))
mat = bpy.data.materials.new('GGBot paint')
mat.use_nodes = True
shader = mat.node_tree.nodes.get('Principled BSDF')
tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
tex.image = image
mat.node_tree.links.new(tex.outputs['Color'], shader.inputs['Base Color'])
shader.inputs['Roughness'].default_value = .28
for o in bpy.context.scene.objects:
    if o.type != 'MESH':
        continue
    o.data.materials.clear()
    # The importer groups parts by material. Distinct slots retain the rigid
    # node ownership needed by its geometric wheel detector.
    own_mat = mat.copy()
    own_mat.name = o.name
    o.data.materials.append(own_mat)
    # Source is oversized; output is approximately 4.1 m long.
    o.location *= .65
    o.scale *= .65
    o.select_set(True)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
bpy.ops.export_scene.gltf(filepath=str(Path(output).resolve()), export_format='GLB',
                          use_selection=True, export_yup=True)

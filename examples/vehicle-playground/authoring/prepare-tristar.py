"""Prepare designersoup's Tristar Racer for this game (Blender 5.2).
blender -b --python prepare-tristar.py -- SOURCE.fbx OUTPUT.glb
Source texture must remain beside the FBX in its original .fbm directory.
The vehicle bake simplifies wheels to its authored triangle budget.
This preparation preserves source geometry and its UV palette.
"""
from pathlib import Path
import sys, math
import bpy
import bmesh
from mathutils import Matrix
source, output = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(Path(source).resolve()))
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
wheels = [o for o in objects if 'wheel' in o.name.lower()]
assert len(wheels) == 4
# FBX wheels can share mesh data and inherit the body's transform.
world = {o: o.matrix_world.copy() for o in objects}
for o in objects:
    o.parent = None
    o.matrix_world = world[o]
    o.data = o.data.copy()
for o in objects:
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    o.matrix_world = Matrix.Rotation(math.pi / 2, 4, 'Z') @ o.matrix_world
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
    for slot in o.material_slots:
        slot.material = slot.material.copy()
        slot.material.name = o.name + ' ' + slot.material.name
    if o in wheels:
        # Runtime repeats one wheel mesh at all four anchors. Mirror its
        # outward half so the rim is visible from both sides of the car.
        bm = bmesh.new()
        bm.from_mesh(o.data)
        positive = o.location.x > 0
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
            plane_co=(0,0,0), plane_no=(1,0,0), dist=1e-6,
            clear_inner=positive, clear_outer=not positive)
        dup = bmesh.ops.duplicate(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces))
        for v in dup['geom']:
            if isinstance(v,bmesh.types.BMVert): v.co.x = -v.co.x
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(o.data)
        bm.free()
    o.data.calc_loop_triangles()
    print('PREPARED',o.name,len(o.data.loop_triangles),list(o.dimensions),list(o.location))
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(Path(output).resolve()), export_format='GLB',
                          use_selection=True, export_yup=True)

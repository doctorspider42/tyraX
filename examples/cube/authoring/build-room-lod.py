"""Blender: -b --factory-startup --python authoring/build-room-lod.py.

Standalone, complete far room. Project six orthographic material/AO captures
onto the original 20 solid slabs: preserve apertures, bake detail into colour.
Does not modify the near model, manifest, prefabs or procedural settings.
"""
import bpy
import math
import tempfile
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'res/models'
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'authoring/cube-room.blend'))
scene = bpy.context.scene
preview_camera = scene.camera
source = [o for o in scene.objects if o.type == 'MESH']
shells = [o for o in source if o.name.startswith('Collision shell / ')]
assert len(shells) == 20, 'Expected the complete room collision shell'
scene.render.engine = 'CYCLES'
scene.cycles.samples = 16
scene.cycles.use_denoising = True
scene.render.resolution_x = scene.render.resolution_y = 256
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.view_settings.exposure = 0
scene.view_settings.gamma = 1

# Material colour with local AO, not the preview lamps baked into albedo.
for mat in list(bpy.data.materials):
    if not mat.use_nodes: continue
    tree = mat.node_tree
    base = tree.nodes.get('Principled BSDF').inputs['Base Color']
    ao = tree.nodes.new('ShaderNodeAmbientOcclusion')
    ao.inputs['Distance'].default_value = .7
    ao.samples = 16
    mix = tree.nodes.new('ShaderNodeMixRGB')
    mix.blend_type = 'MULTIPLY'
    mix.inputs[0].default_value = .35
    if base.is_linked:
        tree.links.new(base.links[0].from_socket, mix.inputs[1])
    else:
        mix.inputs[1].default_value = base.default_value
    tree.links.new(ao.outputs['AO'], mix.inputs[2])
    emit = tree.nodes.new('ShaderNodeEmission')
    tree.links.new(mix.outputs[0], emit.inputs['Color'])
    tree.links.new(emit.outputs[0], tree.nodes.get('Material Output').inputs['Surface'])

# Tyra Y-up -> Blender Z-up; all mapping vectors are in game coordinates.
def blender(v): return Vector((v[0], -v[2], v[1]))
def game(v): return Vector((v.x, v.z, -v.y))

# Each tile: plane centre, screen-right axis, screen-up axis, inward normal.
faces = []
for side in range(4):
    a = side*math.pi/2
    u, v = Vector((math.cos(a),0,math.sin(a))), Vector((0,1,0))
    n = u.cross(v)
    faces.append((-n*6.5+Vector((0,7,0)), u, v, n))
faces += [(Vector((0,.5,0)),Vector((1,0,0)),Vector((0,0,-1)),Vector((0,1,0))),
          (Vector((0,13.5,0)),Vector((1,0,0)),Vector((0,0,1)),Vector((0,-1,0)))]
centres = {o: sum((game(o.matrix_world @ Vector(p)) for p in o.bound_box),Vector())/8 for o in source}
bpy.ops.object.camera_add()
bake_cam = bpy.context.object
bake_cam.name = 'Temporary orthographic capture'
bake_cam.data.type = 'ORTHO'
bake_cam.data.ortho_scale = 14
bake_cam.data.clip_start = .01
bake_cam.data.clip_end = 4
scene.camera = bake_cam

atlas = bpy.data.images.new('Cube room LOD atlas', width=1024, height=512, alpha=False)
# Two unused tiles also provide a padded solid colour for edges/exterior.
pixels = [.09,.12,.15,1] * (1024*512)
with tempfile.TemporaryDirectory(prefix='cube-lod-') as tmp:
    for tile,(origin,u,v,n) in enumerate(faces):
        for obj in source:
            # Only this surface: perpendicular walls must not occlude the bake.
            obj.hide_render = abs((centres[obj]-origin).dot(n)) > .45
        bake_cam.location = blender(origin+n*2)
        bake_cam.rotation_euler = blender(-n).to_track_quat('-Z', 'Y').to_euler()
        # Align the camera's local up exactly with the UV projection, including
        # horizontal surfaces where to_track_quat otherwise picks another roll.
        from mathutils import Matrix
        bake_cam.rotation_euler = Matrix((blender(u),blender(v),blender(n))).transposed().to_euler()
        scene.render.filepath = str(Path(tmp)/f'face-{tile}.png')
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(scene.render.filepath, check_existing=False)
        data = list(img.pixels)
        for y in range(256):
            for x in range(256):
                i = (y*256+x)*4
                j = (((tile//4)*256+y)*1024+(tile%4)*256+x)*4
                alpha = data[i+3]
                pixels[j:j+4] = [data[i+k]*alpha + (.09,.12,.15)[k]*(1-alpha) for k in range(3)] + [1]
        bpy.data.images.remove(img)
atlas.pixels = pixels
atlas.filepath_raw = str(OUT/'cube-room-lod.png')
atlas.file_format = 'PNG'
atlas.save()

mat = bpy.data.materials.new('Complete room LOD - one atlas')
mat.use_nodes = True
tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
tex.image = atlas
mat.node_tree.links.new(tex.outputs['Color'],mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
mat.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value = .8
lod = []
lines = ['# Complete cube room far LOD; includes shell, Y-up, metres',
         'mtllib cube-room-lod.mtl','usemtl room_lod']
index = 1
triangles = 0
for src in shells:
    obj = src.copy()
    obj.data = src.data.copy()
    obj.name = src.name.replace('Collision shell / ', 'LOD / ')
    scene.collection.objects.link(obj)
    obj.hide_render = False
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    mesh = obj.data
    uv = mesh.uv_layers.active or mesh.uv_layers.new(name='Room projection')
    for poly in mesh.polygons:
        centre = game(obj.matrix_world @ poly.center)
        normal = game(obj.matrix_world.to_3x3().inverted().transposed() @ poly.normal).normalized()
        tile = next((i for i,(o,u,v,n) in enumerate(faces)
                     if normal.dot(n) > .99 and abs((centre-o).dot(n)) < .001), None)
        for li in poly.loop_indices:
            if tile is None:
                uv.data[li].uv = (.625,.75)  # centre of the solid edge tile
            else:
                origin,u,v,n = faces[tile]
                p = game(obj.matrix_world @ mesh.vertices[mesh.loops[li].vertex_index].co)-origin
                uv.data[li].uv = ((tile%4 + .5+p.dot(u)/14)/4,
                                   (tile//4 + .5+p.dot(v)/14)/2)
    mesh.calc_loop_triangles()
    lines.append('o '+obj.name.replace(' ','_').replace('/',''))
    for tri in mesh.loop_triangles:
        for li in tri.loops:
            p = game(obj.matrix_world @ mesh.vertices[mesh.loops[li].vertex_index].co)
            t = uv.data[li].uv
            lines += [f'v {p.x:.6f} {p.y:.6f} {p.z:.6f}',f'vt {t.x:.6f} {t.y:.6f}']
        lines.append('f '+' '.join(f'{i}/{i}' for i in range(index,index+3)))
        index += 3
        triangles += 1
    lod.append(obj)
assert triangles == 240
(OUT/'cube-room-lod.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(OUT/'cube-room-lod.mtl').write_text('newmtl room_lod\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\nmap_Kd cube-room-lod.png\n',encoding='utf-8')

for obj in source: bpy.data.objects.remove(obj,do_unlink=True)
bpy.data.objects.remove(bake_cam,do_unlink=True)
scene.camera = preview_camera
scene.render.film_transparent = False
scene.view_settings.view_transform = 'AgX'
scene.render.resolution_x,scene.render.resolution_y = 1200,900
scene.cycles.samples = 24
scene.render.filepath = str(ROOT/'authoring/room-lod-preview.png')
atlas.pack()
bpy.ops.object.select_all(action='DESELECT')
for obj in lod: obj.select_set(True)
bpy.context.view_layer.objects.active = lod[0]
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'authoring/cube-room-lod.blend'))
bpy.ops.render.render(write_still=True)
print(f'CUBE_ROOM_LOD: {triangles} triangles, complete 20-slab shell, 6 open apertures')

"""Blender 4+/5+: -b --factory-startup --python authoring/build-room.py.

Editable, named mesh pieces; exports the decoration mesh in Tyra's Y-up axes.
The original twenty slabs remain the structural/collision shell in the prefab.
"""
import bpy
import json
import math
import random
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'res/models'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
random.seed(7)

# A single small atlas keeps all details in one material bag per room tint.
colors = [(0.12, .15, .18), (.42, .48, .52), (.65, .71, .73),
          (.23, .28, .32), (.9, .65, .18), (.035, .05, .06),
          (.76, .92, .95), (.35, .57, .61)]
img = bpy.data.images.new('Industrial palette', width=256, height=128)
pixels = []
for y in range(128):
    for x in range(256):
        c = colors[(y // 64) * 4 + x // 64]
        noise = random.uniform(-.018, .018)
        pixels.extend([max(0, min(1, v + noise)) for v in c] + [1])
img.pixels = pixels
img.filepath_raw = str(OUT / 'cube-room.png')
img.file_format = 'PNG'
img.save()
mat = bpy.data.materials.new('Room atlas')
mat.use_nodes = True
tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
tex.image = img
bsdf = mat.node_tree.nodes.get('Principled BSDF')
mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
bsdf.inputs['Roughness'].default_value = .76

details = []

def piece(name, verts, faces, slots):
    # Author Y-up; Blender is Z-up. This proper rotation preserves winding.
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([(x, -z, y) for x, y, z in verts], [], faces)
    mesh.materials.append(mat)
    uv = mesh.uv_layers.new()
    for poly, slot in zip(mesh.polygons, slots):
        coords = [mesh.vertices[mesh.loops[li].vertex_index].co for li in poly.loop_indices]
        axes = sorted(range(3), key=lambda a: max(p[a] for p in coords)-min(p[a] for p in coords), reverse=True)[:2]
        low = [min(p[a] for p in coords) for a in axes]
        span = [max(p[a] for p in coords)-lo for a,lo in zip(axes,low)]
        # Keep UVs well inside each atlas tile, including mip/filter borders.
        for j, li in enumerate(poly.loop_indices):
            a, b = [.15+.7*(coords[j][axis]-lo)/max(s,.00001) for axis,lo,s in zip(axes,low,span)]
            uv.data[li].uv = ((slot % 4 + a) / 4, (slot // 4 + b) / 2)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    details.append(obj)
    return obj

def panel(name, origin, u, v, w, h, depth=.08, slot=1, bevel=.10):
    o, u, v = Vector(origin), Vector(u), Vector(v)
    n = u.cross(v)
    def outline(a, b, c):
        # Narrow handles need real top edges, not coincident chamfer corners.
        c = min(c, .8*a, .8*b)
        return [(-a+c,-b),(a-c,-b),(a,-b+c),(a,b-c),
                (a-c,b),(-a+c,b),(-a,b-c),(-a,-b+c)]
    verts = []
    for ring, z in [(outline(w/2,h/2,bevel),0),
                    (outline(w/2-.045,h/2-.045,max(.02,bevel-.025)),depth)]:
        verts.extend([o + u*x + v*y + n*z for x,y in ring])
    faces = [(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    faces.append(tuple(range(8,16)))
    return piece(name, verts, faces, [3]*8+[slot])

def quad(name, origin, u, v, w, h, slot):
    o,u,v = Vector(origin),Vector(u),Vector(v)
    return piece(name,[o+u*x+v*y for x,y in [(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]],[(0,1,2,3)],[slot])

# Four matching bulkheads. The clear doorway stays 5 wide and 6 high.
for side in range(4):
    angle = side * math.pi/2
    u = Vector((math.cos(angle),0,math.sin(angle)))
    v = Vector((0,1,0))
    n = u.cross(v)
    base = -n*6.49
    def at(x,y,d=0): return base+u*x+v*y+n*d
    for x in (-4.65,4.65):
        for y,h in ((2.0,2.65),(5.2,2.85),(8.5,2.85),(11.6,2.6)):
            panel(f'Wall {side} cassette {x} {y}',at(x,y),u,v,3.35,h,slot=1 if y<8 else 2)
        panel(f'Wall {side} vent recess {x}',at(x,2,.09),u,v,2.4,.95,.025,5)
        for y in (1.70,1.90,2.10,2.30):
            quad('Vent louver',at(x,y,.13),u,v,2.15,.075,3)
        panel('Access handle',at(x,5.2,.09),u,v,.16,.65,.07,2,.06)
        quad('Status light',at(x,6.1,.095),u,v,.55,.10,6)
    for x in (-2.79,2.79):
        panel('Door jamb',at(x,3.25,.02),u,v,.52,5.5,.20,3,.16)
        quad('Jamb light',at(x,3.6,.23),u,v,.09,2.6,6)
    panel('Hatch crown',at(0,6.5),u,v,6.1,.85,.22,3,.28)
    panel('Crown inset',at(0,6.5,.23),u,v,2.5,.38,.02,5,.08)
    for x in (-.9,-.45,0,.45,.9):
        quad('Amber crown indicator',at(x,6.5,.26),u,v,.23,.15,4)
    panel('Upper central service panel',at(0,9.8),u,v,4.6,4.8,.09,1,.25)
    panel('Upper dark inset',at(0,9.8,.1),u,v,2.8,2.5,.03,0,.2)
    for x in (-.85,0,.85):
        quad('Vertical heat sink',at(x,9.8,.14),u,v,.16,1.9,3)
    for x in (-6.22,6.22):
        panel('Corner spine',at(x,7),u,v,.32,12.6,.18,3,.1)
    # Segmented safety markings above the open passage.
    for x in (-2.2,-1.65,-1.1,-.55,0,.55,1.1,1.65,2.2):
        quad('Warning stripe',at(x,7.12,.025),u,v,.30,.19,4)

# Deck and overhead panels leave the central 5x5 hatch completely open.
for ceiling in (False,True):
    o = Vector((0,13.49 if ceiling else .51,0))
    u = Vector((1,0,0))
    v = Vector((0,0,1 if ceiling else -1))
    n = u.cross(v)
    for x,y,w,h in [(-4.65,0,3.45,12.5),(4.65,0,3.45,12.5),
                     (0,-4.65,5.0,3.45),(0,4.65,5.0,3.45)]:
        # Divide the long runners into modules with deep visible seams.
        count = 4 if h > 10 else 1
        for k in range(count):
            yy = y + (k-(count-1)/2)*h/count
            panel('Ceiling tile' if ceiling else 'Deck tile',o+u*x+v*yy,u,v,w,h/count-.14,.055,3 if ceiling else 0,.18)
            if not ceiling:
                for offset in (-.6,0,.6):
                    quad('Deck grip',o+u*(x+offset)+v*yy+n*.065,u,v,.06,h/count-.5,1)
    for k in range(4):
        a = k*math.pi/2
        ru = u*math.cos(a)+v*math.sin(a)
        rv = -u*math.sin(a)+v*math.cos(a)
        panel('Hatch rim',o+rv*2.76,ru,rv,5.85,.44,.10,2,.12)
        for x in (-2,-1,0,1,2):
            quad('Hatch hazard',o+rv*2.76+ru*x+n*.11,ru,rv,.40,.22,4)

# Export directly from the editable Blender meshes, triangulated by Blender.
obj_lines = ['# Blender-authored cube room detail; Y-up, metres', 'mtllib cube-room.mtl', 'usemtl room']
index = 1
triangles = 0
for obj in details:
    # Retain the editable bevelled source, evaluate a PS2 LOD for export/render.
    if len(obj.data.polygons) > 1:
        dec = obj.modifiers.new('PS2 detail budget', 'DECIMATE')
        dec.ratio = .36
        dec.use_collapse_triangulate = True
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    obj_lines.append('o '+obj.name.replace(' ','_'))
    for tri in mesh.loop_triangles:
        for li in tri.loops:
            p = obj.matrix_world @ mesh.vertices[mesh.loops[li].vertex_index].co
            uv = mesh.uv_layers.active.data[li].uv
            obj_lines.append(f'v {p.x:.5f} {p.z:.5f} {-p.y:.5f}')
            obj_lines.append(f'vt {uv.x:.5f} {uv.y:.5f}')
        obj_lines.append('f '+' '.join(f'{i}/{i}' for i in range(index,index+3)))
        index += 3
        triangles += 1
    evaluated.to_mesh_clear()
(OUT/'cube-room.obj').write_text('\n'.join(obj_lines)+'\n',encoding='utf-8')
(OUT/'cube-room.mtl').write_text('newmtl room\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\nmap_Kd cube-room.png\n',encoding='utf-8')

# Reference shell is stored in the .blend, but NOT in the decoration export.
manifest = json.loads((ROOT/'cube.tyra').read_text())
shell_mat = bpy.data.materials.new('Structural shell - reference')
shell_mat.diffuse_color = (.18,.23,.27,1)
shell_mat.use_nodes = True
shell_mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = (.09,.12,.15,1)
for item in manifest['prefabs'][0]['objects']:
    if item['type'] != 'box': continue
    x,y,z = item['position']
    sx,sy,sz = item['scale']
    bpy.ops.mesh.primitive_cube_add(size=1,location=(x,-z,y))
    shell = bpy.context.object
    shell.name = 'Collision shell / '+item['name']
    shell.scale = (sx,sz,sy)
    shell.data.materials.append(shell_mat)

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 24
scene.cycles.use_denoising = True
scene.world.color = (.08,.08,.08)
for loc,power,size in [((0,0,12.5),1400,8),((0,3,5),400,5),((0,-5,4),350,4)]:
    bpy.ops.object.light_add(type='AREA',location=loc)
    lamp = bpy.context.object
    lamp.data.energy = power
    lamp.data.shape = 'DISK'
    lamp.data.size = size
    lamp.rotation_euler = (Vector((0,0,3))-lamp.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(5.4,5.5,4.7))
cam = bpy.context.object
cam.rotation_euler = (Vector((-1,-3,6))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.lens = 20
scene.camera = cam
scene.render.resolution_x = 1200
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
scene.render.filepath = str(ROOT/'authoring/room-preview.png')
scene.view_settings.view_transform = 'AgX'
img.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'authoring/cube-room.blend'))
bpy.ops.render.render(write_still=True)
print(f'CUBE_ROOM: {triangles} decoration triangles; {len(details)} editable pieces')

"""Blender 4+/5+: -b --factory-startup --python authoring/build-spruces.py.
Editable named solid tiers; deterministic Y-up export with flat outward normals.
Presentation instances never affect the source mesh pivots.
"""
import math
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector
ROOT = Path(__file__).resolve().parents[1]
OUT, HERE = ROOT/'res/models', ROOT/'authoring'
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.preferences.filepaths.save_version=0
COLORS={'bark':(.340,.240,.150),'needles':(.160,.380,.190)}
MATS={}
for name,color in COLORS.items():
    mat=bpy.data.materials.new(name)
    mat.diffuse_color=(*color,1)
    mat.use_nodes=True
    bsdf=mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value=(*(c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4 for c in color),1)
    bsdf.inputs['Roughness'].default_value=1
    MATS[name]=mat

def piece(collection,name,vertices,faces,material):
    mesh=bpy.data.meshes.new(name)
    # Proper rotation: game Y-up to Blender Z-up, preserving winding.
    mesh.from_pydata([(x,-z,y) for x,y,z in vertices],[],faces)
    mesh.materials.append(MATS[material])
    bm=bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    obj=bpy.data.objects.new(name,mesh)
    collection.objects.link(obj)
    return obj

def ring(n,radius,y,phase,irregular=False):
    points=[]
    for i in range(n):
        a=phase+2*math.pi*i/n
        r=radius*(1-.16*(i%2) if irregular else 1)
        points.append((r*math.cos(a),y,r*math.sin(a)))
    return points

def crown(collection,name,rim,tip):
    n=len(rim)
    return piece(collection,name,rim+[tip],
                 [(i,(i+1)%n,n) for i in range(n)]+[tuple(reversed(range(n)))],'needles')

def export(collection,stem):
    lines=['# Blender-authored spruce; Y-up, base at origin',f'mtllib {stem}.mtl','s off']
    offset,normal_index=1,1
    for obj in collection.objects:
        mesh=obj.data
        mesh.calc_loop_triangles()
        lines+=['o '+obj.name.replace(' ','_'),'usemtl '+mesh.materials[0].name]
        for v in mesh.vertices:
            p=v.co
            lines.append(f'v {p.x:.6f} {p.z:.6f} {-p.y:.6f}')
        for tri in mesh.loop_triangles:
            n=tri.normal
            lines.append(f'vn {n.x:.6f} {n.z:.6f} {-n.y:.6f}')
            lines.append('f '+' '.join(f'{i+offset}//{normal_index}' for i in tri.vertices))
            normal_index+=1
        offset+=len(mesh.vertices)
    (OUT/f'{stem}.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    mtl='# Opaque colours copied from props.mtl; no textures\n'
    for name,color in COLORS.items():
        mtl+=f'newmtl {name}\nKa 1 1 1\nKd '+' '.join(f'{c:.3f}' for c in color)+'\nKs 0 0 0\nd 1\nillum 1\n\n'
    (OUT/f'{stem}.mtl').write_text(mtl,encoding='utf-8')

# Letter, height, radius, tiers, sides, skirt height, tier spacing.
# Classic, narrow spire, and broad low-skirt spruce.
SPECS=[('a',3.00,.57,5,6,.48,.45),
       ('b',3.34,.46,6,5,.56,.412),
       ('c',2.72,.60,4,7,.35,.55)]
for variant,(letter,height,radius,tiers,sides,skirt,step) in enumerate(SPECS):
    near=bpy.data.collections.new(f'spruce-{letter}')
    # Source collections are unlinked; collection instances present them.
    trunk=ring(3,.065,0,0)+ring(3,.040,1.12,0)
    piece(near,'Trunk '+letter,trunk,
          [(0,2,1),(3,4,5)]+[(i,(i+1)%3,(i+1)%3+3,i+3) for i in range(3)],'bark')
    for tier in range(tiers):
        t=tier/(tiers-1)
        r=radius*(1-.73*t)
        y=skirt+step*tier
        rim=ring(sides,r,y,.22*variant+tier*.39,True)
        # Rotated, unequal branch tips and long top leaders break regular cones.
        top=height if tier==tiers-1 else min(height-.25,y+.93-.10*t)
        crown(near,f'{letter} branch whorl {tier+1}',rim,
              (.025*math.sin(tier*2),top,.020*math.cos(tier*2)))
    points=[(v.co.x,v.co.z,-v.co.y) for obj in near.objects for v in obj.data.vertices]
    lo=[min(p[i] for p in points) for i in range(3)]
    hi=[max(p[i] for p in points) for i in range(3)]
    lod=bpy.data.collections.new(f'spruce-{letter}-lod')
    # Closed tetrahedral trunk (4) + closed pentagonal crown (8).
    piece(lod,'Far trunk '+letter,ring(3,.065,0,0)+[(0,1.12,0)],
          [(0,2,1),(0,1,3),(1,2,3),(2,0,3)],'bark')
    rim=ring(5,1,skirt,.22*variant)
    bounds=[(min(p[i] for p in rim),max(p[i] for p in rim)) for i in (0,2)]
    rim=[(lo[0]+(p[0]-bounds[0][0])/(bounds[0][1]-bounds[0][0])*(hi[0]-lo[0]),skirt,
          lo[2]+(p[2]-bounds[1][0])/(bounds[1][1]-bounds[1][0])*(hi[2]-lo[2])) for p in rim]
    crown(lod,'Far crown '+letter,rim,
          (.025*math.sin((tiers-1)*2),height,.020*math.cos((tiers-1)*2)))
    for is_lod,collection in enumerate((near,lod)):
        stem=f'spruce-{letter}'+('-lod' if is_lod else '')
        export(collection,stem)
        inst=bpy.data.objects.new(stem,None)
        inst.instance_type='COLLECTION'
        inst.instance_collection=collection
        bpy.context.scene.collection.objects.link(inst)
        inst.location=(-4.25+variant*3.4+is_lod*1.35,0,0)
        bpy.ops.object.text_add(location=(inst.location.x,-.72,-.015))
        label=bpy.context.object
        label.name='Label '+stem
        label.data.body=letter.upper()+(' / FAR 12' if is_lod else f' / NEAR {8+tiers*(2*sides-2)}')
        label.data.align_x='CENTER'
        label.data.size=.15
        label.data.materials.append(MATS['bark'])

scene=bpy.context.scene
scene.render.engine='CYCLES'
scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.world.color=(.22,.22,.22)
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.025))
floor=bpy.context.object
floor.name='Preview ground (not exported)'
mat=bpy.data.materials.new('Preview warm grey')
mat.diffuse_color=(.32,.34,.31,1)
mat.use_nodes=True
mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.22,.24,.21,1)
floor.data.materials.append(mat)
for location,power,size in [((-3,-4,7),850,6),((4,2,6),600,5)]:
    bpy.ops.object.light_add(type='AREA',location=location)
    lamp=bpy.context.object
    lamp.data.energy,lamp.data.size=power,size
    lamp.rotation_euler=(Vector((0,0,1.2))-lamp.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(.5,-13,6.5))
camera=bpy.context.object
camera.rotation_euler=(Vector((.5,0,1.45))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'
camera.data.ortho_scale=11.4
scene.camera=camera
scene.render.resolution_x,scene.render.resolution_y=1800,850
scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard'
scene.render.filepath=str(HERE/'spruces-preview.png')
bpy.ops.wm.save_as_mainfile(filepath=str(HERE/'spruces.blend'))
bpy.ops.render.render(write_still=True)
print('SPRUCES: six models, editable blend and preview saved')

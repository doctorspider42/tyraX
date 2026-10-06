# A quilted puffer vest for the Character Generator's "Your own clothes",
# modelled procedurally on a reference body (tyrax-editor --chargen-reference).
# blender -b --factory-startup --python make_vest.py -- reference-male.glb out_dir name
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
ref, out, name = argv[0], argv[1], argv[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=ref)
arm = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE'][0]
body = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]


def joint(n):
    return arm.matrix_world @ arm.data.bones['mixamorig:' + n].head_local


z_hem = joint('Hips').z + 0.02          # the hem a little above the hip joint
z_neck = joint('Neck').z - 0.01         # the neckline at the neck's base
groups = {g.index: g.name for g in body.vertex_groups}
ARM = ('Arm', 'ForeArm', 'Hand')
me = body.data.copy()
me.transform(body.matrix_world)
bm = bmesh.new()
bm.from_mesh(me)
dl = bm.verts.layers.deform.active


def weight_on(v, parts):
    w = 0.0
    if dl is None:
        return 0.0
    for gi, x in v[dl].items():
        nm = groups.get(gi, '')
        if any(nm.endswith(p) and ('Left' + p in nm or 'Right' + p in nm) for p in parts):
            w += x
    return w


keep = []
for f in bm.faces:
    c = f.calc_center_median()
    if not (z_hem < c.z < z_neck):
        continue
    if max(weight_on(v, ARM) for v in f.verts) > 0.35:
        continue  # the armholes
    keep.append(f)
keep = set(keep)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in keep], context='FACES')
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
# the largest connected piece only (stray bits under the arms)
seen, best = set(), []
for f in bm.faces:
    if f in seen:
        continue
    comp, stack = [], [f]
    seen.add(f)
    while stack:
        g = stack.pop()
        comp.append(g)
        for e in g.edges:
            for h in e.link_faces:
                if h not in seen:
                    seen.add(h)
                    stack.append(h)
    if len(comp) > len(best):
        best = comp
bset = set(best)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in bset], context='FACES')
bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
bm.normal_update()

# --- shape: a padded shell, quilted in horizontal bands ---------------------------
BAND = 0.075
H = z_neck - z_hem


def puff(z):
    t = (z - z_hem) / BAND
    return math.sin(math.pi * (t - math.floor(t))) ** 0.6


base = {v: v.co.copy() for v in bm.verts}
for v in bm.verts:
    v.co = base[v] + v.normal * (0.014 + 0.009 * puff(base[v].z))

# --- collar and hem: extrude the open rings -----------------------------------------
def boundary_loops():
    edges = [e for e in bm.edges if e.is_boundary]
    return edges


def ring_edges(pred):
    return [e for e in bm.edges if e.is_boundary and all(pred(v) for v in e.verts)]


cx = sum((v.co for v in bm.verts), Vector()) / len(bm.verts)
nk = joint('Neck')
top = ring_edges(lambda v: v.co.z > z_neck - 0.05 and math.hypot(v.co.x - nk.x, v.co.y - nk.y) < 0.11)
res = bmesh.ops.extrude_edge_only(bm, edges=top)
newv = [g for g in res['geom'] if isinstance(g, bmesh.types.BMVert)]
ring_top = max(v.co.z for e in top for v in e.verts)
for v in newv:  # a level top edge: the neckline ring is not
    d = Vector((v.co.x - cx.x, v.co.y - cx.y, 0.0))
    v.co += d.normalized() * 0.006
    v.co.z = ring_top + 0.04
collar = set(newv)
bot = ring_edges(lambda v: v.co.z < z_hem + 0.04)
res = bmesh.ops.extrude_edge_only(bm, edges=bot)
for g in res['geom']:
    if isinstance(g, bmesh.types.BMVert):
        g.co += Vector((0, 0, -0.018))
bm.normal_update()

# --- uv: cylindrical, front centre at u = 0.5, the seam at the back -----------------
uv = bm.loops.layers.uv.verify()
zlo = min(v.co.z for v in bm.verts)
zhi = max(v.co.z for v in bm.verts)
for f in bm.faces:
    us = []
    for l in f.loops:
        p = l.vert.co - cx
        u = math.atan2(p.x, -p.y) / (2 * math.pi) + 0.5
        us.append(u)
    if max(us) - min(us) > 0.5:  # straddles the back seam: keep it on one side
        us = [u if u > 0.5 else 0.999 for u in us]
    for l, u in zip(f.loops, us):
        z = l.vert.co.z
        v = 0.9 if l.vert in collar else min(0.86, (z - zlo) / (z_neck - zlo) * 0.86)
        l[uv].uv = (min(max(u, 0.001), 0.999), v)

# --- texture: orange nylon, quilting seams, a zip, a ribbed hem, a lined collar ----
S = 256
rng = np.random.default_rng(7)
img = np.zeros((S, S, 3), np.float32)
base_col = np.array([0.86, 0.40, 0.10], np.float32)
yy, xx = np.mgrid[0:S, 0:S]
v = (yy + 0.5) / S            # rows bottom-up, like Blender's pixels
u = (xx + 0.5) / S
zt = zlo + v / 0.86 * (z_neck - zlo)
t = (zt - z_hem) / BAND
fr = t - np.floor(t)
shade = 0.62 + 0.45 * np.sin(np.pi * fr) ** 0.7
seam = np.exp(-((np.minimum(fr, 1 - fr) * BAND) / 0.004) ** 2)
img[:] = base_col * shade[..., None]
img *= (1 - 0.55 * seam)[..., None]
img += (rng.standard_normal((S, S, 1)).astype(np.float32) * 0.015)
# the zip down the front, with its teeth
zipm = np.abs(u - 0.5) < 0.012
teeth = ((yy // 2) % 2 == 0) & (np.abs(u - 0.5) < 0.007)
img[zipm] = [0.10, 0.10, 0.11]
img[teeth & (v < 0.86)] = [0.55, 0.55, 0.58]
# ribbed hem band and the collar's darker lining
hem = v < 0.035
img[hem] = np.stack([0.42 + 0.06 * ((xx[hem] // 3) % 2)] * 3, -1) * base_col / base_col.max()
col = v > 0.86
img[col] = base_col * 0.7
img = np.clip(img, 0, 1)
bimg = bpy.data.images.new(name + '_tex', S, S, alpha=True)
px = np.ones((S, S, 4), np.float32)
px[..., :3] = img
bimg.pixels.foreach_set(px.ravel())
bimg.filepath_raw = os.path.join(out, name + '.png')
bimg.file_format = 'PNG'
bimg.save()

out_me = bpy.data.meshes.new(name)
bm.to_mesh(out_me)
ob = bpy.data.objects.new(name, out_me)
bpy.context.scene.collection.objects.link(ob)
mat = bpy.data.materials.new(name)
mat.use_nodes = True
tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
tex.image = bimg
mat.node_tree.links.new(tex.outputs['Color'], mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
out_me.materials.append(mat)
for o in list(bpy.context.scene.objects):
    o.select_set(o == ob)
bpy.context.view_layer.objects.active = ob
bpy.ops.wm.obj_export(filepath=os.path.join(out, name + '.obj'), export_selected_objects=True,
                      forward_axis='NEGATIVE_Z', up_axis='Y', export_materials=True, path_mode='RELATIVE',
                      export_triangulated_mesh=True)
print('VEST', len(out_me.polygons), 'faces', sum(len(p.vertices) - 2 for p in out_me.polygons), 'triangles')

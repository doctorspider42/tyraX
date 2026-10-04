"""Stage 1 of the Character Generator kit: the body mesh, its PS2 UV atlas and
every texture layer baked into that atlas.

    blender -b --factory-startup --python kit_body.py -- <sys_dir> <mh_data> <stage_dir> [size]

  sys_dir   the extracted MakeHuman CC0 "system assets" pack
            (proxymeshes/, skins/, eyes/, eyebrows/, eyelashes/)
  mh_data   base.obj, targets/, default.mhskel (vendor/mh-assets or a
            makehuman/data checkout)
  stage_dir output: body.npz + layer PNGs, consumed by build_kit.py

Why a re-packed atlas: MakeHuman's UV layout is made for 2048-square photo
skins - the face gets about a fifth of the width, which at the PS2's 256
square is a 50-pixel face. Here the islands keep their shapes (so a skin still
maps onto them) but are re-scaled before packing: the head gains, the hands
and the mouth cavity give way, and the two eyeballs get an island of their own
instead of a separate texture.
"""
import os
import sys

import bpy
import bmesh
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mhkit  # noqa: E402
import bmh  # noqa: E402
from kit_body_masks import MASKS  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:]
SYS, DATA, STAGE = argv[0], argv[1], argv[2]
SIZE = int(argv[3]) if len(argv) > 3 else 1024
# KIT_QUICK=1: pack + one skin only, for iterating on the atlas layout.
QUICK = bool(os.environ.get('KIT_QUICK'))
os.makedirs(STAGE, exist_ok=True)

BODY_PROXY = 'female1605'
# Relative texel density per island class (1 = MakeHuman's own).
ISLAND_SCALE = {'head': 1.55, 'torso': 1.0, 'arm': 1.25, 'leg': 1.0, 'hand': 0.9,
                'foot': 0.8, 'mouth': 0.3, 'eye': 0.22}

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 16

# --- geometry ---------------------------------------------------------------
base_obj = mhkit.load_obj(os.path.join(DATA, 'base.obj'))
base = base_obj.v
w = mhkit.macro_weights()
targets = {s: mhkit.load_target(os.path.join(DATA, 'targets', s + '.target')) for s in w}
avg = mhkit.morph(base, targets, w)   # the average adult: bakes use this pose

pdir = os.path.join(SYS, 'proxymeshes', BODY_PROXY)
body_px = mhkit.load_proxy(os.path.join(pdir, BODY_PROXY + '.proxy'))
body_o = mhkit.load_obj(os.path.join(pdir, BODY_PROXY + '.obj'))
eye_dir = os.path.join(SYS, 'eyes', 'low-poly')
eye_px = mhkit.load_proxy(os.path.join(eye_dir, 'low-poly.mhclo'))
eye_o = mhkit.load_obj(os.path.join(eye_dir, 'low-poly.obj'))

nb = len(body_o.v)
verts_rest = np.concatenate([mhkit.fit_proxy(body_px, base), mhkit.fit_proxy(eye_px, base)])
verts_avg = np.concatenate([mhkit.fit_proxy(body_px, avg), mhkit.fit_proxy(eye_px, avg)])
faces = [list(f) for f in body_o.faces] + [[i + nb for i in f] for f in eye_o.faces]
nvt = len(body_o.vt)
uv_old = np.concatenate([body_o.vt, eye_o.vt])
ftex = [list(t) for t in body_o.ftex] + [[i + nvt for i in t] for t in eye_o.ftex]
part = np.array([0] * len(body_o.faces) + [1] * len(eye_o.faces), dtype=np.int32)

skin_mat = bpy.data.materials.new('kit')
skin_mat.use_nodes = True
body = bmh.mesh_object('body', verts_avg, faces, uv_old, ftex, skin_mat)
me = body.data
me.uv_layers[0].name = 'old'
new_uv = me.uv_layers.new(name='new')
me.uv_layers.active = new_uv

# --- island classification + rescale ------------------------------------------
skel = mhkit.load_skel(os.path.join(DATA, 'default.mhskel'))
neck_y = skel.joint('neck02____head', avg)[1]
wrist_x = abs(skel.joint('wrist.L____head', avg)[0])
ankle_y = skel.joint('foot.L____head', avg)[1]
crotch_y = skel.joint('upperleg02.L____head', avg)[1]
shoulder_x = abs(skel.joint('upperarm01.L____head', avg)[0])

bm = bmesh.new()
bm.from_mesh(me)
uvl = bm.loops.layers.uv['new']
bm.faces.ensure_lookup_table()
seen = set()
islands = []
for f in bm.faces:
    if f.index in seen:
        continue
    stack, isl = [f], []
    seen.add(f.index)
    while stack:
        g = stack.pop()
        isl.append(g)
        for l in g.loops:
            for e in (l.edge,):
                for h in e.link_faces:
                    if h.index in seen:
                        continue
                    # same island when the shared edge's uvs coincide
                    lh = [x for x in h.loops if x.edge == e][0]
                    a0, a1 = l[uvl].uv, l.link_loop_next[uvl].uv
                    b0, b1 = lh[uvl].uv, lh.link_loop_next[uvl].uv
                    if ((a0 - b1).length < 1e-6 and (a1 - b0).length < 1e-6) or \
                       ((a0 - b0).length < 1e-6 and (a1 - b1).length < 1e-6):
                        seen.add(h.index)
                        stack.append(h)
    islands.append(isl)

def classify(isl):
    if part[isl[0].index] == 1:
        return 'eye'
    c = np.mean([np.array(bmh.to_mh(np.array([v.co for v in g.verts])).mean(0)) for g in isl], axis=0)
    if len(isl) < 60 and c[1] > neck_y - 3:
        return 'mouth'
    if c[1] > neck_y:
        return 'head'
    if abs(c[0]) > wrist_x:
        return 'hand'
    if c[1] < ankle_y + 0.6:
        return 'foot'
    return 'body'


def split_body(isl):
    """MakeHuman unwraps torso, arms and legs as ONE island - a starfish no
    packer can fit tightly. Cut it into five pieces by where each face sits."""
    pieces = {}
    for g in isl:
        c = np.array(bmh.to_mh(np.array([v.co for v in g.verts])).mean(0))
        if abs(c[0]) > shoulder_x and c[1] > crotch_y:
            k = 'arm'
        elif c[1] < crotch_y:
            k = 'leg'
        else:
            k = 'torso'
        side = '' if k == 'torso' else ('.L' if c[0] > 0 else '.R')
        pieces.setdefault(k + side, []).append(g)
    return pieces

cls_names = []
split = []
for isl in islands:
    k = classify(isl)
    if k == 'body':
        for name, piece in sorted(split_body(isl).items()):
            split.append((name.split('.')[0], piece))
    else:
        split.append((k, isl))
islands = []
for n, (k, isl) in enumerate(split):
    islands.append(isl)
    cls_names.append(k)
    s = ISLAND_SCALE[k]
    uvs = [l[uvl].uv for g in isl for l in g.loops]
    c = sum((u.copy() for u in uvs), uvs[0].copy() * 0) / len(uvs)
    # Every piece moves by its own offset too, so pieces cut out of one
    # island stop sharing uvs and the packer sees them as separate.
    off = c.copy() * 0
    off.x = 3.0 * n
    for g in isl:
        for l in g.loops:
            l[uvl].uv = c + (l[uvl].uv - c) * s + off
print('ISLANDS', sorted((k, len(i)) for k, i in zip(cls_names, islands)))
islands = [[g.index for g in isl] for isl in islands]
bm.to_mesh(me)
bm.free()

bpy.context.view_layer.objects.active = body
body.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.select_all(action='SELECT')
bpy.ops.uv.pack_islands(rotate=True, rotate_method='ANY', scale=True, margin=0.006,
                        shape_method='CONCAVE')
bpy.ops.object.mode_set(mode='OBJECT')

# --- export the geometry -------------------------------------------------------
loop_new = np.zeros(len(me.loops) * 2, dtype=np.float32)
me.uv_layers['new'].data.foreach_get('uv', loop_new)
loop_new = loop_new.reshape(-1, 2)
face_loops = [list(p.loop_indices) for p in me.polygons]
np.savez_compressed(
    os.path.join(STAGE, 'body.npz'),
    verts=verts_rest, nbody=nb, faces=np.array(faces, dtype=object), part=part,
    uv_new=np.array([[loop_new[li] for li in fl] for fl in face_loops], dtype=object),
    uv_old=np.array([[uv_old[t] for t in tt] for tt in ftex], dtype=object),
    body_ref=body_px.ref, body_w=body_px.w, body_off=body_px.off,
    body_scale=np.array([s for s in body_px.scale]),
    eye_ref=eye_px.ref, eye_w=eye_px.w, eye_off=eye_px.off,
    eye_scale=np.array([s for s in eye_px.scale]),
    island_class=np.array(cls_names),
    allow_pickle=True)

# --- baking ----------------------------------------------------------------------
nt = skin_mat.node_tree
for n in list(nt.nodes):
    nt.nodes.remove(n)
out = nt.nodes.new('ShaderNodeOutputMaterial')
emit = nt.nodes.new('ShaderNodeEmission')
nt.links.new(emit.outputs[0], out.inputs['Surface'])
uv_old_node = nt.nodes.new('ShaderNodeUVMap')
uv_old_node.uv_map = 'old'
src = nt.nodes.new('ShaderNodeTexImage')
nt.links.new(uv_old_node.outputs[0], src.inputs[0])
target_node = nt.nodes.new('ShaderNodeTexImage')
uv_new_node = nt.nodes.new('ShaderNodeUVMap')
uv_new_node.uv_map = 'new'
nt.links.new(uv_new_node.outputs[0], target_node.inputs[0])
nt.nodes.active = target_node
scene.render.bake.margin = 16
scene.render.bake.margin_type = 'EXTEND'


def new_target(name, color=(0, 0, 0, 1)):
    im = bpy.data.images.new(name, SIZE, SIZE, alpha=False, float_buffer=False)
    im.generated_color = color
    im.colorspace_settings.name = 'Non-Color'
    target_node.image = im
    return im


def save(im, name):
    im.filepath_raw = os.path.join(STAGE, name + '.png')
    im.file_format = 'PNG'
    im.save()


def bake_emit_from_old(img_path, name, eye_only=False, use_alpha=False):
    """Re-sample an image painted for MakeHuman's layout into the atlas."""
    im_src = bpy.data.images.load(img_path, check_existing=True)
    im_src.colorspace_settings.name = 'Non-Color'
    src.image = im_src
    if use_alpha:
        nt.links.new(src.outputs['Alpha'], emit.inputs['Color'])
    else:
        nt.links.new(src.outputs['Color'], emit.inputs['Color'])
    im = new_target(name)
    bpy.ops.object.bake(type='EMIT')
    save(im, name)


bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active = body

# Skins: every CC0 diffuse in the pack, re-sampled into the atlas. build_kit.py
# decomposes them into tone and detail.
skins_dir = os.path.join(SYS, 'skins')
for d in sorted(os.listdir(skins_dir)):
    if 'special_suit' in d or d.startswith('toon'):
        continue
    tex = bmh.find_texture(os.path.join(skins_dir, d))
    if tex:
        bake_emit_from_old(tex, 'skin_' + d)
        print('BAKED skin', d)
        if QUICK:
            break
if QUICK:
    sys.exit(0)

# The eyeball: the brown eye, desaturated in build_kit.py into sclera + iris
# detail, plus its own alpha (the island mask).
bake_emit_from_old(os.path.join(SYS, 'eyes', 'materials', 'brown_eye.png'), 'eye_color')

# A mask of which atlas texels belong to which island class: R = head,
# G = eye, B = hand. Painted per face through a color attribute.
col = me.color_attributes.new('cls', 'BYTE_COLOR', 'CORNER')
face_cls = {}
for isl, k in zip(islands, cls_names):
    for gi in isl:
        face_cls[gi] = k
code = {'head': (1, 0, 0, 1), 'eye': (0, 1, 0, 1), 'hand': (0, 0, 1, 1),
        'foot': (1, 1, 0, 1), 'mouth': (1, 0, 1, 1), 'torso': (0, 0, 0, 1),
        'arm': (0, 1, 1, 1), 'leg': (0.5, 0.5, 0.5, 1)}
for p in me.polygons:
    for li in p.loop_indices:
        col.data[li].color = code[face_cls[p.index]]
attr = nt.nodes.new('ShaderNodeVertexColor')
attr.layer_name = 'cls'
nt.links.new(attr.outputs['Color'], emit.inputs['Color'])
scene.render.bake.margin = 2
im = new_target('mask_class')
bpy.ops.object.bake(type='EMIT')
save(im, 'mask_class')
scene.render.bake.margin = 16

# --- selected-to-active: brows, lashes, AO from the high-res body --------------
def bake_cards(asset_dir, name, extrusion=0.012, dist=0.03):
    obj, px, _ = bmh.fitted_asset(asset_dir, avg, name, alpha=True)
    m = obj.data.materials[0]
    mnt = m.node_tree
    tex = [n for n in mnt.nodes if n.type == 'TEX_IMAGE'][0]
    e = mnt.nodes.new('ShaderNodeEmission')
    mnt.links.new(tex.outputs['Alpha'], e.inputs['Color'])
    mnt.links.new(e.outputs[0], mnt.nodes['Material Output'].inputs['Surface'])
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    im = new_target(name)
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=extrusion,
                        max_ray_distance=dist)
    save(im, name)
    bpy.data.objects.remove(obj)


for d in sorted(os.listdir(os.path.join(SYS, 'eyebrows'))):
    p = os.path.join(SYS, 'eyebrows', d)
    if os.path.isdir(p):
        bake_cards(p, 'brow_' + d)
for d in sorted(os.listdir(os.path.join(SYS, 'eyelashes'))):
    p = os.path.join(SYS, 'eyelashes', d)
    if os.path.isdir(p):
        bake_cards(p, 'lash_' + d, extrusion=0.008, dist=0.02)

# Ambient occlusion and the face-paint masks are made on MakeHuman's own
# 13k-quad body (helpers stripped) IN ITS OWN UV LAYOUT - the layout the skins
# are painted in - and then re-sampled into the atlas exactly like a skin.
# Baking straight onto the low-poly body with selected-to-active does not work:
# a cage loose enough to catch every texel starts rays inside the neighbouring
# limb in the armpits and the crotch (black streaks), and one tight enough to
# avoid that misses the fingers and ears (black blocks).
hi_faces, hi_tex = [], []
for f, t, g in zip(base_obj.faces, base_obj.ftex, base_obj.fgroup):
    if g.startswith('body') and t:
        hi_faces.append(f)
        hi_tex.append(t)
hi_mat = bpy.data.materials.new('hi')
hi_mat.use_nodes = True
hi = bmh.mesh_object('hires', avg, hi_faces, base_obj.vt, hi_tex, hi_mat)
# hm08 is not consistently wound; an inward face is fully occluded and bakes
# as a black patch.
bpy.ops.object.select_all(action='DESELECT')
hi.select_set(True)
bpy.context.view_layer.objects.active = hi
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode='OBJECT')
eyes_hi, _, _ = bmh.fitted_asset(os.path.join(SYS, 'eyes', 'high-poly'), avg, 'eyes_hi')
hnt = hi_mat.node_tree
hi_target = hnt.nodes.new('ShaderNodeTexImage')
hnt.nodes.active = hi_target
HI = 2048


def bake_on_hires(name, kind, color=(0, 0, 0, 1)):
    # The low-poly body lies within millimetres of this surface, partly
    # OUTSIDE it - left visible it occludes the reference body in patches.
    body.hide_render = True
    im = bpy.data.images.new(name + '_mh', HI, HI, alpha=False)
    im.generated_color = color
    im.colorspace_settings.name = 'Non-Color'
    hi_target.image = im
    bpy.ops.object.select_all(action='DESELECT')
    hi.select_set(True)
    bpy.context.view_layer.objects.active = hi
    bpy.ops.object.bake(type=kind)
    path = os.path.join(STAGE, name + '_mh.png')
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    return path


scene.world = bpy.data.worlds.new('w')
scene.world.light_settings.distance = 0.1  # contact shading, not a sky dome
scene.cycles.samples = 128
scene.render.bake.margin = 16
ao_mh = bake_on_hires('ao', 'AO', (1, 1, 1, 1))

# Face paint: where makeup and stubble go, found from MakeHuman's own detail
# targets - the "lip volume" target moves exactly the lips, the cheek-volume
# target exactly the cheeks. A target's per-vertex displacement, normalized,
# is a soft region mask on the reference mesh.
TDIR = argv[4] if len(argv) > 4 else None
masks_mh = {}
if TDIR:
    def region(names, gamma=0.6):
        m = np.zeros(len(base))
        for n in names:
            idx, d = mhkit.load_target(os.path.join(TDIR, n + '.target'))
            if len(idx):
                mag = np.linalg.norm(d, axis=1)
                np.maximum.at(m, idx, mag / mag.max())
        return np.clip(m, 0, 1) ** gamma

    lips = region(*MASKS['lips'])
    lids = region(*MASKS['eyeshadow'])
    cheeks = region(*MASKS['cheeks'])
    beard = region(*MASKS['stubble'])
    beard = np.clip(beard - lips * 1.5, 0, 1)
    ca = hi.data.color_attributes.new('paint', 'FLOAT_COLOR', 'POINT')
    ca2 = hi.data.color_attributes.new('paint2', 'FLOAT_COLOR', 'POINT')
    ca.data.foreach_set('color', np.stack([lips, lids, cheeks, np.ones_like(lips)], 1)
                        .astype(np.float32).ravel())
    ca2.data.foreach_set('color', np.stack([beard, beard, beard, np.ones_like(lips)], 1)
                         .astype(np.float32).ravel())
    for n in list(hnt.nodes):
        if n != hi_target:
            hnt.nodes.remove(n)
    hout = hnt.nodes.new('ShaderNodeOutputMaterial')
    hem = hnt.nodes.new('ShaderNodeEmission')
    hnt.links.new(hem.outputs[0], hout.inputs['Surface'])
    hcol = hnt.nodes.new('ShaderNodeVertexColor')
    sep = hnt.nodes.new('ShaderNodeSeparateColor')
    hnt.links.new(hcol.outputs['Color'], sep.inputs[0])
    for name, layer_name, ch in (('mask_lips', 'paint', 0), ('mask_eyeshadow', 'paint', 1),
                                 ('mask_cheeks', 'paint', 2), ('mask_stubble', 'paint2', 0)):
        hcol.layer_name = layer_name
        hnt.links.new(sep.outputs[ch], hem.inputs['Color'])
        masks_mh[name] = bake_on_hires(name, 'EMIT')

# Back to the low-poly body, re-sampling through MakeHuman's layout.
body.hide_render = False
hi.hide_render = True
eyes_hi.hide_render = True
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active = body
scene.cycles.samples = 16
bake_emit_from_old(ao_mh, 'ao')
for name, path in masks_mh.items():
    bake_emit_from_old(path, name)
print('KIT BODY DONE')

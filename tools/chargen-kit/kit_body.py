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
# KIT_ONLY=brows: re-bake only the brow layers into an existing stage (its
# body.npz is checked, not rewritten - the garment layers depend on it).
ONLY = os.environ.get('KIT_ONLY', '')
os.makedirs(STAGE, exist_ok=True)

# Which MakeHuman proxy is the game body: female1605 for women, male1591 for
# men (the female one's breast loops give a man a bust - measured, not
# assumed). Each body gets its own stage directory and atlas.
BODY_PROXY = argv[5] if len(argv) > 5 else 'female1605'
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

# "<proxy>@sub": the HERO body - see the block below.
PROXY_NAME, _, PROXY_MODE = BODY_PROXY.partition('@')
pdir = os.path.join(SYS, 'proxymeshes', PROXY_NAME)
body_px = mhkit.load_proxy(os.path.join(pdir, PROXY_NAME + '.proxy'))
body_o = mhkit.load_obj(os.path.join(pdir, PROXY_NAME + '.obj'))
if PROXY_MODE == 'sub':
    # The HERO body: the standard proxy subdivided once where shape shows -
    # head, torso, arms and legs (hands and feet already carry their fingers
    # and toes) - and every NEW vertex bound to the nearest point of
    # MakeHuman's 13k-quad reference surface, so it gets that surface's real
    # detail under every target, not a smoothed low-poly chord. The UVs are
    # the standard body's, interpolated: same islands, same atlas layout.
    # (MakeHuman's generic 13.8k proxies, un-subdivided, were tried first:
    # their UVs were laid out AFTER subdividing, seams run through the
    # middle of cage faces, and the merged faces stretched across islands.)
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    rest = mhkit.fit_proxy(body_px, base)
    _skel = mhkit.load_skel(os.path.join(DATA, 'default.mhskel'))
    _neck = _skel.joint('neck02____head', base)[1]
    _wrist = abs(_skel.joint('wrist.L____head', base)[0])
    _ankle = _skel.joint('foot.L____head', base)[1]
    ubm = bmesh.new()
    orig = ubm.verts.layers.int.new('orig')  # original index + 1, 0 = new
    bv = []
    for i, co in enumerate(rest):
        v = ubm.verts.new(co)
        v[orig] = i + 1
        bv.append(v)
    ubm.verts.ensure_lookup_table()
    uvl_ = ubm.loops.layers.uv.new('uv')
    for f, t in zip(body_o.faces, body_o.ftex):
        try:
            face = ubm.faces.new([bv[i] for i in f])
        except ValueError:
            continue
        for l, ti in zip(face.loops, t):
            l[uvl_].uv = body_o.vt[ti]
    ubm.faces.ensure_lookup_table()
    edges = set()
    for face in ubm.faces:
        c = np.mean([np.array(v.co) for v in face.verts], axis=0)
        if c[1] <= _neck and (abs(c[0]) > _wrist or c[1] < _ankle + 0.6):
            continue  # a hand or a foot
        for e in face.edges:
            edges.add(e)
    # the original vertices by identity: subdivide_edges COPIES the 'orig'
    # layer into the vertices it makes (every new one looked original)
    first = {v: v[orig] - 1 for v in ubm.verts}
    bmesh.ops.subdivide_edges(ubm, edges=list(edges), cuts=1, use_grid_fill=True)
    ngons = [f for f in ubm.faces if len(f.verts) > 4]
    if ngons:  # where a subdivided face meets a kept one
        bmesh.ops.triangulate(ubm, faces=ngons)
    ubm.verts.ensure_lookup_table()
    ubm.faces.ensure_lookup_table()
    # the reference surface (body groups only), triangulated
    btris = []
    for f, g in zip(base_obj.faces, base_obj.fgroup):
        if g.startswith('body'):
            f = list(f)
            for k in range(1, len(f) - 1):
                btris.append((f[0], f[k], f[k + 1]))
    btris = np.array(btris)
    bvh = BVHTree.FromPolygons([Vector(p) for p in base], btris.tolist())
    # fit_proxy scales offsets by the reference's own x/y/z measurements
    sc_rest = np.ones(3)
    for ax in range(3):
        if body_px.scale[ax]:
            i0, i1, dist = body_px.scale[ax]
            sc_rest[ax] = abs(base[i0, ax] - base[i1, ax]) / dist
    nv, ref, wts, off = [], [], [], []
    index_of = {}
    for k, v in enumerate(ubm.verts):
        index_of[v.index] = k
        if v in first:
            i = first[v]
            nv.append(list(body_o.v[i]))
            ref.append(body_px.ref[i])
            wts.append(body_px.w[i])
            off.append(body_px.off[i])
            continue
        loc, _n, ti, _d = bvh.find_nearest(Vector(v.co))
        a, b, c = (base[j] for j in btris[ti])
        # barycentrics of the nearest point
        v0, v1, v2 = b - a, c - a, np.array(loc) - a
        d00, d01, d11 = v0 @ v0, v0 @ v1, v1 @ v1
        d20, d21 = v2 @ v0, v2 @ v1
        den = d00 * d11 - d01 * d01
        wb = (d11 * d20 - d01 * d21) / den if den else 0.0
        wc = (d00 * d21 - d01 * d20) / den if den else 0.0
        nv.append(list(v.co))
        ref.append(btris[ti])
        wts.append([1.0 - wb - wc, wb, wc])
        # ON the reference surface - unless it is more than 5 mm off it (the
        # mouth bag, the lip line): snapped, those faces collapsed. They keep
        # their place relative to the surface, in the proxy's offset units.
        d = np.array(v.co) - np.array(loc)
        off.append((d / sc_rest).tolist() if np.linalg.norm(d) > 0.05 else [0.0, 0.0, 0.0])
    nfaces, nvt, nftex = [], [], []
    for f in ubm.faces:
        nfaces.append([index_of[v.index] for v in f.verts])
        tt = []
        for l in f.loops:
            tt.append(len(nvt))
            nvt.append([l[uvl_].uv.x, l[uvl_].uv.y])
        nftex.append(tt)
    ubm.free()
    body_o.v = np.array(nv, dtype=np.float64)
    body_o.faces = nfaces
    body_o.vt = np.array(nvt, dtype=np.float64)
    body_o.ftex = nftex
    body_px.ref = np.array(ref, dtype=np.int64)
    body_px.w = np.array(wts, dtype=np.float64)
    body_px.off = np.array(off, dtype=np.float64)
    ntri = sum(len(f) - 2 for f in nfaces)
    print('SUB', PROXY_NAME, len(nv), 'verts', len(nfaces), 'faces', ntri, 'triangles')
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
        # The generic (hero) proxies unwrap the head, hands and feet into the
        # same island too - cut them out, or the face gets no extra texels.
        if c[1] > neck_y:
            pieces.setdefault('head', []).append(g)
            continue
        if abs(c[0]) > wrist_x:
            pieces.setdefault('hand' + ('.L' if c[0] > 0 else '.R'), []).append(g)
            continue
        if c[1] < ankle_y + 0.6:
            pieces.setdefault('foot' + ('.L' if c[0] > 0 else '.R'), []).append(g)
            continue
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
import json  # noqa: E402
loop_new = np.zeros(len(me.loops) * 2, dtype=np.float32)
me.uv_layers['new'].data.foreach_get('uv', loop_new)
loop_new = loop_new.reshape(-1, 2)
face_loops = [list(p.loop_indices) for p in me.polygons]
if ONLY:
    # The layers must land in the atlas the stage already has, and packing is
    # not repeatable - so take the stage's atlas UVs back onto the mesh.
    had = np.load(os.path.join(STAGE, 'body.npz'), allow_pickle=True)['uv_new']
    assert len(had) == len(face_loops), 'a different body - rebuild the stage'
    for fl, uvs in zip(face_loops, had):
        assert len(fl) == len(uvs)
        for li, uv in zip(fl, uvs):
            loop_new[li] = uv
    me.uv_layers['new'].data.foreach_set('uv', loop_new.ravel())
    print('ONLY', ONLY, 'atlas from', STAGE)
else:
    with open(os.path.join(STAGE, 'body.json'), 'w') as fh:
        json.dump({'proxy': BODY_PROXY}, fh)
if not ONLY:
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


# Base-mesh vertex -> its UV in MakeHuman's layout (the skins' layout).
base_vuv = np.full((len(base), 2), np.nan)
for f, t in zip(base_obj.faces, base_obj.ftex):
    if t:
        for vi, ti in zip(f, t):
            if np.isnan(base_vuv[vi, 0]):
                base_vuv[vi] = base_obj.vt[ti]


def bake_brow_flat(asset_dir, name, HI=2048):
    """A brow card laid flat into MakeHuman's layout through its own .mhclo
    binding, then re-sampled into the atlas like a skin. Ray-baking the card
    onto the body (selected-to-active) smeared it: the card floats above the
    low-poly chord, so rays reach it at a grazing angle and stretch its alpha
    into streaks - visible above every brow, worst at 512 on a hero face."""
    pf = [f for f in os.listdir(asset_dir) if f.endswith('.mhclo') or f.endswith('.proxy')][0]
    px = mhkit.load_proxy(os.path.join(asset_dir, pf))
    stem = os.path.basename(asset_dir.rstrip('/\\'))
    co = mhkit.load_obj(os.path.join(asset_dir, px.obj_file or stem + '.obj'))
    wn = px.w / np.maximum(px.w.sum(axis=1, keepdims=True), 1e-9)
    duv = (base_vuv[px.ref] * wn[:, :, None]).sum(axis=1)  # card vertex -> base UV
    tim = bpy.data.images.load(bmh.find_texture(asset_dir), check_existing=True)
    tw, th = tim.size
    alpha = np.array(tim.pixels[:], dtype=np.float32).reshape(th, tw, 4)[:, :, 3]

    def sample(u, v):  # bilinear, rows bottom-up like Blender's pixels
        x = np.clip(u * tw - 0.5, 0, tw - 1.001)
        y = np.clip(v * th - 0.5, 0, th - 1.001)
        x0, y0 = x.astype(int), y.astype(int)
        fx, fy = x - x0, y - y0
        a = alpha[y0, x0] * (1 - fx) + alpha[y0, x0 + 1] * fx
        b = alpha[y0 + 1, x0] * (1 - fx) + alpha[y0 + 1, x0 + 1] * fx
        return a * (1 - fy) + b * fy

    out_a = np.zeros((HI, HI), dtype=np.float32)
    for f, t in zip(co.faces, co.ftex):
        if not t:
            continue
        for k in range(1, len(f) - 1):  # fan
            c = [0, k, k + 1]
            d = np.array([duv[f[i]] for i in c]) * HI - 0.5
            s = np.array([co.vt[t[i]] for i in c])
            if np.isnan(d).any():
                continue
            x0, y0 = np.floor(d.min(axis=0)).astype(int)
            x1, y1 = np.ceil(d.max(axis=0)).astype(int)
            x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, HI - 1), min(y1, HI - 1)
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            m = np.array([[d[1, 0] - d[0, 0], d[2, 0] - d[0, 0]],
                          [d[1, 1] - d[0, 1], d[2, 1] - d[0, 1]]])
            if abs(np.linalg.det(m)) < 1e-9:
                continue
            inv = np.linalg.inv(m)
            px_ = np.stack([gx - d[0, 0], gy - d[0, 1]])
            b1 = inv[0, 0] * px_[0] + inv[0, 1] * px_[1]
            b2 = inv[1, 0] * px_[0] + inv[1, 1] * px_[1]
            inside = (b1 >= -1e-4) & (b2 >= -1e-4) & (b1 + b2 <= 1 + 1e-4)
            if not inside.any():
                continue
            b0 = 1 - b1 - b2
            su = b0 * s[0, 0] + b1 * s[1, 0] + b2 * s[2, 0]
            sv = b0 * s[0, 1] + b1 * s[1, 1] + b2 * s[2, 1]
            val = np.where(inside, sample(su, sv), 0.0)
            reg = out_a[y0:y1 + 1, x0:x1 + 1]
            np.maximum(reg, val, out=reg)
    im = bpy.data.images.new(name + '_mh', HI, HI, alpha=False)
    im.colorspace_settings.name = 'Non-Color'
    px4 = np.ones((HI, HI, 4), dtype=np.float32)
    px4[:, :, 0] = px4[:, :, 1] = px4[:, :, 2] = out_a
    im.pixels.foreach_set(px4.ravel())
    path = os.path.join(STAGE, name + '_mh.png')
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    bake_emit_from_old(path, name)


bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active = body

def bake_all_brows():
    for d in sorted(os.listdir(os.path.join(SYS, 'eyebrows'))):
        p = os.path.join(SYS, 'eyebrows', d)
        if os.path.isdir(p):
            bake_brow_flat(p, 'brow_' + d)
            print('BAKED brow', d)


if ONLY == 'brows':
    bake_all_brows()
    print('DONE brows')
    sys.exit(0)

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
# Which texels belong to ANY island (white), with no margin: build_kit.py
# grows a shell's coverage only into texels outside every island, which closes
# the filtering seam at an island's edge without painting a neighbour.
for p_ in me.polygons:
    for li in p_.loop_indices:
        col.data[li].color = (1, 1, 1, 1)
scene.render.bake.margin = 0
im = new_target('mask_island')
bpy.ops.object.bake(type='EMIT')
save(im, 'mask_island')
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


bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active = body
bake_all_brows()
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
    # The jaw target also moves the neck (and a little of the chest), and a
    # beard down to the collarbones reads as a rash: fade it out across the
    # upper neck joint. (Subtracting the neck targets' own region does not
    # work - they move the lower face too, and the beard vanishes.)
    # Measured against the chin (the jaw bone's tail): a beard goes a little
    # under it and stops; behind the jaw line it is neck.
    chin = skel.joint('jaw____tail', avg)
    fy = np.clip((avg[:, 1] - (chin[1] - 0.2)) / 0.2, 0, 1)
    fz = np.clip((avg[:, 2] - (chin[2] - 0.6)) / 0.25, 0, 1)
    fz = np.where(avg[:, 1] > chin[1] + 0.25, 1.0, fz)
    beard = np.clip(beard * fy * fz, 0, 1)
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

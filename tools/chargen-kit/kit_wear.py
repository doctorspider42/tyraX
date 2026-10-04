"""Stage 1b of the Character Generator kit: the wardrobe and the hair.

    blender -b --factory-startup --python kit_wear.py -- <sys_dir> <packs_dir> <mh_data> <stage_dir> [only_id ...]

Reads the CC0 garments (MakeHuman's system assets and the CC0 asset packs,
each a .mhclo bound to the reference body) and turns each into one of two
PS2-shaped things, written into <stage_dir>/wear/<id>.npz (+ PNGs):

SHELL - for anything that follows the body (shirts, sweaters, trousers,
  suits, gloves). The garment is NOT kept as geometry: the body's own vertices
  under it are pushed out onto its surface, and its texture is baked into the
  body's atlas. A shell costs no triangles at all, cannot let skin poke through
  (there is no skin under it - it IS the skin, moved), fits every morph because
  it is the body, and is skinned identically for the same reason.

MESH - for what leaves the body (skirts, dresses, shoes, hats, glasses, hair).
  The garment is remeshed to a budget, unwrapped, its look baked into its own
  texture, and every vertex is BOUND to the nearest point of the body surface
  (triangle + barycentric + offset along the normal), so it follows morphs
  and skinning without a cloth solver.

Everything is authored against the average adult body; the runtime moves it.
"""
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mhkit  # noqa: E402
import bmh  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:]
REUSE = None
if '--reuse-mesh' in argv:
    # A second body (male1591) binds the SAME low-poly items the first one
    # made: the kit carries each item's mesh and texture once, and one binding
    # per body.
    k = argv.index('--reuse-mesh')
    REUSE = argv[k + 1]
    argv = argv[:k] + argv[k + 2:]
SYS, PACKS, DATA, STAGE = argv[:4]
ONLY = set(argv[4:])
OUT = os.path.join(STAGE, 'wear')
os.makedirs(OUT, exist_ok=True)
ATLAS = 1024

from catalog import CATALOG  # noqa: E402


def src_dir(spec):
    root, rel = spec.split(':', 1)
    return os.path.join(SYS if root == 'sys' else PACKS, *rel.split('/'))


# --- the body, at the average adult, carrying the atlas uvs -------------------
st = np.load(os.path.join(STAGE, 'body.npz'), allow_pickle=True)
base_obj = mhkit.load_obj(os.path.join(DATA, 'base.obj'))
base = base_obj.v
w = mhkit.macro_weights()
targets = {s: mhkit.load_target(os.path.join(DATA, 'targets', s + '.target')) for s in w}
avg = mhkit.morph(base, targets, w)


def proxy_from(prefix):
    p = mhkit.Proxy()
    p.ref, p.w, p.off = st[prefix + '_ref'], st[prefix + '_w'], st[prefix + '_off']
    p.scale = [(int(a), int(b), float(c)) for a, b, c in st[prefix + '_scale']]
    return p


body_px, eye_px = proxy_from('body'), proxy_from('eye')
NB = int(st['nbody'])
verts_avg = np.concatenate([mhkit.fit_proxy(body_px, avg), mhkit.fit_proxy(eye_px, avg)])
faces = [list(f) for f in st['faces']]
part = st['part']
uv_new = st['uv_new']

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 8
scene.render.bake.margin = 16
scene.render.bake.margin_type = 'EXTEND'


def make_body(name, verts):
    me = bpy.data.meshes.new(name)
    me.from_pydata(bmh.to_blender(verts).tolist(), [], faces)
    uvl = me.uv_layers.new(name='new')
    loop_uv = np.concatenate([np.asarray(list(u), dtype=np.float32) for u in uv_new])
    uvl.data.foreach_set('uv', loop_uv.ravel())
    me.update()
    me.shade_smooth()
    o = bpy.data.objects.new(name, me)
    scene.collection.objects.link(o)
    return o


body = make_body('body', verts_avg)
bvn = np.array([v.normal for v in body.data.vertices])  # Blender space normals
bvp = np.array([v.co for v in body.data.vertices])


def emission_material(name, image_path=None, alpha=False, color=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    em = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(em.outputs[0], out.inputs['Surface'])
    if image_path:
        t = nt.nodes.new('ShaderNodeTexImage')
        t.image = bpy.data.images.load(image_path, check_existing=True)
        t.image.colorspace_settings.name = 'Non-Color'
        nt.links.new(t.outputs['Alpha' if alpha else 'Color'], em.inputs['Color'])
    elif color is not None:
        em.inputs['Color'].default_value = color
    return m, nt


def bake_target(obj, name, uv='new', fill=(0, 0, 0, 1), size=ATLAS):
    """Make `obj`'s material bake into a fresh image; returns the image."""
    im = bpy.data.images.new(name, size, size, alpha=False)
    im.generated_color = fill
    im.colorspace_settings.name = 'Non-Color'
    for slot in obj.material_slots:
        nt = slot.material.node_tree
        t = nt.nodes.get('__bake') or nt.nodes.new('ShaderNodeTexImage')
        t.name = '__bake'
        t.image = im
        nt.nodes.active = t
    obj.data.uv_layers.active = obj.data.uv_layers[uv]
    return im


def save(im, path):
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()


def fitted_garment(d):
    pf = [f for f in os.listdir(d) if f.endswith('.mhclo')][0]
    px = mhkit.load_proxy(os.path.join(d, pf))
    o = mhkit.load_obj(os.path.join(d, px.obj_file or pf[:-6] + '.obj'))
    pos = mhkit.fit_proxy(px, avg)
    tex = bmh.find_texture(d)
    return px, o, pos, tex


def texture_stats(path):
    """Mean colour and mean luma over the opaque texels: a dye's reference."""
    im = bpy.data.images.load(path, check_existing=True)
    a = np.array(im.pixels[:], dtype=np.float32).reshape(-1, 4)
    m = a[:, 3] > 0.5
    if m.sum() < 16:
        m[:] = True
    rgb = a[m, :3]
    luma = rgb @ np.array([0.299, 0.587, 0.114])
    return rgb.mean(0), float(luma.mean())


# --- MESH ---------------------------------------------------------------------
# The body's triangles exactly as build_kit.py makes them: items bind to them
# by index.
rest_body = np.concatenate([mhkit.fit_proxy(body_px, base), mhkit.fit_proxy(eye_px, base)])
KTRI, _ = mhkit.triangulate(faces, rest_body)
body_tri_bvh = BVHTree.FromPolygons([Vector(v) for v in bvp], [tuple(int(i) for i in t) for t in KTRI])
# Skirts and dresses bind only to the body ABOVE the crotch. Bound to the
# nearest point, a hem 20 cm off the thigh rides the thigh's rotation like a
# lever and spikes out when the leg swings; bound to the pelvis it moves
# rigidly with the hips, which is what PS2 games did (legs pass through a long
# skirt in a stride, a skirt never tears).
_skel = mhkit.load_skel(os.path.join(DATA, 'default.mhskel'))
CROTCH_Z = bmh.to_blender(_skel.joint('upperleg02.L____head', avg)[None])[0][2]
_upper = [i for i, t in enumerate(KTRI) if bvp[list(t)][:, 2].min() > CROTCH_Z + 0.02]
upper_tri_bvh = BVHTree.FromPolygons([Vector(v) for v in bvp],
                                     [tuple(int(i) for i in KTRI[j]) for j in _upper])
TEX = 256


def apply_modifiers(o):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    for m in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def lowpoly(src, budget, thin, voxel):
    """A clean, closed low-poly stand-in for `src`: solidify (cloth and hair
    cards have no thickness), voxel remesh (one connected surface, no
    internal cards, no holes), then collapse to the triangle budget."""
    lo = src.copy()
    lo.data = src.data.copy()
    lo.data.materials.clear()
    scene.collection.objects.link(lo)
    if thin:
        so = lo.modifiers.new('solid', 'SOLIDIFY')
        so.thickness = thin
        so.offset = 0.0
    if voxel:
        rm = lo.modifiers.new('remesh', 'REMESH')
        rm.mode = 'VOXEL'
        rm.voxel_size = voxel
        rm.use_smooth_shade = True
    apply_modifiers(lo)
    if voxel:
        # Drop crumbs: keep connected pieces holding at least 3% of the mesh.
        bm = bmesh.new()
        bm.from_mesh(lo.data)
        seen, pieces = set(), []
        for v in bm.verts:
            if v.index in seen:
                continue
            stack, comp = [v], []
            seen.add(v.index)
            while stack:
                x = stack.pop()
                comp.append(x)
                for e in x.link_edges:
                    y = e.other_vert(x)
                    if y.index not in seen:
                        seen.add(y.index)
                        stack.append(y)
            pieces.append(comp)
        total = len(bm.verts)
        kill = [v for c in pieces if len(c) < 0.03 * total for v in c]
        bmesh.ops.delete(bm, geom=kill, context='VERTS')
        bm.to_mesh(lo.data)
        bm.free()
    lo.data.calc_loop_triangles()
    n = len(lo.data.loop_triangles)
    if n > budget and voxel:
        # QuadriFlow lays an even quad grid over the (now manifold) surface at
        # the asked density. A quadric collapse straight down to 2% of a voxel
        # mesh does not simplify it - it crumples it into overlapping shards.
        bpy.ops.object.select_all(action='DESELECT')
        lo.select_set(True)
        bpy.context.view_layer.objects.active = lo
        try:
            bpy.ops.object.quadriflow_remesh(target_faces=max(16, budget // 2), seed=1,
                                             use_mesh_symmetry=False)
        except Exception as e:  # non-manifold input: fall back below
            print('quadriflow failed', e)
        lo.data.calc_loop_triangles()
        n = len(lo.data.loop_triangles)
    if n > budget:
        dc = lo.modifiers.new('dec', 'DECIMATE')
        dc.ratio = budget / n
        apply_modifiers(lo)
    tm = lo.modifiers.new('tri', 'TRIANGULATE')
    apply_modifiers(lo)
    lo.data.shade_smooth()
    return lo


def unwrap(o):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    while o.data.uv_layers:
        o.data.uv_layers.remove(o.data.uv_layers[0])
    o.data.uv_layers.new(name='new')
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.03,
                             scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')


def bake_from(src, dst, name, alpha, extrusion, dist, size=TEX):
    """src's diffuse (or its alpha) baked onto dst's 'new' uvs."""
    m = src.data.materials[0]
    nt = m.node_tree
    tex = [n for n in nt.nodes if n.type == 'TEX_IMAGE' and n.name != '__bake'][0]
    em = [n for n in nt.nodes if n.type == 'EMISSION'][0]
    nt.links.new(tex.outputs['Alpha' if alpha else 'Color'], em.inputs['Color'])
    if not dst.data.materials:
        dst.data.materials.append(emission_material('dm_' + name)[0])
    im = bake_target(dst, name, fill=(0, 0, 0, 1), size=size)
    bpy.ops.object.select_all(action='DESELECT')
    src.select_set(True)
    dst.select_set(True)
    bpy.context.view_layer.objects.active = dst
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=extrusion,
                        max_ray_distance=dist)
    return im


def fill_misses(rgb, hit, iters=24):
    """Texels the bake's rays missed are black; a recolour (luminance against
    the average) turns them into black streaks. Grow the hit colours outward
    into them, then give anything still empty the average."""
    rgb = rgb.copy()
    hit = hit.copy()
    for _ in range(iters):
        if hit.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(hit.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            h = np.roll(hit, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * h[:, :, None]
            cnt += h
        grow = (~hit) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        hit = hit | grow
    if (~hit).any():
        rgb[~hit] = rgb[hit].mean(0) if hit.any() else 0.5
    return rgb


# Per slot: triangle budget, solidify thickness, voxel size (metres).
# The solidify thickness must be at least twice the voxel: a sheet thinner
# than a voxel voxelizes into hundreds of disconnected crumbs.
MESH_STYLE = {
    'hair':  (700, 0.016, 0.007),
    'feet':  (320, 0.012, 0.005),
    'head':  (260, 0.012, 0.006),
    'face':  (160, 0.0, 0.0),     # glasses: thin frames die in a voxel remesh
    'skirt': (360, 0.016, 0.007),
    'dress': (560, 0.016, 0.008),
}


def make_mesh(gid, d, slot, extra):
    px, o, pos, tex = fitted_garment(d)
    if not tex:
        print('SKIP (no texture)', gid)
        return None
    cutout = bool(extra.get('cutout'))
    g = bmh.mesh_object('g_' + gid, pos, o.faces, o.vt, o.ftex,
                        emission_material('gm_' + gid, tex)[0])
    style = extra.get('style', slot)
    budget, thin, voxel = MESH_STYLE[style]
    budget = extra.get('budget', budget)
    if REUSE:
        prev = np.load(os.path.join(REUSE, 'wear', gid + '.npz'), allow_pickle=True)
        lo = bmh.mesh_object('lo_' + gid, prev['pos'] * 10.0, prev['tri'].tolist(), None, None, None)
        me0 = lo.data
        uvl0 = me0.uv_layers.new(name='new')
        uvl0.data.foreach_set('uv', prev['uv'].astype(np.float32).ravel())
    else:
        lo = lowpoly(g, budget, thin, voxel)
        unwrap(lo)
    # Push the stand-in a hair outward so the bake's rays start outside the
    # original even where the remesh cut inside it.
    if REUSE:
        col = a = None  # the texture is the first body's
    else:
        col = bake_from(g, lo, gid + '_c', False, 0.02, 0.05)
        a = bake_from(g, lo, gid + '_a', True, 0.02, 0.05)
    if col is not None:
        ca = np.array(col.pixels[:], dtype=np.float32).reshape(TEX, TEX, 4)
        aa = np.array(a.pixels[:], dtype=np.float32).reshape(TEX, TEX, 4)[:, :, 0]
        rgba = ca.copy()
        # The alpha bake doubles as the hit mask: a ray that found the source
        # brought back its alpha, a miss left the image's black.
        rgba[:, :, :3] = fill_misses(ca[:, :, :3], aa > 0.5)
        rgba[:, :, 3] = (aa > 0.5).astype(np.float32) if cutout else 1.0
        out = bpy.data.images.new(gid + '_tex', TEX, TEX, alpha=True)
        out.pixels[:] = rgba.ravel()
        save(out, os.path.join(OUT, gid + '_tex.png'))

    # Bind every vertex to the body surface.
    me = lo.data
    me.calc_loop_triangles()  # stale after the modifiers were applied
    lv = np.array([v.co for v in me.vertices])
    if REUSE:
        # Straight from the first body's file, not from the rebuilt object:
        # Blender's validate() drops degenerate triangles, and a binding that
        # does not line up with the shared mesh is worse than useless.
        lv = bmh.to_blender(prev['pos'] * 10.0)
    bind_tri = np.zeros(len(lv), np.int32)
    bind_bary = np.zeros((len(lv), 3), np.float32)
    bind_off = np.zeros(len(lv), np.float32)
    for i, p in enumerate(lv):
        if style in ('skirt', 'dress'):
            loc, nrm, ti, dist = upper_tri_bvh.find_nearest(Vector(p))
            ti = _upper[ti]
        else:
            loc, nrm, ti, dist = body_tri_bvh.find_nearest(Vector(p))
        t = KTRI[ti]
        A, B, C = (Vector(bvp[k]) for k in t)
        # barycentric of loc in ABC
        v0, v1, v2 = B - A, C - A, loc - A
        d00, d01, d11 = v0.dot(v0), v0.dot(v1), v1.dot(v1)
        d20, d21 = v2.dot(v0), v2.dot(v1)
        den = d00 * d11 - d01 * d01
        if abs(den) < 1e-12:
            bb, cc = 0.0, 0.0
        else:
            bb = (d11 * d20 - d01 * d21) / den
            cc = (d00 * d21 - d01 * d20) / den
        bary = np.array([1 - bb - cc, bb, cc])
        n = sum((Vector(bvn[k]) * float(w_) for k, w_ in zip(t, bary)), Vector())
        n.normalize()
        bind_tri[i] = ti
        bind_bary[i] = bary
        bind_off[i] = (Vector(p) - loc).dot(n)
    ltri = np.array([list(t.vertices) for t in me.loop_triangles], np.int32)
    luv = np.zeros((len(ltri), 3, 2), np.float32)
    uvd = me.uv_layers['new'].data
    for k, t in enumerate(me.loop_triangles):
        for c in range(3):
            luv[k, c] = uvd[t.loops[c]].uv
    if REUSE:
        ltri = prev['tri'].astype(np.int32)
        luv = prev['uv'].astype(np.float32).reshape(-1, 3, 2)
    # What it hides: body triangles whose every corner rides base vertices
    # the garment declares hidden (shoes take the feet, a dress the thighs).
    hide = np.array([all(int(r) in px.delete for r in body_px.ref[i]) for i in range(NB)]
                    + [False] * (len(bvp) - NB))
    # ...and that really lie INSIDE the low-poly stand-in: MakeHuman's list was
    # written for its own garment, and a remeshed dress that sits a little
    # higher left a hole in the thigh where the list still hid the skin.
    lo_bvh = BVHTree.FromPolygons([Vector(v) for v in lv], [tuple(int(i) for i in t) for t in ltri])

    def inside(pt):
        hits, o = 0, Vector(pt)
        d = Vector((0.0123, 0.0071, 1.0)).normalized()  # off-axis: no grazing edges
        for _ in range(64):
            h = lo_bvh.ray_cast(o, d)
            if h[0] is None:
                break
            hits += 1
            o = h[0] + d * 1e-5
        return hits % 2 == 1

    closed = style not in ('face',)
    cover = np.array([ti for ti, t in enumerate(KTRI) if all(hide[k] for k in t)
                      and (not closed or inside(bvp[list(t)].mean(0)))], np.int32)

    # Hair also paints the scalp: through the gaps between strands a bald
    # head would show, so the hair's own colour goes onto the body's atlas
    # where it covers (alpha = coverage), and the runtime lays it under.
    if slot == 'hair':
        scalp = body.copy()
        scalp.data = body.data.copy()
        scene.collection.objects.link(scalp)
        scalp.data.materials.clear()
        scalp.data.materials.append(emission_material('sc_' + gid)[0])
        sc = bake_from(g, scalp, gid + '_sc', False, 0.03, 0.08, ATLAS)
        sa = bake_from(g, scalp, gid + '_sa', True, 0.03, 0.08, ATLAS)
        cc_ = np.array(sc.pixels[:], np.float32).reshape(ATLAS, ATLAS, 4)
        aa_ = np.array(sa.pixels[:], np.float32).reshape(ATLAS, ATLAS, 4)[:, :, 0]
        cc_[:, :, :3] = fill_misses(cc_[:, :, :3], aa_ > 0.3, 8)
        cc_[:, :, 3] = np.clip(aa_ * 1.4, 0, 1)
        so = bpy.data.images.new(gid + '_scalp', ATLAS, ATLAS, alpha=True)
        so.pixels[:] = cc_.ravel()
        save(so, os.path.join(OUT, gid + '_scalp.png'))
        bpy.data.objects.remove(scalp)

    mean_rgb, mean_luma = texture_stats(tex)
    np.savez_compressed(os.path.join(OUT, gid + '.npz'), kind='mesh',
                        pos=bmh.to_mh(lv) * 0.1, tri=ltri, uv=luv.reshape(-1, 6),
                        bindTri=bind_tri, bindBary=bind_bary, bindOff=bind_off,
                        cover=cover, mean_rgb=mean_rgb, mean_luma=mean_luma)
    print('MESH', gid, 'tris', len(ltri), 'cover', len(cover))
    bpy.data.objects.remove(g)
    bpy.data.objects.remove(lo)
    return True


# --- SHELL --------------------------------------------------------------------
# A shell's push-out is MEASURED on two reference bodies, the average woman and
# the average man, and the runtime blends the two by gender. Measured on one
# androgynous body only, a T-shirt bridging under the breasts carried that gap
# over to a flat male chest as two bumps - a man in a T-shirt grew a bust.
def body_frame(macro):
    """Positions and normals (Blender space) of the body under a macro."""
    ww = mhkit.macro_weights(**macro)
    tg = {s: mhkit.load_target(os.path.join(DATA, 'targets', s + '.target')) for s in ww}
    b = mhkit.morph(base, tg, ww)
    v = np.concatenate([mhkit.fit_proxy(body_px, b), mhkit.fit_proxy(eye_px, b)])
    o = make_body('frame', v)
    pos = np.array([x.co for x in o.data.vertices])
    nrm = np.array([x.normal for x in o.data.vertices])
    bpy.data.objects.remove(o)
    return b, pos, nrm


FRAMES = {'f': body_frame({'gender': 0.0}), 'm': body_frame({'gender': 1.0})}

# Each body vertex's mirror twin across x = 0 (nearest vertex to its mirrored
# position on the average body).
_mirrored = bvp[:NB].copy()
_mirrored[:, 0] *= -1
MIRROR = np.array([int(np.argmin(((bvp[:NB] - m) ** 2).sum(1))) for m in _mirrored])

# Edge-adjacent faces of the body, for closing small holes in a coverage.
_edge_faces = {}
for fi, f in enumerate(faces):
    for k in range(len(f)):
        e = tuple(sorted((f[k], f[(k + 1) % len(f)])))
        _edge_faces.setdefault(e, []).append(fi)
FACE_NB = [[] for _ in faces]
for fl in _edge_faces.values():
    for a_ in fl:
        for b_ in fl:
            if a_ != b_:
                FACE_NB[a_].append(b_)


def measure(px, o, frame):
    """Distance from each body vertex, outward along its normal, to the
    garment fitted to the same morph; NaN where it is not reached. Also flags
    the vertices within 4.5 cm of the cloth (see make_shell's face rule)."""
    b, pos, nrm = frame
    gpos = mhkit.fit_proxy(px, b)
    tmp = bmh.mesh_object('measure', gpos, o.faces, None, None, None)
    bvh = BVHTree.FromObject(tmp, bpy.context.evaluated_depsgraph_get())
    bpy.data.objects.remove(tmp)
    dist = np.full(len(pos), np.nan)
    close = np.zeros(len(pos), bool)
    # A ray starts 3 cm INSIDE the body and looks outward for the garment's
    # first sheet. Where that misses, the nearest point decides: on the slope
    # of the upper chest the normal tilts up, and a ray along it leaves through
    # the neck opening - a crew-neck T-shirt came out with a deep square
    # neckline. Anything within 2.5 cm of the skin is the garment over it.
    for i in range(NB):
        n = Vector(nrm[i])
        p0 = Vector(pos[i])
        close[i] = bvh.find_nearest(p0, 0.045)[0] is not None
        hit = bvh.ray_cast(p0 - n * 0.03, n, 0.11)
        if hit[0] is not None and abs(hit[1].dot(n)) > 0.2:
            dist[i] = hit[3] - 0.03
            continue
        near = bvh.find_nearest(p0, 0.025)
        if near[0] is not None:
            dist[i] = max((near[0] - p0).dot(n), 0.0)
    return dist, close


def make_shell(gid, d, extra):
    px, o, pos, tex = fitted_garment(d)
    if not tex:
        print('SKIP (no texture)', gid)
        return None
    g = bmh.mesh_object('g_' + gid, pos, o.faces, o.vt, o.ftex,
                        emission_material('gm_' + gid, tex)[0])
    nv = len(bvp)
    meas = {k: measure(px, o, fr) for k, fr in FRAMES.items()}
    dists = {k: m[0] for k, m in meas.items()}
    # The garment's own declaration of what it hides, plus anything the
    # outward rays reach on either body (a loose trouser leg sits 3-6 cm off
    # the shin).
    hide = np.array([all(int(r) in px.delete for r in body_px.ref[i]) for i in range(NB)]
                    + [False] * (nv - NB))
    covered = hide.copy()
    for dd in dists.values():
        # A tight collar sits a little UNDER the low-poly skin: allow 2 cm.
        covered |= (~np.isnan(dd)) & (dd > -0.02) & (dd < 0.07)
    covered[NB:] = False
    # Rays are not symmetric (a quad's diagonal, a vertex a millimetre off the
    # other side's), and a T-shirt came out with one shoulder bare. Shells are
    # symmetric garments: a vertex is covered if it or its mirror image is.
    covered[:NB] |= covered[MIRROR]
    close = np.zeros(nv, bool)
    for m in meas.values():
        close[:NB] |= m[1][:NB]
    close[:NB] |= close[MIRROR]
    # A quad needs three covered corners, not four: the low-poly rows are
    # 5-6 cm tall, and demanding the whole quad dropped a crew neckline by a
    # full row (the top corners sit on the collarbone, just outside the cloth).
    # And a quad whose cloth edge crosses mid-row (two corners covered, the
    # other two within 4.5 cm of the cloth) is taken in too: the male tee's
    # neckline sits halfway up the sternum row, and dropping that one row cut
    # a square bib of skin out of a crew neck.
    def face_in(f):
        n = sum(covered[v] for v in f)
        return n >= len(f) - (len(f) == 4) or (n >= 2 and all(covered[v] or close[v] for v in f))
    fcov = np.array([part[fi] == 0 and face_in(f) for fi, f in enumerate(faces)])
    # Close pinholes (a crotch, an armpit: normals there point at the other
    # leg or the arm, and the rays miss) and drop lone speckles. A face with
    # ANY covered corner and covered faces on at least half its sides is taken
    # in; the crotch faces of a pair of trousers sit exactly like that.
    some = np.array([part[fi] == 0 and any(covered[v] for v in f) for fi, f in enumerate(faces)])
    for _ in range(3):
        nxt = fcov.copy()
        for fi in range(len(faces)):
            if part[fi] != 0:
                continue
            nb = FACE_NB[fi]
            k = sum(fcov[j] for j in nb)
            if not fcov[fi] and some[fi] and 2 * k >= len(nb):
                nxt[fi] = True
            elif fcov[fi] and k <= 1:
                nxt[fi] = False
        fcov = nxt
    if fcov.sum() < 8:
        print('SKIP (covers nothing)', gid)
        bpy.data.objects.remove(g)
        return None
    cv = np.zeros(nv, bool)
    for fi in np.nonzero(fcov)[0]:
        cv[faces[fi]] = True
    border = np.zeros(nv, bool)
    for fi, f in enumerate(faces):
        if not fcov[fi] and part[fi] == 0:
            for v in f:
                if cv[v]:
                    border[v] = True
    adj = [[] for _ in range(nv)]
    for f in faces:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            adj[a].append(b)
            adj[b].append(a)

    def inflation(dist):
        # Measured where we have it, a small default where only the
        # declaration says covered, zero on the border (the shell meets the
        # skin it does not cover without a crack), smoothed along the surface.
        infl = np.where(np.isnan(dist), 0.004, np.clip(dist, 0.0015, 0.05))
        infl[~cv] = 0
        for _ in range(3):
            nxt = infl.copy()
            for v in np.nonzero(cv & ~border)[0]:
                nb = [infl[u] for u in adj[v] if cv[u]]
                if nb:
                    nxt[v] = 0.5 * infl[v] + 0.5 * float(np.mean(nb))
            infl = nxt
        infl[border] = 0
        return infl

    infl_f = inflation(dists['f'])
    infl_m = inflation(dists['m'])
    infl = 0.5 * (infl_f + infl_m)  # the bake uses the average body

    # The texture: bake the garment onto the pushed-out body.
    shell_pos = bvp + bvn * infl[:, None]
    shell = make_body('shell_' + gid, bmh.to_mh(shell_pos))
    sm, snt = emission_material('sm_' + gid)
    shell.data.materials.append(sm)
    im = bake_target(shell, gid + '_col')
    bpy.ops.object.select_all(action='DESELECT')
    g.select_set(True)
    shell.select_set(True)
    bpy.context.view_layer.objects.active = shell
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=0.015,
                        max_ray_distance=0.04)
    # Where the rays missed the garment (a collar edge, a gap at the cuff) the
    # bake left black, and a black wedge at a T-shirt's neckline is what that
    # looks like. The garment's alpha, baked the same way, is the hit mask;
    # grow the hit colours into the misses.
    gnt = g.data.materials[0].node_tree
    gtex = [n for n in gnt.nodes if n.type == 'TEX_IMAGE' and n.name != '__bake'][0]
    gem = [n for n in gnt.nodes if n.type == 'EMISSION'][0]
    col_px = np.array(im.pixels[:], np.float32).reshape(ATLAS, ATLAS, 4)
    gnt.links.new(gtex.outputs['Alpha'], gem.inputs['Color'])
    im_hit = bake_target(shell, gid + '_hit')
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=0.015,
                        max_ray_distance=0.04)
    hit = np.array(im_hit.pixels[:], np.float32).reshape(ATLAS, ATLAS, 4)[:, :, 0] > 0.5
    col_px[:, :, :3] = fill_misses(col_px[:, :, :3], hit, 12)
    im.pixels[:] = col_px.ravel()
    save(im, os.path.join(OUT, gid + '_col.png'))
    # Coverage: which atlas texels belong to the shell's faces.
    ca = shell.data.color_attributes.new('cov', 'BYTE_COLOR', 'CORNER')
    for p in shell.data.polygons:
        c = (1, 1, 1, 1) if fcov[p.index] else (0, 0, 0, 1)
        for li in p.loop_indices:
            ca.data[li].color = c
    col_node = snt.nodes.new('ShaderNodeVertexColor')
    col_node.layer_name = 'cov'
    snt.links.new(col_node.outputs['Color'], snt.nodes['Emission'].inputs['Color'])
    scene.render.bake.margin = 2
    im2 = bake_target(shell, gid + '_cov')
    bpy.ops.object.select_all(action='DESELECT')
    shell.select_set(True)
    bpy.context.view_layer.objects.active = shell
    bpy.ops.object.bake(type='EMIT')
    save(im2, os.path.join(OUT, gid + '_cov.png'))
    scene.render.bake.margin = 16

    mean_rgb, mean_luma = texture_stats(tex)
    idx = np.nonzero((infl_f > 0) | (infl_m > 0))[0]
    np.savez_compressed(os.path.join(OUT, gid + '.npz'), kind='shell',
                        cover=np.zeros(0, np.int32),
                        infl_idx=idx.astype(np.int32),
                        infl=(infl_f[idx]).astype(np.float32),
                        infl_m=(infl_m[idx]).astype(np.float32),
                        mean_rgb=mean_rgb, mean_luma=mean_luma,
                        shell_faces=np.nonzero(fcov)[0].astype(np.int32))
    bpy.data.objects.remove(g)
    bpy.data.objects.remove(shell)
    print('SHELL', gid, 'faces', int(fcov.sum()), 'max infl %.3f' % infl.max())
    return True


for gid, label, slot, kind, spec, extra in CATALOG:
    if ONLY and gid not in ONLY:
        continue
    d = src_dir(spec)
    if not os.path.isdir(d):
        print('MISSING', gid, d)
        continue
    ok = make_shell(gid, d, extra) if kind == 'shell' else make_mesh(gid, d, slot, extra)
    if ok:
        with open(os.path.join(OUT, gid + '.json'), 'w') as fh:
            json.dump({'id': gid, 'label': label, 'slot': slot, 'kind': kind,
                       'sex': extra.get('sex', ''),
                       'source': spec.split(':', 1)[1], 'cutout': bool(extra.get('cutout')),
                       'layer': {'hands': 1, 'bottom': 2, 'top': 3, 'full': 3, 'feet': 4,
                                 'face': 5, 'head': 6, 'hair': 5}.get(slot, 0)}, fh)
print('WEAR DONE')

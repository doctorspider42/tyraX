"""Stage 2 of the Character Generator kit: everything the editor needs at
runtime, in ONE binary file (resources/chargen-kit.bin, embedded into the
editor at build time - see CMakeLists.txt).

    python build_kit.py <stage_dir>[,<stage_dir_m>] <mh_data> <targets_dir> <out.bin> [anims.json]

  stage_dir    kit_body.py + kit_wear.py output for one body (female1605); a
               second, comma-separated, is the male1591 body
  mh_data      base.obj, default.mhskel, default_weights.mhw (+ targets/)
  targets_dir  a checkout of makehuman/data/targets (macrodetails/, nose/, ...)
  anims.json   anim_retarget.py's output (optional)

Why the runtime never sees MakeHuman's own files: a target is a sparse delta
on the 19158-vertex reference mesh, and the game mesh is a 1700-vertex proxy
that RIDES that mesh. Evaluating the proxy against a morphed reference is
linear in the reference's positions apart from three scale factors, so a
target's effect on the proxy can be computed once, here, as a delta on OUR
vertices - and the editor blends ~200 small arrays instead of carrying 120 MB
of MakeHuman data and the fitting code that reads it.

File format (little-endian), deliberately trivial so C++ reads it in 40 lines:
  'TXCK' u32 version u32 count
  count x { u16 nameLen, name, u8 type, u8 0, u32 n, data (n elements), pad to 4 }
  types: 0 f32, 1 i32, 2 u8, 3 i16, 4 raw bytes (PNG/JPEG), 5 UTF-8 JSON
"""
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mhkit  # noqa: E402
import rig  # noqa: E402
from catalog import CATALOG  # noqa: E402

CATALOG_BY_ID = {c[0]: c for c in CATALOG}

VERSION = 1
DM = 0.1  # MakeHuman decimetres -> metres

# ---------------------------------------------------------------------------
# Sliders. Each is (id, label, group, negative target(s), positive target(s)).
# A target name is a path under targets_dir without '.target'; a leading
# "l|r-" expands to both sides, so one slider moves both ears.
SLIDERS = [
    # Body
    ('belly', 'Belly', 'Body', 'stomach/stomach-pregnant-decr', 'stomach/stomach-pregnant-incr'),
    ('waist', 'Waist', 'Body', 'measure/measure-waist-circ-decr', 'measure/measure-waist-circ-incr'),
    ('hips', 'Hips', 'Body', 'measure/measure-hips-circ-decr', 'measure/measure-hips-circ-incr'),
    ('bust', 'Bust', 'Body', 'measure/measure-bust-circ-decr', 'measure/measure-bust-circ-incr'),
    ('breastLift', 'Breast lift', 'Body', 'breast/breast-volume-vert-down', 'breast/breast-volume-vert-up'),
    ('shoulders', 'Shoulder width', 'Body', 'measure/measure-shoulder-dist-decr', 'measure/measure-shoulder-dist-incr'),
    ('chest', 'Chest (pectorals)', 'Body', 'torso/torso-muscle-pectoral-decr', 'torso/torso-muscle-pectoral-incr'),
    ('vshape', 'V-shape', 'Body', 'torso/torso-vshape-decr', 'torso/torso-vshape-incr'),
    ('buttocks', 'Buttocks', 'Body', 'buttocks/buttocks-volume-decr', 'buttocks/buttocks-volume-incr'),
    ('neckWidth', 'Neck thickness', 'Body', 'measure/measure-neck-circ-decr', 'measure/measure-neck-circ-incr'),
    ('armFat', 'Arm fat', 'Body', ['armslegs/l|r-upperarm-fat-decr', 'armslegs/l|r-lowerarm-fat-decr'],
     ['armslegs/l|r-upperarm-fat-incr', 'armslegs/l|r-lowerarm-fat-incr']),
    ('armMuscle', 'Arm muscle', 'Body', ['armslegs/l|r-upperarm-muscle-decr', 'armslegs/l|r-lowerarm-muscle-decr'],
     ['armslegs/l|r-upperarm-muscle-incr', 'armslegs/l|r-lowerarm-muscle-incr']),
    ('legFat', 'Leg fat', 'Body', ['armslegs/l|r-upperleg-fat-decr', 'armslegs/l|r-lowerleg-fat-decr'],
     ['armslegs/l|r-upperleg-fat-incr', 'armslegs/l|r-lowerleg-fat-incr']),
    ('legMuscle', 'Leg muscle', 'Body', ['armslegs/l|r-upperleg-muscle-decr', 'armslegs/l|r-lowerleg-muscle-decr'],
     ['armslegs/l|r-upperleg-muscle-incr', 'armslegs/l|r-lowerleg-muscle-incr']),
    # Proportions
    ('legLength', 'Leg length', 'Proportions', ['armslegs/upperlegs-height-decr', 'armslegs/lowerlegs-height-decr'],
     ['armslegs/upperlegs-height-incr', 'armslegs/lowerlegs-height-incr']),
    ('torsoLength', 'Torso length', 'Proportions', 'torso/torso-scale-vert-decr', 'torso/torso-scale-vert-incr'),
    ('armLength', 'Arm length', 'Proportions', ['measure/measure-upperarm-length-decr', 'measure/measure-lowerarm-length-decr'],
     ['measure/measure-upperarm-length-incr', 'measure/measure-lowerarm-length-incr']),
    ('neckLength', 'Neck length', 'Proportions', 'neck/neck-scale-vert-decr', 'neck/neck-scale-vert-incr'),
    ('headSize', 'Head size', 'Proportions', ['head/head-scale-horiz-decr', 'head/head-scale-vert-decr', 'head/head-scale-depth-decr'],
     ['head/head-scale-horiz-incr', 'head/head-scale-vert-incr', 'head/head-scale-depth-incr']),
    ('handSize', 'Hand size', 'Proportions', 'armslegs/l|r-hand-scale-decr', 'armslegs/l|r-hand-scale-incr'),
    ('footSize', 'Foot size', 'Proportions', 'armslegs/l|r-foot-scale-decr', 'armslegs/l|r-foot-scale-incr'),
    # Head
    ('headFat', 'Face fullness', 'Head', 'head/head-fat-decr', 'head/head-fat-incr'),
    ('headAge', 'Face age', 'Head', 'head/head-age-decr', 'head/head-age-incr'),
    ('headWidth', 'Head width', 'Head', 'head/head-scale-horiz-decr', 'head/head-scale-horiz-incr'),
    ('headLength', 'Head length', 'Head', 'head/head-scale-vert-decr', 'head/head-scale-vert-incr'),
    ('headOval', 'Oval head', 'Head', None, 'head/head-oval'),
    ('headRound', 'Round head', 'Head', None, 'head/head-round'),
    ('headSquare', 'Square head', 'Head', None, 'head/head-square'),
    ('headTriangle', 'Triangular head', 'Head', None, 'head/head-triangular'),
    ('headDiamond', 'Diamond head', 'Head', None, 'head/head-diamond'),
    ('forehead', 'Forehead height', 'Head', 'forehead/forehead-scale-vert-decr', 'forehead/forehead-scale-vert-incr'),
    ('browRidge', 'Brow ridge', 'Head', 'forehead/forehead-nubian-decr', 'forehead/forehead-nubian-incr'),
    ('browHeight', 'Eyebrow height', 'Head', 'eyebrows/eyebrows-trans-down', 'eyebrows/eyebrows-trans-up'),
    ('browAngle', 'Eyebrow angle', 'Head', 'eyebrows/eyebrows-angle-down', 'eyebrows/eyebrows-angle-up'),
    # Eyes
    ('eyeSize', 'Eye size', 'Eyes', 'eyes/l|r-eye-scale-decr', 'eyes/l|r-eye-scale-incr'),
    ('eyeHeight', 'Eye height', 'Eyes', 'eyes/l|r-eye-trans-down', 'eyes/l|r-eye-trans-up'),
    ('eyeSpacing', 'Eye spacing', 'Eyes', 'eyes/l|r-eye-trans-in', 'eyes/l|r-eye-trans-out'),
    ('eyeTilt', 'Eye tilt', 'Eyes', 'eyes/l|r-eye-eyefold-angle-down', 'eyes/l|r-eye-eyefold-angle-up'),
    ('eyeOpen', 'Eye opening', 'Eyes', 'eyes/l|r-eye-height2-decr', 'eyes/l|r-eye-height2-incr'),
    ('epicanthus', 'Epicanthic fold', 'Eyes', 'eyes/l|r-eye-epicanthus-in', 'eyes/l|r-eye-epicanthus-out'),
    ('eyeBags', 'Eye bags', 'Eyes', 'eyes/l|r-eye-bag-decr', 'eyes/l|r-eye-bag-incr'),
    # Nose
    ('noseSize', 'Nose size', 'Nose', ['nose/nose-scale-horiz-decr', 'nose/nose-scale-vert-decr'],
     ['nose/nose-scale-horiz-incr', 'nose/nose-scale-vert-incr']),
    ('noseWidth', 'Nose width', 'Nose', 'nose/nose-scale-horiz-decr', 'nose/nose-scale-horiz-incr'),
    ('noseLength', 'Nose length', 'Nose', 'nose/nose-scale-vert-decr', 'nose/nose-scale-vert-incr'),
    ('noseDepth', 'Nose projection', 'Nose', 'nose/nose-scale-depth-decr', 'nose/nose-scale-depth-incr'),
    ('noseHeight', 'Nose height', 'Nose', 'nose/nose-trans-down', 'nose/nose-trans-up'),
    ('noseTip', 'Tip up/down', 'Nose', 'nose/nose-point-down', 'nose/nose-point-up'),
    ('noseTipWidth', 'Tip width', 'Nose', 'nose/nose-point-width-decr', 'nose/nose-point-width-incr'),
    ('nostrils', 'Nostril width', 'Nose', 'nose/nose-nostrils-width-decr', 'nose/nose-nostrils-width-incr'),
    ('noseHump', 'Hump', 'Nose', 'nose/nose-hump-decr', 'nose/nose-hump-incr'),
    ('noseCurve', 'Concave/convex', 'Nose', 'nose/nose-curve-concave', 'nose/nose-curve-convex'),
    ('noseGreek', 'Greek nose', 'Nose', 'nose/nose-greek-decr', 'nose/nose-greek-incr'),
    # Mouth
    ('mouthWidth', 'Mouth width', 'Mouth', 'mouth/mouth-scale-horiz-decr', 'mouth/mouth-scale-horiz-incr'),
    ('mouthHeight', 'Mouth height', 'Mouth', 'mouth/mouth-trans-down', 'mouth/mouth-trans-up'),
    ('mouthDepth', 'Mouth forward', 'Mouth', 'mouth/mouth-trans-backward', 'mouth/mouth-trans-forward'),
    ('upperLip', 'Upper lip', 'Mouth', 'mouth/mouth-upperlip-volume-decr', 'mouth/mouth-upperlip-volume-incr'),
    ('lowerLip', 'Lower lip', 'Mouth', 'mouth/mouth-lowerlip-volume-decr', 'mouth/mouth-lowerlip-volume-incr'),
    ('mouthCorners', 'Smile/frown', 'Mouth', 'mouth/mouth-angles-down', 'mouth/mouth-angles-up'),
    ('cupidsBow', "Cupid's bow", 'Mouth', 'mouth/mouth-cupidsbow-decr', 'mouth/mouth-cupidsbow-incr'),
    ('dimples', 'Dimples', 'Mouth', None, 'mouth/mouth-dimples-in'),
    ('laughLines', 'Laugh lines', 'Mouth', None, 'mouth/mouth-laugh-lines-in'),
    # Jaw & cheeks
    ('chinOut', 'Chin forward', 'Jaw', 'chin/chin-prominent-decr', 'chin/chin-prominent-incr'),
    ('chinWidth', 'Chin width', 'Jaw', 'chin/chin-width-decr', 'chin/chin-width-incr'),
    ('chinHeight', 'Chin height', 'Jaw', 'chin/chin-height-decr', 'chin/chin-height-incr'),
    ('jaw', 'Jaw', 'Jaw', 'chin/chin-bones-decr', 'chin/chin-bones-incr'),
    ('underbite', 'Underbite', 'Jaw', 'chin/chin-prognathism-decr', 'chin/chin-prognathism-incr'),
    ('chinCleft', 'Chin cleft', 'Jaw', None, 'chin/chin-cleft-incr'),
    ('cheekBones', 'Cheekbones', 'Jaw', 'cheek/l|r-cheek-bones-decr', 'cheek/l|r-cheek-bones-incr'),
    ('cheekVolume', 'Cheek volume', 'Jaw', 'cheek/l|r-cheek-volume-decr', 'cheek/l|r-cheek-volume-incr'),
    ('doubleChin', 'Double chin', 'Jaw', 'neck/neck-double-decr', 'neck/neck-double-incr'),
    # Ears
    ('earSize', 'Ear size', 'Ears', 'ears/l|r-ear-scale-decr', 'ears/l|r-ear-scale-incr'),
    ('earFlap', 'Ears stick out', 'Ears', 'ears/l|r-ear-flap-decr', 'ears/l|r-ear-flap-incr'),
    ('earPointed', 'Pointed ears', 'Ears', None, 'ears/l|r-ear-shape-pointed'),
    ('earLobe', 'Ear lobes', 'Ears', 'ears/l|r-ear-lobe-decr', 'ears/l|r-ear-lobe-incr'),
]


def expand(spec):
    if spec is None:
        return []
    if isinstance(spec, str):
        spec = [spec]
    out = []
    for s in spec:
        if 'l|r-' in s:
            out += [s.replace('l|r-', 'l-'), s.replace('l|r-', 'r-')]
        else:
            out.append(s)
    return out


class Writer:
    def __init__(self):
        self.chunks = []

    def add(self, name, arr, kind=None):
        if kind == 'bytes':
            self.chunks.append((name, 4, bytes(arr), len(arr)))
            return
        if kind == 'json':
            b = json.dumps(arr, separators=(',', ':')).encode('utf-8')
            self.chunks.append((name, 5, b, len(b)))
            return
        a = np.ascontiguousarray(arr)
        t = {np.dtype('float32'): 0, np.dtype('int32'): 1, np.dtype('uint8'): 2,
             np.dtype('int16'): 3}[a.dtype]
        self.chunks.append((name, t, a.tobytes(), a.size))

    def write(self, path):
        with open(path, 'wb') as f:
            f.write(b'TXCK' + struct.pack('<II', VERSION, len(self.chunks)))
            for name, t, data, n in self.chunks:
                nb = name.encode('utf-8')
                f.write(struct.pack('<H', len(nb)) + nb + struct.pack('<BBI', t, 0, n))
                f.write(data)
                pad = (-len(data)) % 4
                f.write(b'\0' * pad)


def png_bytes(img, **kw):
    b = io.BytesIO()
    img.save(b, format='PNG', optimize=True, **kw)
    return b.getvalue()


def jpg_bytes(img, q=90):
    b = io.BytesIO()
    img.save(b, format='JPEG', quality=q, subsampling=0)
    return b.getvalue()


def emit_body(W, stage, P, data, tdir, shared):
    """One game body under chunk prefix P ('' or 'm/'): its mesh, weights,
    targets, texture layers and its share of the wardrobe (shell layers and
    pushes, mesh-item bindings). `shared` collects what every body has in
    common and is written once by main()."""
    LAYER = 512  # authored at 2x the default output so a 256 bake is a clean downsample
    st = np.load(os.path.join(stage, 'body.npz'), allow_pickle=True)
    base = shared['base']

    def px(prefix):
        p = mhkit.Proxy()
        p.ref, p.w, p.off = st[prefix + '_ref'], st[prefix + '_w'], st[prefix + '_off']
        p.scale = [(int(a), int(b), float(c)) for a, b, c in st[prefix + '_scale']]
        return p

    body_px, eye_px = px('body'), px('eye')

    def fit(b):
        return np.concatenate([mhkit.fit_proxy(body_px, b), mhkit.fit_proxy(eye_px, b)])

    rest = fit(base)
    V = len(rest)
    assert np.allclose(rest, st['verts'], atol=1e-6)

    # --- mesh: triangles with per-corner atlas uvs ------------------------------
    faces, uvn, part = st['faces'], st['uv_new'], st['part']
    tri_i, src = mhkit.triangulate(faces, rest)
    tri, tuv, tpart = [], [], []
    for (a, b, c), (fi, ka, kb, kc) in zip(tri_i, src):
        u = [np.asarray(x) for x in uvn[fi]]
        tri.append([a, b, c])
        tuv.append([u[ka], u[kb], u[kc]])
        tpart.append(part[fi])
    tri = np.array(tri, dtype=np.int32)
    tuv = np.array(tuv, dtype=np.float32)
    tpart = np.array(tpart)
    # Winding: outward normals point away from the body's centre line.
    nrm = np.cross(rest[tri[:, 1]] - rest[tri[:, 0]], rest[tri[:, 2]] - rest[tri[:, 0]])
    axis = rest[tri].mean(1)
    axis[:, 1] = 0  # roughly radial
    flipped = (nrm[tpart == 0] * axis[tpart == 0]).sum() < 0
    if flipped:
        tri = tri[:, [0, 2, 1]]
        tuv = tuv[:, [0, 2, 1]]
    W.add(P + 'mesh/pos', (rest * DM).astype(np.float32))
    W.add(P + 'mesh/tri', tri)
    W.add(P + 'mesh/uv', tuv)  # Blender convention: v up
    W.add(P + 'mesh/part', tpart.astype(np.uint8))
    print(P or '(female)', 'mesh', V, 'verts', len(tri), 'tris')

    # --- skin weights --------------------------------------------------------
    bw = shared['bw']

    def proxy_weights(p):
        ws = (p.w / np.maximum(p.w.sum(1, keepdims=True), 1e-9))
        return (bw[p.ref] * ws[:, :, None]).sum(1)

    vw = np.concatenate([proxy_weights(body_px), proxy_weights(eye_px)])
    joints = np.argsort(-vw, axis=1)[:, :4]
    wts = np.take_along_axis(vw, joints, 1)
    wts[wts < 0.02] = 0
    wts /= np.maximum(wts.sum(1, keepdims=True), 1e-9)
    q = np.round(wts * 255).astype(np.int32)
    q[:, 0] += 255 - q.sum(1)  # bytes that still sum to exactly 255
    W.add(P + 'mesh/joints', joints.astype(np.uint8))
    W.add(P + 'mesh/weights', q.astype(np.uint8))

    # --- targets ----------------------------------------------------------------
    skel, heads = shared['skel'], shared['heads']

    def add_target(key, path):
        tg = mhkit.load_target(path)
        b2 = base.copy()
        b2[tg[0]] += tg[1]
        d = (fit(b2) - rest) * DM
        jd = (rig.heads(skel, b2) - heads) * DM
        idx = np.nonzero(np.abs(d).max(1) > 2e-5)[0].astype(np.int32)
        dd = d[idx]
        scale = max(float(np.abs(dd).max()) if len(dd) else 0.0, 1e-9) / 32767.0
        W.add(P + 't/%s/idx' % key, idx)
        W.add(P + 't/%s/d' % key, np.round(dd / scale).astype(np.int16))
        W.add(P + 't/%s/s' % key, np.array([scale], dtype=np.float32))
        W.add(P + 't/%s/j' % key, jd.astype(np.float32))
        return len(idx)

    total = 0
    for stem in mhkit.macro_stems():
        total += add_target(stem, os.path.join(data, 'targets', stem + '.target'))
    used = set()
    for sid, label, group, neg, pos in SLIDERS:
        for t in expand(neg) + expand(pos):
            if t not in used:
                total += add_target(t, os.path.join(tdir, t + '.target'))
                used.add(t)
    print('  targets', len(used) + 96, 'entries', total)

    # --- texture layers -----------------------------------------------------------
    def load(name, mode='RGB'):
        return Image.open(os.path.join(stage, name + '.png')).convert(mode).resize(
            (LAYER, LAYER), Image.LANCZOS)

    I = 'img/' + P
    for age in ('young', 'middleage', 'old'):
        for eth in ('african', 'asian', 'caucasian'):
            for g in ('female', 'male'):
                W.add(I + 'skin/%s-%s-%s' % (eth, g, age),
                      jpg_bytes(load('skin_%s_%s_%s' % (age, eth, g))), 'bytes')
    cls = np.asarray(load('mask_class'))
    eye_mask = (cls[:, :, 1] > 128) & (cls[:, :, 0] < 128) & (cls[:, :, 2] < 128)
    eye = np.asarray(load('eye_color')).astype(np.float32) / 255
    hsv = np.asarray(Image.fromarray((eye * 255).astype(np.uint8)).convert('HSV')).astype(np.float32) / 255
    luma = eye @ np.array([0.299, 0.587, 0.114])
    iris = ((hsv[:, :, 1] > 0.3) | (luma < 0.25)) & eye_mask
    pupil = (luma < 0.08) & eye_mask
    # The eye layer: RGB = the eyeball with the iris DESATURATED (the runtime
    # tints it), A = 255 on eye texels. A second layer marks the iris.
    eye_rgba = np.zeros((LAYER, LAYER, 4), np.uint8)
    gray = np.repeat(luma[:, :, None], 3, 2)
    rgb = np.where(iris[:, :, None], gray / max(float(luma[iris].mean()), 1e-3) * 0.5, eye)
    eye_rgba[:, :, :3] = np.clip(rgb * 255, 0, 255).astype(np.uint8)
    eye_rgba[:, :, 3] = eye_mask * 255
    W.add(I + 'eye', png_bytes(Image.fromarray(eye_rgba, 'RGBA')), 'bytes')
    W.add(I + 'iris', png_bytes(Image.fromarray(((iris & ~pupil) * 255).astype(np.uint8), 'L')), 'bytes')
    W.add(I + 'class', png_bytes(Image.fromarray(cls.astype(np.uint8), 'RGB')), 'bytes')
    W.add(I + 'ao', png_bytes(load('ao', 'L')), 'bytes')
    # Face paint masks (kit_body.py derives them from MakeHuman's targets).
    for m in ('lips', 'eyeshadow', 'cheeks'):
        if os.path.exists(os.path.join(stage, 'mask_%s.png' % m)):
            W.add(I + 'mask/' + m, png_bytes(load('mask_' + m, 'L')), 'bytes')
    if os.path.exists(os.path.join(stage, 'mask_stubble.png')):
        # Stubble is hairs, not paint: break the region up with fixed noise so
        # it reads as stubble at 512 and as a soft shadow at 128.
        rng = np.random.default_rng(7)
        noise = rng.random((LAYER, LAYER))
        m = np.asarray(load('mask_stubble', 'L')).astype(np.float32) / 255
        stb = np.clip(m * 1.3, 0, 1) * np.clip((noise - 0.25) * 1.8, 0, 1)
        W.add(I + 'mask/stubble', png_bytes(Image.fromarray((stb * 255).astype(np.uint8), 'L')), 'bytes')
    brows = sorted(f[:-4] for f in os.listdir(stage) if f.startswith('brow_'))
    lashes = sorted(f[:-4] for f in os.listdir(stage) if f.startswith('lash_'))
    for b in brows + lashes:
        W.add(I + b.replace('_', '/', 1), png_bytes(load(b, 'L')), 'bytes')
    lists = {'brows': [b[5:] for b in brows], 'lashes': [b[5:] for b in lashes]}
    if 'lists' in shared:
        assert shared['lists'] == lists, 'every body must bake the same brows and lashes'
    shared['lists'] = lists

    # --- wardrobe ------------------------------------------------------------------
    wear_dir = os.path.join(stage, 'wear')
    head = (cls[:, :, 0] > 128) & (cls[:, :, 2] < 128)
    island = (np.asarray(load('mask_island', 'L')) > 250) if os.path.exists(
        os.path.join(stage, 'mask_island.png')) else None
    for fn in sorted(os.listdir(wear_dir)) if os.path.isdir(wear_dir) else []:
        if not fn.endswith('.json'):
            continue
        meta = json.load(open(os.path.join(wear_dir, fn)))
        gid = meta['id']
        cat = CATALOG_BY_ID.get(gid)
        if cat is None:
            continue  # dropped from the catalog since it was built
        z = np.load(os.path.join(wear_dir, gid + '.npz'), allow_pickle=True)
        pre = P + 'g/%s/' % gid
        if meta['kind'] == 'shell':
            W.add(pre + 'inflIdx', z['infl_idx'].astype(np.int32))
            W.add(pre + 'inflDist', (z['infl'] * 1.0).astype(np.float32))
            W.add(pre + 'inflDistM', (z['infl_m'] * 1.0).astype(np.float32))
            col = load(os.path.join('wear', gid + '_col'))
            cov = load(os.path.join('wear', gid + '_cov'), 'L')
            if island is not None:
                # Coverage at an island's edge is fractional after the
                # downsample, and the skin under it shows as a seam (a man's
                # torso is two islands: a line down the middle of his shirt).
                # Grow it - into texels outside every island only.
                grown = np.asarray(cov.filter(ImageFilter.MaxFilter(5)))
                c0 = np.asarray(cov)
                cov = Image.fromarray(np.where(island, c0, grown).astype(np.uint8), 'L')
            W.add(I + 'g/' + gid, jpg_bytes(col, 88), 'bytes')
            W.add(I + 'g/%s/a' % gid, png_bytes(cov), 'bytes')
        else:
            if gid not in shared['mesh']:
                # The item's own mesh and texture are written once, by the
                # first body; later bodies only bind to it.
                shared['mesh'][gid] = (z['tri'].astype(np.int32), z['uv'].astype(np.float32).ravel(),
                                       os.path.join(wear_dir, gid + '_tex.png'))
            else:
                assert len(shared['mesh'][gid][0]) == len(z['tri']), \
                    gid + ': a second body bound a different mesh (kit_wear.py --reuse-mesh)'
            bary = z['bindBary'].astype(np.float64)
            if flipped:  # the body triangles' corners were swapped above
                bary = bary[:, [0, 2, 1]]
            btri = z['bindTri'].astype(np.int64)
            W.add(pre + 'cover', z['cover'].astype(np.int32))
            W.add(pre + 'bindTri', btri.astype(np.int32))
            W.add(pre + 'bindBary', bary.astype(np.float32).ravel())
            off = z['bindOff'].astype(np.float32).reshape(-1, 3)
            if flipped:  # swapping B and C negates kit_wear's tangent_frame w
                off[:, 2] = -off[:, 2]
            W.add(pre + 'bindOff', off.ravel())
            # Skinned like the body point it rides: the corners' weights
            # mixed by the same barycentrics.
            gw = (vw[tri[btri]] * bary[:, :, None]).sum(1)
            gj = np.argsort(-gw, axis=1)[:, :4]
            gwt = np.take_along_axis(gw, gj, 1)
            gwt[gwt < 0.02] = 0
            gwt /= np.maximum(gwt.sum(1, keepdims=True), 1e-9)
            gq = np.round(gwt * 255).astype(np.int32)
            gq[:, 0] += 255 - gq.sum(1)
            W.add(pre + 'joints', gj.astype(np.uint8).ravel())
            W.add(pre + 'weights', gq.astype(np.uint8).ravel())
            scalp = os.path.join(wear_dir, gid + '_scalp.png')
            if os.path.exists(scalp):
                sc = Image.open(scalp).convert('RGBA').resize((LAYER, LAYER), Image.LANCZOS)
                # Only on the head: the bake's margin bleeds the paint into
                # whatever island lies next to the head in the atlas (the
                # shins, as it happens - orange streaks on blondes' legs).
                a_ = np.asarray(sc.getchannel('A')).astype(np.float32) * head
                sc.putalpha(Image.fromarray(a_.astype(np.uint8), 'L'))
                W.add(I + 'g/%s/body' % gid, jpg_bytes(sc.convert('RGB'), 88), 'bytes')
                W.add(I + 'g/%s/body/a' % gid, png_bytes(sc.getchannel('A')), 'bytes')
        entry = {'id': gid, 'label': cat[1], 'slot': cat[2], 'sex': cat[5].get('sex', ''),
                 'kind': meta['kind'], 'cutout': meta.get('cutout', False),
                 'layer': meta.get('layer', 0), 'dyeable': True,
                 'luma': float(z['mean_luma']),
                 'color': [round(float(c), 3) for c in z['mean_rgb']]}
        shared['wardrobe'].setdefault(gid, entry)
        shared['wear_count'][gid] = shared['wear_count'].get(gid, 0) + 1


def main():
    stages = sys.argv[1].split(',')
    data, tdir, out = sys.argv[2:5]
    anims = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] != '-' else None
    W = Writer()

    base = mhkit.load_obj(os.path.join(data, 'base.obj')).v
    skel = mhkit.load_skel(os.path.join(data, 'default.mhskel'))
    cmap = rig.collapse_map(skel.bones)
    mhw = mhkit.load_weights(os.path.join(data, 'default_weights.mhw'), len(base))
    bw = np.zeros((len(base), len(rig.BONES)))
    for bone, (idx, wt) in mhw.items():
        bw[idx, cmap.get(bone, 0)] += wt
    s = bw.sum(1, keepdims=True)
    s[s == 0] = 1
    bw /= s
    heads = rig.heads(skel, base)
    shared = {'base': base, 'skel': skel, 'bw': bw, 'heads': heads, 'mesh': {},
              'wardrobe': {}, 'wear_count': {}}

    # Body 0 (female1605) keeps the unprefixed names; body 1 (male1591) is 'm/'.
    prefixes = ['', 'm/']
    bodies = []
    for k, stage in enumerate(stages):
        info = json.load(open(os.path.join(stage, 'body.json'))) if os.path.exists(
            os.path.join(stage, 'body.json')) else {'proxy': 'female1605'}
        emit_body(W, stage, prefixes[k], data, tdir, shared)
        bodies.append({'prefix': prefixes[k], 'proxy': info.get('proxy', '')})
    W.add('bodies', bodies, 'json')

    # --- shared: rig, sliders, lists, wardrobe, mesh items, clips ----------------
    W.add('rig/names', [rig.PREFIX + n for n in rig.NAMES], 'json')
    W.add('rig/parent', np.array(rig.PARENT, dtype=np.int32))
    W.add('rig/head', (heads * DM).astype(np.float32))
    W.add('sliders', [{'id': sid, 'label': label, 'group': group, 'neg': expand(neg),
                       'pos': expand(pos)} for sid, label, group, neg, pos in SLIDERS], 'json')
    W.add('lists', shared['lists'], 'json')
    for gid, (gtri, guv, tex) in shared['mesh'].items():
        W.add('g/%s/tri' % gid, gtri)
        W.add('g/%s/uv' % gid, guv)
        W.add('img/g/' + gid, png_bytes(Image.open(tex).convert('RGBA')), 'bytes')
    # Only items every body can wear.
    wardrobe = [e for gid, e in sorted(shared['wardrobe'].items())
                if shared['wear_count'][gid] == len(stages)]
    W.add('wardrobe', wardrobe, 'json')
    print('wardrobe', len(wardrobe), 'bodies', len(stages))

    if anims:
        a = json.load(open(anims))
        assert a['bones'] == rig.NAMES, 'anims.json bone order differs from rig.py'
        meta = []
        for c in a['clips']:
            rot = np.array(c['rot'], dtype=np.float32)  # N x B x 4
            hips = np.array(c['hips'], dtype=np.float32)  # N x 3
            key = 'a/' + c['name']
            W.add(key + '/rot', np.round(rot * 32767).astype(np.int16))
            W.add(key + '/hips', hips)
            meta.append({'name': c['name'], 'loop': bool(c['loop']), 'frames': len(rot)})
        W.add('anims', {'fps': a['fps'], 'hipsHeight': a['hipsHeight'], 'clips': meta,
                        'source': a.get('source', '')}, 'json')
        print('anims', len(meta))

    W.write(out)
    print('wrote', out, os.path.getsize(out), 'bytes')


if __name__ == '__main__':
    main()

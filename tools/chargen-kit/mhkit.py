"""Readers for MakeHuman's CC0 data formats (numpy only, no bpy).

Authoring-time only: the Character Generator kit (resources/chargen/) is
built from these files by build_kit.py, and the editor never parses them.
Everything here is in MakeHuman's own space - decimetres, origin at the hips,
+Y up, the character facing +Z.

Formats:
  .obj      the hm08 base mesh and every proxy/garment mesh (quads + tris)
  .target   sparse per-vertex offsets against the base mesh ("idx dx dy dz")
  .proxy / .mhclo   a mesh bound to the base: 3 base verts + barycentric
            weights + an offset in units of three reference distances
  .mhskel   JSON: bones name head/tail JOINTS, a joint is a cube of base verts
  .mhw      JSON: per-bone [vertex, weight] lists on the base mesh
"""
import json
import os
import numpy as np


class Obj:
    def __init__(self):
        self.v = None        # (N,3) float64
        self.vt = None       # (T,2)
        self.faces = []      # list of vertex index lists
        self.ftex = []       # list of uv index lists (or None)
        self.fgroup = []     # group name per face
        self.groups = []

    def tris(self):
        """Fan-triangulated (vertex idx, uv idx) lists."""
        tv, tt = [], []
        for f, t in zip(self.faces, self.ftex):
            for k in range(1, len(f) - 1):
                tv.append((f[0], f[k], f[k + 1]))
                tt.append((t[0], t[k], t[k + 1]) if t else (-1, -1, -1))
        return np.array(tv, dtype=np.int64), np.array(tt, dtype=np.int64)


def load_obj(path):
    o = Obj()
    v, vt = [], []
    group = ''
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            if line.startswith('v '):
                v.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('vt '):
                vt.append([float(x) for x in line.split()[1:3]])
            elif line.startswith('g ') or line.startswith('usemtl '):
                if line.startswith('g '):
                    group = line.split(None, 1)[1].strip() if len(line.split()) > 1 else ''
            elif line.startswith('f '):
                fv, ft = [], []
                for tok in line.split()[1:]:
                    parts = tok.split('/')
                    fv.append(int(parts[0]) - 1)
                    ft.append(int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1)
                o.faces.append(fv)
                o.ftex.append(ft if all(t >= 0 for t in ft) else None)
                o.fgroup.append(group)
    o.v = np.array(v, dtype=np.float64)
    o.vt = np.array(vt, dtype=np.float64) if vt else np.zeros((0, 2))
    o.groups = sorted(set(o.fgroup))
    return o


def load_target(path):
    idx, d = [], []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            if not line.strip() or line[0] == '#':
                continue
            p = line.split()
            idx.append(int(p[0]))
            d.append([float(p[1]), float(p[2]), float(p[3])])
    return np.array(idx, dtype=np.int64), np.array(d, dtype=np.float64).reshape(-1, 3)


def apply_target(base, tgt, w):
    idx, d = tgt
    out = base.copy()
    out[idx] += d * w
    return out


def _ranges(tokens):
    out = []
    i = 0
    while i < len(tokens):
        a = int(tokens[i])
        if i + 2 < len(tokens) and tokens[i + 1] == '-':
            b = int(tokens[i + 2])
            out.extend(range(a, b + 1))
            i += 3
        else:
            out.append(a)
            i += 1
    return out


class Proxy:
    """A .proxy or .mhclo binding."""
    def __init__(self):
        self.ref = None      # (n,3) base vert indices
        self.w = None        # (n,3) barycentric weights
        self.off = None      # (n,3) offsets in reference-distance units
        self.scale = []      # [(i, j, dist)] for x, y, z
        self.delete = set()
        self.obj_file = None
        self.name = ''
        self.material = None
        self.z_depth = 50
        self.tags = []


def load_proxy(path):
    p = Proxy()
    ref, w, off = [], [], []
    scale = {}
    mode = None
    del_tokens = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            s = line.strip()
            if not s or s.startswith('#'):
                continue
            tok = s.split()
            key = tok[0]
            if mode == 'verts' and (key[0].isdigit() or key[0] == '-'):
                if len(tok) >= 9:
                    ref.append([int(tok[0]), int(tok[1]), int(tok[2])])
                    w.append([float(tok[3]), float(tok[4]), float(tok[5])])
                    off.append([float(tok[6]), float(tok[7]), float(tok[8])])
                else:
                    # A bare index: the vertex sits exactly on that base vertex.
                    ref.append([int(tok[0])] * 3)
                    w.append([1.0, 0, 0])
                    off.append([0, 0, 0])
                continue
            if mode == 'delete' and (key[0].isdigit() or key == '-'):
                del_tokens.extend(tok)
                continue
            # A key line does not end the verts block: some upstream files put
            # `material` between `verts 0` and the first vertex.
            if key == 'verts':
                mode = 'verts'
            elif key == 'delete_verts':
                mode = 'delete'
            elif key in ('x_scale', 'y_scale', 'z_scale'):
                scale[key[0]] = (int(tok[1]), int(tok[2]), float(tok[3]))
            elif key == 'obj_file':
                p.obj_file = tok[1]
            elif key == 'name':
                p.name = ' '.join(tok[1:])
            elif key == 'material':
                p.material = tok[1]
            elif key == 'z_depth':
                p.z_depth = int(tok[1])
            elif key == 'tag':
                p.tags.append(' '.join(tok[1:]).lower())
    p.ref = np.array(ref, dtype=np.int64)
    p.w = np.array(w, dtype=np.float64)
    p.off = np.array(off, dtype=np.float64)
    p.scale = [scale.get(a) for a in 'xyz']
    p.delete = set(_ranges(del_tokens))
    return p


def fit_proxy(p, base):
    """Positions of a proxy's vertices on a (possibly morphed) base mesh."""
    sc = np.ones(3)
    for a in range(3):
        s = p.scale[a]
        if s:
            i, j, dist = s
            sc[a] = abs(base[i, a] - base[j, a]) / dist
    wsum = p.w.sum(axis=1, keepdims=True)
    wsum[wsum < 1e-9] = 1
    pos = (base[p.ref] * (p.w / wsum)[:, :, None]).sum(axis=1)
    return pos + p.off * sc[None, :]


class Skeleton:
    def __init__(self, d):
        self.bones = d['bones']
        self.joints = {k: np.array(v, dtype=np.int64) for k, v in d['joints'].items()}

    def joint(self, name, base):
        return base[self.joints[name]].mean(axis=0)

    def head(self, bone, base):
        return self.joint(self.bones[bone]['head'], base)

    def tail(self, bone, base):
        return self.joint(self.bones[bone]['tail'], base)


def load_skel(path):
    with open(path, encoding='utf-8') as fh:
        return Skeleton(json.load(fh))


def load_weights(path, nverts):
    """{bone: (idx array, weight array)}"""
    with open(path, encoding='utf-8') as fh:
        d = json.load(fh)['weights']
    out = {}
    for bone, pairs in d.items():
        a = np.array(pairs, dtype=np.float64).reshape(-1, 2)
        out[bone] = (a[:, 0].astype(np.int64), a[:, 1])
    return out


# ---------------------------------------------------------------------------
# Macro blending: MakeHuman's macro targets are the CORNERS of the slider
# space, and a setting is the product of one factor per axis.

AGE_STOPS = [('baby', 0.0), ('child', 0.1875), ('young', 0.5), ('old', 1.0)]


def _levels(v, stops):
    v = min(max(v, 0.0), 1.0)
    for i in range(len(stops) - 1):
        (n0, s0), (n1, s1) = stops[i], stops[i + 1]
        if v > s1 and i + 2 < len(stops):
            continue
        f = min(max((v - s0) / (s1 - s0), 0.0), 1.0) if s1 > s0 else 0.0
        out = []
        if 1 - f > 0: out.append((n0, 1 - f))
        if f > 0: out.append((n1, f))
        return out
    return [(stops[-1][0], 1.0)]


def macro_weights(gender=0.5, age=0.5, muscle=0.5, weight=0.5,
                  african=1 / 3, asian=1 / 3, caucasian=1 / 3):
    """{target stem: weight} for a macro setting."""
    g = _levels(gender, [('female', 0.0), ('male', 1.0)])
    a = _levels(age, AGE_STOPS)
    m = _levels(muscle, [('minmuscle', 0.0), ('averagemuscle', 0.5), ('maxmuscle', 1.0)])
    w = _levels(weight, [('minweight', 0.0), ('averageweight', 0.5), ('maxweight', 1.0)])
    eth = [max(african, 0), max(asian, 0), max(caucasian, 0)]
    s = sum(eth)
    eth = [e / s for e in eth] if s > 0 else [0, 0, 1]
    out = {}
    for en, ew in zip(('african', 'asian', 'caucasian'), eth):
        if ew <= 0: continue
        for gn, gw in g:
            for an, aw in a:
                if ew * gw * aw > 0:
                    out['%s-%s-%s' % (en, gn, an)] = ew * gw * aw
    for gn, gw in g:
        for an, aw in a:
            for mn, mw in m:
                for wn, ww in w:
                    x = gw * aw * mw * ww
                    if x > 0:
                        out['universal-%s-%s-%s-%s' % (gn, an, mn, wn)] = x
    return out


def macro_stems():
    out = []
    for e in ('african', 'asian', 'caucasian'):
        for g in ('female', 'male'):
            for a, _ in AGE_STOPS:
                out.append('%s-%s-%s' % (e, g, a))
    for g in ('female', 'male'):
        for a, _ in AGE_STOPS:
            for m in ('minmuscle', 'averagemuscle', 'maxmuscle'):
                for w in ('minweight', 'averageweight', 'maxweight'):
                    out.append('universal-%s-%s-%s-%s' % (g, a, m, w))
    return out


def morph(base, targets, weights):
    out = base.copy()
    for stem, w in weights.items():
        idx, d = targets[stem]
        out[idx] += d * w
    return out


def triangulate(faces, rest):
    """Quads -> triangles along the shorter diagonal of the REST mesh: the
    flatter fold. build_kit.py and kit_wear.py must agree on this exactly,
    because garments bind to triangles by index."""
    tri, src = [], []
    for fi, f in enumerate(faces):
        f = list(f)
        if len(f) == 4:
            d02 = np.linalg.norm(rest[f[0]] - rest[f[2]])
            d13 = np.linalg.norm(rest[f[1]] - rest[f[3]])
            order = [(0, 1, 2), (0, 2, 3)] if d02 <= d13 else [(0, 1, 3), (1, 2, 3)]
        else:
            order = [(0, 1, 2)]
        for a, b, c in order:
            tri.append((f[a], f[b], f[c]))
            src.append((fi, a, b, c))
    return np.array(tri, dtype=np.int64), src

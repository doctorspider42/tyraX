"""Shared plumbing for the lofted, Blender-built vehicles of this example.

make-ravager.py is the worked example and stays self-contained. The cars that
came after it (make-pica.py, make-strix.py) keep only their DESIGN in their own
script - the character lines, the stations, the trim, the lamps, the paint -
and take everything that does not change from car to car from here:

* the loft builder (character lines per half section, arch lift, recessed end
  caps, mirrored halves with the SAME UVs),
* the 256x256 world-space atlas (side / top / front / rear tiles, flat and
  ramp cells, a free slot with the far model's wheel face) and its painting
  helpers,
* the symmetric wheel, the Cycles AO bake, the Eevee previews, the glTF export,
* the AUTHORED far model (far_main): the same loft on fewer stations and fewer
  lines, wearing the full model's atlas byte for byte.

A car module passed to these functions is a plain Python module; the
attributes it must define are listed in main() and far_main().

Runs inside Blender only (bpy, bmesh). Units are metres, Blender axes: +X
forward, +Y to the car's left, +Z up.
"""
import importlib.util
import json
import math
import os
import struct
import sys
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))


def load_car(filename):
    """Import a car script (make-x.py) as a module; its main() is guarded."""
    name = os.path.splitext(os.path.basename(filename))[0].replace("-", "_")
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, filename))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod  # the car's section() finds itself through sys.modules
    spec.loader.exec_module(mod)
    return mod


# --- curves ----------------------------------------------------------------------
def pchip(keys):
    """Monotone cubic through (x, v) keys; clamps outside."""
    keys = sorted(keys)
    xs = np.array([k[0] for k in keys], float)
    ys = np.array([k[1] for k in keys], float)
    n = len(xs)
    h = np.diff(xs)
    d = np.diff(ys) / h
    m = np.zeros(n)
    for i in range(1, n - 1):
        if d[i - 1] * d[i] > 0:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    m[0], m[-1] = d[0], d[-1]

    def f(x):
        x = min(max(x, xs[0]), xs[-1])
        i = int(np.clip(np.searchsorted(xs, x) - 1, 0, n - 2))
        t = (x - xs[i]) / h[i]
        return float((2 * t**3 - 3 * t**2 + 1) * ys[i] + (t**3 - 2 * t**2 + t) * h[i] * m[i]
                     + (-2 * t**3 + 3 * t**2) * ys[i + 1] + (t**3 - t**2) * h[i] * m[i + 1])
    return f


def lerp(a, b, t):
    return a + (b - a) * t


def ramp(x, x0, x1):
    return min(1.0, max(0.0, (x - x0) / (x1 - x0)))


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def chrome_ramp(t):
    """Chrome as the era painted it: a sky-lit top, a dark horizon band just
    below the middle, the ground's warm grey at the bottom. t = 0 top .. 1."""
    t = np.clip(t, 0.0, 1.0)
    r = np.interp(t, [0, .38, .5, .62, 1], [238, 214, 70, 120, 176])
    g = np.interp(t, [0, .38, .5, .62, 1], [240, 218, 72, 116, 168])
    b = np.interp(t, [0, .38, .5, .62, 1], [248, 230, 80, 108, 156])
    return np.stack([r, g, b], axis=-1)


def plastic_ramp(t):
    """Black textured plastic (bumpers, cladding): a soft sky sheen on top,
    darkening towards the bottom."""
    t = np.clip(t, 0.0, 1.0)
    v = np.interp(t, [0, .15, .5, 1], [78, 52, 34, 20])
    return np.stack([v, v * 1.02, v * 1.08], axis=-1)


def gunmetal_ramp(t):
    t = np.clip(t, 0.0, 1.0)
    r = np.interp(t, [0, .4, .52, .65, 1], [170, 140, 44, 70, 96])
    return np.stack([r, r * 1.02, r * 1.07], axis=-1)


def vec(fn, arr):
    """fn over an array, evaluated once per distinct value (a tile's world
    x is the same down each column, so this is ~1000 calls, not ~400 000)."""
    u, inv = np.unique(arr, return_inverse=True)
    return np.array([fn(float(v)) for v in u], np.float64)[inv].reshape(arr.shape)


def grain(shape, seed, scale=1.0):
    rng = np.random.RandomState(seed)
    return rng.rand(*shape) * scale


# --- a tiny mesh builder ---------------------------------------------------------
class Geo:
    def __init__(self):
        self.faces = []  # (points, material, uvmode, mirror)

    def face(self, pts, mat="body paint", uv="auto", mirror=True, out=None):
        pts = [Vector(p) for p in pts]
        if out is not None:
            n = Vector((0, 0, 0))
            for i in range(1, len(pts) - 1):
                n += (pts[i] - pts[0]).cross(pts[i + 1] - pts[0])
            if n.dot(Vector(out)) < 0:
                pts.reverse()
        self.faces.append((pts, mat, uv, mirror))

    def box(self, c, h, mat="body paint", uv="auto", mirror=True, skip=()):
        cx, cy, cz = c
        hx, hy, hz = h
        v = lambda sx, sy, sz: (cx + sx * hx, cy + sy * hy, cz + sz * hz)
        quads = {
            "+x": ([v(1, -1, -1), v(1, 1, -1), v(1, 1, 1), v(1, -1, 1)], (1, 0, 0)),
            "-x": ([v(-1, 1, -1), v(-1, -1, -1), v(-1, -1, 1), v(-1, 1, 1)], (-1, 0, 0)),
            "+y": ([v(1, 1, -1), v(-1, 1, -1), v(-1, 1, 1), v(1, 1, 1)], (0, 1, 0)),
            "-y": ([v(-1, -1, -1), v(1, -1, -1), v(1, -1, 1), v(-1, -1, 1)], (0, -1, 0)),
            "+z": ([v(-1, -1, 1), v(1, -1, 1), v(1, 1, 1), v(-1, 1, 1)], (0, 0, 1)),
            "-z": ([v(-1, 1, -1), v(1, 1, -1), v(1, -1, -1), v(-1, -1, -1)], (0, 0, -1)),
        }
        for k, (q, o) in quads.items():
            if k not in skip:
                self.face(q, mat, uv, mirror, out=o)


# --- the atlas --------------------------------------------------------------------
TEX = 256
SS = 4
KEEP_OUT = 0.035   # m: far-only paint stays this far inside the edges the full
#                    model's own faces meet (Closest filtering samples the texel
#                    just past a face edge - make-ravager.py)


class Atlas:
    """The 256x256 layout every car here shares: side (x, z) 256x96, top
    (x, |y|) 192x64, the ENGINE BAY floor (x, |y|) 64x64 beside it, front and
    rear (|y|, z) 96x96, seven 32x24 cells and the far model's wheel face in
    the eighth slot. `cells` names the seven cells; `ramps` maps the ones drawn
    as a vertical ramp to a colour function. `bay` is the car's BAY dict
    (build_engine_bay): the engine tile spans its floor."""

    def __init__(self, xr, xf, side_z, front_z, rear_z, half_w, cells, ramps, colours,
                 bay=None):
        # The top tile gave a quarter of its length resolution to the engine
        # bay: a bonnet can come off in the game (docs/vehicles.md, "Loose
        # panels and glass"), and what is under it is a 64x64 picture, not
        # geometry - no VRAM, no triangles beyond the bay's own few.
        bx = (bay["x0"], bay["x1"]) if bay else (0.0, 1.0)
        bw = bay["w"] if bay else 1.0
        self.tile = {
            "side": ((0, 0, 256, 96), (xr - 0.02, xf + 0.05), side_z),
            "top": ((0, 96, 192, 64), (xr - 0.02, xf + 0.05), (0.0, half_w)),
            "engine": ((192, 96, 64, 64), bx, (0.0, bw)),
            "front": ((0, 160, 96, 96), (0.0, half_w), front_z),
            "rear": ((96, 160, 96, 96), (0.0, half_w), rear_z),
        }
        assert len(cells) <= 7
        self.cells = list(cells)
        self.cell_rect = {n: (192 + (i % 2) * 32, 160 + (i // 2) * 24, 32, 24) for i, n in enumerate(cells)}
        self.ramps = dict(ramps)
        self.colours = dict(colours)
        self.wheel_rect = (224, 232, 32, 24)
        self.wheel_disc = (240.0, 244.0, 11.5)
        self.rgb = None
        self.aomask = None

    # ---- UVs ----
    def tile_uv(self, name, a, b):
        (u0, v0, w, h), (a0, a1), (b0, b1) = self.tile[name]
        u = u0 + (a - a0) / (a1 - a0) * w
        if name in ("top", "engine"):
            v = v0 + (b - b0) / (b1 - b0) * h
        else:
            v = v0 + (1 - (b - b0) / (b1 - b0)) * h
        return (u / TEX, 1.0 - v / TEX)

    def cell_uv(self, name):
        x, y, w, h = self.cell_rect[name]
        return ((x + w * 0.5) / TEX, 1.0 - (y + h * 0.5) / TEX)

    def face_uvs(self, pts, uvmode):
        """UVs per corner and the tile the face landed in (None for a cell)."""
        if uvmode.startswith("cell:") and uvmode[5:] in self.ramps:
            x, y, w, h = self.cell_rect[uvmode[5:]]
            z0, z1 = min(p.z for p in pts), max(p.z for p in pts)
            if z1 - z0 > 0.004:
                return [((x + w * 0.5) / TEX, 1.0 - (y + 1 + (h - 2) * (z1 - p.z) / (z1 - z0)) / TEX)
                        for p in pts], None
        if uvmode.startswith("cell:"):
            return [self.cell_uv(uvmode[5:])] * len(pts), None
        if uvmode.startswith("tile:"):
            t = uvmode[5:]
            if t in ("front", "rear"):
                return [self.tile_uv(t, abs(p.y), p.z) for p in pts], t
            if t in ("top", "engine"):
                return [self.tile_uv(t, p.x, abs(p.y)) for p in pts], t
            return [self.tile_uv(t, p.x, p.z) for p in pts], t
        n = Vector((0, 0, 0))
        for i in range(1, len(pts) - 1):
            n += (pts[i] - pts[0]).cross(pts[i + 1] - pts[0])
        n.normalize()
        ax, ay, az = abs(n.x), abs(n.y), abs(n.z)
        if n.z < -0.6:
            return self.face_uvs(pts, "cell:black")[0], None
        if ax >= ay and ax >= az:
            t = "front" if n.x > 0 else "rear"
            return [self.tile_uv(t, abs(p.y), p.z) for p in pts], t
        if az >= ay:
            return [self.tile_uv("top", p.x, abs(p.y)) for p in pts], "top"
        if n.y < 0:  # an inward-facing wall (a recess, a sail's inside): flat paint
            return self.face_uvs(pts, "cell:paint")[0], None
        return [self.tile_uv("side", p.x, p.z) for p in pts], "side"

    # ---- painting (0..255 floats, supersampled) ----
    def begin(self, base):
        self.rgb = np.zeros((TEX * SS, TEX * SS, 3), np.float32)
        self.rgb[:] = base
        self.aomask = np.ones((TEX * SS, TEX * SS), np.float32)

    def grid(self, name):
        """World coordinates of every supersampled texel of a tile."""
        (u0, v0, w, h), (a0, a1), (b0, b1) = self.tile[name]
        us = (np.arange(w * SS) + 0.5) / SS
        vs = (np.arange(h * SS) + 0.5) / SS
        U, V = np.meshgrid(us, vs)
        A = a0 + U / w * (a1 - a0)
        B = b0 + (V / h if name in ("top", "engine") else (1 - V / h)) * (b1 - b0)
        return A, B

    def view(self, name):
        (u0, v0, w, h) = self.tile[name][0]
        return self.rgb[v0 * SS:(v0 + h) * SS, u0 * SS:(u0 + w) * SS]

    def flat(self, name, mask):
        """Keep the baked occlusion off these texels (far-only paint)."""
        (u0, v0, w, h) = self.tile[name][0]
        t = self.aomask[v0 * SS:(v0 + h) * SS, u0 * SS:(u0 + w) * SS]
        t[mask] = 0.0

    def fill(self, name, mask, col, a=1.0):
        t = self.view(name)
        m = (mask.astype(np.float32) * a)[..., None]
        t[:] = t * (1 - m) + np.asarray(col, np.float32) * m

    def mul(self, name, mask, f):
        t = self.view(name)
        m = mask.astype(np.float32)[..., None]
        f = np.asarray(f, np.float32)
        if f.ndim == 2:
            f = f[..., None]
        t[:] = t * (1 - m) + t * f * m

    def ramp(self, name, mask, t, fn=chrome_ramp):
        c = fn(t)
        tv = self.view(name)
        m = mask.astype(np.float32)[..., None]
        tv[:] = tv * (1 - m) + c * m

    def glass(self, name, mask, sky, streak, tint):
        """The far model's windows, where the full model's glass parts cover
        the tile: a dark tint, the sky's light towards the top, a soft
        diagonal reflection streak."""
        c = np.asarray(tint, np.float32) * (0.85 + 1.25 * sky[..., None])
        c = c + np.asarray((60, 66, 72), np.float32) * streak[..., None]
        tv = self.view(name)
        m = mask.astype(np.float32)[..., None]
        tv[:] = tv * (1 - m) + np.clip(c, 0, 255) * m
        self.flat(name, mask)

    def paint_cells(self):
        for name, (x, y, w_, h) in self.cell_rect.items():
            sl = (slice(y * SS, (y + h) * SS), slice(x * SS, (x + w_) * SS))
            self.aomask[sl] = 0.0
            if name in self.ramps:
                prof = self.ramps[name](np.linspace(0, 1, h * SS))
                self.rgb[sl] = prof[:, None, :]
            else:
                self.rgb[sl] = self.colours[name]

    def paint_wheel_face(self, face_fn):
        """The far model's wheel face in the eighth slot. face_fn(r, ang, up)
        -> RGB for r (0 hub .. 1 tyre edge), angle, up (-1 bottom .. 1 top)."""
        x0, y0, w_, h = self.wheel_rect
        cx, cy, rr = self.wheel_disc
        sl = (slice(y0 * SS, (y0 + h) * SS), slice(x0 * SS, (x0 + w_) * SS))
        py, px = np.mgrid[sl]
        px = (px + 0.5) / SS
        py = (py + 0.5) / SS
        r = np.hypot(px - cx, py - cy) / rr
        ang = np.arctan2(cy - py, px - cx)
        up = (cy - py) / rr
        self.rgb[sl] = face_fn(r, ang, up)
        self.aomask[sl] = 0.0

    def paint_engine(self, bay, accent=(150, 28, 24)):
        """The picture under the bonnet, painted in the bay's own world
        coordinates (x along the car, |y| from the centre line - the floor is
        mirrored, so everything is symmetric): a dark bay, the block with two
        ribbed valve covers in the car's accent colour, a chrome air cleaner,
        hoses, a battery and, for a front engine, the radiator. No baked AO -
        the bonnet above it would bake it black - but its own shading."""
        X, Y = self.grid("engine")
        x0, x1, w = bay["x0"], bay["x1"], bay["w"]
        L = abs(x1 - x0)
        u = (X - x0) / (x1 - x0)             # 0 firewall .. 1 the far end
        base = np.full(X.shape + (3,), 26.0, np.float32)
        base += grain(X.shape, 11, 10.0)[..., None]
        edge = np.minimum(np.minimum(u, 1.0 - u) * L, w - Y) / 0.12
        base *= (0.55 + 0.45 * np.clip(edge, 0, 1))[..., None]
        t = self.view("engine")
        t[:] = base
        m = lambda mask: mask.astype(np.float32)[..., None]
        front = bay.get("radiator", True)
        # the block, in u (along the bay) and Y (across it)
        u0b, u1b = 0.12, 1.0 - (0.30 if front else 0.12)
        block = (u > u0b) & (u < u1b) & (Y < 0.62 * w)
        shade = 96 + 34 * np.clip(1.0 - Y / (0.62 * w), 0, 1)
        t[:] = t * (1 - m(block)) + np.stack([shade, shade * 1.02, shade * 1.06], -1) * m(block)
        # valve covers, ribbed
        du = 0.04 / L
        vc = (u > u0b + du) & (u < u1b - du) & (Y > 0.18 * w) & (Y < 0.50 * w)
        rib = 0.78 + 0.22 * (np.sin(X * 90.0) > 0)
        col = np.asarray(accent, np.float32)[None, None, :] * rib[..., None]
        t[:] = t * (1 - m(vc)) + col * m(vc)
        # the air cleaner: a chrome disc on the centre line
        cxw = x0 + 0.5 * (u0b + u1b) * (x1 - x0)
        r = np.hypot(X - cxw, Y) / (0.30 * w)
        disc = r < 1.0
        ring = np.clip(1.0 - np.abs(r - 0.85) / 0.15, 0, 1)
        chrome = 120 + 110 * (1.0 - r) + 40 * ring
        t[:] = t * (1 - m(disc)) + np.stack([chrome] * 3, -1) * m(disc)
        t[:] *= (1.0 - 0.6 * m(disc & (r < 0.18)))
        # hoses: two dark runs from the block to the far end
        for yy in (0.25 * w, 0.40 * w):
            hose = (np.abs(Y - yy) < 0.018) & (u > u1b) & (u < 1.0 - 0.04 / L)
            t[:] = t * (1 - m(hose)) + np.asarray((16, 16, 17), np.float32) * m(hose)
        # battery: a black box, red terminal, by the firewall
        bat = (u > 0.05 / L) & (u < 0.05 / L + 0.22) & (Y > 0.70 * w) & (Y < 0.92 * w)
        t[:] = t * (1 - m(bat)) + np.asarray((18, 18, 19), np.float32) * m(bat)
        term = bat & (np.hypot((u - 0.10 / L) * L, Y - 0.76 * w) < 0.02)
        t[:] = t * (1 - m(term)) + np.asarray((170, 30, 24), np.float32) * m(term)
        if front:
            # the radiator across the front: dark fins
            rad = u > 1.0 - 0.09 / L
            fins = 22 + 22 * (np.sin(Y * 160.0) > 0.3)
            t[:] = t * (1 - m(rad)) + np.stack([fins] * 3, -1) * m(rad)
        self.flat("engine", np.ones(X.shape, bool))

    def finish(self, ao):
        img = self.rgb.reshape(TEX, SS, TEX, SS, 3).mean(axis=(1, 3))
        m = self.aomask.reshape(TEX, SS, TEX, SS).mean(axis=(1, 3))
        occ = 1.0 - m * (1.0 - ao)
        img = img * occ[..., None]
        return np.clip(img + 0.5, 0, 255).astype(np.uint8)


# --- the loft ---------------------------------------------------------------------
def arch_z(C, x):
    for ax in (C.AXF, C.AXR):
        dx = x - ax
        if abs(dx) < C.ARCH_R:
            return C.HUB_Z + math.sqrt(C.ARCH_R * C.ARCH_R - dx * dx)
    return None


def on_arch(C, pts):
    """Every corner lifted onto the arch: the face is wheel-well roof."""
    for p in pts:
        za = arch_z(C, p[0])
        if za is None or abs(p[2] - za) > 1e-4:
            return False
    return True


def lift_arches(C, p, x):
    """Lift the lower lines (floor edge .. flank) onto each arch circle."""
    for ax in (C.AXF, C.AXR):
        dx = x - ax
        if abs(dx) < C.ARCH_R:
            za = C.HUB_Z + math.sqrt(C.ARCH_R * C.ARCH_R - dx * dx)
            if za > p[1][1]:
                p[1] = (min(p[1][0], C.ARCH_INNER_Y), max(p[1][1], za))
            for k in (2, 3, 4):
                p[k] = (p[k][0], max(p[k][1], za))
    return p


def merge_stations(xs):
    out = []
    for x in sorted(xs, reverse=True):
        if not out or out[-1] - x > 0.02:
            out.append(x)
    return out


def build_loft(g, C, st, section, lines=None, uv_hook=None, glass=True):
    """Quads between consecutive stations and consecutive character lines.
    lines: which section() indices to use (all of them when None)."""
    secs = [section(x) for x in st]
    if lines is not None:
        secs = [[s[k] for k in lines] for s in secs]
    idx = lines if lines is not None else list(range(len(secs[0])))
    for i in range(len(st) - 1):
        xa, xb = st[i], st[i + 1]
        xm = 0.5 * (xa + xb)
        for j in range(len(idx) - 1):
            k = idx[j]
            a0, a1 = secs[i][j], secs[i][j + 1]
            b0, b1 = secs[i + 1][j], secs[i + 1][j + 1]
            pts = [(xa, a0[0], a0[1]), (xb, b0[0], b0[1]), (xb, b1[0], b1[1]), (xa, a1[0], a1[1])]
            cy = 0.25 * (a0[0] + a1[0] + b0[0] + b1[0])
            cz = 0.25 * (a0[1] + a1[1] + b0[1] + b1[1])
            # outward: the half section runs keel -> crown counter-clockwise in
            # (y, z), so its outward side is (dz, -dy) of the segment - which
            # also orients a wall that faces inward and up (a hood sitting in a
            # valley between fender humps); a centroid test flips those
            dy = (a1[0] - a0[0]) + (b1[0] - b0[0])
            dz = (a1[1] - a0[1]) + (b1[1] - b0[1])
            ref = (0.0, dz, -dy) if abs(dy) + abs(dz) > 1e-4 else (0.0, cy, cz - C.ZMID)
            if j == 0:
                ref = (0.0, 0.2 if lines is not None else 0.0, -1.0)
            elif k <= 1:
                ref = (0.0, 0.3, -1.0)
            mat, uv = "body paint", "auto"
            if glass and C.glass_strip(k, xm):
                mat = "glass window"
            elif j == 0:
                uv = "cell:black"
            elif 1 <= k <= 3 and on_arch(C, pts):
                uv = "cell:black"
            elif uv_hook is not None:
                uv = uv_hook(k, xm, pts) or uv
            g.face(pts, mat, uv, out=ref)
    return secs


def build_caps(g, C, section, lines=None, fan=True):
    """Recessed end caps (grille, tail panel): an inset ring whose walls are
    black, and the face itself. The full model fans each half to the centre
    line; the far model closes each end with one mirrored polygon."""
    for (x0, sign, depth, zc, sy, sz) in C.CAPS:
        ring = section(x0)
        if lines is not None:
            ring = [ring[k] for k in lines]
        inner = [(y * sy, zc + (z - zc) * sz) for (y, z) in ring]
        xi = x0 - sign * depth
        tile = "front" if sign > 0 else "rear"
        for k in range(len(ring) - 1):
            (y0, z0), (y1, z1) = ring[k], ring[k + 1]
            (u0, w0), (u1, w1) = inner[k], inner[k + 1]
            g.face([(x0, y0, z0), (x0, y1, z1), (xi, u1, w1), (xi, u0, w0)],
                   "body paint", "cell:black", out=(sign * 0.2, -(y0 + y1), -(z0 + z1 - 2 * zc)))
        if fan:
            for k in range(len(inner) - 1):
                (u0, w0), (u1, w1) = inner[k], inner[k + 1]
                g.face([(xi, 0.0, zc), (xi, u0, w0), (xi, u1, w1)], "body paint", "tile:" + tile,
                       out=(sign, 0, 0))
        else:
            left = [(xi, y, z) for (y, z) in inner]
            right = [(xi, -y, z) for (y, z) in reversed(inner) if y > 1e-4]
            g.face(left + right, "body paint", "tile:" + tile, mirror=False, out=(sign, 0, 0))


def bar(g, path, z0, z1, depth, cell="chrome", under="dark", top=True):
    """A swept bumper bar along an (x, y, outward normal) path (half, mirrored)."""
    for (xa, ya, na), (xb, yb, nb) in zip(path, path[1:]):
        oa, ob = Vector((xa, ya)), Vector((xb, yb))
        ia, ib = oa - Vector(na) * depth, ob - Vector(nb) * depth
        P = lambda v, z: (v.x, v.y, z)
        nout = Vector(((na[0] + nb[0]) * 0.5, (na[1] + nb[1]) * 0.5, 0))
        g.face([P(oa, z0), P(ob, z0), P(ob, z1), P(oa, z1)], "body paint", "cell:" + cell, out=nout)
        if top:
            g.face([P(oa, z1), P(ob, z1), P(ib, z1), P(ia, z1)], "body paint", "cell:" + cell, out=(0, 0, 1))
        if under:
            g.face([P(oa, z0), P(ob, z0), P(ib, z0), P(ia, z0)], "body paint", "cell:" + under, out=(0, 0, -1))
    (xe, ye, ne) = path[-1]
    oe = Vector((xe, ye))
    ie = oe - Vector(ne) * depth
    tang = Vector((xe - path[-2][0], ye - path[-2][1], 0))
    g.face([(oe.x, oe.y, z0), (ie.x, ie.y, z0), (ie.x, ie.y, z1), (oe.x, oe.y, z1)],
           "body paint", "cell:" + cell, out=tang)


def disc_lamp(g, mat, x, yc, zc, r, n, sign):
    ring = [(x, yc + r * math.cos(2 * math.pi * s / n), zc + r * math.sin(2 * math.pi * s / n))
            for s in range(n)]
    for s in range(n):
        g.face([(x + sign * 0.01, yc, zc), ring[s], ring[(s + 1) % n]], mat, "auto", out=(sign, 0, 0))


def rect_lamp(g, mat, x, y0, y1, z0, z1, sign):
    g.face([(x, y0, z0), (x, y1, z0), (x, y1, z1), (x, y0, z1)], mat, "auto", out=(sign, 0, 0))


def build_interior(g, C, I):
    """A minimal cabin (tub, dash, seats, wheel) UV'd into the dark cells: it
    only shows through translucent glass."""
    wi = lambda x: C.half_w(x) - I["inset"]
    zf, xa, xb = I["floor"], I["rear"], I["dash"]
    g.face([(xa, 0, zf), (xb, 0, zf), (xb, wi(xb), zf), (xa, wi(xa), zf)], uv="cell:int", out=(0, 0, 1))
    xm = 0.5 * (xa + xb)
    for (x0, x1) in ((xa, xm), (xm, xb)):
        g.face([(x0, wi(x0), zf), (x1, wi(x1), zf), (x1, wi(x1), C.belt(x1) + 0.035),
                (x0, wi(x0), C.belt(x0) + 0.035)], uv="cell:int", out=(0, -1, 0))
        g.face([(x0, wi(x0), C.belt(x0) + 0.035), (x1, wi(x1), C.belt(x1) + 0.035),
                (x1, C.half_w(x1) - I["sill"], C.belt(x1) + 0.04),
                (x0, C.half_w(x0) - I["sill"], C.belt(x0) + 0.04)], uv="cell:intdark", out=(0, 0, 1))
    zd, xd = I["dash_z"], I["dash_back"]
    g.face([(xd, 0, zd), (xb + 0.08, 0, zd), (xb + 0.08, wi(xb), zd), (xd, wi(xd), zd)],
           uv="cell:intdark", out=(0, 0, 1))
    g.face([(xd, 0, zf + 0.22), (xd, wi(xd), zf + 0.22), (xd, wi(xd), zd), (xd, 0, zd)],
           uv="cell:int", out=(-1, 0, 0))
    if "shelf" in I:
        xs, zs = I["shelf"]
        g.face([(xs - 0.12, 0, zs), (xs + 0.22, 0, zs), (xs + 0.22, wi(xs), zs), (xs - 0.12, wi(xs), zs)],
               uv="cell:intdark", out=(0, 0, 1))
    if "bench" in I:
        bx, bz = I["bench"]
        g.box((bx, 0.34, bz), (0.22, 0.34, 0.06), uv="cell:seat", skip=("-z", "-y"))
        g.face([(bx - 0.25, 0, bz + 0.06), (bx - 0.25, 0.68, bz + 0.06), (bx - 0.34, 0.68, bz + 0.45),
                (bx - 0.34, 0, bz + 0.45)], uv="cell:seat", out=(1, 0, 0.3))
    sx, sy, sz, sh = I["seat"]
    g.box((sx, sy, sz), (0.23, 0.22, 0.07), uv="cell:seat", skip=("-z",))
    back = [(sx - 0.23, sy - 0.22, sz + 0.07), (sx - 0.23, sy + 0.22, sz + 0.07),
            (sx - 0.23 - sh * 0.2, sy + 0.19, sz + 0.07 + sh), (sx - 0.23 - sh * 0.2, sy - 0.19, sz + 0.07 + sh)]
    g.face(back, uv="cell:seat", out=(1, 0, 0.2))
    g.face([(p[0] - 0.09, p[1], p[2]) for p in back], uv="cell:intdark", out=(-1, 0, -0.2))
    g.face([back[1], back[2], (back[2][0] - 0.09, back[2][1], back[2][2]),
            (back[1][0] - 0.09, back[1][1], back[1][2])], uv="cell:intdark", out=(0, 1, 0))
    g.face([back[2], back[3], (back[3][0] - 0.09, back[3][1], back[3][2]),
            (back[2][0] - 0.09, back[2][1], back[2][2])], uv="cell:intdark", out=(0, 0, 1))
    # steering wheel on the driver's (left) side: a flat octagonal ring
    c = Vector(I["wheel"])
    tilt = Matrix.Rotation(math.radians(-I.get("wheel_tilt", 62)), 3, "Y")
    n, ro, ri = 8, I.get("wheel_r", 0.18), I.get("wheel_r", 0.18) - 0.035
    pt = lambda r, s: c + tilt @ Vector((0, r * math.cos(2 * math.pi * s / n + 0.39),
                                         r * math.sin(2 * math.pi * s / n + 0.39)))
    for s in range(n):
        q = [pt(ro, s), pt(ro, s + 1), pt(ri, s + 1), pt(ri, s)]
        g.face(q, uv="cell:intdark", mirror=False, out=tilt @ Vector((1, 0, 0)))
        g.face(q, uv="cell:intdark", mirror=False, out=tilt @ Vector((-1, 0, 0)))
    # A little more cabin for what a lost door or window now shows: a centre
    # console with a gear lever, an instrument binnacle over the wheel, door
    # cards with an armrest and a pull. All in the dark cells - a darker
    # cabin hides how little geometry it is.
    cx0, cx1 = sx - 0.30, xd - 0.02
    g.box((0.5 * (cx0 + cx1), 0.0, zf + 0.11), (0.5 * (cx1 - cx0), 0.085, 0.11),
          uv="cell:intdark", mirror=False, skip=("-z",))
    g.box((sx + 0.22, 0.0, zf + 0.27), (0.02, 0.015, 0.06), uv="cell:dark", mirror=False, skip=("-z",))
    g.box((sx + 0.22, 0.0, zf + 0.34), (0.03, 0.03, 0.02), uv="cell:dark", mirror=False)
    wy = I["wheel"][1]
    g.box((xd + 0.03, wy, zd + 0.035), (0.07, 0.15, 0.035), uv="cell:dark", mirror=False, skip=("-z",))
    for (x0, x1) in ((xa, xm), (xm, xb)):
        xc = 0.5 * (x0 + x1)
        zc = 0.5 * (zf + C.belt(xc)) + 0.06
        g.box((xc, wi(xc) - 0.03, zc), (0.5 * (x1 - x0) - 0.06, 0.03, 0.025), uv="cell:intdark",
              skip=("+y",))
        g.box((xc, wi(xc) - 0.012, zc + 0.12), (0.10, 0.012, 0.012), uv="cell:dark", skip=("+y",))


def surface_z(C, x, y):
    """Height of the car's outer skin at (x, |y|): the highest crossing of
    the half section at station x - what an inner wall has to stay under."""
    sec = C.section(x)
    best = None
    for (y0, z0), (y1, z1) in zip(sec, sec[1:]):
        if (y0 - y) * (y1 - y) <= 0 and abs(y1 - y0) > 1e-6:
            z = z0 + (z1 - z0) * (y - y0) / (y1 - y0)
            best = z if best is None else max(best, z)
    return best if best is not None else max(z for _, z in sec)


def build_engine_bay(g, C, B):
    """What a lost bonnet (or boot lid) leaves: a floor carrying the engine
    tile (or a dark one), and dark inner walls up to just under the skin, so
    the hole shows an engine bay instead of the inside of the body shell.
    B: x0 / x1 (the ends, firewall first), w (half width), z (floor height),
    engine (paint the engine tile on the floor), radiator (a front engine)."""
    x0, x1, w, zf = B["x0"], B["x1"], B["w"], B["z"]
    lo, hi = min(x0, x1), max(x0, x1)
    uv = "tile:engine" if B.get("engine", True) else "cell:dark"
    g.face([(lo, 0, zf), (hi, 0, zf), (hi, w, zf), (lo, w, zf)], uv=uv, out=(0, 0, 1))
    n = 6
    xs = [lo + (hi - lo) * i / n for i in range(n + 1)]
    for xa, xb in zip(xs, xs[1:]):  # the inner fender, following the skin
        za, zb = surface_z(C, xa, w) - 0.03, surface_z(C, xb, w) - 0.03
        g.face([(xa, w, zf), (xb, w, zf), (xb, w, zb), (xa, w, za)], uv="cell:dark", out=(0, -1, 0))
    ys = [w * i / 3 for i in range(4)]
    for xe, sgn in ((lo, 1), (hi, -1)):  # the two end walls
        for ya, yb in zip(ys, ys[1:]):
            za, zb = surface_z(C, xe, ya) - 0.03, surface_z(C, xe, yb) - 0.03
            g.face([(xe, ya, zf), (xe, yb, zf), (xe, yb, zb), (xe, ya, za)], uv="cell:dark",
                   out=(sgn, 0, 0))


# --- wheel ------------------------------------------------------------------------
def build_wheel(g, R, W, seg, slot, rs=0.62, hub=0.19, cap_n=5, phase=0.21):
    """Centred on the hub, axle along Y. SYMMETRIC (the runtime draws all four
    wheels from one unmirrored mesh): tyre band, and on both faces a dish of
    `seg` quads between rs*R and hub*R whose material is slot(s) ("rim chrome"
    or "rim barrel"), plus a raised cap."""
    W = W * 0.5
    ring = [(math.cos(2 * math.pi * s / seg + phase), math.sin(2 * math.pi * s / seg + phase))
            for s in range(seg)]
    P = lambda r, c, s, y: (r * c, y, r * s)
    rr = rs * R
    prof = [(rr, -W * 0.90), (R, -W * 0.62), (R, W * 0.62), (rr, W * 0.90)]
    for s in range(seg):
        (c0, s0), (c1, s1) = ring[s], ring[(s + 1) % seg]
        for k in range(len(prof) - 1):
            (r0, y0), (r1, y1) = prof[k], prof[k + 1]
            q = [P(r0, c0, s0, y0), P(r0, c1, s1, y0), P(r1, c1, s1, y1), P(r1, c0, s0, y1)]
            mid = Vector(P(0.5 * (r0 + r1), 0.5 * (c0 + c1), 0.5 * (s0 + s1), 0.5 * (y0 + y1)))
            g.face(q, "tyre rubber", mirror=False, out=(mid.x, 1.5 * (y0 + y1) / W, mid.z))
    for side in (1, -1):
        yl, yh, rh = side * W * 0.90, side * W * 0.50, hub * R
        for s in range(seg):
            (c0, s0), (c1, s1) = ring[s], ring[(s + 1) % seg]
            g.face([P(rr, c0, s0, yl), P(rr, c1, s1, yl), P(rh, c1, s1, yh), P(rh, c0, s0, yh)],
                   slot(s), mirror=False, out=(0, side, 0))
        cap = [(rh * math.cos(2 * math.pi * k / cap_n), yh, rh * math.sin(2 * math.pi * k / cap_n))
               for k in range(cap_n)]
        for k in range(cap_n):
            g.face([(0, yh + side * 0.025, 0), cap[k], cap[(k + 1) % cap_n]], "rim chrome",
                   mirror=False, out=(0, side, 0))


# --- Blender plumbing -------------------------------------------------------------
def make_material(name, colour=None, image=None, glass_alpha=None, emissive=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.45
    if image is not None:
        tex = m.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = image
        tex.interpolation = "Closest"
        m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = colour
    if emissive:
        bsdf.inputs["Emission Color"].default_value = colour
        bsdf.inputs["Emission Strength"].default_value = 0.08
    if glass_alpha is not None:
        bsdf.inputs["Alpha"].default_value = glass_alpha
        bsdf.inputs["Roughness"].default_value = 0.1
        try:
            m.surface_render_method = "BLENDED"
        except Exception:
            pass
    return m


def to_object(g, name, mats, atlas, sharp_deg=38, triangulate=False):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    order = list(mats)
    for pts, mat, uvmode, mirror in g.faces:
        uvs = atlas.face_uvs(pts, uvmode)[0] if mat == "body paint" else [(0.0, 0.0)] * len(pts)
        copies = [(pts, uvs)]
        if mirror:  # the twin: y negated, winding reversed, same texels
            copies.append(([Vector((p.x, -p.y, p.z)) for p in reversed(pts)], list(reversed(uvs))))
        for cp, cuv in copies:
            vs = [bm.verts.new(p) for p in cp]
            try:
                f = bm.faces.new(vs)
            except ValueError:
                continue
            f.material_index = order.index(mat)
            for loop, uv in zip(f.loops, cuv):
                loop[uvl].uv = uv
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bad = [f for f in bm.faces if f.calc_area() < 1e-7]
    bmesh.ops.delete(bm, geom=bad, context="FACES")
    if triangulate:
        bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method="BEAUTY", ngon_method="BEAUTY")
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in order:
        me.materials.append(mats[m])
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    return ob


def bake_ao(body, image):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 160
    nt = body.data.materials[0].node_tree
    node = nt.nodes.new("ShaderNodeTexImage")
    node.image = image
    nt.nodes.active = node
    for o in bpy.context.scene.objects:
        o.select_set(False)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    sc.render.bake.margin = 3
    sc.world.light_settings.distance = 0.6
    bpy.ops.object.bake(type="AO")
    nt.nodes.remove(node)


def look_at(cam, target):
    d = Vector(target) - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


VIEWS = {  # designed around a 5.2 m car; scaled by the car's length
    "front34": ((5.2, 4.4, 1.9), (0.1, 0, 0.55), 40),
    "rear34": ((-5.6, 4.0, 2.2), (-0.2, 0, 0.55), 40),
    "side": ((0, 8.5, 0.75), (-0.2, 0, 0.62), 36),
    "chase": ((-7.4, 1.4, 2.4), (0.8, 0, 0.6), 42),
    "top": ((1.5, 2.2, 7.0), (-0.1, 0, 0.5), 38),
    "cabin": ((2.2, 3.0, 2.6), (-0.4, 0.2, 0.8), 36),
}


def stage():
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 960, 600
    sc.render.film_transparent = False
    sc.world.use_nodes = True
    bg = sc.world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.42, 0.50, 0.58, 1)
    bg.inputs["Strength"].default_value = 0.9
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.2
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    sc.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0))
    gnd = bpy.context.active_object
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
    gnd.data.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    return cam


def view_list(C, extra=None):
    views = dict(VIEWS)
    if extra:
        views.update(extra)
    f = C.SCALE * (0.35 + 0.65 * C.LENGTH / 5.2)
    zf = C.HEIGHT / 1.36
    return {k: (Vector(loc) * f, Vector((tgt[0] * f, tgt[1] * f, tgt[2] * C.SCALE * zf)), fov)
            for k, (loc, tgt, fov) in views.items()}


def render_previews(outdir, C):
    sc = bpy.context.scene
    cam = stage()
    for name, (loc, tgt, fov) in view_list(C).items():
        cam.location = loc
        look_at(cam, tgt)
        cam.data.angle = math.radians(fov)
        sc.render.filepath = os.path.join(outdir, f"{C.NAME}-{name}.png")
        bpy.ops.render.render(write_still=True)


def contact_sheet(paths, out, cols):
    """Half-size renders in a grid, left to right, top to bottom."""
    ims = []
    for p in paths:
        im = bpy.data.images.load(p)
        w, h = im.size
        px = np.array(im.pixels[:], np.float32).reshape(h, w, 4)[::-1]
        ims.append(px[::2, ::2])
        bpy.data.images.remove(im)
    h, w = ims[0].shape[:2]
    rows = []
    for r in range(0, len(ims), cols):
        row = ims[r:r + cols]
        while len(row) < cols:
            row.append(np.ones_like(ims[0]))
        rows.append(np.concatenate(row, axis=1))
    sheet = np.concatenate(rows, axis=0)[::-1]
    hh, ww = sheet.shape[:2]
    img = bpy.data.images.new("sheet", ww, hh, alpha=True)
    img.pixels[:] = np.ascontiguousarray(sheet).ravel()
    img.filepath_raw = out
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def place_wheels(C, wheel_mesh):
    names = {(True, 1): "wheel front left", (True, -1): "wheel front right",
             (False, 1): "wheel rear left", (False, -1): "wheel rear right"}
    wheels = []
    for front, wx in ((True, C.AXF), (False, C.AXR)):
        for side in (1, -1):
            o = bpy.data.objects.new(names[(front, side)], wheel_mesh)
            o.location = (wx, side * C.TRACK * 0.5, C.R_WHEEL)
            if side < 0:
                o.rotation_euler = (0, 0, math.pi)
            bpy.context.scene.collection.objects.link(o)
            wheels.append(o)
    return wheels


def export(objs, out):
    for o in bpy.context.scene.objects:
        o.select_set(o in objs)
    bpy.ops.export_scene.gltf(filepath=os.path.abspath(out), export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_materials="EXPORT",
                              export_image_format="AUTO")


def parse_args(defaults):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(defaults)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--preview", "--full"):
            opt[a[2:]] = argv[i + 1]
            i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--opaque-glass":
            opt["glass_alpha"] = None
        i += 1
    return opt


def main(C):
    """Build the full model. C defines: NAME, SCALE, LENGTH, HEIGHT, the
    dimensions (XF XR AXF AXR R_WHEEL W_WHEEL TRACK ARCH_R HUB_Z ARCH_INNER_Y
    ZMID CAPS), half_w(x), belt(x), section(x), stations(), glass_strip(k, xm),
    make_atlas(), build_trim(g), build_lamps(g), INTERIOR, build_wheel(g),
    paint(atlas), wheel_face(r, ang, up), COLOURS (material colours) and
    optionally uv_hook(k, xm, pts)."""
    opt = parse_args({"out": os.path.join(HERE, "..", "res", "models", C.NAME + ".glb"),
                      "preview": None, "bake": True, "glass_alpha": getattr(C, "GLASS_ALPHA", 0.42)})
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.world = bpy.data.worlds.new("world")
    atlas = C.make_atlas()

    img = bpy.data.images.new(C.NAME + "-paint", TEX, TEX, alpha=False)
    mats = {"body paint": make_material("body paint", image=img),
            "glass window": make_material("glass window", C.COLOURS["glass window"],
                                          glass_alpha=opt["glass_alpha"]),
            "headlights": make_material("headlights", C.COLOURS["headlights"], emissive=True),
            "rear lights": make_material("rear lights", C.COLOURS["rear lights"], emissive=True)}
    wmats = {n: make_material(n, C.COLOURS[n]) for n in ("tyre rubber", "rim chrome", "rim barrel")}

    g = Geo()
    build_loft(g, C, C.stations(), C.section, uv_hook=getattr(C, "uv_hook", None))
    build_caps(g, C, C.section)
    C.build_trim(g)
    C.build_lamps(g)
    build_interior(g, C, C.INTERIOR)
    for bay in getattr(C, "BAYS", []):
        build_engine_bay(g, C, bay)
    body = to_object(g, "body", mats, atlas)

    wg = Geo()
    C.build_wheel(wg)
    wheel_mesh = to_object(wg, "wheel", wmats, atlas).data
    bpy.data.objects.remove(bpy.data.objects["wheel"])
    wheels = place_wheels(C, wheel_mesh)

    atlas.begin(C.COLOURS["paint"])
    C.paint(atlas)
    for bay in getattr(C, "BAYS", []):
        if bay.get("engine", True):
            atlas.paint_engine(bay, getattr(C, "ENGINE_ACCENT", (150, 28, 24)))
    atlas.paint_cells()
    atlas.paint_wheel_face(C.wheel_face)
    ao = np.ones((TEX, TEX), np.float32)
    if opt["bake"]:
        aoimg = bpy.data.images.new("ao", TEX, TEX, alpha=False, float_buffer=True)
        bake_ao(body, aoimg)
        px = np.array(aoimg.pixels[:], np.float32).reshape(TEX, TEX, 4)[::-1, :, 0]
        pp = np.pad(px, 1, mode="edge")
        px = sum(pp[dy:dy + TEX, dx:dx + TEX] for dy in range(3) for dx in range(3)) / 9.0
        ao = np.clip(0.30 + 0.70 * px, 0, 1) ** 0.85
    texels = atlas.finish(ao)
    pix = np.ones((TEX, TEX, 4), np.float32)
    pix[..., :3] = texels[::-1] / 255.0
    img.pixels[:] = pix.ravel()
    img.pack()
    ncol = len({tuple(c) for c in texels.reshape(-1, 3)})

    body.data.transform(Matrix.Scale(C.SCALE, 4))
    wheel_mesh.transform(Matrix.Scale(C.SCALE, 4))
    for o in wheels:
        o.location = o.location * C.SCALE
    bpy.context.view_layer.update()

    bytype = {}
    for p in body.data.polygons:
        k = body.data.materials[p.material_index].name
        bytype[k] = bytype.get(k, 0) + len(p.vertices) - 2
    xs = [v.co.x for v in body.data.vertices]
    wb = (C.AXF - C.AXR) * C.SCALE
    axc = 0.5 * (C.AXF + C.AXR) * C.SCALE
    over = max(max(xs) - axc, axc - min(xs)) - wb * 0.5
    print(f"{C.NAME.upper()} body {tris(body)} tris {bytype}; wheel {tris(wheels[0])} tris; "
          f"texture {TEX}x{TEX} {ncol} colours")
    print(f"{C.NAME.upper()} drive: wheelBase {wb:.3f} track {C.TRACK * C.SCALE:.3f} "
          f"wheelRadius {C.R_WHEEL * C.SCALE:.3f} bodyOverhang {over:.2f} "
          f"(front overhang {(max(xs) - C.AXF * C.SCALE):.2f}, rear {(C.AXR * C.SCALE - min(xs)):.2f})")

    if opt["preview"]:
        os.makedirs(opt["preview"], exist_ok=True)
        img.save_render(os.path.join(opt["preview"], f"{C.NAME}-atlas.png"))
        render_previews(opt["preview"], C)
        contact_sheet([os.path.join(opt["preview"], f"{C.NAME}-{v}.png") for v in VIEWS],
                      os.path.join(opt["preview"], f"{C.NAME}-sheet.png"), 2)
    # the glass exports opaque; the definition's Glass opacity makes it see-through
    mats["glass window"].node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 1.0
    export([body] + wheels, opt["out"])
    print(f"{C.NAME.upper()} wrote", os.path.abspath(opt["out"]))


# --- the authored far model -------------------------------------------------------
def load_full_texture(glb, name):
    """The full model's "body paint" image: the far model must ship the SAME
    pixels (the vehicle bake compares them)."""
    with open(glb, "rb") as f:
        data = f.read()
    magic, _ver, _len = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67, "not a .glb"
    jlen, _ = struct.unpack_from("<II", data, 12)
    js = json.loads(data[20:20 + jlen])
    bin_off = 20 + jlen + 8
    mat = next(m for m in js["materials"] if m["name"] == "body paint")
    tex = js["textures"][mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
    img = js["images"][tex["source"]]
    bv = js["bufferViews"][img["bufferView"]]
    off = bin_off + bv.get("byteOffset", 0)
    path = os.path.join(tempfile.gettempdir(), f"{name}-paint-from-full.png")
    with open(path, "wb") as f:
        f.write(data[off:off + bv["byteLength"]])
    im = bpy.data.images.load(path)
    im.name = name + "-paint"
    im.pack()
    return im


def far_wheel(g, C, atlas, seg=8):
    """An octagonal tread in the black cell and the painted wheel face on BOTH
    sides (symmetric, like the full wheel)."""
    rad, hw = C.R_WHEEL, C.W_WHEEL * 0.5 * 0.9
    ring = [(rad * math.cos(2 * math.pi * s / seg + math.pi / seg),
             rad * math.sin(2 * math.pi * s / seg + math.pi / seg)) for s in range(seg)]
    for s in range(seg):
        (x0, z0), (x1, z1) = ring[s], ring[(s + 1) % seg]
        g.face([(x0, -hw, z0), (x1, -hw, z1), (x1, hw, z1), (x0, hw, z0)], uv="cell:black",
               mirror=False, out=(x0 + x1, 0, z0 + z1))
    for side in (1, -1):
        g.face([(x, side * hw, z) for (x, z) in ring], uv="disc", mirror=False, out=(0, side, 0))


class FarAtlas:
    """The full model's atlas plus the "disc" mode for the far wheel face."""

    def __init__(self, atlas):
        self.a = atlas

    def face_uvs(self, pts, uv):
        if uv == "disc":
            cx, cy, rr = self.a.wheel_disc
            rad = max(math.hypot(p.x, p.z) for p in pts)
            return [((cx + rr * p.x / rad) / TEX, 1.0 - (cy - rr * p.z / rad) / TEX) for p in pts], None
        return self.a.face_uvs(pts, uv)


def far_main(C):
    """Build the far model of car C (after its full model: it reads that
    model's texture). C also defines FAR_LINES (section indices kept),
    far_stations() and far_trim(g)."""
    full_default = os.path.join(HERE, "..", "res", "models", C.NAME + ".glb")
    opt = parse_args({"out": os.path.join(HERE, "..", "res", "models", C.NAME + "-far.glb"),
                      "preview": None, "full": full_default})
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.world = bpy.data.worlds.new("world")
    atlas = C.make_atlas()
    fa = FarAtlas(atlas)
    img = load_full_texture(os.path.abspath(opt["full"]), C.NAME)
    mat = make_material("body paint", image=img)

    g = Geo()
    build_loft(g, C, C.far_stations(), C.section, lines=C.FAR_LINES, glass=False,
               uv_hook=getattr(C, "far_uv_hook", None))
    build_caps(g, C, C.section, lines=C.FAR_LINES, fan=False)
    C.far_trim(g)
    body = to_object(g, "body", {"body paint": mat}, fa, triangulate=True)
    wg = Geo()
    far_wheel(wg, C, atlas)
    wheel_mesh = to_object(wg, "wheel", {"body paint": mat}, fa, sharp_deg=50, triangulate=True).data
    bpy.data.objects.remove(bpy.data.objects["wheel"])
    # the full wheels are drawn UNLIT, these ride in the lit body tier: face
    # normals turned towards the sky keep the dish as bright from every side
    loops = []
    for poly in wheel_mesh.polygons:
        n = poly.normal
        up = Vector((0.0, 0.35 * (1 if n.y > 0 else -1), 1.0)).normalized()
        for _ in poly.loop_indices:
            loops.append(up if abs(n.y) > 0.9 else n.copy())
    wheel_mesh.normals_split_custom_set(loops)
    wheels = place_wheels(C, wheel_mesh)

    body.data.transform(Matrix.Scale(C.SCALE, 4))
    wheel_mesh.transform(Matrix.Scale(C.SCALE, 4))
    for o in wheels:
        o.location = o.location * C.SCALE
    bpy.context.view_layer.update()
    print(f"{C.NAME.upper()}-FAR body {tris(body)} tris; wheel {tris(wheels[0])} tris x4; "
          f"far tier {tris(body) + 4 * tris(wheels[0])} tris; {len(C.far_stations())} stations, "
          f"{len(C.FAR_LINES)} lines")
    export([body] + wheels, opt["out"])
    print(f"{C.NAME.upper()}-FAR wrote", os.path.abspath(opt["out"]))

    if opt["preview"]:
        os.makedirs(opt["preview"], exist_ok=True)
        before = set(bpy.context.scene.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.abspath(opt["full"]))
        full_objs = [o for o in bpy.context.scene.objects if o not in before and o.type == "MESH"]
        far_objs = [body] + wheels
        cam = stage()
        views = view_list(C, {"distant": ((-11.0, 11.0, 3.2), (0.0, 0, 0.5), 42)})
        views.pop("cabin")
        paths = []
        for name, (loc, tgt, fov) in views.items():
            cam.location = loc
            look_at(cam, tgt)
            cam.data.angle = math.radians(fov)
            for tag, show, hide in (("full", full_objs, far_objs), ("far", far_objs, full_objs)):
                for o in show:
                    o.hide_render = False
                for o in hide:
                    o.hide_render = True
                p = os.path.join(opt["preview"], f"{C.NAME}-{tag}-{name}.png")
                sc.render.filepath = p
                bpy.ops.render.render(write_still=True)
                paths.append(p)
        contact_sheet(paths, os.path.join(opt["preview"], f"{C.NAME}-far-sheet.png"), 2)

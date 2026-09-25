#!/usr/bin/env python3
"""Generate the Strata GT: a 90s sports coupe built for the PS2 vehicle path.

Writes res/models/strata-gt.glb (+ a preview render with --preview PNG).

Built to the vehicle import's rules (docs/vehicles.md, "Importing a model"):
- ONE file, the four wheels separate identical nodes (found by shape, not name);
- untextured materials only, so the bake merges the whole body into one
  palette part - one submit - plus the lamps part;
- lamp materials named `headlights` / `rear lights` (the mesh-lamp split),
  matte trim named with `rubber`/`trim`, glass with `glass`;
- ~2.2k body triangles against the 2400 default budget, so the bake keeps the
  silhouette instead of decimating it, and ~140 per wheel: the wheels are the
  part the EE rebuilds every frame;
- front overhang shorter than the rear, so the nose heuristic needs no flip.

The body is a loft: ~40 stations along the car, each a 12-point half section
(bottom, wheel-arch cut-outs, sill, door, shoulder, greenhouse, roof stripe),
mirrored. Wheel arches are cut by raising the section's lower side over each
axle; the greenhouse (windscreen rake, roof, fastback) comes from blending the
upper section points between a "hood" and a "cabin" shape along the car.
Deterministic: the same script writes the same bytes.

Units are metres; +X forward, +Y up, +Z to the car's left.
"""
import argparse
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "res" / "models" / "strata-gt.glb"

# --- dimensions ---------------------------------------------------------------
LEN_F, LEN_R = 2.10, -2.22       # nose / tail x (front overhang shorter)
WHEEL_X = (1.28, -1.26)          # front / rear axle x  (wheelbase 2.54)
TRACK = 1.52
WHEEL_R, WHEEL_W = 0.325, 0.235
HALF_W = 0.89                    # body half width
Y_BOTTOM = 0.17                  # ground clearance of the floor
BELT = 0.74                      # door top / window base

# --- materials: (name, base colour) - names drive the bake's rules ----------
MATERIALS = [
    ("paint", (0.06, 0.20, 0.62)),          # deep blue metallic
    ("paint stripe", (0.93, 0.93, 0.90)),   # twin racing stripes
    ("trim rubber", (0.045, 0.045, 0.05)),  # bumpers, sills, arches: matte
    ("glass window", (0.10, 0.14, 0.19)),
    ("headlights", (0.97, 0.95, 0.86)),
    ("rear lights", (0.78, 0.04, 0.05)),
    ("grille trim", (0.02, 0.02, 0.025)),
    ("chrome", (0.78, 0.79, 0.81)),
    ("tyre rubber", (0.035, 0.035, 0.04)),
    ("rim chrome", (0.72, 0.73, 0.76)),
]
MAT = {n: i for i, (n, _) in enumerate(MATERIALS)}


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


# --- body profile functions of x ------------------------------------------------
def half_width(x):
    s = (x - (LEN_F + LEN_R) * 0.5) / ((LEN_F - LEN_R) * 0.5)  # -1..1
    # round the nose and the tail in plan view
    return HALF_W * (1.0 - 0.16 * abs(s) ** 5) - (0.05 if x > LEN_F - 0.12 else 0.0) * smooth(LEN_F - 0.12, LEN_F, x)


def hood_top(x):
    """The top surface where there is no cabin: nose -> hood -> cowl, and the
    deck -> tail with a small ducktail."""
    if x >= 0.0:
        # nose 0.60, rising along the hood to the cowl at 0.80
        return lerp(0.60, 0.80, smooth(LEN_F, 0.95, x)) + 0.03 * smooth(LEN_F, LEN_F - 0.35, x) * 0
    # deck: from the fastback base to a raised tail lip
    return lerp(0.86, 0.90, smooth(-1.30, LEN_R + 0.05, x))


def cabin_top(x):
    """Roof line: windscreen from the cowl, flat roof, fastback to the deck."""
    if x > 0.36:  # windscreen
        return lerp(0.80, 1.24, smooth(1.02, 0.36, x) ** 0.85)
    if x > -0.52:  # roof, very slight crown towards the front
        return 1.24 + 0.012 * smooth(-0.52, 0.2, x)
    return lerp(1.24, 0.88, smooth(-0.52, -1.32, x) ** 0.9)  # fastback


def cabin_factor(x):
    """0 = hood/deck section, 1 = full greenhouse section."""
    return smooth(1.06, 0.92, x) * smooth(-1.40, -1.24, x)


def arch_height(x):
    """Lower-side height: raised over each axle into a wheel arch."""
    y = Y_BOTTOM
    for wx in WHEEL_X:
        dx = x - wx
        R = WHEEL_R + 0.085
        if abs(dx) < R:
            y = max(y, 0.30 + math.sqrt(R * R - dx * dx) * 0.98)
    return y


def section(x):
    """12 half-section points (y, z) from the bottom centre up to the roof
    centre, the same count at every station so the loft is a grid."""
    w = half_width(x)
    ya = arch_height(x)
    over_arch = ya > Y_BOTTOM + 1e-4
    wb = min(w - 0.10, TRACK * 0.5 - WHEEL_W * 0.5 - 0.05) if over_arch else w - 0.10
    c = cabin_factor(x)
    ytop_h = hood_top(x)
    ytop_c = cabin_top(x)
    ytop = lerp(ytop_h, ytop_c, c) if c < 1 else ytop_c
    ytop = max(ytop, lerp(ytop_h, ytop_c, c))
    belt = lerp(ytop_h - 0.035, BELT, c) if x >= 0 else lerp(ytop_h - 0.04, BELT + 0.05, c)
    belt = max(belt, min(ytop_h, BELT) - 0.03)
    # tumblehome: the greenhouse leans in
    wg1 = lerp(w - 0.07, w - 0.09, c)
    wg2 = lerp(w * 0.60, 0.64, c)
    yg1 = lerp(ytop_h - 0.012, belt + 0.03, c)
    yg2 = lerp(ytop - 0.018, ytop - 0.05, c)
    ystr = ytop - 0.004
    pts = [
        (Y_BOTTOM, 0.0),                    # 0 floor centre
        (Y_BOTTOM, wb),                     # 1 floor edge
        (max(ya - 0.02, Y_BOTTOM), wb),     # 2 inner arch wall top
        (ya, w - 0.035),                    # 3 arch lip / sill
        (ya + 0.07, w),                     # 4 top of the sill
        (belt - 0.16, w + 0.012),           # 5 door crease (slight bulge)
        (belt - 0.02, w - 0.012),           # 6 shoulder
        (belt, w - 0.035),                  # 7 belt line
        (yg1, wg1),                         # 8 window base / hood crown
        (yg2, wg2),                         # 9 roof edge
        (ystr, 0.20),                       # 10 stripe edge
        (ytop, 0.0),                        # 11 roof / hood centre
    ]
    return pts


def quad_material(k, x):
    """Material of the loft face between section points k and k+1."""
    c = cabin_factor(x)
    if k <= 2:
        return MAT["trim rubber"]
    if k == 3:
        # sill strip, and the bumpers at both ends
        return MAT["trim rubber"]
    if k in (4, 5, 6):
        return MAT["paint"]
    if k == 7:
        return MAT["paint"]
    if k == 8:  # side glass between the pillars
        if c > 0.6 and -1.05 < x < 0.86 and not (-0.18 < x < -0.08):
            return MAT["glass window"]
        return MAT["paint"]
    if k == 9:  # roof edge band: windscreen / rear window run through it
        if c > 0.6 and (0.40 < x < 0.98 or -1.22 < x < -0.60):
            return MAT["glass window"]
        return MAT["paint"]
    if k == 10:  # centre band: glass on the screens, stripes elsewhere
        if c > 0.6 and (0.40 < x < 0.98 or -1.22 < x < -0.60):
            return MAT["glass window"]
        return MAT["paint stripe"]
    return MAT["paint"]


# --- stations: dense around the arches and the greenhouse transitions ---------
def stations():
    xs = set()
    n = 22
    for i in range(n + 1):
        t = i / n
        # ease: denser at both ends (rounded nose/tail)
        e = 0.5 - 0.5 * math.cos(math.pi * t)
        xs.add(round(lerp(LEN_R, LEN_F, lerp(t, e, 0.35)), 4))
    for wx in WHEEL_X:
        R = WHEEL_R + 0.085
        for f in (-1.0, -0.8, -0.45, 0.0, 0.45, 0.8, 1.0):
            xs.add(round(wx + f * R, 4))
    for x in (1.02, 0.94, 0.62, 0.36, -0.52, -0.90, -1.24, -1.32):
        xs.add(x)
    return sorted(v for v in xs if LEN_R <= v <= LEN_F)


# --- mesh builder -------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.prims = {}  # material -> (positions, indices)

    def tri(self, m, a, b, c):
        pos, idx = self.prims.setdefault(m, ([], []))
        base = len(pos)
        pos.extend([a, b, c])
        idx.extend([base, base + 1, base + 2])

    def quad(self, m, a, b, c, d):
        self.tri(m, a, b, c)
        self.tri(m, a, c, d)

    def tris(self):
        return sum(len(i) // 3 for _, i in self.prims.values())


def build_body():
    m = Mesh()
    xs = stations()
    secs = [section(x) for x in xs]
    for i in range(len(xs) - 1):
        x0, x1 = xs[i], xs[i + 1]
        s0, s1 = secs[i], secs[i + 1]
        xm = 0.5 * (x0 + x1)
        for k in range(len(s0) - 1):
            mat = quad_material(k, xm)
            for side in (1, -1):
                a = (x0, s0[k][0], side * s0[k][1])
                b = (x1, s1[k][0], side * s1[k][1])
                c = (x1, s1[k + 1][0], side * s1[k + 1][1])
                d = (x0, s0[k + 1][0], side * s0[k + 1][1])
                if side > 0:
                    m.quad(mat, a, b, c, d)
                else:
                    m.quad(mat, a, d, c, b)
    # --- end caps: nose (grille + headlights) and tail (lamps) -------------
    for xi, front in ((len(xs) - 1, True), (0, False)):
        x = xs[xi]
        s = secs[xi]
        cx = x + (0.0 if front else 0.0)
        for k in range(len(s) - 1):
            y0, z0 = s[k]
            y1, z1 = s[k + 1]
            ym = 0.5 * (y0 + y1)
            mat = MAT["trim rubber"] if ym <= 0.26 else MAT["paint"]
            for side in (1, -1):
                a = (cx, y0, side * z0)
                b = (cx, y1, side * z1)
                c = (cx, ym, 0.0)
                if (side > 0) == front:
                    m.tri(mat, a, b, (cx, y1, 0.0))
                    m.tri(mat, a, (cx, y1, 0.0), (cx, y0, 0.0))
                else:
                    m.tri(mat, a, (cx, y1, 0.0), b)
                    m.tri(mat, a, (cx, y0, 0.0), (cx, y1, 0.0))

    # --- lamps: slim panels just proud of the body -------------------------
    def panel(mat, xa, y0, y1, z0, z1, xb=None):
        xb = xa if xb is None else xb
        for side in (1, -1):
            p = [(xa, y0, side * z0), (xa, y1, side * z0), (xb, y1, side * z1), (xb, y0, side * z1)]
            if (side > 0) == (xa >= 0):
                m.quad(mat, p[0], p[3], p[2], p[1])
            else:
                m.quad(mat, p[0], p[1], p[2], p[3])

    xf = xs[-1] + 0.012
    panel(MAT["headlights"], xf, 0.47, 0.57, 0.30, 0.62)              # headlamps
    panel(MAT["headlights"], xf, 0.30, 0.34, 0.54, 0.64)              # fog lamps
    panel(MAT["grille trim"], xf, 0.45, 0.55, 0.0, 0.26)              # grille
    panel(MAT["grille trim"], xf, 0.28, 0.35, 0.0, 0.52)              # lower intake
    xr = xs[0] - 0.012
    panel(MAT["rear lights"], xr, 0.63, 0.77, 0.34, 0.68)             # tail lamp clusters
    panel(MAT["grille trim"], xr, 0.66, 0.74, 0.0, 0.32)              # centre garnish
    panel(MAT["grille trim"], xr, 0.46, 0.58, 0.0, 0.26)              # plate recess
    panel(MAT["trim rubber"], xr, 0.22, 0.30, 0.0, 0.80)              # diffuser strip
    # chrome exhaust tip under the rear bumper (right side), a small box
    def box(mat, cx, cy, cz, hx, hy, hz):
        v = [(cx + sx * hx, cy + sy * hy, cz + sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        for f in faces:
            m.quad(mat, *[v[i] for i in f])
    box(MAT["chrome"], xs[0] - 0.02, 0.24, -0.50, 0.05, 0.035, 0.05)
    box(MAT["chrome"], xs[0] - 0.02, 0.24, -0.36, 0.05, 0.035, 0.05)
    # mirrors on the doors, ahead of the side glass
    for side in (1, -1):
        wz = half_width(0.78) + 0.045
        box(MAT["paint"], 0.78, BELT + 0.08, side * wz, 0.07, 0.045, 0.065)
        box(MAT["glass window"], 0.705, BELT + 0.08, side * wz, 0.004, 0.032, 0.05)
    # rear wing: two uprights and a blade over the deck
    for side in (1, -1):
        box(MAT["trim rubber"], -1.92, 1.00, side * 0.62, 0.05, 0.10, 0.025)
    blade = [(-1.80, 1.11), (-2.08, 1.13)]
    for side in (1,):
        pass
    y0, y1 = 1.10, 1.14
    xa, xb = -1.80, -2.10
    zc = 0.80
    top = [(xa, y1, zc), (xb, y1 + 0.02, zc), (xb, y1 + 0.02, -zc), (xa, y1, -zc)]
    bot = [(xa, y0, zc), (xb, y0 + 0.02, zc), (xb, y0 + 0.02, -zc), (xa, y0, -zc)]
    m.quad(MAT["paint"], top[0], top[3], top[2], top[1])
    m.quad(MAT["paint"], bot[0], bot[1], bot[2], bot[3])
    m.quad(MAT["paint"], bot[0], top[0], top[1], bot[1])  # left end
    m.quad(MAT["paint"], bot[3], bot[2], top[2], top[3])
    m.quad(MAT["trim rubber"], bot[1], top[1], top[2], bot[2])  # trailing edge
    m.quad(MAT["paint"], bot[0], bot[3], top[3], top[0])        # leading edge
    return m


def build_wheel():
    """A wheel centred on its hub, axle along Z (outer face at +Z): tyre,
    rim lip, one dark disc behind five spokes, a hub cap. ~140 triangles:
    the wheel batch is rebuilt on the EE every frame, vertex by vertex, so a
    wheel costs EE time the body does not (docs/vehicles.md, the CC96 and
    Tristar ship 76-triangle wheels for the same reason)."""
    m = Mesh()
    seg = 14
    R, W = WHEEL_R, WHEEL_W * 0.5
    ring = [(math.cos(2 * math.pi * s / seg), math.sin(2 * math.pi * s / seg)) for s in range(seg)]
    # tyre: inner sidewall, tread, outer sidewall
    prof = [(0.215, -W * 0.92), (R, -W * 0.55), (R, W * 0.55), (0.215, W * 0.92)]
    for s in range(seg):
        (c0, s0), (c1, s1) = ring[s], ring[(s + 1) % seg]
        for k in range(len(prof) - 1):
            r0, z0 = prof[k]
            r1, z1 = prof[k + 1]
            m.quad(MAT["tyre rubber"], (r0 * c0, r0 * s0, z0), (r0 * c1, r0 * s1, z0),
                   (r1 * c1, r1 * s1, z1), (r1 * c0, r1 * s0, z1))
    zlip, zdish = W * 0.92, W * 0.45
    rl, rd = 0.215, 0.185
    for s in range(seg):
        (c0, s0), (c1, s1) = ring[s], ring[(s + 1) % seg]
        # the rim lip, sloping into the dish
        m.quad(MAT["rim chrome"], (rl * c0, rl * s0, zlip), (rl * c1, rl * s1, zlip),
               (rd * c1, rd * s1, zdish), (rd * c0, rd * s0, zdish))
        # the dark disc behind the spokes (reads as the open barrel)
        m.tri(MAT["tyre rubber"], (0.0, 0.0, zdish - 0.01), (rd * c1, rd * s1, zdish),
              (rd * c0, rd * s0, zdish))
    for sp in range(5):
        a = 2 * math.pi * sp / 5
        ca, sa = math.cos(a), math.sin(a)
        px, py = -sa, ca
        hw0, hw1 = 0.032, 0.022
        r0, r1 = 0.05, 0.19
        z0, z1 = zdish + 0.035, zdish + 0.004
        m.quad(MAT["rim chrome"], (ca * r0 + px * hw0, sa * r0 + py * hw0, z0),
               (ca * r1 + px * hw1, sa * r1 + py * hw1, z1),
               (ca * r1 - px * hw1, sa * r1 - py * hw1, z1),
               (ca * r0 - px * hw0, sa * r0 - py * hw0, z0))
    for s in range(7):
        a0 = 2 * math.pi * s / 7
        a1 = 2 * math.pi * (s + 1) / 7
        m.tri(MAT["rim chrome"], (0, 0, zdish + 0.05), (0.055 * math.cos(a0), 0.055 * math.sin(a0), zdish + 0.035),
              (0.055 * math.cos(a1), 0.055 * math.sin(a1), zdish + 0.035))
    return m


# --- glTF writer ----------------------------------------------------------------
def face_normals(pos):
    out = []
    for i in range(0, len(pos), 3):
        a, b, c = pos[i], pos[i + 1], pos[i + 2]
        u = [b[j] - a[j] for j in range(3)]
        v = [c[j] - a[j] for j in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        L = math.sqrt(sum(t * t for t in n)) or 1.0
        n = [t / L for t in n]
        out.extend([n, n, n])
    return out


def write_glb(path, body, wheel):
    buf = bytearray()
    views, accessors, meshes = [], [], []

    def add(data, target, comp, typ, count, mn=None, mx=None):
        while len(buf) % 4:
            buf.append(0)
        off = len(buf)
        buf.extend(data)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        acc = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
        if mn is not None:
            acc["min"], acc["max"] = mn, mx
        accessors.append(acc)
        return len(accessors) - 1

    def mesh_of(m, name):
        prims = []
        for mat, (pos, idx) in sorted(m.prims.items()):
            nrm = face_normals(pos)
            pdata = b"".join(struct.pack("<3f", *p) for p in pos)
            ndata = b"".join(struct.pack("<3f", *n) for n in nrm)
            idata = b"".join(struct.pack("<I", i) for i in idx)
            mn = [min(p[j] for p in pos) for j in range(3)]
            mx = [max(p[j] for p in pos) for j in range(3)]
            pa = add(pdata, 34962, 5126, "VEC3", len(pos), mn, mx)
            na = add(ndata, 34962, 5126, "VEC3", len(nrm))
            ia = add(idata, 34963, 5125, "SCALAR", len(idx))
            prims.append({"attributes": {"POSITION": pa, "NORMAL": na}, "indices": ia, "material": mat})
        meshes.append({"name": name, "primitives": prims})
        return len(meshes) - 1

    bm = mesh_of(body, "body")
    wm = mesh_of(wheel, "wheel")
    nodes = [{"name": "body", "mesh": bm}]
    names = {(True, 1): "wheel front left", (True, -1): "wheel front right",
             (False, 1): "wheel rear left", (False, -1): "wheel rear right"}
    for front, wx in ((True, WHEEL_X[0]), (False, WHEEL_X[1])):
        for side in (1, -1):
            node = {"name": names[(front, side)], "mesh": wm,
                    "translation": [wx, WHEEL_R, side * TRACK * 0.5]}
            if side < 0:  # outer face (+Z in the wheel mesh) faces outward
                node["rotation"] = [0.0, 1.0, 0.0, 0.0]
            nodes.append(node)
    gltf = {
        "asset": {"version": "2.0", "generator": "make-strata-gt.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": [{"name": n, "pbrMetallicRoughness": {"baseColorFactor": [c[0], c[1], c[2], 1.0],
                                                             "metallicFactor": 0.0, "roughnessFactor": 0.6}}
                      for n, c in MATERIALS],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buf)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    while len(buf) % 4:
        buf.append(0)
    out = struct.pack("<3I", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(buf))
    out += struct.pack("<2I", len(js), 0x4E4F534A) + js
    out += struct.pack("<2I", len(buf), 0x004E4942) + bytes(buf)
    path.write_bytes(out)


# --- preview: a small z-buffered rasterizer ---------------------------------------
def preview(path, body, wheel, views=((35, 18), (145, 14), (90, 3), (-20, 55))):
    import numpy as np
    from PIL import Image
    W, H = 640, 360
    tiles = []
    wheel_nodes = []
    for front, wx in ((True, WHEEL_X[0]), (False, WHEEL_X[1])):
        for side in (1, -1):
            wheel_nodes.append((wx, WHEEL_R, side * TRACK * 0.5, side))
    tris = []
    for mat, (pos, idx) in body.prims.items():
        for i in range(0, len(pos), 3):
            tris.append((pos[i], pos[i + 1], pos[i + 2], mat))
    for (tx, ty, tz, side) in wheel_nodes:
        for mat, (pos, idx) in wheel.prims.items():
            for i in range(0, len(pos), 3):
                t = []
                for (x, y, z) in pos[i:i + 3]:
                    if side < 0:
                        x, z = -x, -z
                    t.append((x + tx, y + ty, z + tz))
                if side < 0:
                    t = [t[0], t[1], t[2]]
                tris.append((t[0], t[1], t[2], mat))
    T = np.array([[a, b, c] for a, b, c, _ in tris], dtype=np.float64)
    M = np.array([m for *_, m in tris])
    cols = np.array([MATERIALS[m][1] for m in M])
    for yaw, pitch in views:
        ya, pa = math.radians(yaw), math.radians(pitch)
        # camera on a sphere around the car, looking at its centre
        fwd = np.array([-math.cos(pa) * math.cos(ya), -math.sin(pa), -math.cos(pa) * math.sin(ya)])
        right = np.cross(fwd, [0.0, 1.0, 0.0]); right /= np.linalg.norm(right)
        up = np.cross(right, fwd)
        C = T - np.array([0.0, 0.62, 0.0])
        sx = W / 2 + (C @ right) * 125
        sy = H / 2 - (C @ up) * 125
        dz = C @ fwd
        img = np.full((H, W, 3), [0.52, 0.60, 0.66])
        zb = np.full((H, W), 1e9)
        # lambert light
        e1 = T[:, 1] - T[:, 0]
        e2 = T[:, 2] - T[:, 0]
        n = np.cross(e1, e2)
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        L = np.array([0.4, 0.8, 0.45]); L /= np.linalg.norm(L)
        shade = 0.35 + 0.65 * np.abs(n @ L)
        for t in range(len(T)):
            x0, y0 = sx[t], sy[t]
            minx, maxx = int(max(0, x0.min())), int(min(W - 1, x0.max() + 1))
            miny, maxy = int(max(0, y0.min())), int(min(H - 1, y0.max() + 1))
            if minx > maxx or miny > maxy:
                continue
            xs, ys = np.meshgrid(np.arange(minx, maxx + 1) + 0.5, np.arange(miny, maxy + 1) + 0.5)
            (ax, bx, cx), (ay, by, cy) = x0, y0
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            w0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
            w1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
            w2 = 1 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            z = w0 * dz[t, 0] + w1 * dz[t, 1] + w2 * dz[t, 2]
            sub = zb[miny:maxy + 1, minx:maxx + 1]
            ok = inside & (z < sub)
            sub[ok] = z[ok]
            img[miny:maxy + 1, minx:maxx + 1][ok] = cols[t] * shade[t]
        # ground shadow line
        tiles.append((np.clip(img, 0, 1) * 255).astype(np.uint8))
    top = np.concatenate(tiles[:2], axis=1)
    bot = np.concatenate(tiles[2:4], axis=1)
    Image.fromarray(np.concatenate([top, bot], axis=0)).save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--preview", default="")
    a = ap.parse_args()
    body, wheel = build_body(), build_wheel()
    write_glb(Path(a.out), body, wheel)
    print(f"wrote {a.out}: body {body.tris()} tris in {len(body.prims)} materials, "
          f"wheel {wheel.tris()} tris")
    if a.preview:
        preview(a.preview, body, wheel)
        print("preview", a.preview)


if __name__ == "__main__":
    main()

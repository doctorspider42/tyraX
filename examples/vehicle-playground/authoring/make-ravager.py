"""Build the Ravager: a late-60s muscle coupe for the PS2 vehicle path.

Run through Blender, not plain Python:

    blender -b --factory-startup --python make-ravager.py -- [--out GLB]
            [--preview DIR] [--no-bake] [--opaque-glass]

Writes res/models/ravager.glb and, with --preview, a set of Eevee renders
(three-quarter front and rear, side, chase camera) into DIR.

The body is a loft over ELEVEN longitudinal character lines per half side
(keel, floor edge, rocker, tuck-under, flank, belt crease, fender round, sill,
greenhouse rail, rail inner edge, crown), sampled at ~34 stations and mirrored.
Each line is a function of x per body region (hood, windscreen, roof, tunnel
rear window between the flying-buttress sails, deck), which is what gives the
car its intended shape rather than a lofted box: the tumblehome of the side
glass, the coke-bottle hip over the rear wheel, the recessed tunnel window, the
ducktail. Wheel arches are cut by lifting the lower lines over each axle.

Everything opaque shares ONE textured material ("body paint"): paint, vinyl
roof, chrome trim, grille, bumpers, interior and wheel wells are cells of one
256x256 atlas, projected per face (side / top / front / rear tiles, mirrored
halves share texels) and painted here in world coordinates. Cycles bakes
ambient occlusion into the same UVs. The image is truecolour: the vehicle
bake folds it to the model's Texture depth (8 bits, 256 colours, in the Motor
District - at 4 bits the paint's shading bands into visible steps). Glass, headlights and rear lights are untextured materials named for
the vehicle import's rules (docs/vehicles.md).

The interior is deliberately minimal (tub, two buckets, bench, dash, wheel):
it only matters once the glass is drawn translucent.

The atlas also carries paint that only the LOW-POLY far model samples
(make-ravager-far.py, which imports this file and wears this texture): the
windows, in the texels this model's glass parts cover; lamp lenses under its
lamp parts; a wheel face in the free eighth cell slot. Those areas keep
KEEP_OUT off every edge this model's faces meet, and the AO is kept off them.
Re-run make-ravager-far.py after this script.

Units are metres, Blender axes: +X forward, +Y to the car's left, +Z up.
Deterministic apart from the AO bake's sampling noise (blurred before use).
"""
import math
import os
import sys
import types

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "res", "models", "ravager.glb")

# --- dimensions (designed 1:1, exported at SCALE) ------------------------------
SCALE = 0.92                 # a 5.2 m car is large for the district's streets
XF, XR = 2.40, -2.82         # front / rear faces (front overhang is shorter)
AXF, AXR = 1.48, -1.49       # axles: wheelbase 2.97
R_WHEEL, W_WHEEL = 0.345, 0.245
TRACK = 1.56
ARCH_R = 0.405
HUB_Z = R_WHEEL
XWB, XWT = 0.60, -0.20       # windscreen base / top
XRE, XRW = -1.05, -1.55      # roof end / rear window base
XSE = -2.05                  # sail end (buttresses meet the deck)
XQ = -1.117                  # rear edge of the quarter glass
XDASH = 0.55                 # interior front (dash) / rear (parcel shelf)
XSHELF = -1.50
ARCH_INNER_Y = 0.58
# Under the bonnet (carkit.build_engine_bay / Atlas.paint_engine, shared with
# the other lofted cars): a big V8 with black crinkle valve covers, the floor
# at hub height, the radiator behind the grille.
BAY = {"x0": XWB - 0.03, "x1": XF - 0.24, "w": ARCH_INNER_Y - 0.04, "z": HUB_Z + 0.10,
       "engine": True, "radiator": True}
ENGINE_ACCENT = (215, 92, 22)   # Hemi orange, like the paint


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


# plan-view half width: coke bottle - full front fender, waisted doors, hips
half_w = pchip([(-2.82, .925), (-2.62, .958), (-2.1, .975), (-1.6, .978), (-1.1, .962),
                (-0.45, .946), (0.3, .950), (1.0, .960), (1.8, .962), (2.25, .952), (2.40, .932)])
# belt crease height: kicks up over the rear wheel
belt = pchip([(-2.82, .935), (-2.5, .955), (-1.95, .958), (-1.5, .946), (-1.0, .918),
              (-0.3, .902), (0.6, .905), (1.5, .900), (2.2, .886), (2.40, .872)])
# floor / rocker bottom: low between the axles, rising into the overhangs
rocker_z = pchip([(-2.82, .36), (-2.4, .30), (-1.95, .25), (-1.0, .23), (1.0, .23),
                  (1.9, .26), (2.25, .29), (2.40, .31)])
hood_z = pchip([(0.60, .948), (1.0, .938), (1.6, .918), (2.1, .898), (2.40, .878)])
deck_z = pchip([(-2.82, .978), (-2.76, .992), (-2.5, .986), (-2.0, .990), (-1.55, 1.0)])
roof_z = pchip([(XWT, 1.345), (-0.6, 1.356), (XRE, 1.343)])
sail_z = pchip([(XSE, .992), (-1.85, 1.04), (-1.6, 1.115), (-1.3, 1.225), (XRE, 1.300)])


def center_z(x):
    """The crown line: nose, hood, windscreen, roof, tunnel window, deck."""
    if x >= XWB:
        return hood_z(x)
    if x >= XWT:  # windscreen, a little curvature
        t = (XWB - x) / (XWB - XWT)
        return lerp(hood_z(XWB) + 0.002, roof_z(XWT), t) + 0.012 * math.sin(math.pi * t)
    if x >= XRE:
        return roof_z(x)
    if x >= XRW:  # rear window, flat
        t = (XRE - x) / (XRE - XRW)
        return lerp(roof_z(XRE), deck_z(XRW), t)
    return deck_z(x)


def rail(x):
    """Greenhouse outer line P8: hood edge, A-pillar, roof rail, sail top, deck."""
    w = half_w(x)
    if x >= XWB:
        return w - 0.25, hood_z(x) - 0.012
    if x >= XWT:
        t = (XWB - x) / (XWB - XWT)
        return lerp(half_w(XWB) - 0.25, 0.645, t), lerp(hood_z(XWB) - 0.004, 1.315, t)
    if x >= XRE:
        t = (XWT - x) / (XWT - XRE)
        return lerp(0.645, 0.652, t), lerp(1.315, 1.300, t)
    if x >= XSE:
        t = (XRE - x) / (XRE - XSE)
        return lerp(0.652, 0.742, t ** 1.4), sail_z(x)
    return 0.742, deck_z(x) - 0.006


def section(x):
    """Half cross-section at station x: 11 (y, z) points, keel to crown."""
    w, b, zr = half_w(x), belt(x), rocker_z(x)
    p = [
        (0.0, zr - 0.012),                 # 0 keel
        (w - 0.20, zr - 0.010),            # 1 floor edge
        (w - 0.095, zr),                   # 2 rocker bottom
        (w - 0.042, zr + 0.14),            # 3 tuck-under
        (w, 0.60),                         # 4 flank
        (w - 0.008, b),                    # 5 belt crease
        (w - 0.038, b + 0.026),            # 6 fender round
        (w - 0.118, b + 0.040),            # 7 sill / fender top inner
    ]
    ry, rz = rail(x)
    p.append((ry, rz))                     # 8 greenhouse rail
    # 9 rail inner edge (pillar / drip rail / flat top of the sail)
    if x >= XWB:
        p.append((ry - 0.07, hood_z(x) - 0.004))
    elif x >= XRE:
        p.append((ry - 0.07, rz + (0.018 if x < XWT else 0.012)))
    elif x >= XSE:
        p.append((ry - 0.062, rz + 0.004))
    else:
        p.append((ry - 0.07, deck_z(x) - 0.002))
    # 10 glass edge / crown shoulder
    cz = center_z(x)
    if x >= XRE + 0.0 or x < XSE:
        iy = 0.36 if x >= XRE else 0.40
        dz = -0.010 if XWT <= x < XWB else (-0.006 if XWT > x >= XRE else 0.0)
        p.append((iy, cz + dz))
    else:  # tunnel: the window edge hugs the inside of the sail
        t = ramp(XRE - x, 0.0, 0.035)
        p.append((lerp(0.36, p[9][0] - 0.012, t), cz))
    p.append((0.0, cz))                    # 11 crown
    # wheel arches: lift the lower lines over each axle
    for ax in (AXF, AXR):
        dx = x - ax
        if abs(dx) < ARCH_R:
            za = HUB_Z + math.sqrt(ARCH_R * ARCH_R - dx * dx)
            if za > p[1][1]:
                p[1] = (min(p[1][0], 0.58), max(p[1][1], za))
            for k in (2, 3, 4):
                p[k] = (p[k][0], max(p[k][1], za))
    return p


def stations():
    xs = {XF, XR, XR + 0.03, -2.74, -2.6, -2.35, XSE, -1.8, XRW, XRE, -0.62, XWT, 0.2,
          XWB, 0.85, 2.1, 2.27, -0.1 + XWT, XQ}
    for ax in (AXF, AXR):
        for f in (-1, -.92, -.7, -.38, 0, .38, .7, .92, 1):
            xs.add(ax + f * ARCH_R)
    out = []
    for x in sorted(xs, reverse=True):
        if not out or out[-1] - x > 0.02:
            out.append(x)
    return out


# --- a tiny mesh builder: faces with a material and a UV mode -------------------
class Geo:
    def __init__(self):
        self.faces = []  # (points[list of Vector], material, uvmode, mirror)

    def face(self, pts, mat, uv="auto", mirror=True, out=None):
        pts = [Vector(p) for p in pts]
        if out is not None:
            n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
            if len(pts) > 3:
                n += (pts[2] - pts[0]).cross(pts[3] - pts[0])
            if n.dot(Vector(out)) < 0:
                pts.reverse()
        self.faces.append((pts, mat, uv, mirror))

    def box(self, c, h, mat, uv="auto", mirror=True, skip=()):
        cx, cy, cz = c
        hx, hy, hz = h
        v = lambda sx, sy, sz: (cx + sx * hx, cy + sy * hy, cz + sz * hz)
        quads = {
            "+x": [v(1, -1, -1), v(1, 1, -1), v(1, 1, 1), v(1, -1, 1)],
            "-x": [v(-1, 1, -1), v(-1, -1, -1), v(-1, -1, 1), v(-1, 1, 1)],
            "+y": [v(1, 1, -1), v(-1, 1, -1), v(-1, 1, 1), v(1, 1, 1)],
            "-y": [v(-1, -1, -1), v(1, -1, -1), v(1, -1, 1), v(-1, -1, 1)],
            "+z": [v(-1, -1, 1), v(1, -1, 1), v(1, 1, 1), v(-1, 1, 1)],
            "-z": [v(-1, 1, -1), v(1, 1, -1), v(1, -1, -1), v(-1, -1, -1)],
        }
        for k, q in quads.items():
            if k not in skip:
                self.face(q, mat, uv, mirror)


def glass_strip(k, xm):
    """Which loft strips are glass: side glass, windscreen, rear window."""
    if k == 7 and XQ < xm < XWB:
        return True
    if k in (9, 10) and XWT < xm < XWB:
        return True
    if k == 10 and XRW < xm < XRE:
        return True
    return False


def arch_z(x):
    """Height of the wheel-arch opening at x, or None outside both arches."""
    for ax in (AXF, AXR):
        dx = x - ax
        if abs(dx) < ARCH_R:
            return HUB_Z + math.sqrt(ARCH_R * ARCH_R - dx * dx)
    return None


def on_arch(pts):
    """Every corner lifted onto the arch: the face is wheel-well roof."""
    for p in pts:
        za = arch_z(p[0])
        if za is None or abs(p[2] - za) > 1e-4:
            return False
    return True


def build_body(g):
    st = stations()
    secs = [section(x) for x in st]
    npt = len(secs[0])
    for i in range(len(st) - 1):
        xa, xb = st[i], st[i + 1]
        xm = 0.5 * (xa + xb)
        for k in range(npt - 1):
            a0, a1 = secs[i][k], secs[i][k + 1]
            b0, b1 = secs[i + 1][k], secs[i + 1][k + 1]
            pts = [(xa, a0[0], a0[1]), (xb, b0[0], b0[1]), (xb, b1[0], b1[1]), (xa, a1[0], a1[1])]
            # outward reference: from the section's middle towards the face
            cy = 0.25 * (a0[0] + a1[0] + b0[0] + b1[0])
            cz = 0.25 * (a0[1] + a1[1] + b0[1] + b1[1])
            ref = (0.0, cy - 0.0, cz - 0.62)
            if k <= 1:
                ref = (0.0, 0.0, -1.0) if k == 0 else (0.0, 0.3, -1.0)
            mat, uv = "body paint", "auto"
            if glass_strip(k, xm):
                mat = "glass window"
            elif 1 <= k <= 3 and on_arch(pts):
                uv = "cell:black"   # wheel-well roof
            elif k == 0:
                uv = "cell:black"   # floor; the lifted wheel-well roofs face
                #                     down and land in the black cell by normal
            g.face(pts, mat, uv, out=ref)
    # recessed end caps: the grille and the tail panel
    for x0, sign, depth in ((XF, 1, 0.075), (XR, -1, 0.045)):
        ring = section(x0)
        zc = 0.60
        inner = [(y * 0.94, zc + (z - zc) * 0.90) for (y, z) in ring]
        xi = x0 - sign * depth
        for k in range(len(ring) - 1):
            (y0, z0), (y1, z1) = ring[k], ring[k + 1]
            (u0, w0), (u1, w1) = inner[k], inner[k + 1]
            g.face([(x0, y0, z0), (x0, y1, z1), (xi, u1, w1), (xi, u0, w0)],
                   "body paint", "cell:black", out=(sign * 0.2, -(y0 + y1), -(z0 + z1 - 2 * zc)))
        # the face itself: fan to a point on the centre line, per strip
        for k in range(len(inner) - 1):
            (u0, w0), (u1, w1) = inner[k], inner[k + 1]
            g.face([(xi, 0.0, zc), (xi, u0, w0), (xi, u1, w1)], "body paint", "auto",
                   out=(sign, 0, 0))


def build_trim(g):
    # chrome blade bumpers wrapping round the corners (half, mirrored)
    def bar(path, z0, z1, depth):
        # path: list of (x, y) along the outside; extruded inward by depth
        for (xa, ya, na), (xb, yb, nb) in zip(path, path[1:]):
            oa = Vector((xa, ya)); ob = Vector((xb, yb))
            ia = oa - Vector(na) * depth; ib = ob - Vector(nb) * depth
            P = lambda v, z: (v.x, v.y, z)
            nout = Vector(((na[0] + nb[0]) * 0.5, (na[1] + nb[1]) * 0.5, 0))
            g.face([P(oa, z0), P(ob, z0), P(ob, z1), P(oa, z1)], "body paint", "cell:chrome", out=nout)
            g.face([P(oa, z1), P(ob, z1), P(ib, z1), P(ia, z1)], "body paint", "cell:chrome", out=(0, 0, 1))
            g.face([P(oa, z0), P(ob, z0), P(ib, z0), P(ia, z0)], "body paint", "cell:dark", out=(0, 0, -1))
        (xe, ye, ne) = path[-1]
        oe = Vector((xe, ye)); ie = oe - Vector(ne) * depth
        g.face([(oe.x, oe.y, z0), (ie.x, ie.y, z0), (ie.x, ie.y, z1), (oe.x, oe.y, z1)],
               "body paint", "cell:chrome", out=(-ne[1], ne[0], 0) if ne[0] > 0 else (ne[1], -ne[0], 0))
    wf = half_w(XF - 0.1)
    s2 = math.sqrt(0.5)
    bar([(XF + 0.035, 0.0, (1, 0)), (XF + 0.03, 0.62, (1, 0)), (XF + 0.0, wf - 0.02, (s2, s2)),
         (XF - 0.16, wf + 0.018, (0, 1))], 0.455, 0.535, 0.05)
    wr = half_w(XR + 0.1)
    bar([(XR - 0.04, 0.0, (-1, 0)), (XR - 0.035, 0.62, (-1, 0)), (XR - 0.005, wr - 0.02, (-s2, s2)),
         (XR + 0.17, wr + 0.018, (0, 1))], 0.43, 0.52, 0.05)
    # driver's mirror (left side only)
    xm, ym, zm = 0.36, half_w(0.36) + 0.055, belt(0.36) + 0.075
    g.box((xm, ym, zm), (0.028, 0.048, 0.03), "body paint", "cell:chrome", mirror=False, skip=("-y",))
    g.box((xm + 0.008, half_w(0.36) + 0.006, belt(0.36) + 0.035), (0.016, 0.01, 0.022),
          "body paint", "cell:chrome", mirror=False, skip=("-y",))
    # exhaust tips under the rear valance
    for yy in (0.52,):
        g.box((XR + 0.02, yy, 0.31), (0.07, 0.028, 0.022), "body paint", "cell:dark", skip=("+x",))


def build_lamps(g):
    # four round headlamps in the grille (the hidden-lamp doors, opened)
    xh = XF - 0.075 + 0.012
    for yc in (0.50, 0.73):
        zc, r, n = 0.690, 0.066, 8
        ring = [(xh, yc + r * math.cos(2 * math.pi * s / n), zc + r * math.sin(2 * math.pi * s / n))
                for s in range(n)]
        for s in range(n):
            g.face([(xh + 0.01, yc, zc), ring[s], ring[(s + 1) % n]], "headlights", "auto",
                   out=(1, 0, 0))
    # split tail lamps in the recessed tail panel
    xt = XR + 0.045 - 0.012
    for (y0, y1) in ((0.14, 0.44), (0.50, 0.80)):
        g.face([(xt, y0, 0.735), (xt, y1, 0.735), (xt, y1, 0.845), (xt, y0, 0.845)],
               "rear lights", "auto", out=(-1, 0, 0))


def _kit():
    """carkit.py, the shared plumbing of the later lofted cars - borrowed for
    the engine bay only; everything else here stays this file's own."""
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    import carkit
    return carkit


def build_interior(g):
    wi = lambda x: half_w(x) - 0.13
    zf = 0.42
    xa, xb = XSHELF, XDASH
    # floor and inner door panels (the tub), facing inward
    g.face([(xa, 0, zf), (xb, 0, zf), (xb, wi(xb), zf), (xa, wi(xa), zf)], "body paint", "cell:int", out=(0, 0, 1))
    for (x0, x1) in ((xa, -0.45), (-0.45, xb)):
        g.face([(x0, wi(x0), zf), (x1, wi(x1), zf), (x1, wi(x1), belt(x1) + 0.035), (x0, wi(x0), belt(x0) + 0.035)],
               "body paint", "cell:int", out=(0, -1, 0))
        # door top cap, from the inner panel to the sill line
        g.face([(x0, wi(x0), belt(x0) + 0.035), (x1, wi(x1), belt(x1) + 0.035),
                (x1, half_w(x1) - 0.118, belt(x1) + 0.04), (x0, half_w(x0) - 0.118, belt(x0) + 0.04)],
               "body paint", "cell:intdark", out=(0, 0, 1))
    # dash: top slab and its rear face
    zd = 0.905
    g.face([(0.30, 0, zd), (XDASH + 0.08, 0, zd), (XDASH + 0.08, wi(XDASH), zd), (0.30, wi(0.3), zd)],
           "body paint", "cell:intdark", out=(0, 0, 1))
    g.face([(0.30, 0, 0.66), (0.30, wi(0.3), 0.66), (0.30, wi(0.3), zd), (0.30, 0, zd)],
           "body paint", "cell:int", out=(-1, 0, 0))
    # parcel shelf
    g.face([(XSHELF - 0.12, 0, 0.99), (XSHELF + 0.22, 0, 0.99), (XSHELF + 0.22, wi(XSHELF), 0.99),
            (XSHELF - 0.12, wi(XSHELF), 0.99)], "body paint", "cell:intdark", out=(0, 0, 1))
    # rear bench: cushion + back
    g.box((-1.02, 0.36, 0.49), (0.25, 0.36, 0.07), "body paint", "cell:seat", skip=("-z", "-y"))
    g.face([(-1.30, 0, 0.56), (-1.30, 0.72, 0.56), (-1.40, 0.72, 0.99), (-1.40, 0, 0.99)],
           "body paint", "cell:seat", out=(1, 0, 0.3))
    # front buckets
    for yc in (0.40,):
        g.box((-0.12, yc, 0.52), (0.24, 0.23, 0.08), "body paint", "cell:seat", skip=("-z",))
        back = [(-0.36, yc - 0.23, 0.60), (-0.36, yc + 0.23, 0.60), (-0.47, yc + 0.20, 1.14), (-0.47, yc - 0.20, 1.14)]
        g.face(back, "body paint", "cell:seat", out=(1, 0, 0.2))
        g.face([(p[0] - 0.09, p[1], p[2]) for p in back], "body paint", "cell:intdark", out=(-1, 0, -0.2))
        g.face([back[1], back[2], (back[2][0] - 0.09, back[2][1], back[2][2]),
                (back[1][0] - 0.09, back[1][1], back[1][2])], "body paint", "cell:intdark", out=(0, 1, 0))
        g.face([back[2], back[3], (back[3][0] - 0.09, back[3][1], back[3][2]),
                (back[2][0] - 0.09, back[2][1], back[2][2])], "body paint", "cell:intdark", out=(0, 0, 1))
    # steering wheel (driver's side only): a flat octagonal ring, both faces
    c = Vector((0.20, 0.40, 0.86))
    tilt = Matrix.Rotation(math.radians(-62), 3, "Y")
    n = 8
    ro, ri = 0.19, 0.155
    pt = lambda r, s: c + tilt @ Vector((0, r * math.cos(2 * math.pi * s / n + 0.39),
                                         r * math.sin(2 * math.pi * s / n + 0.39)))
    for s in range(n):
        q = [pt(ro, s), pt(ro, s + 1), pt(ri, s + 1), pt(ri, s)]
        g.face(q, "body paint", "cell:intdark", mirror=False, out=tilt @ Vector((1, 0, 0)))
        g.face(q, "body paint", "cell:intdark", mirror=False, out=tilt @ Vector((-1, 0, 0)))
    # a centre console with a gear lever, a binnacle over the wheel, door
    # cards with an armrest - all in the dark cells
    g.box((0.05, 0.0, 0.53), (0.33, 0.085, 0.11), "body paint", "cell:intdark", mirror=False, skip=("-z",))
    g.box((0.20, 0.0, 0.70), (0.02, 0.015, 0.06), "body paint", "cell:dark", mirror=False, skip=("-z",))
    g.box((0.20, 0.0, 0.77), (0.03, 0.03, 0.02), "body paint", "cell:dark", mirror=False)
    g.box((0.33, 0.40, zd + 0.035), (0.07, 0.16, 0.035), "body paint", "cell:dark", mirror=False, skip=("-z",))
    for (x0, x1) in ((xa, -0.45), (-0.45, xb)):
        xc = 0.5 * (x0 + x1)
        zc = 0.5 * (zf + belt(xc)) + 0.06
        g.box((xc, wi(xc) - 0.03, zc), (0.5 * (x1 - x0) - 0.06, 0.03, 0.025), "body paint",
              "cell:intdark", skip=("+y",))
    hub = c + tilt @ Vector((0.02, 0, 0))
    g.face([hub + Vector((0, -0.03, 0)), hub + Vector((0, 0.03, 0)), Vector((0.42, 0.43, 0.70)),
            Vector((0.42, 0.37, 0.70))], "body paint", "cell:intdark", mirror=False, out=(-0.5, 0, 1))


# --- the atlas -------------------------------------------------------------------
TEX = 256
SS = 4                       # paint supersampling
TILE = {                     # name: (u0, v0, w, h) in pixels, v down; world ranges
    "side": ((0, 0, 256, 96), (XR - 0.02, XF + 0.05), (0.14, 1.40)),
    "top": ((0, 96, 192, 64), (XR - 0.02, XF + 0.05), (0.0, 1.0)),
    # a quarter of the top tile's length went to the engine bay's floor
    "engine": ((192, 96, 64, 64), (BAY["x0"], BAY["x1"]), (0.0, BAY["w"])),
    "front": ((0, 160, 96, 96), (0.0, 1.0), (0.18, 1.00)),
    "rear": ((96, 160, 96, 96), (0.0, 1.0), (0.18, 1.06)),
}
CELLS = ["black", "dark", "chrome", "int", "intdark", "seat", "paint"]
CELL_RECT = {name: (192 + (i % 2) * 32, 160 + (i // 2) * 24, 32, 24) for i, name in enumerate(CELLS)}
# The eighth cell slot: a painted wheel face for the LOW-POLY far model
# (make-ravager-far.py), whose wheels are two octagons. This model never
# samples it. Centre and radius in pixels.
WHEEL_RECT = (224, 232, 32, 24)
WHEEL_DISC = (240.0, 244.0, 11.5)

# The colours, in 0..255. The texture is truecolour; the vehicle bake folds it
# to the model's Texture depth (the Motor District ships it at 8 bits, 256
# colours - 4 bits bands the paint's shading into visible steps).
PAINT = (230, 102, 18)       # Hemi orange
BLACK = (20, 20, 22)
VINYL_C = (26, 26, 27)
AMBER = (240, 150, 28)
RED = (190, 22, 20)
PLATE = (226, 206, 120)
SEAT = (44, 40, 36)          # the cabin is dark on purpose: a lost door or
INT_C = (27, 25, 23)         # window shows it, and dark hides how little it is
GLASS_C = (26, 33, 41)       # the "glass window" material's colour
KEEP_OUT = 0.035             # m, two texels: see the far-model glass below
LAMP_C = (234, 232, 214)
VINYL = True


def tile_uv(name, a, b):
    (u0, v0, w, h), (a0, a1), (b0, b1) = TILE[name]
    if name == "side":   # x left->right = rear->front, z up
        u = u0 + (a - a0) / (a1 - a0) * w
        v = v0 + (1 - (b - b0) / (b1 - b0)) * h
    elif name in ("top", "engine"):  # x, |y| down from the centre line
        u = u0 + (a - a0) / (a1 - a0) * w
        v = v0 + (b - b0) / (b1 - b0) * h
    else:                # |y| from the centre outwards, z up
        u = u0 + (a - a0) / (a1 - a0) * w
        v = v0 + (1 - (b - b0) / (b1 - b0)) * h
    return (u / TEX, 1.0 - v / TEX)


def face_uvs(pts, uvmode):
    if uvmode == "cell:chrome":
        # a vertical chrome ramp: each face spans the cell top to bottom
        x, y, w, h = CELL_RECT["chrome"]
        z0, z1 = min(p.z for p in pts), max(p.z for p in pts)
        if z1 - z0 > 0.004:
            return [((x + w * 0.5) / TEX, 1.0 - (y + 1 + (h - 2) * (z1 - p.z) / (z1 - z0)) / TEX)
                    for p in pts], None
    if uvmode == "tile:engine":
        return [tile_uv("engine", p.x, abs(p.y)) for p in pts], "engine"
    if uvmode.startswith("cell:"):
        x, y, w, h = CELL_RECT[uvmode[5:]]
        c = ((x + w * 0.5) / TEX, 1.0 - (y + h * 0.5) / TEX)
        return [c] * len(pts), None
    n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
    if len(pts) > 3:
        n += (pts[2] - pts[0]).cross(pts[3] - pts[0])
    n.normalize()
    ax, ay, az = abs(n.x), abs(n.y), abs(n.z)
    if n.z < -0.6:
        return face_uvs(pts, "cell:black")[0], None
    if ax >= ay and ax >= az:
        t = "front" if n.x > 0 else "rear"
        return [tile_uv(t, abs(p.y), p.z) for p in pts], t
    if az >= ay:
        return [tile_uv("top", p.x, abs(p.y)) for p in pts], "top"
    if n.y < 0:  # an inward-facing side wall (sail inside, recess): flat paint
        return face_uvs(pts, "cell:paint")[0], None
    return [tile_uv("side", p.x, p.z) for p in pts], "side"


def world_grid(name):
    """World coordinates of every supersampled texel of a tile."""
    (u0, v0, w, h), (a0, a1), (b0, b1) = TILE[name]
    us = (np.arange(w * SS) + 0.5) / SS
    vs = (np.arange(h * SS) + 0.5) / SS
    U, V = np.meshgrid(us, vs)
    A = a0 + U / w * (a1 - a0)
    if name in ("top", "engine"):
        B = b0 + V / h * (b1 - b0)
    else:
        B = b0 + (1 - V / h) * (b1 - b0)
    return A, B


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


def grain(shape, seed, scale=1.0):
    """Deterministic value noise in 0..1 - vinyl, plate text."""
    rng = np.random.RandomState(seed)
    return rng.rand(*shape) * scale


def paint_atlas():
    """RGB (0..255 floats) per supersampled texel, plus a mask of where the
    baked occlusion may darken (the flat cells stay flat)."""
    rgb = np.zeros((TEX * SS, TEX * SS, 3), np.float32)
    rgb[:] = PAINT
    aomask = np.ones((TEX * SS, TEX * SS), np.float32)

    def view(name):
        (u0, v0, w, h) = TILE[name][0]
        return rgb[v0 * SS:(v0 + h) * SS, u0 * SS:(u0 + w) * SS]

    def flat(name, mask):
        """Keep the baked occlusion off these texels: paint only the far model
        samples (this model covers them with its own glass and lamp parts),
        which the AO bake never reaches and would leave at its floor."""
        (u0, v0, w, h) = TILE[name][0]
        t = aomask[v0 * SS:(v0 + h) * SS, u0 * SS:(u0 + w) * SS]
        t[mask] = 0.0

    def glass(name, mask, sky, streak):
        """The far model's windows, painted where this model's glass parts
        cover the tile: a dark tint with the sky's light towards the top and a
        soft diagonal reflection streak. sky, streak: 0..1 per texel."""
        c = np.asarray(GLASS_C, np.float32) * (0.85 + 1.25 * sky[..., None])
        c = c + np.asarray((60, 66, 72), np.float32) * streak[..., None]
        tv = view(name)
        m = mask.astype(np.float32)[..., None]
        tv[:] = tv * (1 - m) + np.clip(c, 0, 255) * m
        flat(name, mask)

    def fill(name, mask, col, a=1.0):
        t = view(name)
        m = (mask.astype(np.float32) * a)[..., None]
        c = np.asarray(col, np.float32)
        t[:] = t * (1 - m) + c * m

    def mul(name, mask, f):
        t = view(name)
        m = mask.astype(np.float32)[..., None]
        f = np.asarray(f, np.float32)
        if f.ndim == 2:
            f = f[..., None]
        t[:] = t * (1 - m) + t * f * m

    def chrome(name, mask, t):
        c = chrome_ramp(t)
        tv = view(name)
        m = mask.astype(np.float32)[..., None]
        tv[:] = tv * (1 - m) + c * m

    def line(A, c, wdt):
        return np.abs(A - c) < wdt

    def box(A, B, a0, a1, b0, b1):
        return (A > a0) & (A < a1) & (B > b0) & (B < b1)

    # ---- side: x, z ----
    X, Z = world_grid("side")
    bz = np.vectorize(belt)(X)
    rz = np.vectorize(rocker_z)(X)
    # the flank's light: brighter toward the crease, falling off to the rocker
    mul("side", Z < bz, 0.80 + 0.22 * smooth(rz, bz, Z))
    # the crease: a bright line on its lip, a soft shadow band tucked under it
    mul("side", (Z < bz - 0.004) & (Z > bz - 0.016), 1.16)
    mul("side", (Z < bz - 0.016) & (Z > bz - 0.07), 0.86 + 0.14 * smooth(bz - 0.07, bz - 0.016, Z))
    # the fender top above the crease on the side view (front and rear): lit
    mul("side", (Z > bz) & ((X > XWB) | (X < XSE)), 1.08)
    # the door scallops: long tapering recesses, a hard shadow under the top
    # lip, then the recess brightening back towards its bottom edge
    for (xfull, xtip, zc, hmax) in ((0.46, -0.30, 0.715, 0.075), (-1.04, -0.36, 0.555, 0.07)):
        t = np.clip((X - xfull) / (xtip - xfull), 0, 1)
        inside = (X >= min(xfull, xtip)) & (X <= max(xfull, xtip))
        h = hmax * (1 - t) ** 0.6
        top = zc + h * 0.5
        rec = inside & (np.abs(Z - zc) < h * 0.5)
        mul("side", rec, 0.70 + 0.28 * smooth(top, zc - h * 0.5, Z))
        mul("side", inside & (Z >= top) & (Z < top + 0.008), 1.18)
    # wheel arches: a shadowed lip round the opening
    for ax in (AXF, AXR):
        r = np.hypot(X - ax, Z - HUB_Z)
        mul("side", (r > ARCH_R) & (r < ARCH_R + 0.05), 0.55 + 0.45 * smooth(ARCH_R, ARCH_R + 0.05, r))
    # rocker: the tuck-under reads dark
    mul("side", Z < rz + 0.10, 0.62 + 0.38 * smooth(rz, rz + 0.10, Z))
    # shut lines: a dark gap with a lit edge beside it
    for xg in (0.52, -0.74):
        g = (Z > rz + 0.06) & (Z < bz + 0.03)
        mul("side", g & line(X, xg, 0.006), 0.30)
        mul("side", g & line(X, xg + 0.013, 0.005), 1.12)
    mul("side", (X > -0.74) & (X < 0.52) & line(Z, rz + 0.065, 0.005), 0.4)
    # door handle: chrome bar over a dark pocket, and the lock
    hz = bz - 0.075
    mul("side", box(X, Z, -0.715, -0.545, hz - 0.028, hz + 0.018), 0.45)
    chrome("side", box(X, Z, -0.705, -0.555, hz - 0.012, hz + 0.012), (Z - (hz + 0.012)) / -0.024)
    lk = np.hypot(X + 0.50, Z - hz)
    chrome("side", lk < 0.013, 0.3 + (lk / 0.013) * 0.4)
    fill("side", lk < 0.005, BLACK)
    # side markers in chrome bezels
    for (x0, x1, z0, z1, col) in ((2.10, 2.27, 0.635, 0.695, AMBER), (-2.73, -2.57, 0.655, 0.715, RED)):
        chrome("side", box(X, Z, x0 - 0.01, x1 + 0.01, z0 - 0.01, z1 + 0.01), (z1 + 0.01 - Z) / (z1 - z0 + 0.02))
        fill("side", box(X, Z, x0, x1, z0, z1), col)
        fill("side", box(X, Z, x0, x1, z1 - 0.018, z1), (255, 230, 170) if col == AMBER else (255, 120, 100), 0.55)
    # fender badge behind the front wheel: a chrome plate with dark "letters"
    bx0, bx1, bz0, bz1 = 0.80, 1.00, 0.735, 0.775
    chrome("side", box(X, Z, bx0, bx1, bz0, bz1), (bz1 - Z) / (bz1 - bz0))
    for k in range(4):
        xa = bx0 + 0.02 + k * 0.045
        fill("side", box(X, Z, xa, xa + 0.028, bz0 + 0.009, bz1 - 0.009), BLACK, 0.85)
    # the race-style fuel filler
    r = np.hypot(X + 2.02, Z - 0.895)
    chrome("side", r < 0.042, 0.15 + (r / 0.042) * 0.7)
    fill("side", (r < 0.026), (60, 62, 66))
    fill("side", (r < 0.026) & (line(X, -2.02, 0.004) | line(Z, 0.895, 0.004)), (200, 202, 208))
    # bumblebee stripe round the tail: two black bands, a fine lit edge
    for (x0, x1) in ((-2.47, -2.37), (-2.345, -2.30)):
        st = (X > x0) & (X < x1) & (Z > 0.62)
        fill("side", st, BLACK)
        mul("side", st & (Z > bz - 0.012) & (Z < bz - 0.002), 3.0)
    # vinyl roof down the sails, grained, with a chrome edge on the paint
    if VINYL:
        sail = (X < XQ + 0.02) & (X > XSE - 0.02) & (Z > bz + 0.05)
        fill("side", sail, VINYL_C)
        mul("side", sail, 0.85 + 0.3 * grain(X.shape, 11))
        chrome("side", (X < XQ + 0.02) & (X > XSE - 0.04) & line(Z, bz + 0.05, 0.011),
               0.5 + (Z - (bz + 0.05)) / -0.022)
    # sill chrome under the side glass
    chrome("side", (X < XWB + 0.02) & (X > XQ - 0.02) & line(Z, bz + 0.03, 0.011),
           0.5 + (Z - (bz + 0.03)) / -0.022)
    # the FAR model's side glass (make-ravager-far.py). This model's windows
    # are a material of their own, so it never samples these texels. Every
    # painted-for-the-far-model area stays KEEP_OUT metres inside the edges
    # this model's own faces meet: with Closest filtering a face samples the
    # texel just past its edge, and a 1-texel line of window showed on the
    # roof edge until they did.
    rz8 = np.vectorize(lambda x: rail(x)[1])(X)
    win = (X > XQ + KEEP_OUT) & (X < XWB - KEEP_OUT) & (Z > bz + 0.05) & (Z < rz8)
    streak = (np.exp(-((X - 0.9 * (Z - 1.1) + 0.30) / 0.07) ** 2)
              + 0.6 * np.exp(-((X - 0.9 * (Z - 1.1) + 0.55) / 0.035) ** 2))
    glass("side", win, smooth(bz + 0.04, rz8, Z), streak)
    fill("side", win & (Z > rz8 - 0.012), (14, 16, 19))   # the top seal

    # ---- top: x, |y| ----
    X, Y = world_grid("top")
    w = np.vectorize(half_w)(X)
    # the fender tops: a lit ridge along the crease
    mul("top", (Y > w - 0.035) & (Y < w - 0.012), 1.14)
    # sill chrome and drip rail
    chrome("top", (X < XWB + 0.03) & (X > XQ) & (Y > w - 0.13) & (Y < w - 0.105),
           (Y - (w - 0.13)) / 0.025)
    chrome("top", (X < XWT + 0.02) & (X > XRE) & (Y > 0.60) & (Y < 0.63), (Y - 0.60) / 0.03)
    if VINYL:
        roof = (X < XWT - 0.01) & (X > XSE) & (Y < 0.62)
        vr = roof & ((X > XRE + 0.01) | (Y > 0.54))
        fill("top", vr, VINYL_C)
        mul("top", vr, 0.85 + 0.3 * grain(X.shape, 12))
        mul("top", vr & line(Y, 0.30, 0.005), 0.6)         # the two seams
        mul("top", (X < XWT - 0.01) & (X > XRE) & (Y < 0.62) & (Y > 0.585), 0.55)
    # the hood: a lit crown along the centre, shut lines with a lit edge
    mul("top", (X > XWB) & (Y < 0.05), 1.05)
    mul("top", line(X, 0.64, 0.005) & (Y < w - 0.26), 0.35)
    mul("top", line(X, 0.652, 0.004) & (Y < w - 0.26), 1.12)
    mul("top", line(Y, w - 0.25, 0.006) & (X > XWB) & (X < XF - 0.02), 0.4)
    # twin hood vents: chrome frames around black louvres
    for (x0, x1) in ((1.60, 1.90),):
        chrome("top", box(X, Y, x0 - 0.022, x1 + 0.022, 0.155, 0.445), (Y - 0.155) / 0.29)
        vent = box(X, Y, x0, x1, 0.178, 0.422)
        fill("top", vent, BLACK)
        for k in range(5):
            xa = x0 + 0.03 + k * 0.055
            fill("top", vent & (X > xa) & (X < xa + 0.018), (70, 70, 74))
    # the cowl and the wipers
    cowl = (X > XWB - 0.01) & (X < XWB + 0.07) & (Y < w - 0.26)
    fill("top", cowl, BLACK)
    for (y0, dx) in ((0.08, 0.03), (0.50, 0.03)):
        fill("top", cowl & line(X - (Y - y0) * 0.08, XWB + dx, 0.006) & (Y > y0) & (Y < y0 + 0.4),
             (90, 92, 96))
    # deck lid: shut lines, the lock, the bumblebee stripe
    mul("top", line(X, XRW - 0.1, 0.005) & (Y < 0.70), 0.35)
    mul("top", line(Y, 0.70, 0.005) & (X < XRW - 0.1) & (X > XR + 0.06), 0.4)
    lk = np.hypot(X - (XR + 0.13), Y)
    chrome("top", lk < 0.03, lk / 0.03)
    for (x0, x1) in ((-2.47, -2.37), (-2.345, -2.30)):
        fill("top", (X > x0) & (X < x1), BLACK)
    # the FAR model's windscreen and tunnel rear window (never sampled here:
    # this model's are glass parts)
    y9 = np.vectorize(lambda x: section(x)[9][0])(X)
    y10 = np.vectorize(lambda x: section(x)[10][0])(X)
    ws = (X > XWT + KEEP_OUT) & (X < XWB - KEEP_OUT) & (Y < y9 - KEEP_OUT)
    glass("top", ws, smooth(XWB, XWT, X), np.exp(-((X + 0.8 * Y - 0.42) / 0.06) ** 2))
    rw = (X > XRW + KEEP_OUT) & (X < XRE - KEEP_OUT) & (Y < y10 - KEEP_OUT)
    glass("top", rw, smooth(XRW, XRE, X), 0.7 * np.exp(-((X - 0.6 * Y + 1.22) / 0.05) ** 2))

    # ---- front: |y|, z ----
    Y, Z = world_grid("front")
    sur = (Y < 0.875) & (Z > 0.51) & (Z < 0.85)
    chrome("front", sur, (0.85 - Z) / 0.34)
    grille = (Y < 0.845) & (Z > 0.535) & (Z < 0.825)
    fill("front", grille, (12, 12, 14))
    # the fine horizontal bars, each lit on top and dark below
    for k in range(9):
        zb = 0.555 + k * 0.03
        fill("front", grille & (Z > zb) & (Z < zb + 0.010), (150, 152, 158))
        fill("front", grille & (Z > zb + 0.010) & (Z < zb + 0.016), (60, 62, 66))
    chrome("front", grille & (Y < 0.02), (0.825 - Z) / 0.29)          # centre divider
    em = np.hypot(Y, Z - 0.68)
    chrome("front", em < 0.05, 0.2 + em / 0.05 * 0.6)                  # centre emblem
    fill("front", em < 0.03, RED)
    mul("front", grille & line(Y, 0.615, 0.007), 0.3)                  # lamp door split
    # the FAR model's headlamps, under this model's lamp fans: a lens with a
    # hot spot in a thin chrome bezel
    for yc in (0.50, 0.73):
        r = np.hypot(Y - yc, Z - 0.690)
        # r 0.048: inside the lamp fan's 0.061 even with the 1.2 cm parallax
        # of a three-quarter view
        chrome("front", r < 0.048, 0.25 + (r / 0.048) * 0.5)
        fill("front", r < 0.041, LAMP_C)
        fill("front", np.hypot(Y - yc + 0.01, Z - 0.70) < 0.016, (255, 255, 250))
        flat("front", r < 0.048)
    # valance: parking lamps in chrome, the plate, a dark lower lip
    chrome("front", box(Y, Z, 0.585, 0.795, 0.38, 0.44), (0.44 - Z) / 0.06)
    fill("front", box(Y, Z, 0.60, 0.78, 0.39, 0.43), AMBER)
    fill("front", box(Y, Z, 0.60, 0.78, 0.415, 0.43), (255, 225, 160), 0.6)
    fill("front", box(Y, Z, 0.0, 0.17, 0.30, 0.43), PLATE)
    chrome("front", box(Y, Z, 0.0, 0.175, 0.295, 0.435) & ~box(Y, Z, 0.0, 0.165, 0.305, 0.425), 0.4)
    for k in range(3):
        fill("front", box(Y, Z, 0.012 + k * 0.05, 0.045 + k * 0.05, 0.335, 0.395), (40, 38, 50))
    mul("front", Z < 0.36, 0.55 + 0.45 * smooth(0.2, 0.36, Z))

    # ---- rear: |y|, z ----
    Y, Z = world_grid("rear")
    chrome("rear", (Y < 0.865) & (Z > 0.695) & (Z < 0.885), (0.885 - Z) / 0.19)
    fill("rear", (Y < 0.845) & (Z > 0.715) & (Z < 0.865), (14, 14, 16))
    # the FAR model's tail lamps, exactly under this model's lamp quads. The
    # far model's lamps are ordinary lit paint while this model's are a
    # fullbright part, so they are painted brighter than RED to read the same
    # on the console (measured side by side in PCSX2)
    for (y0, y1) in ((0.14, 0.44), (0.50, 0.80)):
        lm = box(Y, Z, y0 + 0.022, y1 - 0.022, 0.735 + 0.02, 0.845 - 0.02)
        fill("rear", lm, (250, 46, 38))
        fill("rear", lm & (Z > 0.805), (255, 130, 110))
        flat("rear", lm)
    chrome("rear", (Y < 0.10) & (Z > 0.765) & (Z < 0.815), (0.815 - Z) / 0.05)   # centre badge
    # the plate in a chrome frame, with its lamp above
    chrome("rear", box(Y, Z, 0.0, 0.18, 0.555, 0.67), (0.67 - Z) / 0.115)
    fill("rear", box(Y, Z, 0.0, 0.165, 0.565, 0.655), PLATE)
    for k in range(3):
        fill("rear", box(Y, Z, 0.012 + k * 0.05, 0.045 + k * 0.05, 0.58, 0.64), (40, 38, 50))
    fill("rear", box(Y, Z, 0.0, 0.06, 0.672, 0.688), (250, 245, 225))
    # back-up lamps under the tail panel
    fill("rear", box(Y, Z, 0.42, 0.56, 0.60, 0.645), (235, 235, 228))
    chrome("rear", box(Y, Z, 0.41, 0.57, 0.59, 0.655) & ~box(Y, Z, 0.42, 0.56, 0.60, 0.645), 0.4)
    mul("rear", Z < 0.40, 0.5 + 0.5 * smooth(0.22, 0.40, Z))

    # ---- flat cells ----
    for name, (x, y, w_, h) in CELL_RECT.items():
        sl = (slice(y * SS, (y + h) * SS), slice(x * SS, (x + w_) * SS))
        aomask[sl] = 0.0
        col = {"black": BLACK, "dark": (10, 10, 11), "int": INT_C, "intdark": (15, 14, 14),
               "seat": SEAT, "paint": PAINT}.get(name)
        if name == "chrome":
            prof = chrome_ramp(np.linspace(0, 1, h * SS))
            rgb[sl] = prof[:, None, :]
        else:
            rgb[sl] = col
    # the FAR model's wheel face: tyre sidewall, a chrome dish with five dark
    # slots (the full wheel's pattern), a cap. Up in the image is up on the car.
    x0, y0, w_, h = WHEEL_RECT
    cx, cy, rr = WHEEL_DISC
    sl = (slice(y0 * SS, (y0 + h) * SS), slice(x0 * SS, (x0 + w_) * SS))
    py, px = np.mgrid[sl]
    px = (px + 0.5) / SS
    py = (py + 0.5) / SS
    r = np.hypot(px - cx, py - cy) / rr
    ang = np.arctan2(cy - py, px - cx)
    reg = np.empty(r.shape + (3,), np.float32)
    reg[:] = BLACK
    side = (r > 0.62) & (r < 1.0)
    reg[side] = np.asarray((24, 24, 27), np.float32) * (0.8 + 0.5 * smooth(1.0, 0.8, r[side]))[..., None]
    dish = r <= 0.62
    # brighter than the chrome ramp: the full wheel is drawn UNLIT (the wheel
    # batch has no lighting), the far model's wheels are lit like the body
    reg[dish] = 0.55 * chrome_ramp(0.5 - 0.5 * (cy - py[dish]) / (rr * 0.62)) + 0.45 * 250
    slot = dish & (r > 0.24) & (r < 0.55) & (np.mod(ang / (2 * np.pi) * 5 + 0.1, 1.0) < 0.33)
    reg[slot] = (14, 14, 16)
    cap = r < 0.2
    reg[cap] = chrome_ramp(0.2 + r[cap] * 2.0)
    rgb[sl] = reg
    aomask[sl] = 0.0

    # the three calls carkit's painter makes, answered by this atlas
    canvas = types.SimpleNamespace(grid=world_grid, view=view, flat=flat)
    _kit().Atlas.paint_engine(canvas, BAY, ENGINE_ACCENT)
    return rgb, aomask


def finish(rgb, aomask, ao):
    """Supersampled paint + the baked occlusion -> the 8-bit RGB image."""
    img = rgb.reshape(TEX, SS, TEX, SS, 3).mean(axis=(1, 3))
    m = aomask.reshape(TEX, SS, TEX, SS).mean(axis=(1, 3))
    occ = 1.0 - m * (1.0 - ao)
    img = img * occ[..., None]
    return np.clip(img + 0.5, 0, 255).astype(np.uint8)


# --- wheel ------------------------------------------------------------------------
def build_wheel(g):
    """Centred on the hub, axle along Y. SYMMETRIC: the runtime draws all four
    wheels from one unmirrored mesh (renderVehicleWheels), so a dish modelled
    on one face only would show the tyre's open back on one side of the car.
    Each face is a chrome dish with five dark slots (every third segment) and
    a raised cap; 15 segments, 160 triangles."""
    seg = 15
    R, W = R_WHEEL, W_WHEEL * 0.5
    ring = [(math.cos(2 * math.pi * s / seg + 0.21), math.sin(2 * math.pi * s / seg + 0.21))
            for s in range(seg)]
    P = lambda r, c, s, y: (r * c, y, r * s)
    rs = 0.215
    prof = [(rs, -W * 0.90), (R, -W * 0.62), (R, W * 0.62), (rs, W * 0.90)]
    for s in range(seg):
        (c0, s0), (c1, s1) = ring[s], ring[(s + 1) % seg]
        for k in range(len(prof) - 1):
            (r0, y0), (r1, y1) = prof[k], prof[k + 1]
            q = [P(r0, c0, s0, y0), P(r0, c1, s1, y0), P(r1, c1, s1, y1), P(r1, c0, s0, y1)]
            mid = Vector(P(0.5 * (r0 + r1), 0.5 * (c0 + c1), 0.5 * (s0 + s1), 0.5 * (y0 + y1)))
            g.face(q, "tyre rubber", mirror=False, out=(mid.x, 1.5 * (y0 + y1) / W, mid.z))
    for side in (1, -1):
        yl, yh, rh = side * W * 0.90, side * W * 0.50, 0.065
        for s in range(seg):
            (c0, s0), (c1, s1) = ring[s], ring[(s + 1) % seg]
            mat = "rim barrel" if s % 3 == 2 else "rim chrome"
            g.face([P(rs, c0, s0, yl), P(rs, c1, s1, yl), P(rh, c1, s1, yh), P(rh, c0, s0, yh)],
                   mat, mirror=False, out=(0, side, 0))
        cap = [(rh * math.cos(2 * math.pi * k / 5), yh, rh * math.sin(2 * math.pi * k / 5)) for k in range(5)]
        for k in range(5):
            g.face([(0, yh + side * 0.025, 0), cap[k], cap[(k + 1) % 5]], "rim chrome", mirror=False,
                   out=(0, side, 0))


# --- Blender plumbing -------------------------------------------------------------
MAT_DEF = {
    "body paint": None,  # textured
    "glass window": (0.10, 0.13, 0.16, 1.0),
    "headlights": (0.92, 0.91, 0.84, 1.0),
    "rear lights": (0.72, 0.05, 0.05, 1.0),
    "tyre rubber": (0.035, 0.035, 0.04, 1.0),
    "rim chrome": (0.78, 0.79, 0.81, 1.0),
    "rim barrel": (0.05, 0.05, 0.055, 1.0),
}


def make_material(name, image=None, glass_alpha=None):
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
        bsdf.inputs["Base Color"].default_value = MAT_DEF[name]
    if name in ("headlights", "rear lights"):
        bsdf.inputs["Emission Color"].default_value = MAT_DEF[name]
        bsdf.inputs["Emission Strength"].default_value = 0.08
    if glass_alpha is not None:
        bsdf.inputs["Alpha"].default_value = glass_alpha
        bsdf.inputs["Roughness"].default_value = 0.1
        try:
            m.surface_render_method = "BLENDED"
        except Exception:
            pass
    return m


def to_object(g, name, mats):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    order = list(mats)
    for pts, mat, uvmode, mirror in g.faces:
        uvs = face_uvs(pts, uvmode)[0] if mat == "body paint" else [(0.0, 0.0)] * len(pts)
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
    # drop degenerate faces left by the arch lift
    bad = [f for f in bm.faces if f.calc_area() < 1e-7]
    bmesh.ops.delete(bm, geom=bad, context="FACES")
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in order:
        me.materials.append(mats[m])
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(38))
    return ob


def bake_ao(objs, image):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 160
    body = objs[0]
    mat = body.data.materials[0]
    nt = mat.node_tree
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


def render_previews(outdir, objs):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 960, 600
    sc.render.film_transparent = False
    world = sc.world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.42, 0.50, 0.58, 1)
    bg.inputs["Strength"].default_value = 0.9
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.2
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    sc.collection.objects.link(sun)
    # ground
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0))
    gnd = bpy.context.active_object
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
    gnd.data.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    s = SCALE
    views = {
        "front34": ((5.2, 4.4, 1.9), (0.1, 0, 0.55), 40),
        "rear34": ((-5.6, 4.0, 2.2), (-0.2, 0, 0.55), 40),
        "side": ((0, 8.5, 0.75), (-0.2, 0, 0.62), 36),
        "chase": ((-7.4, 1.4, 2.4), (0.8, 0, 0.6), 42),
        "top": ((1.5, 2.2, 7.0), (-0.1, 0, 0.5), 38),
        "cabin": ((2.2, 3.0, 2.6), (-0.4, 0.2, 0.8), 36),
    }
    for name, (loc, tgt, fov) in views.items():
        cam.location = Vector(loc) * s
        look_at(cam, Vector(tgt) * s)
        cam.data.angle = math.radians(fov)
        sc.render.filepath = os.path.join(outdir, f"ravager-{name}.png")
        bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out, preview, do_bake, glass_alpha = OUT, None, True, 0.42
    i = 0
    while i < len(argv):
        if argv[i] == "--out":
            out = argv[i + 1]; i += 1
        elif argv[i] == "--preview":
            preview = argv[i + 1]; i += 1
        elif argv[i] == "--no-bake":
            do_bake = False
        elif argv[i] == "--opaque-glass":
            glass_alpha = None
        i += 1

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.world = bpy.data.worlds.new("world")

    img = bpy.data.images.new("ravager-paint", TEX, TEX, alpha=False)
    mats = {n: make_material(n, img if n == "body paint" else None,
                             glass_alpha if n == "glass window" else None) for n in MAT_DEF}

    g = Geo()
    build_body(g)
    build_trim(g)
    build_lamps(g)
    build_interior(g)

    class KitGeo:  # carkit's face() takes the material as a keyword
        def face(self, pts, mat="body paint", uv="auto", mirror=True, out=None):
            g.face(pts, mat, uv, mirror, out)
    _kit().build_engine_bay(KitGeo(), sys.modules[__name__], BAY)
    body = to_object(g, "body", {k: mats[k] for k in ("body paint", "glass window", "headlights", "rear lights")})

    wg = Geo()
    build_wheel(wg)
    wheel_mesh = to_object(wg, "wheel", {k: mats[k] for k in ("tyre rubber", "rim chrome", "rim barrel")}).data
    bpy.data.objects.remove(bpy.data.objects["wheel"])
    names = {(True, 1): "wheel front left", (True, -1): "wheel front right",
             (False, 1): "wheel rear left", (False, -1): "wheel rear right"}
    wheels = []
    for front, wx in ((True, AXF), (False, AXR)):
        for side in (1, -1):
            o = bpy.data.objects.new(names[(front, side)], wheel_mesh)
            o.location = (wx, side * TRACK * 0.5, R_WHEEL)
            if side < 0:
                o.rotation_euler = (0, 0, math.pi)
            sc.collection.objects.link(o)
            wheels.append(o)

    rgb, aomask = paint_atlas()
    ao = np.ones((TEX, TEX), np.float32)
    if do_bake:
        aoimg = bpy.data.images.new("ao", TEX, TEX, alpha=False, float_buffer=True)
        bake_ao([body] + wheels, aoimg)
        px = np.array(aoimg.pixels[:], np.float32).reshape(TEX, TEX, 4)[::-1, :, 0]
        # a 3x3 box blur: sampling noise near a ramp threshold turns into
        # single-texel speckle once quantised
        pp = np.pad(px, 1, mode="edge")
        px = sum(pp[dy:dy + TEX, dx:dx + TEX] for dy in range(3) for dx in range(3)) / 9.0
        ao = np.clip(0.30 + 0.70 * px, 0, 1) ** 0.85
    texels = finish(rgb, aomask, ao)
    pix = np.ones((TEX, TEX, 4), np.float32)
    pix[..., :3] = texels[::-1] / 255.0
    img.pixels[:] = pix.ravel()
    img.pack()
    ncol = len({tuple(c) for c in texels.reshape(-1, 3)})

    # scale for the district and export
    body.data.transform(Matrix.Scale(SCALE, 4))
    wheel_mesh.transform(Matrix.Scale(SCALE, 4))
    for o in wheels:
        o.location = o.location * SCALE
    bpy.context.view_layer.update()

    tris = lambda o: sum(len(p.vertices) - 2 for p in o.data.polygons)
    bytype = {}
    for p in body.data.polygons:
        k = body.data.materials[p.material_index].name
        bytype[k] = bytype.get(k, 0) + len(p.vertices) - 2
    print(f"RAVAGER body {tris(body)} tris {bytype}; wheel {tris(wheels[0])} tris; "
          f"texture {TEX}x{TEX} {ncol} colours")

    # the glass exports opaque (the bake has no translucent part yet); the
    # preview renders it translucent to show where this is heading
    if preview:
        os.makedirs(preview, exist_ok=True)
        img.save_render(os.path.join(preview, "ravager-atlas.png"))
        render_previews(preview, [body] + wheels)
    gm = mats["glass window"]
    gm.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 1.0
    for o in bpy.context.scene.objects:
        o.select_set(o in [body] + wheels)
    bpy.ops.export_scene.gltf(filepath=os.path.abspath(out), export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_materials="EXPORT",
                              export_image_format="AUTO")
    print("RAVAGER wrote", os.path.abspath(out))


# make-ravager-far.py imports this file for its design data and atlas rules.
if __name__ == "__main__":
    main()

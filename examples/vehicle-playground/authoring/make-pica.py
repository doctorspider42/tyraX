"""Build the Pica Turbo: a boxy mid-80s three-door hot hatch for the PS2 path.

Run through Blender, not plain Python:

    blender -b --factory-startup --python make-pica.py -- [--out GLB]
            [--preview DIR] [--no-bake] [--opaque-glass]

Writes res/models/pica.glb and, with --preview, Eevee renders (front34, rear34,
side, chase, top, cabin, a contact sheet and the atlas) into DIR. Run
make-pica-far.py after it: the far model wears this model's texture.

An original design in the spirit of the era's hot hatches, not a copy of one:
a short flat hood, an upright windscreen, a long three-door cabin with a
blacked-out B-pillar, a thick C-pillar, a steep hatch with a roof spoiler,
bolted-on wheel-arch flares, black plastic bumpers and side cladding with a red
pinstripe, twin square headlamps, a sunroof and "pepper-pot" alloys.

The body is the same kind of loft as the Ravager (make-ravager.py): twelve
character lines per half section - keel, floor edge, rocker, tuck-under,
flank, belt crease, shoulder, sill, greenhouse rail, rail inner edge, glass
edge, crown - sampled at ~32 stations, mirrored with the same UVs, painted in
world coordinates into one 256x256 atlas and AO-baked (carkit.py has the
shared plumbing). The arch flares are an offset on the three lines below the
belt (rocker, tuck-under, flank), so they bulge out under the crease the way a
bolted-on flare does, and the texture paints them black.

It is strip-friendly by construction: continuous quad loops along the whole
body, smooth shading with sharp edges only past 38 degrees (real creases), UVs
that are a function of position within a tile so neighbouring corners weld.

Units are metres, Blender axes: +X forward, +Y to the car's left, +Z up.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import carkit as K  # noqa: E402
from carkit import lerp, pchip, smooth  # noqa: E402

NAME = "pica"
SCALE = 0.92                 # the same scale as the Ravager: cars share one world
XF, XR = 1.84, -1.92         # front / rear faces (front overhang 0.62 < rear 0.70)
AXF, AXR = 1.22, -1.22       # wheelbase 2.44
R_WHEEL, W_WHEEL = 0.285, 0.195
TRACK = 1.42
ARCH_R = 0.335
HUB_Z = R_WHEEL
ARCH_INNER_Y = 0.52
XWB, XWT = 0.70, 0.00        # windscreen base / top
XRE, XRW = -1.38, -1.70      # roof end / hatch glass base
XQ = -0.98                   # rear edge of the quarter glass
BP0, BP1 = -0.42, -0.34      # the blacked-out B-pillar
ZMID = 0.60
LENGTH, HEIGHT = XF - XR, 1.36
FLARE = 0.026
GLASS_ALPHA = 0.6            # the preview's; the definition's Glass opacity is 0.6 too

half_w = pchip([(XR, .772), (XR + 0.16, .795), (-1.2, .800), (0.0, .798), (1.2, .800),
                (XF - 0.16, .792), (XF, .765)])
belt = pchip([(XR, .905), (-1.2, .898), (0.0, .886), (XWB, .872), (1.2, .838), (XF, .792)])
rocker_z = pchip([(XR, .30), (-1.6, .27), (-0.9, .235), (0.9, .235), (1.6, .27), (XF, .29)])
hood_z = pchip([(XWB, .925), (1.2, .898), (XF, .832)])
roof_z = pchip([(XWT, 1.342), (-0.8, 1.356), (XRE, 1.346)])
deck_z = pchip([(XR, .948), (XRW, .968)])


def flare(x):
    """Bolted-on arch flares: a bump on the lower lines around each axle."""
    f = 0.0
    for ax in (AXF, AXR):
        d = abs(x - ax)
        f = max(f, 1.0 - float(smooth(ARCH_R + 0.02, ARCH_R + 0.16, d)))
    return FLARE * f


def center_z(x):
    if x >= XWB:
        return hood_z(x)
    if x >= XWT:
        t = (XWB - x) / (XWB - XWT)
        return lerp(hood_z(XWB) + 0.002, roof_z(XWT), t) + 0.010 * math.sin(math.pi * t)
    if x >= XRE:
        return roof_z(x)
    if x >= XRW:  # the hatch glass, flat and steep (53 degrees)
        t = (XRE - x) / (XRE - XRW)
        return lerp(roof_z(XRE), deck_z(XRW), t)
    return deck_z(x)


def rail(x):
    """Greenhouse outer line: hood edge, A-pillar, roof rail, D-pillar, lip."""
    w, b = half_w(x), belt(x)
    if x >= XWB:
        return w - 0.11, max(hood_z(x) - 0.008, b + 0.036)
    if x >= XWT:
        t = (XWB - x) / (XWB - XWT)
        y0, z0 = half_w(XWB) - 0.11, max(hood_z(XWB) - 0.004, belt(XWB) + 0.036)
        return lerp(y0, 0.622, t), lerp(z0, 1.322, t)
    if x >= XRE:
        t = (XWT - x) / (XWT - XRE)
        return lerp(0.622, 0.628, t), lerp(1.322, 1.318, t)
    if x >= XRW:
        t = (XRE - x) / (XRE - XRW)
        return lerp(0.628, half_w(XRW) - 0.10, t ** 0.8), lerp(1.318, belt(XRW) + 0.05, t)
    return w - 0.10, deck_z(x) - 0.005


def section(x):
    """Half cross-section at station x: 12 (y, z) points, keel to crown."""
    w, b, zr, fl = half_w(x), belt(x), rocker_z(x), flare(x)
    p = [
        (0.0, zr - 0.012),                 # 0 keel
        (w - 0.18, zr - 0.010),            # 1 floor edge
        (w - 0.07 + fl * 0.7, zr),         # 2 rocker bottom
        (w - 0.024 + fl, zr + 0.09),       # 3 tuck-under
        (w + fl, 0.56),                    # 4 flank
        (w - 0.004, b),                    # 5 belt crease
        (w - 0.022, b + 0.018),            # 6 shoulder
        (w - 0.075, b + 0.030),            # 7 sill
    ]
    ry, rz = rail(x)
    p.append((ry, rz))                     # 8 greenhouse rail
    if x >= XWB:
        p.append((ry - 0.06, hood_z(x) - 0.003))
    elif x >= XRE:
        p.append((ry - 0.06, rz + (0.014 if x < XWT else 0.008)))
    elif x >= XRW:
        p.append((ry - 0.055, rz + 0.006))
    else:
        p.append((ry - 0.06, deck_z(x) - 0.002))
    cz = center_z(x)
    dz = -0.008 if XWT <= x < XWB else (-0.004 if XRE <= x < XWT else 0.0)
    p.append((0.34, cz + dz))              # 10 glass edge / crown shoulder
    p.append((0.0, cz))                    # 11 crown
    return K.lift_arches(sys.modules[__name__], p, x)


def stations():
    xs = {XF, XF - 0.16, XR, XR + 0.16, XWB, XWB + 0.12, XWT, XRE, XRW, XQ, BP0, BP1,
          -0.65, 0.35, -1.54}
    for ax in (AXF, AXR):
        for f in (-1, -.92, -.7, -.38, 0, .38, .7, .92, 1):
            xs.add(ax + f * ARCH_R)
    return K.merge_stations(xs)


def glass_strip(k, xm):
    if k == 7 and XQ < xm < XWB and not (BP0 < xm < BP1):
        return True
    if k in (9, 10) and (XWT < xm < XWB or XRW < xm < XRE):
        return True
    return False


# recessed end caps: (x, sign, depth, centre z, y inset, z inset)
CAPS = [(XF, 1, 0.040, 0.55, 0.95, 0.90), (XR, -1, 0.035, 0.60, 0.95, 0.90)]

# --- colours ------------------------------------------------------------------
PAINT = (200, 26, 24)        # signal red
BLACK = (20, 20, 22)
PLASTIC = (30, 30, 33)
STRIPE = (228, 40, 34)
AMBER = (240, 150, 28)
LAMP_C = (234, 232, 214)
GLASS_C = (26, 33, 41)
KO = K.KEEP_OUT
COLOURS = {
    "paint": PAINT,
    "glass window": (0.10, 0.13, 0.16, 1.0),
    "headlights": (0.92, 0.91, 0.84, 1.0),
    "rear lights": (0.72, 0.05, 0.05, 1.0),
    "tyre rubber": (0.035, 0.035, 0.04, 1.0),
    "rim chrome": (0.80, 0.81, 0.83, 1.0),
    "rim barrel": (0.05, 0.05, 0.055, 1.0),
}


# Under the bonnet (carkit.build_engine_bay): a transverse four with red cam
# covers, the floor at hub height, well below the skin.
BAYS = [{"x0": XWB - 0.03, "x1": XF - 0.20, "w": ARCH_INNER_Y - 0.04, "z": HUB_Z + 0.12,
         "engine": True, "radiator": True}]
ENGINE_ACCENT = (160, 30, 26)


def make_atlas():
    # The cabin colours are dark on purpose: a lost door or window shows it.
    return K.Atlas(XR, XF, side_z=(0.14, 1.40), front_z=(0.20, 0.96), rear_z=(0.20, 1.40), half_w=0.86,
                   cells=["black", "dark", "plastic", "int", "intdark", "seat", "paint"],
                   ramps={"plastic": K.plastic_ramp},
                   colours={"black": BLACK, "dark": (10, 10, 11), "int": (28, 26, 24),
                            "intdark": (15, 14, 14), "seat": (46, 28, 31), "paint": PAINT},
                   bay=BAYS[0])


# --- trim, lamps, interior, wheel ----------------------------------------------
def build_trim(g):
    s2 = math.sqrt(0.5)
    wf, wr = half_w(XF - 0.1), half_w(XR + 0.1)
    K.bar(g, [(XF + 0.06, 0.0, (1, 0)), (XF + 0.055, 0.58, (1, 0)), (XF + 0.02, wf - 0.02, (s2, s2)),
              (XF - 0.22, wf + 0.016, (0, 1))], 0.30, 0.47, 0.07, cell="plastic", under="black")
    K.bar(g, [(XR - 0.06, 0.0, (-1, 0)), (XR - 0.055, 0.58, (-1, 0)), (XR - 0.02, wr - 0.02, (-s2, s2)),
              (XR + 0.26, wr + 0.016, (0, 1))], 0.30, 0.49, 0.07, cell="plastic", under="black")
    # door mirrors, both sides: a black plastic pod on a short stalk
    xm = 0.60
    w, b = half_w(xm), belt(xm)
    g.box((xm, w + 0.075, b + 0.095), (0.045, 0.055, 0.042), uv="cell:plastic", skip=("-y",))
    g.box((xm + 0.01, w + 0.012, b + 0.05), (0.03, 0.012, 0.02), uv="cell:plastic", skip=("-y",))
    # the roof spoiler over the hatch
    g.box((XRE - 0.06, 0.30, 1.345), (0.10, 0.30, 0.016), uv="cell:plastic", skip=("-y",))
    # exhaust tip
    g.box((XR + 0.04, 0.46, 0.27), (0.08, 0.03, 0.025), uv="cell:dark", mirror=False, skip=("+x",))


def build_lamps(g):
    xh = XF - CAPS[0][2] + 0.012
    for (y0, y1) in ((0.40, 0.54), (0.555, 0.695)):     # twin square headlamps
        K.rect_lamp(g, "headlights", xh, y0, y1, 0.625, 0.755, 1)
    xt = XR + CAPS[1][2] - 0.012
    K.rect_lamp(g, "rear lights", xt, 0.40, 0.735, 0.655, 0.785, -1)


INTERIOR = {"inset": 0.12, "sill": 0.075, "floor": 0.36, "rear": -1.30, "dash": 0.60,
            "dash_back": 0.38, "dash_z": 0.86, "shelf": (-1.46, 0.93), "bench": (-0.86, 0.46),
            "seat": (0.04, 0.37, 0.46, 0.55), "wheel": (0.30, 0.37, 0.80), "wheel_r": 0.17}


def build_wheel(g):
    """14 segments, seven dark "pepper-pot" holes: 150 triangles."""
    K.build_wheel(g, R_WHEEL, W_WHEEL, 14, lambda s: "rim barrel" if s % 2 else "rim chrome",
                  rs=0.66, hub=0.21, phase=0.0)


def wheel_face(r, ang, up):
    """The far model's wheel face: sidewall, a silver dish with seven holes."""
    reg = np.empty(r.shape + (3,), np.float32)
    reg[:] = BLACK
    side = (r > 0.66) & (r < 1.0)
    reg[side] = np.asarray((24, 24, 27), np.float32) * (0.8 + 0.5 * smooth(1.0, 0.8, r[side]))[..., None]
    dish = r <= 0.66
    reg[dish] = 0.55 * K.chrome_ramp(0.5 - 0.5 * up[dish] / 0.66) + 0.45 * 250
    for k in range(7):
        a = 2 * math.pi * (k + 0.5) / 7
        hole = np.hypot(r * np.cos(ang) - 0.44 * math.cos(a), r * np.sin(ang) - 0.44 * math.sin(a)) < 0.12
        reg[dish & hole] = (14, 14, 16)
    cap = r < 0.2
    reg[cap] = K.chrome_ramp(0.2 + r[cap] * 2.0)
    return reg


# --- paint ------------------------------------------------------------------------
def paint(A):
    line = lambda a, c, wdt: np.abs(a - c) < wdt
    box = lambda a, b, a0, a1, b0, b1: (a > a0) & (a < a1) & (b > b0) & (b < b1)

    # ---- side: x, z ----
    X, Z = A.grid("side")
    bz = K.vec(belt, X)
    rz = K.vec(rocker_z, X)
    A.mul("side", Z < bz, 0.80 + 0.22 * smooth(rz, bz, Z))
    A.mul("side", (Z < bz - 0.003) & (Z > bz - 0.013), 1.16)
    A.mul("side", (Z < bz - 0.013) & (Z > bz - 0.06), 0.87 + 0.13 * smooth(bz - 0.06, bz - 0.013, Z))
    A.mul("side", (Z > bz) & (Z < bz + 0.035), 1.07)
    # the side skirt and the rubbing strip, black plastic, the strip with a
    # red pinstripe and a lit top edge
    xa, xb = AXR + ARCH_R + 0.06, AXF - ARCH_R - 0.06
    between = (X > xa) & (X < xb)
    A.fill("side", between & (Z < rz + 0.07), PLASTIC)
    A.mul("side", between & line(Z, rz + 0.068, 0.004), 1.8)
    rs = between & (Z > 0.50) & (Z < 0.565)
    A.fill("side", rs, PLASTIC)
    A.mul("side", rs & (Z > 0.556), 2.0)
    A.fill("side", between & (Z > 0.527) & (Z < 0.537), STRIPE)
    # the bolted-on flares: black plastic round each arch, a lit outer lip
    for ax in (AXF, AXR):
        r = np.hypot(X - ax, Z - HUB_Z)
        fl = (r > ARCH_R) & (r < ARCH_R + 0.07) & (Z < bz - 0.02)
        A.fill("side", fl, PLASTIC)
        A.mul("side", fl & (r > ARCH_R + 0.058), 1.9)
        A.mul("side", fl & (r < ARCH_R + 0.014), 0.55)
    # rocker tuck-under reads dark
    A.mul("side", Z < rz + 0.05, 0.62 + 0.38 * smooth(rz, rz + 0.05, Z))
    # the long door: shut lines (dark gap, lit edge), the handle
    for xg in (XWB - 0.01, BP0 - 0.01):
        gz = (Z > rz + 0.07) & (Z < bz + 0.03)
        A.mul("side", gz & line(X, xg, 0.005), 0.30)
        A.mul("side", gz & line(X, xg + 0.012, 0.004), 1.12)
    hz = bz - 0.065
    A.fill("side", box(X, Z, BP0 + 0.05, BP0 + 0.17, hz - 0.018, hz + 0.018), PLASTIC)
    A.mul("side", box(X, Z, BP0 + 0.06, BP0 + 0.16, hz + 0.004, hz + 0.014), 2.2)
    # indicator repeater behind the front arch
    A.fill("side", box(X, Z, AXF - ARCH_R - 0.19, AXF - ARCH_R - 0.10, 0.705, 0.745), AMBER)
    A.fill("side", box(X, Z, AXF - ARCH_R - 0.19, AXF - ARCH_R - 0.10, 0.73, 0.745), (255, 225, 160), 0.6)
    # fuel filler flap on the rear quarter
    r = np.hypot(X + 1.62, Z - 0.79)
    A.mul("side", (r > 0.042) & (r < 0.050), 0.45)
    A.mul("side", (r > 0.050) & (r < 0.056), 1.12)
    # the B-pillar (a paint strip in the glass run) and the window seals: black
    A.fill("side", (X > BP0) & (X < BP1) & (Z > bz + 0.02), BLACK)
    A.fill("side", (X > XQ - 0.01) & (X < XWB + 0.02) & line(Z, bz + 0.03, 0.009), BLACK)
    # the C-pillar decal: three slanted bars
    for k in range(3):
        xc = XQ - 0.07 - k * 0.075
        A.fill("side", (np.abs(X - xc + (Z - 1.1) * 0.35) < 0.024) & (Z > bz + 0.05) & (Z < 1.22), BLACK)
    # the far model's side glass (never sampled by this model: its glass is a
    # material of its own), KEEP_OUT inside every edge this model's faces meet
    rz8 = K.vec(lambda x: rail(x)[1], X)
    win = (X > XQ + KO) & (X < XWB - KO) & (Z > bz + 0.03 + KO) & (Z < rz8 - KO) \
        & ~((X > BP0 - KO) & (X < BP1 + KO))
    streak = (np.exp(-((X - 0.9 * (Z - 1.1) + 0.10) / 0.07) ** 2)
              + 0.6 * np.exp(-((X - 0.9 * (Z - 1.1) + 0.35) / 0.035) ** 2))
    A.glass("side", win, smooth(bz + 0.03, rz8, Z), streak, GLASS_C)

    # ---- top: x, |y| ----
    X, Y = A.grid("top")
    w = K.vec(half_w, X)
    A.mul("top", (Y > w - 0.03) & (Y < w - 0.012), 1.12)
    # hood: shut lines along the edges and at the cowl
    A.mul("top", line(Y, w - 0.10, 0.005) & (X > XWB + 0.06) & (X < XF - 0.02), 0.4)
    A.mul("top", line(Y, w - 0.088, 0.004) & (X > XWB + 0.06) & (X < XF - 0.02), 1.12)
    A.mul("top", line(X, XWB + 0.065, 0.005) & (Y < w - 0.10), 0.35)
    cowl = (X > XWB - 0.01) & (X < XWB + 0.06) & (Y < w - 0.11)
    A.fill("top", cowl, BLACK)
    for y0 in (0.06, 0.46):
        A.fill("top", cowl & line(X - (Y - y0) * 0.10, XWB + 0.03, 0.006) & (Y > y0) & (Y < y0 + 0.36),
               (90, 92, 96))
    # a bonnet vent: a black slot in a plastic frame
    A.fill("top", box(X, Y, 1.28, 1.52, -1, 0.16), PLASTIC)
    A.fill("top", box(X, Y, 1.31, 1.49, -1, 0.13), BLACK)
    A.fill("top", box(X, Y, 1.31, 1.49, -1, 0.13) & (np.abs(X - 1.40) < 0.012), (60, 62, 66))
    # the roof: black drip rails and a tinted sunroof in a black frame
    A.fill("top", (X < XWT) & (X > XRE) & (Y > 0.56) & (Y < 0.59), BLACK)
    A.fill("top", box(X, Y, -0.80, -0.20, -1, 0.40), BLACK)
    sr = box(X, Y, -0.78, -0.22, -1, 0.38)
    A.fill("top", sr, GLASS_C)
    A.mul("top", sr, 1.0 + 0.9 * smooth(-0.78, -0.22, X))
    A.fill("top", sr & (np.abs(X + 0.36) < 0.04), (80, 92, 104), 0.7)
    # hatch lip: the lock
    lk = np.hypot(X - (XR + 0.10), Y)
    A.fill("top", lk < 0.03, PLASTIC)
    # the far model's windscreen
    y9 = K.vec(lambda x: section(x)[9][0], X)
    ws = (X > XWT + KO) & (X < XWB - KO) & (Y < y9 - KO)
    A.glass("top", ws, smooth(XWB, XWT, X), np.exp(-((X + 0.8 * Y - 0.25) / 0.06) ** 2), GLASS_C)

    # ---- front: |y|, z ----
    Y, Z = A.grid("front")
    A.fill("front", (Y < 0.72) & (Z > 0.605) & (Z < 0.775), BLACK)          # lamp + grille band
    for zb in (0.645, 0.700):                                                 # two thick slats
        A.fill("front", (Y < 0.385) & (Z > zb) & (Z < zb + 0.022), (64, 66, 70))
        A.fill("front", (Y < 0.385) & (Z > zb + 0.016) & (Z < zb + 0.022), (120, 122, 128))
    em = np.hypot(Y, Z - 0.69)
    A.ramp("front", em < 0.038, 0.2 + em / 0.038 * 0.6)                       # the badge
    A.fill("front", em < 0.022, STRIPE)
    A.fill("front", (Y < 0.72) & (Z > 0.596) & (Z < 0.606), STRIPE)         # the red pinstripe
    # the far model's headlamps, inside this model's lamp quads
    for (y0, y1) in ((0.40, 0.54), (0.555, 0.695)):
        lm = (Y > y0 + 0.012) & (Y < y1 - 0.012) & (Z > 0.637) & (Z < 0.743)
        A.fill("front", lm, LAMP_C)
        A.fill("front", lm & (np.hypot(Y - (y0 + y1) * 0.5, Z - 0.70) < 0.03), (255, 255, 250))
        A.flat("front", lm)
    # the valance: indicators, the plate, a black air dam under the bumper
    A.fill("front", (Y > 0.50) & (Y < 0.68) & (Z > 0.50) & (Z < 0.565), AMBER)
    A.fill("front", (Y > 0.50) & (Y < 0.68) & (Z > 0.548) & (Z < 0.565), (255, 225, 160), 0.6)
    A.fill("front", (Y < 0.20) & (Z > 0.49) & (Z < 0.58), (228, 228, 218))
    for k in range(4):
        A.fill("front", (Y > 0.02 + k * 0.045) & (Y < 0.05 + k * 0.045) & (Z > 0.51) & (Z < 0.56), (40, 38, 50))
    A.mul("front", Z < 0.30, 0.45)

    # ---- rear: |y|, z ----
    Y, Z = A.grid("rear")
    A.fill("rear", (Y < 0.745) & (Z > 0.64) & (Z < 0.80), BLACK)            # the tail band
    lm = (Y > 0.40 + 0.022) & (Y < 0.735 - 0.022) & (Z > 0.655 + 0.02) & (Z < 0.785 - 0.02)
    A.fill("rear", lm, (250, 46, 38))                                          # far-only lens
    A.fill("rear", lm & (Z > 0.745), (255, 130, 110))
    A.flat("rear", lm)
    A.fill("rear", (Y > 0.28) & (Y < 0.37) & (Z > 0.66) & (Z < 0.78), (232, 232, 226))   # reversing
    A.fill("rear", (Y < 0.19) & (Z > 0.655) & (Z < 0.765), (228, 228, 218))              # plate
    for k in range(4):
        A.fill("rear", (Y > 0.02 + k * 0.045) & (Y < 0.05 + k * 0.045) & (Z > 0.675) & (Z < 0.745), (40, 38, 50))
    A.fill("rear", (Y < 0.08) & (Z > 0.772) & (Z < 0.785), (250, 245, 225))              # plate lamp
    A.fill("rear", (Y < 0.24) & (Z > 0.84) & (Z < 0.87), PLASTIC)                        # hatch handle
    A.mul("rear", Z < 0.30, 0.5)
    # the far model's hatch glass, and the rear face of the D-pillar above it
    zt, zb = roof_z(XRE), deck_z(XRW)
    xz = XRE - (zt - Z) / (zt - zb) * (XRE - XRW)
    y9 = K.vec(lambda x: section(min(max(x, XRW), XRE))[9][0], xz)
    hg = (Z > zb + KO) & (Z < zt - KO) & (Y < y9 - KO)
    A.glass("rear", hg, smooth(zb, zt, Z), 0.7 * np.exp(-((Y - 0.7 * (Z - 0.95) - 0.18) / 0.05) ** 2), GLASS_C)


# --- the far model's design data (make-pica-far.py) -------------------------------
# section() indices kept: keel, rocker bottom, flank, belt crease, sill,
# greenhouse rail, glass edge, crown
FAR_LINES = [0, 2, 4, 5, 7, 8, 10, 11]


def far_stations():
    xs = {XF, XR, XWB, XWT, XRE, XRW}
    for ax in (AXF, AXR):
        for f in (-1.0, -0.7, 0.0, 0.7, 1.0):
            xs.add(ax + f * ARCH_R)
    return K.merge_stations(xs)


def far_trim(g):
    s2 = math.sqrt(0.5)
    wf, wr = half_w(XF - 0.1), half_w(XR + 0.1)
    K.bar(g, [(XF + 0.06, 0.0, (1, 0)), (XF + 0.02, wf - 0.02, (s2, s2)), (XF - 0.22, wf + 0.016, (0, 1))],
          0.30, 0.47, 0.07, cell="plastic", under=None)
    K.bar(g, [(XR - 0.06, 0.0, (-1, 0)), (XR - 0.02, wr - 0.02, (-s2, s2)), (XR + 0.26, wr + 0.016, (0, 1))],
          0.30, 0.49, 0.07, cell="plastic", under=None)


if __name__ == "__main__":
    K.main(sys.modules[__name__])

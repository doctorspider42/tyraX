"""Build the Strix V12: a wide, low early-90s mid-engine wedge supercar.

Run through Blender, not plain Python:

    blender -b --factory-startup --python make-strix.py -- [--out GLB]
            [--preview DIR] [--no-bake] [--opaque-glass]

Writes res/models/strix.glb and, with --preview, Eevee renders (front34, rear34,
side, chase, top, cabin, a contact sheet and the atlas) into DIR. Run
make-strix-far.py after it: the far model wears this model's texture.

An original design in the spirit of the era's V12 wedges, not a copy of one:
a blunt low nose with slim lamps and fender humps either side of a hood that
sits in a valley, a long raked windscreen and a cab-forward glasshouse with
deep tumblehome, a side intake scooped out of the flank ahead of the huge rear
hips, a louvred engine cover in a tunnel between flying buttresses, a
full-width black tail panel with four round lamps, a rear wing on posts, and
five-spoke wheels.

The same loft recipe as the Ravager (make-ravager.py; carkit.py has the shared
plumbing): twelve character lines per half section on ~36 stations, mirrored
with the same UVs, painted in world coordinates into one 256x256 atlas and
AO-baked. The side intake is an inward offset on the flank and tuck-under
lines between the door and the rear arch, painted black: the same loops run
through it, so it costs no triangles and no strip seams.

Units are metres, Blender axes: +X forward, +Y to the car's left, +Z up.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import carkit as K  # noqa: E402
from carkit import lerp, pchip, ramp, smooth  # noqa: E402

NAME = "strix"
SCALE = 0.92
XF, XR = 2.20, -2.34         # front / rear faces (front overhang 0.90 < rear 0.98)
AXF, AXR = 1.30, -1.36       # wheelbase 2.66
R_WHEEL, W_WHEEL = 0.33, 0.27
TRACK = 1.62
ARCH_R = 0.385
HUB_Z = R_WHEEL
ARCH_INNER_Y = 0.60
XWB, XWT = 0.62, -0.28       # windscreen base / top
XRE = -0.85                  # roof end
XRG = -1.12                  # the small rear window's base (engine cover behind it)
XRW = -1.75                  # engine cover meets the rear deck
XSE = -1.95                  # sail end (the buttresses meet the deck)
XQ = -0.80                   # rear edge of the side glass
XI0, XI1 = -0.52, -0.94      # the side intake, door to rear arch
ZMID = 0.52
LENGTH, HEIGHT = XF - XR, 1.13
GLASS_ALPHA = 0.45

half_w = pchip([(XR, .945), (XR + 0.2, .972), (-1.36, .985), (-0.8, .962), (0.0, .935),
                (0.7, .942), (1.30, .955), (1.9, .935), (XF, .895)])
belt = pchip([(XR, .870), (-2.0, .885), (-1.36, .875), (-0.8, .842), (0.0, .800),
              (0.75, .762), (1.30, .735), (1.75, .680), (XF, .588)])
rocker_z = pchip([(XR, .24), (-1.9, .19), (-1.0, .15), (0.9, .15), (1.8, .18), (XF, .20)])
hood_z = pchip([(XWB, .800), (1.3, .722), (1.9, .640), (XF, .596)])
roof_z = pchip([(XWT, 1.118), (-0.55, 1.130), (XRE, 1.116)])
deck_z = pchip([(XR, .902), (-2.1, .925), (XRW, .936)])
sail_z = pchip([(XSE, .945), (-1.7, .995), (-1.3, 1.060), (XRE, 1.098)])


def intake(x):
    """How far the side intake scoops the flank in at x."""
    return 0.06 * float(smooth(XI0, XI0 - 0.12, x)) * float(smooth(XI1 - 0.02, XI1 + 0.10, x))


def center_z(x):
    if x >= XWB:
        return hood_z(x)
    if x >= XWT:
        t = (XWB - x) / (XWB - XWT)
        return lerp(hood_z(XWB) + 0.002, roof_z(XWT), t) + 0.016 * math.sin(math.pi * t)
    if x >= XRE:
        return roof_z(x)
    if x >= XRW:  # rear window, then the engine cover
        t = (XRE - x) / (XRE - XRW)
        return lerp(roof_z(XRE), deck_z(XRW), t ** 0.85)
    return deck_z(x)


def rail(x):
    """Greenhouse outer line: hood edge, A-pillar, roof rail, sail top, deck."""
    w = half_w(x)
    if x >= XWB:
        return w - 0.25, hood_z(x) - 0.010
    if x >= XWT:
        t = (XWB - x) / (XWB - XWT)
        return lerp(half_w(XWB) - 0.25, 0.600, t), lerp(hood_z(XWB) - 0.004, 1.090, t)
    if x >= XRE:
        t = (XWT - x) / (XWT - XRE)
        return lerp(0.600, 0.612, t), lerp(1.090, 1.080, t)
    if x >= XSE:
        t = (XRE - x) / (XRE - XSE)
        return lerp(0.612, 0.800, t ** 1.4), sail_z(x)
    return 0.800, deck_z(x) - 0.006


def section(x):
    """Half cross-section at station x: 12 (y, z) points, keel to crown."""
    w, b, zr, si = half_w(x), belt(x), rocker_z(x), intake(x)
    p = [
        (0.0, zr - 0.012),                 # 0 keel
        (w - 0.20, zr - 0.010),            # 1 floor edge
        (w - 0.08, zr),                    # 2 rocker bottom
        (w - 0.030 - si * 0.6, zr + 0.10),  # 3 tuck-under
        (w - si, 0.45),                    # 4 flank
        (w - 0.010, b),                    # 5 belt crease
        (w - 0.050, b + 0.030),            # 6 fender round
        (w - 0.140, b + 0.045),            # 7 sill
    ]
    ry, rz = rail(x)
    p.append((ry, rz))                     # 8 greenhouse rail
    if x >= XWB:
        p.append((ry - 0.07, hood_z(x) - 0.004))
    elif x >= XRE:
        p.append((ry - 0.065, rz + (0.016 if x < XWT else 0.010)))
    elif x >= XSE:
        p.append((ry - 0.060, rz + 0.004))
    else:
        p.append((ry - 0.07, deck_z(x) - 0.002))
    cz = center_z(x)
    if x >= XRE or x < XSE:
        dz = -0.010 if XWT <= x < XWB else (-0.006 if XRE <= x < XWT else 0.0)
        p.append((0.40, cz + dz))          # 10 glass edge / crown shoulder
    else:  # tunnel: the cover's edge hugs the inside of the sail
        t = ramp(XRE - x, 0.0, 0.035)
        p.append((lerp(0.40, p[9][0] - 0.012, t), cz))
    p.append((0.0, cz))                    # 11 crown
    return K.lift_arches(sys.modules[__name__], p, x)


def stations():
    xs = {XF, XF - 0.18, XR, XR + 0.15, XWB, XWB + 0.30, XWT, XRE, XRG, XRW, XSE, XQ,
          XI0, XI0 - 0.10, XI1 + 0.08, 0.1, 1.9, -2.12, -0.55}
    for ax in (AXF, AXR):
        for f in (-1, -.92, -.7, -.38, 0, .38, .7, .92, 1):
            xs.add(ax + f * ARCH_R)
    return K.merge_stations(xs)


def glass_strip(k, xm):
    if k == 7 and XQ < xm < XWB:
        return True
    if k in (9, 10) and XWT < xm < XWB:
        return True
    if k == 10 and XRG < xm < XRE:
        return True
    return False


CAPS = [(XF, 1, 0.050, 0.40, 0.95, 0.90), (XR, -1, 0.050, 0.56, 0.95, 0.90)]

# --- colours ------------------------------------------------------------------
PAINT = (104, 46, 158)       # early-90s metallic violet
BLACK = (18, 18, 20)
CARBON = (28, 28, 31)
AMBER = (240, 150, 28)
RED = (190, 22, 20)
LAMP_C = (234, 232, 214)
GLASS_C = (24, 30, 38)
KO = K.KEEP_OUT
COLOURS = {
    "paint": PAINT,
    "glass window": (0.09, 0.11, 0.14, 1.0),
    "headlights": (0.92, 0.91, 0.84, 1.0),
    "rear lights": (0.72, 0.05, 0.05, 1.0),
    "tyre rubber": (0.035, 0.035, 0.04, 1.0),
    "rim chrome": (0.66, 0.67, 0.70, 1.0),
    "rim barrel": (0.06, 0.06, 0.065, 1.0),
}


def make_atlas():
    return K.Atlas(XR, XF, side_z=(0.10, 1.16), front_z=(0.12, 0.82), rear_z=(0.16, 1.16), half_w=1.0,
                   cells=["black", "dark", "chrome", "int", "intdark", "seat", "paint"],
                   ramps={"chrome": K.gunmetal_ramp},
                   colours={"black": BLACK, "dark": (10, 10, 11), "int": (44, 40, 38),
                            "intdark": (24, 23, 22), "seat": (120, 104, 82), "paint": PAINT})


# --- trim, lamps, interior, wheel ----------------------------------------------
def build_trim(g):
    s2 = math.sqrt(0.5)
    wf, wr = half_w(XF - 0.1), half_w(XR + 0.1)
    # front splitter and rear diffuser lip
    K.bar(g, [(XF + 0.04, 0.0, (1, 0)), (XF + 0.03, 0.70, (1, 0)), (XF - 0.02, wf - 0.02, (s2, s2)),
              (XF - 0.30, wf + 0.004, (0, 1))], 0.13, 0.19, 0.08, cell="dark", under="black")
    K.bar(g, [(XR - 0.03, 0.0, (-1, 0)), (XR - 0.025, 0.72, (-1, 0)), (XR + 0.01, wr - 0.02, (-s2, s2)),
              (XR + 0.24, wr + 0.004, (0, 1))], 0.20, 0.33, 0.07, cell="dark", under="black")
    # the rear wing: a paint-cell blade on two posts, with end plates
    g.box((-2.12, 0.46, 1.118), (0.13, 0.46, 0.020), uv="cell:paint", skip=("-y",))
    g.box((-2.12, 0.92, 1.085), (0.15, 0.008, 0.07), uv="cell:paint")
    g.box((-2.08, 0.60, 1.01), (0.05, 0.014, 0.09), uv="cell:dark", skip=("-z",))
    # door mirrors, body colour
    xm = 0.52
    w, b = half_w(xm), belt(xm)
    g.box((xm, w + 0.05, b + 0.10), (0.05, 0.06, 0.035), uv="cell:paint", skip=("-y",))
    g.box((xm + 0.02, w - 0.02, b + 0.06), (0.03, 0.03, 0.012), uv="cell:dark", skip=("-y",))
    # twin exhausts in the diffuser
    g.box((XR - 0.01, 0.28, 0.28), (0.06, 0.055, 0.035), uv="cell:dark", skip=("+x",))


def build_lamps(g):
    xh = XF - CAPS[0][2] + 0.012
    for (y0, y1) in ((0.50, 0.645), (0.665, 0.81)):          # slim headlamps
        K.rect_lamp(g, "headlights", xh, y0, y1, 0.50, 0.555, 1)
    xt = XR + CAPS[1][2] - 0.012
    for yc in (0.50, 0.74):                                    # four round tail lamps
        K.disc_lamp(g, "rear lights", xt, yc, 0.715, 0.072, 8, -1)


INTERIOR = {"inset": 0.15, "sill": 0.14, "floor": 0.30, "rear": -0.95, "dash": 0.55,
            "dash_back": 0.28, "dash_z": 0.78, "seat": (-0.38, 0.40, 0.36, 0.50),
            "wheel": (0.02, 0.40, 0.67), "wheel_r": 0.17, "wheel_tilt": 58}


def build_wheel(g):
    """15 segments, five spokes (every third segment): 160 triangles."""
    K.build_wheel(g, R_WHEEL, W_WHEEL, 15, lambda s: "rim chrome" if s % 3 == 0 else "rim barrel",
                  rs=0.68, hub=0.20, phase=0.21)


def wheel_face(r, ang, up):
    reg = np.empty(r.shape + (3,), np.float32)
    reg[:] = BLACK
    side = (r > 0.68) & (r < 1.0)
    reg[side] = np.asarray((24, 24, 27), np.float32) * (0.8 + 0.5 * smooth(1.0, 0.8, r[side]))[..., None]
    dish = r <= 0.68
    reg[dish] = (16, 16, 18)
    lit = 0.55 * K.gunmetal_ramp(0.5 - 0.5 * up / 0.68) + 0.45 * 220
    spoke = dish & (np.mod(ang / (2 * np.pi) * 5 + 0.08, 1.0) < 0.26)
    rim = (r > 0.60) & dish
    reg[spoke | rim] = lit[spoke | rim]
    cap = r < 0.2
    reg[cap] = K.gunmetal_ramp(0.2 + r[cap] * 2.0)
    return reg


# --- paint ------------------------------------------------------------------------
def paint(A):
    line = lambda a, c, wdt: np.abs(a - c) < wdt
    box = lambda a, b, a0, a1, b0, b1: (a > a0) & (a < a1) & (b > b0) & (b < b1)

    # ---- side: x, z ----
    X, Z = A.grid("side")
    bz = K.vec(belt, X)
    rz = K.vec(rocker_z, X)
    # the flank's light, brighter towards the shoulder; a pearl lift on top
    A.mul("side", Z < bz, 0.78 + 0.26 * smooth(rz, bz, Z))
    A.mul("side", (Z < bz - 0.004) & (Z > bz - 0.015), 1.18)
    A.mul("side", (Z < bz - 0.015) & (Z > bz - 0.07), 0.86 + 0.14 * smooth(bz - 0.07, bz - 0.015, Z))
    A.mul("side", (Z > bz), 1.10)
    # the side intake: a black mesh in the scooped flank, a lit leading lip
    xin = (X < XI0 - 0.03) & (X > XI1)
    top = bz - 0.05
    inl = xin & (Z > rz + 0.12) & (Z < top)
    A.fill("side", inl, BLACK)
    for zb in np.arange(0.36, 0.80, 0.07):
        A.fill("side", inl & (Z > zb) & (Z < zb + 0.018), (44, 44, 48))
    A.mul("side", xin & (Z >= top) & (Z < top + 0.012), 1.25)
    A.mul("side", (X < XI0 + 0.02) & (X > XI0 - 0.05) & (Z > rz + 0.12) & (Z < top), 1.2)
    # carbon rocker skirt with a lit edge
    A.fill("side", Z < rz + 0.085, CARBON)
    A.mul("side", line(Z, rz + 0.082, 0.004), 2.0)
    # shadowed arch lips
    for ax in (AXF, AXR):
        r = np.hypot(X - ax, Z - HUB_Z)
        A.mul("side", (r > ARCH_R) & (r < ARCH_R + 0.05), 0.55 + 0.45 * smooth(ARCH_R, ARCH_R + 0.05, r))
    # door shut lines, the hidden handle slot in the intake's leading edge
    for xg in (XWB - 0.02, XI0 + 0.03):
        gz = (Z > rz + 0.09) & (Z < bz + 0.035)
        A.mul("side", gz & line(X, xg, 0.005), 0.30)
        A.mul("side", gz & line(X, xg + 0.013, 0.004), 1.15)
    A.mul("side", (X > XI0 + 0.03) & (X < XWB - 0.02) & line(Z, rz + 0.095, 0.004), 0.4)
    A.fill("side", box(X, Z, XI0 + 0.05, XI0 + 0.17, bz - 0.075, bz - 0.058), BLACK)
    # side markers
    A.fill("side", box(X, Z, 1.86, 2.02, 0.49, 0.525), AMBER)
    A.fill("side", box(X, Z, -2.24, -2.10, 0.60, 0.635), RED)
    # a chrome model badge on the rear fender, dark "letters"
    A.ramp("side", box(X, Z, -1.95, -1.76, 0.745, 0.785), (0.785 - Z) / 0.04, K.chrome_ramp)
    for k in range(3):
        xa = -1.94 + 0.012 + k * 0.058
        A.fill("side", box(X, Z, xa, xa + 0.04, 0.754, 0.776), BLACK, 0.85)
    # the window seal under the side glass
    A.fill("side", (X < XWB + 0.02) & (X > XQ - 0.02) & line(Z, bz + 0.045, 0.008), BLACK)
    # the far model's side glass
    rz8 = K.vec(lambda x: rail(x)[1], X)
    win = (X > XQ + KO) & (X < XWB - KO) & (Z > bz + 0.045 + KO) & (Z < rz8 - KO)
    streak = (np.exp(-((X - 1.2 * (Z - 0.95) + 0.05) / 0.07) ** 2)
              + 0.6 * np.exp(-((X - 1.2 * (Z - 0.95) + 0.30) / 0.035) ** 2))
    A.glass("side", win, smooth(bz + 0.04, rz8, Z), streak, GLASS_C)

    # ---- top: x, |y| ----
    X, Y = A.grid("top")
    w = K.vec(half_w, X)
    A.mul("top", (Y > w - 0.06) & (Y < w - 0.02), 1.14)          # the shoulder ridge
    # the front lid: shut lines, two NACA ducts
    A.mul("top", line(Y, w - 0.24, 0.005) & (X > XWB + 0.08) & (X < XF - 0.03), 0.4)
    A.mul("top", line(Y, w - 0.228, 0.004) & (X > XWB + 0.08) & (X < XF - 0.03), 1.12)
    A.mul("top", line(X, XWB + 0.085, 0.005) & (Y < w - 0.24), 0.35)
    for yc in (0.26,):
        t = np.clip((X - 1.05) / 0.45, 0, 1)
        duct = (X > 1.05) & (X < 1.50) & (np.abs(Y - yc) < 0.03 + 0.06 * t)
        A.fill("top", duct, BLACK)
        A.mul("top", duct & (X > 1.44), 3.0)
    cowl = (X > XWB - 0.01) & (X < XWB + 0.07) & (Y < w - 0.25)
    A.fill("top", cowl, BLACK)
    A.fill("top", cowl & line(X - Y * 0.1, XWB + 0.035, 0.006) & (Y > 0.1) & (Y < 0.55), (90, 92, 96))
    # the engine cover: black louvres between body-colour slats, a lit edge
    cov = (X < XRG - 0.06) & (X > XRW + 0.04) & (Y < 0.34)
    A.fill("top", cov, BLACK)
    for k in range(5):
        xa = XRG - 0.10 - k * 0.12
        A.fill("top", cov & (X < xa) & (X > xa - 0.06), PAINT)
        A.mul("top", cov & (X < xa) & (X > xa - 0.012), 1.3)
    # the deck: the lock, the shut line round the cover
    A.mul("top", line(X, XRW + 0.02, 0.005) & (Y < 0.40), 0.35)
    # the far model's windscreen and small rear window
    y9 = K.vec(lambda x: section(x)[9][0], X)
    y10 = K.vec(lambda x: section(x)[10][0], X)
    ws = (X > XWT + KO) & (X < XWB - KO) & (Y < y9 - KO)
    A.glass("top", ws, smooth(XWB, XWT, X), np.exp(-((X + 0.8 * Y - 0.30) / 0.07) ** 2), GLASS_C)
    rw = (X > XRG + KO) & (X < XRE - KO) & (Y < y10 - KO)
    A.glass("top", rw, smooth(XRG, XRE, X), 0.7 * np.exp(-((X - 0.6 * Y + 0.95) / 0.05) ** 2), GLASS_C)

    # ---- front: |y|, z ----
    Y, Z = A.grid("front")
    A.fill("front", (Y < 0.83) & (Z > 0.485) & (Z < 0.57), BLACK)          # lamp band
    for (y0, y1) in ((0.50, 0.645), (0.665, 0.81)):                          # far-only lenses
        lm = (Y > y0 + 0.012) & (Y < y1 - 0.012) & (Z > 0.512) & (Z < 0.543)
        A.fill("front", lm, LAMP_C)
        A.fill("front", lm & (Y > y0 + 0.03) & (Y < y0 + 0.07), (255, 255, 250))
        A.flat("front", lm)
    # the big intake: black with one body-colour bar, a lit lip
    intk = (Y < 0.72) & (Z > 0.22) & (Z < 0.41)
    A.fill("front", intk, BLACK)
    A.fill("front", intk & (Z > 0.30) & (Z < 0.33), (44, 44, 48))
    A.mul("front", (Y < 0.72) & (Z >= 0.41) & (Z < 0.418), 1.25)
    # brake ducts at the corners, indicators, the badge
    A.fill("front", (Y > 0.76) & (Y < 0.84) & (Z > 0.24) & (Z < 0.38), BLACK)
    A.fill("front", (Y > 0.60) & (Y < 0.80) & (Z > 0.44) & (Z < 0.47), AMBER)
    em = np.hypot(Y, Z - 0.47)
    A.ramp("front", em < 0.035, 0.2 + em / 0.035 * 0.6, K.chrome_ramp)
    A.fill("front", em < 0.02, (220, 180, 40))
    A.mul("front", Z < 0.22, 0.45)

    # ---- rear: |y|, z ----
    Y, Z = A.grid("rear")
    tp = (Y < 0.88) & (Z > 0.60) & (Z < 0.83)
    A.fill("rear", tp, BLACK)                                                # tail panel
    for zb in (0.64, 0.79):
        A.fill("rear", tp & (Y < 0.36) & (Z > zb) & (Z < zb + 0.015), (48, 48, 52))
    for yc in (0.50, 0.74):                                                  # far-only lenses
        r = np.hypot(Y - yc, Z - 0.715)
        lm = r < 0.052
        A.fill("rear", lm, (250, 46, 38))
        A.fill("rear", lm & (Z > 0.735), (255, 130, 110))
        A.flat("rear", lm)
    A.ramp("rear", (Y < 0.30) & (Z > 0.69) & (Z < 0.74), (0.74 - Z) / 0.05, K.chrome_ramp)   # badge bar
    A.fill("rear", (Y < 0.28) & (Z > 0.700) & (Z < 0.730), BLACK, 0.5)
    A.fill("rear", (Y < 0.20) & (Z > 0.44) & (Z < 0.555), (228, 228, 218))                     # plate
    for k in range(4):
        A.fill("rear", (Y > 0.02 + k * 0.045) & (Y < 0.05 + k * 0.045) & (Z > 0.46) & (Z < 0.535), (40, 38, 50))
    A.fill("rear", (Y > 0.36) & (Y < 0.62) & (Z > 0.50) & (Z < 0.53), (232, 232, 226))        # reversing
    A.mul("rear", Z < 0.34, 0.45)


# --- the far model's design data (make-strix-far.py) ------------------------------
FAR_LINES = [0, 2, 4, 5, 7, 8, 10, 11]


def far_stations():
    xs = {XF, XR, XWB, XWT, XRE, XRW, XSE, XR + 0.15}
    for ax in (AXF, AXR):
        for f in (-1.0, -0.7, 0.0, 0.7, 1.0):
            xs.add(ax + f * ARCH_R)
    return K.merge_stations(xs)


def far_trim(g):
    g.box((-2.12, 0.46, 1.118), (0.13, 0.46, 0.020), uv="cell:paint", skip=("-y", "-z"))
    g.box((-2.08, 0.60, 1.01), (0.05, 0.014, 0.09), uv="cell:dark", skip=("-z", "-y", "+z"))


if __name__ == "__main__":
    K.main(sys.modules[__name__])

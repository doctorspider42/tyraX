"""The studio hairstyles, modelled as hair cards on guide locks (Blender).

    blender -b --factory-startup --python grooms.py -- <head_f.obj> <head_m.obj> <out_dir>
            [--only id,id] [--atlas strands.png --preview <dir> [--hero-only]]

Writes <out_dir>/<id>.npz (hero / standard / crowd cards in kit space; see
studio_gear.modelled_groom). groomkit.py has the tools; this file is the
shapes. A style is GROUPS of guide locks, each group interpolated across its
neighbours, laid in three layers (as in the layered lob that came first):

    under   wide opaque strips 0-1 close to the scalp - no skin through gaps
    mid     strips 2-4, the body of the cut
    outer   strips 5-7, narrower, interpolated more, cut shorter at random

plus a crown whorl dome and a style's own extras (bun coil, ponytail).
Angles: azimuth 0 = the face, 90 = the character's left; elevation from the
skull centre (0, 0.745, 0.045). The front hairline is about 33 degrees up,
the top of the ear -20, the nape -45.
"""
import math
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from groomkit import (S, S_dir, Builder, Heads, direction, lerp_lock, preview,  # noqa: E402
                      scale_in, shorten, slerp, sweep)

LEVELS = {
    #           outer-fill mid-fill under-step rows across width-scale
    "hero":     (2, 1, 1, 12, 3, 1.0),
    "standard": (1, 0, 2, 8, 3, 1.25),
    "crowd":    (0, 0, 1, 6, 2, 1.7),
}


def hairline(az):
    """Elevation of the hairline at azimuth az (degrees)."""
    return float(np.interp(abs(((az + 180) % 360) - 180),
                           [0, 30, 48, 62, 76, 90, 105, 125, 150, 180],
                           [33, 31, 25, 4, -14, -19, -22, -26, -40, -45]))


def hang(az, ys, radii, cz=0.036):
    """Points of a falling lock at azimuth az: (rx, rz) of the hair's
    horizontal outline at each height y."""
    a = math.radians(az)
    return [(rx * math.sin(a), y, cz + rz * math.cos(a)) for y, (rx, rz) in zip(ys, radii)]


def combed_to(h, target_az, target_el, n=20, lift=0.008):
    """Locks from the whole hairline combed to one point on the head (a bun,
    a tie), each along the great circle from its root: front locks go over
    the crown, side locks round the side, the nape up."""
    goal = direction(target_az, target_el)
    target = np.array(S(h, target_az, target_el, 0.010))
    locks = []
    for az in np.linspace(-180, 180, n + 1)[:-1]:
        d0 = direction(az, hairline(az) + 1)
        pts = []
        for t in (0.0, 0.12, 0.32, 0.55, 0.78, 0.92):
            up = lift * min(1.0, t / 0.3) ** 0.7
            pts.append(S_dir(h, slerp(d0, goal, t), 0.002 + up))
        d = np.array(pts[-1]) - target
        pts.append(tuple(target + 0.008 * d / max(np.linalg.norm(d), 1e-6)))
        locks.append(pts)
    locks.append(locks[0])
    return locks, target


# ---------------------------------------------------------------------------
# The layered lob (2.14.0): chin-to-collarbone, deep side part, swept fringe.
def lob(h):
    px = 0.024
    big = [
        [(px + 0.011, 0.795, 0.124), (-0.012, 0.792, 0.151), (-0.055, 0.766, 0.146),
         (-0.086, 0.716, 0.108), (-0.099, 0.656, 0.090), (-0.096, 0.596, 0.082)],
        [(px + 0.011, 0.806, 0.106), (-0.020, 0.810, 0.136), (-0.066, 0.787, 0.122),
         (-0.096, 0.732, 0.084), (-0.105, 0.662, 0.068), (-0.101, 0.598, 0.060)],
        [(px + 0.011, 0.816, 0.086), (-0.026, 0.824, 0.110), (-0.074, 0.802, 0.090),
         (-0.102, 0.742, 0.058), (-0.108, 0.668, 0.044), (-0.103, 0.601, 0.038)],
        [(px + 0.011, 0.826, 0.062), (-0.031, 0.835, 0.080), (-0.080, 0.810, 0.055),
         (-0.105, 0.748, 0.030), (-0.109, 0.671, 0.018), (-0.104, 0.603, 0.014)],
        [(0.020, 0.834, 0.036), (-0.031, 0.841, 0.044), (-0.082, 0.814, 0.021),
         (-0.106, 0.751, 0.000), (-0.108, 0.673, -0.007), (-0.102, 0.605, -0.010)],
    ]
    small = [
        [(px - 0.008, 0.796, 0.124), (0.060, 0.786, 0.138), (0.086, 0.746, 0.114),
         (0.098, 0.690, 0.094), (0.101, 0.640, 0.084), (0.097, 0.598, 0.078)],
        [(px - 0.008, 0.808, 0.104), (0.064, 0.801, 0.113), (0.093, 0.756, 0.082),
         (0.105, 0.692, 0.062), (0.105, 0.640, 0.055), (0.100, 0.600, 0.050)],
        [(px - 0.008, 0.820, 0.080), (0.069, 0.812, 0.082), (0.098, 0.760, 0.050),
         (0.107, 0.692, 0.031), (0.106, 0.640, 0.024), (0.101, 0.601, 0.020)],
        [(px - 0.008, 0.830, 0.054), (0.071, 0.818, 0.049), (0.100, 0.762, 0.020),
         (0.108, 0.692, 0.002), (0.106, 0.641, -0.005), (0.101, 0.602, -0.009)],
    ]
    whorl = (0.006, 0.837, 0.010)

    def back(deg, drop):
        a = math.radians(deg)
        s, c = math.sin(a), math.cos(a)
        return [(whorl[0] + 0.030 * s, whorl[1] - 0.003, whorl[2] + 0.034 * c)] + hang(
            deg, (0.816, 0.766, 0.704, 0.648, 0.602 - drop),
            ((0.058, 0.066), (0.093, 0.099), (0.104, 0.099), (0.098, 0.091), (0.086, 0.079)))

    backs = [back(d, 0.004 * math.cos(math.radians(d - 180))) for d in (124, 142, 160, 180, 200, 218, 236)]
    return dict(groups=[small + backs + big[::-1]], whorl=(whorl, 0.050), cut=(0.012, 0.050))


# ---------------------------------------------------------------------------
# Side crop: a short man's cut, side part on the left, top swept right,
# sides and back combed down close and tapered to the nape.
def side_crop(h):
    part = [(20 + 20 * t, 36 + 40 * t) for t in np.linspace(0, 1, 5)]
    top = []
    for k, (az, el) in enumerate(part):  # big side, swept across to the right
        t = k / (len(part) - 1)
        top.append(sweep(h, (az + 6, el), (-58 - 55 * t, 18 + 6 * t), 0.013 - 0.004 * t, n=6, rise=0.45,
                         end_lift=0.005))
    left = [sweep(h, (az - 7, el), (62 + 30 * t, 20 + 4 * t), 0.009, n=5, rise=0.4, end_lift=0.004)
            for t, (az, el) in zip(np.linspace(0, 1, 5), part)]
    sides = []
    for az in np.linspace(60, 300, 17):  # temples, sides and back, combed down short
        a = ((az + 180) % 360) - 180
        root_el = 78 if abs(a) > 100 else 50
        sides.append(sweep(h, (a * 0.9, root_el), (a + 8 * math.copysign(1, a), hairline(a) - 3), 0.006,
                           n=6, rise=0.30, end_lift=0.0025))
    return dict(groups=[top[::-1] + [sides[-1]], left + sides], whorl=(S(h, 180, 82), 0.045),
                cut=(0.0, 0.012), under=0.6, mid=0.3, widths=(0.045, 0.030, 0.024), gaps=(0.0025, 0.005, 0.008),
                tip=0.030, rows=0.7)


# ---------------------------------------------------------------------------
# Choppy pixie: short and textured, a long fringe swept right onto the brow,
# lifted choppy ends over the crown, a tapered nape.
def choppy_pixie(h):
    part = [(26 + 22 * t, 40 + 36 * t) for t in np.linspace(0, 1, 6)]
    top = []
    for k, (az, el) in enumerate(part):
        t = k / (len(part) - 1)
        if k < 3:  # a long fringe swept right, down onto the brow
            lock = sweep(h, (az + 6, el), (-20 - 22 * k, -6 + 9 * k), 0.022 - 0.003 * k, n=7, rise=0.35,
                         end_lift=0.011)
        else:
            lock = sweep(h, (az + 6, el), (-70 - 45 * t, 14 + 10 * t), 0.024 - 0.008 * t, n=6, rise=0.40,
                         end_lift=0.010)
        top.append(lock)
    left = [sweep(h, (az - 7, el), (58 + 34 * t, 10 + 8 * t), 0.016, n=5, rise=0.4, end_lift=0.007)
            for t, (az, el) in zip(np.linspace(0, 1, 6), part)]
    back = []
    for az in np.linspace(60, 300, 17):
        a = ((az + 180) % 360) - 180
        nape = max(0.0, -math.cos(math.radians(a)))  # pointed at the nape
        root_el = 78 if abs(a) > 100 else 52
        back.append(sweep(h, (a * 0.9, root_el), (a + 6 * math.copysign(1, a), hairline(a) - 6 - 8 * nape ** 4),
                          0.014, n=6, rise=0.35, end_lift=0.004))
    return dict(groups=[top[::-1] + [back[-1]], left + back], whorl=(S(h, 180, 82), 0.045),
                cut=(0.004, 0.035), under=0.6, mid=0.3, widths=(0.045, 0.028, 0.020), gaps=(0.003, 0.007, 0.012),
                flare=0.010, tip=0.040, rows=0.75)


# ---------------------------------------------------------------------------
# Rain bob: a chin-length bob with blunt full bangs, no part.
def rain_bob(h):
    bangs = []
    for x in np.linspace(-0.062, 0.062, 7):
        root = S(h, math.degrees(math.atan2(x, 0.09)) * 0.8, 72, 0.003)
        bangs.append([root, (x * 1.02, 0.818, 0.126), (x * 1.06, 0.792, 0.144),
                      (x * 1.06, 0.768, 0.146), (x * 1.04, 0.750, 0.143)])
    body = []
    for az in np.linspace(60, 300, 15):
        a = ((az + 180) % 360) - 180
        front = max(0.0, math.cos(math.radians(a)))
        root = S(h, a * 0.6, 74, 0.003)
        end_y = 0.636 + 0.010 * front
        body.append([root] + hang(a, (0.808, 0.762, 0.704, 0.662, end_y),
                                  ((0.070, 0.074), (0.098, 0.101), (0.106, 0.102), (0.104, 0.097), (0.094, 0.088))))
    return dict(groups=[bangs, body], whorl=(S(h, 0, 88), 0.050), cut=(0.004, 0.030))


# ---------------------------------------------------------------------------
# Curtain shag: collarbone-length, centre-parted curtain fringe, heavy layers
# with flicked ends and volume at the crown.
def curtain_shag(h):
    curtains = []
    for side in (-1, 1):
        for k in range(3):
            o = 0.012 * k
            curtains.append([(-side * 0.006, 0.800 + 0.010 * k, 0.122 - 0.016 * k),
                             (side * (0.030 + o), 0.796 + 0.008 * k, 0.152 - 0.012 * k),
                             (side * (0.064 + o), 0.758, 0.140 - 0.014 * k),
                             (side * (0.088 + o * 0.6), 0.705, 0.108 - 0.012 * k),
                             (side * (0.098 + o * 0.4), 0.648, 0.086 - 0.01 * k),
                             (side * (0.100 + o * 0.3), 0.598 - 0.01 * k, 0.074 - 0.01 * k)])
    left, right = curtains[3:], curtains[:3]
    body = []
    for az in np.linspace(65, 295, 15):
        a = ((az + 180) % 360) - 180
        root = S(h, a * 0.55, 74, 0.004)
        body.append([root] + hang(a, (0.818, 0.770, 0.705, 0.640, 0.585, 0.560),
                                  ((0.074, 0.080), (0.106, 0.110), (0.116, 0.112), (0.112, 0.104),
                                   (0.104, 0.096), (0.100, 0.092))))
    return dict(groups=[left + body + right[::-1]], whorl=(S(h, 0, 88), 0.050), cut=(0.015, 0.070),
                flare=0.009, gaps=(0.004, 0.009, 0.015))


# ---------------------------------------------------------------------------
# Tide knot: everything combed up and back into a high bun.
def tide_knot(h):
    locks, knot = combed_to(h, 180, 52)

    def bun(b, heads, level, rng):
        out = np.array(knot) - np.array([0.0, 0.745, 0.045])
        out /= np.linalg.norm(out)
        a = np.cross(out, [1.0, 0, 0])
        a /= np.linalg.norm(a)
        c = np.cross(out, a)
        turns = 1.6
        n = 28 if level != "crowd" else 12
        coil = []
        for i in range(n):
            t = i / (n - 1)
            ang = 2 * math.pi * turns * t
            r = 0.030 * (1 - 0.60 * t)
            coil.append(tuple(np.array(knot) + out * (0.006 + 0.014 * t) + r * (math.cos(ang) * a + math.sin(ang) * c)))
        sectors = 8 if level != "crowd" else 5
        b.tube(coil, lambda t: 0.016 * (1 - 0.35 * t), sectors, 0)
        if level != "crowd":
            for k in range(6 if level == "hero" else 3):
                ph = k / 6.0
                path = coil[int(ph * 6):]
                b.card(path[:: max(1, len(path) // 6)], 0.020, 0.0, 5 + k % 3, curl=0.3, taper=0.4, snap=False)
        # Loose wisps at the temples.
        for side in (-1, 1):
            b.card([S(heads, side * 60, 24, 0.004), S(heads, side * 72, 2, 0.008),
                    (side * 0.090, 0.700, 0.092), (side * 0.088, 0.672, 0.088)],
                   0.012, 0.006, 7, curl=0.1, taper=0.3)

    return dict(groups=[locks], whorl=None, cut=(0.0, 0.006), extra=bun, sheet=True, mid_layer=False,
                widths=(0.050, 0.034, 0.026), gaps=(0.0065, 0.008, 0.010), tip=0.02, root=0.5)


# ---------------------------------------------------------------------------
# Low ponytail: combed back to a tie at the nape, the tail down the back.
def low_ponytail(h):
    locks, tie = combed_to(h, 180, -30)

    def tail(b, heads, level, rng):
        path = [tuple(tie), (0.0, 0.676, tie[2] - 0.020), (0.002, 0.630, tie[2] - 0.028),
                (0.006, 0.575, tie[2] - 0.026), (0.010, 0.520, tie[2] - 0.016), (0.012, 0.485, tie[2] - 0.006)]
        sectors = 8 if level != "crowd" else 5
        b.tube(path, lambda t: 0.020 * (1 - 0.75 * t ** 1.3) + 0.002, sectors, 0)
        n = {"hero": 10, "standard": 6, "crowd": 3}[level]
        for k in range(n):
            ang = 2 * math.pi * k / n
            off = np.array([math.cos(ang), 0.0, math.sin(ang)])
            lock = [tuple(np.array(p) + off * 0.012 * (1 - 0.6 * i / 5) * (1 if i else 0.4)) for i, p in enumerate(path)]
            lock[-1] = tuple(np.array(lock[-1]) + off * 0.006 + np.array([0, -0.01 * rng.random(), 0]))
            b.card(lock, 0.022, 0.0, 5 + k % 3, curl=0.25, taper=0.35, snap=False)
        b.tube([tuple(tie + np.array([0, 0.004, 0.006])), tuple(tie + np.array([0, -0.006, -0.010]))],
               lambda t: 0.017, sectors, 1, v_span=0.05)  # the tie wrap

    return dict(groups=[locks], whorl=None, cut=(0.0, 0.006), extra=tail, sheet=True, mid_layer=False,
                widths=(0.050, 0.034, 0.026), gaps=(0.0065, 0.008, 0.010), tip=0.02, root=0.5)


STYLES = {
    "layered-lob": lob,
    "side-crop": side_crop,
    "choppy-pixie": choppy_pixie,
    "rain-bob": rain_bob,
    "curtain-shag": curtain_shag,
    "tide-knot": tide_knot,
    "low-ponytail": low_ponytail,
}


def trim_root(lock, metres):
    pts = np.array(lock, float)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    if seg[0] <= metres:
        return lock[1:] if len(lock) > 3 else lock
    t = metres / seg[0]
    return [tuple(pts[0] + (pts[1] - pts[0]) * t)] + list(lock[1:])


def build(spec, level, heads):
    fill_out, fill_mid, under_step, rows, across, wscale = LEVELS[level]
    rows = max(4, round(rows * spec.get("rows", 1.0)))
    rng = random.Random(17)
    b = Builder(heads, rows, across)
    b.tipz = spec.get("tip", 0.045)
    w_under, w_mid, w_outer = spec.get("widths", (0.050, 0.036, 0.030))
    g_under, g_mid, g_outer = spec.get("gaps", (0.004, 0.008, 0.0125))
    pull_under, pull_mid = spec.get("under", 0.12), spec.get("mid", 0.06)
    if level == "crowd":
        w_under, w_mid = w_under * 1.4, w_mid * 1.65

    def root_width(lock):
        # Locks fanning from the crown start narrow, so their root ends do not
        # show as flakes over the layer below; part and hairline roots do not.
        if "root" in spec:
            return spec["root"]
        crown = spec.get("whorl")
        if not crown:
            return 0.8
        d = np.linalg.norm(np.array(lock[0]) - np.array(crown[0]))
        return 0.8 if d > crown[1] * 1.6 else 0.35

    def interpolated(group, fill):
        out = []
        for p, q in zip(group[:-1], group[1:]):
            out.append(p)
            for k in range(fill):
                out.append(lerp_lock(p, q, (k + 1) / (fill + 1)))
        out.append(group[-1])
        return out

    if spec.get("whorl"):
        centre, radius = spec["whorl"]
        b.whorl(centre, radius, 8 if level != "crowd" else 5, 3 if level != "crowd" else 2)
    for group in spec["groups"]:
        if spec.get("sheet"):
            # Swept back: a continuous under-surface, then cards whose roots
            # start inside the hairline so it stays one clean edge.
            b.sheet(interpolated(group, 1 if level != "crowd" else 0), max(g_under, 0.0065), 0)
            group = [trim_root(lock, 0.012) for lock in group]
        for i, lock in enumerate([] if spec.get("sheet") else group[::under_step]):
            b.card(scale_in(shorten(lock, 0.012 * min(1.0, pull_under / 0.12)), pull_under), w_under, g_under,
                   i % 2, curl=0.08, taper=0.8)
        for k, lock in enumerate(interpolated(group, fill_mid) if spec.get("mid_layer", True) else []):
            b.card(scale_in(lock, pull_mid), w_mid, g_mid, 2 + k % 3, curl=0.14, root=root_width(lock))
        lo, hi = spec.get("cut", (0.012, 0.050))
        for k, lock in enumerate(interpolated(group, fill_out)):
            front = lock[0][2] > 0.06 and lock[1][1] > 0.78
            cut = rng.uniform(lo, hi) if k % 3 else rng.uniform(0.0, lo + 0.25 * (hi - lo))
            width = (0.8 if front else 1.0) * w_outer * wscale
            b.card(shorten(lock, cut), width, g_outer, 5 + k % 3, curl=0.18, taper=0.45,
                   flare=spec.get("flare", 0.0), root=root_width(lock), v0=0.35 if spec.get("sheet") else 0.0)
    if spec.get("extra"):
        spec["extra"](b, heads, level, rng)
    return b.arrays()


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    head_f, head_m, out_dir = args[:3]
    only = args[args.index("--only") + 1].split(",") if "--only" in args else list(STYLES)
    atlas = args[args.index("--atlas") + 1] if "--atlas" in args else None
    prev = args[args.index("--preview") + 1] if "--preview" in args else None
    heads = Heads([head_f, head_m])
    for gid in only:
        spec = STYLES[gid](heads)
        result = {}
        for level in ("hero", "standard", "crowd"):
            pos, tri, uv = build(spec, level, heads)
            result[level + "_pos"] = pos.astype("<f4")
            result[level + "_tri"] = tri.astype("<i4")
            result[level + "_uv"] = uv.astype("<f4")
            print(gid, level, len(tri), "tris")
        np.savez_compressed(os.path.join(out_dir, gid + ".npz"), **result)
        if prev and atlas:
            for level in ("hero", "standard", "crowd"):
                arrays = (result[level + "_pos"], result[level + "_tri"], result[level + "_uv"])
                preview(heads, arrays, atlas, prev, gid + "_" + level,
                        head=1 if gid in ("side-crop",) else 0)
                if "--hero-only" in args:
                    break


main()

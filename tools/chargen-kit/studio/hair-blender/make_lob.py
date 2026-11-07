"""Model the layered lob in Blender: authored guide locks -> layered hair cards.

    blender -b --factory-startup --python make_lob.py -- <head_f.obj> <head_m.obj> <out.npz>
            [--atlas strands.png] [--preview <dir>]

The heads are the kit's hero meshes in kit space (Y up, metres, +Z the face,
+X the character's left); `patch_hair.py --heads` writes them. Everything here
is in that space, and the npz holds it unsculpted for studio_gear.

The cut is a chin-to-collarbone lob with a deep side part on the character's
left: a long fringe sweeps across the forehead to the right and falls as a
face-framing piece; the back falls from a crown whorl. The shape is the GUIDE
LOCKS below - each a hand-placed path from root to tip. Cards fill between
neighbouring guides in three layers, as a groom artist would lay them:

    under   wide, opaque strips 0-1, 4 mm off the scalp - no skin through gaps
    mid     strips 2-4, the body of the cut, full length
    outer   strips 5-7, narrower, interpolated twice, layered shorter

Every point is kept clear of BOTH heads (the larger of the two at each place),
so the groom fits either sex before the generator's skull fit.
"""
import math
import random
import sys

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

TEX, TILE = 512, 64

# ---------------------------------------------------------------------------
# Guide locks, kit coordinates (x left, y up, z front), root first.
PART_X = 0.024

BIG_SIDE = [  # from the part across the forehead to the character's right
    [(PART_X + 0.011, 0.795, 0.124), (-0.012, 0.792, 0.151), (-0.055, 0.766, 0.146),
     (-0.086, 0.716, 0.108), (-0.099, 0.656, 0.090), (-0.096, 0.596, 0.082)],
    [(PART_X + 0.011, 0.806, 0.106), (-0.020, 0.810, 0.136), (-0.066, 0.787, 0.122),
     (-0.096, 0.732, 0.084), (-0.105, 0.662, 0.068), (-0.101, 0.598, 0.060)],
    [(PART_X + 0.011, 0.816, 0.086), (-0.026, 0.824, 0.110), (-0.074, 0.802, 0.090),
     (-0.102, 0.742, 0.058), (-0.108, 0.668, 0.044), (-0.103, 0.601, 0.038)],
    [(PART_X + 0.011, 0.826, 0.062), (-0.031, 0.835, 0.080), (-0.080, 0.810, 0.055),
     (-0.105, 0.748, 0.030), (-0.109, 0.671, 0.018), (-0.104, 0.603, 0.014)],
    [(0.020, 0.834, 0.036), (-0.031, 0.841, 0.044), (-0.082, 0.814, 0.021),
     (-0.106, 0.751, 0.000), (-0.108, 0.673, -0.007), (-0.102, 0.605, -0.010)],
]
SMALL_SIDE = [  # from the part toward the left ear
    [(PART_X - 0.008, 0.796, 0.124), (0.060, 0.786, 0.138), (0.086, 0.746, 0.114),
     (0.098, 0.690, 0.094), (0.101, 0.640, 0.084), (0.097, 0.598, 0.078)],
    [(PART_X - 0.008, 0.808, 0.104), (0.064, 0.801, 0.113), (0.093, 0.756, 0.082),
     (0.105, 0.692, 0.062), (0.105, 0.640, 0.055), (0.100, 0.600, 0.050)],
    [(PART_X - 0.008, 0.820, 0.080), (0.069, 0.812, 0.082), (0.098, 0.760, 0.050),
     (0.107, 0.692, 0.031), (0.106, 0.640, 0.024), (0.101, 0.601, 0.020)],
    [(PART_X - 0.008, 0.830, 0.054), (0.071, 0.818, 0.049), (0.100, 0.762, 0.020),
     (0.108, 0.692, 0.002), (0.106, 0.641, -0.005), (0.101, 0.602, -0.009)],
]
WHORL = (0.006, 0.837, 0.010)


def back_lock(deg, drop=0.0):
    """A lock fanning from the crown whorl down the back at azimuth deg
    (0 = face, 90 = left, 180 = back)."""
    a = math.radians(deg)
    s, c = math.sin(a), math.cos(a)
    cz = 0.036
    pts = [(WHORL[0] + 0.030 * s, WHORL[1] - 0.003, WHORL[2] + 0.034 * c)]
    for y, rx, rz in ((0.816, 0.058, 0.066), (0.766, 0.093, 0.099),
                      (0.704, 0.104, 0.099), (0.648, 0.098, 0.091),
                      (0.602 - drop, 0.086, 0.079)):
        pts.append((rx * s, y, cz + rz * c))
    return pts


BACK = [back_lock(d, 0.004 * math.cos(math.radians(d - 180)))
        for d in (124, 142, 160, 180, 200, 218, 236)]

# Around the head, in order; neighbours are interpolated. The face is the gap.
RING = SMALL_SIDE + BACK + BIG_SIDE[::-1]

LEVELS = {
    #           outer-fill mid-fill under-step rows across
    "hero":     (2, 1, 1, 12, 3),
    "standard": (1, 0, 2, 8, 3),
    "crowd":    (0, 0, 1, 6, 2),
}


# ---------------------------------------------------------------------------
def k2b(p):
    return (p[0], -p[2], p[1])


def load_head(path):
    V, F = [], []
    for line in open(path):
        if line.startswith("v "):
            V.append(tuple(map(float, line.split()[1:4])))
        elif line.startswith("f "):
            F.append([int(i) - 1 for i in line.split()[1:4]])
    return V, F


class Heads:
    def __init__(self, paths):
        self.trees = []
        self.meshes = []
        for p in paths:
            V, F = load_head(p)
            self.trees.append(BVHTree.FromPolygons([Vector(v) for v in V], F))
            self.meshes.append((V, F))

    def clear(self, p, gap):
        """Push p out until it is gap metres outside every head."""
        p = Vector(p)
        for _ in range(3):
            moved = False
            for tree in self.trees:
                hit, normal, _, dist = tree.find_nearest(p)
                if hit is None:
                    continue
                d = (p - hit)
                outside = d.dot(normal) > 0
                if not outside or dist < gap:
                    p = hit + normal * gap
                    moved = True
            if not moved:
                break
        return p

    @staticmethod
    def outward(p):
        """Radial from the skull centre above it, from the neck axis below:
        smooth over the ears, where nearest-surface normals flip."""
        p = np.asarray(p, float)
        centre = np.array([0.0, 0.745, 0.045])
        if p[1] < centre[1]:
            centre = np.array([0.0, p[1], 0.045])
        d = p - centre
        return d / max(np.linalg.norm(d), 1e-9)

    def normal(self, p):
        best = None
        for tree in self.trees:
            hit, normal, _, dist = tree.find_nearest(Vector(p))
            if hit is not None and (best is None or dist < best[0]):
                best = (dist, normal)
        return np.array(best[1]) if best else np.array([0.0, 1.0, 0.0])


def brow(p):
    """Extra clearance over the forehead: generated brows and foreheads
    stand further forward than the reference heads, and the skull fit does
    not follow them - 7 mm keeps the fringe roots out of the skin."""
    front = min(1.0, max(0.0, (p[2] - 0.075) / 0.03))
    low = min(1.0, max(0.0, (0.822 - p[1]) / 0.015))
    return 0.0075 * front * low


def spline(points, n, bias=1.0):
    """Catmull-Rom through the guide points, resampled; bias > 1 packs the
    samples toward the root, where a lock bends over the skull."""
    P = np.asarray(points, float)
    P = np.vstack([2 * P[0] - P[1], P, 2 * P[-1] - P[-2]])
    dense = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for t in np.linspace(0, 1, 16, endpoint=False):
            t2, t3 = t * t, t * t * t
            dense.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                                + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    dense.append(P[-2])
    dense = np.array(dense)
    seg = np.linalg.norm(np.diff(dense, axis=0), axis=1)
    acc = np.concatenate(([0], np.cumsum(seg)))
    s = acc[-1] * np.linspace(0, 1, n) ** bias
    return np.stack([np.interp(s, acc, dense[:, k]) for k in range(3)], -1)


def lerp_lock(a, b, t):
    return [tuple((1 - t) * np.array(p) + t * np.array(q)) for p, q in zip(a, b)]


def scale_in(lock, k):
    """The same lock pulled toward the head axis by k (under/mid layers)."""
    out = []
    for i, (x, y, z) in enumerate(lock):
        w = min(1.0, i / 2.0)
        cz = 0.04
        out.append((x * (1 - k * w), y - 0.004 * w, cz + (z - cz) * (1 - k * w)))
    return out


def shorten(lock, metres):
    pts = spline(lock, 64)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    acc = np.concatenate(([0], np.cumsum(seg)))
    keep = acc <= acc[-1] - metres
    return [tuple(p) for p in pts[keep][:: max(1, keep.sum() // 6)]] + [tuple(pts[keep][-1])]


class Builder:
    def __init__(self, heads, rows, across):
        self.heads, self.rows, self.across = heads, rows, across
        self.pos, self.tri, self.uv = [], [], []

    def card(self, lock, width, gap, tile, curl=0.16, taper=0.5, rng=None):
        rows, across = self.rows, self.across
        line = spline(lock, rows, 1.5)
        line = np.array([self.heads.clear(p, max(gap * min(1.0, 0.5 + i / 2.5), 0.005, brow(p)))
                         for i, p in enumerate(line)])
        # Roots sit ON the scalp, whatever the guide said.
        best = None
        for tree in self.heads.trees:
            hit, normal, _, dist = tree.find_nearest(Vector(line[0]))
            if hit is not None and (best is None or dist < best[0]):
                best = (dist, hit, normal)
        if best is not None:
            line[0] = np.array(best[1] + best[2] * max(0.005, brow(line[0])))
        # Smooth once after the push so a cleared point does not kink the card.
        line[1:-1] = 0.25 * line[:-2] + 0.5 * line[1:-1] + 0.25 * line[2:]
        tang = np.gradient(line, axis=0)
        tang /= np.linalg.norm(tang, axis=1, keepdims=True)
        length = float(np.sum(np.linalg.norm(np.diff(line, axis=0), axis=1)))
        base = len(self.pos)
        t = np.concatenate(([0.0], np.cumsum(np.linalg.norm(np.diff(line, axis=0), axis=1)))) / max(length, 1e-6)
        q = np.linspace(-1, 1, across)
        for r in range(rows):
            n = self.heads.outward(line[r])
            n = n - tang[r] * np.dot(n, tang[r])
            n /= max(np.linalg.norm(n), 1e-9)
            side = np.cross(tang[r], n)
            half = 0.5 * width * (0.80 + 0.20 * math.sin(math.pi * min(t[r] * 1.8, 1.0)) ** 0.5)
            half *= 1 - (1 - taper) * max(0.0, (t[r] - 0.65) / 0.35) ** 1.5
            for c in q:
                lift = curl * half * (1 - c * c) - 0.002 * (1 - t[r]) ** 6
                self.pos.append(line[r] + side * c * half + n * lift)
        # The last 4.5 cm always land on the strip's tip zone (bottom 28%).
        acc = np.concatenate(([0.0], np.cumsum(np.linalg.norm(np.diff(line, axis=0), axis=1))))
        tipz = 0.045
        v = np.where(acc < length - tipz, acc / max(length - tipz, 1e-3) * 0.72,
                     0.72 + (acc - (length - tipz)) / tipz * 0.28)
        x0 = tile * TILE + 1.5
        u = (x0 + (q + 1) / 2 * (TILE - 3)) / TEX
        for r in range(rows - 1):
            for c in range(across - 1):
                a = base + r * across + c
                quad = (a, a + 1, a + across + 1, a + across)
                for f in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                    self.tri.append(f)
                    self.uv.append([(u[(i - base) % across],
                                     1 - (2 + v[(i - base) // across] * (TEX - 4)) / TEX) for i in f])

    def onto_scalp(self, p, gap):
        """The scalp point in the direction of p seen from the skull centre."""
        origin = Vector((0.0, 0.745, 0.045))
        d = (Vector(p) - origin).normalized()
        far = None
        for tree in self.heads.trees:
            hit = tree.ray_cast(origin, d, 0.3)
            if hit[0] is not None and (far is None or (hit[0] - origin).length > (far - origin).length):
                far = hit[0]
        return np.array(far + d * gap) if far is not None else np.array(p)

    def whorl(self, centre, radius, sectors, rings):
        """A dome patch over the crown, mapped so each sector is one strip
        width and v runs out from the centre: the hair radiates from the whorl."""
        c = np.array(centre)
        base = len(self.pos)
        grid = []
        for i in range(rings + 1):
            r = radius * i / rings
            row = []
            for k in range(sectors + 1):
                a = 2 * math.pi * k / sectors
                p = c + np.array([r * math.sin(a), 0.0, r * math.cos(a)])
                row.append(self.onto_scalp(p, 0.0035))
            grid.append(row)
        W = sectors + 1
        for row in grid:
            self.pos.extend(row)
        for i in range(rings):
            for k in range(sectors):
                a = base + i * W + k
                quad = (a, a + 1, a + W + 1, a + W)
                for f in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                    self.tri.append(f)
                    corners = []
                    for idx in f:
                        ii, kk = divmod(idx - base, W)
                        u = (1.5 + (kk - k) * (TILE - 3)) / TEX  # strip 0
                        v = 0.45 * ii / rings
                        corners.append((u, 1 - (2 + v * (TEX - 4)) / TEX))
                    self.uv.append(corners)

    def arrays(self):
        pos = np.array(self.pos)
        tri = np.array(self.tri, np.int64)
        uv = np.array(self.uv, float)
        # Face every triangle away from the head (vertex normals follow winding).
        n = np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]])
        flip = np.array([np.dot(n[i], self.heads.outward(pos[f].mean(0))) < 0
                         for i, f in enumerate(tri)])
        tri[flip] = tri[flip][:, ::-1]
        uv[flip] = uv[flip][:, ::-1]
        return pos, tri, uv


def build(level, heads):
    fill_out, fill_mid, under_step, rows, across = LEVELS[level]
    rng = random.Random(17)
    b = Builder(heads, rows, across)
    ring = RING
    pairs = list(zip(ring[:-1], ring[1:]))

    def interpolated(fill):
        locks = []
        for i, (p, q) in enumerate(pairs):
            locks.append((p, i))
            for k in range(fill):
                locks.append((lerp_lock(p, q, (k + 1) / (fill + 1)), i))
        locks.append((ring[-1], len(pairs)))
        return locks

    # The crown whorl: strands radiating from one point, under everything.
    b.whorl(WHORL, 0.050, 8 if level != "crowd" else 5, 3 if level != "crowd" else 2)
    # Under: wide opaque strips, every under_step-th guide, pulled in.
    for i, lock in enumerate(ring[::under_step]):
        b.card(scale_in(shorten(lock, 0.012), 0.12), 0.050 if level != "crowd" else 0.07,
               0.004, i % 2, curl=0.08, taper=0.8)
    # Mid: the body, every guide and the mid fill.
    if fill_mid >= 0:
        for k, (lock, i) in enumerate(interpolated(fill_mid)):
            b.card(scale_in(lock, 0.06), 0.036 if level != "crowd" else 0.06, 0.008, 2 + k % 3, curl=0.14)
    # Outer: layered, shorter by up to 3.5 cm, narrower at the front.
    for k, (lock, i) in enumerate(interpolated(fill_out)):
        front = lock[0][2] > 0.06 and lock[1][1] > 0.78
        cut = rng.uniform(0.012, 0.050) if k % 3 else rng.uniform(0.0, 0.012)
        width = (0.024 if front else 0.030) * (1.0 if level == "hero" else 1.25 if level == "standard" else 1.7)
        b.card(shorten(lock, cut), width, 0.0125, 5 + k % 3, curl=0.18, taper=0.45)
    return b.arrays()


# ---------------------------------------------------------------------------
# Preview: the head in clay, the hair alpha-clipped with the baked atlas, dyed.
def preview(heads, arrays, atlas, outdir, tag):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 32
    sc.render.resolution_x, sc.render.resolution_y = 600, 700
    V, F = heads.meshes[0]
    me = bpy.data.meshes.new("head")
    me.from_pydata([k2b(v) for v in V], [], F)
    me.update()
    for poly in me.polygons:
        poly.use_smooth = True
    head = bpy.data.objects.new("head", me)
    sc.collection.objects.link(head)
    skin = bpy.data.materials.new("skin")
    skin.use_nodes = True
    skin.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.62, 0.45, 0.37, 1)
    head.data.materials.append(skin)

    pos, tri, uv = arrays
    me = bpy.data.meshes.new("hair")
    me.from_pydata([k2b(p) for p in pos], [], [tuple(int(i) for i in t) for t in tri])
    me.update()
    layer = me.uv_layers.new()
    for poly, corners in zip(me.polygons, uv):
        for li, c in zip(poly.loop_indices, corners):
            layer.data[li].uv = (float(c[0]), float(c[1]))
    for poly in me.polygons:
        poly.use_smooth = True
    hair = bpy.data.objects.new("hair", me)
    sc.collection.objects.link(hair)
    m = bpy.data.materials.new("hair")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    img = nt.nodes.new("ShaderNodeTexImage")
    img.image = bpy.data.images.load(atlas)
    img.interpolation = "Closest"
    # Alpha test at 50%, like the console.
    gt = nt.nodes.new("ShaderNodeMath")
    gt.operation = "GREATER_THAN"
    gt.inputs[1].default_value = 0.5
    nt.links.new(img.outputs["Alpha"], gt.inputs[0])
    nt.links.new(gt.outputs[0], bsdf.inputs["Alpha"])
    tint = nt.nodes.new("ShaderNodeMix")
    tint.data_type = "RGBA"
    tint.blend_type = "MULTIPLY"
    tint.inputs["Factor"].default_value = 1.0
    tint.inputs["B"].default_value = (1.3, 0.75, 0.45, 1)
    nt.links.new(img.outputs["Color"], tint.inputs["A"])
    nt.links.new(tint.outputs["Result"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.6
    hair.data.materials.append(m)

    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.30, 0.33, 0.36, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
    sc.world = world
    key = bpy.data.objects.new("key", bpy.data.lights.new("key", "SUN"))
    key.data.energy = 3.0
    key.rotation_euler = (math.radians(50), 0, math.radians(-30))
    sc.collection.objects.link(key)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 85
    sc.collection.objects.link(cam)
    sc.camera = cam
    target = Vector((0, -0.04, 0.71))
    shots = []
    for name, az, el in (("front", 0, 5), ("three", 35, 10), ("side", 90, 3),
                         ("back", 180, 8), ("rear34", 215, 15), ("top", 20, 60), ("part", 0, 25)):
        a, e = math.radians(az), math.radians(el)
        d = 0.95 if name != "part" else 0.40
        cam.location = target + Vector((d * math.sin(a) * math.cos(e), -d * math.cos(a) * math.cos(e),
                                        d * math.sin(e)))
        cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
        path = "%s/%s_%s.png" % (outdir, tag, name)
        sc.render.filepath = path
        bpy.ops.render.render(write_still=True)
        shots.append(path)
    print("preview", shots)


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    head_f, head_m, out = args[:3]
    atlas = args[args.index("--atlas") + 1] if "--atlas" in args else None
    prev = args[args.index("--preview") + 1] if "--preview" in args else None
    heads = Heads([head_f, head_m])
    result = {}
    for level in ("hero", "standard", "crowd"):
        pos, tri, uv = build(level, heads)
        result[level + "_pos"] = pos.astype("<f4")
        result[level + "_tri"] = tri.astype("<i4")
        result[level + "_uv"] = uv.astype("<f4")
        print(level, len(tri), "tris", len(pos), "verts")
    np.savez_compressed(out, **result)
    if prev and atlas:
        for level in ("hero", "standard", "crowd"):
            arrays = (result[level + "_pos"], result[level + "_tri"], result[level + "_uv"])
            preview(heads, arrays, atlas, prev, level)
            if "--hero-only" in args:
                break


main()

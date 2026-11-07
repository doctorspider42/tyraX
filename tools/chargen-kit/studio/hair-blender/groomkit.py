"""Shared tools for the hair modelled in Blender (grooms.py holds the styles).

Kit space throughout: Y up, metres, +Z the face, +X the character's left.
The heads are the kit's hero meshes (`patch_hair.py --heads`).

    Heads      both reference heads as BVH trees; `clear` keeps a point a gap
               outside both, `outward` is the smooth card orientation
    S, sweep   a point on the scalp by azimuth/elevation from the skull centre,
               and a lock that lies along the scalp - the short-hair primitive
    spline     Catmull-Rom through guide points, resampled (root-biased)
    Builder    cards along locks (curved cross-section, tip zone mapped to the
               strip's alpha tips), the crown whorl dome, `tube` for a tail or
               a coil, and winding turned away from the head
    preview    clay head + alpha-tested, dyed cards, seven Cycles views
"""
import math

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

TEX, TILE = 512, 64
ORIGIN = (0.0, 0.745, 0.045)  # skull centre the angles are measured from

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


def S(heads, az, el, off=0.0):
    """The scalp point at azimuth az (degrees, 0 = face, 90 = the character's
    left) and elevation el (degrees above the skull centre), off metres out."""
    a, e = math.radians(az), math.radians(el)
    d = Vector((math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)))
    o = Vector(ORIGIN)
    far = None
    for tree in heads.trees:
        hit = tree.ray_cast(o, d, 0.3)
        if hit[0] is not None and (far is None or (hit[0] - o).length > (far - o).length):
            far = hit[0]
    if far is None:
        far = o + d * 0.09
    return tuple(far + d * off)


def sweep(heads, root, end, lift, n=5, rise=0.6, end_lift=None):
    """A lock lying along the scalp from root (az, el) to end (az, el): it
    leaves the root, stands up to `lift` metres at `rise` of the way and
    settles to end_lift (default lift / 2) - short hair, combed."""
    end_lift = lift * 0.5 if end_lift is None else end_lift
    pts = []
    for i in range(n):
        t = i / (n - 1)
        az = root[0] + (end[0] - root[0]) * t
        el = root[1] + (end[1] - root[1]) * t
        up = lift * math.sin(math.pi * 0.5 * min(t / rise, 1.0)) if t <= rise else \
            lift + (end_lift - lift) * ((t - rise) / (1 - rise)) ** 1.5
        pts.append(S(heads, az, el, 0.002 + up))
    return pts


def direction(az, el):
    a, e = math.radians(az), math.radians(el)
    return np.array([math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)])


def S_dir(heads, d, off=0.0):
    """S() for a direction vector from the skull centre."""
    d = d / np.linalg.norm(d)
    return S(heads, math.degrees(math.atan2(d[0], d[2])), math.degrees(math.asin(max(-1.0, min(1.0, d[1])))), off)


def slerp(a, b, t):
    om = math.acos(max(-1.0, min(1.0, float(np.dot(a, b)))))
    if om < 1e-4:
        return a
    return (math.sin((1 - t) * om) * a + math.sin(t * om) * b) / math.sin(om)


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

    def card(self, lock, width, gap, tile, curl=0.16, taper=0.5, snap=True, flare=0.0, root=0.8, v0=0.0):
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
        if best is not None and snap:
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
            half = 0.5 * width * (root + (1 - root) * math.sin(math.pi * min(t[r] * 1.8, 1.0)) ** 0.5)
            half *= 1 - (1 - taper) * max(0.0, (t[r] - 0.65) / 0.35) ** 1.5
            for c in q:
                # flare > 0 lifts the free end off the layer below (a flick).
                lift = (curl * half * (1 - c * c) - 0.002 * (1 - t[r]) ** 6
                        + flare * max(0.0, (t[r] - 0.55) / 0.45) ** 2)
                self.pos.append(line[r] + side * c * half + n * lift)
        # The last 4.5 cm always land on the strip's tip zone (bottom 28%).
        acc = np.concatenate(([0.0], np.cumsum(np.linalg.norm(np.diff(line, axis=0), axis=1))))
        tipz = getattr(self, "tipz", 0.045)
        v = np.where(acc < length - tipz, v0 + acc / max(length - tipz, 1e-3) * (0.72 - v0),
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

    def sheet(self, locks, gap, tile, rows=None, v_span=0.70, v0=0.42):
        """One continuous surface lofted across neighbouring locks (root row
        on the hairline): swept-back hair has no gaps for skin to show through.
        Each column of quads maps across the strip, mirrored every other one."""
        R = rows or self.rows
        lines = []
        for lock in locks:
            line = spline(lock, R, 1.2)
            lines.append([np.array(self.heads.clear(p, max(gap, brow(p)))) for p in line])
        base = len(self.pos)
        for line in lines:
            self.pos.extend(line)
        x0 = tile * TILE + 1.5
        for i in range(len(lines) - 1):
            for r in range(R - 1):
                a, b = base + i * R + r, base + (i + 1) * R + r
                quad = (a, b, b + 1, a + 1)
                for f in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                    self.tri.append(f)
                    corners = []
                    for idx in f:
                        col, row = divmod(idx - base, R)
                        u = (x0 + ((col - i) if i % 2 == 0 else 1 - (col - i)) * (TILE - 3)) / TEX
                        v = v0 + (v_span - v0) * row / (R - 1)
                        corners.append((u, 1 - (2 + v * (TEX - 4)) / TEX))
                    self.uv.append(corners)

    def tube(self, path, radius, sectors, tile, v_span=0.70, v0=0.25):
        """A closed tube along path (a ponytail core, a bun coil). radius(t)
        in metres; one strip wraps the circumference."""
        line = spline(path, max(4, len(path) * 3))
        rows = len(line)
        tang = np.gradient(line, axis=0)
        tang /= np.linalg.norm(tang, axis=1, keepdims=True)
        ref = np.array([1.0, 0.0, 0.0])
        base = len(self.pos)
        for r in range(rows):
            t = r / (rows - 1)
            a = np.cross(tang[r], ref)
            if np.linalg.norm(a) < 1e-3:
                a = np.cross(tang[r], [0.0, 0.0, 1.0])
            a /= np.linalg.norm(a)
            b = np.cross(tang[r], a)
            for k in range(sectors + 1):
                ang = 2 * math.pi * k / sectors
                self.pos.append(line[r] + radius(t) * (math.cos(ang) * a + math.sin(ang) * b))
        W = sectors + 1
        x0 = tile * TILE + 1.5
        for r in range(rows - 1):
            for k in range(sectors):
                i = base + r * W + k
                quad = (i, i + 1, i + W + 1, i + W)
                for f in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                    self.tri.append(f)
                    corners = []
                    for idx in f:
                        rr, kk = divmod(idx - base, W)
                        u = (x0 + ((kk - k) if kk - k in (0, 1) else 0) * (TILE - 3)) / TEX
                        v = v0 + (v_span - v0) * rr / (rows - 1)
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


# ---------------------------------------------------------------------------
# Preview: the head in clay, the hair alpha-clipped with the baked atlas, dyed.
def preview(heads, arrays, atlas, outdir, tag, head=0, dye=(1.3, 0.75, 0.45)):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 32
    sc.render.resolution_x, sc.render.resolution_y = 600, 700
    V, F = heads.meshes[head]
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
    tint.inputs["B"].default_value = (*dye, 1)
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



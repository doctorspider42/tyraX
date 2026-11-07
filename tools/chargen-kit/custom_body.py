"""Build the trunk and limbs from anatomical sections in rig rest space.

The face, articulated hands and feet attach at five closed borders. Source
surface correspondence transfers the kit's skin paint, joint weights and
slider deltas after the new surface has been built.
"""

from collections import defaultdict
import numpy as np
from studio_gear import bindings, unit

# Height, half width, front and rear extent of the neutral rib cage and pelvis.
TRUNK = [
    (0.040, 0.165, 0.125, -0.087),
    (0.077, 0.168, 0.138, -0.082),
    (0.12, 0.162, 0.145, -0.067),
    (0.167, 0.147, 0.148, -0.049),
    (0.21, 0.132, 0.147, -0.033),
    (0.25, 0.126, 0.143, -0.022),
    (0.29, 0.127, 0.150, -0.023),
    (0.325, 0.133, 0.155, -0.028),
    (0.36, 0.140, 0.160, -0.035),
    (0.398, 0.146, 0.150, -0.043),
    (0.434, 0.151, 0.132, -0.051),
    (0.46, 0.161, 0.125, -0.057),
    (0.493, 0.172, 0.118, -0.054),
    (0.525, 0.179, 0.103, -0.051),
    (0.551, 0.150, 0.079, -0.040),
    (0.567, 0.103, 0.071, -0.037),
    (0.578, 0.073, 0.067, -0.030),
    (0.589, 0.054, 0.069, -0.028),
    (0.599, 0.052, 0.075, -0.026),
]
# Each leg section has a lateral centre, width and separate front/back depths.
LEG = [
    (-0.699, 0.223, 0.032, 0.039, -0.038),
    (-0.66, 0.215, 0.034, 0.038, -0.042),
    (-0.608, 0.207, 0.038, 0.036, -0.053),
    (-0.55, 0.197, 0.046, 0.038, -0.061),
    (-0.502, 0.188, 0.051, 0.047, -0.062),
    (-0.46, 0.179, 0.051, 0.057, -0.055),
    (-0.414, 0.169, 0.046, 0.067, -0.042),
    (-0.38, 0.161, 0.047, 0.079, -0.034),
    (-0.351, 0.154, 0.050, 0.078, -0.033),
    (-0.315, 0.148, 0.051, 0.085, -0.034),
    (-0.267, 0.14, 0.058, 0.094, -0.041),
    (-0.205, 0.131, 0.067, 0.107, -0.049),
    (-0.141, 0.124, 0.075, 0.115, -0.057),
    (-0.084, 0.114, 0.081, 0.119, -0.064),
    (-0.036, 0.102, 0.086, 0.116, -0.071),
]
# Shoulder to wrist. Radii lie in planes perpendicular to the arm.
ARM = [
    (0.202, 0.495, 0.017, 0.055, 0.058),
    (0.222, 0.471, 0.019, 0.054, 0.053),
    (0.248, 0.442, 0.018, 0.050, 0.050),
    (0.273, 0.408, 0.017, 0.044, 0.049),
    (0.292, 0.375, 0.016, 0.037, 0.040),
    (0.31, 0.350, 0.017, 0.034, 0.037),
    (0.326, 0.336, 0.039, 0.036, 0.038),
    (0.348, 0.316, 0.071, 0.037, 0.038),
    (0.37, 0.291, 0.101, 0.035, 0.035),
    (0.393, 0.266, 0.135, 0.027, 0.030),
    (0.411, 0.25, 0.156, 0.022, 0.026),
]


def bump(value, center, width):
    return np.exp(-(((value - center) / width) ** 2))


def border_loops(faces, removed):
    def edges(tris):
        return {tuple(sorted((a, b))) for t in tris for a, b in zip(t, np.roll(t, -1))}

    adjacent = defaultdict(list)
    for a, b in edges(faces) & edges(removed):
        adjacent[a].append(b)
        adjacent[b].append(a)
    loops = []
    while adjacent:
        first = next(iter(adjacent))
        ring = [first]
        while True:
            neighbors = adjacent.pop(ring[-1])
            assert len(neighbors) == 2
            nxt = next((v for v in neighbors if v in adjacent), None)
            if nxt is None:
                break
            ring.append(nxt)
        loops.append(ring)
    return loops


class Surface:
    def __init__(self, pos):
        self.points = list(pos)
        self.faces = []

    def ring(self, points):
        start = len(self.points)
        self.points.extend(points)
        return list(range(start, len(self.points)))

    def bridge(self, a, b):
        # Align closed borders, then zipper them by arc length. This permits
        # sparse limb rings to meet denser shoulder and hip loops.
        pa = np.array([self.points[i] for i in a])
        pb = np.array([self.points[i] for i in b])
        da = unit(pa - pa.mean(0))
        db = unit(pb - pb.mean(0))
        k = np.argmax(db @ da[0])
        step = 1 if (db[(k + 1) % len(b)] - db[k - 1]) @ (da[1] - da[-1]) > 0 else -1
        b = [b[(k + step * i) % len(b)] for i in range(len(b))]

        def arc(ids):
            p = np.array([self.points[i] for i in ids])
            d = np.cumsum(np.linalg.norm(np.roll(p, -1, axis=0) - p, axis=1))
            return d / d[-1]

        ta, tb = arc(a), arc(b)
        i = j = 0
        while i < len(a) or j < len(b):
            l, r = a[i % len(a)], b[j % len(b)]
            if j == len(b) or (i < len(a) and ta[i] <= tb[j]):
                i += 1
                self.faces.append((l, r, a[i % len(a)]))
            else:
                j += 1
                self.faces.append((l, r, b[j % len(b)]))


def draft(pos, tri, keep, level, sex):
    surface = Surface(pos)
    borders = {}
    for ring in border_loops(tri[keep], tri[~keep]):
        x, y, _ = pos[ring].mean(0)
        key = "neck" if y > 0.55 else "wrist" if y > 0 else "ankle"
        borders[key, 0 if key == "neck" else int(np.sign(x))] = ring
    n, limb, stride = {
        "hero": (48, 24, 1),
        "standard": (24, 12, 2),
        "crowd": (16, 8, 2),
    }[level]
    a = 2 * np.pi * np.arange(n) / n
    sn, cs = np.sin(a), np.cos(a)
    trunk = []
    male = 1 if sex == "m" else 0.5 if not sex else 0
    sections = {
        "crowd": (0, 3, 5, 8, 10, 12, 14, 16, 18),
        "standard": (0, 2, 4, 6, 8, 9, 10, 12, 14, 15, 17, 18),
        "hero": tuple(range(len(TRUNK))),
    }[level]
    for section in sections:
        y, w, front, back = TRUNK[section]
        # Oblique rib margin, paired clavicles and the shallow channel down
        # the back. Keep the form legible without a painted muscle outline.
        w += 0.006 * male * bump(y, 0.41, 0.10) - 0.005 * (1 - male) * bump(
            y, 0.22, 0.075
        )
        x = w * sn
        z = (front + back) / 2 + (front - back) / 2 * cs
        ax = abs(x)
        z += np.maximum(cs, 0) * (
            (0.019 + 0.027 * (1 - male))
            * bump(ax, 0.073, 0.048)
            * bump(y, 0.394, 0.048)
            - 0.009 * bump(ax, 0.072, 0.05) * bump(y, 0.349, 0.019)
            + 0.013 * bump(y, 0.513 + 0.11 * ax, 0.013) * bump(ax, 0.086, 0.068)
            - 0.006 * bump(x, 0, 0.025) * bump(y, 0.438, 0.074)
            - 0.005 * bump(y, 0.291 + 0.40 * ax, 0.021)
            + 0.005 * bump(ax, 0.11, 0.03) * bump(y, 0.122, 0.044)
        )
        z += np.maximum(-cs, 0) * (
            0.008 * bump(x, 0, 0.019)
            - 0.014 * bump(ax, 0.083, 0.044) * bump(y, 0.051, 0.070)
            - 0.007 * bump(ax, 0.086, 0.039) * bump(y, 0.456, 0.071)
        )
        # Collar and trapezius rise towards the neck. The lower rib and
        # iliac crests cross the horizontal sections on a shallow diagonal.
        height = (
            y
            - 0.018 * bump(ax, 0.16, 0.055) * bump(y, 0.547, 0.046)
            + 0.011 * (ax / 0.16) ** 2 * bump(y, 0.115, 0.06)
            + 0.007 * (1 - cs**2) * bump(y, 0.304, 0.040)
        )
        trunk.append(surface.ring(np.column_stack((x, height, z))))
    low, high = sections.index(10), sections.index(14)
    span = n // 8
    for j, (lower, upper) in enumerate(zip(trunk, trunk[1:])):
        for i in range(n):
            if low <= j < high and any(
                k - span <= i < k + span for k in (n // 4, 3 * n // 4)
            ):
                continue
            k = (i + 1) % n
            surface.faces.extend(
                [(lower[i], lower[k], upper[k]), (lower[i], upper[k], upper[i])]
            )
    surface.bridge(trunk[-1], borders["neck", 0])
    a = 2 * np.pi * np.arange(limb) / limb
    sn, cs = np.sin(a), np.cos(a)
    legs = {}
    for side, idx in [(1, n // 4), (-1, 3 * n // 4)]:
        l, r = idx - span, idx + span
        socket = (
            trunk[low][l : r + 1]
            + [trunk[j][r] for j in range(low + 1, high)]
            + trunk[high][l : r + 1][::-1]
            + [trunk[j][l] for j in range(high - 1, low, -1)]
        )
        prev = socket
        centers = np.asarray(ARM)[:, :3]
        for i, (cx, y, cz, wx, wz) in enumerate(ARM):
            if stride > 1 and i % 2 and i < len(ARM) - 1:
                continue
            tangent = unit(centers[min(len(ARM) - 1, i + 1)] - centers[max(0, i - 1)])
            across = unit(np.cross(tangent, [0, 0, 1]))
            depth = unit(np.cross(across, tangent))
            points = centers[i] + wx * sn[:, None] * across + wz * cs[:, None] * depth
            points[:, 0] *= side
            ring = surface.ring(points)
            surface.bridge(prev, ring)
            prev = ring
        surface.bridge(prev, borders["wrist", side])
        prev = borders["ankle", side]
        for i, (y, cx, w, front, back) in enumerate(LEG):
            if stride > 1 and i % 2 and i < len(LEG) - 1:
                continue
            x = cx + w * sn
            z = (front + back) / 2 + (front - back) / 2 * cs
            z += 0.005 * np.maximum(cs, 0) ** 5 * bump(y, -0.365, 0.028)
            z -= (
                0.005
                * bump(sn, -0.42, 0.48)
                * np.maximum(-cs, 0)
                * bump(y, -0.50, 0.078)
            )
            height = y + 0.024 * np.maximum(-sn, 0) ** 2 * bump(y, -0.036, 0.066)
            ring = surface.ring(np.column_stack((side * x, height, z)))
            surface.bridge(prev, ring)
            prev = ring
        legs[side] = prev
    # The inner arcs meet across the perineum. The two outer arcs and the
    # front/back cross-edges form one continuous boundary below the pelvis.
    left, right = legs[1], legs[-1]
    lo, hi = limb // 2 + limb // 8, limb - limb // 8
    medial = np.array([surface.points[left[k]] for k in range(lo, hi + 1)])
    medial[:, 0] = 0
    medial[:, 1] += 0.013
    mid = surface.ring(medial)
    for k in range(lo, hi):
        i = k - lo
        for edge in (left, right):
            surface.faces.extend(
                [(edge[k], mid[i], mid[i + 1]), (edge[k], mid[i + 1], edge[k + 1])]
            )
    outer = list(range(hi, limb)) + list(range(lo + 1))
    pelvis = (
        [left[k] for k in outer]
        + [mid[0]]
        + [right[k] for k in reversed(outer)]
        + [mid[-1]]
    )
    surface.bridge(pelvis, trunk[0])
    return np.asarray(surface.points, dtype="<f4"), np.asarray(
        surface.faces, dtype="<i4"
    )


def orient(retained, drafted):
    """Propagate the winding of the five attachment borders across the body."""
    faces = np.vstack((retained, drafted))
    edges = defaultdict(list)
    for i, face in enumerate(faces):
        for a, b in zip(face, np.roll(face, -1)):
            edges[min(a, b), max(a, b)].append((i, a < b))
    flip = {i: False for i in range(len(retained))}
    queue = list(flip)
    while queue:
        i = queue.pop()
        face = faces[i]
        for a, b in zip(face, np.roll(face, -1)):
            for j, forward in edges[min(a, b), max(a, b)]:
                if j not in flip:
                    flip[j] = bool(flip[i] ^ ((a < b) == forward))
                    queue.append(j)
    assert len(flip) == len(faces), "Detached skin surface"
    for i in range(len(retained), len(faces)):
        if flip[i]:
            faces[i] = faces[i, ::-1]
    return faces[len(retained) :]


def rebuild(chunks, prefix, level, sex):
    def read(name, dtype, width):
        return np.frombuffer(chunks[prefix + name][1], dtype).reshape(-1, width)

    def put(name, values):
        key = prefix + name
        data = np.ascontiguousarray(values)
        chunks[key] = (chunks[key][0], data.tobytes(), data.size)

    pos = read("mesh/pos", "<f4", 3)
    tri = read("mesh/tri", "<i4", 3)
    uv = read("mesh/uv", "<f4", 6).reshape(-1, 3, 2)
    parts = read("mesh/part", "u1", 1).ravel()
    x, y, z = pos[tri].mean(1).T
    wrist = (abs(x) - 0.416) * 0.54 + (z - 0.176) * 0.68 - (y - 0.245) * 0.49
    keep = (y > 0.608) | (y < -0.725) | ((abs(x) > 0.33) & (wrist > 0))
    points, draft_faces = draft(pos, tri, keep, level, sex)
    # Fair only the new skin. The attachment vertices are fixed to the
    # animated extremities. Irregular junctions benefit from a wider blend.
    edge = np.unique(
        np.sort(
            np.concatenate(
                [draft_faces[:, [0, 1]], draft_faces[:, [1, 2]], draft_faces[:, [2, 0]]]
            ),
            axis=1,
        ),
        axis=0,
    )
    src, dst = np.concatenate([edge, edge[:, ::-1]]).T
    counts = np.bincount(src, minlength=len(points)).clip(1)
    ax, y = abs(points[:, 0]), points[:, 1]
    mix = (
        0.18
        + 0.40 * bump(ax, 0.17, 0.08) * bump(y, 0.49, 0.12)
        + 0.36 * bump(y, -0.025, 0.09)
    )
    mix[: len(pos)] = 0
    for _ in range(5):
        sums = np.zeros_like(points)
        np.add.at(sums, src, points[dst])
        points += mix[:, None] * (sums / counts[:, None] - points)
    drafted = orient(tri[keep], draft_faces)
    faces = np.vstack((tri[keep], drafted))
    used, remap = np.unique(faces, return_inverse=True)
    kept = used[used < len(pos)]
    new = points[used[used >= len(pos)]]
    donor, bary = bindings(new, pos, tri, parts == 0)
    corners = tri[donor]

    def transfer(values):
        return np.vstack((values[kept], np.einsum("ij,ijk->ik", bary, values[corners])))

    joints = read("mesh/joints", "u1", 4)
    weights = read("mesh/weights", "u1", 4)
    dense = np.zeros((len(pos), int(joints.max()) + 1))
    for j, w in zip(joints.T, weights.T):
        np.add.at(dense, (np.arange(len(pos)), j), w)
    dense = transfer(dense)
    top = np.argsort(-dense, axis=1, kind="stable")[:, :4]
    mass = np.take_along_axis(dense, top, axis=1)
    packed = np.rint(255 * mass / mass.sum(axis=1, keepdims=True)).astype(int)
    packed[:, 0] += 255 - packed.sum(1)
    put("mesh/joints", top.astype("u1"))
    put("mesh/weights", packed.astype("u1"))
    put("mesh/pos", points[used])
    put("mesh/tri", remap.astype("<i4"))
    put("mesh/part", np.r_[parts[keep], np.zeros(len(drafted), "u1")])
    from body_atlas import unwrap, paint_map, transfer_layers

    coords = unwrap(points, drafted, uv[keep])
    mapping = paint_map(coords, points[faces], uv, keep, pos, tri, parts == 0)
    transfer_layers(chunks, prefix, mapping)
    put("mesh/uv", coords)
    for key in list(chunks):
        if not key.startswith(prefix + "t/") or not key.endswith("/idx"):
            continue
        base = key[len(prefix) : -3]
        old = np.zeros_like(pos)
        old[read(base + "idx", "<i4", 1).ravel()] = read(base + "d", "<i2", 3)
        delta = np.rint(transfer(old)).astype("<i2")
        idx = np.flatnonzero(delta.any(1))
        put(base + "idx", idx.astype("<i4"))
        put(base + "d", delta[idx])

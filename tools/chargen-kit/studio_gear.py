"""Fit the interchangeable studio meshes to the generator's five neutral bodies."""

from pathlib import Path

import numpy as np
from PIL import Image

ART = Path(__file__).with_name("studio")
TAU = 2 * np.pi


def unit(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1.0e-10)


class Panels:
    def __init__(self):
        self.pos, self.tri, self.uv = [], [], []

    def add(self, rows, rect=(0, 0, 1, 1), reverse=False):
        rows = np.asarray(rows)
        h, w = rows.shape[:2]
        base = len(self.pos)
        self.pos.extend(rows.reshape(-1, 3))
        for y in range(h - 1):
            for x in range(w - 1):
                corners = [(y, x), (y, x + 1), (y + 1, x + 1), (y + 1, x)]
                for order in ((0, 1, 2), (0, 2, 3)):
                    indices = order[::-1] if reverse else order
                    coords = [corners[k] for k in indices]
                    self.tri.append([base + r * w + c for r, c in coords])
                    self.uv.append(
                        [
                            (
                                rect[0] + rect[2] * c / (w - 1),
                                rect[1] + rect[3] * r / (h - 1),
                            )
                            for r, c in coords
                        ]
                    )

    def arrays(self):
        return np.asarray(self.pos), np.asarray(self.tri), np.asarray(self.uv)


def crown(a, t, long_cut=False, hat=False):
    front = (np.cos(a) + 1) * 0.5
    bearing = np.arccos(np.clip(np.cos(a), -1, 1))
    hairline = np.interp(
        bearing,
        [0, 0.42, 0.86, 1.18, 1.54, 2.1, np.pi],
        [0.773, 0.778, 0.786, 0.760, 0.721, 0.725, 0.708],
    )
    hairline += 0.005 * np.sin(3 * a + 0.4) * front
    if long_cut:
        hairline = 0.658 + 0.126 * front**3 + 0.014 * np.sin(a * 2 + 0.3) * front
    if hat:
        hairline = 0.731 + 0.017 * front
    y = hairline + (0.861 - hairline) * t
    dome = np.sqrt(np.maximum(0.00008, 1 - (np.maximum(0, y - 0.739) / 0.122) ** 2))
    tuck = 1 - 0.14 * np.maximum(0, (0.720 - y) / 0.105)
    a = a + 0.48 * t + 0.18 * np.sin(a) * np.sin(t * np.pi)
    lift = 0.009 * np.sin(np.pi * t) * front * (1 if not long_cut and not hat else 0.2)
    return np.stack(
        (
            0.004 * t + 0.087 * dome * tuck * np.sin(a),
            y + lift,
            0.050 + 0.114 * dome * tuck * np.cos(a),
        ),
        axis=-1,
    )


# Allocate more texels to the portrait while retaining a separate profile,
# nape and four alpha locks. Coordinates are in a 512-square source.
VIEWS = [(0, 0, 304, 336), (304, 0, 512, 336), (0, 336, 226, 512)]
GROOM_HEIGHT = [0.236, 0.258, 0.251, 0.215, 0.285, 0.340]
# The photographed-silhouette lofts, in the order the code below indexes them.
LEGACY = [("side-crop", "Side crop"), ("rain-bob", "Rain bob"),
          ("tide-knot", "Tide knot"), ("choppy-pixie", "Choppy pixie"),
          ("curtain-shag", "Curtain shag"), ("low-ponytail", "Low ponytail")]
# Grooms modelled in Blender (studio/hair-blender/grooms.py) replace them
# under the same ids: studio/<id>.npz holds hero/standard/crowd cards in the
# kit's runtime (sculpted) space; all share one
# Cycles-baked strand atlas, card-strands.png. The lofts stay as "<id>-old" for comparison.
MODELLED = tuple(gid for gid, _ in LEGACY) + ("layered-lob",)
HAIR_STYLES = ([(gid, label) for gid, label in LEGACY] + [("layered-lob", "Layered lob")]
               + [(gid + "-old", label + " (old)") for gid, label in LEGACY])


def base_id(gid):
    """The style a hair id belongs to: "side-crop-old" -> "side-crop"."""
    return gid[:-4] if gid.endswith("-old") else gid


def legacy_index(style):
    """HAIR_STYLES index of an "-old" loft -> its index in LEGACY."""
    return [g for g, _ in LEGACY].index(base_id(HAIR_STYLES[style][0]))


def modelled_groom(gid, level):
    from studio_bodies import sculpt

    with np.load(ART / (gid + ".npz")) as d:
        points = d[level + "_pos"].astype(float)
        tri, uv = d[level + "_tri"].astype(np.int64), d[level + "_uv"].astype(float)
    # Every studio item is authored before studio_bodies.sculpt; invert it.
    pre = points.copy()
    for _ in range(12):
        pre -= sculpt(pre) - points
    return pre, tri, uv.reshape(-1, 3, 2)


def captured_view(style, view):
    style = 2 if style == 5 else style
    source = Image.open(ART / ("groom-views.jpg" if style < 3 else "groom-extra-views.jpg")).convert("RGB")
    style %= 3
    tile = source.crop((view * 384, style * 384, (view + 1) * 384, (style + 1) * 384))
    rgb = np.asarray(tile, dtype=float).copy()
    spill = rgb[:, :, 1] - np.maximum(rgb[:, :, 0], rgb[:, :, 2])
    opacity = np.clip((24 - spill) / 23, 0, 1)
    rgb[:, :, 1] = np.minimum(rgb[:, :, 1], np.maximum(rgb[:, :, 0], rgb[:, :, 2]))
    return rgb, opacity


def hair_atlas(style):
    gid = HAIR_STYLES[style][0]
    if gid in MODELLED:
        return Image.open(ART / "card-strands.png").convert("RGBA")
    style = legacy_index(style)
    atlas = Image.new("RGBA", (512, 512))
    roots = (
        Image.open(ART / "crowns.jpg")
        .convert("RGB")
        .crop(((style % 3) * 512, 0, (style % 3 + 1) * 512, 512))
    )
    for k, (x0, y0, x1, y1) in enumerate(VIEWS):
        rgb, alpha = captured_view(style, k)
        # Give the filter a dark strand colour outside the silhouette.
        bg = np.asarray(roots.resize((384, 384)), dtype=float) * 0.5
        rgb = rgb * alpha[:, :, None] + bg * (1 - alpha[:, :, None])
        patch = Image.fromarray(np.uint8(np.dstack((rgb, alpha * 255))))
        atlas.paste(
            patch.resize((x1 - x0, y1 - y0), Image.Resampling.LANCZOS), (x0, y0)
        )
    strips = Image.open(ART / "strands.png").convert("RGBA")
    atlas.paste(strips.resize((286, 176), Image.Resampling.LANCZOS), (226, 336))
    atlas.paste(roots.resize((64, 176), Image.Resampling.LANCZOS), (448, 336))
    return atlas


def clean_groom(geometry):
    """Weld coincident roots and discard pole triangles with zero area."""
    pos, tri, uv = geometry
    _, first, remap = np.unique(np.round(pos, 7), axis=0, return_index=True, return_inverse=True)
    tri = remap[tri]
    pos = pos[first]
    area = np.linalg.norm(np.cross(pos[tri[:, 1]] - pos[tri[:, 0]],
                                   pos[tri[:, 2]] - pos[tri[:, 0]]), axis=1)
    keep = area > 1e-10
    return pos, tri[keep], uv[keep]


def scalp_layer(mesh, segments, style=0):
    """Opaque root surface under the cards, closed at the crown pole."""
    angles = np.linspace(-np.pi, np.pi, segments + 1)
    rings = []
    for t in np.linspace(0, 1, 10):
        pts = crown(angles, t)
        pts[:, 0] *= 1.025
        pts[:, 2] = 0.050 + (pts[:, 2] - 0.050) * 1.025
        pts[:, 1] += 0.004
        if style == 4:
            pts[:, 1] -= 0.025 * (1 - t) * (1 - np.maximum(np.cos(angles), 0))
        pts = (pts - [0, 0.745, 0.050]) * [0.86, 0.88, 0.86] + [0, 0.750, 0.050]
        rings.append(pts)
    pole = np.array([0.00344, 0.85648, 0.050])
    rings.append(np.broadcast_to(pole, rings[-1].shape))
    rings = np.asarray(rings)
    # Repeat an opaque root patch around the scalp instead of stretching
    # one narrow atlas column over its whole circumference.
    for j in range(segments):
        mesh.add(rings[:, j:j+2], ((450 + 59*(j % 4)/4)/512, 0.012, 59/2048, 0.315))


def hairstyle(style, level="hero"):
    """Loft cross sections from three silhouettes. Add raised, tapered guide
    meshes in the direction each lock grows, then feather their free ends."""
    if HAIR_STYLES[style][0] in MODELLED:
        return modelled_groom(HAIR_STYLES[style][0], level)
    style = legacy_index(style)
    slices, rows_count, side_locks, samples = {
        "crowd": (18, 8, 6, 4),
        "standard": (30, 12, 24, 7),
        "hero": (42, 20, 44, 10),
    }[level]
    masks = [captured_view(style, k)[1] for k in range(3)]
    top_v = min(np.flatnonzero((mask > 0.43).sum(axis=1) > 12)[0] / 383 for mask in masks)
    height = GROOM_HEIGHT[2 if style == 5 else style]
    crown = 0.877
    # Images use the same eye-level orthographic cameras as the strand guides.
    sx = 0.232 if style in (1, 4) else 0.224
    sz = 0.286
    ys = np.linspace(0.015, 0.975, 65)

    def extent(mask):
        spans = []
        for y in ys:
            r = round(y * 383)
            cols = np.flatnonzero(mask[max(0, r - 2) : r + 3].mean(axis=0) > 0.43)
            spans.append(
                (cols[0] / 383, cols[-1] / 383) if len(cols) > 2 else (np.nan, np.nan)
            )
        vals = np.array(spans)
        for i in (0, 1):
            good = np.isfinite(vals[:, i])
            vals[:, i] = np.interp(ys, ys[good], vals[good, i])
        return vals

    wide = extent(np.maximum(masks[0], masks[2][:, ::-1]))
    deep = extent(masks[1])

    def limits(v):
        # The final rows contain isolated tips, not a complete head section.
        # Continue the last stable section; alpha and the guide tips define
        # the cut, instead of a single stray lock flipping its centre sideways.
        v = np.minimum(v, 0.80)
        left = (np.interp(v, ys, wide[:, 0]) - 0.51) * sx
        right = (np.interp(v, ys, wide[:, 1]) - 0.51) * sx
        front = (0.51 - np.interp(v, ys, deep[:, 0])) * sz + 0.047
        back = (0.51 - np.interp(v, ys, deep[:, 1])) * sz + 0.047
        # The camera sees the opposite temple through a parted fringe.
        front = np.maximum(front, 0.138 * np.clip((v - 0.21) / 0.15, 0, 1))
        if style in (2, 5):
            back = np.maximum(back, -0.061)
        # Below the nape a profile may contain only the FRONT fringe.
        # Do not teleport the rear cross-section to that isolated silhouette.
        back = np.minimum(back, 0.050 - 0.085*np.clip((v-0.25)/0.30, 0, 1))
        return (
            (left + right) / 2,
            (right - left) / 2,
            (front + back) / 2,
            (front - back) / 2,
        )

    def shell(a, v, lift=0):
        cx, rx, cz, rz = limits(v)
        wave = 0.003 * np.sin(8 * a + 5 * v) * np.sin(np.pi * v)
        return np.stack(
            (
                cx + (rx + lift + wave) * np.sin(a),
                np.broadcast_to(crown - height * v, np.shape(a + v)),
                cz + (rz + lift + wave) * np.cos(a),
            ),
            axis=-1,
        )

    def project(points, view):
        x, y, z = np.moveaxis(points, -1, 0)
        u = [0.51 + x / sx, 0.51 - (z - 0.047) / sz, 0.49 - x / sx][view]
        return np.clip(u, 0, 1), np.clip((crown - y) / height, 0, 1)

    def region(a):
        d = abs((a + np.pi) % TAU - np.pi)
        return 0 if d < 1.04 else 1 if d < 2.20 else 2

    def panel(rows, view, trim=False):
        first = len(mesh.tri)
        mesh.add(rows)
        faces = mesh.tri[first:]
        points = np.asarray(mesh.pos)[faces]
        u, v = project(points, view)
        x0, y0, x1, y1 = VIEWS[view]
        uv = np.stack(
            (
                (x0 + u * (x1 - x0 - 2) + 1) / 512,
                1 - (y0 + v * (y1 - y0 - 2) + 1) / 512,
            ),
            axis=-1,
        )
        use = np.ones(len(faces), bool)
        if trim:
            a = masks[view][np.int32(v * 383), np.int32(u * 383)]
            mid = masks[view][
                np.int32(v.mean(axis=1) * 383), np.int32(u.mean(axis=1) * 383)
            ]
            use = (a.max(axis=1) > 0.22) | (mid > 0.22)
        mesh.tri[first:] = list(np.array(faces)[use])
        mesh.uv[first:] = list(uv[use])

    mesh = Panels()
    # The lower outline follows the actual photographic cut in each view,
    # including the long left section of the bob and the cropped right temple.
    for j in range(slices):
        a = np.linspace(-np.pi + TAU * j / slices, -np.pi + TAU * (j + 1) / slices, 2)
        bottom = 0.69 if style == 5 and abs(a.mean()) > 2.0 else 0.97
        rows = [shell(a, v) for v in np.linspace(bottom, top_v + 0.015, rows_count)]
        panel(rows, region(a.mean()), True)

    # An opaque scalp continues below the photographic silhouette seams.
    # Alpha cards add loose ends, never carry the entire coverage of the head.
    scalp_layer(mesh, slices)
    angles = np.linspace(-np.pi, np.pi, slices + 1)
    rings = [shell(angles, max(v, top_v + 0.015), -0.002) for v in (0.31, 0.23, 0.14, 0.07, 0.025)]
    center = rings[-1].mean(axis=0)
    center[1] = crown - height * max(0, top_v - 0.005)
    rings.append(np.broadcast_to(center, rings[-1].shape))
    mesh.add(rings, (450 / 512, 0.012, 59 / 512, 0.315))

    # Each set is traced from root to tip in the front orthographic view.
    # Control points leave the underlayer in depth, producing visible arched
    # ridges, parting gaps and an irregular outline in profile.
    front_guides = [
        [
            [(0.61, 0.14), (0.46, -0.08), (0.20, 0.14), (0.13, 0.46)],
            [(0.63, 0.16), (0.49, 0.02), (0.25, 0.23), (0.29, 0.61)],
            [(0.60, 0.19), (0.47, 0.13), (0.36, 0.27), (0.44, 0.49)],
            [(0.62, 0.16), (0.83, 0.01), (0.86, 0.29), (0.86, 0.54)],
            [(0.56, 0.13), (0.30, 0.00), (0.05, 0.26), (0.11, 0.62)],
        ],
        [
            [(0.64, 0.12), (0.35, -0.03), (0.37, 0.26), (0.19, 0.54)],
            [(0.48, 0.13), (0.17, 0.20), (0.07, 0.56), (0.14, 0.90)],
            [(0.33, 0.24), (0.23, 0.40), (0.03, 0.67), (0.12, 0.98)],
            [(0.25, 0.27), (0.12, 0.43), (0.29, 0.63), (0.19, 0.91)],
            [(0.64, 0.15), (0.85, 0.18), (0.89, 0.50), (0.84, 0.65)],
        ],
        [
            [(0.58, 0.18), (0.40, 0.04), (0.18, 0.25), (0.12, 0.59)],
            [(0.36, 0.23), (0.14, 0.32), (0.23, 0.66), (0.12, 0.92)],
            [(0.59, 0.19), (0.86, 0.18), (0.91, 0.40), (0.91, 0.69)],
        ],
        [
            [(0.48, 0.12), (0.42, 0.04), (0.26, 0.22), (0.22, 0.46)],
            [(0.54, 0.11), (0.59, 0.06), (0.68, 0.23), (0.72, 0.45)],
            [(0.45, 0.14), (0.38, 0.13), (0.43, 0.28), (0.44, 0.44)],
            [(0.63, 0.18), (0.74, 0.16), (0.85, 0.30), (0.85, 0.53)],
        ],
        [
            [(0.49, 0.12), (0.27, 0.12), (0.24, 0.33), (0.16, 0.66)],
            [(0.51, 0.12), (0.75, 0.10), (0.78, 0.34), (0.85, 0.67)],
            [(0.31, 0.25), (0.13, 0.39), (0.12, 0.67), (0.18, 0.95)],
            [(0.72, 0.23), (0.87, 0.39), (0.90, 0.65), (0.82, 0.96)],
        ],
        [
            [(0.50, 0.14), (0.29, 0.04), (0.15, 0.26), (0.16, 0.55)],
            [(0.53, 0.14), (0.75, 0.06), (0.86, 0.27), (0.86, 0.55)],
        ],
    ][2 if style == 5 else style]

    def strip_uv(index):
        # Root at the top of the tile, feathered end at the bottom.
        return ((228 + 71.5 * (index % 3)) / 512, 0.003, 0.130, 0.331)

    def front_lock(controls, width, loft, strand):
        controls = np.asarray(controls)
        rows = []
        for t in np.linspace(0, 1, samples):
            k = 1 - t
            uv = (
                k**3 * controls[0]
                + 3 * t * k * k * controls[1]
                + 3 * t * t * k * controls[2]
                + t**3 * controls[3]
            )
            tangent = unit(
                k * k * (controls[1] - controls[0])
                + 2 * t * k * (controls[2] - controls[1])
                + t * t * (controls[3] - controls[2])
            )
            across = np.array([-1.0, -0.38, 0.24, 1.0])
            half = width * (0.04 + 0.96 * np.sin(t * np.pi) ** 0.72)
            u = uv[0] - tangent[1] * across * half
            v = uv[1] + tangent[0] * across * half
            x = (u - 0.51) * sx
            cx, rx, cz, rz = limits(v)
            depth = cz + rz * np.sqrt(
                np.maximum(0.018, 1 - ((x - cx) / np.maximum(rx, 0.008)) ** 2)
            )
            depth += (
                loft * np.sin(np.pi * t) ** 0.75 * (1 - 0.77 * across**2)
                - 0.003 * (1 - t) ** 3
            )
            rows.append(np.stack((x, crown - height * v, depth), axis=-1))
        mesh.add(rows[::-1], strip_uv(strand))

    # Small locks fan out along each guide. Offset the tip more than the root
    # and stagger the depth so the bands separate as the head turns.
    splits = {"hero": 4, "standard": 2, "crowd": 1}[level]
    for k, guide in enumerate(front_guides):
        controls = np.asarray(guide)
        tangent = unit(controls[-1] - controls[0])
        normal = np.array([-tangent[1], tangent[0]])
        for strand, offset in enumerate((np.arange(splits) + 0.5) / splits - 0.5):
            path = controls + offset * np.outer([0.045, 0.10, 0.15, 0.12], normal)
            path[-1, 1] += 0.027 * np.sin(2.3 * strand + k)
            width = (0.037 + 0.007 * (k % 3)) / np.sqrt(splits)
            front_lock(path, width, 0.011 + 0.003 * ((k + strand) % 4), k + strand)

    # Arched locks over the sides and nape have an actual convex cross section.
    # Their loose tips extend beyond the measured silhouette and can spring.
    rng = np.random.default_rng(62 + style)
    for k in range(side_locks):
        a = -np.pi + TAU * (k + 0.38) / side_locks
        if abs(a) < 0.83:
            continue
        view = region(a)
        sample_v = np.linspace(0.16, 0.96, 65)
        u, v = project(shell(a, sample_v), view)
        visible = masks[view][np.int32(v * 383), np.int32(u * 383)] > 0.53
        if not visible.any():
            continue
        end = sample_v[visible][-1]
        if style in (2, 5) and abs(a) > 2.35:
            end = min(end, 0.69)
        root = rng.uniform(0.12, 0.24)
        bend = np.sign(a) * rng.uniform(0.35, 0.85)
        width = rng.uniform(0.07, 0.14)
        lift = rng.uniform(0.006, 0.016)
        rows = []
        for t in np.linspace(0, 1, min(samples, 9)):
            q = np.array([-1, -0.40, 0.22, 1])
            f = np.sin(t * np.pi)
            aa = a + bend * (1 - t) ** 1.45 + width * (0.04 + 0.96 * f**0.7) * q
            y = root + (end - root) * t + 0.012 * q * f
            rows.append(
                shell(aa, y, lift * f * (1 - 0.89 * q * q) - 0.002 * (1 - t) ** 3)
            )
        mesh.add(rows[::-1], strip_uv(k))
        wisps = []
        for t in np.linspace(0, 1, min(samples, 6)):
            aa = (
                a
                + 0.17 * (1 - t) * np.sign(a)
                + np.array([-1.0, 0.0, 1.0]) * width * (0.24 + 0.48 * np.sin(t * np.pi))
            )
            wisps.append(
                shell(aa, end - 0.11 + 0.155 * t, 0.0025 + 0.004 * np.sin(t * np.pi))
            )
        mesh.add(wisps[::-1], strip_uv(k + 1))
    if style == 5:
        # The tail has its own volume. Front flyaways cannot inflate its
        # silhouette or turn the back of the head into a wide hair curtain.
        def tail(t, angle, extra=0):
            radius = 0.021 * (1 - 0.98*t**1.5) + extra
            return np.stack((0.015*np.sin(2.3*t) + radius*np.sin(angle),
                             np.broadcast_to(0.726 - 0.170*t, np.shape(angle+t)),
                             -0.073 - 0.061*np.sin(1.6*t) + radius*np.cos(angle)), axis=-1)
        angles = np.linspace(-np.pi, np.pi, max(10, slices//2)+1)
        rings = [tail(t, angles) for t in np.linspace(0, 1, samples)]
        rings.append(np.broadcast_to(tail(1.01, 0, -0.0001), rings[-1].shape))
        mesh.add(rings, (450/512,0.012,59/512,0.315), reverse=True)
        for i in range(max(8, side_locks//2)):
            a = TAU*i/max(8, side_locks//2)
            rows = []
            for t in np.linspace(0, 1, samples):
                q = np.array([-1.,-.35,.35,1.])
                angle = a + .24*np.sin(t*2.8) + .22*q
                rows.append(tail(t,angle,.003*np.sin(np.pi*t)*(1-q*q)))
            mesh.add(rows[::-1], strip_uv(i))
    if style == 2:
        with np.load(ART / "chignon.npz") as bun:
            shift = len(mesh.pos)
            pos = bun["pos"].copy()
            pos[:, 2] -= 0.012
            mesh.pos.extend(pos)
            mesh.tri.extend(bun["tri"] + shift)
            u, v = project(pos[bun["tri"]], 2)
            mesh.uv.extend(
                np.stack(((1 + 224 * u) / 512, 1 - (337 + 174 * v) / 512), axis=-1)
            )
    return clean_groom(mesh.arrays())


def watch_cap(brim):
    mesh = Panels()
    angles = np.linspace(0, TAU, 33)
    rows = []
    for t, extra in [
        (0, 0.001),
        (0.05, 0.007),
        (0.18, 0.007),
        (0.19, 0.001),
        (0.38, 0.005),
        (0.63, 0.007),
        (0.82, 0.005),
        (1, 0.001),
    ]:
        pts = crown(angles, t, hat=True)
        pts[:, 0] += extra * np.sin(angles)
        pts[:, 2] += extra * np.cos(angles)
        pts[:, 1] += 0.019 * t
        pts[:, 0] *= 1.075
        pts[:, 2] = 0.049 + 1.065 * (pts[:, 2] - 0.049)
        rows.append(pts)
    mesh.add(rows, (0.06, 0.06, 0.88, 0.88))
    if brim:
        angles = np.linspace(-1.26, 1.26, 21)
        rows = []
        for r in [1.0, 1.04, 1.43, 1.44, 1.03]:
            pts = crown(angles, 0, hat=True)
            pts[:, 2] += (r - 1) * 0.14 * np.cos(angles)
            pts[:, 1] -= 0.006 * (r - 1) + (0.002 if len(rows) > 2 else 0)
            rows.append(pts)
        mesh.add(rows)
    return mesh.arrays()


def glasses(shaded):
    mesh = Panels()
    a = np.linspace(0, TAU, 21)
    for sign in (-1, 1):
        rows = []
        for r, z in [(1, 0.149), (0.98, 0.152), (0.91, 0.152), (0.90, 0.150)]:
            x = sign * (0.032 + 0.027 * r * np.cos(a))
            rows.append(
                np.stack(
                    (
                        x,
                        0.731 + 0.016 * r * np.sin(a),
                        z - 0.012 * (abs(x) / 0.06) ** 2,
                    ),
                    axis=-1,
                )
            )
        mesh.add(rows, (0.12, 0.12, 0.76, 0.76))
        if shaded:
            mesh.add(
                [rows[-1], rows[-1] * 0 + rows[-1].mean(axis=0)],
                (0.12, 0.12, 0.76, 0.76),
            )
        for pts in [
            [
                (0.057, 0.733, 0.140),
                (0.067, 0.739, 0.090),
                (0.076, 0.731, 0.040),
                (0.078, 0.714, 0.020),
            ],
            [(0.004, 0.732, 0.151), (0.001, 0.735, 0.155), (0, 0.735, 0.155)],
        ]:
            mesh.add(
                [
                    [(sign * x, y - 0.0016, z), (sign * x, y + 0.0016, z)]
                    for x, y, z in pts
                ]
            )
    return mesh.arrays()


def jacket_texture(fabric):
    panel = Image.fromarray(fabric)
    img = panel.crop((18, 100, 105, 405)).resize((512, 512))
    for rect, patch in [
        ((0.02, 0.34, 0.48, 0.98), panel),
        ((0.52, 0.34, 0.98, 0.98), panel.crop((15, 45, 105, 495))),
        ((0.02, 0.02, 0.48, 0.30), panel.crop((8, 0, 115, 512))),
        ((0.52, 0.02, 0.98, 0.30), panel.crop((385, 0, 501, 512))),
    ]:
        x0, y0, x1, y1 = rect
        box = (
            round(512 * x0) - 2,
            round(512 * (1 - y1)) - 2,
            round(512 * x1) + 2,
            round(512 * (1 - y0)) + 2,
        )
        img.paste(
            patch.resize((box[2] - box[0], box[3] - box[1]), Image.Resampling.LANCZOS),
            box[:2],
        )
    return img


def crowd_mesh(geometry, budget):
    """Weld distant detail into spatial cells while preserving corner UVs."""
    pos, tri, uv = geometry
    if len(tri) <= budget:
        return geometry
    for cell in np.geomspace(0.006, 0.10, 24):
        _, mapped = np.unique(
            np.floor((pos - pos.min(axis=0)) / cell), axis=0, return_inverse=True
        )
        mapped = mapped.ravel()
        lo = np.zeros((mapped.max() + 1, 3))
        np.add.at(lo, mapped, pos)
        lo /= np.bincount(mapped)[:, None]
        faces = mapped[tri]
        keep = (
            (faces[:, 0] != faces[:, 1])
            & (faces[:, 1] != faces[:, 2])
            & (faces[:, 2] != faces[:, 0])
        )
        if keep.sum() <= budget:
            break
    return lo.astype("f4"), faces[keep], uv[keep]


def skirt(dress=False, level="hero"):
    """A flared skirt with a shaped waistband, vertical folds and turned hem."""
    mesh = Panels()
    segments = {"hero": 48, "standard": 24, "crowd": 16}[level]
    angles = np.linspace(-np.pi, np.pi, segments + 1)
    rows = []
    heights = np.array(
        [
            0.159,
            0.153,
            0.141,
            0.122,
            0.09,
            0.035,
            -0.035,
            -0.11,
            -0.19,
            -0.27,
            -0.35,
            -0.39,
            -0.394,
        ]
    )
    if level != "hero":
        heights = heights[[0, 2, 4, 6, 8, 10, 12]]
    for y in heights:
        t = np.clip((0.15 - y) / 0.54, 0, 1)
        width = np.interp(
            y, [-0.394, -0.19, 0.035, 0.159], [0.275, 0.233, 0.191, 0.158]
        )
        depth = np.interp(
            y, [-0.394, -0.19, 0.035, 0.159], [0.183, 0.150, 0.127, 0.103]
        )
        fold = (0.0015 + 0.007 * t) * np.cos(12 * angles + 0.45 * t)
        x = (width + fold) * np.sin(angles)
        z = 0.030 + (depth + fold) * np.cos(angles)
        # Slightly lower waist at the front and a rolled lower edge.
        yy = y - 0.009 * np.maximum(np.cos(angles), 0) * (1 - t) ** 3
        rows.append(np.stack((x, yy, z), axis=-1))
    mesh.add(rows, reverse=True)
    pos, tri, _ = mesh.arrays()
    corner = pos[tri]
    x, y, z = np.moveaxis(corner, -1, 0)
    width = np.interp(y, [-0.394, -0.19, 0.035, 0.159], [0.275, 0.233, 0.191, 0.158])
    # The two broad panels meet at the side seams.
    u = np.where(z >= 0.03, 0.5 + x / (2 * width), 0.5 - x / (2 * width))
    v = (0.56 - y) / 0.96 if dress else (0.16 - y) / 0.56
    uv = np.stack((0.012 + 0.976 * u, 1 - (0.012 + 0.976 * v)), axis=-1)
    return pos, tri, uv


def models(fabrics):
    def stored(file):
        with np.load(ART / (file + ".npz")) as d:
            return d["pos"], d["tri"], d["uv"]

    leather = Image.open(ART / "boots.png").convert("RGB")
    yield (
        "yard-jacket",
        "Yard jacket",
        "top",
        stored("jacket"),
        jacket_texture(fabrics[:512, :512]),
    )
    yield "storm-boots", "Storm boots", "feet", stored("boots"), leather
    pos, tri, uv = stored("boots")
    pos[:, 1] = np.where(
        pos[:, 1] > -0.733, -0.733 + (pos[:, 1] + 0.733) * 0.29, pos[:, 1]
    )
    pos[:, 2] += 0.009 * np.clip((-pos[:, 1] - 0.73) / 0.07, 0, 1)
    yield (
        "service-shoes",
        "Service derbies",
        "feet",
        (pos, tri, uv),
        leather.point(lambda v: min(255, v * 1.18 + 4)),
    )
    cloth = Image.open(ART / "women-workwear.jpg").convert("RGB")
    yield (
        "a-line-skirt",
        "A-line skirt",
        "bottom",
        skirt(),
        cloth.crop((0, 512, 512, 1024)),
    )
    yield (
        "moss-dress",
        "Moss dress",
        "full",
        skirt(True),
        cloth.crop((512, 512, 1024, 1024)),
    )
    for index, (gid, label) in enumerate(HAIR_STYLES):
        yield gid, label, "hair", hairstyle(index), hair_atlas(index)
    knit = Image.fromarray(fabrics[:512, 1024:]).point(lambda v: v * 0.43)
    yield "watch-cap", "Watch cap", "head", watch_cap(False), knit
    yield (
        "yard-cap",
        "Yard cap",
        "head",
        watch_cap(True),
        Image.fromarray(fabrics[:512, :512]).crop((30, 50, 115, 190)),
    )
    from studio_cyber import meshes

    yield from meshes(fabrics, leather)
    yield (
        "wire-frames",
        "Wire frames",
        "face",
        glasses(False),
        Image.new("RGB", (16, 16), (89, 73, 50)),
    )
    yield (
        "smoke-lenses",
        "Smoke lenses",
        "face",
        glasses(True),
        Image.new("RGB", (16, 16), (38, 42, 41)),
    )


def bindings(points, positions, triangles, allowed):
    """Closest surface point and barycentrics, including points outside a face."""
    ids = np.flatnonzero(allowed)
    faces = positions[triangles[ids]]
    centers = faces.mean(axis=1)
    from scipy.spatial import cKDTree

    tree = cKDTree(centers)
    selected = []
    bary = []
    for start in range(0, len(points), 96):
        pt = points[start : start + 96, None, :]
        k = min(24, len(ids))
        _, near = tree.query(pt[:, 0], k=list(range(1, k + 1)))
        a, b, c = np.moveaxis(faces[near], 2, 0)
        e, f = b - a, c - a
        dot = lambda x, y: np.sum(x * y, axis=-1)
        ee, ff, ef = dot(e, e), dot(f, f), dot(e, f)
        pe, pf = dot(pt - a, e), dot(pt - a, f)
        det = np.maximum(ee * ff - ef * ef, 1.0e-16)
        v = (pe * ff - pf * ef) / det
        w = (pf * ee - pe * ef) / det
        face = np.stack((1 - v - w, v, w), axis=-1)
        options = [face]
        for j, (l, r) in enumerate(((a, b), (b, c), (c, a))):
            t = np.clip(
                dot(pt - l, r - l) / np.maximum(dot(r - l, r - l), 1.0e-16), 0, 1
            )
            edge = np.zeros_like(face)
            edge[:, :, j] = 1 - t
            edge[:, :, (j + 1) % 3] = t
            options.append(edge)
        weights = np.stack(options, axis=2)
        offsets = np.einsum("bkci,bkij->bkcj", weights, faces[near]) - pt[:, :, None, :]
        cost = np.sum(offsets**2, axis=3)
        cost[:, :, 0] = np.where(face.min(axis=2) >= 0, cost[:, :, 0], np.inf)
        best = cost.reshape(len(pt), -1).argmin(axis=1)
        row = np.arange(len(pt))
        selected.extend(ids[near[row, best // 4]])
        bary.extend(weights[row, best // 4, best % 4])
    return np.array(selected), np.array(bary)


def bind_item(pos, tris, part, joints, weights, points, slot, gid, names):
    center = pos[tris].mean(axis=1)
    ok = part == 0
    if slot in ("hair", "face", "head"):
        ok &= center[:, 1] > 0.565
    elif slot == "feet":
        ok &= center[:, 1] < -0.55
    else:
        ok &= (center[:, 1] > 0.015) & (center[:, 1] < 0.66)
    anchor = points.copy()
    if gid in ("a-line-skirt", "moss-dress"):
        # Bind the hem to the hips so leg separation cannot split the skirt.
        anchor[:, 1] = np.maximum(anchor[:, 1], 0.11)
        shrink = np.minimum(1, 0.17 / np.maximum(abs(anchor[:, 0]), 0.001))
        anchor[:, 0] *= shrink
        anchor[:, 2] = 0.03 + (anchor[:, 2] - 0.03) * shrink
    faces, bary = bindings(anchor, pos, tris, ok)
    corners = tris[faces]
    a, b, c = np.moveaxis(pos[corners], 1, 0)
    normals = np.zeros_like(pos)
    fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
    for idx in tris.T:
        np.add.at(normals, idx, fn)
    n = unit(np.einsum("ij,ijk->ik", bary, unit(normals)[corners]))
    e, f = unit(b - a), unit(c - a)

    def tangent(v):
        return unit(v - n * np.sum(v * n, axis=1, keepdims=True))

    frame = np.stack((n, tangent(e + f), tangent(e - f)), axis=2)
    delta = points - np.einsum("ij,ijk->ik", bary, pos[corners])
    offset = np.einsum("ijk,ik->ij", np.linalg.pinv(frame, rcond=0.005), delta)
    # Sliver triangles can have nearly parallel projected tangents. Large
    # cancelling coefficients become a spike when an animated face changes shape.
    unstable = np.linalg.norm(offset, axis=1) > 1.8 * np.maximum(
        0.001, np.linalg.norm(delta, axis=1)
    )
    offset[unstable] = np.einsum("ikj,ik->ij", frame[unstable], delta[unstable])
    if slot == "hair":
        offset[:, 1:] = np.clip(offset[:, 1:], -0.012, 0.012)
        offset[:, 0] = np.maximum(offset[:, 0], 0.003)
    gw = np.zeros((len(pos), len(names)))
    for j, w in zip(joints.T, weights.T):
        np.add.at(gw, (np.arange(len(pos)), j), w / 255.0)
    gw = np.einsum("ij,ijk->ik", bary, gw[corners])
    if slot in ("hair", "face", "head"):
        gw[:] = 0
        gw[:, names.index("mixamorig:Head")] = 1
    if slot == "hair":
        # Only free, low lengths lag behind the skull. The part and hairline
        # are rigid, keeping bangs out of the eyes when the character turns.
        free = np.clip((0.734 - points[:, 1]) / 0.089, 0, 1)
        behind = np.clip((0.128 - points[:, 2]) / 0.14, 0, 1)
        gid = base_id(gid)
        strength = {"rain-bob": 0.18, "tide-knot": 0.08,
                    "curtain-shag": 0.16, "low-ponytail": 0.45,
                    "layered-lob": 0.16}.get(gid, 0.0)
        if gid == "low-ponytail":
            behind *= np.clip((-0.040 - points[:, 2]) / 0.03, 0, 1)
        spring = strength * free * behind
        gw[:, names.index("mixamorig:Head")] = 1 - spring
        gw[:, names.index("mixamorig:HairTail1")] = spring * (1 - 0.60 * free)
        gw[:, names.index("mixamorig:HairTail2")] = spring * 0.60 * free
    if gid in ("a-line-skirt", "moss-dress"):
        loose = np.clip((0.11 - points[:, 1]) / 0.48, 0, 1)
        gw[:] = 0
        gw[:, names.index("mixamorig:Hips")] = 1 - 0.47 * loose
        side = np.clip(0.5 + points[:, 0] / 0.20, 0, 1)
        gw[:, names.index("mixamorig:LeftUpLeg")] = 0.32 * loose * side
        gw[:, names.index("mixamorig:RightUpLeg")] = 0.32 * loose * (1 - side)
        front = np.clip(0.5 + (points[:, 2] - 0.03) / 0.12, 0, 1)
        gw[:, names.index("mixamorig:SkirtFront")] = 0.15 * loose * front
        gw[:, names.index("mixamorig:SkirtBack")] = 0.15 * loose * (1 - front)
    joint = np.argsort(-gw, axis=1)[:, :4]
    mass = np.take_along_axis(gw, joint, axis=1)
    quant = np.round(
        255 * mass / np.maximum(mass.sum(axis=1, keepdims=True), 1.0e-10)
    ).astype(int)
    quant[:, 0] += 255 - quant.sum(axis=1)
    cover = np.zeros(len(tris), bool)
    from studio_cyber import CLEAR

    if gid in CLEAR:
        pass  # translucent: the body under it stays
    elif gid in ("a-line-skirt", "moss-dress"):
        # Cover whole thigh faces above the hem even when a walking leg
        # moves beyond the rest-pose radius of the cloth.
        vertices = pos[tris]
        cover = (
            (vertices[:, :, 1].min(1) > -0.43)
            & (vertices[:, :, 1].max(1) < 0.145)
            & (abs(vertices[:, :, 0]).max(1) < 0.29)
        )
    if slot == "feet":
        limit = -0.653 if gid in ("storm-boots", "tech-boots") else -0.728
        cover = pos[tris, 1].max(axis=1) < limit
    if slot == "top" and gid not in CLEAR:
        # Hide the trunk and upper arms only, leaving generous margins at openings.
        x, y, z = center.T
        cover = (y > 0.105) & (y < 0.50) & (abs(x) < 0.365)
        cover &= ~((abs(x) < 0.10) & (y > 0.44) & (z > 0.04))
    binding = {
        "bindTri": faces.astype("i4"),
        "bindBary": bary.astype("f4"),
        "bindOff": offset.astype("f4"),
        "joints": joint.astype("u1"),
        "weights": quant.astype("u1"),
        "cover": np.flatnonzero(cover).astype("i4"),
    }
    if slot == "hair":
        # Keep the authored sweep coherent as the brow and jaw morph under it.
        # Transfer one skull-space shape, retaining the triangle fit for hats.
        # Independent nearest-face pushes tore adjacent ribbons apart by up
        # to four centimetres. The closed authored scalp supplies clearance;
        # runtime fitting applies one affine skull transform to the whole groom.
        binding["groom"] = points.astype("<f4")
    return binding

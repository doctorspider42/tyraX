"""The cyberpunk set: a clear PVC jacket, a neon visor, a piped tech top,
strapped cargos and tech boots.

Everything is painted in WORLD coordinates (the studio items' pre-sculpt rest
space, metres): a shell garment per body-atlas texel (studio_bodies passes the
texel's rest point), a mesh item per texel of its own UV layout (`uv_world`
rasterizes the item's triangles into its texture and keeps each texel's 3D
point). Seams, piping, zips, pockets and straps are masks in metres, so they
land in the same place on every body.

Two items are CLEAR (`CLEAR`): the generator keeps their texture's partial
alpha, puts them in a "clear:" part and the game draws that part blended
without writing z (docs/character-generator.md, "Clear plastic"). Nothing
under a clear item is hidden.
"""

import numpy as np
from PIL import Image

CLEAR = ("clear-pvc-jacket", "neon-visor")
NEON = np.array([40.0, 236.0, 255.0])     # cyan piping
MAGENTA = np.array([255.0, 46.0, 170.0])  # the cargos' stripe, the visor's edge


def bell(v, c, w):
    return np.exp(-(((v - c) / w) ** 2))


def ramp(v, lo, hi):
    t = np.clip((v - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def band(v, c, half, soft=0.0015):
    """1 inside |v - c| < half, smooth over soft metres."""
    return 1 - ramp(np.abs(v - c), half, half + soft)


def glow(rgb, mask, colour):
    """Lay a neon line over the fabric."""
    return rgb * (1 - mask[..., None]) + colour * mask[..., None]


def weave(tile, x, y, z, scale=1.0):
    """Fabric detail from a studio fabric tile, tiled in metres."""
    h, w = tile.shape[:2]
    u = ((x * 9 + z * 5) * scale) % 1
    v = ((y * 7) * scale) % 1
    return tile[np.int32(v * (h - 1)), np.int32(u * (w - 1))].astype(float)


# ---------------------------------------------------------------------------
# Shell garments (painted into the body atlas, pushed out by `ease`)

def wrist_coord(x, y, z):
    ax = abs(x)
    return (ax - 0.431) * 0.34 + (z - 0.176) * 0.65 + (0.245 - y) * 0.65


def tech_top_cut(x, y, z):
    ax = np.abs(x)
    front = ramp(z, 0.03, 0.11)
    collar = 0.604 - 0.010 * front  # a mock neck
    return (ramp(y, 0.092, 0.098) * (1 - ramp(y, collar, collar + 0.005))
            * (1 - ramp(wrist_coord(x, y, z), -0.016, -0.010)) * (ax < 0.47))


def tech_top_paint(tile, x, y, z):
    ax = np.abs(x)
    base = weave(tile, x, y, z) * 0.22 + np.array([8.0, 9.0, 12.0])
    front = z > 0.03
    w = wrist_coord(x, y, z)
    torso = ax < 0.17
    lines = np.zeros_like(x)
    # piped yoke across chest and back, and the side panels down from it
    lines = np.maximum(lines, band(y, 0.468 - 0.04 * ramp(ax, 0.10, 0.17), 0.0022) * torso)
    lines = np.maximum(lines, band(ax, 0.112, 0.0022) * (y < 0.466) * (y > 0.11) * front)
    # chevron on the chest
    chev = band(y, 0.405 - 0.55 * ax, 0.0020) * (ax < 0.07) * front
    lines = np.maximum(lines, chev)
    # collar ring, hem band, cuffs and elbow rings
    lines = np.maximum(lines, band(y, 0.590, 0.0025) * torso)
    lines = np.maximum(lines, band(y, 0.106, 0.0025) * torso)
    lines = np.maximum(lines, band(w, -0.030, 0.0025) * (ax > 0.20))
    lines = np.maximum(lines, band(w, -0.125, 0.0020) * (ax > 0.20))
    # a darker quilted shoulder panel
    shoulder = ramp(ax, 0.13, 0.15) * ramp(y, 0.45, 0.47) * (ax < 0.24)
    base = base * (1 - 0.35 * shoulder[..., None])
    return np.clip(glow(base, lines, NEON), 0, 255)


def cargos_cut(x, y, z):
    ax = np.abs(x)
    waist = 0.150 - 0.012 * ramp(z, 0.04, 0.13)
    return (ramp(y, -0.735, -0.729) * (1 - ramp(y, waist, waist + 0.005)) * (1 - ramp(ax, 0.29, 0.33)))


def cargos_paint(tile, x, y, z):
    ax = np.abs(x)
    leg = np.interp(y, [-0.81, -0.37, 0.06], [0.22, 0.157, 0.105])
    outer = ax - leg
    base = weave(tile, x, y, z, 1.3) * 0.30 + np.array([16.0, 17.0, 20.0])
    # cargo pocket on the outer thigh, its flap and a darker border
    pocket = (outer > 0.035) * (y < -0.20) * (y > -0.36) * (np.abs(z - 0.012) < 0.045)
    flap = pocket * (y > -0.235)
    border = pocket * ((np.abs(y + 0.20) < 0.003) | (np.abs(y + 0.36) < 0.003)
                       | (np.abs(np.abs(z - 0.012) - 0.045) < 0.003) | (np.abs(y + 0.235) < 0.002))
    rgb = base * (1 + 0.18 * pocket[..., None]) * (1 - 0.25 * flap[..., None])
    rgb = rgb * (1 - 0.6 * border[..., None])
    # two black straps round the thigh, with silver buckles at the side
    straps = np.zeros_like(x)
    for c in (-0.06, -0.15):
        straps = np.maximum(straps, band(y, c - 0.18 * (z - 0.01), 0.008) * (y > -0.40))
    buckle = straps * (outer > 0.045) * (np.abs(z - 0.012) < 0.012)
    rgb = rgb * (1 - 0.78 * straps[..., None])
    rgb = rgb * (1 - buckle[..., None]) + np.array([170.0, 175.0, 182.0]) * buckle[..., None]
    # magenta stripe down the outer seam, the waistband and its neon buckle
    stripe = (outer > 0.0) * band(z, 0.010, 0.0035) * (y < 0.10)
    rgb = glow(rgb, stripe, MAGENTA)
    waistband = ramp(y, 0.115, 0.118)
    rgb = rgb * (1 - 0.45 * waistband[..., None])
    clip = waistband * band(x, 0.0, 0.016) * (z > 0.05)
    rgb = glow(rgb, clip, NEON)
    return np.clip(rgb, 0, 255)


SHELLS = (
    # id, label, slot, ease (m), fabric panel, cut, paint
    ("neon-tech-top", "Neon tech top", "top", 0.007, 2, tech_top_cut, tech_top_paint),
    ("strap-cargos", "Strap cargos", "bottom", 0.019, 4, cargos_cut, cargos_paint),
)
SHELL_IDS = tuple(s[0] for s in SHELLS)


# ---------------------------------------------------------------------------
# Mesh items

def uv_world(pos, tri, uv):
    """Each texel's 3D point (512 square) and the island mask, by rasterizing
    the item's triangles into its own UV layout (per-corner UVs, V up)."""
    from studio_bodies import surface_map

    xyz = surface_map(pos, tri, uv, np.zeros(len(tri), np.uint8))
    inside = np.abs(xyz).sum(axis=0) > 0
    return xyz, inside


def island_edges(inside, px=3):
    from scipy.ndimage import binary_erosion

    return inside & ~binary_erosion(inside, iterations=px)


def clear_jacket(fabrics):
    from studio_gear import ART

    with np.load(ART / "jacket.npz") as d:
        pos, tri, uv = d["pos"].copy(), d["tri"], d["uv"]
    # A touch roomier than the yard jacket and with a taller collar: PVC
    # stands off the body.
    centre = np.array([0.0, 0.35, 0.03])
    pos = centre + (pos - centre) * np.array([1.012, 1.0, 1.02])
    top = pos[:, 1] > 0.585
    pos[top, 1] += (pos[top, 1] - 0.585) * 0.8
    xyz, inside = uv_world(pos, tri, uv)
    x, y, z = xyz
    ax = np.abs(x)
    tint = np.array([170.0, 215.0, 232.0])
    rgb = np.zeros(x.shape + (3,)) + tint
    alpha = np.full(x.shape, 0.20)
    # sheen: soft streaks where the plastic catches the light, brighter on
    # the shoulders and chest (facing up and forward)
    sheen = np.clip(np.sin((x * 1.4 + y) * 31) * 0.5 + 0.5, 0, 1) ** 14
    sheen = np.clip(sheen + 0.35 * ramp(y, 0.44, 0.56) * ramp(z, 0.04, 0.12), 0, 1)
    rgb += sheen[..., None] * 80
    alpha += sheen * 0.40
    # welded seams: every island border, plus hem, cuffs and collar bands
    edge = island_edges(inside, 2).astype(float)
    hem = band(y, 0.064, 0.004)
    cuff = ramp(ax, 0.428, 0.434)
    collar = ramp(y, 0.645, 0.652)
    trim = np.clip(np.maximum.reduce([edge * 0.7, hem, cuff, collar]), 0, 1)
    rgb = rgb * (1 - trim[..., None]) + np.array([120.0, 140.0, 152.0]) * trim[..., None]
    alpha = alpha * (1 - trim) + 0.85 * trim
    # a black zip down the front with a cyan pull
    zip_ = band(x, 0.0, 0.0035) * (z > 0.05) * (y < 0.64)
    rgb = rgb * (1 - zip_[..., None]) + np.array([24.0, 26.0, 30.0]) * zip_[..., None]
    alpha = np.maximum(alpha, zip_)
    pull = band(x, 0.0, 0.006) * band(y, 0.56, 0.012) * (z > 0.05)
    rgb = glow(rgb, pull, NEON)
    alpha = np.maximum(alpha, pull)
    img = np.dstack((np.clip(rgb, 0, 255), np.clip(alpha, 0, 1) * 255 * inside))
    return (pos, tri, uv), Image.fromarray(np.uint8(img), "RGBA")


def neon_visor():
    """A wraparound visor at eye level, offset from the face."""
    centre = np.array([0.0, 0.731, 0.046])
    cols = np.linspace(-1.75, 1.75, 21)
    rows = np.array([-1.0, -0.33, 0.33, 1.0])
    pos = []
    for r in rows:
        for a in cols:
            front = np.cos(a)
            half = 0.019 * (0.45 + 0.55 * max(front, 0.0) ** 0.5)
            pos.append(centre + [0.088 * np.sin(a), r * half - 0.012 * (1 - front) * 0.5,
                                 0.121 * np.cos(a) - 0.008 * r * r])
    pos = np.array(pos)
    tri, uv = [], []
    W = len(cols)
    for i in range(len(rows) - 1):
        for j in range(W - 1):
            a, b = i * W + j, i * W + j + 1
            c, d = a + W, b + W
            for f in ((a, b, d), (a, d, c)):
                tri.append(f)
                uv.append([(0.04 + 0.92 * (k % W) / (W - 1), 0.06 + 0.88 * (k // W) / (len(rows) - 1))
                           for k in f])
    size = 128
    u = np.linspace(0, 1, size)[None, :]
    v = np.linspace(1, 0, size)[:, None]
    grad = np.clip((u - 0.04) / 0.92, 0, 1)
    rgb = NEON * (1 - grad[..., None]) * 0.85 + MAGENTA * grad[..., None] * 0.85
    rgb = np.broadcast_to(rgb, (size, size, 3)).copy()
    alpha = np.full((size, size), 0.48)
    rim = (np.abs(v - 0.5) > 0.40).astype(float)
    scan = (np.sin(v * 120) > 0.92).astype(float) * 0.18 * (1 - rim)
    rgb = rgb * (1 - rim[..., None]) + np.array([230.0, 240.0, 250.0]) * rim[..., None]
    rgb += scan[..., None] * 60
    alpha = np.maximum(alpha + scan, rim * 0.95)
    img = np.dstack((np.clip(rgb, 0, 255), np.clip(alpha, 0, 1) * 255))
    return (pos, np.array(tri), np.array(uv)), Image.fromarray(np.uint8(img), "RGBA")


def tech_boots(leather):
    from studio_gear import ART

    with np.load(ART / "boots.npz") as d:
        pos, tri, uv = d["pos"], d["tri"], d["uv"]
    xyz, inside = uv_world(pos, tri, uv)
    x, y, z = xyz
    src = np.asarray(leather.convert("RGB").resize((512, 512)), float)
    rgb = src * 0.30 + np.array([6.0, 6.0, 8.0])
    # gloss: a highlight along the toe cap and shaft
    rgb += (bell(z, 0.10, 0.03) * bell(y, -0.76, 0.03))[..., None] * 40
    rgb = rgb * (1 - 0.5 * (y < -0.792)[..., None])  # the sole, below the welt
    strip = band(y, -0.788, 0.0040)
    heel = band(z, -0.06, 0.006) * (y > -0.78) * (y < -0.70)
    rgb = glow(rgb, np.maximum(strip, heel), NEON)
    rgb = np.where(inside[..., None], rgb, src * 0.3)
    return (pos, tri, uv), Image.fromarray(np.uint8(np.clip(rgb, 0, 255)))


def meshes(fabrics, leather):
    geometry, texture = clear_jacket(fabrics)
    yield "clear-pvc-jacket", "Clear PVC jacket", "top", geometry, texture
    geometry, texture = neon_visor()
    yield "neon-visor", "Neon visor", "face", geometry, texture
    geometry, texture = tech_boots(leather)
    yield "tech-boots", "Tech boots", "feet", geometry, texture


MESH_IDS = ("clear-pvc-jacket", "neon-visor", "tech-boots")

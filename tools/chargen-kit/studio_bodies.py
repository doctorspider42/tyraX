"""Bake the studio body sculpts and interchangeable workwear into the native kit.

Coordinates below use the shared rig's neutral, Y-up metre space. Each
garment is rebound after construction of the custom skin surface.
"""

import io
import json
from pathlib import Path

import numpy as np
from PIL import Image

ART = Path(__file__).with_name("studio")
SIZE = 512
# ID, display name, slot, garment ease in metres, source panel.
GARMENTS = (
    ("mill-flannel", "Mill flannel", "top", 0.016, 1),
    ("watch-henley", "Watch henley", "top", 0.008, 2),
    ("wash-jeans", "Washed jeans", "bottom", 0.013, 3),
    ("ranger-cargos", "Ranger cargos", "bottom", 0.023, 4),
    ("station-cords", "Station cords", "bottom", 0.017, 5),
    ("dock-gloves", "Dock gloves", "hands", 0.003, 2),
    ("ribbed-top", "Ribbed top", "top", 0.006, 6),
    ("button-cardigan", "Button cardigan", "top", 0.012, 7),
    ("moss-dress", "Moss dress", "full", 0.008, 9),
)


def bell(value, center, width):
    return np.exp(-(((value - center) / width) ** 2))


def ramp(value, low, high):
    t = np.clip((value - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)


def sculpt(points):
    x, y, z = points.T
    ax = abs(x)
    out = points.copy()
    front = ramp(z, 0.048, 0.105)
    # Taper the mandible into the chin; build distinct cheek, orbital and
    # nasal planes. The eyeballs and their pivots undergo the same warp.
    out[:, 0] *= 1 - 0.092 * bell(y, 0.663, 0.035) * front
    out[:, 2] += front * (
        0.0075 * bell(ax, 0.044, 0.022) * bell(y, 0.708, 0.019)
        - 0.0050 * bell(ax, 0.037, 0.020) * bell(y, 0.680, 0.021)
        + 0.0040 * bell(ax, 0.031, 0.023) * bell(y, 0.748, 0.010)
        + 0.0045 * bell(x, 0, 0.014) * bell(y, 0.696, 0.018)
        + 0.0035 * bell(x, 0, 0.028) * bell(y, 0.652, 0.012)
    )
    # A shorter nape, sloping clavicles, fuller deltoids and less cylindrical
    # legs define the body even when the wardrobe is empty.
    orbit = bell(ax, 0.031, 0.022) * bell(y, 0.729, 0.013) * front
    out[:, 1] -= 0.26 * (y - 0.727) * orbit
    out[:, 2] -= 0.0030 * orbit
    out[:, 2] += 0.004 * bell(x, 0, 0.027) * bell(y, 0.663, 0.009) * front
    head = ramp(y, 0.565, 0.672)
    out[:, 1] -= 0.018 * head
    out[:, 1] -= 0.010 * bell(ax, 0.153, 0.064) * bell(y, 0.530, 0.083)
    arm = ramp(ax, 0.060, 0.183) * ramp(y, 0.045, 0.180) * (1 - ramp(y, 0.56, 0.66))
    out[:, 0] -= np.sign(x) * 0.024 * arm
    out[:, 1] -= 0.005 * arm * bell(y, 0.51, 0.11)
    out[:, 0] *= 1 - 0.042 * bell(y, 0.20, 0.108)
    out[:, 2] += (
        0.008 * bell(ax, 0.098, 0.080) * bell(y, 0.427, 0.085) * ramp(z, 0.025, 0.10)
    )
    out[:, 2] += 0.006 * bell(y, -0.225, 0.12) * ramp(z, 0.024, 0.10)
    out[:, 2] -= 0.005 * bell(y, -0.49, 0.09) * (1 - ramp(z, -0.025, 0.02))
    return out


def body_relief(xyz, male):
    """Diffuse tone across the clavicles, sternum, flanks and shoulder blades."""
    x, y, z = xyz
    ax = abs(x)
    chest = ramp(z, 0.04, 0.10)
    back = 1 - ramp(z, -0.035, 0.012)
    collar = 0.521 + 0.13 * ax
    arch = 0.333 + 0.24 * ax
    tone = chest * (
        0.046 * bell(y, collar, 0.010) * bell(ax, 0.085, 0.064)
        - 0.071 * bell(y, collar - 0.015, 0.013) * bell(ax, 0.092, 0.055)
        - 0.063 * bell(x, 0, 0.017) * bell(y, 0.453, 0.067)
        - (0.078 if male else 0.105) * bell(y, arch, 0.013) * bell(ax, 0.074, 0.049)
        - 0.047 * bell(y, 0.29 + 0.34 * ax, 0.022) * bell(ax, 0.092, 0.045)
        + 0.041 * bell(y, 0.110 + 0.17 * ax, 0.018) * bell(ax, 0.110, 0.040)
    )
    tone += back * (
        -0.073 * bell(ax, 0, 0.015) * bell(y, 0.29, 0.19)
        - 0.064 * bell(ax, 0.055 + 0.36 * (y - 0.40), 0.016) * bell(y, 0.43, 0.070)
        + 0.040 * bell(ax, 0.094, 0.040) * bell(y, 0.465, 0.053)
    )
    return 1 + tone[..., None]


def surface_map(pos, tris, uv, parts):
    """Inverse rasterization: one neutral-space point per body-atlas texel."""
    pixels = np.zeros((SIZE, SIZE, 3), np.float32)
    best = np.full((SIZE, SIZE), -np.inf, np.float32)
    uv = uv.copy()
    uv[:, :, 1] = 1 - uv[:, :, 1]
    for ids, corners in zip(tris[parts == 0], uv[parts == 0] * SIZE - 0.5):
        a, b, c = corners
        u, v = b - a, c - a
        det = u[0] * v[1] - v[0] * u[1]
        if abs(det) < 1.0e-6:
            continue
        left, top = np.maximum(0, np.floor(corners.min(axis=0) - 2).astype(int))
        right, bottom = np.minimum(SIZE, np.ceil(corners.max(axis=0) + 3).astype(int))
        yy, xx = np.mgrid[top:bottom, left:right]
        dx, dy = xx - a[0], yy - a[1]
        wb = (dx * v[1] - dy * v[0]) / det
        wc = (dy * u[0] - dx * u[1]) / det
        wa = 1 - wb - wc
        # Signed distance to the nearest edge; fill a two-texel gutter.
        lengths = np.maximum(
            [np.linalg.norm(c - b), np.linalg.norm(v), np.linalg.norm(u)], 0.01
        )
        dist = abs(det) * np.minimum(
            np.minimum(wa / lengths[0], wb / lengths[1]), wc / lengths[2]
        )
        score = best[top:bottom, left:right]
        use = (dist > score) & (dist >= -2)
        pa, pb, pc = pos[ids]
        xyz = pa + wb[..., None] * (pb - pa) + wc[..., None] * (pc - pa)
        pixels[top:bottom, left:right][use] = xyz[use]
        score[use] = dist[use]
    return pixels.transpose(2, 0, 1)


def sample(image, u, v):
    h, w = image.shape[:2]
    return image[
        np.clip(v * (h - 1), 0, h - 1).astype(int),
        np.clip(u * (w - 1), 0, w - 1).astype(int),
    ]


def face_detail(atlas, male, xyz):
    x, y, z = xyz
    source = atlas[:, 512:] if male else atlas[:, :512]
    u = 0.5 + x / 0.124
    v = np.interp(
        y, [0.636, 0.665, 0.691, 0.729, 0.755], [0.99, 0.785, 0.554, 0.210, 0.005]
    )
    rgb = sample(source, u, v)
    # Retain independently moving eyelids/eyes and the original lip boundary,
    # eyebrow paint and ancestry/age layers. Transfer local pigmentation and
    # relief relative to each portrait's own cheek colour.
    ref = np.median(source[185:225, 78:116], axis=(0, 1))
    relief = np.clip((rgb + 1.0) / (ref + 1.0), 0.42, 1.48)
    mask = ramp(z, 0.090, 0.130) * ramp(y, 0.638, 0.652) * (1 - ramp(y, 0.742, 0.759))
    mask *= 1 - ramp(abs(x), 0.045, 0.069)
    mask *= 1 - 0.99 * bell(abs(x), 0.031, 0.017) * bell(y, 0.729, 0.009)
    mask *= 1 - 0.82 * bell(x, 0, 0.029) * bell(y, 0.669, 0.007)
    return relief, mask[..., None]


def garment_paint(tile, xyz, top):
    x, y, z = xyz
    if top:
        u = 0.5 + x / 0.34
        v = (0.556 - y) / 0.485
        # Sleeves and the back use the uninterrupted side panel; a zipper or
        # chest pocket must appear only once on the garment.
        torso = (abs(x) < 0.18) & (z > 0.023)
        u = np.where(torso, u, 0.015 + 0.14 * (0.5 + 0.5 * np.sin(19 * x + 24 * z)))
    else:
        leg_center = np.interp(y, [-0.81, -0.37, 0.06], [0.22, 0.157, 0.105])
        u = 0.51 + (abs(x) - leg_center) / 0.35
        v = (0.130 - y) / 0.915
        u = np.where(z < -0.018, 0.29 + 0.09 * np.sin(x * 23), u)
    return sample(tile, u, v)


def cut(gid, xyz):
    """Signed distances to the hem, neckline, cuffs and waist in rest space."""
    x, y, z = xyz
    ax = abs(x)
    wrist = (ax - 0.431) * 0.34 + (z - 0.176) * 0.65 + (0.245 - y) * 0.65
    if gid in ("ribbed-top", "button-cardigan", "moss-dress"):
        front = ramp(z, 0.025, 0.10)
        hem = 0.155 if gid == "ribbed-top" else 0.115
        if gid == "button-cardigan":
            neckline = 0.565 - 0.100 * bell(x, 0, 0.073) * front
            cuff = 1 - ramp(wrist, -0.018, -0.011)
        else:
            neckline = 0.557 - 0.080 * bell(x, 0, 0.067) * front
            cuff = 1 - ramp(
                ax, 0.118 + 0.024 * (1 - front), 0.145 + 0.018 * (1 - front)
            )
        return (
            ramp(y, hem, hem + 0.006) * (1 - ramp(y, neckline, neckline + 0.006)) * cuff
        )
    if gid == "dock-gloves":
        return ramp(ax, 0.34, 0.37) * ramp(wrist, -0.032, -0.027)
    if gid in ("mill-flannel", "watch-henley"):
        short = gid == "watch-henley"
        front = ramp(z, 0.03, 0.11)
        collar = 0.584 - 0.050 * front + 0.012 * ramp(ax, 0.040, 0.095)
        sleeve = (0.427 - y) * 0.72 + (ax - 0.23) * 0.62
        cuff = (
            1 - ramp(sleeve, 0.097, 0.102) if short else 1 - ramp(wrist, -0.016, -0.010)
        )
        return (
            ramp(y, 0.079 if short else 0.061, 0.085 if short else 0.067)
            * (1 - ramp(y, collar, collar + 0.005))
            * cuff
        )
    waist = 0.144 - 0.014 * ramp(z, 0.04, 0.13)
    hem = {"wash-jeans": -0.749, "ranger-cargos": -0.725, "station-cords": -0.740}[gid]
    return (
        ramp(y, hem, hem + 0.006)
        * (1 - ramp(y, waist, waist + 0.005))
        * (1 - ramp(ax, 0.29, 0.33))
    )


def make_shell(chunks, prefix, rest, xyz, skin_pixels, gid, label, slot, ease, tile):
    from build_kit import jpg_bytes, png_bytes, key_colour

    surface = cut(gid, rest.T)
    idx = np.flatnonzero(surface > 0.002).astype("<i4")
    group = prefix + "g/" + gid
    folds = 0.80 + 0.20 * np.cos(rest[idx, 1] * (65 if slot == "top" else 53))
    distance = (ease * folds * surface[idx]).astype("<f4")
    chunks[group + "/inflIdx"] = 1, idx.tobytes(), idx.size
    for name in ("inflDist", "inflDistM"):
        chunks[group + "/" + name] = 0, distance.tobytes(), idx.size
    coverage = cut(gid, xyz) * skin_pixels
    if slot == "hands":
        x, y, z = xyz
        rgb = np.uint8(sample(tile, (x * 15) % 1, (y * 12 + z * 4) % 1) * 0.39)
    else:
        rgb = garment_paint(tile, xyz, slot in ("top", "full"))
        if gid in ("ribbed-top", "button-cardigan", "moss-dress"):
            x, y, z = xyz
            u = np.where(
                (abs(x) < 0.17) & (z > 0.015),
                0.5 + x / 0.33,
                0.03 + 0.12 * (0.5 + 0.5 * np.sin(x * 18 + z * 21)),
            )
            v = (0.56 - y) / (0.96 if gid == "moss-dress" else 0.41)
            rgb = sample(tile, u, v)
    paint = jpg_bytes(Image.fromarray(rgb), 91)
    alpha = png_bytes(Image.fromarray(np.uint8(coverage * 255)))
    chunks["img/" + group] = 4, paint, len(paint)
    chunks["img/" + group + "/a"] = 4, alpha, len(alpha)
    color = key_colour(rgb[coverage > 0.9].astype(np.float32) / 255)
    return dict(
        id=gid,
        label=label,
        slot=slot,
        kind="shell",
        sex="",
        layer=1,
        cutout=False,
        dyeable=True,
        color=color.tolist(),
        luma=float(color @ [0.299, 0.587, 0.114]),
    )


def bake(writer):
    from build_kit import jpg_bytes, key_colour

    chunks = {name: (kind, data, count) for name, kind, data, count in writer.chunks}

    def array(key, dtype, columns=1):
        return np.frombuffer(chunks[key][1], dtype).reshape(-1, columns)

    def put_array(key, data):
        kind = chunks[key][0]
        chunks[key] = kind, data.tobytes(), data.size

    def put_image(key, data):
        chunks[key] = 4, data, len(data)

    def put_json(key, value):
        data = json.dumps(value, separators=(",", ":")).encode()
        chunks[key] = 5, data, len(data)

    portraits = np.asarray(Image.open(ART / "faces.jpg").convert("RGB"), np.float32)
    fabrics = np.asarray(Image.open(ART / "workwear.jpg").convert("RGB"))
    feminine = np.asarray(Image.open(ART / "women-workwear.jpg").convert("RGB"))
    face_variants = np.asarray(
        Image.open(ART / "face-variants.jpg").convert("RGB"), np.float32
    )
    new_items = {}
    from studio_gear import models, bind_item, hairstyle, crowd_mesh, skirt, HAIR_STYLES
    from studio_topology import refine
    from custom_body import rebuild

    gear = list(models(fabrics))
    lods = {"hero": {}, "standard": {}, "crowd": {}}
    hair_styles = [gid for gid, _ in HAIR_STYLES]
    for gid, _, slot, geometry, _ in gear:
        if slot == "hair":
            for level in ("standard", "crowd"):
                lods[level][gid] = hairstyle(hair_styles.index(gid), level)
        elif gid in ("a-line-skirt", "moss-dress"):
            for level in ("standard", "crowd"):
                lods[level][gid] = skirt(gid == "moss-dress", level)
        elif slot in ("feet", "head", "top"):
            lods["crowd"][gid] = crowd_mesh(geometry, 520 if slot == "top" else 160)
    rig_names = json.loads(chunks["rig/names"][1])
    for body in json.loads(chunks["bodies"][1]):
        p = body["prefix"]
        refine(chunks, p, body["detail"])
        rebuild(chunks, p, body["detail"], body["sex"])
        rest = array(p + "mesh/pos", "<f4", 3)
        tris = array(p + "mesh/tri", "<i4", 3)
        uv = array(p + "mesh/uv", "<f4", 6).reshape(-1, 3, 2)
        parts = array(p + "mesh/part", "u1").ravel()
        xyz = surface_map(rest, tris, uv, parts)
        put_array(p + "mesh/pos", sculpt(rest))
        for male in (False, True):
            sex = "male" if male else "female"
            relief, face_mask = face_detail(portraits, male, xyz)
            anatomy = body_relief(xyz, male)
            for variant in range(3):
                alt, _ = face_detail(
                    face_variants[variant * 512 : (variant + 1) * 512], male, xyz
                )
                ratio = np.clip(alt / np.maximum(relief, 0.2), 0, 1.99)
                layer = np.uint8(np.dstack((ratio * 127.5, face_mask[:, :, 0] * 255)))
                buf = io.BytesIO()
                Image.fromarray(layer).save(buf, "PNG")
                put_image(
                    "img/" + p + "face/" + str(variant + 1) + "-" + sex, buf.getvalue()
                )
            for key in list(chunks):
                if key.startswith("img/" + p + "skin/") and "-" + sex + "-" in key:
                    skin = np.asarray(
                        Image.open(io.BytesIO(chunks[key][1])).convert("RGB")
                    )
                    # Older skins retain more of their baked age detail.
                    amount = 0.70 if key.endswith("-old") else 1.0
                    x, y, z = xyz
                    cheek = (
                        (abs(abs(x) - 0.044) < 0.012)
                        & (abs(y - 0.704) < 0.009)
                        & (z > 0.099)
                    )
                    base = np.median(skin[cheek], axis=0) * [0.95, 0.92, 0.90]
                    mix = face_mask * amount * 0.88
                    result = np.clip(
                        skin * anatomy * (1 - mix) + base * relief * mix, 0, 255
                    ).astype("uint8")
                    put_image(key, jpg_bytes(Image.fromarray(result), 94))

        h, w = fabrics.shape[0] // 2, fabrics.shape[1] // 3
        eye_key = "img/" + p + "eye"
        eye = np.array(Image.open(io.BytesIO(chunks[eye_key][1])).convert("RGBA"))
        skin_pixels = np.any(xyz != 0, axis=0) & (eye[:, :, 3] < 128)
        ex, ey, ez = surface_map(rest, tris, uv, (parts == 0).astype("u1"))
        dx, dy = np.abs(ex) - 0.031, ey - 0.728
        radius = np.hypot(dx, dy) / 0.0064
        iris = (1 - ramp(radius, 0.90, 1.05)) * ramp(ez, 0.124, 0.131)
        angle = np.arctan2(dy, dx)
        fiber = 0.11 * np.sin(27 * angle + 2 * radius) + 0.08 * np.sin(
            73 * angle + radius
        )
        tone = (0.56 + fiber) * (1 - 0.63 * ramp(radius, 0.73, 0.98)) * ramp(
            radius, 0.34, 0.48
        ) + 0.026
        glint = bell(dx, -0.0018, 0.00085) * bell(dy, 0.0022, 0.0010)
        base = np.array([0.78, 0.75, 0.72])
        rgb = (1 - iris[..., None]) * base + iris[..., None] * tone[..., None]
        eye[:, :, :3] = np.uint8(np.clip(rgb + 0.56 * glint[..., None], 0, 1) * 255)
        from build_kit import png_bytes

        put_image(eye_key, png_bytes(Image.fromarray(eye)))
        put_image(
            "img/" + p + "iris",
            png_bytes(Image.fromarray(np.uint8(255 * iris * (1 - glint)))),
        )
        for gid, label, slot, ease, panel in GARMENTS:
            row, col = divmod(panel, 3)
            tile = fabrics[row * h : (row + 1) * h, col * w : (col + 1) * w]
            if panel >= 6:
                row, col = divmod(panel - 6, 2)
                tile = feminine[
                    row * 512 : (row + 1) * 512, col * 512 : (col + 1) * 512
                ]
            item = make_shell(
                chunks, p, rest, xyz, skin_pixels, gid, label, slot, ease, tile
            )
            new_items.setdefault(gid, item)
        for gid, label, slot, (points, faces, coords), texture in gear:
            source_faces, source_uv = faces, coords
            local = lods[body["detail"]].get(gid)
            if local is not None:
                points, faces, coords = local
                for field, data, kind in [
                    ("lodTri", faces.astype("<i4"), 1),
                    ("lodUV", coords.astype("<f4"), 0),
                ]:
                    chunks[p + "g/" + gid + "/" + field] = (
                        kind,
                        data.tobytes(),
                        data.size,
                    )
            authored = sculpt(points)
            bound = bind_item(
                sculpt(rest),
                tris,
                parts,
                array(p + "mesh/joints", "u1", 4),
                array(p + "mesh/weights", "u1", 4),
                authored,
                slot,
                gid,
                rig_names,
            )
            group = "g/" + gid + "/"
            for field, values in bound.items():
                kind = {"f": 0, "i": 1, "u": 2}[values.dtype.kind]
                chunks[p + group + field] = kind, values.tobytes(), values.size
            if slot == "hair":
                from build_kit import png_bytes

                x, y, z = xyz
                front = np.clip((z + 0.05) / 0.20, 0, 1)
                edge = (
                    0.698 + 0.071 * front**3
                    if gid in ("side-crop", "choppy-pixie", "tide-knot", "low-ponytail")
                    else 0.683 + 0.084 * front**4
                )
                alpha = np.uint8(255 * ramp(y, edge, edge + 0.018))
                rgb = np.zeros((SIZE, SIZE, 3), np.uint8) + [40, 28, 22]
                put_image(
                    "img/" + p + "g/" + gid + "/body",
                    jpg_bytes(Image.fromarray(rgb.astype("uint8"))),
                )
                put_image(
                    "img/" + p + "g/" + gid + "/body/a",
                    png_bytes(Image.fromarray(alpha)),
                )
            if gid in new_items and gid != "moss-dress":
                continue
            for field, values, kind in [
                ("tri", source_faces.astype("<i4"), 1),
                ("uv", source_uv.astype("<f4"), 0),
            ]:
                chunks[group + field] = kind, values.tobytes(), values.size
            buf = io.BytesIO()
            texture.save(buf, format="PNG", optimize=True)
            put_image("img/g/" + gid, buf.getvalue())
            pixels = np.asarray(texture.convert("RGBA")).reshape(-1, 4)
            rgb = pixels[pixels[:, 3] > 127, :3].astype(np.float32) / 255
            color = key_colour(rgb)
            new_items[gid] = dict(
                id=gid,
                label=label,
                slot=slot,
                kind="mesh",
                sex="",
                cutout=slot == "hair",
                dyeable=True,
                layer=5 if slot == "hair" else 3,
                luma=float(color @ [0.299, 0.587, 0.114]),
                color=color.tolist(),
            )
    # Ship only the new catalog. Keep a small name translation for existing recipes.
    put_json("wardrobeAliases", json.loads((ART / "legacy_names.json").read_text()))
    put_array("rig/head", sculpt(array("rig/head", "<f4", 3)))
    put_json("wardrobe", list(new_items.values()))
    writer.chunks = [(key, *value) for key, value in chunks.items()]

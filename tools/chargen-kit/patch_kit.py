"""Rebuild wardrobe items inside an existing kit, without the full kit build.

    python patch_kit.py <chargen-kit.bin> <item id> [<item id> ...]
    python patch_kit.py <chargen-kit.bin> --heads <dir>

The full build (make_kit.py) needs the MakeHuman and Quaternius sources. An
item only needs what the kit already holds: each body's sculpted mesh, UVs
and skin weights. For a hairstyle (studio_gear.HAIR_STYLES), a mesh item
(studio_gear.models) or a shell garment (studio_bodies.GARMENTS,
studio_cyber.SHELLS) this does what studio_bodies.bake does - per-detail
geometry, binding, groom and scalp paint or shell paint and push, the texture
and the wardrobe entry - and rewrites the kit in place. A new id goes after
the last item of its slot; an existing one is replaced.

--heads writes the hero heads (kit space, Y up, metres) as head_f.obj and
head_m.obj for the Blender scripts in studio/hair-blender.

Rebuild the editor afterwards: the kit is embedded.
"""
import io
import json
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from build_kit import VERSION, key_colour  # noqa: E402
from PIL import Image  # noqa: E402
from studio_bodies import (ART, GARMENTS, make_shell, scalp_paint, sculpt,  # noqa: E402
                           surface_map)
from studio_cyber import CLEAR, SHELLS  # noqa: E402
from studio_gear import (HAIR_STYLES, MODELLED, bind_item, crowd_mesh,  # noqa: E402
                         hair_atlas, hairstyle, models)


def read(path):
    data = Path(path).read_bytes()
    assert data[:4] == b"TXCK", "not a character kit"
    _, count = struct.unpack_from("<II", data, 4)
    at, chunks = 12, {}
    for _ in range(count):
        (n,) = struct.unpack_from("<H", data, at)
        name = data[at + 2:at + 2 + n].decode()
        at += 2 + n
        kind, _, size = struct.unpack_from("<BBI", data, at)
        at += 6
        nbytes = size * {0: 4, 1: 4, 2: 1, 3: 2, 4: 1, 5: 1}[kind]
        chunks[name] = (kind, data[at:at + nbytes], size)
        at += nbytes + (-nbytes) % 4
    return chunks


def write(path, chunks):
    with open(path, "wb") as f:
        f.write(b"TXCK" + struct.pack("<II", VERSION, len(chunks)))
        for name, (kind, data, size) in chunks.items():
            nb = name.encode()
            f.write(struct.pack("<H", len(nb)) + nb + struct.pack("<BBI", kind, 0, size))
            f.write(data)
            f.write(b"\0" * ((-len(data)) % 4))


def unsculpt(points):
    pre = points.copy()
    for _ in range(12):
        pre -= sculpt(pre) - points
    return pre


def main():
    kit, ids = sys.argv[1], sys.argv[2:]
    chunks = read(kit)

    def array(key, dtype, columns=1):
        return np.frombuffer(chunks[key][1], dtype).reshape(-1, columns)

    def put(key, values):
        values = np.ascontiguousarray(values)
        kind = {"f": 0, "i": 1, "u": 2}[values.dtype.kind]
        chunks[key] = (kind, values.tobytes(), values.size)

    if ids[:1] == ["--heads"]:
        out = Path(ids[1])
        out.mkdir(parents=True, exist_ok=True)
        for prefix, name in (("hf/", "head_f"), ("hm/", "head_m")):
            pos, tri = array(prefix + "mesh/pos", "<f4", 3), array(prefix + "mesh/tri", "<i4", 3)
            keep = pos[tri][:, :, 1].min(1) > 0.45
            with open(out / (name + ".obj"), "w") as f:
                f.writelines("v %f %f %f\n" % tuple(v) for v in pos)
                f.writelines("f %d %d %d\n" % tuple(t + 1) for t in tri[keep])
        print("wrote", out / "head_f.obj", out / "head_m.obj")
        return

    styles = [gid for gid, _ in HAIR_STYLES]
    names = json.loads(chunks["rig/names"][1])
    bodies = json.loads(chunks["bodies"][1])
    wardrobe = json.loads(chunks["wardrobe"][1])
    fabrics = np.asarray(Image.open(ART / "workwear.jpg").convert("RGB"))
    feminine = np.asarray(Image.open(ART / "women-workwear.jpg").convert("RGB"))
    shells = {g[0]: g for g in GARMENTS + tuple(c[:5] for c in SHELLS)}

    def place(entry):
        at = next((i for i, w in enumerate(wardrobe) if w["id"] == entry["id"]), None)
        if at is None:
            same = [i for i, w in enumerate(wardrobe) if w["slot"] == entry["slot"]]
            wardrobe.insert(max(same) + 1 if same else len(wardrobe), entry)
        else:
            wardrobe[at] = entry

    def body_arrays(body):
        p = body["prefix"]
        rest = array(p + "mesh/pos", "<f4", 3).astype(float)  # already sculpted
        tris = array(p + "mesh/tri", "<i4", 3)
        parts = array(p + "mesh/part", "u1").ravel()
        uv = array(p + "mesh/uv", "<f4", 6).reshape(-1, 3, 2)
        return p, rest, tris, parts, uv

    for gid in ids:
        for stale in [k for k in chunks if ("g/" + gid + "/") in k or k.endswith("g/" + gid)]:
            del chunks[stale]
        if gid in shells:  # painted into the body atlas
            _, label, slot, ease, panel = shells[gid]
            h, w = fabrics.shape[0] // 2, fabrics.shape[1] // 3
            row, col = divmod(panel, 3)
            tile = fabrics[row * h:(row + 1) * h, col * w:(col + 1) * w]
            if panel >= 6:
                row, col = divmod(panel - 6, 2)
                tile = feminine[row * 512:(row + 1) * 512, col * 512:(col + 1) * 512]
            for body in bodies:
                p, rest, tris, parts, uv = body_arrays(body)
                pre = unsculpt(rest)
                xyz = surface_map(pre, tris, uv, parts)
                eye = np.array(Image.open(io.BytesIO(chunks["img/" + p + "eye"][1])).convert("RGBA"))
                skin = np.any(xyz != 0, axis=0) & (eye[:, :, 3] < 128)
                entry = make_shell(chunks, p, pre, xyz, skin, gid, label, slot, ease, tile)
                print(gid, p or "(female)", "shell")
            place(entry)
            continue
        if gid in styles:  # a hairstyle
            index = styles.index(gid)
            label, slot = HAIR_STYLES[index][1], "hair"
            geometry = {level: hairstyle(index, level) for level in ("hero", "standard", "crowd")}
            texture = hair_atlas(index)
        else:  # another mesh item (studio_gear.models)
            found = next(m for m in models(fabrics) if m[0] == gid)
            _, label, slot, hero, texture = found
            geometry = {"hero": hero, "standard": None, "crowd": None}
            if slot in ("feet", "head", "top"):
                geometry["crowd"] = crowd_mesh(hero, 520 if slot == "top" else 160)
        for body in bodies:
            p, rest, tris, parts, uv = body_arrays(body)
            local = geometry[body["detail"]]
            points, faces, coords = local if local is not None else geometry["hero"]
            if body["detail"] != "hero" and local is not None:
                put(p + "g/" + gid + "/lodTri", faces.astype("<i4"))
                put(p + "g/" + gid + "/lodUV", coords.astype("<f4"))
            bound = bind_item(rest, tris, parts, array(p + "mesh/joints", "u1", 4),
                              array(p + "mesh/weights", "u1", 4), sculpt(points),
                              slot, gid, names)
            for field, values in bound.items():
                put(p + "g/" + gid + "/" + field, values)
            if slot == "hair":
                xyz = surface_map(unsculpt(rest), tris, uv, parts)
                groom = (points, faces) if gid in MODELLED else None
                for key, data in scalp_paint(xyz, gid, groom).items():
                    chunks["img/" + p + "g/" + gid + key] = (4, data, len(data))
            print(gid, p or "(female)", body["detail"], len(faces), "tris")
        points, faces, coords = geometry["hero"]
        put("g/" + gid + "/tri", faces.astype("<i4"))
        put("g/" + gid + "/uv", coords.astype("<f4"))
        buf = io.BytesIO()
        texture.save(buf, format="PNG", optimize=True)
        chunks["img/g/" + gid] = (4, buf.getvalue(), len(buf.getvalue()))
        pixels = np.asarray(texture.convert("RGBA")).reshape(-1, 4)
        color = key_colour(pixels[pixels[:, 3] > 127 if slot == "hair" else pixels[:, 3] > 0, :3]
                           .astype(np.float32) / 255)
        place(dict(id=gid, label=label, slot=slot, kind="mesh", sex="", cutout=slot == "hair",
                   clear=gid in CLEAR, dyeable=True, layer=5 if slot == "hair" else 3,
                   luma=float(color @ [0.299, 0.587, 0.114]), color=color.tolist()))
    data = json.dumps(wardrobe, separators=(",", ":")).encode()
    chunks["wardrobe"] = (5, data, len(data))
    write(kit, chunks)
    print("wrote", kit)


if __name__ == "__main__":
    main()

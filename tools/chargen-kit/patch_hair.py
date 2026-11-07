"""Rebuild hairstyles inside an existing kit, without the full kit build.

    python patch_hair.py <chargen-kit.bin> <hair id> [<hair id> ...]
    python patch_hair.py <chargen-kit.bin> --heads <dir>

The full build (make_kit.py) needs the MakeHuman and Quaternius sources. A
hairstyle only needs what the kit already holds: each body's sculpted mesh,
UVs and skin weights. For every id in studio_gear.HAIR_STYLES this does what
studio_bodies.bake does for hair - per-detail geometry, binding, groom, scalp
paint, the atlas and the wardrobe entry - and rewrites the kit in place. A new
id is inserted after the last hairstyle; an existing one is replaced.

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
from studio_bodies import sculpt, scalp_paint, surface_map  # noqa: E402
from studio_gear import HAIR_STYLES, MODELLED, bind_item, hair_atlas, hairstyle  # noqa: E402


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
    for gid in ids:
        index = styles.index(gid)
        label = HAIR_STYLES[index][1]
        geometry = {level: hairstyle(index, level) for level in ("hero", "standard", "crowd")}
        for stale in [k for k in chunks if ("g/" + gid + "/") in k or k.endswith("g/" + gid)]:
            del chunks[stale]
        for body in bodies:
            p = body["prefix"]
            rest = array(p + "mesh/pos", "<f4", 3).astype(float)  # already sculpted
            tris = array(p + "mesh/tri", "<i4", 3)
            parts = array(p + "mesh/part", "u1").ravel()
            uv = array(p + "mesh/uv", "<f4", 6).reshape(-1, 3, 2)
            points, faces, coords = geometry[body["detail"]]
            if body["detail"] != "hero":
                put(p + "g/" + gid + "/lodTri", faces.astype("<i4"))
                put(p + "g/" + gid + "/lodUV", coords.astype("<f4"))
            bound = bind_item(rest, tris, parts, array(p + "mesh/joints", "u1", 4),
                              array(p + "mesh/weights", "u1", 4), sculpt(points),
                              "hair", gid, names)
            for field, values in bound.items():
                put(p + "g/" + gid + "/" + field, values)
            xyz = surface_map(unsculpt(rest), tris, uv, parts)
            groom = (points, faces) if gid in MODELLED else None
            for key, data in scalp_paint(xyz, gid, groom).items():
                chunks["img/" + p + "g/" + gid + key] = (4, data, len(data))
            print(gid, p or "(female)", body["detail"], len(faces), "tris")
        points, faces, coords = geometry["hero"]
        put("g/" + gid + "/tri", faces.astype("<i4"))
        put("g/" + gid + "/uv", coords.astype("<f4"))
        texture = hair_atlas(index)
        buf = io.BytesIO()
        texture.save(buf, format="PNG", optimize=True)
        chunks["img/g/" + gid] = (4, buf.getvalue(), len(buf.getvalue()))
        pixels = np.asarray(texture.convert("RGBA")).reshape(-1, 4)
        color = key_colour(pixels[pixels[:, 3] > 127, :3].astype(np.float32) / 255)
        entry = dict(id=gid, label=label, slot="hair", kind="mesh", sex="", cutout=True,
                     dyeable=True, layer=5, luma=float(color @ [0.299, 0.587, 0.114]),
                     color=color.tolist())
        at = next((i for i, w in enumerate(wardrobe) if w["id"] == gid), None)
        if at is None:
            at = max(i for i, w in enumerate(wardrobe) if w["slot"] == "hair") + 1
            wardrobe.insert(at, entry)
        else:
            wardrobe[at] = entry
    data = json.dumps(wardrobe, separators=(",", ":")).encode()
    chunks["wardrobe"] = (5, data, len(data))
    write(kit, chunks)
    print("wrote", kit)


if __name__ == "__main__":
    main()

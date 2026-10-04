"""Mark the cars' exhaust pipes without re-exporting them.

Run after make-ravager.py / make-pica.py / make-strix.py (and
add-fast-wheels.py, in either order):
    blender --background --python authoring/add-exhaust-markers.py -- --project .

Each car script lists its pipe openings as EXHAUSTS (design metres, Blender
axes: +X forward, +Y left, +Z up). This writes one "exhaust" node per opening
into the shipped GLB - exactly what the car scripts' own export now produces
for those empties (carkit.place_exhausts): the opening times SCALE, the arrow
pointing out of the back. The vehicle import reads them as the pipes the
nitrous flame, the shift backfire and the exhaust smoke come out of
(docs/vehicles.md, "Exhaust pipes").

Only the JSON chunk changes; every mesh, material and image byte survives. The
result is repeatable: existing exhaust nodes are replaced, never duplicated.
"""
import argparse
import importlib.util
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# An empty rotated (0, -90 deg, 0) in Blender - its arrow (local +Z) along -X -
# as the glTF exporter writes it (+Y up): a quarter turn about Z, local +Y out.
ARROW_BACK = [0.0, 0.0, 0.7071068286895752, 0.7071068286895752]


def read_glb(path):
    raw = path.read_bytes()
    size = struct.unpack_from("<I", raw, 12)[0]
    doc = json.loads(raw[20:20 + size])
    binary_size = struct.unpack_from("<I", raw, 20 + size)[0]
    return doc, raw[28 + size:28 + size + binary_size]


def write_glb(path, doc, binary):
    doc["buffers"][0]["byteLength"] = len(binary)
    encoded = json.dumps(doc, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary += b"\0" * (-len(binary) % 4)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(encoded) + len(binary))
                     + struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
                     + struct.pack("<II", len(binary), 0x004E4942) + binary)


def drop_exhausts(doc):
    """Remove every exhaust* node, renumbering the references that remain."""
    keep = [i for i, n in enumerate(doc["nodes"])
            if not n.get("name", "").lower().startswith("exhaust")]
    remap = {old: new for new, old in enumerate(keep)}
    doc["nodes"] = [doc["nodes"][i] for i in keep]
    for n in doc["nodes"]:
        if "children" in n:
            n["children"] = [remap[c] for c in n["children"] if c in remap]
    for sc in doc.get("scenes", []):
        sc["nodes"] = [remap[c] for c in sc.get("nodes", []) if c in remap]
    for sk in doc.get("skins", []):
        sk["joints"] = [remap[j] for j in sk["joints"]]


def load_car(name):
    path = HERE / ("make-" + name + ".py")
    spec = importlib.util.spec_from_file_location("make_" + name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--project", type=pathlib.Path, default=HERE.parent)
    options = args.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    for name in ("ravager", "pica", "strix"):
        car = load_car(name)
        path = options.project / "res" / "models" / (name + ".glb")
        doc, binary = read_glb(path)
        drop_exhausts(doc)
        s = car.SCALE
        for k, (x, y, z) in enumerate(car.EXHAUSTS):
            doc["nodes"].append({
                "name": "exhaust" if k == 0 else "exhaust.%03d" % k,
                "rotation": ARROW_BACK,
                # Blender (x, y, z) -> glTF (x, z, -y)
                "translation": [x * s, z * s, -y * s]})
            doc["scenes"][0]["nodes"].append(len(doc["nodes"]) - 1)
        write_glb(path, doc, binary)
        print(f"{name}: {len(car.EXHAUSTS)} exhaust marker(s)")


if __name__ == "__main__":
    main()

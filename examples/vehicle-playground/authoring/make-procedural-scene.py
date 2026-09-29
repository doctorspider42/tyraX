"""Recreate the painted procedural district using the existing urban assets.

Run from any directory, then run tyrax-editor --refresh-gen on this project.
Only the procedural scene and its authored objects/terrain are replaced.
"""
import copy
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
manifest = ROOT / "vehicle-playground.tyra"
text = manifest.read_text(encoding="utf-8")
project = json.loads(text)
main = project["scenes"][0]
scene = copy.deepcopy(main)
scene["name"] = "procedural"
scene["objects"] = []
scene.pop("roadJunctions", None)
scene["settings"]["terrainMaterial"] = "res/materials/procedural-grass.mtl"
scene["overrides"]["terrainMat"] = True
scene["terrainLayers"] = [{"name": "Concrete", "material": "res/materials/procedural-concrete.mtl", "scale": 1}]

def save_object(obj):
    (ROOT / "objects" / (obj["id"] + ".json")).write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8")
    scene["objects"].append(obj["id"])

for old_id in main["objects"]:
    obj = json.loads((ROOT / "objects" / (old_id + ".json")).read_text())
    if obj["type"] not in ("road", "player", "spawn", "spawnPoint", "vehicle"):
        continue
    obj["id"] = hashlib.sha256(("procedural:" + old_id).encode()).hexdigest()[:16]
    # No car-specific or district-specific logic is needed in this authoring demo.
    obj.pop("flowGraph", None)
    save_object(obj)

nodes, links = [], []
def node(i, kind, x, y, nums=None, rows=None):
    n = {"id": i, "type": kind, "pos": [x, y]}
    if nums is not None:
        n["nums"] = nums
    if rows is not None:
        n["rows"] = rows
    nodes.append(n)

def connect(a, b, pin=0):
    links.append({"id": 100 + len(links), "from": a, "fromPin": 0, "to": b, "toPin": pin})

for base, y, layer, assets, density in (
    (1, 0, 0, ["district-workshop", "district-loft", "district-tower"], 3),
    (11, 430, -1, ["tree-park-large", "tree-park-pine-large"], 2),
):
    node(base, "ScatterSurface", 0, y, {"density": density, "max": 12000})
    asset_scale = {"tree-park-large": 5, "tree-park-pine-large": 7}
    node(base+1, "PickAsset", 280, y, rows=[{"s": "res/models/urban/" + a + ".obj", "v": [1, asset_scale.get(a, 1), asset_scale.get(a, 1), 0]} for a in assets])
    node(base+2, "Vary", 560, y, {"yaw": 360, "tilt": 0, "align": 0, "jitter": 0, "scalemin": 0.85, "scalemax": 1.1})
    node(base+3, "FilterPlacement", 840, y, {"roads": 1, "collisions": 1, "scene": 0, "clearance": 1, "material": 1, "layer": layer, "coverage": 1})
    connect(base, base+1)
    connect(base+1, base+2)
    connect(base+2, base+3)
node(21, "Merge", 1140, 200)
node(22, "FilterPlacement", 1420, 200, {"roads": 0, "collisions": 1, "scene": 0, "clearance": 1, "material": 0})
node(23, "Output", 1700, 200, {"cell": 64, "budget": 90000, "collide": 0, "shadow": 0})
connect(4, 21)
connect(14, 21, 1)
connect(21, 22)
connect(22, 23)
save_object({"id": "procpaint00000001", "name": "Painted district scatter", "type": "scatter",
             "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [280, 30, 280],
             "collision": "none", "castShadow": False,
             "procGraph": {"seed": 42, "nextId": 200, "nodes": nodes, "links": links}})

# Concrete lots alternate with wide grass strips. All cells are authored at
# the existing 4-unit terrain resolution; the filter examines every splat cell.
w = d = 81
(ROOT / "terrain-procedural.heights").write_text(f"{w} {d}\n" + (" ".join(["0"]*w) + "\n")*d)
paint = bytearray()
for z in range(d):
    for x in range(w):
        wx, wz = x*4-160, z*4-160
        concrete = any(a <= wx <= b for a, b in [(-100, -48), (-24, 24), (48, 100)]) and abs(wz) <= 100
        paint.append(255 if concrete else 0)
(ROOT / "terrain-procedural.splat").write_bytes(b"TXSP" + struct.pack("<iii", w, d, 1) + paint)
(ROOT / "res/materials/procedural-grass.mtl").write_text("# Grass base for the painted procedural district\nnewmtl grass\nKd 0.23 0.43 0.16\n")
(ROOT / "res/materials/procedural-concrete.mtl").write_text("# Concrete lots for the painted procedural district\nnewmtl concrete\nKd 0.6 0.62 0.64\n")

# Preserve the rest of the manifest's formatting and all existing scene data.
start = text.index('  "scenes": [')
end = text.index('\n  "fonts":', start)
scenes = [s for s in project["scenes"] if s["name"] != "procedural"] + [scene]
text = text[:start] + '  "scenes": ' + json.dumps(scenes, indent=2) + ',' + text[end:]
manifest.write_text(text, encoding="utf-8")
print(f"procedural scene: {len(scene['objects'])} authored objects, {len(nodes)} nodes")

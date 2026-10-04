"""Author symmetric motion-blurred wheels without re-exporting the cars.

Run after make-ravager.py / make-pica.py / make-strix.py:
    blender --background --python authoring/add-fast-wheels.py -- --project .

Blender exports only the new wheel. Splicing its accessors into the original
GLB preserves every original mesh, material and embedded image byte for byte.
The result is repeatable: an existing wheel_blur is replaced, never duplicated.
"""
import argparse
import copy
import json
import math
import pathlib
import struct
import sys
import tempfile

import bpy
from mathutils import Vector


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


def positions(doc, binary, accessor_index):
    accessor = doc["accessors"][accessor_index]
    view = doc["bufferViews"][accessor["bufferView"]]
    assert accessor["componentType"] == 5126 and accessor["type"] == "VEC3"
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 12)
    return [struct.unpack_from("<fff", binary, offset + i * stride)
            for i in range(accessor["count"])]


def author_wheel(radius, half_width, dish_ratio, cap_ratio, tone, out):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    verts, faces, material_indices = [], [], []
    # Axle Y, radial XZ in Blender; glTF export maps the axle to Z.
    # No spokes or alternating wedges: those alias at high angular speeds.
    segments = 12
    def point(r, axial, k):
        a = 2 * math.pi * k / segments
        return (r * math.cos(a), axial, r * math.sin(a))

    def face(points, material, outward):
        normal = (Vector(points[1]) - Vector(points[0])).cross(
            Vector(points[2]) - Vector(points[0]))
        if normal.dot(Vector(outward)) < 0:
            points = list(reversed(points))
        index = len(verts)
        verts.extend(points)
        faces.append(tuple(range(index, index + len(points))))
        material_indices.append(material)

    profile = [(radius * dish_ratio, -half_width),
               (radius, -half_width * .69),
               (radius, half_width * .69),
               (radius * dish_ratio, half_width)]
    for k in range(segments):
        mid = 2 * math.pi * (k + .5) / segments
        for (r0, y0), (r1, y1) in zip(profile, profile[1:]):
            face([point(r0, y0, k), point(r0, y0, k + 1),
                  point(r1, y1, k + 1), point(r1, y1, k)], 0,
                 (math.cos(mid), (y0 + y1) / half_width, math.sin(mid)))
    for side in (-1, 1):
        outer_y = side * half_width
        inner_y = side * half_width * .78
        for k in range(segments):
            face([point(radius * dish_ratio, outer_y, k),
                  point(radius * dish_ratio, outer_y, k + 1),
                  point(radius * cap_ratio, inner_y, k + 1),
                  point(radius * cap_ratio, inner_y, k)], 1, (0, side, 0))
            face([point(radius * cap_ratio, inner_y, k),
                  point(radius * cap_ratio, inner_y, k + 1),
                  (0, side * half_width * .96, 0)], 2, (0, side, 0))
    mesh = bpy.data.meshes.new("wheel_blur")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    wheel = bpy.data.objects.new("wheel_blur", mesh)
    bpy.context.collection.objects.link(wheel)
    for name, colour in [("tyre rubber", (.035, .035, .04, 1)),
                         ("rim motion blur", (*tone, 1)),
                         ("rim chrome", (.78, .79, .81, 1))]:
        material = bpy.data.materials.new(name)
        material.use_nodes = True
        material.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = colour
        mesh.materials.append(material)
    for polygon, material in zip(mesh.polygons, material_indices):
        polygon.material_index = material
    wheel.select_set(True)
    bpy.context.view_layer.objects.active = wheel
    bpy.ops.export_scene.gltf(filepath=str(out), export_format="GLB",
                              use_selection=True, export_yup=True,
                              export_normals=True, export_animations=False)


def splice(doc, binary, wheel_doc, wheel_binary):
    # Prior output is a suffix; keep the originals untouched on repeat runs.
    mark = doc.get("extras", {}).get("tyraxFastWheelSource")
    if mark:
        for key, size in mark["counts"].items():
            doc[key] = doc[key][:size]
        for scene, nodes in zip(doc["scenes"], mark["sceneNodes"]):
            scene["nodes"] = nodes
        binary = binary[:mark["binaryLength"]]
    mark = {"counts": {key: len(doc.get(key, [])) for key in
                        ("nodes", "meshes", "accessors", "bufferViews", "materials")},
            "sceneNodes": [list(scene["nodes"]) for scene in doc["scenes"]],
            "binaryLength": len(binary)}
    offsets = mark["counts"]
    binary += b"\0" * (-len(binary) % 4)
    binary_offset = len(binary)
    material_map = {}
    for i, material in enumerate(wheel_doc["materials"]):
        match = next((j for j, original in enumerate(doc["materials"])
                      if original.get("name") == material.get("name")), None)
        if match is None:
            match = len(doc["materials"])
            doc["materials"].append(copy.deepcopy(material))
        material_map[i] = match
    for view in wheel_doc["bufferViews"]:
        view = copy.deepcopy(view)
        view["buffer"] = 0
        view["byteOffset"] = view.get("byteOffset", 0) + binary_offset
        doc["bufferViews"].append(view)
    for accessor in wheel_doc["accessors"]:
        accessor = copy.deepcopy(accessor)
        accessor["bufferView"] += offsets["bufferViews"]
        doc["accessors"].append(accessor)
    mesh = copy.deepcopy(wheel_doc["meshes"][0])
    for primitive in mesh["primitives"]:
        primitive["indices"] += offsets["accessors"]
        primitive["attributes"] = {key: value + offsets["accessors"]
                                    for key, value in primitive["attributes"].items()}
        primitive["material"] = material_map[primitive["material"]]
    doc["meshes"].append(mesh)
    # Exported geometry is already in glTF coordinates; no parent transforms.
    doc["nodes"].append({"name": "wheel_blur", "mesh": offsets["meshes"]})
    doc["scenes"][doc.get("scene", 0)]["nodes"].append(offsets["nodes"])
    doc.setdefault("extras", {})["tyraxFastWheelSource"] = mark
    return binary + wheel_binary


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--project", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    options = args.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    styles = {"ravager": (.625, .20, (.55, .56, .58)),
              "pica": (.66, .28, (.34, .35, .37)),
              "strix": (.68, .14, (.43, .44, .46))}
    with tempfile.TemporaryDirectory(prefix="tyrax-fast-wheel-") as temp:
        for name, (dish, cap, tone) in styles.items():
            path = options.project / "res" / "models" / (name + ".glb")
            doc, binary = read_glb(path)
            node = next(n for n in doc["nodes"] if n.get("name") == "wheel front left")
            points = [p for primitive in doc["meshes"][node["mesh"]]["primitives"]
                      for p in positions(doc, binary, primitive["attributes"]["POSITION"])]
            radius = max(math.hypot(x, y) for x, y, z in points)
            half_width = max(abs(z) for x, y, z in points)
            wheel_path = pathlib.Path(temp) / (name + ".glb")
            author_wheel(radius, half_width, dish, cap, tone, wheel_path)
            wheel_doc, wheel_binary = read_glb(wheel_path)
            result = splice(doc, binary, wheel_doc, wheel_binary)
            write_glb(path, doc, result)
            triangles = sum(wheel_doc["accessors"][p["indices"]]["count"] // 3
                            for p in wheel_doc["meshes"][0]["primitives"])
            print(f"{name}: wheel_blur, {triangles} triangles, R={radius:.5f}, width={2 * half_width:.5f}")


if __name__ == "__main__":
    main()

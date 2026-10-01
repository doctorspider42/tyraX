"""Build the Ravager's FAR model: a hand-built low-poly twin of the Ravager
(make-ravager.py) for cars that are far away or that nobody drives - the
traffic LOD of a period racer, not a decimation.

Run through Blender, AFTER make-ravager.py (it reads that model's texture):

    blender -b --factory-startup --python make-ravager-far.py -- [--full GLB]
            [--out GLB] [--preview DIR]

Writes res/models/ravager-far.glb and, with --preview, renders of the far model
beside the full one (the same views make-ravager.py renders) plus a contact
sheet, ravager-far-sheet.png.

How it stays the same car with a fifth of the triangles:

* The SAME loft. It imports make-ravager.py and samples its own section() - the
  same character lines, the same wheel-arch lift - on 18 stations instead of 34
  (the ends, both arches as five stations each, windscreen base and top, the
  rear window base, the sail end, the ducktail) and 8 of its 12 lines: keel,
  rocker, flank, belt crease, sill, greenhouse rail, glass edge, crown. What the
  dropped lines modelled (tuck-under, fender round, drip rail, the sails' inner
  walls, the recessed grille and tail panel) is carried by the texture.

* The SAME texture, byte for byte: the full model's 256x256 atlas, read out of
  its .glb. The atlas is painted in WORLD coordinates and projected per face
  (make-ravager.py, face_uvs), so any surface lying where the car is samples the
  right paint, crease light, shut lines, chrome and grille. make-ravager.py also
  paints the windows and lamps into the texels its own glass and lamp parts
  cover, and a wheel face into its free cell slot, so this model needs ONE
  material and the far tier is ONE submit with no VRAM of its own (the vehicle
  bake checks the image is pixel-equal to the body's).

* Strip-friendly: continuous quad loops along the body, smooth shading with a
  crease angle, and UVs that are a function of position inside each tile - so
  neighbouring corners weld and the bake's stripifier can follow the loops.

* The import's rules still hold: four identical wheel nodes at the full model's
  hubs (the far bake takes every node in place, so they only have to be where
  the wheels are), the material named "body paint", the full model's SCALE.

The wheels are octagonal: a tread band in the atlas's black cell and a painted
wheel face (tyre sidewall, chrome dish, five slots) on both sides - symmetric,
like the full wheel, so the right side is not the tyre's open back.
"""
import importlib.util
import json
import math
import os
import struct
import sys
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("ravager", os.path.join(HERE, "make-ravager.py"))
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)  # design data + atlas rules; its main() is guarded

FULL = os.path.join(HERE, "..", "res", "models", "ravager.glb")
OUT = os.path.join(HERE, "..", "res", "models", "ravager-far.glb")

# section() indices kept: keel, rocker bottom, flank, belt crease, sill,
# greenhouse rail, glass edge / crown shoulder, crown
LINES = [0, 2, 4, 5, 7, 8, 10, 11]
ARCH_F = (-1.0, -0.7, 0.0, 0.7, 1.0)
WHEEL_SEG = 8


def stations():
    xs = {R.XF, 2.20, R.XWB, R.XWT, R.XRW, R.XSE, -2.78, R.XR}
    for ax in (R.AXF, R.AXR):
        for f in ARCH_F:
            xs.add(ax + f * R.ARCH_R)
    # the roof end is the rear arch's leading station (3.5 cm off XRE)
    out = []
    for x in sorted(xs, reverse=True):
        if not out or out[-1] - x > 0.02:
            out.append(x)
    return out


def far_section(x):
    s = R.section(x)
    return [s[k] for k in LINES]


# faces: (points, uv) - uv is a mode ("auto", "cell:<name>", "tile:<name>",
# "disc") resolved against the full model's atlas; mirror=True adds the twin
class Geo:
    def __init__(self):
        self.faces = []

    def face(self, pts, uv="auto", mirror=True, out=None):
        pts = [Vector(p) for p in pts]
        if out is not None:
            n = Vector((0, 0, 0))
            for i in range(1, len(pts) - 1):
                n += (pts[i] - pts[0]).cross(pts[i + 1] - pts[0])
            if n.dot(Vector(out)) < 0:
                pts.reverse()
        self.faces.append((pts, uv, mirror))


def face_uvs(pts, uv):
    if uv.startswith("tile:"):
        t = uv[5:]
        return [R.tile_uv(t, abs(p.y), p.z) for p in pts]
    if uv == "disc":
        cx, cy, rr = R.WHEEL_DISC
        rad = max(math.hypot(p.x, p.z) for p in pts)
        return [((cx + rr * p.x / rad) / R.TEX, 1.0 - (cy - rr * p.z / rad) / R.TEX) for p in pts]
    return R.face_uvs(pts, uv)[0]


def build_body(g):
    st = stations()
    secs = [far_section(x) for x in st]
    n = len(LINES)
    for i in range(len(st) - 1):
        xa, xb = st[i], st[i + 1]
        for k in range(n - 1):
            a0, a1 = secs[i][k], secs[i][k + 1]
            b0, b1 = secs[i + 1][k], secs[i + 1][k + 1]
            pts = [(xa, a0[0], a0[1]), (xb, b0[0], b0[1]), (xb, b1[0], b1[1]), (xa, a1[0], a1[1])]
            cy = 0.25 * (a0[0] + a1[0] + b0[0] + b1[0])
            cz = 0.25 * (a0[1] + a1[1] + b0[1] + b1[1])
            ref = (0.0, cy, cz - 0.62)
            uv = "auto"
            if k == 0:
                ref, uv = (0.0, 0.2, -1.0), "cell:black"   # floor + wheel wells
            elif k == 1 and R.on_arch(pts):
                uv = "cell:black"                           # arch lip roof
            g.face(pts, uv, out=ref)
    # the end caps, RECESSED exactly like the full model's grille and tail
    # panel: the full model's lamps part stays drawn on the far tier (it is
    # fullbright and carries the brake and head lights), and it sits 1.2 cm
    # in front of those recessed faces - a flat cap would bury it
    for x0, sign, depth, tile in ((R.XF, 1, 0.075, "front"), (R.XR, -1, 0.045, "rear")):
        ring = far_section(x0)
        zc = 0.60
        inner = [(y * 0.94, zc + (z - zc) * 0.90) for (y, z) in ring]
        xi = x0 - sign * depth
        for k in range(len(ring) - 1):
            (y0, z0), (y1, z1) = ring[k], ring[k + 1]
            (u0, w0), (u1, w1) = inner[k], inner[k + 1]
            g.face([(x0, y0, z0), (x0, y1, z1), (xi, u1, w1), (xi, u0, w0)], "cell:black",
                   out=(sign * 0.2, -(y0 + y1), -(z0 + z1 - 2 * zc)))
        left = [(xi, y, z) for (y, z) in inner]
        right = [(xi, -y, z) for (y, z) in reversed(inner) if y > 1e-4]
        g.face(left + right, "tile:" + tile, mirror=False, out=(sign, 0, 0))


def build_trim(g):
    """The chrome blade bumpers, two segments a side, front and top faces."""
    def bar(path, z0, z1, depth):
        for (xa, ya, na), (xb, yb, nb) in zip(path, path[1:]):
            oa, ob = Vector((xa, ya)), Vector((xb, yb))
            ia, ib = oa - Vector(na) * depth, ob - Vector(nb) * depth
            P = lambda v, z: (v.x, v.y, z)
            nout = Vector(((na[0] + nb[0]) * 0.5, (na[1] + nb[1]) * 0.5, 0))
            g.face([P(oa, z0), P(ob, z0), P(ob, z1), P(oa, z1)], "cell:chrome", out=nout)
            g.face([P(oa, z1), P(ob, z1), P(ib, z1), P(ia, z1)], "cell:chrome", out=(0, 0, 1))
        (xe, ye, ne) = path[-1]
        oe = Vector((xe, ye))
        ie = oe - Vector(ne) * depth
        g.face([(oe.x, oe.y, z0), (ie.x, ie.y, z0), (ie.x, ie.y, z1), (oe.x, oe.y, z1)],
               "cell:chrome", out=(-1, 0, 0) if ne[0] == 0 and xe > 0 else (1, 0, 0))
    wf = R.half_w(R.XF - 0.1)
    wr = R.half_w(R.XR + 0.1)
    s2 = math.sqrt(0.5)
    bar([(R.XF + 0.035, 0.0, (1, 0)), (R.XF + 0.0, wf - 0.02, (s2, s2)),
         (R.XF - 0.16, wf + 0.018, (0, 1))], 0.455, 0.535, 0.05)
    bar([(R.XR - 0.04, 0.0, (-1, 0)), (R.XR - 0.005, wr - 0.02, (-s2, s2)),
         (R.XR + 0.17, wr + 0.018, (0, 1))], 0.43, 0.52, 0.05)


def build_wheel(g):
    """Hub at the origin, axle along Y: an octagonal tread and a painted face
    on BOTH sides (the runtime-independent twin of the full wheel's symmetry)."""
    rad, hw = R.R_WHEEL, R.W_WHEEL * 0.5 * 0.9
    ring = [(rad * math.cos(2 * math.pi * s / WHEEL_SEG + math.pi / WHEEL_SEG),
             rad * math.sin(2 * math.pi * s / WHEEL_SEG + math.pi / WHEEL_SEG))
            for s in range(WHEEL_SEG)]
    for s in range(WHEEL_SEG):
        (x0, z0), (x1, z1) = ring[s], ring[(s + 1) % WHEEL_SEG]
        g.face([(x0, -hw, z0), (x1, -hw, z1), (x1, hw, z1), (x0, hw, z0)], "cell:black",
               mirror=False, out=(x0 + x1, 0, z0 + z1))
    for side in (1, -1):
        g.face([(x, side * hw, z) for (x, z) in ring], "disc", mirror=False, out=(0, side, 0))


def load_full_texture(glb):
    """The full model's "body paint" image, as a packed Blender image: the far
    model must ship the SAME pixels (the bake compares them)."""
    with open(glb, "rb") as f:
        data = f.read()
    magic, _ver, _len = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67, "not a .glb"
    jlen, _ = struct.unpack_from("<II", data, 12)
    js = json.loads(data[20:20 + jlen])
    bin_off = 20 + jlen + 8
    mat = next(m for m in js["materials"] if m["name"] == "body paint")
    tex = js["textures"][mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
    img = js["images"][tex["source"]]
    bv = js["bufferViews"][img["bufferView"]]
    png = data[bin_off + bv.get("byteOffset", 0):bin_off + bv.get("byteOffset", 0) + bv["byteLength"]]
    path = os.path.join(tempfile.gettempdir(), "ravager-paint-from-full.png")
    with open(path, "wb") as f:
        f.write(png)
    im = bpy.data.images.load(path)
    im.name = "ravager-paint"
    im.pack()
    return im


def make_material(image):
    m = bpy.data.materials.new("body paint")
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.45
    tex = m.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest"
    m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def to_object(g, name, mat, sharp_deg):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    for pts, uv, mirror in g.faces:
        uvs = face_uvs(pts, uv)
        copies = [(pts, uvs)]
        if mirror:
            copies.append(([Vector((p.x, -p.y, p.z)) for p in reversed(pts)], list(reversed(uvs))))
        for cp, cuv in copies:
            vs = [bm.verts.new(p) for p in cp]
            try:
                f = bm.faces.new(vs)
            except ValueError:
                continue
            for loop, t in zip(f.loops, cuv):
                loop[uvl].uv = t
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bad = [f for f in bm.faces if f.calc_area() < 1e-7]
    bmesh.ops.delete(bm, geom=bad, context="FACES")
    # triangulate here (beauty), not in the exporter: the tri count printed is
    # then the one the bake sees
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method="BEAUTY", ngon_method="BEAUTY")
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    return ob


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def render_pair(outdir, far_objs, full_objs):
    """The make-ravager views of both models, one hidden at a time, and a
    contact sheet: full on the left, far on the right."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 960, 600
    sc.world.use_nodes = True
    bg = sc.world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.42, 0.50, 0.58, 1)
    bg.inputs["Strength"].default_value = 0.9
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.2
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    sc.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0))
    gnd = bpy.context.active_object
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
    gnd.data.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    s = R.SCALE
    views = {
        "front34": ((5.2, 4.4, 1.9), (0.1, 0, 0.55), 40),
        "rear34": ((-5.6, 4.0, 2.2), (-0.2, 0, 0.55), 40),
        "side": ((0, 8.5, 0.75), (-0.2, 0, 0.62), 36),
        "chase": ((-7.4, 1.4, 2.4), (0.8, 0, 0.6), 42),
        "top": ((1.5, 2.2, 7.0), (-0.1, 0, 0.5), 38),
        # how big a traffic car really is: 16 units off in a 42-degree view
        "distant": ((-11.0, 11.0, 3.2), (0.0, 0, 0.5), 42),
    }
    shots = []
    for name, (loc, tgt, fov) in views.items():
        cam.location = Vector(loc) * s
        R.look_at(cam, Vector(tgt) * s)
        cam.data.angle = math.radians(fov)
        pair = []
        for tag, show, hide in (("full", full_objs, far_objs), ("far", far_objs, full_objs)):
            for o in show:
                o.hide_render = False
            for o in hide:
                o.hide_render = True
            path = os.path.join(outdir, f"ravager-{tag}-{name}.png")
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            pair.append(path)
        shots.append(pair)
    # the sheet: pairs side by side, one view per row, at half size
    rows = []
    for pair in shots:
        cols = []
        for path in pair:
            im = bpy.data.images.load(path)
            w, h = im.size
            px = np.array(im.pixels[:], np.float32).reshape(h, w, 4)
            cols.append(px[::2, ::2])
            bpy.data.images.remove(im)
        rows.append(np.concatenate(cols, axis=1))
    sheet = np.concatenate(rows[::-1], axis=0)  # Blender images are bottom-up
    h, w = sheet.shape[:2]
    out = bpy.data.images.new("sheet", w, h, alpha=True)
    out.pixels[:] = sheet.ravel()
    out.filepath_raw = os.path.join(outdir, "ravager-far-sheet.png")
    out.file_format = "PNG"
    out.save()


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    full, out, preview = FULL, OUT, None
    i = 0
    while i < len(argv):
        if argv[i] == "--full":
            full = argv[i + 1]; i += 1
        elif argv[i] == "--out":
            out = argv[i + 1]; i += 1
        elif argv[i] == "--preview":
            preview = argv[i + 1]; i += 1
        i += 1

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.world = bpy.data.worlds.new("world")
    img = load_full_texture(os.path.abspath(full))
    mat = make_material(img)

    g = Geo()
    build_body(g)
    build_trim(g)
    body = to_object(g, "body", mat, 38)
    wg = Geo()
    build_wheel(wg)
    wheel_mesh = to_object(wg, "wheel", mat, 50).data
    bpy.data.objects.remove(bpy.data.objects["wheel"])
    # The full wheels are drawn UNLIT (the wheel batch has no lighting); these
    # ride in the lit body tier. Face normals turned towards the sky keep the
    # painted dish as bright as the real wheel from every side of the car -
    # measured in PCSX2, true normals left it grey on the shaded side.
    loops = []
    for poly in wheel_mesh.polygons:
        n = poly.normal
        up = Vector((0.0, 0.35 * (1 if n.y > 0 else -1), 1.0)).normalized()
        for _ in poly.loop_indices:
            loops.append(up if abs(n.y) > 0.9 else n.copy())
    wheel_mesh.normals_split_custom_set(loops)
    names = {(True, 1): "wheel front left", (True, -1): "wheel front right",
             (False, 1): "wheel rear left", (False, -1): "wheel rear right"}
    wheels = []
    for front, wx in ((True, R.AXF), (False, R.AXR)):
        for side in (1, -1):
            o = bpy.data.objects.new(names[(front, side)], wheel_mesh)
            o.location = (wx, side * R.TRACK * 0.5, R.R_WHEEL)
            if side < 0:
                o.rotation_euler = (0, 0, math.pi)
            sc.collection.objects.link(o)
            wheels.append(o)

    body.data.transform(Matrix.Scale(R.SCALE, 4))
    wheel_mesh.transform(Matrix.Scale(R.SCALE, 4))
    for o in wheels:
        o.location = o.location * R.SCALE
    bpy.context.view_layer.update()
    print(f"RAVAGER-FAR body {tris(body)} tris; wheel {tris(wheels[0])} tris x4; "
          f"far tier {tris(body) + 4 * tris(wheels[0])} tris; {len(stations())} stations, "
          f"{len(LINES)} lines")

    for o in bpy.context.scene.objects:
        o.select_set(o in [body] + wheels)
    bpy.ops.export_scene.gltf(filepath=os.path.abspath(out), export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_materials="EXPORT",
                              export_image_format="AUTO")
    print("RAVAGER-FAR wrote", os.path.abspath(out))

    if preview:
        os.makedirs(preview, exist_ok=True)
        before = set(bpy.context.scene.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.abspath(full))
        full_objs = [o for o in bpy.context.scene.objects if o not in before and o.type == "MESH"]
        render_pair(preview, [body] + wheels, full_objs)


if __name__ == "__main__":
    main()

"""Bake the layered lob's strand atlas from real hair curves (Blender, Cycles).

    blender -b --factory-startup --python bake_strands.py -- <out.png> [--only 0,2,5] [--samples N] [--reuse]

Eight strips of 64 x 512 texels side by side, root at the top, rendered at 4x
and box-filtered. Each strip is a few thousand Cycles hair strands laid on a
gently arched card, so the Principled Hair BSDF puts its primary and secondary
highlights where a card bent over a head would have them:

    0-1  under   dense and opaque, over a dark backing (no skin through it)
    2-4  mid     a solid body, ends breaking into staggered clumps
    5-7  outer   looser clumps, ragged edges, longest-to-shortest stagger

Strand colour is a neutral mid brown; the generator dyes it to the chosen hair
colour by luminance, so the render's value range is what matters. Roots are
darkened by Hair Info > Intercept (occlusion from the hair above). Alpha comes
from the film's transparency; the generator alpha-tests it at 50%.
"""
import math
import os
import random
import sys

import bpy
import numpy as np

TILE_W, TILE_H, SS = 64, 512, 4
CONTRAST = 1.8  # value-range expansion before the dye (see main)
LENGTH = 8.0  # strip units: width 1, length 8 (matches 64 x 512)


def strands_for(kind, rng):
    """Polylines in strip space (x across [0, 1], y root 0 -> tip -8) and a
    per-strand clump tone, so locks read as locks and not as one sheet."""
    count = {"under": 1500, "mid": 1300, "outer": 950}[kind]
    clumps = {"under": 7, "mid": 5, "outer": 4}[kind]
    gather = {"under": 0.40, "mid": 0.82, "outer": 0.90}[kind]
    centres = [(i + rng.uniform(0.3, 0.7)) / clumps for i in range(clumps)]
    ends = [rng.uniform(0.90, 1.0) for _ in range(clumps)]
    sway = [rng.uniform(-0.06, 0.06) for _ in range(clumps)]
    tones = [rng.uniform(0.60, 1.20) for _ in range(clumps)]
    lines, tone = [], []
    P = 24
    while len(lines) < count:
        c = rng.randrange(clumps)
        spread = 0.55 / clumps if kind != "under" else 0.9 / clumps
        x0 = rng.gauss(centres[c], spread)
        if not 0.015 < x0 < 0.985:
            continue
        edge = min(x0, 1 - x0)
        if kind != "under" and rng.random() > min(1.0, (edge / 0.16) ** 1.5):
            continue  # sparse, ragged card edges instead of a cut line
        if kind == "under":
            end = 1.0
        else:
            # Strands stop before their clump's tip: the clump tapers.
            end = ends[c] - (rng.random() ** 1.6) * (0.30 if kind == "mid" else 0.45)
            if edge < 0.12:
                end = min(end, 0.45 + rng.random() * 0.5)
        z0 = rng.uniform(0.0, 0.10)
        ph = rng.uniform(0, 6.28)
        freq = rng.uniform(1.6, 3.2)
        pts = []
        for k in range(P):
            t = k / (P - 1) * end
            g = gather * max(0.0, min(1.0, (t - 0.25) / 0.65)) ** 1.25
            x = x0 + (centres[c] + sway[c] * t - x0) * g
            x += 0.010 * math.sin(t * freq * 6.28 + ph)
            z = 0.30 * math.sin(math.pi * min(t, 1.0) * 0.9) + z0 * (1 - 0.6 * t)
            pts.append((x, -t * LENGTH, z))
        lines.append(pts)
        tone.append(tones[c])
    return lines, tone


def build_curves(lines, tone, name, radius):
    curves = bpy.data.hair_curves.new(name)
    n, p = len(lines), len(lines[0])
    curves.add_curves([p] * n)
    flat = [c for line in lines for pt in line for c in pt]
    curves.position_data.foreach_set("vector", flat)
    rad = curves.attributes.get("radius") or curves.attributes.new("radius", "FLOAT", "POINT")
    vals = []
    for _ in range(n):
        for k in range(p):
            t = k / (p - 1)
            vals.append(radius * (1.0 - 0.7 * t ** 2))
    rad.data.foreach_set("value", vals)
    curves.attributes.new("tone", "FLOAT", "CURVE").data.foreach_set("value", tone)
    obj = bpy.data.objects.new(name, curves)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def hair_material():
    """Colour = occluded-root ramp x strand and clump tone + a broken sheen band."""
    m = bpy.data.materials.new("strand")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    N, L = nt.nodes.new, nt.links.new

    def math_node(op, a=None, b=None, c=None):
        n = N("ShaderNodeMath")
        n.operation = op
        for i, v in enumerate((a, b, c)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                n.inputs[i].default_value = v
            else:
                L(v, n.inputs[i])
        return n.outputs[0]

    out = N("ShaderNodeOutputMaterial")
    bsdf = N("ShaderNodeBsdfHairPrincipled")
    bsdf.parametrization = "COLOR"
    bsdf.inputs["Roughness"].default_value = 0.35
    bsdf.inputs["Radial Roughness"].default_value = 0.7
    info = N("ShaderNodeHairInfo")
    attr = N("ShaderNodeAttribute")
    attr.attribute_name = "tone"
    ramp = N("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.0, (0.020, 0.011, 0.006, 1)
    els[1].position, els[1].color = 0.24, (0.155, 0.085, 0.042, 1)
    tip = els.new(0.9)
    tip.color = (0.20, 0.115, 0.060, 1)
    L(info.outputs["Intercept"], ramp.inputs["Fac"])
    rnd = info.outputs["Random"]
    # Per strand 0.55..1.25, times the clump's tone.
    k = math_node("MULTIPLY", math_node("MULTIPLY_ADD", rnd, 0.70, 0.55), attr.outputs["Fac"])
    body = N("ShaderNodeVectorMath")
    body.operation = "SCALE"
    L(ramp.outputs["Color"], body.inputs[0])
    L(k, body.inputs["Scale"])
    # Sheen: a band a quarter of the way down, jittered per strand.
    centre = math_node("MULTIPLY_ADD", rnd, 0.10, 0.22)
    d = math_node("DIVIDE", math_node("SUBTRACT", info.outputs["Intercept"], centre), 0.065)
    band = math_node("EXPONENT", math_node("MULTIPLY", math_node("POWER", d, 2.0), -1.0))
    band2 = math_node("EXPONENT", math_node("MULTIPLY", math_node("POWER",
                      math_node("DIVIDE", math_node("SUBTRACT", info.outputs["Intercept"],
                                                    math_node("ADD", centre, 0.30)), 0.10), 2.0), -1.0))
    sheen = math_node("MULTIPLY", math_node("MULTIPLY_ADD", band2, 0.35, band),
                      math_node("MULTIPLY_ADD", rnd, 0.9, 0.5))
    hl = N("ShaderNodeVectorMath")
    hl.operation = "SCALE"
    hl.inputs[0].default_value = (0.30, 0.20, 0.12)
    L(sheen, hl.inputs["Scale"])
    add = N("ShaderNodeVectorMath")
    add.operation = "ADD"
    L(body.outputs[0], add.inputs[0])
    L(hl.outputs[0], add.inputs[1])
    L(add.outputs[0], bsdf.inputs["Color"])
    L(bsdf.outputs[0], out.inputs["Surface"])
    return m


def backing(dark):
    bpy.ops.mesh.primitive_plane_add(size=1, location=(0.5, -LENGTH / 2, -0.05))
    plane = bpy.context.object
    plane.scale = (1.2, LENGTH * 1.1, 1)
    m = bpy.data.materials.new("backing")
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (dark, dark * 0.62, dark * 0.40, 1)
    b.inputs["Roughness"].default_value = 0.9
    plane.data.materials.append(m)
    return plane


def setup():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 96
    sc.cycles.use_denoising = True
    sc.render.film_transparent = True
    sc.render.resolution_x = TILE_W * SS
    sc.render.resolution_y = TILE_H * SS
    sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "Standard"
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = LENGTH
    cam.location = (0.5, -LENGTH / 2, 10)
    sc.collection.objects.link(cam)
    sc.camera = cam
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    sc.world = world
    # Key light from above and behind the viewer, raking down the strands.
    for rot, energy in (((math.radians(-38), 0, 0), 3.2), ((math.radians(30), 0, 0), 0.8)):
        sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
        sun.data.energy = energy
        sun.data.angle = math.radians(8)
        sun.rotation_euler = rot
        sc.collection.objects.link(sun)


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    out = args[0]
    only = [int(i) for i in args[args.index("--only") + 1].split(",")] if "--only" in args else None
    samples = int(args[args.index("--samples") + 1]) if "--samples" in args else 96
    reuse = "--reuse" in args  # keep strips already rendered next to <out.png>
    kinds = ["under", "under", "mid", "mid", "mid", "outer", "outer", "outer"]
    atlas = np.zeros((TILE_H, TILE_W * len(kinds), 4))
    for i, kind in enumerate(kinds):
        if only is not None and i not in only:
            continue
        path = out + ".strip%d.png" % i
        if not (reuse and os.path.exists(path)):
            setup()
            bpy.context.scene.cycles.samples = samples
            rng = random.Random(1000 + i)
            lines, tone = strands_for(kind, rng)
            obj = build_curves(lines, tone, "hair", 0.0050)
            obj.data.materials.append(hair_material())
            if kind == "under":
                backing(0.020)
            sc = bpy.context.scene
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(path)
        img.colorspace_settings.name = "Non-Color"  # raw display-encoded values
        px = np.array(img.pixels[:]).reshape(TILE_H * SS, TILE_W * SS, 4)[::-1]
        # Box down-sample; colour premultiplied so the edges do not halo.
        rgb = px[..., :3] * px[..., 3:4]
        a = px[..., 3:4]
        rgb = rgb.reshape(TILE_H, SS, TILE_W, SS, 3).mean((1, 3))
        a = a.reshape(TILE_H, SS, TILE_W, SS, 1).mean((1, 3))
        rgb = rgb / np.maximum(a, 1e-4)
        if kind == "under":
            a[:] = 1.0
        atlas[:, i * TILE_W:(i + 1) * TILE_W, :3] = rgb
        atlas[:, i * TILE_W:(i + 1) * TILE_W, 3:] = a
    # The generator dyes hair by luminance relative to the atlas' key colour
    # and compresses it (chargen.cpp: shade = 1.8 v / (0.8 + v)), which flattens
    # a photographic render. Expand each strip's value range about its mean
    # first, so strand, clump and sheen contrast survives the dye.
    luma = atlas[..., :3] @ np.array([0.299, 0.587, 0.114])
    for i in range(len(kinds)):
        cols = slice(i * TILE_W, (i + 1) * TILE_W)
        lum = luma[:, cols]
        solid = atlas[:, cols, 3] > 0.5
        mean = np.exp(np.log(np.maximum(lum[solid], 1e-3)).mean())
        gain = (np.maximum(lum, 1e-3) / mean) ** (CONTRAST - 1.0)
        atlas[:, cols, :3] *= gain[..., None]
    img = bpy.data.images.new("atlas", TILE_W * len(kinds), TILE_H, alpha=True)
    img.colorspace_settings.name = "Non-Color"
    img.pixels[:] = np.clip(atlas[::-1], 0, 1).ravel()
    img.filepath_raw = out
    img.file_format = "PNG"
    img.save()
    print("wrote", out)


main()

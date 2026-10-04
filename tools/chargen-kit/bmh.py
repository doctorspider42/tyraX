"""Blender-side helpers: build MakeHuman meshes (fitted proxies, garments,
eyes, brows) as Blender objects with their textures. Authoring-time only.

MakeHuman space is +Y up, facing +Z, decimetres. Blender objects built here
are converted to metres, +Z up, facing -Y (Blender's own front), which is
also what the glTF exporter turns back into +Y up / facing +Z.
"""
import os
import bpy
import numpy as np

import mhkit

DM = 0.1  # decimetres -> metres


def to_blender(p):
    p = np.asarray(p, dtype=np.float64) * DM
    return np.stack([p[:, 0], -p[:, 2], p[:, 1]], axis=1)


def to_mh(p):
    p = np.asarray(p, dtype=np.float64) / DM
    return np.stack([p[:, 0], p[:, 2], -p[:, 1]], axis=1)


def image(path, alpha=True):
    for im in bpy.data.images:
        if im.filepath == path:
            return im
    im = bpy.data.images.load(path, check_existing=True)
    if not alpha:
        im.alpha_mode = 'NONE'
    return im


def material(name, tex=None, alpha=False, color=(0.8, 0.8, 0.8, 1), rough=0.6):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes['Principled BSDF']
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Base Color'].default_value = color
    if tex:
        t = nt.nodes.new('ShaderNodeTexImage')
        t.image = image(tex)
        t.interpolation = 'Linear'
        nt.links.new(t.outputs['Color'], bsdf.inputs['Base Color'])
        if alpha:
            nt.links.new(t.outputs['Alpha'], bsdf.inputs['Alpha'])
            try:
                m.surface_render_method = 'DITHERED'
            except Exception:
                m.blend_method = 'HASHED'
    return m


def mesh_object(name, verts_mh, faces, uvs=None, ftex=None, mat=None, smooth=True):
    """verts in MH space; faces = list of index lists; uvs (T,2) + ftex lists."""
    me = bpy.data.meshes.new(name)
    v = to_blender(verts_mh)
    me.from_pydata(v.tolist(), [], [list(f) for f in faces])
    if uvs is not None and ftex is not None and len(uvs):
        uvl = me.uv_layers.new(name='UVMap')
        loop_uv = []
        for f, t in zip(faces, ftex):
            for k in range(len(f)):
                loop_uv.append(uvs[t[k]] if t else (0, 0))
        uvl.data.foreach_set('uv', np.array(loop_uv, dtype=np.float32).ravel())
    me.validate()
    me.update()
    if smooth:
        me.shade_smooth()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    if mat:
        o.data.materials.append(mat)
    return o


def find_texture(dirpath, stem_hint=None):
    """The diffuse png in an asset directory (from its .mhmat if present)."""
    for f in os.listdir(dirpath):
        if f.endswith('.mhmat'):
            for line in open(os.path.join(dirpath, f), encoding='utf-8', errors='replace'):
                if line.startswith('diffuseTexture'):
                    p = os.path.join(dirpath, line.split(None, 1)[1].strip())
                    if os.path.exists(p):
                        return p
    pngs = [f for f in os.listdir(dirpath) if f.endswith('.png')]
    pngs.sort(key=lambda f: ('diffuse' not in f, len(f)))
    return os.path.join(dirpath, pngs[0]) if pngs else None


def fitted_asset(asset_dir, base_morphed, name=None, alpha=False, tex=None):
    """A .proxy/.mhclo asset fitted to a morphed base mesh -> Blender object."""
    stem = os.path.basename(asset_dir.rstrip('/\\'))
    pf = [f for f in os.listdir(asset_dir) if f.endswith('.mhclo') or f.endswith('.proxy')][0]
    px = mhkit.load_proxy(os.path.join(asset_dir, pf))
    obj = mhkit.load_obj(os.path.join(asset_dir, px.obj_file or stem + '.obj'))
    pos = mhkit.fit_proxy(px, base_morphed)
    if tex is None:
        tex = find_texture(asset_dir)
    m = material(name or stem, tex, alpha=alpha)
    return mesh_object(name or stem, pos, obj.faces, obj.vt, obj.ftex, m), px, obj

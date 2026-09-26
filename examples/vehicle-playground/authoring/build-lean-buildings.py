"""Build single-material exterior shells from bundled CC0 Kenney facades.

Only replaces the three building assets, never the scene. Python + numpy + Pillow.
The orthographic facade bake preserves source colours/UVs and recess shading;
there is no directional light baked into the atlas.
"""
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
URBAN = ROOT / 'res/models/urban'


def read_obj(path):
    vertices, uvs, faces, textures = [], [], [], {}
    material = None
    for line in path.read_text().splitlines():
        s = line.split()
        if not s:
            continue
        if s[0] == 'mtllib':
            for row in (path.parent / s[1]).read_text().splitlines():
                t = row.split()
                if t and t[0] == 'newmtl':
                    name = t[1]
                elif t and t[0] == 'map_Kd':
                    textures[name] = np.asarray(Image.open(path.parent / t[-1]).convert('RGBA').convert('RGB'))
        elif s[0] == 'v':
            vertices.append(list(map(float, s[1:4])))
        elif s[0] == 'vt':
            uvs.append(list(map(float, s[1:3])))
        elif s[0] == 'usemtl':
            material = s[1]
        elif s[0] == 'f':
            cs = [tuple(int(x)-1 for x in c.split('/')[:2]) for c in s[1:]]
            faces.extend((material, [cs[0], cs[i], cs[i+1]]) for i in range(1, len(cs)-1))
    return np.array(vertices), np.array(uvs), faces, textures


def render(path, size, basis, bounds=None, shaded=False):
    """Small deterministic orthographic rasterizer, also used for asset previews."""
    vs, uv, faces, textures = read_obj(path)
    projected = vs @ np.array(basis).T
    lo, hi = (projected[:, :2].min(0), projected[:, :2].max(0)) if bounds is None else bounds
    w, h = size
    screen = (projected[:, :2] - lo) / (hi-lo) * [w, h]
    screen[:, 1] = h - screen[:, 1]
    out = np.full((h, w, 3), (30, 39, 48), dtype=np.uint8)
    depth = np.full((h, w), -np.inf)
    for mat, corners in faces:
        ids = [c[0] for c in corners]
        p = screen[ids]
        a, b, c = p
        den = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
        if abs(den) < 1e-8:
            continue
        x0, y0 = np.maximum(np.floor(p.min(0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(p.max(0)).astype(int), [w, h])
        if x1 <= x0 or y1 <= y0:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1] + .5
        wa = ((b[1]-c[1])*(xx-c[0]) + (c[0]-b[0])*(yy-c[1])) / den
        wb = ((c[1]-a[1])*(xx-c[0]) + (a[0]-c[0])*(yy-c[1])) / den
        weights = np.stack([wa, wb, 1-wa-wb], -1)
        z = weights @ projected[ids, 2]
        mask = (weights.min(-1) >= -1e-7) & (z > depth[y0:y1, x0:x1])
        st = weights @ uv[[c[1] for c in corners]]
        tex = textures[mat]
        tx = np.floor(st[:, :, 0]*tex.shape[1]).astype(int) % tex.shape[1]
        ty = np.floor((1-st[:, :, 1])*tex.shape[0]).astype(int) % tex.shape[0]
        rgb = tex[ty, tx].astype(float)
        if shaded:
            n = np.cross(vs[ids[1]]-vs[ids[0]], vs[ids[2]]-vs[ids[0]])
            n /= np.linalg.norm(n)
            rgb *= .55 + .45*max(0, np.dot(n, [.35, .8, -.48]))
        out[y0:y1, x0:x1][mask] = np.clip(rgb[mask], 0, 255).astype(np.uint8)
        depth[y0:y1, x0:x1][mask] = z[mask]
    return Image.fromarray(out)


def build():
    # Bake at 4x resolution before filtering. Windows retain their recessed
    # inner border as a dark contact ring; no tiny geometry or alpha pass.
    window = render(URBAN/'wall-a-window.obj', (192, 160),
                    [[1,0,0], [0,1,0], [0,0,-1]]).resize((48,40), Image.Resampling.LANCZOS)
    garage = render(URBAN/'wall-a-garage.obj', (192,160),
                    [[1,0,0], [0,1,0], [0,0,-1]]).resize((48,40), Image.Resampling.LANCZOS)
    side = render(URBAN/'wall-a-garage.obj', (192,160),
                  [[0,0,1], [0,1,0], [1,0,0]]).resize((48,40), Image.Resampling.LANCZOS)
    gable = render(URBAN/'wall-a-roof.obj', (384,96),
                   [[0,0,1], [0,1,0], [1,0,0]]).resize((96,24), Image.Resampling.LANCZOS)
    atlas = Image.new('RGB', (256,256), '#777570')
    rects = {}

    def place(name, tile, x, y, nx=1, ny=1):
        patch = Image.new('RGB', (tile.width*nx, tile.height*ny))
        for j in range(ny):
            for i in range(nx):
                patch.paste(tile, (i*tile.width, j*tile.height))
        # Two-pixel extruded gutters prevent bilinear neighbouring-tile bleed.
        atlas.paste(Image.fromarray(np.pad(np.asarray(patch), ((2,2),(2,2),(0,0)), mode='edge')), (x-2,y-2))
        atlas.paste(patch,(x,y))
        rects[name] = (x,y,patch.width,patch.height)

    place('tower', window, 2,2,2,5)
    place('loft', window, 102,2,2,3)
    place('workshop', garage, 102,126,3)
    place('workshop-side', side, 102,170,2)
    place('gable', gable, 2,208)
    place('roof', Image.open(URBAN/'Textures/roof.png').convert('RGBA').convert('RGB').resize((48,40)), 202,170)
    place('trim', Image.open(URBAN/'Textures/concrete.png').convert('RGBA').convert('RGB').resize((32,32)),102,214)
    atlas.save(URBAN/'Textures/district-facades.png')
    (URBAN/'district-buildings.mtl').write_text(
        '# Exterior shells; shared opaque atlas, CC0 Kenney source textures\n'
        'newmtl district_facade\nKd 1 1 1\nmap_Kd Textures/district-facades.png\n', encoding='utf-8')

    for name, width, height in [('workshop',12,3.2), ('loft',8,9.6), ('tower',8,16)]:
        lines = ['# Lean exterior shell; Kenney Retro Urban Kit derived, CC0',
                 'mtllib district-buildings.mtl', 'usemtl district_facade']
        count = 0

        def face(points, tile):
            nonlocal count
            x,y,w,h = rects[tile]
            uv = [(x/256,1-(y+h)/256),((x+w)/256,1-(y+h)/256),
                  ((x+w)/256,1-y/256),(x/256,1-y/256)]
            if len(points) == 3:
                uv = [(x/256,1-(y+h)/256),((x+w/2)/256,1-y/256),
                      ((x+w)/256,1-(y+h)/256)]
            for v, t in zip(points,uv):
                lines.append('v '+' '.join(f'{q:.6f}' for q in v))
                lines.append('vt '+' '.join(f'{q:.6f}' for q in t))
            for tri in ([(1,2,3)] if len(points)==3 else [(1,2,3),(1,3,4)]):
                lines.append('f '+' '.join(f'{count+i}/{count+i}' for i in tri))
            count += len(points)

        # Counter-clockwise faces, all exterior, original collision AABB intact.
        x,z=width/2,4
        ring=[(-x,-z),(-x,z),(x,z),(x,-z)]
        top=height+1
        for i in range(4):
            a,b=ring[i],ring[(i+1)%4]
            tile='workshop-side' if name=='workshop' and i%2==0 else name
            face([(a[0],0,a[1]),(b[0],0,b[1]),(b[0],height,b[1]),(a[0],height,a[1])],tile)
        for bay in range(2):
            left=-z+bay*4
            mid,right=left+2,left+4
            face([(-x,height,left),(-x,top,mid),(x,top,mid),(x,height,left)],'roof')
            face([(-x,top,mid),(-x,height,right),(x,height,right),(x,top,mid)],'roof')
            face([(-x,height,right),(-x,top,mid),(-x,height,left)],'gable')
            face([(x,height,left),(x,top,mid),(x,height,right)],'gable')
        face([(a,0,b) for a,b in reversed(ring)],'trim')
        (URBAN/f'district-{name}.obj').write_text('\n'.join(lines)+'\n', encoding='utf-8')
        print(f'{name}: 22 triangles, 1 material, bounds {width} x {top:g} x 8')


if __name__ == '__main__':
    build()

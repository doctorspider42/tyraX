"""Re-author expensive vehicle wheels without decimating the body.

python prepare-efficient-vehicles.py BASELINE_PROJECT OUTPUT_MODELS_DIRECTORY
BASELINE_PROJECT must have freshly baked original vehicles (.res-baked/vehicles).
Python 3, NumPy, Pillow. Writes GLB variants; never edits the project manifest.
Sources/licences remain those of the original vehicles in res/models.
"""
from pathlib import Path
import argparse
import io
import json
import math
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def read_tmdl(path):
    f = io.BytesIO(path.read_bytes())
    def u():
        return struct.unpack('<I', f.read(4))[0]
    def string(n):
        return f.read(n).split(b'\0')[0].decode()
    def stream():
        vertices = np.frombuffer(f.read(u()*32), dtype='<f4').copy().reshape(-1,8)
        f.read(u())
        return vertices
    assert f.read(4) == b'TMDL' and u() == 4
    bounds = np.frombuffer(f.read(24), dtype='<f4').copy().reshape(2,3)
    parts = []
    for _ in range(u()):
        p = dict(name=string(32), texture=string(64), reflection=string(64))
        p['kd'] = np.frombuffer(f.read(12), dtype='<f4').copy()
        p['ke'] = np.frombuffer(f.read(12), dtype='<f4').copy()
        p['shine'], p['flags'] = struct.unpack('<fI', f.read(8))
        p['verts'] = stream()
        p['lods'] = [stream() for _ in range(u())]
        p['run'] = u()
        p['strips'] = [stream() for _ in range(1+len(p['lods']))]
        parts.append(p)
    shadow = u()
    f.read(shadow*12)
    assert f.tell() == len(f.getvalue())
    return bounds, parts


def texture(root, name):
    return np.asarray(Image.open(root/name).convert('RGBA').convert('RGB'))


def raster(parts, root, basis, size, bounds=None, shade=False):
    """Orthographic model/UV inspection, not a substitute for the PS2 renderer."""
    w,h = size
    basis = np.asarray(basis)
    vertices = np.concatenate([p['verts'][:,:3] for p in parts]) @ basis.T
    lo,hi = (vertices[:,:2].min(0), vertices[:,:2].max(0)) if bounds is None else bounds
    rgb = np.full((h,w,3), (23,29,36), dtype=np.uint8)
    depth = np.full((h,w), -np.inf)
    for part in parts:
        v = part['verts']
        proj = v[:,:3] @ basis.T
        xy = (proj[:,:2]-lo)/(hi-lo)*[w,h]
        xy[:,1] = h-xy[:,1]
        tex = texture(root,part['texture']) if part['texture'] else None
        for j in range(0,len(v),3):
            p = xy[j:j+3]
            a,b,c = p
            den = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-8:
                continue
            x0,y0 = np.maximum(np.floor(p.min(0)).astype(int),0)
            x1,y1 = np.minimum(np.ceil(p.max(0)).astype(int),[w,h])
            if x1<=x0 or y1<=y0:
                continue
            yy,xx = np.mgrid[y0:y1,x0:x1]+.5
            wa = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
            wb = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
            weights = np.stack([wa,wb,1-wa-wb],-1)
            z = weights @ proj[j:j+3,2]
            visible = (weights.min(-1)>=-1e-7)&(z>depth[y0:y1,x0:x1])
            color = np.broadcast_to(part['kd']*255,(*z.shape,3)).copy()
            if tex is not None:
                uv = weights @ v[j:j+3,6:8]
                fx=uv[:,:,0]*tex.shape[1]-.5; fy=uv[:,:,1]*tex.shape[0]-.5
                tx=np.floor(fx).astype(int); ty=np.floor(fy).astype(int)
                ax=(fx-tx)[:,:,None]; ay=(fy-ty)[:,:,None]
                color=((1-ay)*((1-ax)*tex[ty%tex.shape[0],tx%tex.shape[1]]+
                               ax*tex[ty%tex.shape[0],(tx+1)%tex.shape[1]])+
                       ay*((1-ax)*tex[(ty+1)%tex.shape[0],tx%tex.shape[1]]+
                           ax*tex[(ty+1)%tex.shape[0],(tx+1)%tex.shape[1]]))*part['kd']
            if shade:
                normals = weights @ v[j:j+3,3:6]
                normals /= np.maximum(np.linalg.norm(normals,axis=-1,keepdims=True),1e-8)
                lighting = .5+.5*np.maximum(0,normals @ np.array([.4,.82,.4]))
                color = color*lighting[:,:,None]
            rgb[y0:y1,x0:x1][visible] = np.clip(color[visible],0,255).astype(np.uint8)
            depth[y0:y1,x0:x1][visible] = z[visible]
    return Image.fromarray(rgb), np.isfinite(depth)


class Glb:
    def __init__(self, atlas):
        self.data = bytearray()
        self.doc = dict(asset={'version':'2.0','generator':'TyraX efficient vehicle authoring'},
                        scenes=[{'nodes':[]}],scene=0,nodes=[],meshes=[],materials=[],
                        buffers=[],bufferViews=[],accessors=[],samplers=[{'magFilter':9729,'minFilter':9729}],
                        textures=[{'source':0,'sampler':0}])
        png = io.BytesIO()
        atlas.save(png,format='PNG')
        self.doc['images'] = [{'bufferView':self.view(png.getvalue()),'mimeType':'image/png'}]

    def view(self, data):
        self.data.extend(b'\0'*((-len(self.data))%4))
        idx = len(self.doc['bufferViews'])
        self.doc['bufferViews'].append({'buffer':0,'byteOffset':len(self.data),'byteLength':len(data)})
        self.data.extend(data)
        return idx

    def accessor(self, array):
        a = np.asarray(array,dtype='<f4')
        idx = len(self.doc['accessors'])
        self.doc['accessors'].append(dict(bufferView=self.view(a.tobytes()),componentType=5126,
            count=len(a),type={2:'VEC2',3:'VEC3'}[a.shape[1]],min=a.min(0).tolist(),max=a.max(0).tolist()))
        return idx

    def material(self,name,color=(1,1,1),textured=True):
        pbr = dict(baseColorFactor=[*map(float,color),1],metallicFactor=0,roughnessFactor=.4)
        if textured:
            pbr['baseColorTexture'] = {'index':0}
        idx = len(self.doc['materials'])
        self.doc['materials'].append(dict(name=name,pbrMetallicRoughness=pbr))
        return idx

    def mesh(self,name,primitives,position=(0,0,0)):
        ps=[]
        for v,mat in primitives:
            ps.append(dict(attributes={'POSITION':self.accessor(v[:,:3]),
                                       'NORMAL':self.accessor(v[:,3:6]),
                                       'TEXCOORD_0':self.accessor(v[:,6:8])},material=mat,mode=4))
        self.doc['scenes'][0]['nodes'].append(len(self.doc['nodes']))
        self.doc['nodes'].append(dict(name=name,mesh=len(self.doc['meshes']),translation=list(position)))
        self.doc['meshes'].append(dict(name=name,primitives=ps))

    def write(self,path):
        self.doc['buffers'] = [{'byteLength':len(self.data)}]
        raw = json.dumps(self.doc,separators=(',',':')).encode()
        raw += b' '*((-len(raw))%4)
        self.data.extend(b'\0'*((-len(self.data))%4))
        path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(raw)+len(self.data))+
                         struct.pack('<I4s',len(raw),b'JSON')+raw+
                         struct.pack('<I4s',len(self.data),b'BIN\0')+self.data)


def wheel_mesh(bounds, rim_rect, atlas_size, segments=20):
    """Regular cylinder: smooth tread, two opaque textured sidewalls, 76 tris.

    Rim bevels/spokes live in the sidewall bake. The tyre silhouette has <=1.24%
    circular radial error, with exact original XYZ bounds and hub origin.
    """
    # Remove sub-micron source-bake roundoff around the hub. An asymmetric
    # float AABB otherwise shifts the new detected axle centre and can change
    # depth ordering of existing coplanar badges on an untouched body.
    lo,hi = bounds-(bounds[0]+bounds[1])*.5
    center=(lo+hi)*.5
    radius=(hi-lo)*.5
    points=[]
    for x in (lo[0],hi[0]):
        points.append([np.array([x,center[1]+radius[1]*math.sin(i*2*math.pi/segments),
                                center[2]+radius[2]*math.cos(i*2*math.pi/segments)]) for i in range(segments)])
    out=[]
    rx,ry,rsize=rim_rect
    aw,ah=atlas_size
    def corner(p,n,uv):
        return [*p,*n,*uv]
    for i in range(segments):
        j=(i+1)%segments
        ns=[(0,math.sin(k*2*math.pi/segments),math.cos(k*2*math.pi/segments)) for k in (i,j)]
        # Both tread triangles sample the same dark rubber tile.
        a,b,c,d=points[0][i],points[0][j],points[1][j],points[1][i]
        uv=((rx+.5)/aw,(ry+.5)/ah)  # dark outside corner of the rim island
        out.extend([corner(a,ns[0],uv),corner(c,ns[1],uv),corner(b,ns[1],uv),
                    corner(a,ns[0],uv),corner(d,ns[0],uv),corner(c,ns[1],uv)])
    for side in range(2):
        normal=(-1 if side==0 else 1,0,0)
        for i in range(1,segments-1):
            ids=[0,i,i+1] if side==0 else [0,i+1,i]
            for k in ids:
                p=points[side][k]
                # The source is viewed from +X: screen right is -Z, up +Y.
                uv=((rx+(1-(p[2]-lo[2])/(hi[2]-lo[2]))*rsize)/aw,
                    (ry+(1-(p[1]-lo[1])/(hi[1]-lo[1]))*rsize)/ah)
                out.append(corner(p,normal,uv))
    return np.asarray(out,dtype=np.float32)


def rim_texture():
    """Original five-spoke motorsport rim: depth cues in an opaque 128px tile.

    Analytic rings/spokes retain definition after 4-bit quantization. No logo,
    transparent gaps, extra rim geometry or permanently baked world lighting.
    """
    y,x=np.mgrid[:512,:512]
    x=(x+.5-256)/256; y=(y+.5-256)/256
    r=np.sqrt(x*x+y*y); angle=np.arctan2(y,x)
    value=np.full(r.shape,20.0)
    tyre=(r>.77)&(r<=1)
    value[tyre]=22+18*np.sin(np.clip((r[tyre]-.77)/.23,0,1)*math.pi)
    value[(r>.83)&(r<.842)]=49
    value[r<=.77]=12
    # Brake disc behind the spokes, concentric machining rings.
    disc=r<.66
    value[disc]=69+5*np.cos(r[disc]*220)
    for i in range(16):
        theta=2*math.pi*i/16
        hole=(x-.55*math.cos(theta))**2+(y-.55*math.sin(theta))**2<.018**2
        value[hole]=24
    phase=(angle+math.pi/2+math.pi/5)%(2*math.pi/5)-math.pi/5
    spoke=(np.abs(np.sin(phase)*r)<(.060+.025*r))&(r<.72)
    value[spoke]=172+48*(1-np.minimum(1,np.abs(np.sin(phase[spoke])*r[spoke])/.09))
    edge=spoke&(np.abs(np.sin(phase)*r)>(.044+.025*r))
    value[edge]=114
    lip=(r>.69)&(r<.78)
    value[lip]=145+85*np.sin((r[lip]-.69)/.09*math.pi)
    value[(r>.755)&(r<.77)]=245
    value[r<.21]=94
    value[r<.13]=184
    value[r<.09]=74
    for i in range(5):
        theta=2*math.pi*i/5-math.pi/2
        hole=(x-.17*math.cos(theta))**2+(y-.17*math.sin(theta))**2<.023**2
        value[hole]=218
    out=np.repeat(np.clip(value,0,255).astype(np.uint8)[:,:,None],3,axis=2)
    return Image.fromarray(out).resize((128,128),Image.Resampling.LANCZOS)


def build(baseline,output):
    manifest=json.loads(next(baseline.glob('*.tyra')).read_text())
    root=baseline/'.res-baked'
    output.mkdir(parents=True,exist_ok=True)
    for vehicle in manifest['vehicles']:
        if vehicle['name'] not in ('CC96','Tristar Racer'):
            continue  # Rally already has 28-triangle wheels: preserve them.
        expected='car1.fbx' if vehicle['name']=='CC96' else 'tristar-lean.glb'
        assert Path(vehicle['model']).name==expected, 'Bake the original source baseline first'
        stem='vehicles/veh-'+vehicle['id']
        body_bounds,body=read_tmdl(root/(stem+'-body.tmdl'))
        wheel_bounds,wheel=read_tmdl(root/(stem+'-wheel.tmdl'))
        compact=vehicle['name']=='CC96'
        atlas=Image.new('RGB',(256,128) if compact else (256,256),(20,20,20))
        rects={}
        # The largest source image stays at native resolution; tiny palettes
        # fit beside it. No source paint/glass texels are downsampled.
        solids={p['texture']:texture(root,p['texture'])[0,0] for p in body if p['texture']
                and len(np.unique(texture(root,p['texture']).reshape(-1,3),axis=0))==1}
        names=sorted({p['texture'] for p in body if p['texture'] and p['texture'] not in solids},
                     key=lambda n: -texture(root,n).shape[0]*texture(root,n).shape[1])
        for i,name in enumerate(names):
            assert i==0, 'Expected a single non-solid body texture'
            tex=Image.fromarray(texture(root,name)); x,y=0,0
            assert tex.width<=256 and tex.height<=256
            atlas.paste(tex,(x,y));rects[name]=(x,y,tex.width,tex.height)
        if compact:
            rx,ry,rsize=132,4,120
        else:
            # Palette-style Tristar UVs use only a small part of the image.
            # Place the wheel in a proved-unused island instead of doubling
            # texture VRAM. Dilate by 3 pixels for filtering/roundoff; include
            # the wrapped neighbour at U/V=0 in that exclusion mask.
            used=Image.new('L',atlas.size)
            draw=ImageDraw.Draw(used)
            for part in body:
                if part['texture'] not in names:
                    continue
                for tri in part['verts'].reshape(-1,3,8):
                    for dx,dy in [(0,0),(-256,0),(256,0),(0,-256),(0,256)]:
                        draw.polygon([tuple(uv*np.array(atlas.size)+[dx,dy]) for uv in tri[:,6:8]],fill=255)
            used=used.filter(ImageFilter.MaxFilter(7))
            summed=np.pad((np.asarray(used)>0).astype(int),((1,0),(1,0))).cumsum(0).cumsum(1)
            n=136
            occupied=summed[n:,n:]-summed[:-n,n:]-summed[n:,:-n]+summed[:-n,:-n]
            ys,xs=np.where(occupied==0)
            assert len(xs), 'No safe 128px wheel island; do not overwrite body UVs'
            rx,ry,rsize=int(xs[0])+4,int(ys[0])+4,128
        rim=rim_texture().resize((rsize,rsize),Image.Resampling.LANCZOS)
        # Pad the wheel island; no transparent fragments and no extra bag.
        atlas.paste(Image.fromarray(np.pad(np.asarray(rim),((4,4),(4,4),(0,0)),mode='edge')),(rx-4,ry-4))
        atlas.paste(rim,(rx,ry))
        # Pin the source paint colours before adding rim greys: the game's
        # 4-bit texture setting must not recolour a tiny body palette because
        # the much larger wheel tile won median-cut's colour popularity vote.
        colors=sorted({tuple(c) for n in names for c in np.unique(texture(root,n).reshape(-1,3),axis=0)})
        for gray in (20,36,64,94,128,172,218,245):
            if len(colors)<16 and (gray,gray,gray) not in colors:
                colors.append((gray,gray,gray))
        assert len(colors)<=16, 'Baseline must use the district 4-bit texture policy'
        palette=Image.new('P',(1,1))
        palette.putpalette([int(v) for color in colors for v in color]+[0]*(768-3*len(colors)))
        atlas=atlas.quantize(palette=palette,dither=Image.Dither.NONE)
        glb=Glb(atlas)
        primitives=[]
        paint=glb.material('body paint and glass')
        for p in body:
            v=p['verts'].copy()
            if p['name']=='lamps':
                # Preserve the rear/front corner ranges used by brake/head lamps.
                split=vehicle['lampRearVerts']
                for name,vs in [('rear lights',v[:split]),('headlights',v[split:])]:
                    primitives.append((vs,glb.material(name,p['kd'],False)))
            elif not p['reflection']:
                # Keep matte trim out of the reflection pass. Untextured source
                # slots retain that state through vehbake's palette merge.
                tex=texture(root,p['texture'])
                groups={}
                for tri in v.reshape(-1,3,8):
                    assert np.allclose(tri[:,6:8],tri[0,6:8],atol=1e-7,rtol=0), 'Expected flat palette trim'
                    u,t=tri[0,6:8]
                    color=tuple(tex[int(t*tex.shape[0])%tex.shape[0],int(u*tex.shape[1])%tex.shape[1]])
                    groups.setdefault(color,[]).append(tri)
                for color,tris in groups.items():
                    primitives.append((np.concatenate(tris),glb.material('rubber trim '+str(color),np.array(color)/255,False)))
            elif p['texture'] in solids:
                primitives.append((v,glb.material('glass',solids[p['texture']]/255,False)))
            else:
                x,y,w,h=rects[p['texture']]
                v[:,6]=(v[:,6]*w+x)/atlas.width
                v[:,7]=(v[:,7]*h+y)/atlas.height
                primitives.append((v,paint))
        glb.mesh('Body',primitives)
        new_wheel=wheel_mesh(wheel_bounds,(rx,ry,rsize),atlas.size)
        hx,hz=vehicle['drive']['track']/2,vehicle['drive']['wheelBase']/2
        for i,(x,z) in enumerate([(-hx,hz),(hx,hz),(-hx,-hz),(hx,-hz)]):
            end='front' if z>0 else 'rear'
            name=(vehicle['wheels'][i]['node'] if vehicle.get('wheels') else f'{end} wheel {i+1}')
            mat=glb.material(f'{end} wheel {i+1}')  # distinct slot preserves rigid-node ownership
            glb.mesh(name,[(new_wheel,mat)],(x,0,z))
        name='cc96-efficient' if vehicle['name']=='CC96' else 'tristar-efficient'
        glb.write(output/(name+'.glb'))
        print(f'{name}: unchanged {sum(len(p["verts"])//3 for p in body)}-triangle body; '
              f'wheel {sum(len(p["verts"])//3 for p in wheel)} -> {len(new_wheel)//3} triangles')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    build(args.baseline,args.output)

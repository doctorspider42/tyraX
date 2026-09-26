"""Author an additional CC96 coupe with indexed panel grids, without decimation.

Run from any directory: python build-cc96-strip-study.py [OUTPUT_DIRECTORY]
Defaults to res/models/cc96-strip-study. Does not modify the scene or importer.
Requires NumPy and Pillow. Existing wheel proportions/artwork are reused.
"""
from pathlib import Path
import argparse
import collections
import io
import json
import math
import runpy
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
H = runpy.run_path(str(HERE / 'prepare-efficient-vehicles.py'))
TILES = dict(side=(2,2,124,44), hood=(130,2,124,60), front=(2,50,124,36),
             rear=(130,66,124,28), sideglass=(2,98,124,42),
             windshield=(130,98,124,26), paint=(2,148,26,26),
             chrome=(34,148,26,26), rubber=(66,148,26,26),
             glass=(98,148,26,26))


def atlas():
    im=Image.new('RGB',(256,256),(225,149,24)); d=ImageDraw.Draw(im)
    for key in ('side','hood','front','rear'):
        x,y,w,h=TILES[key]
        for j in range(h):
            f=.88+.12*j/max(h-1,1)
            d.line((x,y+j,x+w-1,y+j), fill=tuple(int(v*f) for v in (225,149,24)))
    x,y,w,h=TILES['side']
    d.line([(x+43,y+5),(x+42,y+35),(x+78,y+35),(x+80,y+5)],fill=(149,98,23),width=1)
    d.line((x+43,y+36,x+78,y+36),fill=(241,171,38))
    d.rounded_rectangle((x+47,y+9,x+55,y+11),radius=1,fill=(44,46,45))
    d.line((x+47,y+9,x+55,y+9),fill=(199,202,192))
    d.line((x+4,y+40,x+w-5,y+40),fill=(76,64,43))
    x,y,w,h=TILES['hood']
    d.polygon([(x+16,y+1),(x+w-17,y+1),(x+w-23,y+h-2),(x+22,y+h-2)],fill=(27,30,33))
    for xx in (x+26,x+w-28):
        d.line((xx,y+4,xx,y+h-7),fill=(49,53,55),width=2)
    for key in ('front','rear'):
        x,y,w,h=TILES[key]
        d.rounded_rectangle((x+7,y+5,x+w-8,y+h-6),radius=4,fill=(21,24,26))
        for yy in range(y+8,y+h-7,3):
            d.line((x+10,yy,x+w-11,yy),fill=(56,60,62))
        d.rectangle((x+w//2-9,y+h//2-4,x+w//2+9,y+h//2+4),fill=(188,193,186))
        d.line((x+w//2-6,y+h//2,x+w//2+6,y+h//2),fill=(51,55,57))
    for key in ('sideglass','windshield','glass'):
        x,y,w,h=TILES[key]
        d.rectangle((x,y,x+w-1,y+h-1),fill=(222,147,24))
        inset=3 if key!='glass' else 0
        for j in range(inset,h-inset):
            f=(j-inset)/max(1,h-2*inset)
            d.line((x+inset,y+j,x+w-inset-1,y+j),
                   fill=(int(25+12*f),int(40+16*f),int(53+20*f)))
        if key=='sideglass':
            d.line((x+38,y+3,x+38,y+h-4),fill=(26,29,30),width=3)
    for key,color in [('paint',(225,149,24)),('chrome',(180,188,185)),('rubber',(23,26,29))]:
        x,y,w,h=TILES[key];d.rectangle((x,y,x+w-1,y+h-1),fill=color)
    im.paste(H['rim_texture'](),(128,128))
    return im


def uvrect(tile,u,v):
    x,y,w,h=TILES[tile]
    return ((x+.5+u*(w-1))/256,(y+.5+v*(h-1))/256)


class Mesh:
    def __init__(self):
        self.faces=[]

    def grid(self,name,us,vs,fn,tile='paint',material='body',out=(0,1,0),uvfn=None):
        pts=[[np.asarray(fn(float(u),float(v)),float) for v in vs] for u in us]
        for i in range(len(us)-1):
            for j in range(len(vs)-1):
                ij=[(i,j),(i+1,j),(i+1,j+1),(i,j+1)]
                ps=np.array([pts[a][b] for a,b in ij])
                uv=[uvfn(float(us[a]),float(vs[b])) if uvfn else
                    uvrect(tile,a/(len(us)-1),b/(len(vs)-1)) for a,b in ij]
                n=np.cross(ps[1]-ps[0],ps[2]-ps[0])
                direction=out(ps.mean(0)) if callable(out) else out
                if np.dot(n,direction)<0:ps=ps[::-1];uv=uv[::-1]
                self.faces.append((ps,np.array(uv),material,name))

    def build(self):
        normals={}
        for ps,uv,mat,group in self.faces:
            # Quad area normal avoids dependence on the arbitrary diagonal.
            n=np.cross(ps[1]-ps[0],ps[2]-ps[0])+np.cross(ps[2]-ps[0],ps[3]-ps[0])
            for p in ps:
                k=(group,tuple(np.round(p,7)))
                normals[k]=normals.get(k,np.zeros(3))+n
        parts=collections.defaultdict(list)
        for ps,uv,mat,group in self.faces:
            for ids in ((0,1,2),(0,2,3)):
                p=ps[list(ids)]
                if np.linalg.norm(np.cross(p[1]-p[0],p[2]-p[0]))<1e-10:continue
                for i in ids:
                    n=normals[(group,tuple(np.round(ps[i],7)))];n=n/np.linalg.norm(n)
                    parts[mat].append([*ps[i],*n,*uv[i]])
        self.normals=normals
        return {k:np.array(v,dtype='<f4') for k,v in parts.items()}


def lerp(z,keys,column):
    return float(np.interp(z,[r[0] for r in keys],[r[column] for r in keys]))


def author():
    m=Mesh(); axle=1.033; arch=.261; bottom=-.10
    keys=[(-1.74,.595,.265),(-1.64,.66,.30),(-1.35,.685,.345),
          (-1.03,.699,.356),(-.65,.685,.355),(0,.668,.35),
          (.65,.682,.347),(1.03,.695,.338),(1.38,.68,.31),(1.69,.603,.25)]
    def width(z):return lerp(z,keys,1)
    def shoulder(z):return lerp(z,keys,2)
    zs=list(np.linspace(-1.74,1.69,32))+[r[0] for r in keys]
    # Explicit radial arch cuts. No overlapping discs hiding the wheels.
    for c in (-axle,axle):zs += list(c+arch*np.cos(np.linspace(0,math.pi,15)))
    zs=sorted(set(round(float(z),7) for z in zs))
    def low(z):
        dz=min(abs(z-axle),abs(z+axle))
        return math.sqrt(max(0,arch*arch-dz*dz)) if dz<arch else bottom
    for side in (-1,1):
        def sidefn(z,t):
            y=low(z)+(shoulder(z)-low(z))*t
            # Belt crease and restrained rocker tuck, not a flat slab.
            x=width(z)-.036*(1-t)**2+.014*math.sin(math.pi*t)
            return (side*x,y,z)
        m.grid('flank'+str(side),zs,[0,.08,.25,.50,.78,1],sidefn,'side',out=(side,0,0),
               uvfn=lambda z,t:uvrect('side',(z+1.74)/3.43,1-t))
        # Wheel-well return: a real dark inner lip, smooth around the arch.
        for c in (-axle,axle):
            m.grid('arch'+str((side,c)),np.linspace(0,math.pi,15),[0,1],
                   lambda a,t:(side*(width(c+arch*math.cos(a))-.036-.040*t),
                               arch*math.sin(a),c+arch*math.cos(a)),
                   'rubber',out=lambda p:(-side, -max(0,p[1]),0))
    # Crown surfaces. Longitudinal and transverse loops share exact attributes.
    xs=[-1,-.97,-.86,-.65,-.35,0,.35,.65,.86,.97,1]
    for label,zrange,tile in [('bonnet',(.69,1.69),'hood'),('deck',(-1.74,-1.0),'paint')]:
        zcuts=sorted(set(list(np.linspace(*zrange,10))+[r[0] for r in keys if zrange[0]<r[0]<zrange[1]]))
        m.grid(label,zcuts,xs,
               lambda z,t:(t*width(z),shoulder(z)+.038*(1-t*t),
                   z+.038*(1-t*t)*(max(0,(z-1.40)/.29)-max(0,(-z-1.50)/.24))),tile,
               uvfn=lambda z,t,tile=tile,zrange=zrange:uvrect(tile,(t+1)/2,(z-zrange[0])/(zrange[1]-zrange[0])))
    cabin=[(-1.0,width(-1.0),shoulder(-1.0)),(-.84,.558,.515),(-.61,.50,.715),
           (-.50,.49,.752),(-.15,.49,.765),(.21,.50,.743),(.35,.527,.63),(.69,width(.69),shoulder(.69))]
    def roofw(z):return lerp(z,cabin,1)
    def roofy(z):return lerp(z,cabin,2)
    for label,zrange,tile in [('rear_glazing',(-1.0,-.55),'windshield'),
                               ('roof',(-.55,.21),'paint'),('windscreen',(.21,.69),'windshield')]:
        zcuts=sorted(set(list(np.linspace(*zrange,9))+[r[0] for r in cabin if zrange[0]<r[0]<zrange[1]]))
        m.grid(label,zcuts,xs,
               lambda z,t:(t*roofw(z),roofy(z)+.038*(1-t*t),z),tile,
               uvfn=lambda z,t,tile=tile,zrange=zrange:uvrect(tile,(t+1)/2,(z-zrange[0])/(zrange[1]-zrange[0])))
    for side in (-1,1):
        zcuts=sorted(set(list(np.linspace(-1,.69,19))+[r[0] for r in cabin]+[r[0] for r in keys if -1<r[0]<.69]))
        m.grid('side_glazing'+str(side),zcuts,np.linspace(0,1,5),
               lambda z,t:(side*(width(z)*(1-t)+roofw(z)*t),
                            shoulder(z)*(1-t)+roofy(z)*t,z),
               'sideglass',out=(side,0,0),uvfn=lambda z,t:uvrect('sideglass',(z+1)/1.69,1-t))
    # Rounded fascias and a separate crease at the hood/bumper edge.
    for z,sign,tile in [(1.69,1,'front'),(-1.74,-1,'rear')]:
        w=width(z);top=shoulder(z)
        m.grid(tile,np.linspace(-1,1,17),[0,.18,.45,.75,1],
               lambda t,v:(t*(w-.036*(1-v)**2+.014*math.sin(math.pi*v)),
                           bottom+(top-bottom)*v+.038*(1-t*t)*v,z+sign*.038*(1-t*t)),
               tile,out=(0,0,sign))
        m.grid('bumper'+tile,np.linspace(-1,1,19),np.linspace(0,math.pi,5),
               lambda t,a:(t*(w+.012),-.015+.021*math.cos(a),
                           z+sign*(.046*(1-t*t)+.025*math.sin(a))),
               'chrome',out=(0,0,sign))
    m.grid('floor',[-.48,.48],[-1.65,1.59],lambda x,z:(x,bottom,z),'rubber',out=(0,-1,0))
    # Four recessed round headlamps, an explicit chrome bezel and lens.
    for x in (-.49,-.365,.365,.49):
        z=1.69+.038*(1-(x/width(1.69))**2)+.004
        for label,radii,tile,mat in [('bezel',[.052,.045],'chrome','body'),
                                     ('lens',[.045,0],'chrome','front_lights')]:
            m.grid(label+str(x),np.linspace(0,2*math.pi,13),radii,
                   lambda a,r:(x+r*math.cos(a),.16+r*math.sin(a),z+.007*(1-r/.052)),
                   tile,mat,out=(0,0,1))
    for side in (-1,1):
        m.grid('tail'+str(side),np.linspace(-1,1,7),[-1,0,1],
               lambda u,v:(side*.40+u*.14,.145+v*.037,-1.781+.018*u*u),
               'chrome','rear_lights',out=(0,0,-1))
        # Small rounded mirror housing; longitudinal grid, no bevel modifier.
        cx=side*.725;cy=.43;cz=.48
        m.grid('mirror'+str(side),np.linspace(0,2*math.pi,9),np.linspace(-math.pi/2,math.pi/2,5),
               lambda a,b:(cx+.062*math.cos(a)*math.cos(b),cy+.032*math.sin(b),
                            cz+.064*math.sin(a)*math.cos(b)),
               'paint',out=lambda p:p-np.array([cx,cy,cz]))
    return m


def read_glb(path):
    data=path.read_bytes();n=struct.unpack_from('<I',data,12)[0]
    doc=json.loads(data[20:20+n]);blob=data[28+n:]
    def acc(i):
        a=doc['accessors'][i];v=doc['bufferViews'][a['bufferView']]
        dtype={5126:'<f4',5125:'<u4',5123:'<u2'}[a['componentType']]
        width={'VEC3':3,'VEC2':2,'SCALAR':1}[a['type']]
        return np.frombuffer(blob,dtype=dtype,count=a['count']*width,
                             offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,width).copy()
    return doc,acc


class IndexedGlb(H['Glb']):
    def mesh(self,name,primitives,position=(0,0,0)):
        ps=[]
        for v,mat in primitives:
            # Exact full-attribute sharing, not approximate geometric welding.
            v=np.asarray(v,dtype='<f4');v[v==0]=0
            unique,idx=np.unique(v,axis=0,return_inverse=True)
            a=len(self.doc['accessors']);ix=np.asarray(idx,dtype='<u4')
            self.doc['accessors'].append(dict(bufferView=self.view(ix.tobytes()),
                    componentType=5125,count=len(ix),type='SCALAR',min=[int(ix.min())],max=[int(ix.max())]))
            ps.append(dict(attributes={k:self.accessor(unique[:,s]) for k,s in
                [('POSITION',slice(0,3)),('NORMAL',slice(3,6)),('TEXCOORD_0',slice(6,8))]},
                indices=a,material=mat,mode=4))
        self.doc['scenes'][0]['nodes'].append(len(self.doc['nodes']))
        self.doc['nodes'].append(dict(name=name,mesh=len(self.doc['meshes']),translation=list(position)))
        self.doc['meshes'].append(dict(name=name,primitives=ps))


def main(out):
    out.mkdir(parents=True,exist_ok=True)
    tex=atlas();tex.save(out/'cc96-strip-study-atlas.png')
    mesh=author();parts=mesh.build();g=IndexedGlb(tex)
    g.doc['asset']['generator']='TyraX CC96 strip study / authored indexed panel grids'
    mats={k:g.material(k,{'front_lights':(.85,.92,1),'rear_lights':(.85,.035,.015)}.get(k,(1,1,1)),k=='body') for k in parts}
    g.mesh('Body',[(v,mats[k]) for k,v in parts.items()])
    source,accessor=read_glb(PROJECT/'res/models/cc96-efficient.glb')
    wheels=[]
    for node in source['nodes']:
        if 'wheel' not in node['name'].lower() and not node['name'].startswith('Cylinder'):continue
        src=source['meshes'][node['mesh']]['primitives'][0]
        p=accessor(src['attributes']['POSITION']);bounds=np.array([p.min(0),p.max(0)])
        v=H['wheel_mesh'](bounds,(128,128,128),(256,256))
        mat=g.material('wheel rubber and alloy') if not wheels else wheels[0][3]
        pos=node.get('translation',[0,0,0]);g.mesh(node['name'],[(v,mat)],pos)
        wheels.append((v,np.array(pos),node['name'],mat))
    assert len(wheels)==4
    g.write(out/'cc96-strip-study.glb')
    # Quads are retained in OBJ for editing; GLB contains indexed triangles.
    lines=['# CC96 additional strip study; metres, +Y up, +Z forward','mtllib cc96-strip-study.mtl']
    corners=[];corner_ids={};faces=[]
    for ps,uv,mat,group in mesh.faces:
        ids=[]
        used_positions=[]
        for p,t in zip(ps,uv):
            if any(np.linalg.norm(p-q)<1e-10 for q in used_positions):continue
            used_positions.append(p)
            n=mesh.normals[(group,tuple(np.round(p,7)))];n=n/np.linalg.norm(n)
            key=tuple(np.asarray([*p,*n,*t],dtype=np.float32))
            if key not in corner_ids:corner_ids[key]=len(corners)+1;corners.append(key)
            ids.append(corner_ids[key])
        if len(ids)>=3:faces.append((ids,mat,group))
    for v in corners:lines.append('v '+' '.join(f'{x:.9g}' for x in v[:3]))
    for v in corners:lines.append('vt '+f'{v[6]:.9g} {1-v[7]:.9g}')
    for v in corners:lines.append('vn '+' '.join(f'{x:.9g}' for x in v[3:6]))
    for ids,mat,group in faces:
        lines += ['g '+group,'usemtl '+mat,'f '+' '.join(f'{i}/{i}/{i}' for i in ids)]
    (out/'cc96-strip-study-body.obj').write_text('\n'.join(lines)+'\n')
    (out/'cc96-strip-study.mtl').write_text('newmtl body\nKd 1 1 1\nmap_Kd cc96-strip-study-atlas.png\n\nnewmtl front_lights\nKd 0.85 0.92 1\n\nnewmtl rear_lights\nKd 0.85 0.035 0.015\n')
    for k,v in parts.items():
        assert np.isfinite(v).all() and np.allclose(np.linalg.norm(v[:,3:6],axis=1),1,atol=1e-5)
        assert (v[:,6:]>=0).all() and (v[:,6:]<=1).all()
        p=v[:,:3].reshape(-1,3,3)
        assert (np.linalg.norm(np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]),axis=1)>1e-10).all()
    report=dict(body_triangles=sum(len(v)//3 for v in parts.values()),
                parts={k:dict(triangles=len(v)//3,unique_full_vertices=len(np.unique(v,axis=0))) for k,v in parts.items()},
                wheel_triangles=76,wheel_nodes=[dict(name=n,translation=p.tolist()) for v,p,n,mat in wheels],
                source='New authored body; efficient wheel proportions/artwork retained',
                scope='Additional asset only; no scene/importer changes; no hardware FPS claim')
    (out/'geometry.json').write_text(json.dumps(report,indent=2)+'\n')
    render_parts=[dict(verts=v,texture='cc96-strip-study-atlas.png' if k=='body' else '',
                       kd=np.array({'front_lights':(.85,.92,1),'rear_lights':(.85,.035,.015)}.get(k,(1,1,1)))) for k,v in parts.items()]
    for v,p,n,mat in wheels:
        v=v.copy();v[:,:3]+=p
        render_parts.append(dict(verts=v,texture='cc96-strip-study-atlas.png',kd=np.ones(3)))
    sheet=Image.new('RGB',(1440,1000),(23,29,36));draw=ImageDraw.Draw(sheet)
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',22) if Path('C:/Windows/Fonts/segoeui.ttf').exists() else ImageFont.load_default()
    for i,(yaw,elev,label) in enumerate([(.72,.30,'Front three-quarter'),(2.40,.30,'Rear three-quarter'),
                                         (math.pi/2,.10,'Side profile'),(.72,.30,'Topology / all edges')]):
        s,c=math.sin(yaw),math.cos(yaw);a,b=math.sin(elev),math.cos(elev)
        basis=np.array([[c,0,-s],[-a*s,b,-a*c],[b*s,a,b*c]])
        points=np.concatenate([p['verts'][:,:3] for p in render_parts])@basis.T
        lo,hi=points[:,:2].min(0),points[:,:2].max(0);center=(lo+hi)/2
        extent=max((hi-lo)[0]/660,(hi-lo)[1]/370)*1.08
        bounds=(center-np.array([330,185])*extent,center+np.array([330,185])*extent)
        im,_=H['raster'](render_parts,out,basis,(660,370),bounds,True)
        if i==3:
            im=Image.new('RGB',(660,370),(18,25,31));dd=ImageDraw.Draw(im)
            for ps,uv,mat,group in mesh.faces:
                xy=(ps@basis.T)[:,:2];xy=(xy-bounds[0])/(bounds[1]-bounds[0])*[660,370];xy[:,1]=370-xy[:,1]
                xy=[tuple(p) for p in xy];dd.line(xy+[xy[0]],fill=(77,143,158),width=1)
        x=30+(i%2)*720;y=105+(i//2)*440
        sheet.paste(im,(x,y));draw.text((x,y-35),label,fill=(224,228,231),font=font)
    draw.text((30,16),'CC96 / additional strip-ready body study',fill='white',font=font)
    draw.text((30,953),f"{report['body_triangles']} body triangles / 76 per wheel / host inspection, not an in-game render",fill=(175,190,202),font=font)
    sheet.save(out/'cc96-strip-study-preview.png')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('output',nargs='?',type=Path,default=PROJECT/'res/models/cc96-strip-study')
    main(p.parse_args().output)

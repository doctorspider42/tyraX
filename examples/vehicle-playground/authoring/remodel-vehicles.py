"""Author new station/patch vehicle bodies, without mesh simplification.

python remodel-vehicles.py BASELINE_PROJECT OUTPUT_MODELS_DIRECTORY
The original bake supplies wheel dimensions and driving anchors only. Body
topology, cross sections, glass borders, arches and surface artwork are authored
here. No original body triangles are copied and no decimator is called.
"""
from pathlib import Path
import argparse
import json
import math
import runpy
import numpy as np
from PIL import Image, ImageDraw

H = runpy.run_path(str(Path(__file__).with_name('prepare-efficient-vehicles.py')))
Glb, read_tmdl, wheel_mesh, rim_texture = (H[n] for n in
    ('Glb', 'read_tmdl', 'wheel_mesh', 'rim_texture'))

# z, shoulder width, shoulder height, crown width, crown height. Metres in
# the vehicle's axle-centred coordinate system (+Z forward, +Y up).
# Extra stations around each tyre are inserted analytically, not decimated.
DESIGNS = {
 'CC96': dict(file='cc96-remodeled', paint=(224,151,27), base=-.055,
   stations=[(-1.74,.61,.22,.54,.30),(-1.55,.68,.30,.59,.35),
    (-.98,.70,.32,.60,.39),(-.48,.69,.34,.50,.735),
    (.23,.69,.34,.50,.75),(.69,.68,.32,.59,.36),
    (1.34,.67,.27,.57,.31),(1.68,.59,.20,.52,.255)],
   cabin=(-.98,.69), roof=(-.48,.23), arch=.272, lip=.026),
 'Tristar Racer': dict(file='tristar-remodeled', paint=(79,49,136), base=-.19,
   stations=[(-2.10,.67,.25,.55,.35),(-1.80,.87,.41,.70,.51),
    (-1.35,.94,.44,.72,.55),(-.70,.87,.42,.55,.81),
    (-.12,.85,.40,.55,.83),(.42,.88,.38,.70,.46),
    (1.18,.94,.39,.72,.43),(1.62,.89,.30,.69,.38),
    (1.86,.78,.19,.62,.29),(2.00,.62,.13,.51,.22)],
   cabin=(-1.35,.42), roof=(-.70,-.12), arch=.355, lip=.035),
}

TILES = dict(paint=(2,2,28,28), glass=(34,2,60,28), side=(2,34,124,60),
             hood=(130,2,124,92), front=(2,98,124,28), rear=(130,98,124,28),
             roof=(2,130,124,60))

def artwork(d):
    """One opaque atlas; panel seams and small fittings cost no geometry."""
    color=np.array(d['paint'])
    im=Image.new('RGB',(256,256),tuple(color)); dr=ImageDraw.Draw(im)
    for name,(x,y,w,h) in TILES.items():
        for j in range(h):
            factor=1 if name=='paint' else .84+.16*math.sin(math.pi*(j+.5)/h)
            dr.line((x,y+j,x+w-1,y+j),fill=tuple((color*factor).astype(int)))
    x,y,w,h=TILES['glass']
    dr.rectangle((x,y,x+w-1,y+h-1),fill=(30,39,49))
    # Glass has no baked horizon: environment reflection remains the live pass.
    x,y,w,h=TILES['side']
    dark=tuple((color*.40).astype(int)); light=tuple((color*1.08).clip(0,255).astype(int))
    dr.line((x+10,y+2,x+10,y+h-8,x+w-12,y+h-8,x+w-12,y+2),fill=dark,width=1)
    dr.line((x+11,y+3,x+11,y+h-9),fill=light)
    dr.rounded_rectangle((x+20,y+9,x+34,y+12),radius=1,fill=(27,29,30))
    dr.line((x+21,y+9,x+33,y+9),fill=(176,177,169))
    for k in range(3):
        dr.line((x+w-27,y+h-17+k*3,x+w-9,y+h-20+k*3),fill=dark)
    x,y,w,h=TILES['hood']
    if d is DESIGNS['CC96']:
        dr.polygon([(x+10,y+2),(x+w-11,y+2),(x+w-18,y+h-2),(x+17,y+h-2)],
                   fill=(26,29,32))
        for sx in (x+25,x+w-29):
            dr.line((sx,y+6,sx,y+h-6),fill=(57,61,64),width=2)
    for sx in (x+16,x+w-18):
        dr.line((sx,y+5,sx,y+h-7),fill=dark)
        dr.line((sx+1,y+5,sx+1,y+h-7),fill=light)
    if d is DESIGNS['Tristar Racer']:
        for sx in (x+26,x+w-42):
            dr.rounded_rectangle((sx,y+25,sx+16,y+43),radius=2,fill=(23,25,28))
            for j in range(4):
                dr.line((sx+2,y+28+j*4,sx+14,y+28+j*4),fill=(55,56,57))
    for name in ('front','rear'):
        x,y,w,h=TILES[name]
        dr.rounded_rectangle((x+12,y+7,x+w-13,y+h-7),radius=3,fill=(19,22,25))
        for j in range(y+10,y+h-8,3):
            dr.line((x+16,j,x+w-17,j),fill=(52,55,57))
        if name=='rear':
            dr.rectangle((x+w//2-10,y+9,x+w//2+10,y+18),fill=(177,182,177))
    # Small solid chrome/accent tiles, away from filtered panel borders.
    dr.rectangle((100,2,125,29),fill=(157,169,175))
    im.paste(rim_texture(),(128,128))
    # Author a fixed PS2 palette before the pipeline quantizer. Continuous
    # photographic ramps otherwise turn the windows into dithered noise.
    colors=[tuple((color*f).clip(0,255).astype(int)) for f in (.4,.70,.87,1,1.08)]
    colors += [(20,22,25),(30,39,49),(53,67,82),(65,75,89),
               (48,50,52),(74,77,79),(104,110,115),(145,155,164),
               (183,190,191),(219,224,224),(245,245,235)]
    pal=Image.new('P',(1,1));pal.putpalette([int(v) for c in colors for v in c]+[0]*(768-3*len(colors)))
    return im.quantize(palette=pal,dither=Image.Dither.NONE)

class Surface:
    def __init__(self):
        self.faces=[]

    def patch(self, points, tile='paint', material='paint', smooth='body', uv=None):
        p=np.array(points,dtype=float)
        if uv is None:
            x,y,w,h=TILES[tile]
            x+=.5;y+=.5;w-=1;h-=1
            uv=np.array([(x,y+h),(x+w,y+h),(x+w,y),(x,y)],float)/256
        for ids in ((0,1,2),(0,2,3)) if len(p)==4 else ((0,1,2),):
            vs=p[list(ids)]; n=np.cross(vs[1]-vs[0],vs[2]-vs[0])
            if np.linalg.norm(n)<1e-9:
                continue
            self.faces.append((vs,np.array(uv)[list(ids)],material,smooth,n/np.linalg.norm(n)))

    def window(self, points):
        p=np.array(points); c=p.mean(0); q=c+(p-c)*.88
        for i in range(len(p)):
            j=(i+1)%len(p)
            self.patch([p[i],p[j],q[j],q[i]],smooth='frame')
        if len(p)==4:self.patch(q,'glass',smooth='glass')
        else:
            for ids in ((0,1,2,5),(5,2,3,4)):
                self.patch(q[list(ids)],'glass',smooth='glass')

    def box(self, lo, hi, tile='paint', material='paint'):
        a,b,c=lo;d,e,f=hi
        for face in [((a,b,c),(a,b,f),(d,b,f),(d,b,c)),
                     ((a,e,c),(d,e,c),(d,e,f),(a,e,f)),
                     ((a,b,c),(d,b,c),(d,e,c),(a,e,c)),
                     ((d,b,f),(a,b,f),(a,e,f),(d,e,f)),
                     ((a,b,f),(a,b,c),(a,e,c),(a,e,f)),
                     ((d,b,c),(d,b,f),(d,e,f),(d,e,c))]:
            self.patch(face,tile,material,smooth=None)

    def primitives(self, glb):
        normals={}
        for ps,uv,mat,group,n in self.faces:
            if group:
                for p in ps:
                    key=(tuple(np.round(p,6)),group)
                    normals[key]=normals.get(key,np.zeros(3))+n
        materials={'paint':glb.material('paint and glass'),
                   'matte':glb.material('rubber trim',(.065,.07,.08),False),
                   'rear':glb.material('rear lights',(.65,.045,.025),False),
                   'front':glb.material('headlights',(.8,.83,.75),False)}
        groups={}
        for ps,uv,mat,group,n in self.faces:
            # Only the hidden floor and wing supports use this dark swatch.
            # Share the paint bag instead of paying a whole material submit
            # for two floor triangles (the tiny reflected area is intentional).
            if mat=='matte':
                mat='paint';uv=np.full((3,2),128.5/256)
            for p,t in zip(ps,uv):
                nn=normals[(tuple(np.round(p,6)),group)] if group else n
                nn=nn/max(np.linalg.norm(nn),1e-9)
                groups.setdefault(mat,[]).append([*p,*nn,*t])
        return [(np.array(v,dtype=np.float32),materials[m]) for m,v in groups.items()]

def body(d, drive):
    s=Surface(); stations=np.array(d['stations']); rear,front=stations[[0,-1],0]
    axle=drive['wheelBase']/2; r=d['arch']; bottom=d['base']
    # Wheel openings are actual silhouette cuts. Explicit arch stations and
    # a narrow bevel strip retain the highlight without a stacked torus mesh.
    cuts=[z for z in stations[:,0]]
    for center in (-axle,axle):
        cuts.extend(center+(r+d['lip'])*math.cos(t) for t in np.linspace(0,math.pi,9))
    zs=sorted(set(round(z,6) for z in cuts if rear<=z<=front))
    rings=[]
    for z in zs:
        w,shoulder,cw,top=[np.interp(z,stations[:,0],stations[:,i]) for i in range(1,5)]
        dz=min(abs(z-axle),abs(z+axle))
        # The wheel centre is y=0. The crown of the cut follows its tyre.
        low=max(bottom,math.sqrt(max(0,r*r-dz*dz))) if dz<r else bottom
        lip=min(shoulder-.012,low+d['lip'])
        # Guarantee a non-inverted arch shoulder even on the low coupe nose.
        shoulder=max(shoulder,lip+.018)
        ring=[(w*.96,low,z),(w,lip,z),(w,shoulder,z),(cw,top-.022,z),
              (0,top,z),(-cw,top,z),(-w,shoulder,z),(-w,lip,z),(-w*.96,low,z)]
        ring[5]=(-cw,top-.022,z)
        rings.append(np.array(ring))
    for a,b in zip(rings,rings[1:]):
        mid=(a[0,2]+b[0,2])/2
        in_cabin=d['cabin'][0]<mid<d['cabin'][1]
        on_roof=d['roof'][0]<=mid<=d['roof'][1]
        for j in range(8):
            p=[a[j],b[j],b[j+1],a[j+1]]
            # +X side winds out; consistent around the upper shell.
            if j in (2,5) and in_cabin:
                # Windows are applied in larger patches below, not one tiny
                # window per arch segment.
                continue
            if j in (3,4) and in_cabin and not on_roof:
                continue
            tile='hood' if j in (3,4) and mid>d['cabin'][1] else 'paint'
            if j in (1,6) and -axle+r<mid<axle-r:
                tile='side'
            uv=None
            if tile=='side':
                x,y,w,h=TILES[tile]
                uv=[((x+(q[2]+axle-r)/(2*(axle-r))*w)/256,
                     (y+np.clip(1-(q[1]-bottom)/.65,0,1)*h)/256) for q in p]
            if tile=='hood':
                x,y,w,h=TILES[tile]
                uv=[((x+(q[0]/1.9+.5)*w)/256,
                     (y+(q[2]-d['cabin'][1])/(front-d['cabin'][1])*h)/256) for q in p]
            s.patch(p[::-1],tile,smooth='arch' if j in (0,7) else 'body',
                    uv=None if uv is None else uv[::-1])
        # A single underside strip closes only the centre hull, never the
        # open wheel wells. No duplicate hidden chassis / engine / interior.
    s.patch([(-.48,bottom,rear),(.48,bottom,rear),(.48,bottom,front),(-.48,bottom,front)],
            material='matte',smooth=None)
    # Cabin glazing: one window between each authored station, with real
    # narrow paint frames and no glass/paint coplanar overlays.
    cabin_z=[z for z in stations[:,0] if d['cabin'][0]<=z<=d['cabin'][1]]
    def ring(z):
        return rings[min(range(len(zs)),key=lambda i:abs(zs[i]-z))]
    for za,zb in zip(cabin_z,cabin_z[1:]):
        a,b=ring(za),ring(zb)
        for j in (2,5):
            s.window([a[j+1],b[j+1],b[j],a[j]])
        if not(d['roof'][0]<=(za+zb)/2<=d['roof'][1]):
            # Windshield/rear glass cross the crown without a centre pillar.
            s.window([a[5],b[5],b[4],b[3],a[3],a[4]])
    # Front/rear fascia use texture detail instead of many grille bars.
    for end,which in [(rings[0],'rear'),(rings[-1],'front')]:
        w=end[2,0]; z=end[0,2]; y0=bottom; y1=end[2,1]
        # Three fascia bands make a rolled bumper, not a flat cuboid end.
        sign=1 if which=='front' else -1
        for ya,yb,wa,wb,za,zb,tile in [
          (y0,y0+.055,w*.94,w,z-sign*.045,z, 'paint'),
          (y0+.055,y1-.035,w,w,z,z,which),
          (y1-.035,y1,w,w,z,z-sign*.015,'paint')]:
            p=[(-wa,ya,za),(wa,ya,za),(wb,yb,zb),(-wb,yb,zb)]
            if which=='rear':p.reverse()
            s.patch(p,tile,smooth=None)
        # Close the small shoulder/crown wedge above the fascia.
        for j in range(2,6):
            pp=[(0,y1,z),end[j],end[j+1]]
            if which=='rear':pp.reverse()
            s.patch(pp,smooth=None)
        # Lamps are actual separate addressable faces. Offset from the fascia
        # avoids depth fighting; lamp triangles share the existing light part.
        for side in (-1,1):
            cx=side*w*.72; hw=w*.18; yy=y0+(y1-y0)*.63
            zz=z+(.009 if which=='front' else -.009)
            if which=='front' and d is DESIGNS['CC96']:
                for delta in (-.06,.06):
                    for k in range(8):
                        a=k*math.pi/4;b=(k+1)*math.pi/4
                        s.patch([(cx+delta,yy,zz),
                          (cx+delta+.047*math.cos(a),yy+.047*math.sin(a),zz),
                          (cx+delta+.047*math.cos(b),yy+.047*math.sin(b),zz)],
                          material='front',smooth=None)
            else:
                pp=[(cx-hw,yy-.04,zz),(cx+hw,yy-.035,zz),
                    (cx+hw*.85,yy+.045,zz),(cx-hw*.80,yy+.03,zz)]
                if which=='rear':pp.reverse()
                s.patch(pp,material=which,smooth=None)
        if d is DESIGNS['CC96']:
            # One low-profile chrome bumper, shared atlas and paint pass.
            TILES['chrome']=(103,5,18,20)
            za,zb=sorted((z-sign*.006,z+sign*.045))
            s.box((-w*.96,y0+.015,za),(w*.96,y0+.04,zb),'chrome')
    # Small mirror housings carry silhouette. No stem cylinders or inner lens.
    z=d['cabin'][1]-.13
    w=np.interp(z,stations[:,0],stations[:,1])
    y=np.interp(z,stations[:,0],stations[:,2])+.055
    for side in (-1,1):
        xx=side*(w+.065)
        s.box((xx-.065,y-.035,z-.065),(xx+.065,y+.035,z+.045))
    if d is DESIGNS['Tristar Racer']:
        # The GT's broad rear wing remains a real aerofoil silhouette.
        z=-1.55
        for side in (-1,1):
            x=side*.49
            s.box((x-.025,.52,z-.055),(x+.025,.90,z+.055),material='matte')
        s.box((-.91,.89,z-.15),(.91,.94,z+.12))
        for side in (-1,1):
            x=side*.90
            s.box((x-.018,.85,z-.16),(x+.018,1.01,z+.13))
    return s

def main(baseline,output):
    manifest=json.loads(next(baseline.glob('*.tyra')).read_text())
    output.mkdir(parents=True,exist_ok=True)
    report={}
    for v in manifest['vehicles']:
        if v['name'] not in ('CC96','Tristar Racer'):continue
        d=DESIGNS[v['name']]; g=Glb(artwork(d)); shell=body(d,v['drive'])
        primitives=shell.primitives(g);g.mesh('Body',primitives)
        wb,_=read_tmdl(baseline/'.res-baked/vehicles'/('veh-'+v['id']+'-wheel.tmdl'))
        wheels=wheel_mesh(wb,(128,128,128),(256,256))
        hx,hz=v['drive']['track']/2,v['drive']['wheelBase']/2
        for i,(x,z) in enumerate([(-hx,hz),(hx,hz),(-hx,-hz),(hx,-hz)]):
            end='front' if z>0 else 'rear'
            name=v['wheels'][i]['node'] if v.get('wheels') else f'{end} wheel {i+1}'
            mat=g.material(f'{end} wheel {i+1}')
            g.mesh(name,[(wheels,mat)],(x,0,z))
        g.write(output/(d['file']+'.glb'))
        artwork(d).save(output/(d['file']+'-atlas.png'))
        report[v['name']]=dict(body_triangles=sum(len(p)//3 for p,m in primitives),
             wheel_triangles=len(wheels)//3,method='authored station/patch topology; no decimation')
    (output/'remodeled-authoring.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();main(a.baseline,a.output)

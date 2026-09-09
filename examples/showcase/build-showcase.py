#!/usr/bin/env python3
"""Author Aster — a measured, PS2-sized observatory. Python 3 + Pillow.

Run beside the checked-in manifest. Asset preparation is separate; rebuilding
the scene never needs C:/Assets, Blender, downloads, or another worktree.
Coordinates are metres, Y up. Finished walking surfaces are at Y=0.
"""
from pathlib import Path
import hashlib
import json
import math
import random
import struct
import wave
import sys
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
RES = HERE / 'res' / 'aster'
RES.mkdir(parents=True, exist_ok=True)
OBJECTS = HERE / 'objects'
TAU = math.tau


def write(path, value):
    path.write_text(value, encoding='utf-8', newline='\n')


def texture(name, base, kind):
    rng = random.Random(19)
    im = Image.new('RGB', (128, 128))
    for y in range(128):
        for x in range(128):
            n = rng.randint(-7, 7)
            if kind == 'stone':
                mortar = y % 32 < 2 or (x + (y // 32 % 2) * 32) % 64 < 2
                n += -30 if mortar else 0
            elif kind == 'tile':
                n += -32 if x % 32 < 2 or y % 32 < 2 else 0
            elif kind == 'water':
                n = int(13 * math.sin(x*.19 + math.sin(y*.12)*2) + 8*math.cos(y*.28))
            im.putpixel((x, y), tuple(max(0, min(255, v+n)) for v in base))
    im.save(RES / (name+'.png'))


MATERIALS = {
    'limestone': ((.84,.74,.56), 'stone'),
    'ivory': ((.94,.87,.68), None),
    'shadowstone': ((.33,.43,.43), 'stone'),
    'paving': ((.66,.68,.60), 'tile'),
    'terracotta': ((.65,.29,.19), 'tile'),
    'copper': ((.13,.46,.42), None),
    'gold': ((.80,.57,.23), None),
    'brass': ((.91,.66,.27), None),
    'dark': ((.09,.17,.20), None),
    'leaves': ((.13,.30,.22), None),
    'leavesLight': ((.26,.42,.25), None),
    'bark': ((.28,.20,.13), None),
    'water': ((.12,.43,.48), 'water'),
    'glow': ((.48,.92,.92), None),
    'amber': ((1,.72,.28), None),
    'flower': ((.72,.31,.42), None),
}


def materials():
    lib = []
    for name, (color, kind) in MATERIALS.items():
        text = f'newmtl {name}\nKd '+ ' '.join(str(c) for c in color)+'\n'
        if kind:
            texture(name, tuple(int(c*255) for c in color), kind)
            text = f'newmtl {name}\nKd 1 1 1\nmap_Kd {name}.png\n'
        if name in ('gold', 'copper', 'water'):
            text += 'refl -type sphere -mm 0 '+ ('0.32' if name == 'water' else '0.22') +' @sky\n'
        if name in ('glow', 'amber'):
            text += 'Ke '+ ' '.join(str(c*.8) for c in color)+'\n'
        lib.append(text)
        write(RES/(name+'.mtl'), text)
    write(RES/'aster.mtl', '\n'.join(lib))


def soundscape():
    # Original, periodic additive composition: no externally licensed audio.
    rate=22050;duration=24
    with wave.open(str(RES/'tides.wav'),'wb') as f:
        f.setparams((2,2,rate,0,'NONE','not compressed'))
        data=bytearray()
        for i in range(rate*duration):
            t=i/rate
            v=sum(math.sin(TAU*round(hz*duration)/duration*t)*(1+.18*math.sin(TAU*(j+1)*t/duration))/(j+2)
                  for j,hz in enumerate((110,164.8138,220,293.6648,329.6276)))
            data+=struct.pack('<hh',int(v*1800),int(v*1800))
        f.writeframes(data)
    (HERE/'res'/'sfx').mkdir(parents=True,exist_ok=True)
    with wave.open(str(HERE/'res'/'sfx'/'lens.wav'),'wb') as f:
        f.setparams((1,2,rate,0,'NONE','not compressed'))
        f.writeframes(b''.join(struct.pack('<h',int(6000*math.exp(-i/rate*4)*(math.sin(TAU*880*i/rate)+.3*math.sin(TAU*1320*i/rate)))) for i in range(rate)))


class Mesh:
    def __init__(self):
        self.faces = []

    def face(self, points, material='limestone', uv=None):
        if material in MATERIALS and MATERIALS[material][1] is None:
            uv=[(0,0)]*len(points)
        elif uv is None:
            a,b,c = points[:3]
            u = math.dist(a,b)/2
            v = math.dist(b,c)/2
            uv = [(0,0),(u,0),(u,v),(0,v)][:len(points)]
        self.faces.append((points, material, uv))

    def box(self, center, size, mat='limestone'):
        x,y,z = center
        a,b,c = [s/2 for s in size]
        p=[(x-a,y-b,z-c),(x+a,y-b,z-c),(x+a,y+b,z-c),(x-a,y+b,z-c),
           (x-a,y-b,z+c),(x+a,y-b,z+c),(x+a,y+b,z+c),(x-a,y+b,z+c)]
        for f in [(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(3,7,6,2),(0,1,5,4)]:
            self.face([p[i] for i in f], mat)

    def lathe(self, profile, mat='ivory', n=24, center=(0,0,0)):
        x,y,z=center
        repeats=max(r for r,h in profile)*math.pi
        for (r0,h0),(r1,h1) in zip(profile, profile[1:]):
            for i in range(n):
                a,b=i*TAU/n,(i+1)*TAU/n
                self.face([(x+r0*math.cos(a),y+h0,z+r0*math.sin(a)),
                           (x+r1*math.cos(a),y+h1,z+r1*math.sin(a)),
                           (x+r1*math.cos(b),y+h1,z+r1*math.sin(b)),
                           (x+r0*math.cos(b),y+h0,z+r0*math.sin(b))], mat,
                          [(i/n*repeats,h0/2),(i/n*repeats,h1/2),((i+1)/n*repeats,h1/2),((i+1)/n*repeats,h0/2)])

    def ring(self, radius, thickness, mat='brass', n=32, axis='z'):
        for i in range(n):
            a,b=i*TAU/n,(i+1)*TAU/n
            for j in range(4):
                q=[]
                for t,p in [(a,j),(b,j),(b,j+1),(a,j+1)]:
                    h=p*TAU/4+math.pi/4
                    r=radius+thickness*math.cos(h)
                    v=(r*math.cos(t),r*math.sin(t),thickness*math.sin(h))
                    if axis=='y': v=(v[0],v[2],v[1])
                    q.append(v)
                self.face(q,mat)

    def save(self,name):
        lines=['# Aster authored mesh; metres, Y up','mtllib aster.mtl']
        positions={};texcoords={};faces=[]
        for points,mat,uv in self.faces:
            indices=[]
            for p,t in zip(points,uv):
                p=tuple(round(c,5) for c in p);t=tuple(round(c,5) for c in t)
                if p not in positions:positions[p]=len(positions)+1
                if t not in texcoords:texcoords[t]=len(texcoords)+1
                indices.append((positions[p],texcoords[t]))
            for i in range(1,len(points)-1):
                tri=[indices[j] for j in (0,i,i+1)]
                if len({v for v,t in tri})==3:faces.append((mat,tri))
        # Weld positions, keep UV seams deliberate. Untextured surfaces use
        # constant UVs; decorative per-face UV islands would lock the LOD weld.
        lines += ['v '+' '.join(f'{c:.5f}' for c in p) for p in positions]
        lines += ['vt '+' '.join(f'{c:.5f}' for c in p) for p in texcoords]
        previous=None
        for mat,tri in faces:
            if mat!=previous:lines.append('usemtl '+mat);previous=mat
            lines.append('f '+' '.join(f'{v}/{t}' for v,t in tri))
        write(RES/(name+'.obj'),'\n'.join(lines)+'\n')


def geometry():
    # A complete voussoir arch: real opening, aligned spring lines and piers.
    m=Mesh()
    for x in (-3.55,3.55):
        m.box((x,2,0),(1.05,4,1.1))
        m.box((x,.18,0),(1.35,.36,1.4),'shadowstone')
        m.box((x,3.86,0),(1.4,.28,1.45),'ivory')
    for i in range(16):
        a,b=i*math.pi/16+.004,(i+1)*math.pi/16-.004
        pts=[(r*math.cos(t),4+r*math.sin(t),z) for z in (-.55,.55)
             for r,t in [(3,a),(3,b),(4.08,b),(4.08,a)]]
        for f in [(0,1,2,3),(7,6,5,4),(0,4,5,1),(3,2,6,7),(0,3,7,4),(1,5,6,2)]:
            m.face([pts[j] for j in f], 'ivory' if i%4==0 else 'limestone')
    m.box((0,8.26,0),(8.2,.34,1.45),'ivory')
    m.save('arcade')
    # Column capital, shaft and plinth are authored as one watertight profile.
    m=Mesh();m.lathe([(0,0),(.7,0),(.7,.2),(.53,.34),(.40,.46),(.34,4.5),(.5,4.65),(.65,4.72),(.65,4.95),(0,4.95)],n=10);m.save('column')
    m=Mesh();m.lathe([(0,0),(5.5,0),(5.5,.3),(5.1,.48),(5.1,.7)],'ivory',32)
    m.lathe([(5.1,.7),(5.05,1.2),(4.8,2.0),(4.4,2.8),(3.7,3.6),(2.7,4.2),(1.4,4.6),(.45,4.8),(.25,5.5),(0,5.8)],'copper',32)
    ribs=[(5.13,.78),(4.85,1.94),(4.44,2.72),(3.75,3.51),(2.78,4.12),(1.5,4.53),(.45,4.88)]
    for i in range(12):
        a=i*TAU/12
        for (r0,h0),(r1,h1) in zip(ribs,ribs[1:]):
            m.face([(r0*math.cos(a-.01),h0,r0*math.sin(a-.01)),(r1*math.cos(a-.01),h1,r1*math.sin(a-.01)),
                    (r1*math.cos(a+.01),h1,r1*math.sin(a+.01)),(r0*math.cos(a+.01),h0,r0*math.sin(a+.01))],'gold')
    m.save('dome')
    m=Mesh();m.lathe([(0,0),(2.6,0),(2.6,.22),(2.3,.4),(2.3,.6),(1.85,.7),(1.85,1.05),(1.4,1.25),(1.4,1.4),(0,1.4)],'ivory',32);m.save('orrery-base')
    for name,r,t in [('orbit-outer',2.9,.10),('orbit-inner',2.2,.085),('orbit-equator',2.6,.07)]:
        m=Mesh();m.ring(r,t,axis='y' if name.endswith('equator') else 'z');m.save(name)
    m=Mesh();m.lathe([(0,0),(.6,0),(.75,.16),(.7,.3),(.48,.45),(.42,1.0),(.58,1.1),(0,1.1)],'terracotta',10);m.save('planter')
    m=Mesh();m.lathe([(0,0),(.18,0),(.13,3.5),(0,4)],'bark',7)
    rng=random.Random(47)
    levels=[(.1,1.1),(.55,1.4),(.8,2.1),(.94,3.0),(.9,3.8),(.72,4.8),(.52,5.7),(.26,6.5),(.02,7.1)]
    rings=[[(r*(1+rng.uniform(-.12,.12))*math.cos(j*TAU/12),h+rng.uniform(-.06,.06),r*(1+rng.uniform(-.12,.12))*math.sin(j*TAU/12)) for j in range(12)] for r,h in levels]
    for a,b in zip(rings,rings[1:]):
        for j in range(12):m.face([a[j],b[j],b[(j+1)%12],a[(j+1)%12]],'leavesLight' if j%4==0 else 'leaves')
    m.save('cypress')
    m=Mesh()
    for i in range(5):
        a=i*TAU/5
        x,z=.55*math.cos(a),.55*math.sin(a)
        m.lathe([(0,0),(.42,.15),(.5,.45),(.3,.7),(0,.8)],'leavesLight',5,center=(x,0,z))
        for k in range(5):
            b=k*TAU/5
            m.face([(x,.82,z),(x+.17*math.cos(b),.9,z+.17*math.sin(b)),(x+.23*math.cos(b+.3),.84,z+.23*math.sin(b+.3)),(x+.17*math.cos(b+.65),.9,z+.17*math.sin(b+.65))],'flower')
    m.save('flowers')
    m=Mesh();m.lathe([(0,0),(1.3,0),(1.3,.18),(1.0,.25),(.9,1.7),(1.1,1.85),(0,1.85)],'limestone',10);m.save('pedestal')
    m=Mesh();m.lathe([(0,0),(4,0),(4,1),(3,3),(2.8,7),(2.1,10),(1.3,12),(0,13)],'shadowstone',9);m.save('island')
    m=Mesh()
    m.lathe([(0,0),(2.7,0),(2.7,.5),(2,1),(1.45,14),(2.15,14.3),(2.15,14.6),(1.3,14.7),(1.3,17),(1.9,17.1),(0,19)],'ivory',16)
    m.lathe([(1.32,14.9),(1.32,16.6)],'amber',16);m.save('lighthouse')
    m=Mesh();m.lathe([(0,0),(.5,0),(.5,.3),(.14,.5),(.1,2.8),(.4,2.9),(.4,3.6),(.55,3.68),(0,4)],'gold',8)
    m.lathe([(.32,3),(.32,3.5)],'amber',8);m.save('lantern')
    # Tiled water avoids one giant polygon at grazing view angles.
    m=Mesh()
    for x in range(-4,4,2):
        for z in range(-16,16,2):m.face([(x,0,z),(x,0,z+2),(x+2,0,z+2),(x+2,0,z)],'water')
    m.save('canal-water')
    # Draped pennants have a shaped hem and physical folds, not flat rectangles.
    m=Mesh()
    for i in range(8):
        x0=-.9+i*.225;x1=x0+.225
        y0=-2.8+abs(x0)*.7;y1=-2.8+abs(x1)*.7
        z0=.10*math.cos(i*math.pi/2);z1=.10*math.cos((i+1)*math.pi/2)
        m.face([(x0,0,z0),(x0,y0,z0),(x1,y1,z1),(x1,0,z1)],'copper')
    m.box((0,.05,0),(2.2,.10,.1),'gold');m.save('pennant')
    # Eight-point compass rose, with real inlay thickness above the paving.
    m=Mesh()
    for i in range(8):
        a=i*TAU/8
        tip=(3*math.sin(a),.02,3*math.cos(a))
        left=(.6*math.sin(a-.35),.02,.6*math.cos(a-.35))
        right=(.6*math.sin(a+.35),.02,.6*math.cos(a+.35))
        m.face([(0,.02,0),left,tip],'gold')
        m.face([(0,.02,0),tip,right],'dark')
    m.save('compass-rose')


def graph(chains):
    nodes=[];links=[];index=1
    for row,chain in enumerate(chains):
        previous=None
        for col,item in enumerate(chain):
            kind,text,nums,*extra=item
            node={'id':index,'type':kind,'pos':[col*250,row*150],'str':text,'num':list(nums)+[0]*(4-len(nums))}
            if extra:node['str2']=extra[0]
            nodes.append(node)
            if previous is not None:
                edge={'id':1000+len(links),'from':previous,'to':index}
                if kind=='SetHudVisible' and nums and nums[0]==0:edge['pin']=1
                links.append(edge)
            # Ordinary actions have NO exec output. Fan their siblings out of
            # the trigger/control node; only explicit flow nodes advance it.
            if previous is None or kind in ('DoOnce','Branch','Delay','Sequence','Cooldown'):
                previous=index
            index+=1
    return {'nextId':1100+index,'nodes':nodes,'links':links}


def spin(x=0,y=0,z=0):
    return graph([[('OnStart','',[]),('SpinObject','',[x,y,z])]])


def use_text(text):
    return graph([[('OnUsed','',[]),('DisplayText','',[.5,.84,16,5],text)]])


objects=[]
def obj(name,kind,pos,scale=(1,1,1),rot=(0,0,0),**kw):
    o={'id':hashlib.sha1(('aster:'+name).encode()).hexdigest()[:16],'name':name,'type':kind,'position':list(pos),'scale':list(scale),'rotation':list(rot),'color':[1,1,1],'physics':False}
    o.update(kw);objects.append(o);return o


def model(name,mesh,pos,scale=(1,1,1),rot=(0,0,0),**kw):
    return obj(name,'model',pos,scale,rot,model='res/aster/'+mesh+'.obj',**kw)


def box(name,pos,size,mat='limestone',**kw):
    # World-sized UVs: a 34 m terrace must not stretch one tiny tile across it.
    m=Mesh();m.box((0,0,0),size,mat);m.save(name)
    return model(name,name,pos,**kw)


def light(name,pos,color=(1,.66,.28),radius=5,dynamic=False):
    return obj(name,'point-light',pos,color=list(color),light={'brightness':.65,'radius':radius,'dynamic':dynamic,'flicker':.08 if dynamic else 0,'beam':1})


def key(t,eye,target,fov=55):
    return dict(t=t,eye=eye,target=target,fov=fov,ease=1)


def sequence(name,keys):
    return dict(name=name,duration=keys[-1]['t'],loop=False,cameraEnabled=True,hidePlayer=True,bars=.75,skippable=True,fadeIn=.7,fadeOut=.7,barsSlideIn=.5,barsSlideOut=.5,tracks=[],cameraKeys=keys)


def batch_architecture():
    """Bake measured modules into four static districts, retaining interactions.

    PS2 submission overhead is paid per material bag, even for tiny models.
    These are authored mesh assets, not an engine batching feature. The source
    recipe above keeps every placement independently editable/reproducible.
    """
    groups={};remap={};kept=[]
    floor_names={'arrival-terrace','west-promenade','east-promenade','observatory-terrace','crossing','ocean','tidal-channel'}
    for o in objects:
        x,y,z=o['position']
        eligible=(o['type']=='model' and o.get('model','').endswith('.obj') and not o.get('flowGraph') and
                  not o.get('usable') and o['name'] not in floor_names and not o.get('drawDistance',0)>80)
        # Preserve the independent colliders around the throw/pickup court.
        if z>17 and o.get('collision')!='none':eligible=False
        if not eligible:kept.append(o);continue
        region='rotunda' if z<-17 else 'arrival' if z>17 else 'west' if x<0 else 'east'
        name='district-'+region
        mesh=groups.setdefault(name,Mesh());remap[o['name']]=name
        vs=[];uvs=[];mat='ivory'
        ax,ay,az=[math.radians(a) for a in o['rotation']]
        for line in (HERE/o['model']).read_text().splitlines():
            bits=line.split()
            if not bits:continue
            if bits[0]=='v':
                a,b,c=[float(bits[i+1])*o['scale'][i] for i in range(3)]
                b,c=b*math.cos(ax)-c*math.sin(ax),b*math.sin(ax)+c*math.cos(ax)
                a,c=a*math.cos(ay)+c*math.sin(ay),-a*math.sin(ay)+c*math.cos(ay)
                a,b=a*math.cos(az)-b*math.sin(az),a*math.sin(az)+b*math.cos(az)
                vs.append((a+x,b+y,c+z))
            elif bits[0]=='vt':uvs.append(tuple(float(t) for t in bits[1:3]))
            elif bits[0]=='usemtl':mat=bits[1]
            elif bits[0]=='f':
                ids=[t.split('/') for t in bits[1:]]
                def idx(i,n):return int(i)-1 if int(i)>0 else n+int(i)
                points=[vs[idx(t[0],len(vs))] for t in ids]
                uv=[uvs[idx(t[1],len(uvs))] if len(t)>1 and t[1] else (0,0) for t in ids]
                mesh.face(points,mat,uv)
    objects[:]=kept
    # The ivy shares the district's library, including its alpha-cutout map.
    with (RES/'aster.mtl').open('a',encoding='utf-8') as f:f.write('\n'+(RES/'ivy.mtl').read_text())
    for name,mesh in groups.items():
        mesh.save(name)
        model(name,name,(0,0,0),collision='none' if name=='district-arrival' else 'mesh',meshLod=0)
    for o in objects:
        for section,keyname in [('portal','objects'),('mirror','objects'),('camera','feedObjects')]:
            if section in o:
                targets=o[section].get(keyname,[])
                o[section][keyname]=list(dict.fromkeys(remap.get(t,t) for t in targets))
    return len(remap)


def author():
    p=json.loads((HERE/'showcase.tyra').read_text(encoding='utf-8-sig'))
    s=p['settings']
    s.update(showFps=False,showMemory=False,showProfiler=False,buildProfile='debug',remotePad=True,liveDebug=True,
             liveLink=False,liveLogic=False,timeMachine=False,
             unitsPerMeter=1,walkSpeed=.10,sprintMultiplier=1.7,terrainDetail=32,terrainViewDistance=0,textureQuant='8bit',
             meshLodDistance=14,staticBatching=True,skyColor=[.88,.65,.43],skyTopColor=[.12,.32,.46],skyDome=True,
             zenithSize=.65,lightDir=[-.6,.7,.35],lightColor=[1,.83,.60],ambient=.44,diffuse=.7,brightness=1,
             fogEnabled=True,fogColor=[.70,.70,.62],fogStart=55,fogEnd=135,aoEnabled=True,aoStrength=.55,aoRadius=2,terrainMaterial='res/aster/shadowstone.mtl',
             giEnabled=True,giRays=64,giBounces=2,giSkyLight=.8,giSunLight=1.1,giAmbientFloor=.08,giProbes=True,
             giProbeSpacing=6,giProbeHeight=3,giProbeLevels=3,modelAo=False,bloom=.18,bloomThreshold=.78,bloomSpread=.25,
             grain=0,dofAmount=0,flare=.08,godRays=0,highlightDistance=3,highlightColor=[.5,.95,1],highlightOpacity=.12,
             blssEnabled=False,loadingScreen=True)
    if '--profile' in sys.argv:
        s.update(showFps=True,showMemory=True,showProfiler=True)
    p.update(name='showcase',projectId='106bc4d00dad4897',template='fpp',ambience=[],defaultAmbience=-1,
             gradings=[],defaultGrading=-1,music=['res/aster/tides.wav'],sounds=['res/sfx/lens.wav'],hud=[],hudTexts=[
                 {'name':'title','text':'A S T E R','pos':[.5,.18],'size':32,'color':[1,.91,.70],'shadow':True,'visibleAtStart':True},
                 {'name':'subtitle','text':'THE TIDE OBSERVATORY','pos':[.5,.26],'size':11,'color':[.78,.92,.92],'shadow':True,'visibleAtStart':True}],sequences=[],layouts=[],activeLayout=0,
             loadingScreens=[],defaultLoadingScreen=-1,saveValues=[{'name':'lenses','default':0},{'name':'aligned','default':0}],saveTitle='ASTER / The Tide Observatory')
    p['usePromptIsText']=True
    p['usePromptText']={'text':'{{use}} INTERACT','size':14,'color':[.85,.97,1],'shadow':True}
    p['pickPromptIsText']=True
    p['pickPromptText']={'text':'{{use}} PICK UP','size':14,'color':[.85,.97,1],'shadow':True}
    p['menus']=[{'name':'pause','title':'ASTER','pauseMenu':True,'accent':[.43,.83,.78],'entries':[
        {'label':'Return to garden','action':'close'},{'label':'Save expedition','action':'save-menu'}]},
        {'name':'save','title':'SAVE EXPEDITION','saveMenu':True,'entries':[]}]
    player=obj('visitor','player',(0,.1,24),rot=(0,180,0),player={'mode':'walk','walkSpeed':.10,'lookSpeed':1,'eyeHeight':1.8,'jumpSpeed':4.5,'canJump':True,'flashlight':{'enabled':False,'toggle':'Circle','color':[.8,.88,1],'range':16,'angle':22}})
    player['flowGraph']=graph([[('OnStart','',[]),('PlaySequence','Arrival',[]),('PlayMusic','res/aster/tides.wav',[45,1])],
                               [('OnSequenceEnd','',[]),('SetTextVisible','title',[]),('SetTextVisible','subtitle',[]),('Branch','',[])],
                               [('OnButton','Select',[]),('PlaySequence','The Grand Tour',[])]])
    player['flowGraph']['nodes'] += [
        {'id':20,'type':'ValueAtLeast','pos':[250,330],'str':'aligned','num':[1,0,0,0]},
        {'id':21,'type':'DisplayText','pos':[1000,150],'str':'','str2':'THE OBSERVATORY AWAKENS / Thank you for exploring.','num':[.5,.9,13,7]},
        {'id':22,'type':'DisplayText','pos':[1000,300],'str':'','str2':'Find three brass lenses. SELECT: guided tour.','num':[.5,.9,13,7]}]
    branch=next(n['id'] for n in player['flowGraph']['nodes'] if n['type']=='Branch')
    player['flowGraph']['links'] += [{'id':1020,'from':20,'to':branch,'bool':True},
        {'id':1021,'from':branch,'to':21},{'id':1022,'from':branch,'to':22,'fpin':1}]
    title_ids={n['id'] for n in player['flowGraph']['nodes'] if n['type']=='SetTextVisible'}
    for e in player['flowGraph']['links']:
        if e['to'] in title_ids:e['pin']=1
    # Platforms close precisely on the canal edge; all top faces at Y=0.
    box('arrival-terrace',(0,-2.5,23),(34,5,12),'paving')
    box('west-promenade',(-10,-2.5,0),(12,5,34),'paving')
    box('east-promenade',(10,-2.5,0),(12,5,34),'paving')
    box('observatory-terrace',(0,-2.5,-23),(34,5,12),'paving')
    box('crossing',(0,-.35,0),(8,.7,4),'ivory')
    model('arrival-compass','compass-rose',(0,0,23),collision='none',castShadow=False)
    for z in (-17,17):box('canal-end-'+str(z),(0,-.45,z),(8,.9,1),'ivory')
    for x in (-4.18,4.18):
        for z in (-9,9):box(f'canal-curb-{x}-{z}',(x,.14,z),(.36,.28,14),'ivory')
    model('tidal-channel','canal-water',(0,-.55,0),collision='none',castShadow=False,bakedLighting=False)
    box('ocean',(0,-3.5,0),(600,.2,600),'water',collision='none',castShadow=False,bakedLighting=False)
    # Repeated 8.4 m bays share spring height, plinth height and coping line.
    for side in (-1,1):
        for j,z in enumerate((-12.6,-4.2,4.2,12.6)):
            model(f'{side}-arcade-{j}','arcade',(side*16,0,z),rot=(0,90,0),collision='mesh',meshLod=25)
            model(f'{side}-ivy-{j}','ivy',(side*15.35,3.8,z+2.9),(1.3,1.3,1.3),rot=(0,-side*90,0),collision='none',castShadow=False)
            model(f'{side}-pennant-{j}','pennant',(side*15.3,7.6,z),rot=(0,-side*90,0),collision='none')
        for z in (-15,-5,5,15):
            model(f'{side}-cypress-{z}','cypress',(side*12.8,1.1,z),collision='none',drawDistance=75)
            model(f'{side}-tree-pot-{z}','planter',(side*12.8,0,z))
        for z in (-10,10):
            model(f'{side}-flowers-{z}','flowers',(side*6.3,1.1,z),collision='none')
            model(f'{side}-flower-pot-{z}','planter',(side*6.3,0,z))
        for z in (-17,0,17):
            model(f'{side}-lantern-{z}','lantern',(side*5.4,0,z))
            light(f'{side}-warm-pool-{z}',(side*5.4,3.2,z))
        # Inlaid directional strips are thin solids, never coplanar decals.
        for z in (-9,9):box(f'{side}-inlay-{z}',(side*8,.012,z),(.12,.024,14),'gold',collision='none')
        box(f'{side}-outer-balustrade',(side*16.3,.75,0),(.45,1.5,33),'shadowstone')
        box(f'{side}-coping',(side*16.3,1.55,0),(.7,.15,33),'ivory')
    # The observatory silhouette: open rotunda, green copper dome, narrow ribs.
    for i in range(12):
        a=i*TAU/12
        model(f'rotunda-column-{i}','column',(5*math.cos(a),0,-23+5*math.sin(a)))
    model('observatory-dome','dome',(0,4.95,-23),collision='none')
    for side in (-1,1):
        # The near towers frame the opening shot and ground the broad terraces.
        box(f'entry-pier-{side}',(side*15,3,25),(3,6,3),'limestone')
        box(f'entry-crown-{side}',(side*15,6.1,25),(3.5,.3,3.5),'ivory')
        model(f'entry-finial-{side}','orbit-inner',(side*15,7.5,25),(.45,.45,.45),collision='none')
        model(f'entry-drape-{side}','ivy',(side*15,2.8,26.52),(1.2,1.2,1.2),collision='none')
    model('surveyors-cart','survey-cart',(-9,0,24),rot=(0,90,0))
    for i in range(3):model(f'survey-crate-{i}','supply-crate',(-10+i*.9,.02,26),( .75,.75,.75),rot=(0,i*8,0))
    model('planetarium-plinth','orrery-base',(0,0,-12))
    model('meridian','orbit-outer',(0,4.1,-12),rot=(12,0,18),collision='none',bakedLighting=False,flowGraph=spin(y=9))
    model('ecliptic','orbit-inner',(0,4.1,-12),rot=(60,20,0),collision='none',bakedLighting=False,flowGraph=spin(y=-14))
    model('equator','orbit-equator',(0,4.1,-12),collision='none',bakedLighting=False,flowGraph=spin(y=18))
    obj('heart-of-aster','sphere',(0,4.1,-12),(1.7,1.7,1.7),detail=16,material='res/aster/glow.mtl',collision='none',bakedLighting=False)
    light('heart-light',(0,4.1,-12),(.25,.8,.9),8,True)
    obj('orbit-dust','emitter',(0,3.3,-12),(3,2,3),color=[.4,.85,1],emitter={'kind':'fireflies','count':24,'size':.07,'enabled':True})
    # Functional optical benches: the controls form a short discovery loop.
    for i,(x,z) in enumerate(((-8,17),(8,1),(-8,-17))):
        model(f'optical-bench-{i}','pedestal',(x,0,z))
        o=obj(f'lens-{i+1}','sphere',(x,2.22,z),(.48,.48,.48),detail=12,usable=True,saveState=True,material='res/aster/gold.mtl',collision='none')
        o['flowGraph']=graph([[('OnUsed','',[]),('DoOnce','',[]),('AddValue','lenses',[1]),('DisplayText','',[.5,.85,14,4],f'LENS {i+1} RECOVERED / Align the central instrument.'),('SetObjectVisible','',[])]] )
        o['flowGraph']['links'][-1]['pin']=1
        # Sound is a sibling of the pickup action, not a delayed timer.
        o['flowGraph']['nodes'].append({'id':30,'type':'PlaySound','pos':[500,150],'str':'res/sfx/lens.wav','num':[65,1,0,0]})
        o['flowGraph']['links'].append({'id':1030,'from':2,'to':30})
    activation=graph([[('OnUsed','',[]),('Branch','',[]),('PlaySequence','Celestial Alignment',[]),('SetValue','aligned',[1])]])
    activation['nodes'] += [{'id':20,'type':'ValueAtLeast','pos':[0,180],'str':'lenses','num':[3,0,0,0]},
                            {'id':21,'type':'DisplayText','pos':[500,180],'str':'','str2':'Recover all three brass lenses to align the instrument.','num':[.5,.85,15,4]}]
    activation['links'] += [{'id':1020,'from':20,'to':2,'bool':True},{'id':1021,'from':2,'to':21,'fpin':1}]
    model('instrument-console','pedestal',(0,0,-6),usable=True,flowGraph=activation)
    model('arrival-guide','pedestal',(3.2,0,23),usable=True,flowGraph=use_text('Collect three brass lenses. SELECT: guided tour.'))
    obj('expedition-checkpoint','save-point',(-3.2,.55,23),(.6,.6,.6),color=[.2,.8,.85],usable=True)
    # A quiet physics nook, with containing walls and three throwable weights.
    for i in range(3):
        obj(f'calibration-weight-{i}','sphere',(8+i*1.1,.5,22),(.4,.4,.4),detail=10,material='res/aster/gold.mtl',physics=True,pickable=True,pickThrow=True,physMass=1,physBounce=.6,physFriction=.5)
    box('weights-backstop',(9,1,25),(6,2,.4),'shadowstone')
    model('physics-guide','pedestal',(12,0,21),usable=True,flowGraph=use_text('CALIBRATION / Square: pick up. Circle: throw.'))
    # The lens vault sits inside the rotunda: two deliberate, bounded second views.
    box('optics-backwall',(0,2.3,-27.5),(8,4.6,.55),'shadowstone')
    box('optics-cornice',(0,4.65,-27.5),(8.5,.25,.8),'gold')
    obj('silver-lens','mirror',(-2,2.5,-27.18),(2.6,2.8,1),drawDistance=15,color=[.7,.9,.9],mirror={'opacity':.15,'raytraced':True,'rtSize':32,'reflectPlayer':False,'objects':['heart-of-aster','lens-standard-a','lens-standard-b','lens-standard-c']})
    for i in range(3):
        obj('lens-standard-'+chr(97+i),'sphere',(-3+i*.8,1,-25.3),(.55,.55,.55),color=[[.2,.8,.8],[1,.65,.2],[.7,.3,.4]][i],detail=10,material='res/aster/gold.mtl',collision='none')
    box('monitor-frame',(2,2.5,-27.10),(3.1,2.35,.25),'gold')
    obj('optical-monitor','box',(2,2.5,-26.95),(2.85,2.05,.08),textureFeed='camera:orrery-camera',collision='none')
    obj('orrery-camera','camera',(5,6,-6),rot=(17,220,0),camera={'fov':58,'feed':True,'feedTerrain':False,'feedObjects':['heart-of-aster','meridian','ecliptic','equator']})
    model('optics-guide','pedestal',(0,0,-25.5),usable=True,flowGraph=use_text('OPTICS / Silver traces light. Gold watches the sky.'))
    light('vault-lamp', (0,3.8,-25),(.25,.75,1),6,True)
    obj('rotunda-acoustics','area',(0,2.5,-23),(10,5,10),reverb={'preset':5,'amount':.4,'priority':1})
    for i,z in enumerate((-15,-5,8)):
        obj('keeper-route-'+str(i+1),'empty',(10,2.2,z))
    obj('keeper','model',(10,2.2,-15),(1.5,1.5,1.5),model='res/aster/keeper.glb',collision='none',
        usable=True,projShadow=True,anim={'clip':'Idle','autoplay':True,'loop':True,'speed':.8},
        flowGraph=graph([[('OnStart','',[]),('PatrolWaypoints','keeper-route-',[1.1,2,0])],
                         [('OnUsed','',[]),('DisplayText','',[.5,.84,13,5],'Lenses: the entrance, crossing and west garden.')]]))
    # A spatial shortcut between opposite covered galleries, with framed views.
    for name,x,z,yaw,target in [('west-gate',-12,-23,0,'east-gate'),('east-gate',12,23,180,'west-gate')]:
        for dx in (-1.7,1.7):box(name+str(dx),(x+dx,2.2,z),(.45,4.4,.6),'ivory')
        box(name+'lintel',(x,4.4,z),(3.85,.45,.65),'gold')
        obj(name,'portal',(x,2.2,z),(2.9,4,1),rot=(0,yaw,0),collision='none',color=[.35,.8,.8],portal={'target':target,'objects':['arrival-terrace','observatory-terrace','west-promenade','east-promenade','planetarium-plinth','meridian','ecliptic','equator','heart-of-aster','observatory-dome','entry-pier--1','entry-pier-1','optics-backwall'],'viewAll':False,'showTerrain':False})
    # Horizon is deliberately staged: silhouettes, clear gaps, one beacon.
    for i,(x,z,h) in enumerate(((-42,-45,1.3),(36,-62,1.7),(-62,-5,.8),(55,-24,.7),(-24,-80,1.0))):
        model(f'sea-stack-{i}','island',(x,-9,z),(2,h,2),collision='none',drawDistance=150)
    model('distant-lighthouse','lighthouse',(-42,7.9,-45),(.8,.8,.8),collision='none')
    # Plaques are authored meshes with an atlas; readable in the close views.
    p['sequences']=[
        sequence('Arrival',[key(0,[21,13,32],[0,3,-9]),key(6,[12,7,18],[0,3,-14]),key(10,[0,2.5,24],[0,3,-12])]),
        sequence('The Grand Tour',[key(0,[0,3,23],[0,4,-12]),key(5,[-11,3,12],[-11,4,-12]),key(10,[-6,3,-3],[0,4,-12]),key(16,[10,7,-16],[0,5,-23]),key(22,[24,15,28],[0,3,-9])]),
        sequence('Celestial Alignment',[key(0,[0,2,-5],[0,4,-12],50),key(4,[5,5,-9],[0,4,-12],48),key(8,[0,8,-5],[0,4,-14],58)])]
    p['sequences'][2]['tracks']=[{'target':'heart-of-aster','animScale':True,'animColor':True,'animPos':False,'animRot':False,'animVis':False,
        'keys':[{'t':t,'pos':[0,4.1,-12],'rot':[0,0,0],'scale':[size]*3,'color':color,'vis':True,'ease':1}
                for t,size,color in [(0,1.7,[1,1,1]),(4,2.6,[1,.95,.7]),(8,2.1,[.8,1,1])]]}]
    batched=batch_architecture()
    p['scenes']=[{'name':'Aster','terrain':{'width':64,'depth':64},'layers':[],'objects':[o['id'] for o in objects]}]
    # Hidden support terrain gives A* a real walkable surface. It follows the
    # architecture, sits below paving, and drops below the sea at the edges.
    heights=[]
    for iz in range(33):
        row=[]
        for ix in range(33):
            x,z=-32+ix*2,-32+iz*2
            h=-.12 if abs(x)<=16 and abs(z)<=28 else -8
            if abs(x)<4 and abs(z)<17 and abs(z)>2:h=-1.5
            row.append(str(h))
        heights.append(' '.join(row))
    write(HERE/'terrain-Aster.heights','33 33\n'+'\n'.join(heights)+'\n')
    p['editor']={'selectedObject':-1,'gizmo':0,'viewMode':0,'showFog':True,'cam':[.55,.35,65,0,2,-5]}
    old_ids={f.stem for f in OBJECTS.glob('*.json')}
    for o in objects:write(OBJECTS/(o['id']+'.json'),json.dumps(o,ensure_ascii=False,indent=2)+'\n')
    # Remove only objects explicitly referenced by the old, replaced showcase.
    active={o['id'] for o in objects}
    for obsolete in old_ids-active:(OBJECTS/(obsolete+'.json')).unlink()
    write(HERE/'showcase.tyra',json.dumps(p,ensure_ascii=False,indent=2)+'\n')
    print(f'ASTER: {len(objects)} runtime objects; {batched} static pieces in district meshes; {len(p["sequences"])} camera sequences')


if __name__=='__main__':
    materials()
    soundscape()
    geometry()
    author()

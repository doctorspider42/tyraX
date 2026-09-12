"""Rebuild the CC0 Motor District scene and textures (Python 3 + Pillow).

Run from any directory. Inputs live in res/models/urban; no external asset
folder, network service or Blender installation is needed to rebuild the map.
The separately prepared GGBot vehicle is already shipped as a GLB.
"""
from pathlib import Path
import hashlib
import json
import math
import random
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
URBAN = ROOT / 'res/models/urban'
TEX = ROOT / 'res/textures'
MAT = ROOT / 'res/materials'
MAT.mkdir(exist_ok=True)
manifest = ROOT / 'vehicle-playground.tyra'
p = json.loads(manifest.read_text())
scene = p['scenes'][0]


def write(path, data):
    path.write_text(data, encoding='utf-8', newline='\n')


def material(name, texture, scale=1):
    write(MAT / (name + '.mtl'), f'newmtl {name}\nKd 1 1 1\n'
          f'map_Kd -s {scale} {scale} 1 ../textures/{texture}\n')


# Small shared textures: keep the GS texture cache useful during driving.
rng = random.Random(4096)
asphalt = Image.new('RGB', (128, 128))
asphalt.putdata([(v, v + 2, v + 5) for v in
                 (rng.randrange(37, 53) for _ in range(128 * 128))])
asphalt.save(TEX / 'district-asphalt.png')
road = asphalt.copy()
d = ImageDraw.Draw(road)
d.rectangle((5, 0, 7, 127), fill=(216, 214, 190))
d.rectangle((120, 0, 122, 127), fill=(216, 214, 190))
d.rectangle((62, 20, 65, 91), fill=(227, 177, 68))
road.save(TEX / 'district-road.png')
ground = Image.new('RGB', (64, 64))
ground.putdata([(v + 8, v + 9, v) for v in
                (rng.randrange(66, 82) for _ in range(64 * 64))])
ground.save(TEX / 'district-ground.png')
material('district-ground', 'district-ground.png', .5)
material('district-asphalt', 'district-asphalt.png', .5)

# A readable garage sign, authored here, with no external font dependency.
sign = Image.new('RGB', (256, 128), '#122831')
d = ImageDraw.Draw(sign)
d.rectangle((0, 0, 255, 6), fill='#f4b246')
d.rectangle((0, 121, 255, 127), fill='#53cecf')
d.text((128, 27), 'MOTOR', font=ImageFont.load_default(size=34),
       fill='#f4b246', anchor='mt')
d.text((128, 70), 'DISTRICT  /  04', font=ImageFont.load_default(size=21),
       fill='#e2f0e8', anchor='mt')
sign.save(TEX / 'district-sign.png')
material('district-sign', 'district-sign.png')


class Mesh:
    """Join authored Kenney modules without adding material draw calls."""
    def __init__(self):
        self.lines = ['# Kitbash of Kenney Retro Urban Kit, CC0',
                      'mtllib district-buildings.mtl']
        self.count = [0, 0, 0]
        self.materials = set()

    def add(self, name, pos, scale):
        offsets = self.count.copy()
        for line in (URBAN / (name + '.obj')).read_text().splitlines():
            fields = line.split()
            if not fields:
                continue
            kind = fields[0]
            if kind == 'v':
                v = [float(x) * s + t for x, s, t in
                     zip(fields[1:4], scale, pos)]
                self.lines.append('v ' + ' '.join(f'{x:.6f}' for x in v))
                self.count[0] += 1
            elif kind == 'vt':
                self.lines.append(line)
                self.count[1] += 1
            elif kind == 'vn':
                n = [float(x) / s for x, s in zip(fields[1:4], scale)]
                length = math.sqrt(sum(x*x for x in n)) or 1
                self.lines.append('vn ' + ' '.join(f'{x/length:.6f}' for x in n))
                self.count[2] += 1
            elif kind == 'usemtl':
                self.materials.add(fields[1])
                self.lines.append(line)
            elif kind == 'f':
                corners = []
                for field in fields[1:]:
                    corners.append('/'.join(str(int(x) + offsets[i]) if x else ''
                                            for i, x in enumerate(field.split('/'))))
                self.lines.append('f ' + ' '.join(corners))

    def save(self, name):
        write(URBAN / (name + '.obj'), '\n'.join(self.lines) + '\n')


materials = {}
for name in ('wall-a-window', 'wall-a-garage', 'wall-a-roof'):
    text = (URBAN / (name + '.mtl')).read_text()
    for block in text.split('newmtl ')[1:]:
        materials[block.splitlines()[0]] = 'newmtl ' + block
write(URBAN / 'district-buildings.mtl', '\n'.join(materials.values()))
for name, floors, nx, nz in [('workshop', 1, 3, 2), ('loft', 3, 2, 2),
                              ('tower', 5, 2, 2)]:
    mesh = Mesh()
    for x in range(nx):
        for z in range(nz):
            for y in range(floors):
                mesh.add('wall-a-garage' if name == 'workshop' else 'wall-a-window',
                         ((x-(nx-1)/2)*4, y*3.2, (z-(nz-1)/2)*4), (4, 3.2, 4))
            mesh.add('wall-a-roof', ((x-(nx-1)/2)*4, floors*3.2,
                                   (z-(nz-1)/2)*4), (4, 2, 4))
    mesh.save('district-' + name)


# The city floor stays flat; the outer east course has gentle, continuous
# crests. Road vertices and wheels both sample this same 81x81 heightfield.
def height(x, z):
    east = max(0, min(1, (x-62)/35))
    return east * (3.8 * math.sin((z+50)/22)**2 +
                   1.8 * math.sin((x-70)/24)**2)

write(ROOT / 'terrain-main.heights', '81 81\n' + '\n'.join(
    ' '.join(f'{height(-160+x*4, -160+z*4):.5f}' for x in range(81))
    for z in range(81)) + '\n')

old = [json.loads((ROOT/'objects'/f'{i}.json').read_text()) for i in scene['objects']]
# Only replace this generator's previous objects and the old obstacle course.
keep = [o for o in old if not o['id'].startswith('district')
        and o['type'] != 'road' and not o['name'].startswith('pillar')]
objects = keep


def add(name, kind, pos, scale=(1, 1, 1), **kw):
    obj = dict(id='district' + hashlib.sha1(name.encode()).hexdigest()[:8],
               name=name, type=kind, position=list(pos), rotation=[0, 0, 0],
               scale=list(scale), color=[1, 1, 1], physics=False)
    obj.update(kw)
    objects.append(obj)
    return obj


def model(name, asset, x, z, scale=1, reflected=False, yaw=0):
    return add(name, 'model', (x, height(x, z), z), (scale,)*3,
               model=f'res/models/urban/{asset}.obj', rotation=[0, yaw, 0],
               drawDistance=145, reflected=reflected, castShadow=False)


def road(name, points, width=11):
    add(name, 'road', (0, 0, 0), roadPoints=[v for pair in points for v in pair],
        roadWidth=width, roadTexture='res/textures/district-road.png')


road('Ring road', [(-96,-100),(0,-108),(96,-100),(120,-64),(122,44),
                   (94,103),(0,112),(-94,103),(-122,64),(-122,-64),(-96,-100)], 13)
road('Garage boulevard', [(0,-108),(0,-50),(0,0),(0,55),(0,112)], 13)
road('Market cross street', [(-122,0),(-65,0),(0,0),(62,0),(122,0)], 11)
road('Foundry link', [(-115,-70),(-60,-58),(0,-55),(60,-58),(117,-70)], 10)
road('Skyline avenue', [(-119,65),(-60,58),(0,58),(58,58),(116,65)], 10)
road('East crest run', [(62,-58),(80,-30),(70,0),(88,30),(62,58)], 9)
road('West service lane', [(-60,-58),(-65,-25),(-65,0),(-60,58)], 9)

# Pads mask centre-line overlaps at the flat crossroads and make drift space.
for x, z, w, depth in [(0,0,16,16),(0,-55,16,16),(0,58,16,16),
                       (-65,0,14,14),(-60,-58,15,14),(-60,58,15,14),
                       (28,-27,44,36),(-87,30,32,30)]:
    add(f'Asphalt apron {x} {z}', 'box', (x,.04,z), (w,.2,depth),
        material='res/materials/district-asphalt.mtl', collision='none', castShadow=False)

buildings = [(-25,-27,'workshop'),(55,-27,'workshop'),
             (-17,13,'tower'),(17,13,'tower'),(48,34,'loft'),
             (-33,80,'tower'),(28,83,'loft'),(-88,78,'loft'),
             (-87,-32,'workshop'),(-87,-80,'loft'),
             (-35,-82,'loft'),(32,-82,'tower'),(65,-82,'loft'),
             (-92,27,'workshop')]
for i, (x,z,kind) in enumerate(buildings):
    model(f'{kind.title()} block {i+1:02}', 'district-'+kind, x,z,
          scale=1.5 if i in (2,3) else 1,
          reflected=i in (0,1,2,3,4,10,11))
    # A pavement plinth gives each block a deliberate footprint.
    add(f'Pavement {i+1:02}', 'box', (x,.02,z),
        (16 if kind=='workshop' or i in (2,3) else 12,.16,16 if i in (2,3) else 12), color=[.44,.46,.44],
        castShadow=False, collision='none')

for x,z in [(-9,-80),(9,-35),(-9,24),(9,83),(-45,-7),(40,7),
            (-75,7),(-45,65),(42,65),(-40,-65),(43,-65)]:
    model(f'Streetlight {x} {z}', 'detail-light-double', x,z,6, yaw=90)
for x,z in [(-9,-9),(9,9),(-9,-64),(9,67)]:
    model(f'Traffic signal {x} {z}', 'detail-light-traffic', x,z,5, yaw=90)
for x,z in [(-47,24),(-47,39),(-15,40),(14,40),(43,83),(49,92),
            (-72,87),(-104,86),(-104,13),(-74,46),(-42,-92),(46,-91),
            (16,-94),(96,78),(99,-84),(-106,-88)]:
    model(f'Park tree {x} {z}', 'tree-park-large', x,z,5)
for x,z in [(142,-95),(142,-55),(140,-12),(144,34),(139,79),(-140,92),
            (-141,38),(-141,-22),(-140,-93)]:
    model(f'Outer pine {x} {z}', 'tree-park-pine-large', x,z,7)
for x,z in [(16,-42),(24,-42),(32,-42),(40,-42),(-103,15),(-103,23)]:
    model(f'Drift barrier {x} {z}', 'detail-barrier-type-a', x,z,4)
for x,z in [(-33,-35),(60,-35),(-94,-39)]:
    model(f'Workshop dumpster {x} {z}', 'detail-dumpster-closed', x,z,2)
for x,z in [(-20,39),(20,41),(-73,38)]:
    model(f'Bench {x} {z}', 'detail-bench', x,z,2)
for x,z in [(47,-38),(49,-38),(51,-38)]:
    o = model(f'Loose pallet {x} {z}', 'pallet', x,z,1.7)
    o.update(physics=True, physMass=.4, physTumble=True)

# Sign faces the spawn, +Z side of the garage.
add('Motor District garage sign', 'box', (-25,4.3,-22.8), (10,2.6,.15),
    material='res/materials/district-sign.mtl', castShadow=False)

patrol = [(120,-64),(122,44),(94,103),(0,112),(-94,103),
          (-122,64),(-122,-64),(-96,-100),(0,-108),(96,-100)]
for o in keep:
    if o['name'].startswith('pillar'):
        continue
    if o['type'] == 'player':
        o['position'] = [-2.3,0,-18]
        o['rotation'] = [0,100,0]
    elif o['name'] == 'coupe':
        o['position'] = [0,.3525,-18]
        o['color'] = [1,1,1]  # preserve the source's gold paint; cyan made it green
    elif o['name'] == 'rival':
        o['position'] = [114,height(114,-80),-80]
        o['rotation'] = [0,25,0]
    elif o['name'] == 'rival-2':
        o['position'] = [106,height(106,-90),-90]
        o['rotation'] = [0,30,0]
        o['vehicle']['def'] = 'Rally 04'
        o['scale'] = [1,1,1]
    elif o['name'].startswith('circuit-'):
        i = ord(o['name'][-1])-ord('a')
        x,z = patrol[i]
        o['position'] = [x,height(x,z),z]
    elif o['name'].startswith('crate'):
        i = int(o['name'][-1])
        o['position'] = [18+i*2,1,-15]

for i,(x,z) in enumerate(patrol[4:],4):
    add('circuit-'+chr(ord('a')+i), 'area', (x,height(x,z),z), (5,5,5))

add('Rally 04 - test drive', 'vehicle', (8,.341,-22), shadowMode=3,
    vehicle={'def':'Rally 04','driveable':True})

add('Tristar Racer - test drive', 'vehicle', (-8,.31,-22), shadowMode=2,
    vehicle={'def':'Tristar Racer','driveable':True})

for v in p['vehicles']:
    v['bodyReflMap'] = ''  # actual sky + opted-in scene geometry
    v['bodyShine'] = .45 if v['name']=='CC96' else .35
    v['farDistance'] = 48
    # Longer lower gears and a restrained shift/body response.
    v['drive'].update(gearSpread=1.28, shiftTime=.10, gearTorque=.35,
                      shiftUpFrac=.95, shiftDownFrac=.48,
                      leanAmount=.35 if v['name']=='Rally 04' else .25)
    if v['name']=='Rally 04':
        v['drive'].update(wheelBase=2.733, track=1.562, wheelRadius=.341,
                          rideHeight=.341, topSpeed=26, accel=10, grip=22,
                          handbrakeGrip=5, suspensionTravel=.26, nosCapacity=4)
        v['headlights'] = False
    elif v['name']=='Tristar Racer':
        v['drive'].update(topSpeed=32, accel=12, grip=29, highSpeedSteerDeg=12,
                          nosCapacity=4, nosRefill=.18)
    else:
        v['drive'].update(topSpeed=29, accel=11, nosCapacity=4,
                          nosRefill=.18, highSpeedSteerDeg=14)

p['settings'].update(terrainMaterial='res/materials/district-ground.mtl',
    terrainViewDistance=150, envProbeReflected=False,
    skyColor=[.58,.64,.66], skyTopColor=[.12,.28,.42],
    lightDir=[-.55,.65,.35], ambient=.48, diffuse=.52,
    lightColor=[1,.87,.69], fogEnabled=True, fogColor=[.58,.64,.66],
    fogStart=100, fogEnd=165)
p['ambience'][0].update({k:v for k,v in p['settings'].items()
                        if k in p['ambience'][0]})
# Night dressing: eight live lights, inexpensive emissive window cards and neon.
for name, rgb in [('night-warm',(1,.60,.13)), ('night-cyan',(.15,.85,1)),
                  ('night-pink',(1,.2,.55))]:
    write(MAT/(name+'.mtl'), f'newmtl {name}\nKd {rgb[0]} {rgb[1]} {rgb[2]}\nKe {rgb[0]*1.9} {rgb[1]*1.9} {rgb[2]*1.9}\n')
night_objects = []
def night_box(name, pos, scale, mat):
    o = add('Night '+name, 'box', pos, scale, material='res/materials/'+mat+'.mtl',
            collision='none', castShadow=False, reflected=True)
    night_objects.append(o['id'])
for x in (-17,17):
    for y, dx in [(3.4,-3),(8.2,3),(13,-3),(17.8,3)]:
        night_box(f'window {x} {y}', (x+dx,y,6.97), (1.7,1.5,.035), 'night-warm')
for y in (3.0,5.6):
    night_box(f'garage trim {y}', (-25,y,-22.67), (10,.12,.08), 'night-cyan')
night_box('garage blade', (-31,4.3,-22.7), (.15,3.4,.15), 'night-pink')
model('Streetlight start', 'detail-light-double', -9,-14,6,yaw=90)
for i,(x,z) in enumerate([(9,-35),(-9,24),(-9,-80),(9,83),(-45,-7),
                         (40,7),(-9,-14),(-25,-22)]):
    add(f'District night lamp {i+1}', 'point-light', (x,5.7 if i<7 else 4.8,z),
        color=[.75,.52,.23] if i<7 else [.18,.65,.8],
        light={'brightness':1.15,'radius':16,'dynamic':True,'flicker':.025 if i==6 else 0,
               'spot':True,'spotAngle':48,'shadowVolumes':1,'beam':2})
# A persistent menu value selects a fixed hour. The script pins the existing
# runtime cycle; the renderer supplies its sky/fog/world-grade path.
night_value = next((i for i,v in enumerate(p['saveValues']) if v['name']=='district-night'),len(p['saveValues']))
if night_value == len(p['saveValues']):
    p['saveValues'].append({'name':'district-night','default':0})
p['menus'] = [m for m in p['menus'] if m['name']!='district-pause']
for m in p['menus']: m['pauseMenu'] = False
p['menus'].append({'name':'district-pause','title':'MOTOR DISTRICT','pauseMenu':True,
                  'pauseGame':True,'panelW':256,'screenPos':[.5,.45],
                  'accent':[.15,.85,1], 'entries':[
                      {'label':'RESUME','action':'close'},
                      {'label':'TIME OF DAY','action':'toggle','param':'district-night','options':['DAY','NIGHT']}]})
day = {'hour':12,'skyColor':[.58,.64,.66],'skyTopColor':[.12,.28,.42],
       'lightColor':[1,.87,.69],'ambient':.48,'diffuse':.52,'brightness':1,
       'fogColor':[.58,.64,.66],'stars':0}
night = {'hour':0,'skyColor':[.035,.045,.085],'skyTopColor':[.006,.012,.036],
         'lightColor':[.48,.60,.92],'ambient':.20,'diffuse':.30,'brightness':.85,
         'fogColor':[.035,.045,.085],'stars':1}
p['ambience'][0]['cycle']={'enabled':True,'time':12,'runtime':True,'dayLength':86400,
    'bakeHour':12,'runtimeGrade':True,'sunrise':6,'sunset':18,'sunAzimuth':75,'sunTilt':28,
    'moonAzimuth':105,'moonTilt':34,'moonOffset':12,'moonSize':4,'moonPhase':.65,
    'starsEnabled':True,'starCount':160,'starSeed':4096,'keys':[night,day]}
p['settings'].update(bloom=.12,bloomThreshold=.7,bloomSpread=.12)
indices = [i for i,o in enumerate(objects) if o['id'] in night_objects]
write(ROOT/'inc/scripts/district_data.hpp', '#pragma once\nnamespace Vehicle_playground {\n'
      + f'constexpr int DISTRICT_NIGHT_VALUE = {night_value};\n'
      + 'constexpr int DISTRICT_NIGHT_OBJECTS[] = {' + ','.join(map(str,indices)) + '};\n}\n')

p['editor'].update(selectedObject=1,cam=[.75,.65,145,0,0,0])
scene['objects'] = [o['id'] for o in objects]
for o in objects:
    write(ROOT/'objects'/f"{o['id']}.json",json.dumps(o,indent=2)+'\n')
write(manifest,json.dumps(p,indent=2)+'\n')
print(f'Motor District: {len(objects)} objects, 7 roads, 14 building blocks, 3 vehicle models')

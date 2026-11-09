"""make_stress.py SRC DST [PARTS] - a stress copy of a vehicle-playground project.

PARTS (comma list, default crates,props,buildings,pavements,cars), main scene
only: crates x3 (two more fields beside the first), decorative props x4
(scattered copies), buildings / pavements mirrored through the map centre,
four more parked cars by the scripted drive's route; `cratefield` first lays
~120 crates in rows along the spawn road (so `crates` then triples a field the
example project does not have); `instanced` draws props
and buildings instanced, `release` switches the build profile to release with
the on-screen profiler off. Deterministic (fixed seed) so the PS2 arms of any
change see the same scene. The 2026-11 "crates only" fixture is
`crates,pavements` (an operator-precedence slip mirrored the pavements
whatever the parts were)."""
import json, os, random, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
PARTS = set((sys.argv[3] if len(sys.argv) > 3 else 'crates,props,buildings,pavements,cars').split(','))
INSTANCED = 'instanced' in PARTS  # props + buildings drawn instanced (SceneObject::drawInstanced)
if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst, ignore=shutil.ignore_patterns('bin', 'obj', 'logs', 'recordings'))
os.makedirs(os.path.join(dst, 'bin'), exist_ok=True)
rng = random.Random(1104)
tyra = os.path.join(dst, 'vehicle-playground.tyra')
d = json.load(open(tyra, encoding='utf-8'))
main = [s for s in d['scenes'] if s['name'] == 'main'][0]
ids = set(os.path.splitext(f)[0] for f in os.listdir(os.path.join(dst, 'objects')))


def load(i):
    return json.load(open(os.path.join(dst, 'objects', i + '.json'), encoding='utf-8'))


def add(o):
    while True:
        nid = '%016x' % rng.getrandbits(64)
        if nid not in ids:
            break
    ids.add(nid)
    o['id'] = nid
    json.dump(o, open(os.path.join(dst, 'objects', nid + '.json'), 'w', encoding='utf-8'))
    main['objects'].append(nid)


objs = [load(i if isinstance(i, str) else i['id']) for i in list(main['objects'])]
added = {'cratefield': 0, 'crates': 0, 'props': 0, 'buildings': 0, 'cars': 0}
if 'cratefield' in PARTS:  # rows of three along the spawn road, ~120 crates (the shape of a hand-built crate run)
    tpl = [o for o in objs if o['type'] == 'box' and o.get('physics') and o['name'].startswith('crate')][:3]
    for r in range(40):
        for k, t in enumerate(tpl):
            c = json.loads(json.dumps(t))
            c['position'] = [0.8 + 2.0 * k, 0.5, -6.0 + 2.0 * r]
            c['name'] = '%s-field%d' % (t['name'], r)
            add(c)
            objs.append(c)
            added['cratefield'] += 1
PROPS = ('Park tree', 'Streetlight', 'Outer pine', 'Traffic signal', 'Drift barrier',
         'Workshop dumpster', 'Bench')
for o in objs:
    n = o['name']
    if 'crates' in PARTS and o['type'] == 'box' and o.get('physics') and n.startswith('crate'):
        for dx in (-14.0, 14.0):  # two more crate fields beside the first
            c = json.loads(json.dumps(o))
            c['position'][0] += dx
            c['name'] = n + '-stress%+d' % int(dx)
            add(c)
            added['crates'] += 1
    elif 'props' in PARTS and o['type'] == 'model' and n.startswith(PROPS):
        for k in range(3):  # x4 props, scattered within 40 units
            c = json.loads(json.dumps(o))
            c['position'][0] = max(-150.0, min(150.0, c['position'][0] + rng.uniform(-40, 40)))
            c['position'][2] = max(-150.0, min(150.0, c['position'][2] + rng.uniform(-40, 40)))
            c['name'] = n + '-stress%d' % k
            if INSTANCED:
                c['drawInstanced'] = True
            add(c)
            added['props'] += 1
    elif ('buildings' in PARTS and o['type'] == 'model' and 'block' in n) or \
            ('pavements' in PARTS and o['type'] == 'box' and n.startswith('Pavement')):
        c = json.loads(json.dumps(o))  # mirrored through the map centre
        c['position'][0] = -c['position'][0]
        c['position'][2] = -c['position'][2]
        c['rotation'][1] = c['rotation'][1] + 180.0
        c['name'] = n + '-stress'
        if INSTANCED and c['type'] == 'model':
            c['drawInstanced'] = True
        add(c)
        added['buildings'] += 1
cars = [o for o in objs if o['type'] == 'vehicle' and not o.get('flowGraph')]
spots = [(-9.0, 20.0, 0.0), (9.0, -40.0, 180.0), (-4.5, -110.0, 0.0), (9.0, -120.0, 90.0)]
for k, (x, z, yaw) in enumerate(spots if 'cars' in PARTS else []):
    c = json.loads(json.dumps(cars[k % len(cars)]))
    c['position'] = [x, 0.5, z]
    c['rotation'] = [0, yaw, 0]
    c['name'] = c['name'] + '-stress%d' % k
    c.get('vehicle', {})['driveable'] = False
    add(c)
    added['cars'] += 1
if INSTANCED:  # the originals too: every copy of a model shares one
    for i in main['objects']:
        p_ = os.path.join(dst, 'objects', i + '.json')
        o_ = json.load(open(p_, encoding='utf-8'))
        if o_['type'] == 'model' and (o_['name'].startswith(PROPS) or 'block' in o_['name']) and not o_.get('physics'):
            o_['drawInstanced'] = True
            json.dump(o_, open(p_, 'w', encoding='utf-8'))
if 'release' in PARTS:  # judge 60 FPS here: debug builds cost ~1.1-1.4 ms a frame more
    d['settings']['buildProfile'] = 'release'
    d['settings']['showProfiler'] = False
json.dump(d, open(tyra, 'w', encoding='utf-8'), indent=2)
print('stress project', dst, added, 'main scene objects', len(main['objects']))

"""Validate A08 correctness witnesses; deliberately do not report frame timings."""
import hashlib
import argparse
import json
import re
import struct
import sys
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixture',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
root=args.fixture
game_manifest=root/'paint-game-manifest.json'
integrated=game_manifest.is_file()
if integrated:
    manifest=json.loads(game_manifest.read_text())
    assert manifest['hour']==0 and manifest['phaseCount']==3
    assert hashlib.sha256((root/'production-paint-block.txt').read_bytes()).hexdigest()==manifest['productionBlockSha256']

def lod_inventory(path):
    # TmdlLoader::load / readMesh / readStrip, v1-v4 layout.
    data=path.read_bytes(); pos=0
    def take(n):
        nonlocal pos
        assert n>=0 and pos+n<=len(data)
        result=data[pos:pos+n]; pos+=n; return result
    def u32(): return struct.unpack('<I',take(4))[0]
    def mesh(empty=False):
        n=u32(); assert (n>0 or empty) and n%3==0 and n<=1<<21
        take(n*32); ao=u32(); assert ao in (0,n); take(ao)
    assert take(4)==b'TMDL'
    version=u32(); assert 1<=version<=4
    take(24); count=u32(); assert 0<count<=256
    parts=[]
    for _ in range(count):
        name=take(32).split(b'\0')[0].decode(); take(64)
        reflection=take(64).split(b'\0')[0].decode()
        take(12)
        if version>=2: take(12)
        strength=struct.unpack('<f',take(4))[0]; take(4)
        mesh(); lods=u32(); assert lods<=4
        for _ in range(lods): mesh()
        if version>=4:
            take(4); mesh(True)
            for _ in range(lods): mesh(True)
        if reflection and strength>0: parts.append(dict(name=name,farTiers=lods))
    if version>=3:
        n=u32(); assert n%3==0 and n<=1<<21; take(n*12)
    assert pos==len(data)
    return dict(file=path.name,sha256=hashlib.sha256(data).hexdigest(),reflectiveParts=parts)

authored_path=root/'authored-manifest.json'
authored=json.loads(authored_path.read_text(encoding='utf-8-sig'))
vehicle_ids=[v['id'] for v in authored['vehicles']]
assert len(vehicle_ids)==3 and len(set(vehicle_ids))==3
assets=[lod_inventory(root/'game/bin/vehicles'/f'veh-{i}-body.tmdl') for i in sorted(vehicle_ids)]
assert assets and any(a['reflectiveParts'] for a in assets)
max_lod=max(p['farTiers'] for a in assets for p in a['reflectiveParts'])
assert max_lod>=1
expected_lods=(1<<(min(max_lod,2)+1))-1
binding=json.loads((root/'native-binding.json').read_text(encoding='utf-8-sig'))
launch=json.loads((root/'emulator/launch.json').read_text(encoding='utf-8-sig'))
elf=hashlib.sha256((root/'game/bin/vehicle-playground.elf').read_bytes()).hexdigest()
assert elf==binding['elfSha256']==launch['elfSha256']
for name,digest in binding['files'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
logpath=root/'emulator/emulator.log'
log=logpath.read_text(errors='replace')
pattern=r'PAINTLIFE phase=(\d+) visits=(\d+) accepts=(\d+) refreshes=(\d+) held=(\d+) modes=(\d+) lods=(\d+) legacy=(\d+) custom=(\d+) errors=(\d+)'
def validate(text):
    assert 'DRIVEDONE valid=1' in text,'Incomplete target run'
    rows=[tuple(map(int,r)) for r in re.findall(pattern,text)]
    assert len(rows)==3 and [r[0] for r in rows]==[0,1,2]
    for phase,visits,accepts,refreshes,held,modes,lods,legacy,custom,errors in rows:
        assert errors==0 and visits>0 and accepts>0 and refreshes>0 and held>0
        assert modes==63 and lods==expected_lods and legacy>0 and custom>0
        assert held<=refreshes and refreshes<=visits and accepts<=visits
        assert re.search(rf'DRIVEPHASE phase={phase} .*valid=1 rows=840 ',text)
        hour='0' if integrated else '12'
        witness=re.search(rf'NIGHTROADS phase={phase} hour={hour}\.000000 paused=1 chunks=(\d+) lights=(\d+)',text)
        assert witness
        assert int(witness[1])>0
        if integrated: assert int(witness[2])>0
    return rows
rows=validate(log)
mutations={
    'missing-completion':log.replace('DRIVEDONE valid=1','DRIVEDONE valid=0'),
    'lost-held-transition':re.sub(r'held=\d+','held=0',log,count=1),
    'missing-legacy':re.sub(r'legacy=\d+','legacy=0',log,count=1),
    'missing-override':re.sub(r'custom=\d+','custom=0',log,count=1),
    'oracle-error':re.sub(r'errors=0','errors=1',log,count=1),
    'missing-lod':re.sub(r'lods=\d+','lods=1',log,count=1)}
if integrated:
    mutations['wrong-hour']=log.replace('hour=0.000000','hour=12.000000',1)
    mutations['missing-lights']=re.sub(r'lights=\d+','lights=0',log,count=1)
    mutations['missing-roads']=re.sub(r'chunks=\d+ lights=', 'chunks=0 lights=',log,count=1)
for name,text in mutations.items():
    try: validate(text)
    except AssertionError: pass
    else: raise AssertionError('Negative control escaped: '+name)
names='phase visits accepts refreshes held modes lods legacy custom errors'.split()
result=dict(scope=('A11 integrated night-road' if integrated else 'A08 private')+' emulator lifecycle and scalar colour oracle, NOT performance or full raster',
            elfSha256=elf,nativeSourceFiles=len(binding['files']),
            logSha256=hashlib.sha256(logpath.read_bytes()).hexdigest(),
            lodAssets=assets,expectedLodMask=expected_lods,
            authoredSha256=hashlib.sha256(authored_path.read_bytes()).hexdigest(),
            phases=[dict(zip(names,r)) for r in rows],negativeControls=list(mutations))
(args.output or root/'paint-lifecycle-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))

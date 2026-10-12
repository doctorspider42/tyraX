"""Read actual A44 complete-cell activation and exact held main/env image pairs."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

def witness(log, phase):
    complete=re.findall(r'A44GPU phase=(\d+) arms=3 target=(\d+) complete=1',log)
    assert sum(int(p)==phase for p,_ in complete)==1, 'Missing/duplicate GPU completion'
    end=re.search(rf'A44GPU phase={phase} arms=3 target=\d+ complete=1',log).end()
    log=log[:end]
    rows=re.findall(r'A44ARM phase=(\d+) arm=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+)',log)
    rows=[tuple(map(int,r)) for r in rows if int(r[0])==phase]
    assert [r[1] for r in rows]==[0,1,2], 'Arm order/completeness'
    assert rows[0][2:]==rows[2][2:]==(0,0,0), 'Control used candidate'
    assert rows[1][2]>0 and rows[1][2]==rows[1][3] and rows[1][4]==0, 'No full activation'
    builds=re.findall(r'A44TERRAIN compile=PASS cells=(\d+) passes=(\d+) textures=(\d+) words=(\d+) heap=(\d+)',log)
    assert builds, 'No complete producer witness'
    cells,passes,textures,words,heap=map(int,builds[-1])
    assert cells>0 and passes>=cells and 0<textures<=64 and 0<words<=heap*3//4
    return dict(requested=rows[1][2],submitted=rows[1][3],fallback=0,
                cells=cells,passes=passes,textures=textures,words=words,heap=heap)

def image(path,stage,kind):
    raw=path.read_bytes()
    assert len(raw)>=16
    width,height,psm,number=struct.unpack_from('<4I',raw)
    assert number==stage and psm in (0,2)
    assert (width,height)==((128,128) if kind=='env' else (448,448))
    stride=4 if psm==0 else 2
    assert len(raw)==16+width*height*stride
    assert len(set(struct.unpack('<' + ('I' if stride==4 else 'H')*(width*height),
                                 raw[16:])))>16, 'Blank/constant image'
    return (width,height,psm),raw[16:]

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--phase',type=int,default=0)
a=p.parse_args();root=a.fixture.resolve()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
launch=json.loads((root/'emulator/launch.json').read_text())
assert launch['elfSha256']==binding['elfSha256']
logpath=root/'emulator/emulator.log';log=logpath.read_text(errors='replace')
data=witness(log,a.phase)
positive=(f'A44TERRAIN compile=PASS cells=25 passes=43 textures=19 words=39104 heap=501760\n'
          f'A44ARM phase={a.phase} arm=0 requested=0 submitted=0 fallback=0\n'
          f'A44ARM phase={a.phase} arm=1 requested=3 submitted=3 fallback=0\n'
          f'A44ARM phase={a.phase} arm=2 requested=0 submitted=0 fallback=0\n'
          f'A44GPU phase={a.phase} arms=3 target=0 complete=1')
assert witness(positive,a.phase)['submitted']==3
controls={'missing':'','duplicate':positive+'\n'+positive,
          'fallback':positive.replace('submitted=3 fallback=0','submitted=3 fallback=1'),
          'noActivation':positive.replace('requested=3 submitted=3','requested=0 submitted=0'),
          'partialActivation':positive.replace('submitted=3','submitted=2'),
          'controlActivation':positive.replace('arm=0 requested=0','arm=0 requested=1'),
          'missingProducer':positive.replace('compile=PASS','compile=FAIL'),
          'budget':positive.replace('words=39104','words=501760')}
for name,text in controls.items():
    try:witness(text,a.phase)
    except AssertionError:continue
    raise AssertionError('Accepted invalid GPU witness: '+name)
bins=root/'game/bin';pairs=[]
for kind in ('main','env'):
    paths=[bins/f'cell-{a.phase*3+arm}-{kind}.bin' for arm in range(3)]
    decoded=[image(path,a.phase*3+arm,kind) for arm,path in enumerate(paths)]
    assert decoded[0][0]==decoded[1][0]==decoded[2][0], 'Raster layout changed'
    base,candidate,returned=[d[1] for d in decoded]
    assert base==returned, 'Ordinary return drift: '+kind
    different=sum(x!=y for x,y in zip(base,candidate))
    pairs.append(dict(kind=kind,files=[str(f.relative_to(root)) for f in paths],
                      hashes=[sha(f) for f in paths],differentBytes=different))
report={'valid':all(row['differentBytes']==0 for row in pairs),
        'experiment':'A44-complete-terrain-GPU','platform':'emulator',
        'phase':a.phase,'activation':data,'images':pairs,
        'readerNegativeControls':list(controls),
        'elfSha256':binding['elfSha256'],'logSha256':sha(logpath),
        'promotionAllowed':False,'limits':['Held main/env, not display qualification.',
          'No moving candidate, sustained pressure, full lifetime or physical price.',
          'Exact differences are reported; no tolerance introduced after failure.']}
(root/f'world-cell-terrain-emulator-phase{a.phase}.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
assert report['valid'], 'Complete-cell raster mismatch'

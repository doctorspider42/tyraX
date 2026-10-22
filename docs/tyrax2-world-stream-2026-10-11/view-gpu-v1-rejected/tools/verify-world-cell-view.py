"""Qualify A44 four-direction actual cell activation and main/env/display images."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

def witness(log,phase):
    pattern=rf'A44VIEWGPU phase={phase} views=4 arms=3 target=0 complete=1'
    assert len(re.findall(pattern,log))==1,'Missing/duplicate view completion'
    log=log[:re.search(pattern,log).end()]
    rows=re.findall(r'A44VIEWARM phase=(\d+) angle=(\d+) arm=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+)',log)
    rows=[tuple(map(int,r)) for r in rows if int(r[0])==phase]
    assert [(r[1],r[2]) for r in rows]==[(v,a) for v in range(4) for a in range(3)]
    counts=[]
    for view in range(4):
        before,candidate,after=rows[view*3:view*3+3]
        assert before[3:]==after[3:]==(0,0,0),'Control activated candidate'
        assert candidate[3]>0 and candidate[3]==candidate[4] and candidate[5]==0
        counts.append(candidate[4])
    assert len(set(counts))>1,'No distinct visible-cell coverage'
    builds=re.findall(r'A44TERRAIN compile=PASS cells=(\d+) passes=(\d+) textures=(\d+) words=(\d+) heap=(\d+)',log)
    assert builds
    cells,passes,textures,words,heap=map(int,builds[-1])
    assert cells>0 and passes>=cells and 0<textures<=64 and 0<words<=heap*3//4
    return dict(submittedByView=counts,cells=cells,passes=passes,textures=textures,
                words=words,heap=heap,fallback=0)

def read_image(path,stage,kind):
    raw=path.read_bytes();assert len(raw)>=16
    w,h,psm,n=struct.unpack_from('<4I',raw)
    assert n==stage and (w,h)==((128,128) if kind=='env' else (448,448))
    assert psm==(2 if kind=='display' else 0)
    stride=2 if psm==2 else 4
    assert len(raw)==16+w*h*stride
    assert len(set(struct.unpack('<'+('H' if stride==2 else 'I')*(w*h),raw[16:])))>16
    return raw[16:]

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--phase',type=int,default=0)
a=p.parse_args();root=a.fixture.resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
launch=json.loads((root/'emulator/launch.json').read_text())
assert launch['elfSha256']==binding['elfSha256']
logpath=root/'emulator/emulator.log';log=logpath.read_text(errors='replace')
data=witness(log,a.phase)
positive='A44TERRAIN compile=PASS cells=25 passes=43 textures=19 words=39104 heap=501760\n'
for view,count in enumerate((3,5,12,7)):
    for arm in range(3):
        n=count if arm==1 else 0
        positive+=f'A44VIEWARM phase={a.phase} angle={view} arm={arm} requested={n} submitted={n} fallback=0\n'
positive+=f'A44VIEWGPU phase={a.phase} views=4 arms=3 target=0 complete=1'
assert witness(positive,a.phase)['submittedByView']==[3,5,12,7]
controls={'missing':'','duplicate':positive+'\n'+positive,
          'partial':positive.replace('requested=3 submitted=3','requested=3 submitted=2'),
          'fallback':positive.replace('submitted=3 fallback=0','submitted=3 fallback=1'),
          'wrongTarget':positive.replace('target=0','target=1'),
          'viewLoss':positive.replace('angle=3','angle=2'),
          'budget':positive.replace('words=39104','words=501760')}
for name,text in controls.items():
    try:witness(text,a.phase)
    except AssertionError:continue
    raise AssertionError('Invalid view witness accepted: '+name)
pairs=[]
for view in range(4):
    for kind in ('main','env','display'):
        stages=[a.phase*12+view*3+arm for arm in range(3)]
        paths=[root/'game/bin'/f'cell-{stage}-{kind}.bin' for stage in stages]
        before,candidate,after=[read_image(path,stage,kind) for path,stage in zip(paths,stages)]
        pairs.append(dict(view=view,kind=kind,
            files=[f.relative_to(root).as_posix() for f in paths],hashes=[sha(f) for f in paths],
            candidateDifferentBytes=sum(x!=y for x,y in zip(before,candidate)),
            candidateReturnDifferentBytes=sum(x!=y for x,y in zip(candidate,after)),
            returnDifferentBytes=sum(x!=y for x,y in zip(before,after))))
report={'valid':all(p['candidateDifferentBytes']==p['returnDifferentBytes']==0 for p in pairs),
        'experiment':'A44-terrain-four-view-display','platform':'emulator','phase':a.phase,
        'activation':data,'images':pairs,'readerNegativeControls':list(controls),
        'elfSha256':binding['elfSha256'],'logSha256':sha(logpath),'promotionAllowed':False,
        'limits':['Four directions at one end-of-drive position, not every possible cell/view.',
                  'No moving candidate, sustained pressure, physical price or60fps proof.']}
(root/f'world-cell-view-emulator-phase{a.phase}.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
assert report['valid'],'A44 main/env/display image mismatch'

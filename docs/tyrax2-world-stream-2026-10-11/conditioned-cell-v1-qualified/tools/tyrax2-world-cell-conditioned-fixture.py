"""Activate complete terrain cells only after the conditioned ordinary view gate passes."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
logpath=a.source/'emulator/emulator.log';log=logpath.read_text(errors='replace')
assert 'DRIVEDONE valid=1' in log
order=json.loads((a.source/'world-command-order-manifest.json').read_text())
assert order['heldMode']==2 and order['movingAutoUnchanged']
assert re.findall(r'A44ORDER stage=(\d+) mode=(\d+) active=(\d+)',log)==[
    (str(s),'2','1') for s in range(9)]
for phase in range(3):
    report=json.loads((a.source/f'world-cell-view-emulator-phase{phase}.json').read_text())
    assert report['valid'] and report['ordinaryOnly']
    assert report['elfSha256']==binding['elfSha256'] and report['logSha256']==sha(logpath)
    assert report['activation']['submittedByView']==[0]*4
    for row in report['images']:
        for rel,digest in zip(row['files'],row['hashes']):assert sha(a.source/rel)==digest,rel
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-','cell-','source-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json','world-source-manifest.json',
            'world-source-switch-manifest.json','world-command-source-manifest.json',
            'world-command-order-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
physics=a.out/'game/src/gen/game_physics.gen.cpp';t=physics.read_text()
old='    for(unsigned arm=0;arm<3;++arm){\n    WorldCellComplete::active=false;'
assert t.count(old)==1
t=t.replace(old,'    for(unsigned arm=0;arm<3;++arm){\n    WorldCellComplete::active=arm==1;')
old='    if(arm==1 && WorldCellComplete::active)TYRA_ASSERT('
assert t.count(old)==1;t=t.replace(old,'    if(arm==1)TYRA_ASSERT(')
physics.write_text(t)
manifest=json.loads((a.out/'world-cell-view-manifest.json').read_text())
manifest.update(ordinaryOnly=False,baseElfSha256=binding['elfSha256'],status='UNQUALIFIED',
                generatorSha256=sha(Path(__file__)))
(a.out/'world-cell-view-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.out/'world-cell-conditioned-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'heldMode':2,'fixedSceneStep':True,'movingCandidate':False,'status':'UNQUALIFIED',
    'limits':['Actual complete-cell activation in held four-view gate only.',
              'No moving lifetime or physical performance claim.']},indent=2)+'\n')
print(a.out,'UNQUALIFIED complete terrain cells; fixed dt/held order2 preserved')

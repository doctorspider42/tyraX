"""Freeze source-bound A44 terrain GPU evidence and require exact reader replay."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('fixture',type=Path)
a=p.parse_args();root=a.fixture.resolve();out=a.out.resolve()
assert not out.exists()
repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
assert sha(root/'tyra/engine/bin/libtyra.a')==binding['engineArchiveSha256']
names=set(binding['files'])
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
names.update(('native-binding.json','symbols.txt','authored-manifest.json',
              'world-cell-terrain-manifest.json','build.log',
              'game/bin/vehicle-playground.elf','game/bin/vehicle-playground.elf.sym',
              'tyra/engine/bin/libtyra.a','emulator/launch.json',
              'emulator/emulator.log','emulator/stdout.log','emulator/xvfb.log',
              'emulator/profile/PCSX2/inis/PCSX2.ini'))
original=[]
for phase in range(3):
    rel=f'world-cell-terrain-emulator-phase{phase}.json'
    report=json.loads((root/rel).read_text())
    assert report['valid'] and report['elfSha256']==binding['elfSha256']
    assert report['logSha256']==sha(root/'emulator/emulator.log')
    original.append((root/rel).read_bytes());names.add(rel)
    for row in report['images']:
        for rel,digest in zip(row['files'],row['hashes']):
            rel=rel.replace('\\','/');assert sha(root/rel)==digest,rel;names.add(rel)
for rel in sorted(names):
    dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/rel,dest)
for name in ('tyrax2-world-cell-complete.hpp','tyrax2-world-cell-complete-methods.inc',
             'tyrax2-world-cell-terrain-methods.inc','tyrax2-world-cell-terrain-fixture.py',
             'verify-world-cell-terrain.py','tyrax2-world-cell-terrain-archive.py',
             'tyrax2-motion-run.py'):
    (out/'tools').mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,out/'tools'/name)
for phase in range(3):
    r=subprocess.run([sys.executable,str(out/'tools/verify-world-cell-terrain.py'),
                      str(out),'--phase',str(phase)],capture_output=True,text=True)
    (out/f'reader-phase{phase}.log').write_text(r.stdout+r.stderr)
    assert r.returncode==0,r.stderr
    assert (out/f'world-cell-terrain-emulator-phase{phase}.json').read_bytes()==original[phase]
pins={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(pins,indent=2)+'\n')
assert all(sha(out/rel)==digest for rel,digest in pins.items())
print(json.dumps({'valid':True,'files':len(pins),'exactReaderReplays':3,
                  'promotionAllowed':False,'physicalPrice':False}))

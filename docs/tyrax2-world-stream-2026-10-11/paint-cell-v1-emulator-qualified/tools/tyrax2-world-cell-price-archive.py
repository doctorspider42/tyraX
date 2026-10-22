"""Freeze source-bound clean price emulator evidence and replay its exact gate."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('fixture',type=Path)
p.add_argument('--rejected',action='store_true')
p.add_argument('--physical-attempt',type=Path)
a=p.parse_args();root=a.fixture.resolve();out=a.out.resolve()
assert not out.exists()
assert not (a.rejected and a.physical_attempt)
repo=Path(__file__).resolve().parent;sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
assert sha(root/'tyra/engine/bin/libtyra.a')==binding['engineArchiveSha256']
original=(root/'world-cell-price-emulator.json').read_bytes();report=json.loads(original)
assert report['valid']==(not a.rejected) and report['platform']=='emulator' and not report['physicalPrice']
assert report['elfSha256']==binding['elfSha256'] and report['logSha256']==sha(root/'emulator/emulator.log')
names=set(binding['files'])
names.update(('native-binding.json','symbols.txt','authored-manifest.json','world-cell-price-manifest.json',
              'build.log','game/bin/vehicle-playground.elf','game/bin/vehicle-playground.elf.sym',
              'tyra/engine/bin/libtyra.a','game/bin/drive-probe.csv','game/bin/log.txt',
              'emulator/launch.json','emulator/emulator.log','emulator/stdout.log','emulator/xvfb.log',
              'emulator/profile/PCSX2/inis/PCSX2.ini','world-cell-price-emulator.json'))
for row in report['images']:
    for name,digest in zip(row['files'],row['hashes']):
        rel='game/bin/'+name;assert sha(root/rel)==digest,rel;names.add(rel)
for path in (root/'game/bin').glob('triple-present-*.csv'):names.add(path.relative_to(root).as_posix())
for path in (root/'provenance').glob('*'):names.add(path.relative_to(root).as_posix())
for rel in sorted(names):
    target=out/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/rel,target)
for name in ('verify-world-cell-price.py','tyrax2-world-cell-price-fixture.py',
             'tyrax2-world-cell-price-checkpoint.inc','tyrax2-world-cell-incremental-methods.inc',
             'tyrax2-world-cell-skip-a31-fixture.py',
             'tyrax2-world-road-cell-fixture.py',
             'tyrax2-world-cell-direct-fixture.py','tyrax2-world-cell-direct.hpp',
             'tyrax2-paint-cell-fixture.py','tyrax2-paint-cell-price.hpp',
             'tyrax2-world-cell-price-archive.py','tyrax2-motion-run.py','tyrax2-deploy-ps2.ps1'):
    (out/'tools').mkdir(exist_ok=True);shutil.copy2(repo/name,out/'tools'/name)
r=subprocess.run([sys.executable,str(out/'tools/tyrax2-motion-run.py'),'stage',str(root),
                  '--stage-dir',str(out/'inputs')],capture_output=True,text=True)
(out/'input-stage.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
inputs=json.loads((out/'inputs/launch-inputs.json').read_text())
assert len(inputs)==305
assert all(sha(out/'inputs/bin'/rel)==digest for rel,digest in inputs.items())
assert not any(Path(rel).name.startswith('world-price-') and rel.endswith('.bin') for rel in inputs)
target=out/'world-cell-price-emulator.json';target.unlink()
r=subprocess.run([sys.executable,str(out/'tools/verify-world-cell-price.py'),str(out),
                  '--platform','emulator'],capture_output=True,text=True)
(out/'reader.log').write_text(r.stdout+r.stderr)
assert r.returncode==(1 if a.rejected else 0),r.stderr
if a.rejected:assert 'World-cell price conditioning/delivery/image gate rejected' in r.stderr
assert target.read_bytes()==original
if a.physical_attempt:
    attempt=a.physical_attempt.resolve()
    physicalOriginal=(attempt/'world-cell-price-ps2.json').read_bytes()
    physicalReport=json.loads(physicalOriginal)
    assert physicalReport['valid'] and physicalReport['physicalPrice']
    assert physicalReport['elfSha256']==binding['elfSha256']
    assert physicalReport['logSha256']==sha(attempt/'console.log')
    shutil.copytree(attempt,out/'physical')
    target=out/'physical/world-cell-price-ps2.json';target.unlink()
    r=subprocess.run([sys.executable,str(out/'tools/verify-world-cell-price.py'),str(out),
                      '--platform','ps2','--attempt',str(out/'physical')],capture_output=True,text=True)
    (out/'physical-reader.log').write_text(r.stdout+r.stderr)
    assert r.returncode==0,r.stderr
    assert target.read_bytes()==physicalOriginal
pins={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(pins,indent=2)+'\n')
assert all(sha(out/rel)==digest for rel,digest in pins.items())
print(json.dumps(dict(valid=True,files=len(pins),inputs=len(inputs),exactReaderReplay=True,
                      testValid=report['valid'],physicalPrice=bool(a.physical_attempt),
                      physicalReaderReplay=bool(a.physical_attempt),promotionAllowed=False)))

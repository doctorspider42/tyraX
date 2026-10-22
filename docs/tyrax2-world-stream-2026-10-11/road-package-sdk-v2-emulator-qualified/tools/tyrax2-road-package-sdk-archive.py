"""Freeze/replay actual EE SDK split plus ordinary inherited image regression."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('fixture',type=Path)
p.add_argument('--failed-fixture',type=Path)
a=p.parse_args()
root=a.fixture.resolve();out=a.out.resolve()
assert not out.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
sdk_bytes=(root/'road-package-sdk-qualified.json').read_bytes()
sdk=json.loads(sdk_bytes)
price_bytes=(root/'world-cell-price-emulator.json').read_bytes()
price=json.loads(price_bytes)
assert sdk['valid'] and price['valid'] and not sdk['gpuExecuted'] and not price['physicalPrice']
assert price['elfSha256']==sdk['elfSha256']==binding['elfSha256']
assert sha(root/'emulator/emulator.log')==sdk['logSha256']==price['logSha256']
assert sha(root/'tyra/engine/bin/libtyra.a')==binding['engineArchiveSha256']
names=set(binding['files'])
names.update(('native-binding.json','symbols.txt','authored-manifest.json',
              'road-package-sdk-manifest.json','road-package-sdk-qualified.json',
              'world-cell-price-manifest.json','world-cell-price-emulator.json','build.log',
              'game/bin/vehicle-playground.elf','game/bin/vehicle-playground.elf.sym',
              'tyra/engine/bin/libtyra.a','game/bin/drive-probe.csv','game/bin/log.txt',
              'emulator/launch.json','emulator/emulator.log','emulator/stdout.log','emulator/xvfb.log',
              'emulator/profile/PCSX2/inis/PCSX2.ini'))
for image in price['images']:
    for name,digest in zip(image['files'],image['hashes']):
        rel='game/bin/'+name;assert sha(root/rel)==digest;names.add(rel)
for f in (root/'game/bin').glob('triple-present-*.csv'):names.add(f.relative_to(root).as_posix())
for rel in sorted(names):
    if rel in binding['files']:assert sha(root/rel)==binding['files'][rel]
    dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/rel,dest)
repo=Path(__file__).resolve().parent
for name in ('tyrax2-road-package-sdk.hpp','tyrax2-road-package-sdk-check.hpp',
             'tyrax2-road-package-sdk-fixture.py','verify-road-package-sdk.py',
             'tyrax2-road-package-sdk-archive.py','verify-world-cell-price.py','tyrax2-motion-run.py'):
    (out/'tools').mkdir(exist_ok=True);shutil.copy2(repo/name,out/'tools'/name)
if a.failed_fixture:
    for rel in ('build.log','road-package-sdk-manifest.json',
                'tyra/engine/inc/debug/road_package_sdk_check.hpp'):
        dest=out/'failed-native-v1'/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(a.failed_fixture/rel,dest)
for tool,target,args,expected in [
    ('verify-road-package-sdk.py','road-package-sdk-qualified.json',[],sdk_bytes),
    ('verify-world-cell-price.py','world-cell-price-emulator.json',['--platform','emulator'],price_bytes)]:
    (out/target).unlink()
    result=subprocess.run([sys.executable,str(out/'tools'/tool),str(out),*args],capture_output=True,text=True)
    (out/(target+'.replay.log')).write_text(result.stdout+result.stderr)
    result.check_returncode();assert (out/target).read_bytes()==expected,target
stage=subprocess.run([sys.executable,str(out/'tools/tyrax2-motion-run.py'),'stage',str(root),
                      '--stage-dir',str(out/'inputs')],capture_output=True,text=True)
(out/'input-stage.log').write_text(stage.stdout+stage.stderr)
stage.check_returncode()
inputs=json.loads((out/'inputs/launch-inputs.json').read_text())
assert all(sha(out/'inputs/bin'/name)==digest for name,digest in inputs.items())
pins={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(pins,indent=2)+'\n')
print('PASS SDK-only archive',len(pins),'hashes',len(inputs),'launch inputs; exact two reader replays; no new GPU path or PS2 gain')

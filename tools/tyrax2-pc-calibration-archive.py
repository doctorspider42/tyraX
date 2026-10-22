"""Preserve accepted closed A37 calibration inputs and exact readers."""
import argparse,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();root=a.fixture.resolve();out=a.out.resolve();assert not out.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
paths=set(binding['files'])
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
for target,reportname in [('emulator','pc-calibration-emulator-v2.json'),('physical','pc-calibration-physical.json')]:
    report=json.loads((root/reportname).read_text());assert report['valid'] and report['qualificationOnly']
    assert report['target']==target and report['elfSha256']==binding['elfSha256']
    folder='physical/bin' if target=='physical' else 'game/bin'
    log='physical/console.log' if target=='physical' else 'emulator/emulator.log'
    assert sha(root/log)==report['logSha256'] and sha(root/folder/'pc-calibration.csv')==report['csvSha256']
    assert sha(root/folder/'vehicle-playground.elf')==binding['elfSha256']
    paths.update((reportname,log,folder+'/pc-calibration.csv',folder+'/vehicle-playground.elf'))
paths.update(('native-binding.json','symbols.txt','pc-calibration-manifest.json','authored-manifest.json',
              'sdk-timer-disassembly.txt','build.log','build-repair.log','build-repair2.log',
              'physical/editor-deploy.json','physical/launch-inputs.json','emulator/launch.json'))
for rel in sorted(paths):
    dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/rel,dest)
sdk=Path('C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
manifest=json.loads((root/'pc-calibration-manifest.json').read_text())
for rel,digest in manifest['sdkPins'].items():
    assert sha(sdk/rel)==digest
    dest=out/'sdk'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(sdk/rel,dest)
repo=Path(__file__).resolve().parents[1]
for name in ('tyrax2-pc-calibration.py','tyrax2-pc-calibration.hpp','tyrax2-pc-sample-store.hpp',
             'verify-pc-sample-store.cpp','tyrax2-pc-calibration-check.py','tyrax2-pc-calibration-archive.py'):
    dest=out/'tools'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/'tools'/name,dest)
inventory={str(f.relative_to(out)).replace('\\','/'):sha(f) for f in sorted(out.rglob('*')) if f.is_file()}
(out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n');print('Preserved',len(inventory),'files:',out)

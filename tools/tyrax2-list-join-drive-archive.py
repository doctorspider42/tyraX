"""Preserve complete A23 moving evidence and exact tools without asset duplicates."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists()
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
binding=json.loads((a.fixture/'native-binding.json').read_text())
files=list(binding['files'])
for rel,h in binding['files'].items():assert sha(a.fixture/rel)==h,rel
files+=['native-binding.json','symbols.txt','authored-manifest.json','list-join-drive-manifest.json',
        'game/Makefile','tyra/Makefile.base','tyra/engine/Makefile','game/bin/vehicle-playground.elf','build.log']
for extra in ('build-missing-diagnostic-target.log',):
    if (a.fixture/extra).exists():files.append(extra)
for target,folder in [('emulator','game/bin'),('physical','physical/bin')]:
    report=a.fixture/f'list-join-drive-{target}.json'
    assert report.exists();data=json.loads(report.read_text());assert data['valid'] and data['rasterEqual']
    log=a.fixture/target/('console.log' if target=='physical' else 'emulator.log')
    assert sha(log)==data['logSha256']
    files.append(report.name)
    files += [str(path.relative_to(a.fixture)) for path in (a.fixture/target).iterdir() if path.is_file()]
    files += [str(path.relative_to(a.fixture)) for path in (a.fixture/folder).glob('tape-*.bin')]
    files.append(folder+'/drive-probe.csv')
    if target=='physical':files.append(folder+'/vehicle-playground.elf')
for rel in sorted(set(files)):
    dst=a.out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a.fixture/rel,dst)
for target,folder in [('emulator','game/bin'),('physical','physical/bin')]:
    path=a.fixture/folder
    (a.out/f'{target}-bin-hashes.json').write_text(json.dumps({str(f.relative_to(path)).replace('\\','/'):sha(f) for f in path.rglob('*') if f.is_file()},indent=2)+'\n')
repo=Path(__file__).resolve().parents[1]
for name in ('tyrax2-list-join-drive.py','tyrax2-list-join-drive-analyze.py','tyrax2-list-join-drive-archive.py',
             'tyrax2-list-join-fast.hpp','tyrax2-list-join.hpp','tyrax2-deploy-ps2.ps1','tyrax2-motion-run.py','tyrax2-quiet-fixture.py'):
    dst=a.out/'tools'/name;dst.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,dst)
inventory={str(f.relative_to(a.out)).replace('\\','/'):sha(f) for f in a.out.rglob('*') if f.is_file()}
(a.out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print(json.dumps(dict(files=len(inventory),elf=binding['elfSha256'])))

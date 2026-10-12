"""Archive complete A18 three-route evidence, with asset hashes instead of duplicates."""
import argparse,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
b=json.loads((a.fixture/'native-binding.json').read_text());files=list(b['files'])
for rel,h in b['files'].items():assert sha(a.fixture/rel)==h,rel
if (a.fixture/'list-join-emulator.json').exists():
    joined=json.loads((a.fixture/'list-join-emulator.json').read_text())
    assert joined['valid'] and joined['rasterEqual'] and joined['qualificationOnly']
    assert sha(a.fixture/'emulator/emulator.log')==joined['logSha256']
files+=['native-binding.json','symbols.txt','game/Makefile','tyra/Makefile.base','tyra/engine/Makefile','game/bin/vehicle-playground.elf']
for name in ('authored-manifest.json','bag-tape-manifest.json','command-tape-manifest.json','command-pricing-manifest.json','list-join-manifest.json','list-join-emulator.json','list-join-emulator-base.json','clone.json','build.log','analyzer-controls.json','game/bin/log.txt'):
    if(a.fixture/name).exists():files.append(name)
for target,folder in [('emulator','game/bin'),('physical','physical/bin')]:
    report=a.fixture/f'command-price-{target}.json'
    if not (a.fixture/target).exists():continue
    if report.exists():
        r=json.loads(report.read_text());assert r['valid'] and r['rasterEqual']
        files.append(report.name)
    files += [str(f.relative_to(a.fixture)) for f in (a.fixture/target).iterdir() if f.is_file()]
    for pattern in ('tape-*.bin','command-*.bin','three-path.cfg'):
        files += [str(f.relative_to(a.fixture)) for f in (a.fixture/folder).glob(pattern)]
    if target=='physical':files.append('physical/bin/vehicle-playground.elf')
    log=a.fixture/target/('console.log' if target=='physical' else 'emulator.log')
    if report.exists():assert sha(log)==r['logSha256'],'Reanalyze the settled log before archiving'
for rel in sorted(set(files)):
    dst=a.out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a.fixture/rel,dst)
for target,folder in [('emulator','game/bin'),('physical','physical/bin')]:
    path=a.fixture/folder
    if path.exists():
        inventory={str(f.relative_to(path)).replace('\\','/'):sha(f) for f in path.rglob('*') if f.is_file()}
        (a.out/f'{target}-bin-hashes.json').write_text(json.dumps(inventory,indent=2)+'\n')
repo=Path(__file__).resolve().parents[1]
for tool in ('tyrax2-command-tape.py','tyrax2-command-tape.hpp','tyrax2-command-replay.hpp','tyrax2-command-pricing.py','tyrax2-command-pricing.hpp','tyrax2-command-price-analyze.py','tyrax2-command-price-archive.py','verify-command-price-analyze.py','tyrax2-deploy-ps2.ps1'):
    dst=a.out/'tools'/tool;dst.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/tool,dst)
if (a.fixture/'list-join-manifest.json').exists():
    for tool in ('tyrax2-list-join.py','tyrax2-list-join.hpp','tyrax2-list-join-analyze.py','verify-list-join.cpp','verify-list-join-analyze.py','tyrax2-bag-tape-clone.py'):
        shutil.copy2(repo/'tools'/tool,a.out/'tools'/tool)
inventory={str(f.relative_to(a.out)).replace('\\','/'):sha(f) for f in a.out.rglob('*') if f.is_file()}
(a.out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
for rel,h in inventory.items():assert sha(a.out/rel)==h
print(json.dumps(dict(files=len(inventory),elf=b['elfSha256'],source=str(a.fixture))))

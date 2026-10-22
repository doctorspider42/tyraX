"""Archive a completed A18 qualification, sources and images without asset duplication."""
import argparse,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((a.fixture/'native-binding.json').read_text())
files=list(b['files'])
for rel,h in b['files'].items():assert sha(a.fixture/rel)==h
files+=['native-binding.json','bag-tape-manifest.json','authored-manifest.json','symbols.txt','game/Makefile','tyra/Makefile.base','tyra/engine/Makefile','game/bin/vehicle-playground.elf','emulator/launch.json','emulator/emulator.log','emulator/stdout.log']
files += [str(f.relative_to(a.fixture)) for f in (a.fixture/'game/bin').glob('tape-*.bin')]
files += [str(f.relative_to(a.fixture)) for f in a.fixture.glob('tape-*') if f.is_file()]
for optional in ('build.log','clone.json','game/bin/bag-tape-control.cfg'):
    if(a.fixture/optional).exists():files.append(optional)
if (a.fixture/'tape-physical.json').exists():
    files += [str(f.relative_to(a.fixture)) for f in (a.fixture/'physical').iterdir() if f.is_file()]
    files += [str(f.relative_to(a.fixture)) for f in (a.fixture/'physical/bin').glob('tape-*.bin')]
    files.append('physical/bin/vehicle-playground.elf')
for rel in files:
    dst=a.out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a.fixture/rel,dst)
assets={str(f.relative_to(a.fixture/'game/bin')):sha(f) for f in (a.fixture/'game/bin').rglob('*') if f.is_file()}
(a.out/'game-bin-hashes.json').write_text(json.dumps(assets,indent=2)+'\n')
inventory={str(f.relative_to(a.out)).replace('\\','/'):sha(f) for f in a.out.rglob('*') if f.is_file()}
(a.out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
for rel,h in inventory.items():assert sha(a.out/rel)==h
print(json.dumps(dict(files=len(inventory),source=str(a.fixture),elf=b['elfSha256'])))

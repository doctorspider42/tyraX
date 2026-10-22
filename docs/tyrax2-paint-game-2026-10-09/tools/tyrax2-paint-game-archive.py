"""Archive bound A11 sources, target evidence and host checks without overwriting."""
import hashlib
import json
import shutil
import sys
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
root,host,out=[Path(p).resolve() for p in sys.argv[1:4]]
assert out.is_dir() and sorted(p.name for p in out.iterdir())==['README.md']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def copy(source,dest):
    assert not dest.exists(),dest
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,dest)
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():
    source=root/rel
    assert sha(source)==digest,rel
    copy(source,out/'fixture'/rel)
elf=root/'game/bin/vehicle-playground.elf'
assert sha(elf)==binding['elfSha256']
copy(elf,out/'fixture/game/bin'/elf.name)
for path in root.iterdir():
    if path.is_file():copy(path,out/'fixture'/path.name)
for path in (root/'emulator').rglob('*'):
    if path.is_file() and (path.parent==root/'emulator' or path.suffix=='.ini'):
        copy(path,out/'fixture/emulator'/path.relative_to(root/'emulator'))
for kind in ('fpp','orbit'):
    tree=root/('generated-'+kind)
    for folder in ('src','inc'):
        for path in (tree/folder).rglob('*'):
            if path.is_file():copy(path,out/tree.name/path.relative_to(tree))
    copy(tree/'vehicle-playground.tyra',out/tree.name/'vehicle-playground.tyra')
analysis=json.loads((root/'paint-lifecycle-analysis.json').read_text())
for asset in analysis['lodAssets']:
    path=root/'game/bin/vehicles'/asset['file']
    assert sha(path)==asset['sha256']
    copy(path,out/'fixture/game/bin/vehicles'/path.name)
for rel in ('game/Makefile','tyra/engine/Makefile'):
    copy(root/rel,out/'fixture'/rel)
assets={str(p.relative_to(root/'game/bin')):sha(p) for p in (root/'game/bin').rglob('*') if p.is_file()}
(out/'fixture/asset-hashes.json').write_text(json.dumps(assets,indent=2)+'\n')
for path in host.rglob('*'):
    if path.is_file():copy(path,out/'host'/path.relative_to(host))
for name in ('tyrax2-paint-game.py','verify-paint-game.py','verify-paint-generated.py',
             'tyrax2-paint-lifecycle-analyze.py','tyrax2-paint-game-archive.py',
             'tyrax2-motion-run.py','tyrax2-sky-capture.py'):
    copy(repo/'tools'/name,out/'tools'/name)
for name in ('templates.cpp','game_templates.inc'):
    copy(repo/'src'/name,out/'current-src'/name)
hashes={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
(out/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
for name,digest in hashes.items():assert sha(out/name)==digest,name
print(f'{len(hashes)} archived files verified; native {len(binding["files"])}; ELF {binding["elfSha256"]}')

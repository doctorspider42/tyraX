"""Preserve A16 bound sources, launch inputs and completed outputs without editing originals."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
p.add_argument('destination',type=Path)
a=p.parse_args();root=a.fixture.resolve();dest=a.destination.resolve()
assert not dest.exists(),'Refuse to overwrite evidence'
binding=json.loads((root/'native-binding.json').read_text())
for name,digest in binding['files'].items():
    source=root/name
    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,name
    target=dest/name;target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
for name in ('game/Makefile','tyra/Makefile.base','tyra/engine/Makefile'):
    shutil.copy2(root/name,dest/name)
for folder in ('game/bin','physical','emulator'):
    if (root/folder).exists():shutil.copytree(root/folder,dest/folder)
for source in root.iterdir():
    if source.is_file() and source.suffix in ('.json','.log','.txt','.png'):
        shutil.copy2(source,dest/source.name)
files={str(f.relative_to(dest)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest()
       for f in sorted(dest.rglob('*')) if f.is_file()}
(dest/'archive-sha256.json').write_text(json.dumps(files,indent=2)+'\n')
for name,digest in files.items():assert hashlib.sha256((dest/name).read_bytes()).hexdigest()==digest
print(json.dumps(dict(files=len(files),source=str(root),destination=str(dest))))

"""A39 immutable BOTH-target source/data/host-component archive."""
from pathlib import Path
parent=Path(__file__).with_name('tyrax2-triple-archive.py')
code=parent.read_text();code=code[:code.index('\ninventory=')]
assert code.count("f'triple-{target}.json'")==2
code=code.replace("f'triple-{target}.json'","f'ready-{target}.json'")
exec(compile(code,str(parent),'exec'))
for rel in ('ready-manifest.json','a38-native-binding.json'):copy(rel)
for name in ('tyrax2-ready-fixture.py','tyrax2-ready-check.py','tyrax2-ready-archive.py',
 'tyrax2-ready-guard.hpp','tyrax2-ready-trial.hpp','verify-ready-guard.cpp','verify-ready-runtime.py'):
 dest=out/'tools'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,dest)
host=source/'host-runtime-final';pins=json.loads((host/'source-pins.json').read_text())
for rel,digest in pins.items():assert sha(source/rel)==digest,rel
for file in host.iterdir():
 if file.is_file():copy(file.relative_to(source).as_posix())
for target in ('physical','emulator'):
 r=json.loads((source/f'ready-{target}.json').read_text())
 assert r['experiment']=='A39' and r['a38ReaderSha256']==sha(repo/'tools/tyrax2-triple-check.py')
 assert len(r['readyRecords'])==24 and all(row[6]>0 for row in r['readyRecords'] if row[3] and (row[1] or target=='physical'))
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print('Archived',len(inventory),'files; qualified A39 target evidence plus modeled host components')

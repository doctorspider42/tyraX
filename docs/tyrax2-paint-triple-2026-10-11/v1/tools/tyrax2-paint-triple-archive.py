"""Archive complete BOTH-target A40 source, original-emitter proof and raster gates."""
from pathlib import Path
parent=Path(__file__).with_name('tyrax2-triple-archive.py')
code=parent.read_text();code=code[:code.index('\ninventory=')]
assert code.count("f'triple-{target}.json'")==2
code=code.replace("f'triple-{target}.json'","f'paint-triple-{target}.json'")
old="'physical/launch-inputs.json','physical/reset.log','physical/reset-listen.log'"
assert code.count(old)==1
code=code.replace(old,"'physical/launch-inputs.json'")
exec(compile(code,str(parent),'exec'))
launch=json.loads((source/'physical/editor-deploy.json').read_text(encoding='utf-8-sig'))
for rel in ('physical/reset.log','physical/reset-listen.log'):
 if (source/rel).exists():copy(rel)
 else:assert launch['resetMode']=='user-confirmed-reset'
failed=source/'physical-start-no-output'
if failed.exists():
 for file in failed.iterdir():
  if file.is_file():copy(file.relative_to(source).as_posix())
for rel in ('paint-triple-manifest.json','a38-native-binding.json','parent-head-writer.cpp'):copy(rel)
for name in ('tyrax2-paint-triple-fixture.py','tyrax2-paint-triple-check.py','tyrax2-paint-triple-archive.py','tyrax2-paint-triple.hpp'):
 dest=out/'tools'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,dest)
for target in ('physical','emulator'):
 r=json.loads((source/f'paint-triple-{target}.json').read_text())
 assert r['experiment']=='A40' and r['a38ReaderSha256']==sha(repo/'tools/tyrax2-triple-check.py')
 assert len(r['paintHeads'])==18 and all(row[3]>0 if row[2] else row[3]==0 for row in r['paintHeads'])
 assert r['sdkEmitterNormalizedSha256']==sha(source/'parent-head-writer.cpp')
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print('Archived',len(inventory),'files; A40 both-target evidence, not a runnable asset mirror')

"""Freeze A44 cold compiler execution; never archive it as a GPU/FPS result."""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();root=a.fixture.resolve();out=a.out.resolve()
assert not out.exists()
tools=Path(__file__).resolve().parent
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
subprocess.run([sys.executable,str(tools/'verify-world-cell-sdk-execution.py'),str(root)],check=True)
report=json.loads((root/'cell-sdk-emulator.json').read_text())
binding=json.loads((root/'native-binding.json').read_text())
assert report['valid'] and report['promotionAllowed'] is False
assert sha(root/'emulator/emulator.log')==report['logSha256']
out.mkdir(parents=True)
def copy(rel):
    source=root/rel;dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,dest);assert sha(source)==sha(dest)
for rel in binding['files']:copy(rel)
for rel in ('native-binding.json','symbols.txt','authored-manifest.json',
            'world-cell-sdk-manifest.json','cell-sdk-emulator.json','build.log',
            'game/bin/vehicle-playground.elf','emulator/emulator.log',
            'emulator/stdout.log','emulator/launch.json',
            'emulator/profile/PCSX2/inis/PCSX2.ini'):
    copy(rel)
for f in (root/'game/bin').glob('*.sym'):copy(f.relative_to(root))
for name in ('tyrax2-world-cell-sdk-fixture.py','tyrax2-world-cell-sdk-archive.py',
             'verify-world-cell-sdk-execution.py','tyrax2-motion-run.py'):
    dest=out/'tools'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(tools/name,dest)
subprocess.run([sys.executable,str(out/'tools/tyrax2-motion-run.py'),'stage',str(root),
                '--stage-dir',str(out/'inputs')],check=True)
before=sha(out/'cell-sdk-emulator.json')
subprocess.run([sys.executable,str(out/'tools/verify-world-cell-sdk-execution.py'),str(out)],check=True)
assert sha(out/'cell-sdk-emulator.json')==before
assert sha(root/'emulator/emulator.log')==report['logSha256'],'Log still changes'
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
assert all(sha(out/rel)==value for rel,value in inventory.items())
print(json.dumps({'valid':True,'files':len(inventory),'readerReplayEqual':True,
                  'promotionAllowed':False,'gpuExecuted':False}))

"""Qualify A44 cell source compiler only; no VIF execution or FPS claim."""
import argparse,hashlib,json,shutil,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('--ee-cxx',type=Path,required=True)
a=p.parse_args();out=a.out.resolve();assert not out.exists();out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
names=('tyrax2-world-cell-plan.hpp','tyrax2-world-strip-plan.hpp',
       'verify-world-cell-plan.cpp','verify-world-cell-plan.py')
for name in names:shutil.copy2(repo/'tools'/name,out/name)
pins={name:sha(out/name) for name in names}
commands=[]
def run(label,command):
 commands.append(command);r=subprocess.run(command,text=True,capture_output=True)
 (out/f'{label}.stdout.log').write_text(r.stdout)
 (out/f'{label}.stderr.log').write_text(r.stderr)
 assert r.returncode==0,(label,r.returncode,r.stderr)
 return r.stdout
common=['-std=c++17','-Wall','-Wextra','-Werror']
for label,flags in [('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 binary=out/label
 run(label+'-compile',['g++',*common,*flags,str(out/'verify-world-cell-plan.cpp'),'-o',str(binary)])
 assert run(label+'-run',[str(binary)])=='WORLDCELL sourceCases=840 failedReplacements=1680 controls=16 lifecycle=PASS\n'
obj=out/'cell-ee.o'
run('ee-compile',[str(a.ee_cxx),*common,'-O2','-c',str(out/'verify-world-cell-plan.cpp'),'-o',str(obj)])
raw=obj.read_bytes();assert raw[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<HH',raw,16)==(1,8)
assert all(sha(out/name)==value==sha(repo/'tools'/name) for name,value in pins.items())
report={'valid':True,'experiment':'A44-cell-source','sources':pins,'commands':commands,
 'sourceCasesPerHostMode':840,'failedReplacementsPerHostMode':1680,
 'negativeControlsPerHostMode':16,'eeObjectSha256':sha(obj),'promotionAllowed':False,
 'limits':['Source-level whole-cell ownership/partition test, not actual VIF compiler.',
  'Material identity is a value, NOT an actual VRAM reservation or texture lease.',
  'Byte budget counts owned stream payload only, not allocator/VIF/whole scene RAM.',
  'No shader, raster, main/ISR lifetime, cache publication or physical performance qualification.',
  'Target object compilation is not execution; integrate actual builder/emitter before pricing.']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('sources','commands','limits')}))

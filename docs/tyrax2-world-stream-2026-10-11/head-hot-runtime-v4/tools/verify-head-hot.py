"""Test actual bound hot owner and byte writer against copied SDK headers."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('--fixture',type=Path,required=True)
p.add_argument('--sdk-host',type=Path,required=True)
a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
binding=json.loads((a.fixture/'native-binding.json').read_text())
for rel in ('tyra/engine/inc/debug/head_hot.hpp','tyra/engine/inc/debug/object_head.hpp'):
 src=a.fixture/rel;assert hashlib.sha256(src.read_bytes()).hexdigest()==binding['files'][rel]
 dest=a.out/'inc/debug'/src.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest)
shutil.copytree(a.sdk_host/'inc',a.out/'sdk')
shutil.copy2(a.out/'inc/debug/object_head.hpp',a.out/'tyrax2-object-head.hpp')
for name in ('verify-head-hot.cpp','verify-head-hot.py','verify-head-hot-facts.cpp','verify-object-head.cpp'):
 shutil.copy2(repo/'tools'/name,a.out/name)
renderer=a.fixture/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
assert hashlib.sha256(renderer.read_bytes()).hexdigest()==binding['files'][str(renderer.relative_to(a.fixture)).replace('\\','/')]
text=renderer.read_text();start=text.index('static ObjectHead::Facts hotHeadFacts(')
end=text.index('bool StaPipQBufferRenderer::installObjectHead',start)
(a.out/'actual-hot-facts.inc').write_text(text[start:end])
for mode,flags in (('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
 for stage,cmd in (
  ('build',['g++','-std=c++17','-D_EE','-fpermissive',*flags,'-I'+str(a.out/'inc'),'-I'+str(a.out/'sdk'),str(a.out/'verify-head-hot.cpp'),'-o',str(a.out/f'verify-{mode}')]),
  ('run',[str(a.out/f'verify-{mode}')])):
  r=subprocess.run(cmd,text=True,capture_output=True)
  (a.out/f'{mode}-{stage}.log').write_text(r.stdout+r.stderr);r.check_returncode()
  if stage=='run':print(r.stdout,end='')
 for stage,cmd in (
  ('facts-build',['g++','-std=c++17','-D_EE','-fpermissive',*flags,'-I'+str(a.out/'inc'),'-I'+str(a.out/'sdk'),str(a.out/'verify-head-hot-facts.cpp'),'-o',str(a.out/f'facts-{mode}')]),
  ('facts-run',[str(a.out/f'facts-{mode}')])):
  r=subprocess.run(cmd,text=True,capture_output=True)
  (a.out/f'{mode}-{stage}.log').write_text(r.stdout+r.stderr);r.check_returncode()
  if stage=='facts-run':print(r.stdout,end='')
inv={str(f.relative_to(a.out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in a.out.rglob('*')
 if f.is_file() and f.name not in ('verify-optimized','verify-sanitized','facts-optimized','facts-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(inv,indent=2)+'\n')

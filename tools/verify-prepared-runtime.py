"""Test the bound A32 tracked owner with copied actual engine bbox algorithms."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('--fixture',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
engine=a.fixture/'tyra/engine'
subprocess.run([sys.executable,str(repo/'tools/verify-prepared-front.py'),str(a.out),
    '--engine',str(engine)],check=True)
binding=json.loads((a.fixture/'native-binding.json').read_text())
pins=json.loads((a.out/'source-pins.json').read_text())
for name in ('prepared_front.hpp','prepared_runtime.hpp'):
    rel='tyra/engine/inc/debug/'+name
    src=engine/'inc/debug'/name
    digest=hashlib.sha256(src.read_bytes()).hexdigest()
    assert binding['files'][rel]==digest
    pins['files'][rel]=digest
    shutil.copy2(src,a.out/'inc/debug'/name)
for name in ('verify-prepared-runtime.cpp','verify-prepared-runtime.py'):
    shutil.copy2(repo/'tools'/name,a.out/name)
(a.out/'source-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
sources=list((a.out/'src').rglob('*.cpp'))
for mode,flags in (('optimized',['-O2']),('sanitized',['-O1','-g',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
    binary=a.out/f'runtime-{mode}'
    cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-ignored-qualifiers',
        '-Wno-deprecated-copy','-Wno-misleading-indentation',*flags,
        '-I'+str(a.out/'inc'),str(a.out/'verify-prepared-runtime.cpp'),
        *map(str,sources),'-o',str(binary)]
    for label,args in (('build',cmd),('run',[str(binary)])):
        result=subprocess.run(args,text=True,capture_output=True)
        (a.out/f'runtime-{mode}-{label}.log').write_text(result.stdout+result.stderr)
        result.check_returncode()
        if label=='run':print(result.stdout,end='')
files={str(f.relative_to(a.out)):hashlib.sha256(f.read_bytes()).hexdigest()
    for f in a.out.rglob('*') if f.is_file() and f.name!='sha256.json'
    and f.name not in ('verify-optimized','verify-sanitized','runtime-optimized','runtime-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(files,indent=2)+'\n')

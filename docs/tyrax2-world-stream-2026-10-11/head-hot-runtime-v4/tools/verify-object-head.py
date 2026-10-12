"""A33 whole post-wrap head template versus copied actual SDK inline writers."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('--sdk',type=Path,required=True)
a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
for name in ('tyrax2-object-head.hpp','verify-object-head.cpp','verify-object-head.py'):
    shutil.copy2(repo/'tools'/name,a.out/name)
include=a.out/'inc';include.mkdir()
roots=[a.sdk/'ee/include',a.sdk/'common/include']
todo=['packet2_utils.h'];pins={}
while todo:
    name=todo.pop()
    if name in pins:continue
    matches=[root/name for root in roots if (root/name).is_file()]
    if not matches:continue
    src=matches[0];dest=include/name;dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(src,dest);pins[name]=hashlib.sha256(src.read_bytes()).hexdigest()
    todo.extend(re.findall(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]',src.read_text(),re.M))
(a.out/'sdk-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
for mode,flags in (('optimized',['-O2']),('sanitized',['-O1','-g',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
    binary=a.out/f'verify-{mode}'
    # The actual SDK includes target-address casts in unused inline helpers.
    args=['g++','-std=c++17','-D_EE','-fpermissive',*flags,'-I'+str(include),
        str(a.out/'verify-object-head.cpp'),'-o',str(binary)]
    for stage,cmd in (('build',args),('run',[str(binary)])):
        r=subprocess.run(cmd,text=True,capture_output=True)
        (a.out/f'{mode}-{stage}.log').write_text(r.stdout+r.stderr)
        r.check_returncode()
        if stage=='run':print(r.stdout,end='')
inv={str(f.relative_to(a.out)):hashlib.sha256(f.read_bytes()).hexdigest()
     for f in a.out.rglob('*') if f.is_file() and f.name not in ('verify-optimized','verify-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(inv,indent=2)+'\n')
print('PASS copied actual SDK headers',len(pins),'files; host ABI, no native/raster/FPS proof')

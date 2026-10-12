"""Freeze/test direct compiler components against the preserved owning/flatten oracles."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
a=p.parse_args()
assert not a.out.exists()
a.out.mkdir(parents=True)
repo=Path(__file__).resolve().parent
for name in ('tyrax2-world-cell-direct.hpp','tyrax2-world-cell-plan.hpp',
             'tyrax2-world-strip-plan.hpp','tyrax2-world-body.hpp',
             'verify-world-cell-direct.cpp','verify-world-cell-direct.py'):
    shutil.copy2(repo/name,a.out/name)
for mode,flags in [('optimized',['-O2']),
                   ('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    binary=a.out/f'verify-{mode}'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,
                    str(a.out/'verify-world-cell-direct.cpp'),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
    (a.out/f'{mode}.log').write_text(result.stdout+result.stderr)
    print(mode,result.stdout.strip())
pins={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.out.iterdir()
      if f.is_file() and not f.name.startswith('verify-optimized') and not f.name.startswith('verify-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(pins,indent=2)+'\n')
print('PASS component inventory',len(pins),'files; not actual SDK/VU/PS2 timing proof')

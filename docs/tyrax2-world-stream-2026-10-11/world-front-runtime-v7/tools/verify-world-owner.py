"""Compile the explicit owner against a preserved actual engine bank table."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('--engine-inc',type=Path,required=True)
p.add_argument('--compiler',default='g++')
a=p.parse_args()
assert not a.out.exists()
a.out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
for name in ('tyrax2-world-owner.hpp','verify-world-owner.cpp',
             'tyrax2-world-strip-plan.hpp','verify-world-owner.py'):
    shutil.copy2(repo/'tools'/name,a.out/name)
for rel in ('renderer/core/paths/path1/frame_chain_arena.hpp',
            'renderer/core/paths/path1/vif1_chain_check.hpp','debug/hardware_trace.hpp'):
    dest=a.out/'inc'/rel;dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(a.engine_inc/rel,dest)
(a.out/'inc/tamtypes.h').write_text('#pragma once\n#include <stdint.h>\n'
 'typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;\n'
 'typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;typedef int64_t s64;\n')
for mode,flags in (('optimized',['-O2']),
                   ('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
    binary=a.out/f'verify-{mode}'
    subprocess.run([a.compiler,'-std=c++17','-Wall','-Wextra','-Werror',
        '-DTYRA_HARDWARE_TRACE=0',*flags,'-I'+str(a.out/'inc'),
        str(a.out/'verify-world-owner.cpp'),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
    (a.out/f'{mode}.log').write_text(result.stdout+result.stderr)
    print(result.stdout,end='')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
files={str(f.relative_to(a.out)).replace('\\','/'):sha(f) for f in a.out.rglob('*')
       if f.is_file() and not f.name.startswith('verify-optimized') and not f.name.startswith('verify-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(files,indent=2)+'\n')
print('PASS actual-source owner inventory',len(files),'files; no PS2 lifecycle/speed claim')

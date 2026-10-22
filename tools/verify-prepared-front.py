"""Private A32 owned frontend facts against preserved actual engine bbox sources."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--compiler',default='g++')
a=p.parse_args()
assert not a.out.exists()
a.out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
for name in ('tyrax2-prepared-front.hpp','verify-prepared-front.cpp','verify-prepared-front.py'):
    shutil.copy2(repo/'tools'/name,a.out/name)
headers=('math/vec4.hpp','math/m4x4.hpp','math/math.hpp','math/plane.hpp',
         'renderer/core/3d/bbox/core_bbox.hpp','renderer/core/3d/bbox/core_bbox_frustum.hpp',
         'renderer/core/3d/bbox/render_bbox.hpp',
         'renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp')
sources=('renderer/core/3d/bbox/core_bbox.cpp','renderer/core/3d/bbox/render_bbox.cpp',
         'renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.cpp')
binding=json.loads((a.engine.parent.parent/'native-binding.json').read_text())
pins={}
for category,paths in (('inc',headers),('src',sources)):
    for rel in paths:
        name=f'tyra/engine/{category}/{rel}'
        digest=hashlib.sha256((a.engine/category/rel).read_bytes()).hexdigest()
        assert binding['files'][name]==digest,name
        pins[name]=digest
(a.out/'source-pins.json').write_text(json.dumps({'sourceElfSha256':binding['elfSha256'],
    'files':pins,'runtimeCandidate':False},indent=2)+'\n')
for rel in headers:
    dest=a.out/'inc'/rel;dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(a.engine/'inc'/rel,dest)
for rel in sources:
    dest=a.out/'src'/rel;dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(a.engine/'src'/rel,dest)
# Replace only the VU0 macro-assembly leaf. All bbox algorithms stay copied.
core=a.out/'src'/sources[0]
original=core.read_text()
start=original.index('inline void vec4ArrayMinMax(')
end=original.index('\n}\n',start)+2
shim='''inline void vec4ArrayMinMax(const Vec4* v,u32 n,Vec4* low,Vec4* high){
  *low=v[0];*high=v[0];for(u32 i=0;i<n;++i){
    if(v[i].x<low->x)low->x=v[i].x;if(v[i].y<low->y)low->y=v[i].y;if(v[i].z<low->z)low->z=v[i].z;
    if(v[i].x>high->x)high->x=v[i].x;if(v[i].y>high->y)high->y=v[i].y;if(v[i].z>high->z)high->z=v[i].z;
  }
}'''
core.write_text(original[:start]+shim+original[end:])
(a.out/'core_bbox.original.cpp').write_text(original)
(a.out/'inc/tamtypes.h').write_text('''#pragma once
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;typedef int64_t s64;
''')
(a.out/'inc/math3d.h').write_text('#pragma once\ntypedef float VECTOR[4];typedef float MATRIX[16];\n')
(a.out/'inc/debug').mkdir(exist_ok=True)
(a.out/'inc/debug/debug.hpp').write_text('#pragma once\n#include <cassert>\n#define TYRA_ASSERT(condition,...) assert(condition)\n')
for mode,flags in (('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
    binary=a.out/f'verify-{mode}'
    cmd=[a.compiler,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-ignored-qualifiers',
         '-Wno-deprecated-copy','-Wno-misleading-indentation',*flags,'-I'+str(a.out/'inc'),
         str(a.out/'verify-prepared-front.cpp'),*[str(a.out/'src'/s) for s in sources],'-o',str(binary)]
    compiled=subprocess.run(cmd,text=True,capture_output=True)
    (a.out/f'{mode}-build.log').write_text(compiled.stdout+compiled.stderr)
    compiled.check_returncode()
    result=subprocess.run([str(binary)],text=True,capture_output=True)
    (a.out/f'{mode}.log').write_text(result.stdout+result.stderr)
    result.check_returncode();print(result.stdout,end='')
files={str(f.relative_to(a.out)):hashlib.sha256(f.read_bytes()).hexdigest()
       for f in a.out.rglob('*') if f.is_file() and f.name not in ('verify-optimized','verify-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(files,indent=2)+'\n')
print('PASS actual-source inventory',len(files),'files; finite host inputs, VU0 leaf/ABI shim, no target timing')

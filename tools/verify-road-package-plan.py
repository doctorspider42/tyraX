"""Cold road jobs against the bound, unchanged actual StaPip packager algorithms."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('--engine', type=Path, required=True)
a = p.parse_args()
assert not a.out.exists()
binding = json.loads((a.engine.parent.parent / 'native-binding.json').read_text())
headers = ('math/vec4.hpp', 'math/m4x4.hpp', 'math/math.hpp', 'math/plane.hpp',
           'renderer/core/3d/bbox/core_bbox.hpp',
           'renderer/core/3d/bbox/core_bbox_frustum.hpp',
           'renderer/core/3d/bbox/render_bbox.hpp',
           'renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp')
packager = 'renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager'
sources = ('renderer/core/3d/bbox/core_bbox.cpp',
           'renderer/core/3d/bbox/render_bbox.cpp',
           'renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.cpp',
           packager + '.cpp')
route_sources = ('renderer/3d/pipeline/static/core/stapip_core.cpp',
                 'renderer/3d/pipeline/static/core/stapip_qbuffer.cpp',
                 'renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')
pins = {}
for category, paths in (('inc', (*headers, packager + '.hpp')), ('src', (*sources, *route_sources))):
    for rel in paths:
        key = f'tyra/engine/{category}/{rel}'
        digest = hashlib.sha256((a.engine / category / rel).read_bytes()).hexdigest()
        assert binding['files'][key] == digest, key
        pins[key] = digest
a.out.mkdir(parents=True)
repo = Path(__file__).resolve().parent
for name in ('tyrax2-road-package-plan.hpp', 'verify-road-package-plan.cpp',
             'verify-road-package-plan.py'):
    shutil.copy2(repo / name, a.out / name)
for category, paths in (('inc', headers), ('src', sources)):
    for rel in paths:
        dest = a.out / category / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(a.engine / category / rel, dest)
shutil.copy2(a.engine / 'inc' / (packager + '.hpp'), a.out / 'packager.original.hpp')
for rel in route_sources:
    shutil.copy2(a.engine / 'src' / rel, a.out / Path(rel).name)
(a.out / 'source-pins.json').write_text(json.dumps({
    'sourceElfSha256': binding['elfSha256'], 'files': pins,
    'runtimeCandidate': False, 'hostShims': ['VU0 minmax leaf', 'ABI declarations',
                                          'trace/debug', 'all probes disabled']}, indent=2) + '\n')

def generated(rel, text):
    target = a.out / 'inc' / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)

generated('tamtypes.h', '''#pragma once
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;typedef int64_t s64;
''')
generated('math3d.h', '#pragma once\ntypedef float VECTOR[4];typedef float MATRIX[16];\n')
generated('debug/debug.hpp', '#pragma once\n#include <cassert>\n#define TYRA_ASSERT(condition,...) assert(condition)\n')
generated('debug/hardware_trace.hpp', '#pragma once\nnamespace HardwareTrace {struct Scope {explicit Scope(const char*){}};}\n')
generated('renderer/3d/pipeline/static/core/stapip_probes.hpp', '''#pragma once
#define TYRA_STAPIP_PROBE_ACCEPT_ALL 0
#define TYRA_STAPIP_PROBE_COARSE_CLASSIFY 0
#define TYRA_STAPIP_ATTRIB 0
''')
generated(packager + '.hpp', '''#pragma once
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp"
#include "math/plane.hpp"
#include <vector>
namespace Tyra {
struct Renderer3DFrustumPlanes;
struct StaPipTextureBag {const Vec4* coordinates=nullptr;};
struct StaPipColorBag {const Vec4* many=nullptr;};
struct StaPipLightingBag {const Vec4* normals=nullptr;};
struct StaPipBag {
 u32 count=0;const Vec4* vertices=nullptr;
 StaPipTextureBag* texture=nullptr;StaPipColorBag* color=nullptr;
 StaPipLightingBag* lighting=nullptr;
};
struct StaPipBagPackage {
 StaPipBag* bag=nullptr;const Vec4 *vertices=nullptr,*sts=nullptr,*colors=nullptr,*normals=nullptr;
 u16 size=0;u32 indexOf1By3BBox=0,endIndexOf1By3BBox=0;
 u8 clipPlaneMask=0;bool guardBandOnly=false;CoreBBoxFrustum isInFrustum=OUTSIDE_FRUSTUM;
};
class StaPipBagPackager {
 public:
 Renderer3DFrustumPlanes* frustumPlanes=nullptr;u32 maxVertCount=0;
 StaPipBagPackagesBBox* renderBBox=nullptr;
 const Plane *objectSpacePlanes=nullptr,*clipObjectSpacePlanes=nullptr;
 bool capturePlaneMasks=false;
 std::vector<StaPipBagPackage> bagPackagesPool,splitPackagesPool;
 StaPipBagPackager();~StaPipBagPackager();void init(Renderer3DFrustumPlanes*);
 StaPipBagPackage* create(u16*,StaPipBag*,u16);
 StaPipBagPackage* create(u16*,const StaPipBagPackage&,u16);
 CoreBBoxFrustum checkFrustum(const StaPipBagPackage&,u8*,bool*);
 void setMaxVertCount(const u32&);
};
}
''')
core = a.out / 'src' / sources[0]
original = core.read_text()
start = original.index('inline void vec4ArrayMinMax(')
end = original.index('\n}\n', start) + 2
shim = '''inline void vec4ArrayMinMax(const Vec4* v,u32 n,Vec4* low,Vec4* high){
 *low=v[0];*high=v[0];for(u32 i=0;i<n;++i){
  if(v[i].x<low->x)low->x=v[i].x;if(v[i].y<low->y)low->y=v[i].y;if(v[i].z<low->z)low->z=v[i].z;
  if(v[i].x>high->x)high->x=v[i].x;if(v[i].y>high->y)high->y=v[i].y;if(v[i].z>high->z)high->z=v[i].z;
 }
}'''
core.write_text(original[:start] + shim + original[end:])
(a.out / 'core_bbox.original.cpp').write_text(original)
for mode, flags in [('optimized', ['-O2']),
                    ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined',
                                   '-fno-omit-frame-pointer'])]:
    binary = a.out / f'verify-{mode}'
    compiled = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-Wno-ignored-qualifiers', '-Wno-deprecated-copy', '-Wno-misleading-indentation',
        *flags, '-I' + str(a.out / 'inc'), str(a.out / 'verify-road-package-plan.cpp'),
        *[str(a.out / 'src' / s) for s in sources], '-o', str(binary)],
        capture_output=True, text=True)
    (a.out / f'{mode}-build.log').write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    (a.out / f'{mode}.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(mode, result.stdout.strip())
files = {str(f.relative_to(a.out)): hashlib.sha256(f.read_bytes()).hexdigest()
         for f in a.out.rglob('*') if f.is_file() and
         f.name not in ('verify-optimized', 'verify-sanitized')}
(a.out / 'sha256.json').write_text(json.dumps(files, indent=2) + '\n')
print('PASS frozen actual-source component; NOT native SDK, DMA lease or PS2 net evidence')

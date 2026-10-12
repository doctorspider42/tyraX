"""Private actual-EE SDK split qualification; no candidate GPU routing or FPS claim."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path)
p.add_argument('--physical-attempt',type=Path,required=True)
a=p.parse_args()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
b=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==b['elfSha256']
for platform,location in [('emulator',a.source),('ps2',a.physical_attempt)]:
    gate=json.loads((location/f'world-cell-price-{platform}.json').read_text())
    assert gate['valid'] and gate['variant']=='paint-cells' and gate['elfSha256']==b['elfSha256']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
for original,dest in [('tyrax2-road-package-sdk.hpp','road_package_sdk.hpp'),
                      ('tyrax2-road-package-sdk-check.hpp','road_package_sdk_check.hpp')]:
    shutil.copy2(repo/original,a.out/'tyra/engine/inc/debug'/dest)
writer=a.out/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
t=writer.read_text()
needle='#include "debug/world_cell_sdk_check.hpp"'
assert t.count(needle)==1
t=t.replace(needle,needle+'\n#include "debug/road_package_sdk_check.hpp"')
needle='    WorldCellSdkCheck::run(untextured,textured);'
assert t.count(needle)==1
t=t.replace(needle,'''    RoadPackageSdkCheck::run(bag,getCullProgramByBag(&plain),untextured.program,
      getCullProgramByBag(bag),textured.program,*prim,worldEpoch,bufferSize);
'''+needle)
writer.write_text(t)
manifest={
    'baseElfSha256':b['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'headers':{n:sha(repo/n) for n in ('tyrax2-road-package-sdk.hpp','tyrax2-road-package-sdk-check.hpp')},
    'completionMarker':'ROADSDKDONE','expected':{'bodies':48,'sdkComparisons':4128,'controls':192,'owned':48},
    'status':'UNQUALIFIED','limits':[
        'Actual compiled EE SDK routines run at cold setup; no new geometry is queued to GPU.',
        'Own geometry bytes, but not queue-bank/texture leases or production admission.',
        'Mask0 retains original all-six fallback; state/start variants compare with actual writer.',
        'No physical net timing or complete game raster qualification from this SDK-only test.']}
(a.out/'road-package-sdk-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED native SDK split fixture')

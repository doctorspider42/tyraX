"""A31 all-plane cold execution qualification, never a performance candidate."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(repo/'tools/tyrax2-world-body-fixture.py'),
                str(a.out),str(a.source)],check=True)
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-world-mask.hpp',engine/'inc/debug/world_mask.hpp')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text=renderer.read_text()
def once(old,new):
    global text
    assert text.count(old)==1,old
    text=text.replace(old,new)
once('#include "debug/world_body.hpp"',
     '#include "debug/world_body.hpp"\n#include "debug/world_mask.hpp"')
once('      auto compiled=emit(&copy);',
     '      copy.clipPlaneMask=63;\n      auto compiled=emit(&copy);')
once('      TYRA_ASSERT(compiled==reference,"A31 compiled SDK body differs");',
     '''      TYRA_ASSERT(WorldMask::sameBody(reference,compiled),"A31 all-plane body differs outside mask");
      if(reference[1].words[3]!=compiled[1].words[3])++WorldMask::changed;''')
renderer.write_text(text)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
text=physics.read_text()
once('#include "debug/world_body.hpp"',
     '#include "debug/world_body.hpp"\n#include "debug/world_mask.hpp"')
once('CoarseStrip::active=ClipTrial::mode==7;WorldBody::reset();',
     'CoarseStrip::active=ClipTrial::mode==7;WorldBody::reset();WorldMask::changed=0;')
once('    printf("WORLDCHAIN phase=%u arm=%u checks=%u words=%u liveHeads=1 coldOnly=1\\n",',
     '''    TYRA_ASSERT(arm==1?WorldMask::changed>0:WorldMask::changed==0,"A31 changed masks admission");
    printf("WORLDMASK phase=%u arm=%u changed=%u planes=63 coldOnly=1\\n",MotionProbe::phase,arm,WorldMask::changed);fflush(stdout);
    printf("WORLDCHAIN phase=%u arm=%u checks=%u words=%u liveHeads=1 coldOnly=1\\n",''')
physics.write_text(text)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
manifest=a.out/'clip-capacity-manifest.json'
plan=json.loads(manifest.read_text())
plan.update(worldAllPlanes=True,worldMaskSha256=sha(engine/'inc/debug/world_mask.hpp'),
            worldRendererSha256=sha(renderer),status='UNQUALIFIED all-plane cold route; NOT performance')
manifest.write_text(json.dumps(plan,indent=2)+'\n')
print(a.out,'UNQUALIFIED all-plane cold route')

"""Exercise the actual P06 header with a fake wrapping clock on the host."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(sys.argv[1]);path=root/'tyra/engine/inc/debug/motion_probe.hpp'
header=path.read_text()
q=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
for name in ('ExpandBuilds','ExpandCachedVerts','PartialBuild','PartialReplay','PartialBytes'):
    assert q.count('MotionProbe::add(MotionProbe::'+name)==1,name
old='inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }'
assert header.count(old)==1
host=header.replace(old,'inline U fake=0; inline U ticks(){return ++fake;}')
test=r'''
#include "probe.hpp"
#include <cassert>
int main(){
using namespace MotionProbe;
offset=1335;
{RoadTrace::Bag b(true,4,75);RoadTrace::Wait w(RoadTrace::VifWait);add(Bags);}
assert(fake==0 && RoadTrace::count==0 && RoadTrace::metrics[RoadTrace::WaitCalls]==0);
clocks=true;sample=true;
{RoadTrace::Camera v(1);RoadTrace::Bag b(true,4,75);add(Bags);add(CullChunks,2);
 {RoadTrace::Wait w(RoadTrace::VifWait);}
}
assert(RoadTrace::count==1 && RoadTrace::view==0);
auto& r=RoadTrace::records[0];
assert(r.id==4 && r.view==1 && r.elapsed==3 && r.wait==1 && r.c[Bags]==1 && r.c[CullChunks]==2);
assert(counters[Bags]==0 && counters[CullChunks]==0);
RoadTrace::save(0);assert(RoadTrace::frameMetrics[0][RoadTrace::VifWait]==1);
offset=1400;{RoadTrace::Bag b(true,4,75);}assert(RoadTrace::count==1);
offset=1575;assert(RoadTrace::window());
fake=0xfffffffeU;{RoadTrace::Wait w(RoadTrace::GsWait);}
assert(RoadTrace::metrics[RoadTrace::GsWait]==1);
{RoadTrace::Wait a(RoadTrace::GsWait);RoadTrace::Wait b(RoadTrace::GsWait);}
assert(RoadTrace::metrics[RoadTrace::Errors]==1);RoadTrace::save(1);assert(!valid);
valid=true;RoadTrace::begin();RoadTrace::count=4096;
{RoadTrace::Bag b(true,4,75);}assert(!valid && RoadTrace::count==4096);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'probe.hpp').write_text(host);(p/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
result=dict(passed=True,headerSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
 checks=['off clock/counters','stable ID/view restore','isolated per-bag counts','windows/repeat','frame save',
         '32-bit wrap','nested wait rejection','overflow rejection','ASan/UBSan'])
(root/'road-trace-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

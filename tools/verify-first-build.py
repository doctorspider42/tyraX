"""Host-check the actual A02a event recorder and bounded source hooks."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1])
subprocess.run([sys.executable, str(Path(__file__).with_name('verify-road-trace.py')), str(root)], check=True)
path = root/'tyra/engine/inc/debug/motion_probe.hpp'
header = path.read_text()
clock = 'inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }'
assert header.count(clock) == 1
header = header.replace(clock, 'inline U fake=0; inline U ticks(){return ++fake;}')
source = (root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
for hook in ('BuildScope firstBuild', 'BuildScope finishBuild', '::PayloadQwords,qwc',
             '::HeaderQwords)', '::GrowthBlocks)', '::CapacityAddedQwords,',
             '::ArenaBytes,qw*16', '::ArenaBuilds)'):
    assert source.count(hook) == 1, hook
test = r'''
#include "probe.hpp"
#include <cassert>
int main(){
using namespace MotionProbe;
using namespace MotionProbe::RoadTrace;
offset=1335;
{Bag b(true,9,1074);BuildScope s(BakeTicks);buildMetric(PayloadQwords,20);}
assert(fake==0 && count==0);
clocks=true;sample=true;timing[PrefixObject]=30;
{Bag b(true,9,1074);
 {BuildScope s(BakeTicks);buildMetric(PayloadQwords,20);buildMetric(HeaderQwords,3);}
 {BuildScope s(FinalizeTicks);buildMetric(ArenaBytes,368);buildMetric(ArenaBuilds);}
 timing[PrefixObject]+=15;
}
auto &r=records[0];
assert(r.elapsed==5 && r.b[BakeTicks]==1 && r.b[FinalizeTicks]==1);
assert(r.b[BakeCalls]==1 && r.b[FinalizeCalls]==1);
assert(r.b[PayloadQwords]==20 && r.b[HeaderQwords]==3 && r.b[ArenaBytes]==368);
assert(r.t[PrefixObject]==15 && currentRecord==nullptr);
U prev=fake;{BuildScope s(BakeTicks);buildMetric(PayloadQwords,99);}
assert(fake==prev && r.b[PayloadQwords]==20);
offset=1400;{Bag b(true,9,1074);BuildScope s(BakeTicks);}assert(count==1);
offset=1575;fake=0xfffffffdU;
{Bag b(true,9,1074);{BuildScope s(BakeTicks);} }
assert(records[1].elapsed==3 && records[1].b[BakeTicks]==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'probe.hpp').write_text(header)
    (p/'test.cpp').write_text(test)
    subprocess.run(['g++', '-std=c++17', '-O2', '-fsanitize=address,undefined',
                    str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
result = dict(passed=True, headerSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
              checks=['off/no active bag no clocks', 'two disjoint scopes', 'existing timing deltas',
                      'bounded windows', '32-bit wrap', 'all source hooks unique', 'ASan/UBSan'])
(root/'first-build-host.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result))

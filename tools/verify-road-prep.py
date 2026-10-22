"""Host qualification of the actual private road clock header and ownership sites."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1]).resolve()
engine = root/'tyra/engine'
path = engine/'inc/debug/motion_probe.hpp'
header = path.read_text()
clock = 'inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }'
assert header.count(clock) == 1
host = header.replace(clock, 'inline U fakeTick=0; inline U ticks(){return ++fakeTick;}')
test = r'''
#include "probe.hpp"
#include <cassert>
int main() {
  using namespace MotionProbe;
  { Scope road(false,true); Bucket packet(PacketTicks); }
  assert(fakeTick==0 && timing[CoreCalls]==0);
  clocks=true;
  { Scope other(false,false); Scope dispatch(true,coreDepth!=0);
    Bucket packet(PacketTicks); }
  assert(fakeTick==0 && timing[CoreCalls]==0 && timing[PacketCalls]==0);
  { Scope road(false,true);
    { Bucket prefix(SubmitTicks); { Bucket copy(PayloadTicks); } }
    { Scope dispatch(true,coreDepth!=0); Bucket packet(PacketTicks); }
  }
  assert(timing[CoreTicks]==9 && timing[PartialTicks]==3);
  assert(timing[SubmitTicks]==2 && timing[PayloadTicks]==1 && timing[PacketTicks]==1);
  assert(timing[CoreCalls]==1 && timing[PartialCalls]==1 && timing[PayloadCalls]==1);
  assert(coreDepth==0 && partialDepth==0 && currentBucket==nullptr && timing[ScopeErrors]==0);
  memset(timing,0,sizeof(timing));fakeTick=0xfffffffeU;
  { Scope road; Bucket packet(PacketTicks); }
  assert(timing[CoreTicks]==3 && timing[PacketTicks]==1 && timing[ScopeErrors]==0);
  { Scope road; Scope recursive; }
  assert(timing[ScopeErrors]==1 && coreDepth==0);
}
'''
if 'struct Prefix {' in header:
    test = test.replace('  assert(timing[ScopeErrors]==1 && coreDepth==0);', '''  assert(timing[ScopeErrors]==1 && coreDepth==0);
  memset(timing,0,sizeof(timing));fakeTick=0;clocks=false;
  { Prefix p(true); p.next(PrefixBounds); p.finish(); }
  assert(fakeTick==0);
  clocks=true;
  { Prefix p(false); p.next(PrefixBounds); }
  assert(fakeTick==0);
  { Scope road; Prefix p(true); p.next(PrefixBounds); p.next(PrefixTexture);
    p.next(PrefixLight); p.next(PrefixObject); p.finish(); }
  assert(timing[CoreTicks]==7 && timing[ScopeErrors]==0);
  for(U k=PrefixHead;k<=PrefixObject;++k) assert(timing[k]==1);
  fakeTick=0xfffffffeU;
  { Prefix p(true); }
  assert(timing[PrefixHead]==2);
''')
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    (folder/'probe.hpp').write_text(host)
    (folder/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra',str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)
sources = '\n'.join(p.read_text() for p in (root/'game/src').rglob('*.cpp'))
assert sources.count('c.bag->diagnosticRoad = c.owner == -3;') == 1
assert sources.count('TYRA_ASSERT(c.bag->diagnosticRoad,"Road preparation ownership");') == 1
core = (engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
assert core.count('MotionProbe::Scope probeCore(false, bag->diagnosticRoad);') == 1
assert core.count('MotionProbe::Scope probeDispatch(true, MotionProbe::coreDepth != 0);') == 1
result = dict(passed=True,headerSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
              prefixPartition='struct Prefix {' in header,
              checks=['disabled clocks','non-road calls excluded','prefix buckets included',
                      'exclusive nested buckets','dispatch containment','balanced scopes',
                      'u32 wrap','recursion negative control','deferred bag ownership sites'])
(root/'road-prep-host.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))

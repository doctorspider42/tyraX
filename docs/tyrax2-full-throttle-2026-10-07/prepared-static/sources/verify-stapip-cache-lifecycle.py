"""Host tests of extracted actual baked-cache ownership and invalidation methods.

The DMA boundary is a deterministic reader mock, not a hardware timing model.
Only pointer hashing is widened for the host ABI. Packet writers are tested by
the separate target byte oracles and frame-arena host controls.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'vendor/tyra/engine'
header = (BASE / 'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp').read_text()
source = (BASE / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
declarations = header[header.index('struct StaPipBakedEntry {'):]
declarations = declarations[:declarations.index('#endif  // TYRA_STAPIP_BAKED_STREAM')]
methods = source[source.index('StaPipBakedStreams::StaPipBakedStreams()'):]
methods = methods[:methods.index('#endif  // TYRA_STAPIP_BAKED_STREAM')]
assert methods.count('reinterpret_cast<u32>(vertices)') == 1
methods = methods.replace('reinterpret_cast<u32>(vertices)', 'reinterpret_cast<uintptr_t>(vertices)')
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
struct alignas(16) qword_t { uint64_t dw[2]; };
#define TYRA_STAPIP_BAKED_BUDGET_QW 262144
#define TYRA_STAPIP_PREPARED_STATIC 1
namespace Vif1Queue {
struct Lease { bool retired=false; unsigned readers=0; };
std::map<const void*, Lease> spans;
bool registerImmutableSpan(const void* p, u32) {
  return spans.emplace(p, Lease{}).second;
}
void retireImmutableSpan(const void* p) {
  if(p) { assert(spans.count(p)); spans.at(p).retired=true; }
}
bool collectRetiredImmutableSpan(const void* p) {
  if(!p || !spans.count(p)) return true;
  auto& s=spans.at(p); assert(s.retired);
  if(s.readers) return false;
  spans.erase(p); return true;
}
void drain() { for(auto& s:spans) s.second.readers=0; }
}
'''
tests = r'''
static void publish(StaPipBakedStreams& cache, StaPipBakedEntry* e, u32 qw=16) {
  assert(e && !e->complete && cache.reserve(qw));
  e->raw.reset(new u8[qw*16]);
  e->arena=reinterpret_cast<qword_t*>(e->raw.get());
  e->arenaQw=qw; e->built=e->packages; e->complete=1;
  std::memset(e->arena,0x5a,qw*16);
  assert(Vif1Queue::registerImmutableSpan(e->arena,qw));
}
static StaPipBakedEntry key() {
  StaPipBakedEntry k{};
  k.vertices=reinterpret_cast<void*>(0x1000);
  k.sts=reinterpret_cast<void*>(0x2000);
  k.colors=reinterpret_cast<void*>(0x3000);
  k.normals=reinterpret_cast<void*>(0x4000);
  k.program=reinterpret_cast<void*>(0x5000);
  k.count=75; k.maxVertCount=75; k.packages=1;
  return k;
}
int main() {
  unsigned checks=0;
  // Every field of the actual equality key, including the expansion namespace.
  for(unsigned field=0;field<16;++field) {
    StaPipBakedStreams cache;
    auto a=key(); auto* original=cache.acquire(a); publish(cache,original);
    assert(cache.acquire(a)==original && original->complete); ++checks;
    for(int i=0;i<3;++i)cache.onFrameEnd();
    auto b=key();
    switch(field) {
      case 0:b.vertices=reinterpret_cast<void*>(0x6000);break;
      case 1:b.sts=nullptr;break;
      case 2:b.colors=nullptr;break;
      case 3:b.normals=nullptr;break;
      case 4:b.program=nullptr;break;
      case 5:++b.count;break;
      case 6:b.maxVertCount|=0x80000000u;break;
      case 7:++b.bboxVersion;break;
      case 8:++b.contentVersion;break;
      case 9:++b.primKey;break;
      case 10:++b.depthScaleBits;break;
      case 11:++b.programAddr;break;
      case 12:b.singleColor=1;break;
      case 13:b.stripped=1;break;
      case 14:b.count=150;b.packages=2;break;
      case 15:b.maxVertCount=72;break;
    }
    auto* changed=cache.acquire(b);
    assert(changed && !changed->complete); ++checks;
    publish(cache,changed);
    assert(cache.acquire(b)==changed && changed->complete); ++checks;
  }
  assert(Vif1Queue::spans.empty());
  {
    StaPipBakedStreams cache; auto k=key(); auto* e=cache.acquire(k);
    publish(cache,e); const auto* old=e->arena;
    Vif1Queue::spans.at(old).readers=1;
    cache.clear(); assert(cache.getBytes()==0); ++checks;
    for(int i=0;i<4;++i)cache.onFrameEnd();
    assert(Vif1Queue::spans.at(old).retired);
    assert(reinterpret_cast<const u8*>(old)[0]==0x5a); ++checks;
    auto* next=cache.acquire(k); publish(cache,next);
    assert(next->arena!=old); ++checks;
    Vif1Queue::spans.at(old).readers=0;
    for(int i=0;i<2;++i)cache.onFrameEnd();
    assert(!Vif1Queue::spans.count(old)); ++checks;
    for(int i=0;i<StaPipBakedStreams::kLifetimeFrames;++i)cache.onFrameEnd();
    assert(cache.getBytes()==0); ++checks;
    assert(!cache.acquire(k)->complete); ++checks;
  }
  {
    StaPipBakedStreams cache; auto k=key(); auto* e=cache.acquire(k);
    publish(cache,e,StaPipBakedStreams::kMaxQwords);
    assert(!cache.reserve(1)); ++checks; // current frame is never evicted
    cache.onFrameEnd();
    auto other=key(); other.maxVertCount|=0x80000000u;
    auto* expanded=cache.acquire(other); publish(cache,expanded);
    assert(cache.getBytes()==16*16); ++checks;
    assert(!cache.reserve(StaPipBakedStreams::kMaxQwords+1)); ++checks;
  }
  {
    StaPipBakedStreams cache; auto k=key(); auto* e=cache.acquire(k);
    publish(cache,e);
    ++k.contentVersion;
    assert(cache.acquire(k)==nullptr); ++checks; // rapid rewrite: ordinary fallback
    for(unsigned i=1;i<StaPipBakedStreams::kSettleFrames;++i)
      assert(cache.acquire(k)==nullptr);
    auto* settled=cache.acquire(k);
    assert(settled && !settled->complete); ++checks;
    publish(cache,settled);
  }
  assert(Vif1Queue::spans.empty());
  {
    StaPipBakedStreams cache; auto k=key(); auto* e=cache.acquire(k);
    publish(cache,e,StaPipBakedStreams::kMaxQwords); e->scenePinned=true;
    for(int i=0;i<2*StaPipBakedStreams::kLifetimeFrames;++i)cache.onFrameEnd();
    assert(cache.getBytes()==StaPipBakedStreams::kMaxQwords*16); ++checks;
    assert(!cache.reserve(1)); ++checks;
    cache.releaseScenePins(); cache.onFrameEnd();
    auto other=key(); other.vertices=reinterpret_cast<void*>(0x7000);
    publish(cache,cache.acquire(other));
    assert(cache.getBytes()==16*16); ++checks;
  }
  assert(Vif1Queue::spans.empty());
  std::printf("PASS actual baked cache lifecycle: %u checks; mocked DMA readers\n",checks);
}
'''
with tempfile.TemporaryDirectory(prefix='stapip-cache-') as tmp:
    path = Path(tmp)
    (path/'test.cpp').write_text(prefix+declarations+methods+tests)
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                    str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

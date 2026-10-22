"""Compile P09's actual loop against host slice/queue mocks with sanitizers."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(sys.argv[1])
core=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
start=core.index('      const bool selected = pkg.bag->diagnosticRoadId')
end=core.index('\n    } else {',start)
body=core[start:end]
test=r'''
#include <cassert>
#include <vector>
#include <cstdint>
#define TYRA_STAPIP_EXPANSION_CACHE 1
#define TYRA_ASSERT(a,b) assert(a)
using u32=uint32_t;
namespace MotionProbe { u32 offset,mode,onceLoops,onceAcquire,onceReuse,onceChecks,onceFailed,onceFallback;u32 variant(){return mode;} }
struct Bag{u32 diagnosticRoadId;bool stripped=true;};
struct StaPipBagPackage { Bag* bag;const int *vertices,*sts,*colors,*normals;u32 size;unsigned clipPlaneMask;};
struct Buffer {
 Bag* bag;const int *vertices,*sts,*colors,*normals;u32 size;unsigned clipPlaneMask;bool stripped;
 void fillByPointer(const StaPipBagPackage& p){bag=p.bag;vertices=p.vertices;sts=p.sts;colors=p.colors;normals=p.normals;size=p.size;clipPlaneMask=p.clipPlaneMask;stripped=bag->stripped;}
 void fillByStripExpand(const StaPipBagPackage& p,u32 first,u32 n){
   auto s=p;s.vertices+=first*3;if(s.sts)s.sts+=first*3;if(s.colors)s.colors+=first*3;if(s.normals)s.normals+=first*3;s.size=n*3;fillByPointer(s);stripped=false;
 }
};
struct Queue {
 Buffer buffer;bool available=true;unsigned calls=0;std::vector<Buffer> draws;
 Buffer* getBuffer(){return &buffer;}
 bool fillCachedStrip(Buffer* b,const StaPipBagPackage& p,u32 first,u32 n){++calls;if(!available)return false;b->fillByStripExpand(p,first,n);return true;}
 void clip(Buffer* b){draws.push_back(*b);}
} qbufferRenderer;
void run(const StaPipBagPackage& pkg,u32 triangles,u32 trisPerChunk){
'''+body+r'''
}
int main(){
 int data[4][512];for(int s=0;s<4;++s)for(int j=0;j<512;++j)data[s][j]=s*10000+j;
 for(u32 count=3;count<=75;++count)for(u32 step=1;step<=25;++step)
 for(u32 streams=0;streams<8;++streams)for(u32 mode:{0u,6u})for(u32 id:{0u,24u,34u})for(bool available:{false,true}){
  Bag bag{id};StaPipBagPackage p{&bag,data[0],streams&1?data[1]:nullptr,streams&2?data[2]:nullptr,streams&4?data[3]:nullptr,count,0x25};
  qbufferRenderer.draws.clear();qbufferRenderer.calls=0;qbufferRenderer.available=available;
  MotionProbe::mode=mode;MotionProbe::offset=1000;
  run(p,count-2,step);
  const auto& ds=qbufferRenderer.draws;assert(ds.size()==(count-2+step-1)/step);
  unsigned first=0;
  for(const auto& b:ds){unsigned n=std::min(step,count-2-first);assert(b.size==n*3&&b.bag==&bag&&!b.stripped&&b.clipPlaneMask==0x25);
   assert(b.vertices==data[0]+first*3);assert(b.sts==(p.sts?p.sts+first*3:nullptr));
   assert(b.colors==(p.colors?p.colors+first*3:nullptr));assert(b.normals==(p.normals?p.normals+first*3:nullptr));first+=n;
  }
  assert(first==count-2);
  qbufferRenderer.calls=0;qbufferRenderer.draws.clear();MotionProbe::offset=1500;
  run(p,count-2,step);
  assert(qbufferRenderer.calls==((mode==6&&id)?1:ds.size()));
 }
 assert(MotionProbe::onceReuse>0&&MotionProbe::onceReuse==MotionProbe::onceChecks&&MotionProbe::onceFailed==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
result=dict(passed=True,cases=73*25*8*2*3*2,loopSha256=hashlib.sha256(body.encode()).hexdigest(),
    scope='actual loop, mocked acquisition/writers: all stream combinations, tails, selected/control, first-miss fallback; not raster')
(root/'strip-once-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

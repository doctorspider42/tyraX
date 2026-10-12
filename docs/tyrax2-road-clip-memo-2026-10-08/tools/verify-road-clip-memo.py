"""Exercise actual memo and original plane bodies with host mutation controls."""
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root=Path(sys.argv[1]).resolve()
repo=Path(__file__).resolve().parents[1]
rel='engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
candidate=(root/'tyra'/rel).read_text(encoding='utf-8-sig')
original=(repo/'vendor/tyra'/rel).read_text(encoding='utf-8-sig')


def body(text):
    start=text.index('void StaPipCore::computeClipObjectSpacePlanes(')
    end=text.index('\n// Modified by TyraX: guard-band routing',start)
    return text[start:end]


defines=(root/'tyra/engine/inc/renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h').read_text()
band=re.search(r'#define VU1_CLIP_XY_BAND ([^\n]+)',defines)[1]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <random>
#define TYRA_ASSERT(test,message) assert(test)
using u8=uint8_t;
struct Vec4 { float x=0,y=0,z=0,w=0; };
struct Plane { Vec4 normal; float distance=0; };
struct M4x4 { float data[16]{}; };
namespace PlanesClipAlgorithm { float clipMargin=-10; }
namespace MotionProbe { unsigned offset=1200,mode=0,memoCalls=0,memoHits=0,memoChecks=0; unsigned variant(){return mode;} }
struct Settings { float near=1,far=1000; float getNear(){return near;} float getFar(){return far;} };
struct Renderer { Settings settings; Settings& getSettings(){return settings;} };
struct StaPipCore {
  Renderer* rendererCore;
  Plane clipObjectSpacePlanes[8];
  bool diagnosticRoadCall=false,roadClipMemoValid=false;
  M4x4 roadClipMemoMvp;
  float roadClipMemoNear=0,roadClipMemoFar=0;
  Plane roadClipMemoPlanes[8];
  void computeClipObjectSpacePlanes(const M4x4&);
  void computeReference(const M4x4&);
};
bool equal(const StaPipCore& a,const StaPipCore& b) {
  for(int i=0;i<8;++i) if(memcmp(&a.clipObjectSpacePlanes[i].normal,&b.clipObjectSpacePlanes[i].normal,16) ||
      memcmp(&a.clipObjectSpacePlanes[i].distance,&b.clipObjectSpacePlanes[i].distance,4)) return false;
  return true;
}
'''
test=r'''
int main() {
  Renderer r; StaPipCore a{&r},b{&r}; M4x4 m;
  std::mt19937 rng(1729);std::uniform_real_distribution<float> values(-100,100);
  for(int trial=0;trial<2000;++trial) {
    for(float& f:m.data) f=values(rng);
    MotionProbe::mode=6;a.diagnosticRoadCall=true;
    a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
    a.computeClipObjectSpacePlanes(m);assert(equal(a,b));
    for(int k=0;k<16;++k) {
      uint32_t bits;memcpy(&bits,&m.data[k],4);bits^=1;memcpy(&m.data[k],&bits,4);
      a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
      assert(memcmp(a.roadClipMemoMvp.data,m.data,64)==0);
    }
    r.settings.near=1+float(trial%30);r.settings.far=1000+float(trial);
    a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
    PlanesClipAlgorithm::clipMargin=-10-float(trial%5);
    a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
    a.diagnosticRoadCall=false;m.data[0]+=1;
    a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
    a.diagnosticRoadCall=true;
    a.computeClipObjectSpacePlanes(m);assert(equal(a,b));
    MotionProbe::offset=1000;a.computeClipObjectSpacePlanes(m);assert(equal(a,b));
    MotionProbe::offset=1200;MotionProbe::mode=0;m.data[1]+=1;
    a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
  }
  assert(MotionProbe::memoHits==2000 && MotionProbe::memoChecks==2000);
  MotionProbe::mode=6;a.computeClipObjectSpacePlanes(m);b.computeReference(m);assert(equal(a,b));
  a.roadClipMemoPlanes[0].normal.x+=1000;
  a.computeClipObjectSpacePlanes(m);assert(!equal(a,b));
}
'''
source=prefix+'\n#define VU1_CLIP_XY_BAND '+band+'\n'+body(candidate)+body(original).replace('computeClipObjectSpacePlanes','computeReference')+test
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp);(folder/'test.cpp').write_text(source)
    subprocess.run(['g++','-std=c++17','-O2','-ffp-contract=off','-fsanitize=address,undefined',str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)
result=dict(passed=True,trials=2000,coldOracleHits=2000,
    checks=['repeat hit','each MVP lane bit mutation','near/far change','clip margin change',
            'non-road overwrite','disabled mode','corrupted cached output rejected by reference'],
    candidateBodySha256=hashlib.sha256(body(candidate).encode()).hexdigest(),
    referenceBodySha256=hashlib.sha256(body(original).encode()).hexdigest())
(root/'clip-memo-host.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))

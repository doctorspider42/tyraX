"""Exhaust actual group dispatch selection over visible masks and mode gates."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root=Path(sys.argv[1]).resolve()
text=(root/'game/src/gen/game_vehicles.gen.cpp').read_text(encoding='utf-8-sig')
start=text.index('  for(size_t i=0;i<procChunks.size();++i) {\n    if(!roadDrawVisible[i])continue;')
end=text.index('\n}',start)
loop=text[start:end]
prefix=r'''
#include <cassert>
#include <vector>
#include <memory>
#include <numeric>
namespace Tyra { struct StaPipBag { unsigned count; std::vector<int> originals; }; }
namespace MotionProbe { unsigned groupSubmitted=0,groupOutput=0,groupMerged=0,groupPadding=0; }
struct Chunk { std::unique_ptr<Tyra::StaPipBag> bag; };
int main() {
  for(unsigned mask=0;mask<256;++mask) for(int enabled=0;enabled<2;++enabled) {
    std::vector<Chunk> procChunks(8),roadDrawGroups(2);
    std::vector<size_t> roadGroupAt(8,size_t(-1)),roadGroupLast={4,7};
    std::vector<unsigned> roadGroupInput(2);
    std::vector<unsigned char> roadDrawVisible(8);
    for(int i=0;i<8;++i) {
      procChunks[i].bag=std::make_unique<Tyra::StaPipBag>();
      procChunks[i].bag->count=(i+1)*75;procChunks[i].bag->originals={i};
      roadDrawVisible[i]=(mask>>i)&1;
    }
    for(int g=0;g<2;++g) {
      roadDrawGroups[g].bag=std::make_unique<Tyra::StaPipBag>();
      const int first=g?4:0,last=g?7:4;roadGroupAt[first]=g;
      for(int i=first;i<last;++i) {
        roadDrawGroups[g].bag->count+=procChunks[i].bag->count;
        roadDrawGroups[g].bag->originals.push_back(i);
      }
      roadGroupInput[g]=roadDrawGroups[g].bag->count;
      roadDrawGroups[g].bag->count+=21;
    }
    std::vector<int> actual,expected;
    unsigned expectedVertices=0;
    for(int i=0;i<8;++i) if(roadDrawVisible[i]) {expected.push_back(i);expectedVertices+=procChunks[i].bag->count;}
    auto submitHeavy=[&](Tyra::StaPipBag* bag){actual.insert(actual.end(),bag->originals.begin(),bag->originals.end());};
    bool useGroups=enabled,witness=true;
    MotionProbe::groupSubmitted=MotionProbe::groupOutput=MotionProbe::groupMerged=MotionProbe::groupPadding=0;
'''
suffix=r'''
    assert(actual==expected && MotionProbe::groupOutput==expectedVertices+MotionProbe::groupPadding);
    if(!enabled) assert(MotionProbe::groupMerged==0 && MotionProbe::groupSubmitted==expected.size());
    if(mask==255 && enabled) assert(MotionProbe::groupSubmitted==3 && MotionProbe::groupMerged==2);
  }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp);(folder/'test.cpp').write_text(prefix+loop+suffix)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)
result=dict(passed=True,cases=512,actualDispatchSha256=hashlib.sha256(loop.encode()).hexdigest(),
            checks=['all visibility masks','enabled/disabled gate','primitive owner order',
                    'original vertices plus explicit padding','partial group fallback','ungrouped tail'])
(root/'road-groups-host.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))

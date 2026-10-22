#include "tyrax2-world-strip-plan.hpp"
#include <cassert>
#include <iostream>
#define WORLD_BODY_HOST
// Header paths are remapped only for the host's private source copy.
#include "tyrax2-world-body.hpp"
using WorldBody::Quad;
static Quad payload[2]{{{1,2,3,4}},{{5,6,7,8}}};
static const Quad* resolve(unsigned address, unsigned count, void*) {
  return address == 0x1000 && count <= 2 ? payload : nullptr;
}
int main() {
  std::vector<Quad> chain{
    {{0x10000001,0,0x01000101,0x6c010000}}, {{9,10,11,12}},
    {{0x30000002,0x1000,0,0x6c020002}},
    {{0x10000000,0,0,0x14000120}}};
  const std::vector<Quad> expected{
    {{0,0,0x01000101,0x6c010000}}, {{9,10,11,12}},
    {{0,0,0,0x6c020002}}, payload[0],payload[1],{{0,0,0,0x14000120}}};
  assert(WorldBody::flatten(chain.data(),chain.size(),resolve)==expected);
  unsigned rejected=0;
  for(unsigned i=0;i<10;++i){
    auto bad=chain;
    if(i<5)bad[0].words[0]=(unsigned(i==0?0:i==1?2:i==2?4:i==3?5:7)<<28)|1;
    if(i==5)bad[0].words[0]|=0x80000000;
    if(i==6)bad[2].words[1]|=1;
    if(i==7)bad[2].words[1]=0x2000;
    if(i==8)bad[0].words[0]=0x10000009;
    if(i==9)bad[2].words[0]=0x30000003;
    try{(void)WorldBody::flatten(bad.data(),bad.size(),resolve);}
    catch(const std::invalid_argument&){++rejected;}
  }
  assert(rejected==10);
  auto compiled=WorldBody::flatten(chain.data(),chain.size(),resolve);
  payload[0].words[0]^=1;chain.clear();
  assert(compiled==expected);
  std::cout<<"PASS A31 fragment flatten positive=1 controls=10 owned-after-source-change=1\n";
}

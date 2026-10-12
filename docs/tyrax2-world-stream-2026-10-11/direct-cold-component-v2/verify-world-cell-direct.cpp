#include "tyrax2-world-cell-direct.hpp"
#include <cassert>
#include <cstdio>
using namespace WorldCellPlan;
template<class F>static void rejects(F f){
  bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);
}
static Quad payload[2]{{{1,2,3,4}},{{5,6,7,8}}};
static const Quad* resolve(unsigned address,unsigned count,void*){
  return address==0x1000 && count<=2 ? payload : nullptr;
}
int main(){
  unsigned cases=0;
  for(unsigned textured=0;textured<2;++textured)
  for(unsigned topology=0;topology<2;++topology)
  for(unsigned cap=3;cap<=72;++cap)
  for(unsigned scale=1;scale<=3;++scale){
    const unsigned count=scale*75+3;
    std::array<std::vector<Quad>,3> data;
    for(unsigned s=0;s<3;++s)for(unsigned i=0;i<count;++i)
      data[s].push_back(Quad{{i,s,0x7fc00000u^i,0x80000000u+i}});
    PassSource src{{Input{data[0].data(),count},Input{data[1].data(),count},Input{data[2].data(),count}},
      count,topology?75u:0u,cap,bool(textured),topology?Topology::IndependentStrips:Topology::Triangles,{1,0}};
    const auto old=compile({src},1,1024*1024);
    unsigned part=0,words=0;
    WorldCellDirect::partitions(src,[&](WorldCellDirect::Range range){
      const auto& oracle=old->passes[0].parts.at(part++);
      assert(oracle.parity==range.parity && oracle.streams[0].size()==range.count);
      for(unsigned s=0;s<(textured?3U:2U);++s)
        assert(!std::memcmp(oracle.streams[s].data(),data[s].data()+range.first,range.count*sizeof(Quad)));
      words+=4+(textured?3U:2U)*(range.count+1);
    });
    assert(part==old->passes[0].parts.size());
    if(words<=65535)assert(WorldCellDirect::geometryQwords(src)==words);
    else rejects([&]{WorldCellDirect::geometryQwords(src);});
    ++cases;
  }
  std::vector<Quad> chain{{{0x10000001,0,0x01000101,0x6c010000}},{{9,10,11,12}},
    {{0x30000002,0x1000,0,0x6c020002}},{{0x10000000,0,0,0x14000120}}};
  const auto oracle=WorldBody::flatten(chain.data(),chain.size(),resolve);
  const Quad prefix{{41,42,43,44}};
  std::vector<Quad> direct{prefix};
  WorldCellDirect::appendFragment(direct,chain.data(),chain.size(),resolve,7);
  assert(direct.size()==7 && direct[0]==prefix);
  assert(std::equal(oracle.begin(),oracle.end(),direct.begin()+1));
  std::array<Quad,9> sourceData{};
  PassSource good{{Input{sourceData.data(),9},Input{sourceData.data(),9},Input{sourceData.data(),9}},
    6,0,6,true,Topology::Triangles,{1,0}};
  unsigned sourceControls=0;
  for(unsigned fault=0;fault<12;++fault){
    auto bad=good;
    if(fault==0)bad.count=0;
    if(fault==1)bad.count=4;
    if(fault==2)bad.capacity=2;
    if(fault==3)bad.capacity=73;
    if(fault==4)bad.streams[0].data=nullptr;
    if(fault==5)bad.streams[1].count=5;
    if(fault==6)bad.streams[2].count=5;
    if(fault==7)bad.material.generation=0;
    if(fault==8)bad.topology=static_cast<Topology>(99);
    if(fault==9)bad.run=3;
    if(fault==10){bad.topology=Topology::IndependentStrips;bad.run=4;}
    if(fault==11){bad.topology=Topology::IndependentStrips;bad.run=6;bad.count=7;}
    rejects([&]{compile({bad},1,4096);});
    rejects([&]{WorldCellDirect::geometryQwords(bad);});++sourceControls;
  }
  unsigned controls=0;
  for(unsigned fault=0;fault<14;++fault){
    auto bad=chain;
    if(fault<5)bad[0].words[0]=(unsigned(fault==0?0:fault==1?2:fault==2?4:fault==3?5:7)<<28)|1;
    if(fault==5)bad[0].words[0]|=0x80000000;
    if(fault==6)bad[2].words[1]|=1;
    if(fault==7)bad[2].words[1]=0x2000;
    if(fault==8)bad[0].words[0]=0x10000009;
    if(fault==9)bad[2].words[0]=0x30000003;
    std::vector<Quad> out{prefix};
    rejects([&]{WorldCellDirect::appendFragment(out,fault==10?nullptr:bad.data(),
      fault==11?0:bad.size(),resolve,fault==12?6:fault==13?65536:7);});
    assert(out.size()==1 && out[0]==prefix);++controls;
  }
  payload[0].words[0]^=1;chain.clear();
  assert(std::equal(oracle.begin(),oracle.end(),direct.begin()+1));
  std::printf("PASS direct ranges=%u sourceControls=%u fragmentControls=%u final-owned-after-source-change=1\n",cases,sourceControls,controls);
}

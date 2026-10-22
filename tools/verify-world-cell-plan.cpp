#include "tyrax2-world-cell-plan.hpp"
#include <cassert>
#include <cstdio>
using namespace WorldCellPlan;
using Triangle=std::array<Quad,9>;
using Triangles=std::vector<Triangle>;
static void emit(Triangles& out,const std::array<Input,3>& streams,
                 unsigned first,unsigned parity,bool textured){
  Triangle t{};
  const unsigned order[3]={first+(parity?1u:0u),first+(parity?0u:1u),first+2};
  for(unsigned v=0;v<3;++v)for(unsigned s=0;s<(textured?3u:2u);++s)
    t[v*3+s]=streams[s].data[order[v]];
  out.push_back(t);
}
static Triangles original(const PassSource& s){
  Triangles out;
  if(s.topology==Topology::Triangles){
    for(unsigned v=0;v<s.count;v+=3)emit(out,s.streams,v,0,s.textured);
  }else{
    for(unsigned base=0;base<s.count;base+=s.run){
      const auto count=std::min(s.run,s.count-base);
      for(unsigned v=0;v<count-2;++v)emit(out,s.streams,base+v,v&1,s.textured);
    }
  }
  return out;
}
static Triangles delivered(const Pass& p){
  Triangles out;
  for(const auto& part:p.parts){
    std::array<Input,3> streams{};
    for(unsigned k=0;k<3;++k)streams[k]={part.streams[k].data(),unsigned(part.streams[k].size())};
    const auto count=streams[0].count;
    if(p.topology==Topology::Triangles){
      assert(count%3==0 && part.parity==0);
      for(unsigned v=0;v<count;v+=3)emit(out,streams,v,0,p.textured);
    }else for(unsigned v=0;v<count-2;++v)emit(out,streams,v,(v+part.parity)&1,p.textured);
  }
  return out;
}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
  unsigned cases=0,failures=0;
  for(unsigned textured=0;textured<2;++textured)
  for(unsigned topology=0;topology<2;++topology)
  for(unsigned capacity=3;capacity<=72;++capacity)
  for(unsigned scale=1;scale<=3;++scale){
    std::array<std::vector<Quad>,3> data;
    const unsigned count=scale*75+3;
    for(unsigned s=0;s<3;++s)for(unsigned i=0;i<count;++i)
      data[s].push_back(Quad{{i,s,i^0x7fc00000u,0x80000000u+i}});
    const MaterialEpoch material{4,2};
    PassSource input{{Input{data[0].data(),count},Input{data[1].data(),count},Input{data[2].data(),count}},
      count,topology?75u:0u,capacity,bool(textured),topology?Topology::IndependentStrips:Topology::Triangles,material};
    std::vector<PassSource> sources(5,input);
    std::array<std::vector<Quad>,5> colors;
    std::array<Triangles,5> references;
    for(unsigned p=0;p<5;++p){
      colors[p]=data[1];
      for(auto& q:colors[p])q.words[1]+=p*17;
      sources[p].streams[1]={colors[p].data(),count};
      sources[p].material.slot=p;
      if(p==1)sources[p].textured=!input.textured;
      if(p==3){sources[p].topology=Topology::Triangles;sources[p].run=0;sources[p].count=6;}
      references[p]=original(sources[p]);
    }
    Owner owner;owner.replace(sources,1024*1024);const auto lease=owner.lease();
    assert(owner.admits(lease) && lease->passes.size()==5);
    for(unsigned p=0;p<5;++p){
      const auto& pass=lease->passes[p];
      assert(pass.material.generation==4 && pass.material.slot==p && delivered(pass)==references[p]);
      for(const auto& part:pass.parts)assert(part.streams[0].size()<=capacity);
    }
    std::uint64_t bytes=0;
    for(const auto& pass:lease->passes)for(const auto& part:pass.parts)
      for(const auto& stream:part.streams)bytes+=stream.size()*sizeof(Quad);
    assert(bytes==lease->bytes);
    rejects([&]{owner.replace(sources,lease->bytes-1);});++failures;
    assert(owner.admits(lease));
    const auto saved=sources[3];sources[3].streams[1].count=sources[3].count-1;
    rejects([&]{owner.replace(sources,1024*1024);});++failures;
    assert(owner.admits(lease));sources[3]=saved;
    owner.replace(sources,lease->bytes);
    assert(!owner.admits(lease) && owner.lease()->generation==2);
    owner.retire();assert(!owner.admits(lease) && !owner.lease());
    for(auto& stream:data){stream.clear();stream.shrink_to_fit();}
    for(auto& stream:colors){stream.clear();stream.shrink_to_fit();}
    for(unsigned p=0;p<5;++p)assert(delivered(lease->passes[p])==references[p]);
    ++cases;
  }
  std::array<Quad,9> data{};
  const PassSource good{{Input{data.data(),9},Input{data.data(),9},Input{data.data(),9}},
    6,0,6,true,Topology::Triangles,{1,0}};
  unsigned controls=0;
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
    rejects([&]{compile({bad},1,4096);});++controls;
  }
  rejects([&]{compile({},1,4096);});++controls;
  rejects([&]{compile(std::vector<PassSource>(65,good),1,4096);});++controls;
  rejects([&]{compile({good},0,4096);});++controls;
  rejects([&]{compile({good},1,0);});++controls;
  std::printf("WORLDCELL sourceCases=%u failedReplacements=%u controls=%u lifecycle=PASS\n",cases,failures,controls);
}

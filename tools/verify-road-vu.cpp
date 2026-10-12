// Check actual VCL: unchanged controls and bounded state-only bypass output.
#include "vuasm.hpp"
#include "vusim.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

static void put(std::vector<uint32_t>& m,int q,int k,float f) {
  std::memcpy(&m[q*4+k],&f,4);
}
static bool validBypass(const vusim::Result& r,const std::vector<uint32_t>& m,int top) {
  if(!r.ok || r.hitStepLimit || r.kicks!=std::vector<int>{top}) return false;
  const int src[9]={19,10,19,9,19,11,19,21,top+1};
  for(int i=0;i<4096;++i) {
    uint32_t want=m[i];
    if(i>=top*4 && i<(top+9)*4) {
      const int q=i/4-top,k=i%4;
      want=(q==8 && k==0)?0x8000u:m[src[q]*4+k];
    }
    if(r.mem[i]!=want)return false;
  }
  return true;
}
int main(int argc,char**argv) {
  assert(argc==2);std::string path=argv[1];
  std::ifstream file(path);assert(file);
  std::string source((std::istreambuf_iterator<char>(file)),{}),reference=source;
  const auto start=reference.find("    ; Private road VU/output bypass.");
  const auto end=reference.find("diagContinue:\n",start);
  assert(start!=std::string::npos && end!=std::string::npos);
  reference.erase(start,end+std::string("diagContinue:\n").size()-start);
  vuasm::Options opts;opts.includeRoot=path.substr(0,path.find("/src/"));
  vuir::Program original,patched,corrupt;std::string error;
  assert(vuasm::parseText(reference,path,opts,original,error));
  assert(vuasm::parseText(source,path,opts,patched,error));
  auto bad=source;const auto at=bad.find("isw.x diagZero, 8(buffer)");assert(at!=std::string::npos);
  bad.replace(at,std::string("isw.x diagZero, 8(buffer)").size(),"isw.x vi00, 8(buffer)");
  assert(vuasm::parseText(bad,path,opts,corrupt,error));
  std::mt19937 random(2813);std::uniform_real_distribution<float> d(-1,1);
  const bool clip=path.find("/clip/")!=std::string::npos;
  unsigned checks=0;
  for(int trial=0;trial<120;++trial) {
    const int top=trial%2?32:480,n=12,pos=top+2;
    std::vector<uint32_t> mem(4096,0);
    const float matrix[4][4]={{1.2F,0,0,0},{0,1.6F,0,0},{0,0,-1.002F,-1},{0,0,-2,60}};
    for(int q=0;q<4;++q)for(int k=0;k<4;++k)put(mem,q,k,matrix[q][k]);
    for(int q:{9,10,11,19,21})for(int k=0;k<4;++k)mem[q*4+k]=random();
    put(mem,top,0,2048);put(mem,top,1,2048);put(mem,top,2,8388607.5F);
    mem[top*4+3]=n|(clip?0xfc00:0x8000);
    mem[(top+1)*4]=n|0x8000;mem[(top+1)*4+2]=0xc12;
    put(mem,20,0,-4095);put(mem,20,1,-5096);put(mem,20,3,4096);
    const float planes[6][4]={{0,0,1,0},{0,0,-1,0},{-1,0,0,1},{1,0,0,1},{0,-1,0,1},{0,1,0,1}};
    for(int q=0;q<6;++q)for(int k=0;k<4;++k)put(mem,944+q*2,k,planes[q][k]);
    put(mem,945,0,5);put(mem,947,0,1000);
    for(int i=0;i<n;++i) {
      put(mem,pos+i,0,d(random)*(clip?80:5));put(mem,pos+i,1,d(random)*(clip?80:5));
      put(mem,pos+i,2,d(random)*5);put(mem,pos+i,3,1);
      for(int k=0;k<4;++k){put(mem,pos+n+i,k,.5F);put(mem,pos+2*n+i,k,128);}
    }
    vusim::Config cfg;cfg.top=top;
    for(int mode:{0,-1}) {
      mem[8*4+1]=uint32_t(mode);
      const auto a=vusim::run(original,mem,cfg),b=vusim::run(patched,mem,cfg);
      assert(a.ok && b.ok && !a.hitStepLimit && !b.hitStepLimit && !a.kicks.empty());
      assert(a.mem==b.mem && a.kicks==b.kicks);++checks;
    }
    mem[8*4+1]=3;
    assert(validBypass(vusim::run(patched,mem,cfg),mem,top));
    assert(!validBypass(vusim::run(corrupt,mem,cfg),mem,top));
  }
  std::cout<<"PASS controls="<<checks<<" bypass=120 corrupt-EOP-rejected=120 "<<path<<"\n";
}

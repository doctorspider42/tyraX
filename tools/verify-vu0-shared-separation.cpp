// Execute the shared-body VCL itself; canonical fallback must not hide a dead filter.
#include "vuasm.hpp"
#include "vusim.hpp"
#include "ps2rig/vu0_shared_packet.hpp"
#include "ps2rig/vu0_sat_probe.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace Tyra;
int main(int argc,char** argv){
  assert(argc==2);vuir::Program program;std::string error;
  { // Integer products distinguish the cyclic swizzle and source order exactly.
    vuir::Program cross;
    const char* source="#vuprog cross\nlq a,0(vi00)\nlq b,1(vi00)\nopmula.xyz ACC,a,b\nopmsub.xyz a,b,a\nsq a,2(vi00)\n#endvuprog\n";
    assert(vuasm::parseText(source,"cross",{},cross,error));
    std::vector<unsigned> memory(1024);const float input[8]={2,3,5,19,7,11,13,23};
    std::memcpy(memory.data(),input,sizeof(input));vusim::Config config;config.target=vusim::Target::VU0;
    const auto run=vusim::run(cross,memory,config);assert(run.ok&&run.warnings.empty());
    float actual[4];std::memcpy(actual,run.mem.data()+8,sizeof(actual));
    assert(actual[0]==-16&&actual[1]==9&&actual[2]==1&&actual[3]==19);
    assert(vuir::disassemble(cross,cross.code[2]).find("opmula.xyz")!=std::string::npos);
  }
  if(!vuasm::parseFile(argv[1],{},program,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
  unsigned seed=0x53485244,hits=0,misses=0,rawRejected=0,crossWitness=0,crossMask=0;
  constexpr int Trials=120000;
  for(int trial=0;trial<Trials;++trial){
    Vu0SatProbe::Case c{};Vu0SatProbe::make(c,seed,trial);
    // Include stationary arbitrary orientations, rather than only moving pairs.
    if(trial>=Trials/2)c.motion[0]=c.motion[1]={};
    Vu0SharedBody body[2];
    for(int k=0;k<2;++k)assert(packVu0SharedBody(body[k],c.view(k),c.box[k],c.motion[k]));
    Vu0SharedPacket packet;const auto ma=c.motion[0],mb=c.motion[1];
    const bool common=ma.x==mb.x&&ma.y==mb.y&&ma.z==mb.z;
    assert(packet.append(0,&body[0],1,&body[1],common)==Vu0SharedPacket::Append::Added);
    std::vector<unsigned> memory(1024,0xccaabbdd);assert(packet.stage(memory.data(),memory.size()));
    vusim::Config config;config.target=vusim::Target::VU0;
    auto run=vusim::run(program,memory,config);
    if(!run.ok||run.divByZero||run.qClobbers||!run.warnings.empty()){
      std::fprintf(stderr,"trial=%d error=%s div=%d q=%d warnings=%zu\n",trial,run.error.c_str(),run.divByZero,run.qClobbers,run.warnings.size());return 1;}
    for(int i=Vu0SharedPacket::ResultBase*4+8;i<1024;++i)assert(run.mem[i]==memory[i]);
    Vu0SharedResult shared;std::memcpy(&shared,run.mem.data()+Vu0SharedPacket::ResultBase*4,sizeof(shared));
    auto result=legacySharedResult(shared);Vu0SatJob job;
    assert(packVu0Sat(job,c.view(0),c.view(1),c.box[0],c.box[1],ma,mb));
    ConvexContact expected{},actual{};
    const bool reference=scalarMotionContactReference(c.view(0),c.view(1),ma,mb,expected,&c.box[0],&c.box[1]);
    const bool hit=finishVu0Shared(shared,c.view(0),c.view(1),c.box[0],c.box[1],ma,mb,actual);
    ConvexContact legacy{};const bool oldHit=finishVu0Sat(result,job,c.view(0),c.view(1),ma,mb,legacy);
    assert(oldHit==hit&&(!hit||!std::memcmp(&legacy,&actual,sizeof(actual))));
    if(hit!=reference||(hit&&std::memcmp(&expected,&actual,sizeof(actual)))){
      std::fprintf(stderr,"canonical mismatch trial=%d ref=%d hit=%d\n",trial,reference,hit);return 1;}
    if(hit)++hits;else ++misses;
    const bool rawHit=shared.interval[2]!=0||(shared.interval[3]!=0&&shared.state[0]!=0&&shared.interval[0]<=1&&shared.interval[1]>=0);
    rawRejected+=!rawHit;
    // A clearly separating cross axis with no separating face axis exercises
    // VU edge generation. The canonical EE fallback cannot satisfy this gate.
    if(common&&!reference){
      bool faceSeparated=false;for(int a=0;a<12;++a){auto n=a<6?c.normals[0][a]:c.normals[1][a-6];
        faceSeparated|=convexSeparatedOnAxis(c.view(0),c.view(1),ma,mb,n,&c.box[0],&c.box[1]);}
      if(!faceSeparated){
        bool robustCross=false;for(int a=0;a<3;++a)for(int b=0;b<3;++b)
          robustCross|=convexSeparatedOnAxis(c.view(0),c.view(1),ma,mb,cvCross(c.edges[0][a],c.edges[1][b]),&c.box[0],&c.box[1]);
        if(robustCross){if(rawHit){std::fprintf(stderr,"cross-axis filter failed trial=%d\n",trial);return 1;}
          assert(shared.state[1]>=12&&shared.state[1]<=20);crossMask|=1u<<(int(shared.state[1])-12);++crossWitness;}
      }
    }
    if(trial%11==9)assert(rawHit); // Exact touching must be admitted.
    if(trial%11==7&&trial<Trials/2)assert(rawHit); // Full crossing CCD.
  }
  assert(rawRejected>1000&&crossWitness>10&&crossMask==0x1ff);
  for(int batch=0;batch<100;++batch){
    Vu0SatProbe::Case cases[6]{};Vu0SharedBody bodies[12];Vu0SharedPacket packet;
    for(int i=0;i<6;++i){Vu0SatProbe::make(cases[i],seed,batch*6+i);
      for(int k=0;k<2;++k)assert(packVu0SharedBody(bodies[2*i+k],cases[i].view(k),cases[i].box[k],cases[i].motion[k]));}
    for(int i=0;i<24;++i){const int c=i%6;const auto a=cases[c].motion[0],b=cases[c].motion[1];
      assert(packet.append(2*c,&bodies[2*c],2*c+1,&bodies[2*c+1],a.x==b.x&&a.y==b.y&&a.z==b.z)==Vu0SharedPacket::Append::Added);}
    std::vector<unsigned> memory(1024,0xccaabbdd);assert(packet.stage(memory.data(),memory.size()));
    vusim::Config config;config.target=vusim::Target::VU0;auto run=vusim::run(program,memory,config);
    assert(run.ok&&run.warnings.empty()&&!run.divByZero&&!run.qClobbers);
    for(int i=0;i<Vu0SharedPacket::ResultBase*4;++i)assert(run.mem[i]==memory[i]);
    for(int i=242*4;i<1024;++i)assert(run.mem[i]==memory[i]);
    for(int i=0;i<24;++i){const auto& c=cases[i%6];Vu0SatJob job;
      assert(packVu0Sat(job,c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]));
      Vu0SharedResult shared;std::memcpy(&shared,run.mem.data()+Vu0SharedPacket::ResultBase*4+i*8,sizeof(shared));
      ConvexContact actual{},expected{};const bool reference=scalarMotionContactReference(c.view(0),c.view(1),c.motion[0],c.motion[1],expected,&c.box[0],&c.box[1]);
      const bool hit=finishVu0Shared(shared,c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1],actual);
      assert(hit==reference&&(!hit||!std::memcmp(&actual,&expected,sizeof(actual))));}
  }
  std::printf("Shared VCL: %d pairs, hits=%u misses=%u rawRejects=%u crossOnlyRejects=%u mask=%x; canonical geometry bitwise equal\n",Trials,hits,misses,rawRejected,crossWitness,crossMask);
  std::puts("100 full packets: 12 shared bodies, 24 ordered jobs, inputs and guard unchanged");
}

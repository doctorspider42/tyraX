// Host semantic oracle for the actual VCL source, not a second kernel model.
#include "vuasm.hpp"
#include "vusim.hpp"
#include "physics/vu0_sat.hpp"
#include "ps2rig/vu0_sat_probe.hpp"
#include "ps2rig/vu0_sat_capture.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
using namespace Tyra;
int main(int argc,char** argv) {
  assert(argc==2 || argc==3); vuir::Program program; std::string error;
  { // Numerical tolerance must never turn into permission to drop a hit.
    ConvexContact reference{{0,1,0},{0,0,0},.01f},actual=reference;
    assert(!compareVu0Sat(false,actual,true,reference,1).accepted);
    actual.depth=4e-6f;assert(compareVu0Sat(true,actual,false,reference,1).accepted);
    actual.depth=.001f;assert(!compareVu0Sat(true,actual,false,reference,1).accepted);
    actual=reference;actual.point.x=.0035f;assert(compareVu0Sat(true,actual,true,reference,1).accepted);
    actual.point.x=1.94f;assert(!compareVu0Sat(true,actual,true,reference,1).accepted);
    actual=reference;actual.normal.y=-1;assert(!compareVu0Sat(true,actual,true,reference,1).accepted);
  }
  if(!vuasm::parseFile(argv[1],{},program,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
  if(argc==3){Vu0SatCapture::Dump dump;FILE* f=std::fopen(argv[2],"rb");
    assert(f && std::fread(&dump,sizeof(dump),1,f)==1);std::fclose(f);assert(Vu0SatCapture::valid(dump));
    std::vector<unsigned> mem(1024,0);mem[0]=1;float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
    std::memcpy(mem.data()+4,constants,16);std::memcpy(mem.data()+8,&dump.job,sizeof(dump.job));
    vusim::Config config;config.target=vusim::Target::VU0;auto run=vusim::run(program,mem,config);assert(run.ok);
    Vu0SatResult result;std::memcpy(&result,run.mem.data()+226*4,sizeof(result));
    const auto& c=dump.c;ConvexContact actual{},hostReference{};
    const bool hit=finishVu0Sat(result,dump.job,c.view(0),c.view(1),c.motion[0],c.motion[1],actual);
    const bool hostHit=scalarMotionContactReference(c.view(0),c.view(1),c.motion[0],c.motion[1],hostReference,&c.box[0],&c.box[1]);
    float radius[2]={};for(int k=0;k<2;++k)for(int i=0;i<3;++i)radius[k]+=cvDot(c.box[k].halfAxes[i],c.box[k].halfAxes[i]);
    const float scale=std::sqrt(std::min(radius[0],radius[1]));
    const auto comparison=compareVu0Sat(hit,actual,dump.referenceHit,dump.reference,scale);
    ConvexContact recorded{};
    const bool recordedHit=finishVu0Sat(dump.result,dump.job,c.view(0),c.view(1),c.motion[0],c.motion[1],recorded);
    const auto recordedComparison=compareVu0Sat(recordedHit,recorded,dump.referenceHit,dump.reference,scale);
    unsigned common;std::memcpy(&common,&dump.job.q[31][2],4);
    std::printf("capture ids=%d,%d common=%u recorded=%u EE=%u hostReference=%u replay=%u accepted=%u recordedRecovery=%u recoveryAccepted=%u flags=%g,%g,%g,%g\n",dump.a,dump.b,common,dump.hit,dump.referenceHit,hostHit,hit,comparison.accepted,recordedHit,recordedComparison.accepted,result.flags[0],result.flags[1],result.flags[2],result.flags[3]);
    return comparison.accepted && recordedComparison.accepted?0:1;
  }
  for(int controlled=0;controlled<2;++controlled){auto c=controlled?Vu0SatProbe::clearWinner():Vu0SatProbe::nearTie();Vu0SatJob job;
    assert(packVu0Sat(job,c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]));
    assert(!vu0SatScalarPreferred(job));
    std::vector<unsigned> mem(1024,0);mem[0]=1;float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
    std::memcpy(mem.data()+4,constants,16);std::memcpy(mem.data()+8,&job,sizeof(job));
    vusim::Config config;config.target=vusim::Target::VU0;auto run=vusim::run(program,mem,config);
    assert(run.ok);Vu0SatResult result;std::memcpy(&result,run.mem.data()+226*4,sizeof(result));
    if(controlled)assert(result.flags[3]==0 && "Losing ambiguous axes must not force scalar confirmation");
    else assert(result.flags[3]>=2 && "Distinct near-minimum axes must use canonical scalar selection");
  }
  for(int pattern=0;pattern<4;++pattern){auto c=Vu0SatProbe::commonMotion(pattern);Vu0SatJob job;
    assert(packVu0Sat(job,c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]));
    long long fastSteps=0;
    for(int full=0;full<2;++full){
      if(full){const unsigned zero=0;std::memcpy(&job.q[31][2],&zero,4);}
      std::vector<unsigned> mem(1024,0);mem[0]=1;float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
      std::memcpy(mem.data()+4,constants,16);std::memcpy(mem.data()+8,&job,sizeof(job));
      vusim::Config config;config.target=vusim::Target::VU0;auto run=vusim::run(program,mem,config);
      assert(run.ok && !run.divByZero && !run.qClobbers && run.warnings.empty());
      Vu0SatResult result;std::memcpy(&result,run.mem.data()+226*4,sizeof(result));
      float d=0,n=0,p=0;assert(Vu0SatProbe::compare(c,job,result,d,n,p));
      if(full)assert(run.steps>fastSteps && "Common-motion shortcut must remove work");else fastSteps=run.steps;
    }
  }
  std::mt19937 rng(0x534154);std::uniform_real_distribution<float> pos(-4,4),size(.05f,2),angle(-3,3);
  int hits=0,misses=0;float maxDepth=0,maxNormal=0,maxPoint=0;
  for(int trial=0;trial<10000;++trial) {
    CollisionV3 vertices[2][8],axes[2][3],motion[2];CollisionObb boxes[2];
    for(int k=0;k<2;++k) {
      float y=angle(rng),x=angle(rng),cy=std::cos(y),sy=std::sin(y),cx=std::cos(x),sx=std::sin(x);
      axes[k][0]={cy,0,-sy};axes[k][1]={sy*sx,cx,cy*sx};axes[k][2]={sy*cx,-sx,cy*cx};
      CollisionV3 c={pos(rng),pos(rng),pos(rng)};float h[3]={size(rng),size(rng),size(rng)};
      if(trial%5==0 && k==1)c=boxes[0].centre; // dense overlap
      boxes[k].centre=c;
      for(int j=0;j<3;++j)boxes[k].halfAxes[j]=cvMul(axes[k][j],h[j]);
      for(int i=0;i<8;++i){vertices[k][i]=c;for(int j=0;j<3;++j)vertices[k][i]=cvAdd(vertices[k][i],cvMul(boxes[k].halfAxes[j],i&(1<<j)?1.f:-1.f));}
      motion[k]=trial%3?CollisionV3{pos(rng),pos(rng),pos(rng)}:CollisionV3{};
    }
    ConvexView a{vertices[0],8,axes[0],3,axes[0],3},b{vertices[1],8,axes[1],3,axes[1],3};
    Vu0SatJob job;assert(packVu0Sat(job,a,b,boxes[0],boxes[1],motion[0],motion[1]));
    std::vector<unsigned> mem(1024,0);mem[0]=1;float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
    std::memcpy(mem.data()+4,constants,16);std::memcpy(mem.data()+8,&job,sizeof(job));
    vusim::Config config;config.target=vusim::Target::VU0;
    auto run=vusim::run(program,mem,config);
    if(!run.ok || run.divByZero || run.qClobbers || !run.warnings.empty()) {
      std::fprintf(stderr,"trial %d: %s warnings=%zu div=%d q=%d\n",trial,run.error.c_str(),run.warnings.size(),run.divByZero,run.qClobbers);return 1;
    }
    Vu0SatResult result;std::memcpy(&result,run.mem.data()+226*4,sizeof(result));
    ConvexContact expected,actual;
    bool ref=convexMotionContact(a,b,motion[0],motion[1],expected,&boxes[0],&boxes[1]);
    bool hit=finishVu0Sat(result,job,a,b,motion[0],motion[1],actual);
    if(hit!=ref){std::fprintf(stderr,"classification trial=%d ref=%d micro=%d flags=%g,%g,%g,%g\n",trial,ref,hit,result.flags[0],result.flags[1],result.flags[2],result.flags[3]);return 1;}
    if(hit){++hits;float de=std::fabs(expected.depth-actual.depth),ne=std::sqrt(cvDot(cvSub(expected.normal,actual.normal),cvSub(expected.normal,actual.normal))),pe=std::sqrt(cvDot(cvSub(expected.point,actual.point),cvSub(expected.point,actual.point)));
      maxDepth=std::max(maxDepth,de);maxNormal=std::max(maxNormal,ne);maxPoint=std::max(maxPoint,pe);
      if(!std::isfinite(de+ne+pe) || de>1e-4f || ne>1e-4f || pe>1e-3f){std::fprintf(stderr,"geometry trial=%d errors=%g,%g,%g\n",trial,de,ne,pe);return 1;}
    }else ++misses;
  }
  std::printf("10000 VCL SAT/CCD pairs: %d hits, %d misses; max errors depth=%g normal=%g point=%g\n",hits,misses,maxDepth,maxNormal,maxPoint);
  unsigned seed=0x534154;
  for(int first=0;first<2048;first+=Vu0SatBatch::Capacity) {
    const int count=std::min(Vu0SatBatch::Capacity,2048-first);
    Vu0SatProbe::Case cases[Vu0SatBatch::Capacity];Vu0SatJob jobs[Vu0SatBatch::Capacity];
    for(int i=0;i<count;++i){Vu0SatProbe::make(cases[i],seed,first+i);assert(packVu0Sat(jobs[i],cases[i].view(0),cases[i].view(1),cases[i].box[0],cases[i].box[1],cases[i].motion[0],cases[i].motion[1]));}
    std::vector<unsigned> mem(1024,0);mem[0]=count;float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
    std::memcpy(mem.data()+4,constants,16);std::memcpy(mem.data()+8,jobs,sizeof(Vu0SatJob)*count);
    mem[1023]=0x12345678;
    vusim::Config config;config.target=vusim::Target::VU0;auto run=vusim::run(program,mem,config);
    assert(run.ok && run.warnings.empty() && !run.divByZero && !run.qClobbers && run.mem[1023]==0x12345678);
    for(int i=0;i<count;++i){Vu0SatResult result;std::memcpy(&result,run.mem.data()+226*4+i*16,sizeof(result));
      if(!Vu0SatProbe::compare(cases[i],jobs[i],result,maxDepth,maxNormal,maxPoint)){std::fprintf(stderr,"adversarial case=%d flags=%g,%g,%g,%g errors=%g,%g,%g\n",first+i,result.flags[0],result.flags[1],result.flags[2],result.flags[3],maxDepth,maxNormal,maxPoint);return 1;}
    }
  }
  std::printf("2048 cases in mixed batches of up to seven: touching/parallel/CCD/large coordinates pass, data sentinel intact\n");
}

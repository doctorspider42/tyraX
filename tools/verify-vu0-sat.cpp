// Host semantic oracle for the actual VCL source, not a second kernel model.
#include "vuasm.hpp"
#include "vusim.hpp"
#include "physics/vu0_sat.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
using namespace Tyra;
int main(int argc,char** argv) {
  assert(argc==2); vuir::Program program; std::string error;
  if(!vuasm::parseFile(argv[1],{},program,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
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
}

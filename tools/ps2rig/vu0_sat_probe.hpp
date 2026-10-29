// Same-input assembled-kernel oracle, executed before any measured frame.
#pragma once
#include "physics/vu0_sat.hpp"
#include <cstdio>
#ifdef _EE
#include "math/vu0_owner.hpp"
#include "renderer/rt/vu0_raytracer.hpp"
#endif
namespace Vu0SatProbe {
struct Case {
  Tyra::CollisionV3 vertices[2][8],normals[2][6],edges[2][3],motion[2];
  Tyra::CollisionObb box[2];
  int pattern=0;
  Tyra::ConvexView view(int k)const{return {vertices[k],8,normals[k],6,edges[k],3};}
};
inline void make(Case& c,unsigned& seed,int round) {
  using namespace Tyra;
  c.pattern=round%11;
  auto random=[&](float scale){seed=seed*1664525u+1013904223u;return float(int((seed>>8)%20001)-10000)*scale/10000.f;};
  for(int k=0;k<2;++k) {
    float yaw=random(3),pitch=random(3);
    if(round%11>=6){yaw=round%11==8?k*1e-7f:0;pitch=0;}
    float cy=std::cos(yaw),sy=std::sin(yaw),cx=std::cos(pitch),sx=std::sin(pitch);
    c.edges[k][0]={cy,0,-sy};c.edges[k][1]={sy*sx,cx,cy*sx};c.edges[k][2]={sy*cx,-sx,cy*cx};
    CollisionV3 centre={random(4),random(4),random(4)};
    float h[3]={.05f+std::fabs(random(2)),.05f+std::fabs(random(2)),.05f+std::fabs(random(2))};
    if(round%5==0 && k==1)centre=c.box[0].centre;
    if(round%11==7){centre={k?0.f:5.f,0,0};h[0]=h[1]=h[2]=.5f;}
    if(round%11==9){centre={k?1.f:0.f,0,0};h[0]=h[1]=h[2]=.5f;}
    if(round%11==6){centre=k?CollisionV3{.7f,.7f,.2f}:CollisionV3{};h[0]=h[1]=h[2]=.5f;}
    if(round%11==10){centre.x+=1000;centre.z-=1000;}
    c.box[k].centre=centre;
    for(int a=0;a<3;++a){c.normals[k][a*2]=c.edges[k][a];c.normals[k][a*2+1]=cvMul(c.edges[k][a],-1);c.box[k].halfAxes[a]=cvMul(c.edges[k][a],h[a]);}
    for(int v=0;v<8;++v){c.vertices[k][v]=centre;for(int a=0;a<3;++a)c.vertices[k][v]=cvAdd(c.vertices[k][v],cvMul(c.box[k].halfAxes[a],v&(1<<a)?1.f:-1.f));}
    c.motion[k]=round%3?CollisionV3{random(8),random(8),random(8)}:CollisionV3{};
    if(round%11==7)c.motion[k]={k?0.f:10.f,0,0};
    if(round%11==9 || round%11==6)c.motion[k]={};
  }
}
inline bool compare(const Case& c,const Tyra::Vu0SatJob& job,const Tyra::Vu0SatResult& result,
                    float& maxDepth,float& maxNormal,float& maxPoint) {
  using namespace Tyra;ConvexContact expected{},actual{};
  bool reference=scalarMotionContactReference(c.view(0),c.view(1),c.motion[0],c.motion[1],expected,&c.box[0],&c.box[1]);
  bool hit=finishVu0Sat(result,job,c.view(0),c.view(1),c.motion[0],c.motion[1],actual);
  float scale[2]={};for(int k=0;k<2;++k)for(int j=0;j<3;++j)scale[k]+=cvDot(c.box[k].halfAxes[j],c.box[k].halfAxes[j]);
  const auto comparison=compareVu0Sat(hit,actual,reference,expected,std::sqrt(std::min(scale[0],scale[1])));
  if(!hit || !reference)return comparison.accepted;
  const float d=comparison.depth,n=comparison.normal,p=comparison.point;
  maxDepth=std::max(maxDepth,d);maxNormal=std::max(maxNormal,n);maxPoint=std::max(maxPoint,p);
  return comparison.accepted;
}
#ifdef _EE
inline bool run() {
  using namespace Tyra;Vu0SatBatch batch;if(!batch.begin())return false;
  Case cases[Vu0SatBatch::Capacity];Vu0SatJob jobs[Vu0SatBatch::Capacity];Vu0SatResult results[Vu0SatBatch::Capacity];
  unsigned seed=0x534154,failures=0;float depth=0,normal=0,point=0;
  for(int first=0;first<2048;first+=Vu0SatBatch::Capacity) {
    const int count=std::min(Vu0SatBatch::Capacity,2048-first);
    for(int i=0;i<count;++i){make(cases[i],seed,first+i);if(!packVu0Sat(jobs[i],cases[i].view(0),cases[i].view(1),cases[i].box[0],cases[i].box[1],cases[i].motion[0],cases[i].motion[1]))return false;}
    batch.start(jobs,count);
    volatile unsigned independent=seed;
    for(int i=0;i<128;++i)independent=independent*1664525u+1013904223u;
    batch.observeOverlap();batch.finish(results);
    for(int i=0;i<count;++i)if(!compare(cases[i],jobs[i],results[i],depth,normal,point)){
      if(failures<8) {
        printf("VUSATCHECK FAIL case=%d flags=%g,%g,%g,%g axes=%g,%g\n",first+i,results[i].flags[0],results[i].flags[1],results[i].flags[2],results[i].flags[3],results[i].indices[0],results[i].indices[1]);
      }
      ++failures;
    }
  }
  printf("VUSATCHECK cases=2048 failures=%u depth=%g normal=%g point=%g\n",failures,depth,normal,point);fflush(stdout);
  if(failures)return false;
  // Exercise the cached uploaded=true path after another owner replaced it.
  auto imageHash=[](){auto mem=reinterpret_cast<volatile unsigned*>(0x11000000);unsigned hash=2166136261u;for(int i=0;i<1024;++i)hash=(hash^mem[i])*16777619u;return hash;};
  Vu0Raytracer ray;ray.init();const auto rayOwner=vu0MicroOwner;const unsigned before=imageHash();
  if(!batch.begin())return false;
  batch.start(jobs,1);batch.finish(results);
  bool satOk=compare(cases[0],jobs[0],results[0],depth,normal,point);
  ray.init();const bool ownerOk=satOk && vu0MicroOwner==rayOwner && imageHash()==before;
  printf("VU0OWNERCHECK switches=3 failures=%u\n",ownerOk?0u:1u);fflush(stdout);
  return ownerOk;
}
#endif
}

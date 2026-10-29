// Near-touching adversarial corpus, using the actual EE reference and VU image.
#pragma once
#include "vu0_sat_capture.hpp"
#ifdef _EE
namespace Vu0SatPrecision {
using namespace Tyra;
inline bool replay(){
  FILE* f=fopen(FileUtils::fromCwd("vu0-sat-failure.bin").c_str(),"rb");
  if(!f)return true;
  Vu0SatCapture::Dump dump;const bool loaded=fread(&dump,sizeof(dump),1,f)==1;fclose(f);
  if(!loaded || !Vu0SatCapture::valid(dump))return false;
  bool ok=true;
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
  constexpr int variants=2;
#else
  constexpr int variants=1;
#endif
  for(int reference=0;reference<variants;++reference){
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
    vu0SatProbeReference=reference!=0;
#endif
    Vu0SatBatch batch;if(!batch.begin())return false;Vu0SatResult result;
    batch.start(&dump.job,1);batch.finish(&result);const auto& c=dump.c;ConvexContact actual{};
    const bool hit=finishVu0Sat(result,dump.job,c.view(0),c.view(1),c.motion[0],c.motion[1],actual);
    float radius[2]={};for(int k=0;k<2;++k)for(int i=0;i<3;++i)radius[k]+=cvDot(c.box[k].halfAxes[i],c.box[k].halfAxes[i]);
    const auto comparison=compareVu0Sat(hit,actual,dump.referenceHit,dump.reference,std::sqrt(std::min(radius[0],radius[1])));
    const bool raw=result.flags[1]!=0 || (result.flags[2]!=0 && (int(result.flags[3])&1) && result.sweepEnter[3]<=1 && result.flags[0]>=0);
    printf("VUSATREPLAY reference=%d saved=%u raw=%u hit=%u accepted=%u\n",reference,dump.referenceHit,raw?1u:0u,hit?1u:0u,comparison.accepted?1u:0u);fflush(stdout);
    if(reference==0)ok &= comparison.accepted;
  }
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
  vu0SatProbeReference=false;
#endif
  return ok;
}
inline bool run(){
  if(!replay())return false;
  Vu0SatBatch batch;if(!batch.begin())return false;
  Vu0SatProbe::Case cases[Vu0SatBatch::Capacity];Vu0SatJob jobs[Vu0SatBatch::Capacity];Vu0SatResult results[Vu0SatBatch::Capacity];
  unsigned seed=0x50524543u;
  constexpr int Trials=50000;
  for(int first=0;first<Trials;first+=Vu0SatBatch::Capacity){const int count=std::min(Vu0SatBatch::Capacity,Trials-first);
    for(int i=0;i<count;++i){auto& c=cases[i];const int index=first+i;Vu0SatProbe::make(c,seed,index*11);
      const auto centre=cvAdd(c.box[0].centre,{0,1,-48});
      if(index%3==0)for(int a=0;a<3;++a)c.edges[1][a]=c.edges[0][a];
      if(index%3==2)for(int a=0;a<3;++a){const auto v=c.edges[0][a];const float cy=std::cos(.01f),sy=std::sin(.01f);c.edges[1][a]={cy*v.x+sy*v.z,v.y,-sy*v.x+cy*v.z};}
      auto n=c.edges[0][0];n=cvMul(n,1/std::sqrt(cvDot(n,n)));
      float separation=float(index%81-40)*.000005f;
      for(int k=0;k<2;++k)for(int a=0;a<3;++a)separation+=.45f*std::fabs(cvDot(n,c.edges[k][a]));
      for(int k=0;k<2;++k){c.box[k].centre=k?cvAdd(centre,cvMul(n,separation)):centre;
        for(int a=0;a<3;++a){c.normals[k][a*2]=c.edges[k][a];c.normals[k][a*2+1]=cvMul(c.edges[k][a],-1);c.box[k].halfAxes[a]=cvMul(c.edges[k][a],.45f);}
        for(int v=0;v<8;++v){c.vertices[k][v]=c.box[k].centre;for(int a=0;a<3;++a)c.vertices[k][v]=cvAdd(c.vertices[k][v],cvMul(c.box[k].halfAxes[a],v&(1<<a)?1.f:-1.f));}
        c.motion[k]={};
      }
      if(index%2)c.motion[0].x=1e-12f;
      if(!packVu0Sat(jobs[i],c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]))return false;
    }
    batch.start(jobs,count);batch.finish(results);
    for(int i=0;i<count;++i){const auto& c=cases[i];ConvexContact actual{},reference{};
      const bool hit=finishVu0Sat(results[i],jobs[i],c.view(0),c.view(1),c.motion[0],c.motion[1],actual);
      const bool ref=scalarMotionContactReference(c.view(0),c.view(1),c.motion[0],c.motion[1],reference,&c.box[0],&c.box[1]);
      const auto comparison=compareVu0Sat(hit,actual,ref,reference,.45f*std::sqrt(3.f));
      if(!comparison.accepted){Vu0SatCapture::remember(first+i,-1,c.view(0),c.view(1),jobs[i],results[i],hit,actual,ref,reference);Vu0SatCapture::flush();
        printf("VUSATPRECISION cases=%d failures=1 hit=%u ref=%u depth=%g\n",first+i+1,hit?1u:0u,ref?1u:0u,reference.depth);fflush(stdout);return false;}
    }
  }
  printf("VUSATPRECISION cases=%d failures=0\n",Trials);fflush(stdout);return true;
}
}
#endif

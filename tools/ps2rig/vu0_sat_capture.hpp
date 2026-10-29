// Private diagnostic capture: RAM-only while micro runs; file I/O after drain.
#pragma once
#include "vu0_sat_probe.hpp"
#include <cstddef>
#ifdef _EE
#include "file/file_utils.hpp"
#endif
namespace Vu0SatCapture {
struct alignas(16) Dump {
  unsigned magic=0x53415446u,version=1,bytes=0;
  int a=-1,b=-1;
  unsigned counts[6]{};
  unsigned hit=0,referenceHit=0;
  Vu0SatProbe::Case c{};
  Tyra::Vu0SatJob job{};
  Tyra::Vu0SatResult result{};
  Tyra::ConvexContact actual{},reference{};
};
inline bool valid(const Dump& d){return d.magic==0x53415446u && d.version==1 && d.bytes==sizeof(Dump) &&
  d.counts[0]==8 && d.counts[1]==6 && d.counts[2]==3 && d.counts[3]==8 && d.counts[4]==6 && d.counts[5]==3;}
#ifdef _EE
inline Dump first;
inline bool pending=false,written=false;
inline void remember(int a,int b,const Tyra::ConvexView& av,const Tyra::ConvexView& bv,
    const Tyra::Vu0SatJob& job,const Tyra::Vu0SatResult& result,bool hit,
    const Tyra::ConvexContact& actual,bool referenceHit,const Tyra::ConvexContact& reference){
  if(pending || written)return;
  first.bytes=sizeof(first);first.a=a;first.b=b;first.job=job;first.result=result;
  first.hit=hit;first.referenceHit=referenceHit;
  if(hit)first.actual=actual;
  if(referenceHit)first.reference=reference;
  const Tyra::ConvexView views[2]={av,bv};
  for(int k=0;k<2;++k){const auto& v=views[k];
    first.counts[k*3]=v.nv;first.counts[k*3+1]=v.nn;first.counts[k*3+2]=v.ne;
    for(int i=0;i<std::min(v.nv,8);++i)first.c.vertices[k][i]=v.verts[i];
    for(int i=0;i<std::min(v.nn,6);++i)first.c.normals[k][i]=v.normals[i];
    for(int i=0;i<std::min(v.ne,3);++i)first.c.edges[k][i]=v.edges[i];
    const auto* centre=job.q[k*4];first.c.box[k].centre={centre[0],centre[1],centre[2]};
    for(int i=0;i<3;++i){const auto* axis=job.q[k*4+i+1];first.c.box[k].halfAxes[i]={axis[0],axis[1],axis[2]};}
    const auto* motion=job.q[8+k];first.c.motion[k]={motion[0],motion[1],motion[2]};
  }
  pending=true;
}
inline void flush(){
  if(!pending)return;
  FILE* f=fopen(Tyra::FileUtils::fromCwd("vu0-sat-failure.bin").c_str(),"wb");
  const bool saved=f && fwrite(&first,sizeof(first),1,f)==1;
  if(f)fclose(f);
  printf("VUSATCAPTURE a=%d b=%d bytes=%u saved=%u\n",first.a,first.b,unsigned(sizeof(first)),saved?1u:0u);fflush(stdout);
  if(saved){written=true;pending=false;}
}
#endif
}

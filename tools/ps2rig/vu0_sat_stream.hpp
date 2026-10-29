// Fixed-input physical EE/VU0 throughput probe; no solver or host I/O in timings.
#pragma once
#include "debug/vu0_sat_probe.hpp"
#include "file/file_utils.hpp"
#include <cstring>
#ifdef _EE
namespace Vu0SatStream {
using namespace Tyra;
constexpr int Cases=224,Loops=8,Repeats=8;
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
struct Sample {unsigned ticks,hash,wait,batches;};
inline unsigned digest(unsigned hash,bool hit,const ConvexContact& c) {
  hash=(hash^unsigned(hit))*16777619u;
  if(hit){const float v[]={c.normal.x,c.normal.y,c.normal.z,c.point.x,c.point.y,c.point.z,c.depth};
    for(float x:v){unsigned word;std::memcpy(&word,&x,4);hash=(hash^word)*16777619u;}}
  return hash;
}
inline Sample measure(const Vu0SatProbe::Case* cases,const Vu0SatJob* cached,int mode) {
  Vu0SatBatch batch;
  if(mode!=0 && mode!=3 && !batch.begin())return {};
  Vu0SatJob jobs[2][Vu0SatBatch::Capacity];int ids[2][Vu0SatBatch::Capacity];
  Vu0SatResult results[Vu0SatBatch::Capacity];
  int filling=0,queued=0,active=-1,count=0;
  unsigned hash=2166136261u;
  const unsigned wait=vu0SatMetrics.waitTicks,batches=vu0SatMetrics.batches;
  auto consume=[&](int buffer,int n){
    for(int k=0;k<n;++k){const auto& c=cases[ids[buffer][k]];ConvexContact contact{};
      bool hit=finishVu0Sat(results[k],jobs[buffer][k],c.view(0),c.view(1),c.motion[0],c.motion[1],contact);
      hash=digest(hash,hit,contact);}
  };
  auto launch=[&](){
    const int previous=active,previousCount=count;
    if(previous>=0){batch.finish(results);if(mode==1)consume(previous,previousCount);}
    if(queued){batch.start(jobs[filling],queued);active=filling;count=queued;filling^=1;queued=0;}
    else active=-1;
    if(previous>=0 && mode!=1)consume(previous,previousCount);
  };
  const unsigned start=ticks();
  for(int loop=0;loop<Loops;++loop)for(int i=0;i<Cases;++i){
    const auto& c=cases[i];
    if(mode==0){ConvexContact contact{};
      bool hit=scalarMotionContactReference(c.view(0),c.view(1),c.motion[0],c.motion[1],contact,&c.box[0],&c.box[1]);
      hash=digest(hash,hit,contact);continue;}
    if(mode==4)std::memcpy(&jobs[filling][queued],&cached[i],sizeof(Vu0SatJob));
    else packVu0Sat(jobs[filling][queued],c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]);
    if(mode==3){unsigned word;std::memcpy(&word,&jobs[filling][queued].q[10][0],4);hash=(hash^word)*16777619u;continue;}
    ids[filling][queued++]=i;
    if(queued==Vu0SatBatch::Capacity)launch();
  }
  if(active>=0 || queued)launch();
  if(active>=0)launch();
  const unsigned elapsed=ticks()-start;
  return {elapsed,hash,vu0SatMetrics.waitTicks-wait,vu0SatMetrics.batches-batches};
}
inline bool run(){
  static Vu0SatProbe::Case cases[Cases];static Vu0SatJob cached[Cases];
  Sample samples[2][Repeats][5];
  unsigned seed=0x5354524du;bool ok=true;
  for(int workload=0;workload<2;++workload){
    int serial=0;
    for(int i=0;i<Cases;){auto& c=cases[i];Vu0SatProbe::make(c,seed,serial++);
      if(workload){const auto delta=cvSub(c.box[0].centre,c.box[1].centre);
        c.box[1].centre=c.box[0].centre;for(auto& v:c.vertices[1])v=cvAdd(v,delta);
        c.motion[0]=c.motion[1]={};}
      if(!packVu0Sat(cached[i],c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]))return false;
      if(!vu0SatScalarPreferred(cached[i]))++i;
    }
    // Validate this exact corpus outside timed regions.
    Vu0SatBatch validation;if(!validation.begin())return false;
    for(int first=0;first<Cases;first+=Vu0SatBatch::Capacity){Vu0SatResult result[Vu0SatBatch::Capacity];
      validation.start(cached+first,Vu0SatBatch::Capacity);validation.finish(result);
      float d=0,n=0,p=0;for(int k=0;k<Vu0SatBatch::Capacity;++k)
        ok &= Vu0SatProbe::compare(cases[first+k],cached[first+k],result[k],d,n,p);}
    if(!ok)return false;
    for(int repeat=0;repeat<Repeats;++repeat)for(int order=0;order<5;++order){
      const int mode=repeat%2?4-order:order;
      samples[workload][repeat][mode]=measure(cases,cached,mode);
    }
    for(int r=0;r<Repeats;++r){const unsigned expected=samples[workload][r][1].hash;
      ok &= expected==samples[workload][r][2].hash && expected==samples[workload][r][4].hash;
      for(int m=0;m<5;++m)ok &= samples[workload][r][m].ticks>0;}
  }
  FILE* f=fopen(FileUtils::fromCwd("vu0-stream.csv").c_str(),"wb");
  if(!f)return false;
  fprintf(f,"workload,repeat,mode,jobs,ticks,hash,wait_ticks,batches\n");
  for(int w=0;w<2;++w)for(int r=0;r<Repeats;++r)for(int m=0;m<5;++m){const auto& s=samples[w][r][m];
    fprintf(f,"%d,%d,%d,%d,%u,%u,%u,%u\n",w,r,m,Cases*Loops,s.ticks,s.hash,s.wait,s.batches);}
  fclose(f);
  printf("VUSTREAM cases=%d repeats=%d failures=%u file=1\n",Cases,Repeats,ok?0u:1u);fflush(stdout);
  return ok;
}
}
#endif

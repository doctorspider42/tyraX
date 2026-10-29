// Fixed-input physical EE/VU0 throughput probe; no solver or host I/O in timings.
#pragma once
#include "debug/vu0_sat_probe.hpp"
#include "file/file_utils.hpp"
#include <cstring>
#ifdef _EE
namespace Vu0SatStream {
using namespace Tyra;
constexpr int Cases=224,Loops=8,Repeats=8,Workloads=3;
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
constexpr int Modes=6;
#else
constexpr int Modes=5;
#endif
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
struct Sample {unsigned ticks,hash,wait,batches,confirmedCcd,confirmedTie,confirmedParallel;};
inline unsigned digest(unsigned hash,bool hit,const ConvexContact& c) {
  hash=(hash^unsigned(hit))*16777619u;
  if(hit){const float v[]={c.normal.x,c.normal.y,c.normal.z,c.point.x,c.point.y,c.point.z,c.depth};
    for(float x:v){unsigned word;std::memcpy(&word,&x,4);hash=(hash^word)*16777619u;}}
  return hash;
}
inline Sample measure(const Vu0SatProbe::Case* cases,const Vu0SatJob* cached,int mode) {
  Vu0SatBatch batch;
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
  vu0SatProbeReference=mode==5;
#endif
  if(mode!=0 && mode!=3 && !batch.begin())return {};
  Vu0SatJob jobs[2][Vu0SatBatch::Capacity];int ids[2][Vu0SatBatch::Capacity];
  Vu0SatResult results[Vu0SatBatch::Capacity];
  int filling=0,queued=0,active=-1,count=0;
  unsigned hash=2166136261u;
  const auto before=vu0SatMetrics;
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
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
  vu0SatProbeReference=false;
#endif
  return {elapsed,hash,vu0SatMetrics.waitTicks-before.waitTicks,vu0SatMetrics.batches-before.batches,
    vu0SatMetrics.confirmedCcd-before.confirmedCcd,vu0SatMetrics.confirmedTie-before.confirmedTie,
    vu0SatMetrics.confirmedParallel-before.confirmedParallel};
}
inline bool run(){
  static Vu0SatProbe::Case cases[Cases];static Vu0SatJob cached[Cases];
  Sample samples[Workloads][Repeats][Modes];unsigned inputHashes[Workloads],commonMotionJobs[Workloads];
  int existing=0;
  for(int w=0;w<Workloads;++w){char name[64];snprintf(name,sizeof(name),"vu0-stream-input-%d.bin",w);
    if(FILE* f=fopen(FileUtils::fromCwd(name).c_str(),"rb")){++existing;fclose(f);}}
  if(existing!=0 && existing!=Workloads){printf("VUSTREAMINPUT partial corpus set\n");return false;}
  const bool replay=existing==Workloads;
  unsigned seed=0x5354524du;bool ok=true;
  for(int workload=0;workload<Workloads;++workload){
    char inputName[64];snprintf(inputName,sizeof(inputName),"vu0-stream-input-%d.bin",workload);
    const unsigned header[4]={0x53544341u,1u,Cases,sizeof(Vu0SatProbe::Case)};
    if(replay){FILE* f=fopen(FileUtils::fromCwd(inputName).c_str(),"rb");unsigned actual[4];
      const bool valid=f && fread(actual,sizeof(actual),1,f)==1 && std::memcmp(actual,header,sizeof(header))==0 &&
        fread(cases,sizeof(cases),1,f)==1 && fgetc(f)==EOF;
      if(f)fclose(f);
      if(!valid)return false;
    } else {
    int serial=0;
    for(int i=0;i<Cases;){auto& c=cases[i];Vu0SatProbe::make(c,seed,serial++);
      if(workload){const auto target=cvAdd(c.box[0].centre,workload==1?CollisionV3{}:CollisionV3{.13f,.07f,.2f});
        const auto delta=cvSub(target,c.box[1].centre);
        c.box[1].centre=target;for(auto& v:c.vertices[1])v=cvAdd(v,delta);
        c.motion[0]=c.motion[1]={};}
      if(!packVu0Sat(cached[i],c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]))return false;
      if(!vu0SatScalarPreferred(cached[i]))++i;
    }
    FILE* f=fopen(FileUtils::fromCwd(inputName).c_str(),"wb");
    const bool saved=f && fwrite(header,sizeof(header),1,f)==1 && fwrite(cases,sizeof(cases),1,f)==1;
    if(f)fclose(f);
    if(!saved)return false;
    }
    unsigned inputHash=2166136261u;const auto* bytes=reinterpret_cast<const unsigned char*>(cases);
    for(unsigned i=0;i<sizeof(cases);++i)inputHash=(inputHash^bytes[i])*16777619u;
    inputHashes[workload]=inputHash;
    for(int i=0;i<Cases;++i){const auto& c=cases[i];if(!packVu0Sat(cached[i],c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1]))return false;}
    commonMotionJobs[workload]=0;
    for(const auto& c:cases)commonMotionJobs[workload]+=Loops*(c.motion[0].x==c.motion[1].x && c.motion[0].y==c.motion[1].y && c.motion[0].z==c.motion[1].z);
    // Validate both assembled kernels on this exact corpus outside timing.
    for(int reference=0;reference<(Modes==6?2:1);++reference){
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
    vu0SatProbeReference=reference!=0;
#endif
    Vu0SatBatch validation;if(!validation.begin())return false;
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
    const auto* first=reference?&::Vu0SatReferenceKernel_CodeStart:&::Vu0SatKernel_CodeStart;
    const auto* last=reference?&::Vu0SatReferenceKernel_CodeEnd:&::Vu0SatKernel_CodeEnd;
    const auto* loaded=reinterpret_cast<volatile unsigned*>(0x11000000);
    bool imageOk=vu0MicroOwner==first;
    for(unsigned i=0;i<unsigned(last-first);++i)imageOk &= loaded[i]==first[i];
    printf("VUSTREAMIMAGE workload=%d reference=%d bytes=%u failures=%u\n",workload,reference,unsigned(last-first)*4,imageOk?0u:1u);fflush(stdout);
    if(!imageOk)return false;
#endif
    for(int first=0;first<Cases;first+=Vu0SatBatch::Capacity){Vu0SatResult result[Vu0SatBatch::Capacity];
      const int count=std::min(Vu0SatBatch::Capacity,Cases-first);
      validation.start(cached+first,count);validation.finish(result);
      float d=0,n=0,p=0;for(int k=0;k<count;++k)
        if(!Vu0SatProbe::compare(cases[first+k],cached[first+k],result[k],d,n,p)){
          printf("VUSTREAMVALIDATE workload=%d reference=%d case=%d failure=1\n",workload,reference,first+k);ok=false;}}
    }
#ifdef TYRA_VU0_SAT_KERNEL_REFERENCE
    vu0SatProbeReference=false;
#endif
    if(!ok)return false;
    for(int repeat=0;repeat<Repeats;++repeat)for(int order=0;order<Modes;++order){
      const int mode=repeat%2?Modes-1-order:order;
      samples[workload][repeat][mode]=measure(cases,cached,mode);
    }
    for(int r=0;r<Repeats;++r){const unsigned expected=samples[workload][r][1].hash;
      ok &= expected==samples[workload][r][2].hash && expected==samples[workload][r][4].hash;
      for(int m=0;m<Modes;++m)ok &= samples[workload][r][m].ticks>0;}
  }
  FILE* f=fopen(FileUtils::fromCwd("vu0-stream.csv").c_str(),"wb");
  if(!f)return false;
  fprintf(f,"workload,repeat,mode,jobs,ticks,hash,wait_ticks,batches,confirmed_ccd,confirmed_tie,confirmed_parallel,input_hash,common_motion_jobs\n");
  for(int w=0;w<Workloads;++w)for(int r=0;r<Repeats;++r)for(int m=0;m<Modes;++m){const auto& s=samples[w][r][m];
    fprintf(f,"%d,%d,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",w,r,m,Cases*Loops,s.ticks,s.hash,s.wait,s.batches,s.confirmedCcd,s.confirmedTie,s.confirmedParallel,inputHashes[w],commonMotionJobs[w]);}
  fclose(f);
  printf("VUSTREAM cases=%d workloads=%d modes=%d repeats=%d replay=%d failures=%u file=1\n",Cases,Workloads,Modes,Repeats,replay?1:0,ok?0u:1u);fflush(stdout);
  return ok;
}
}
#endif

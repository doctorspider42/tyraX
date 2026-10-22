#pragma once
#include <cstdio>
#include <cstring>
#include "debug/debug.hpp"

// Private observer: no packet changes, waits, logging or allocation in a sample.
// Buckets are inclusive and can overlap; never sum parent and child intervals.
namespace CriticalPath {
enum Bucket { Game,Scene,Reflection,TerrainUpdate,TerrainBuild,TerrainMain,TerrainReflection,
  RoadsMain,RoadsReflection,Objects,Wheels,Particles,CoreMain,CoreReflection,
  NativeFlush,VifWait,GsWait,Pending,Vsync,Flip,Count };
inline const char* const names[]={"game","scene","reflection","terrainUpdate","terrainBuild",
  "terrainMain","terrainReflection","roadsMain","roadsReflection","objects","wheels","particles",
  "coreMain","coreReflection","nativeFlush","vifWait","gsWait","pending","vsync","flip"};
enum Mark { Start,UpdateDone,PendingEnter,PendingVifDone,PendingGsDone,VsyncDone,FlipDone,NativeStart,End,Marks };
struct Row {
  unsigned phase,offset,elapsed[Count],calls[Count],mark[Marks];
  unsigned previousLaunch,launchSequence,presentSequence,vifStat,chcr,gifStat,csr,nativeStarts;
};
inline bool active=false;
inline unsigned view=0,size=0,lastLaunch=0,lastSequence=0;
inline Row current{},rows[540]{};
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
inline void mark(Mark m){if(active)current.mark[m]=ticks();}
inline void begin(unsigned phase,unsigned offset,bool gas){
  active=gas && phase%2;view=0;
  if(!active)return;
  current={};current.phase=phase;current.offset=offset;mark(Start);
}
inline void end(){
  if(!active)return;
  mark(End);TYRA_ASSERT(size<540,"Critical path row overflow");rows[size++]=current;active=false;
}
struct Scope {
  bool on;Bucket bucket;unsigned start;
  explicit Scope(Bucket b):on(active),bucket(b),start(on?ticks():0){}
  ~Scope(){if(on){current.elapsed[bucket]+=ticks()-start;++current.calls[bucket];}}
};
struct View {
  unsigned saved;
  explicit View(unsigned v):saved(view){view=v;}
  ~View(){view=saved;}
};
inline void pending(unsigned sequence){
  if(!active)return;
  current.previousLaunch=lastLaunch;current.launchSequence=lastSequence;
  current.presentSequence=sequence;
  current.vifStat=*reinterpret_cast<volatile unsigned*>(0x10003c00);
  current.chcr=*reinterpret_cast<volatile unsigned*>(0x10009000);
  current.gifStat=*reinterpret_cast<volatile unsigned*>(0x10003020);
  current.csr=unsigned(*reinterpret_cast<volatile unsigned long long*>(0x12001000));
  mark(PendingEnter);
}
inline void launch(unsigned sequence){
  if(!active)return;
  lastLaunch=ticks();lastSequence=sequence;current.mark[NativeStart]=lastLaunch;++current.nativeStarts;
}
inline void dump(unsigned phase){
  active=false;
  TYRA_ASSERT(size==(phase%2?537U:0U),"Critical path complete gas cohort");
  char path[64];snprintf(path,sizeof(path),"host:critical-path-%u.csv",phase);
  FILE* f=fopen(path,"wb");TYRA_ASSERT(f,"Critical path output");
  fprintf(f,"phase,offset");
  for(const auto* n:names)fprintf(f,",%s,%sCalls",n,n);
  for(unsigned i=0;i<Marks;++i)fprintf(f,",mark%u",i);
  fprintf(f,",previousLaunch,launchSequence,presentSequence,vifStat,chcr,gifStat,csr,nativeStarts\n");
  for(unsigned i=0;i<size;++i){const auto& r=rows[i];fprintf(f,"%u,%u",r.phase,r.offset);
    for(unsigned b=0;b<Count;++b)fprintf(f,",%u,%u",r.elapsed[b],r.calls[b]);
    for(unsigned m:r.mark)fprintf(f,",%u",m);
    fprintf(f,",%u,%u,%u,%u,%u,%u,%u,%u\n",r.previousLaunch,r.launchSequence,r.presentSequence,r.vifStat,r.chcr,r.gifStat,r.csr,r.nativeStarts);
  }
  TYRA_ASSERT(fclose(f)==0,"Critical path close");
  printf("CRITICALPHASE phase=%u observed=%u rows=%u bytes=%u nestedInclusive=1\n",phase,phase%2,size,unsigned(sizeof(rows)));fflush(stdout);
  size=0;lastLaunch=lastSequence=0;
}
}

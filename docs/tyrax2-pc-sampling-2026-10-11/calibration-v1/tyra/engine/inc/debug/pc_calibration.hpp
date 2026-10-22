#pragma once
#include "debug/debug.hpp"
#include "debug/pc_sample_store.hpp"
#include <kernel.h>
#include <timer_alarm.h>
#include <cstdio>
namespace PcCalibration {
using U=uint32_t;
inline PcSample::Store<2048> store;
inline volatile U phase=0,callbacks=0;
inline bool complete=false;
inline U ticks(){U t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
extern "C" char pc_leaf_a_begin[],pc_leaf_a_end[],pc_leaf_b_begin[],pc_leaf_b_end[];
__attribute__((noinline)) inline void leafA(){
  asm volatile(".global pc_leaf_a_begin\npc_leaf_a_begin:");
  const U start=ticks();while(U(ticks()-start)<35389440u)asm volatile("nop":::"memory");
  asm volatile(".global pc_leaf_a_end\npc_leaf_a_end:");
}
__attribute__((noinline)) inline void leafB(){
  asm volatile(".global pc_leaf_b_begin\npc_leaf_b_begin:");
  const U start=ticks();while(U(ticks()-start)<35389440u)asm volatile("nop\n\tnop":::"memory");
  asm volatile(".global pc_leaf_b_end\npc_leaf_b_end:");
}
inline uint64_t handler(int,uint64_t scheduled,uint64_t actual,void* arg,void* pc){
  U epc;asm volatile("mfc0 %0,$14":"=r"(epc)::"memory");
  ++callbacks;
  store.push(U(reinterpret_cast<uintptr_t>(pc)),epc,phase,phase&1u,scheduled,actual);
  uint64_t delay=0;
  if(!PcSample::nextDelay(scheduled,actual,*static_cast<const uint64_t*>(arg),delay))return UINT64_MAX;
  return delay;
}
inline void run(){
  if(complete){SleepThread();return;}
  u32 used=0,unused=0;
  TYRA_ASSERT(GetTimerUsedUnusedCounters(&used,&unused)==0 && unused>0,"A37 shared SDK timer unavailable");
  printf("PCINIT used=%u unused=%u a=%08x..%08x b=%08x..%08x noPg=1\n",unsigned(used),unsigned(unused),
    unsigned(reinterpret_cast<uintptr_t>(pc_leaf_a_begin)),unsigned(reinterpret_cast<uintptr_t>(pc_leaf_a_end)),
    unsigned(reinterpret_cast<uintptr_t>(pc_leaf_b_begin)),unsigned(reinterpret_cast<uintptr_t>(pc_leaf_b_end)));
  fflush(stdout);
  for(U i=0;i<4;++i){
    phase=i;const uint64_t period=USec2TimerBusClock(i<2?997:1733);
    store.armed=true;
    const int id=SetTimerAlarm(period,handler,const_cast<uint64_t*>(&period));
    TYRA_ASSERT(id>=0,"A37 alarm allocation failed");
    if(i&1u)leafB();else leafA();
    TYRA_ASSERT(ReleaseTimerAlarm(id)==0,"A37 alarm release failed");
    store.armed=false;
    const U stopped=callbacks;
    leafA();
    TYRA_ASSERT(stopped==callbacks,"A37 callback after stop");
    printf("PCCAL phase=%u periodUs=%u callbacks=%u rows=%u dropped=%u afterStop=0\n",unsigned(i),i<2?997u:1733u,
      unsigned(callbacks),store.count,store.dropped);fflush(stdout);
  }
  TYRA_ASSERT(!store.dropped && store.count>0,"A37 empty/overflowed samples");
  FILE* f=fopen("host:pc-calibration.csv","w");TYRA_ASSERT(f,"A37 export open failed");
  fprintf(f,"pc,epc,phase,region,scheduled,actual\n");
  for(unsigned i=0;i<store.count;++i){const auto& r=store.rows[i];
    fprintf(f,"%u,%u,%u,%u,%llu,%llu\n",unsigned(r.pc),unsigned(r.epc),unsigned(r.phase),unsigned(r.region),
      static_cast<unsigned long long>(r.scheduled),static_cast<unsigned long long>(r.actual));
  }
  TYRA_ASSERT(fflush(f)==0 && !ferror(f),"A37 export write failed");
  TYRA_ASSERT(fclose(f)==0,"A37 export close failed");
  complete=true;printf("PCDONE rows=%u qualificationOnly=1\n",store.count);fflush(stdout);
}
}

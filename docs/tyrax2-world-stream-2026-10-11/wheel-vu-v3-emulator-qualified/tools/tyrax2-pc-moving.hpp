#pragma once
#include "debug/debug.hpp"
#include "debug/pc_sample_store.hpp"
#include <kernel.h>
#include <timer_alarm.h>
#include <cstdio>
namespace PcMoving {
using U=uint32_t;
inline PcSample::Store<24576> store;
inline volatile U phase=0,region=0,callbacks=0;
inline uint64_t period=0;
inline int alarm=-1;
inline U kind=0,periodUs=0;
inline uint64_t handler(int,uint64_t scheduled,uint64_t actual,void*,void* pc){
  U epc;asm volatile("mfc0 %0,$14":"=r"(epc)::"memory");
  ++callbacks;
  store.push(U(reinterpret_cast<uintptr_t>(pc)),epc,phase,region,scheduled,actual);
  uint64_t delay=0;
  return PcSample::nextDelay(scheduled,actual,period,delay)?delay:UINT64_MAX;
}
inline void start(U p,U k,U us){
  TYRA_ASSERT(alarm<0 && !store.armed,"A37 nested sampler");
  phase=p;kind=k;periodUs=us;region=0;callbacks=0;store.reset();
  if(!us)return;
  period=USec2TimerBusClock(us);store.armed=true;
  alarm=SetTimerAlarm(period,handler,nullptr);
  TYRA_ASSERT(alarm>=0,"A37 moving alarm allocation");
}
inline void mark(U offset,U stage){region=(offset<<4)|stage;}
inline void stop(){
  if(alarm>=0){TYRA_ASSERT(ReleaseTimerAlarm(alarm)==0,"A37 moving release");alarm=-1;}
  store.armed=false;
}
inline void exportRows(){
  TYRA_ASSERT(alarm<0 && !store.armed && !store.dropped,"A37 invalid stopped export");
  const U stopped=callbacks;
  TYRA_ASSERT(callbacks==store.count,"A37 callback/row mismatch");
  char name[80];snprintf(name,sizeof(name),"host:pc-%s-%u.csv",kind?"held":"drive",unsigned(phase));
  FILE* f=fopen(name,"w");TYRA_ASSERT(f,"A37 moving export open");
  fprintf(f,"pc,epc,phase,region,scheduled,actual\n");
  for(unsigned i=0;i<store.count;++i){const auto& r=store.rows[i];
    fprintf(f,"%u,%u,%u,%u,%llu,%llu\n",unsigned(r.pc),unsigned(r.epc),unsigned(r.phase),unsigned(r.region),
      static_cast<unsigned long long>(r.scheduled),static_cast<unsigned long long>(r.actual));
  }
  TYRA_ASSERT(fflush(f)==0 && !ferror(f) && fclose(f)==0,"A37 moving export write");
  TYRA_ASSERT(callbacks==stopped,"A37 moving callback after release");
  printf("PCMOVING phase=%u kind=%u periodUs=%u rows=%u dropped=%u afterStop=0\n",
    unsigned(phase),unsigned(kind),unsigned(periodUs),store.count,store.dropped);fflush(stdout);
}
}

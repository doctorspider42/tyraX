#pragma once
#include "debug/debug.hpp"
#include "world_held_order.hpp"
#include <cstdio>
namespace WorldMoving {
inline bool configured=false,enabled=false;
inline unsigned phase=~0U,frames=0,gasFrames=0,requested=0,submitted=0,fallback=0;
inline unsigned prepares=0,prepareTicks=0,dirtyFrames=0,captures=0;
inline unsigned localRebuilds=0,localTicks=0,heldOldReaders=0;
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t));return t;}
inline void configure(){
  if(configured)return;
  FILE* f=fopen("host:world-moving-control.txt","rb");
  const int mode=f?fgetc(f):-1,extra=f?fgetc(f):-1;
  const bool closed=f && fclose(f)==0;
  TYRA_ASSERT(closed && (mode=='0' || mode=='1') && extra==EOF,"A44 moving control invalid");
  enabled=mode=='1';configured=true;
  printf("A44MOVINGCONFIG enabled=%u fixedStep=1 order=2 deferred=0 incremental=1 roles=production\n",unsigned(enabled));
  fflush(stdout);
}
inline void begin(unsigned p){
  configure();
  if(phase==p)return;
  phase=p;frames=gasFrames=requested=submitted=fallback=prepares=prepareTicks=dirtyFrames=captures=0;
  localRebuilds=localTicks=heldOldReaders=0;
}
inline void finish(){
  printf("A44MOVING phase=%u enabled=%u frames=%u gasFrames=%u requested=%u submitted=%u fallback=%u prepares=%u prepareTicks=%u dirtyFrames=%u captures=%u\n",
    phase,unsigned(enabled),frames,gasFrames,requested,submitted,fallback,prepares,prepareTicks,dirtyFrames,captures);
  printf("A44LOCALPHASE phase=%u rebuilds=%u ticks=%u heldReaders=%u\n",phase,localRebuilds,localTicks,heldOldReaders);
  fflush(stdout);
}
}

#pragma once
#include "debug/debug.hpp"
#include "world_held_order.hpp"
#include <cstdio>
namespace WorldMoving {
inline bool configured=false,enabled=false;
inline bool skipA31=false,roadCells=false;
inline unsigned roadRequested=0,roadSubmitted=0,roadFallback=0,roadCompiled=0;
inline unsigned legacyCalls=0,legacyPrepared=0;
inline unsigned phase=~0U,frames=0,gasFrames=0,requested=0,submitted=0,fallback=0;
inline unsigned prepares=0,prepareTicks=0,dirtyFrames=0,captures=0;
inline unsigned localRebuilds=0,localTicks=0,heldOldReaders=0;
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t));return t;}
inline void begin(unsigned p){
  constexpr unsigned modes[]={0,1,0,1,0,1};
  enabled=p<6;skipA31=true;roadCells=p<6 && modes[p];
  if(phase==p)return;
  phase=p;frames=gasFrames=requested=submitted=fallback=prepares=prepareTicks=dirtyFrames=captures=0;
  localRebuilds=localTicks=heldOldReaders=0;
  legacyCalls=legacyPrepared=0;
  roadRequested=roadSubmitted=roadFallback=roadCompiled=0;
  printf("A44PRICECONFIG phase=%u candidate=%u autoOrder=1 fixedStep=1 clipObserver=0 shakeReset=1 variant=road-cells\n",p,unsigned(roadCells));
  fflush(stdout);
}
inline void finish(){
  printf("A44MOVING phase=%u enabled=%u frames=%u gasFrames=%u requested=%u submitted=%u fallback=%u prepares=%u prepareTicks=%u dirtyFrames=%u captures=%u\n",
    phase,unsigned(enabled),frames,gasFrames,requested,submitted,fallback,prepares,prepareTicks,dirtyFrames,captures);
  printf("A44LOCALPHASE phase=%u rebuilds=%u ticks=%u heldReaders=%u\n",phase,localRebuilds,localTicks,heldOldReaders);
  printf("A44A31PHASE phase=%u skip=%u calls=%u prepared=%u\n",phase,unsigned(skipA31),legacyCalls,legacyPrepared);
  printf("A44ROADPHASE phase=%u enabled=%u compiled=%u requested=%u submitted=%u fallback=%u\n",phase,unsigned(roadCells),roadCompiled,roadRequested,roadSubmitted,roadFallback);
  fflush(stdout);
}
}

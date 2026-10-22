#pragma once
#include "debug/command_replay.hpp"
#include "debug/bag_tape.hpp"
#include "thread/threading.hpp"
#include <graph.h>
namespace TapePricing {
using Tyra::RendererCore;
using U=unsigned;
inline U phase=0,offset=1380;
inline bool configured=false;
inline U tick(){U x;asm volatile("mfc0 %0,$9":"=r"(x)::"memory");return x;}
inline void configure(){
  if(configured)return;configured=true;
  FILE* f=fopen("host:three-path.cfg","rb");
  if(f){U p,o;int n=fscanf(f,"%u %u",&p,&o);fclose(f);
    TYRA_ASSERT(n==2 && (p==0 || p==3) && o>=1200 && o<=1600,"Invalid three-path config");phase=p;offset=o;}
}
constexpr U length=224,stages=10,count=9*128;
// O-L-O-C-O followed by C-O-L-O reverses the intervention order.
inline U mode(U s){constexpr U modes[stages]={0,0,1,0,2,0,2,0,1,0};return modes[s];}
struct Sample {U stage,local,whole,pacing,startup,prepare;};
inline Sample samples[count];inline U used=0,closureHash=0;
inline U hash(const void* p,U n){U h=2166136261u;auto*b=static_cast<const unsigned char*>(p);for(U i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
inline U command(RendererCore& core,bool verify,U& startupTicks){
  U start=tick();CommandTape::startup(core,verify);startupTicks=tick()-start;
  if(core.getFrameYield())Tyra::Threading::switchThread();
  core.sync.clear();CommandTape::submitOwned(CommandTape::memory);core.sync.waitAndClear();
  start=tick();if(core.gs.getFrameBufferCount()<3 && core.getFrameLimit())graph_wait_vsync();
  core.gs.flipBuffers(core.getFrameLimit());++CommandTape::replays;return tick()-start;
}
inline void dump(){
  TYRA_ASSERT(used==count,"Missing three-path samples");
  CommandTape::binary("host:command-times.bin",samples,sizeof(samples));
  printf("PRICEDONE stages=%u samples=%u commands=%u phase=%u offset=%u\n",stages,used,unsigned(CommandTape::replays),phase,offset);fflush(stdout);
}
}

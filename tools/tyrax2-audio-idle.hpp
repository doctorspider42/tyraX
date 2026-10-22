#pragma once
#include <cstdio>
namespace AudioIdle {
inline volatile bool enabled=false;
inline volatile bool observe=true;
inline volatile unsigned epoch=0,fast=0,slow=0,active=0;
inline volatile unsigned pcmStarts=0;
inline void set(bool value){if(enabled!=value){enabled=value;++epoch;}}
inline void setObserved(bool value){if(observe!=value){observe=value;++epoch;}}
inline void reset(){++epoch;fast=slow=active=0;}
inline void witness(unsigned phase,int arm=-1){
  std::printf("AUDIOIDLE phase=%u arm=%d enabled=%u idle16=%u idleHalf=%u pcm=%u starts=%u\n",
    phase,arm,unsigned(enabled),unsigned(fast),unsigned(slow),unsigned(active),unsigned(pcmStarts));
  std::fflush(stdout);
}
}

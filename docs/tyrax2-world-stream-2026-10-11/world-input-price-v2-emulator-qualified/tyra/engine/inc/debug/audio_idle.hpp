#pragma once
#include <cstdio>
#ifdef AUDIO_EVENT_HOST
#include <atomic>
#define AUDIO_EVENT_VALUE(type) std::atomic<type>
#else
#include <kernel.h>
#include "debug/debug.hpp"
#define AUDIO_EVENT_VALUE(type) volatile type
#endif
namespace AudioIdle {
inline AUDIO_EVENT_VALUE(bool) enabled=false,observe=true,waiting=false;
inline AUDIO_EVENT_VALUE(unsigned) epoch=0,fast=0,slow=0,active=0,pcmStarts=0;
inline int wake=-1;
inline void init(){
  if(wake>=0)return;
  ee_sema_t sema{};sema.init_count=0;sema.max_count=1;
  wake=CreateSema(&sema);TYRA_ASSERT(wake>=0,"Audio wake semaphore creation failed");
}
inline void notify(){if(wake>=0)SignalSema(wake);}
inline void set(bool value){if(enabled!=value){enabled=value;++epoch;notify();}}
inline void setObserved(bool value){if(observe!=value){observe=value;++epoch;}}
inline void reset(){++epoch;fast=slow=active=0;}
inline void waitEmpty(){
  TYRA_ASSERT(wake>=0,"Audio wake semaphore not initialized");
  waiting=true;
  const int result=WaitSema(wake);
  waiting=false;
  TYRA_ASSERT(result>=0,"Audio wake semaphore wait failed");
}
inline void witness(unsigned phase,int arm=-1){
  ee_sema_t status{};
  TYRA_ASSERT(ReferSemaStatus(wake,&status)>=0,"Audio semaphore status failed");
  std::printf("AUDIOEVENT phase=%u arm=%d enabled=%u waiting=%u waiters=%u idleHalf=%u pcm=%u starts=%u\n",
    phase,arm,unsigned(enabled),unsigned(waiting),unsigned(status.wait_threads),
    unsigned(slow),unsigned(active),unsigned(pcmStarts));std::fflush(stdout);
}
}
#undef AUDIO_EVENT_VALUE

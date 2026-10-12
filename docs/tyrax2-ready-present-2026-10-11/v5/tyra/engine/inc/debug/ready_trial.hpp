#pragma once
#include "debug/debug.hpp"
#include <cstdio>
namespace ReadyTrial {
inline bool enabled=false;
inline unsigned checks=0,presented=0,protectedPrefixes=0;
inline void reset(bool on){enabled=on;checks=presented=protectedPrefixes=0;}
inline void witness(unsigned phase,unsigned kind,unsigned arm){
  printf("READYWITNESS phase=%u kind=%u arm=%u mode=%u checks=%u presented=%u protected=%u\n",
         phase,kind,arm,unsigned(enabled),checks,presented,protectedPrefixes);fflush(stdout);
}
}

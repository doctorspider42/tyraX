#pragma once
#include <cstdio>
namespace PaintCellPrice {
inline bool enabled=false,held=false;
inline unsigned heads=0,vuHeads=0,headWords=0,paintWords=0;
inline void reset(){heads=vuHeads=headWords=paintWords=0;}
inline void witness(unsigned phase,unsigned arm){
  printf("A44PAINTIMAGE phase=%u arm=%u enabled=%u heads=%u vuHeads=%u headWords=%u paintWords=%u\n",
    phase,arm,unsigned(enabled),heads,vuHeads,headWords,paintWords);fflush(stdout);
}
}

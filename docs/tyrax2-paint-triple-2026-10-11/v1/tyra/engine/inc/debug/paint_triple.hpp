#pragma once
#include <cstdio>
namespace PaintTriple {
inline bool enabled=false;
inline unsigned coldHeads=0;
inline void witness(unsigned phase,unsigned arm){
  printf("PAINTTRIPLE phase=%u arm=%u mode=%u heads=%u\n",phase,arm,unsigned(enabled),coldHeads);fflush(stdout);
}
}

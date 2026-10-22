#pragma once
#include "debug/debug.hpp"
#include <cstdio>
namespace TripleTrial {
struct Row { unsigned tick,slot,isr; };
inline Row rows[1200];
inline volatile unsigned count=0,dropped=0;
inline volatile bool armed=false,inVBlank=false;
inline unsigned phase=0;
inline void record(unsigned slot){
  if(!armed)return;
  unsigned t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");
  const unsigned n=count;
  if(n>=1200){++dropped;return;}
  rows[n]={t,slot,unsigned(inVBlank)};count=n+1;
}
inline void start(unsigned p){phase=p;count=dropped=0;armed=true;}
inline void stopAndExport(){
  armed=false;
  TYRA_ASSERT(!dropped && count>120,"A38 invalid presentation capture");
  char path[80];snprintf(path,sizeof(path),"host:triple-present-%u.csv",phase);
  FILE* f=fopen(path,"w");TYRA_ASSERT(f,"A38 presentation export open");
  fprintf(f,"tick,slot,isr\n");
  for(unsigned i=0;i<count;++i)fprintf(f,"%u,%u,%u\n",rows[i].tick,rows[i].slot,rows[i].isr);
  TYRA_ASSERT(fflush(f)==0 && !ferror(f) && fclose(f)==0,"A38 presentation export write");
  printf("TRIPLEPRESENT phase=%u rows=%u dropped=%u\n",phase,unsigned(count),unsigned(dropped));fflush(stdout);
}
}

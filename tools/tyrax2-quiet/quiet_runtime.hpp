#pragma once
#include "quiet_cadence.hpp"
#include <stdio.h>
#include <ctype.h>
#ifndef PRIVATE_QUIET_HOST
#include "debug/frame_profile.hpp"
#include "debug/hardware_trace.hpp"
static_assert(TYRA_FRAME_PROFILE==0 && TYRA_HARDWARE_TRACE==0,"private quiet fixture requires both profilers compiled out");
#endif
namespace QuietRuntime {
inline FILE* file=nullptr;inline bool initialized=false,done=false;
inline bool readFlag(const char* path,unsigned& value,bool optional=false,unsigned defaultValue=0){
 FILE* f=fopen(path,"r");if(!f){value=defaultValue;return optional;}
 int first;do{first=fgetc(f);}while(first!=EOF&&isspace(static_cast<unsigned char>(first)));
 bool good=first=='0'||first=='1';value=good?unsigned(first-'0'):defaultValue;
 int c;while((c=fgetc(f))!=EOF)if(!isspace(static_cast<unsigned char>(c)))good=false;
 if(ferror(f))good=false;
 fclose(f);return good;
}
inline void init(const char* cfg,const char* artifact){unsigned order=0;const bool good=readFlag(cfg,order);QuietCadence::init(order);file=fopen(artifact,"w");QuietCadence::valid=QuietCadence::valid&&good&&file;initialized=true;done=false;}
inline void config(unsigned video,unsigned display,unsigned depth,unsigned width,unsigned height,unsigned refreshMilliHz,unsigned requested,unsigned night,unsigned rasterWidth,unsigned rasterHeight,unsigned scaleX,unsigned scaleY){
 printf("LOG: QUIETCONFIG order=%u valid=%u profile=0 hardwareTrace=0 ringBytes=0 ordinaryClocks=1 fixedDt=0 sampleBytes=%u chunkBytes=%u phases=3 phaseFrames=1800 taxFirst=800 taxLoops=320 chunkFrames=64 sampleFirst=900 sampleCount=128 video=%u display=%u depth=%u width=%u height=%u refreshMilliHz=%u requested=%u night=%u\n",unsigned(QuietCadence::order),QuietCadence::valid?1u:0u,unsigned(sizeof(QuietCadence::samples)),unsigned(sizeof(QuietCadence::chunks)),video,display,depth,width,height,refreshMilliHz,requested,night);fflush(stdout);
 printf("LOG: QUIETQUALITY rasterWidth=%u rasterHeight=%u scaleX=%u scaleY=%u\n",rasterWidth,rasterHeight,scaleX,scaleY);fflush(stdout);
}
inline void begin(unsigned cumulativePacing){
 if(!initialized)return;
 const unsigned index=QuietCadence::expectedIndex,p=index/1800,o=index%1800;
 if(p<3&&o==0){printf("LOG: QUIETPHASE phase=%u first=%u sampler=%u countReads=%u samplePtr=%08x\n",p,index,QuietCadence::mode(p)?1u:0u,QuietCadence::mode(p)?262u:6u,unsigned(reinterpret_cast<uintptr_t>(QuietCadence::samples)));fflush(stdout);}
 QuietCadence::beginLoop(index,cumulativePacing);
}
inline void afterLoop(unsigned cumulativePacing){
 if(!initialized)return;
 using namespace QuietCadence;
 endLoop(cumulativePacing);const unsigned p=frame/1800,o=frame%1800;
 if(p<3&&o>=1200&&o<1328){
  if(!file){valid=false;return;}const unsigned i=o-1200;
  if(i<5){const auto& c=chunks[p][i];fprintf(file,"LOG: QUIETCHUNK phase=%u i=%u first=%u loops=%u elapsed=%u pacing=%u totalFlips=%u renderedFlips=%u syntheticFlips=%u\n",p,i,unsigned(c.first),unsigned(c.loops),unsigned(c.elapsed),unsigned(c.pacing),unsigned(c.totalFlips),unsigned(c.renderedFlips),unsigned(c.syntheticFlips));}
  if(mode(p)){const auto& s=samples[i];fprintf(file,"LOG: QUIETRAW phase=%u i=%u frame=%u entryPeriod=%u wholeLoop=%u pacing=%u totalFlips=%u renderedFlips=%u syntheticFlips=%u presentPeriod=%u renderedPeriod=%u lastKind=%u sequence=%u context=%u\n",p,i,unsigned(s.frame),unsigned(s.entryPeriod),unsigned(s.wholeLoop),unsigned(s.pacing),unsigned(s.totalFlips),unsigned(s.renderedFlips),unsigned(s.syntheticFlips),unsigned(s.presentPeriod),unsigned(s.renderedPeriod),unsigned(s.lastKind),unsigned(s.sequence),unsigned(s.context));}
  if(ferror(file))valid=false;
  if(i==127){markExported(p);fprintf(file,"LOG: QUIETWINDOW phase=%u sampler=%u raw=%u chunks=%u valid=%u\n",p,mode(p)?1u:0u,unsigned(counts[p]),unsigned(chunkCounts[p]),valid?1u:0u);if(fflush(file)!=0)valid=false;}
 }
 if(frame==5399&&!done){done=true;if(file){if(fclose(file)!=0)valid=false;file=nullptr;}printf("LOG: QUIETDONE order=%u valid=%u loops=5400\n",unsigned(order),complete()?1u:0u);fflush(stdout);}
}
}

#pragma once
// PRIVATE closeout observer. No added waits, no replacement production clocks.
#include "night_sampler.hpp"
#include "debug/frame_profile.hpp"
#include "debug/hardware_trace.hpp"
#include <stdio.h>
static_assert(TYRA_FRAME_PROFILE==0 && TYRA_HARDWARE_TRACE==0,"closeout adds no fairness drain or hot profiler");
namespace Closeout {
inline unsigned order=0,kind=0;
inline bool initialized=false,done=false,restricted=false;
inline FILE* file=nullptr;
inline bool middle(unsigned phase){return(phase==1)!=(order==1);}
inline bool geometryHidden(){return !done&&kind==2&&middle(NightSampler::phase);}
inline bool cold(){const unsigned f=NightSampler::frame;return f<5400&&(f%1800==750||f%1800==1155);}
inline void init(const char* cfg,const char* output){
 FILE* in=fopen(cfg,"r");char trailing;bool good=in&&fscanf(in,"%u %u %c",&order,&kind,&trailing)==2&&order<=1&&kind<=3;if(in)fclose(in);
 NightSampler::PhaseConfig plan[3]{};for(unsigned p=0;p<3;++p)plan[p]={0,kind==1?unsigned(middle(p)):1u};NightSampler::initPlan(plan);NightSampler::order=order;
 file=fopen(output,"w");NightSampler::valid=NightSampler::valid&&good&&file;initialized=true;
 printf("LOG: CLOSEOUTCONFIG order=%u kind=%u valid=%u phases=3 phaseFrames=1800 taxFirst=800 taxLoops=320 sampleFirst=900 sampleCount=128 ordinaryClocks=1 addedWaits=0 frameProfile=0 hardwareTrace=0 devkit=0 sampleBytes=%u chunkBytes=%u\n",order,kind,NightSampler::valid?1u:0u,unsigned(sizeof(NightSampler::samples)),unsigned(sizeof(NightSampler::chunks)));fflush(stdout);
}
inline void begin(unsigned pacing){
 if(!initialized) return;
 const unsigned f=NightSampler::expectedIndex,p=f/1800,o=f%1800;
 if(p<3){restricted=false; /* PRIVATE fixed public All receivers for daylight diagnosis */if(o==0){printf("LOG: CLOSEOUTPHASE phase=%u first=%u players=%u geometryHidden=%u sampler=%u countReads=%u\n",p,f,unsigned(restricted),unsigned(kind==2&&middle(p)),unsigned(NightSampler::mode(p)),NightSampler::mode(p)?262u:6u);fflush(stdout);}}
 NightSampler::beginLoop(f,pacing);
}
inline void after(unsigned pacing){
 if(!initialized) return;
 using namespace NightSampler;endLoop(pacing);const unsigned p=frame/1800,o=frame%1800;
 if(p<3&&o>=1200&&o<1328){
  if(!file){valid=false;return;}const unsigned i=o-1200;
  if(i<5){const auto& c=chunks[p][i];fprintf(file,"LOG: CLOSEOUTCHUNK phase=%u i=%u first=%u loops=%u elapsed=%u pacing=%u totalFlips=%u renderedFlips=%u syntheticFlips=%u\n",p,i,unsigned(c.first),unsigned(c.loops),unsigned(c.elapsed),unsigned(c.pacing),unsigned(c.totalFlips),unsigned(c.renderedFlips),unsigned(c.syntheticFlips));}
  if(mode(p)){const auto& s=samples[i];fprintf(file,"LOG: CLOSEOUTRAW phase=%u i=%u frame=%u entryPeriod=%u wholeLoop=%u pacing=%u totalFlips=%u renderedFlips=%u syntheticFlips=%u presentPeriod=%u renderedPeriod=%u lastKind=%u sequence=%u context=%u\n",p,i,unsigned(s.frame),unsigned(s.entryPeriod),unsigned(s.wholeLoop),unsigned(s.pacing),unsigned(s.totalFlips),unsigned(s.renderedFlips),unsigned(s.syntheticFlips),unsigned(s.presentPeriod),unsigned(s.renderedPeriod),unsigned(s.lastKind),unsigned(s.sequence),unsigned(s.context));}
  if(ferror(file))valid=false;
  if(i==127){markExported(p);fprintf(file,"LOG: CLOSEOUTWINDOW phase=%u sampler=%u raw=%u chunks=%u valid=%u\n",p,unsigned(mode(p)),unsigned(counts[p]),unsigned(chunkCounts[p]),unsigned(valid));if(fflush(file)!=0)valid=false;}
 }
 if(frame==5399&&!done){done=true;if(file){if(fclose(file)!=0)valid=false;file=nullptr;}printf("LOG: CLOSEOUTDONE order=%u kind=%u valid=%u loops=5400\n",Closeout::order,Closeout::kind,unsigned(complete()));fflush(stdout);}
}
}

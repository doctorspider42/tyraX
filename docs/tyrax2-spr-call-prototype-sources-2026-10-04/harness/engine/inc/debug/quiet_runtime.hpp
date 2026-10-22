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
void protoInit(); void protoBeforeLoop(unsigned); void protoAfterLoop(unsigned);
// Exactly four unsigned decimal fields: mode selector ordinal expectedCount.
inline bool readScopeConfig(const char* path,QuietScope::Config& cfg){
 FILE* f=fopen(path,"r");if(!f)return false;unsigned values[4]{};bool good=true;int c=fgetc(f);
 for(unsigned i=0;i<4;++i){while(c!=EOF&&isspace(static_cast<unsigned char>(c)))c=fgetc(f);if(c<'0'||c>'9'){good=false;break;}unsigned n=0;do{const unsigned digit=unsigned(c-'0');if(n>(0xffffffffU-digit)/10U){good=false;break;}n=n*10U+digit;c=fgetc(f);}while(c>='0'&&c<='9');if(!good)break;values[i]=n;if(c!=EOF&&!isspace(static_cast<unsigned char>(c))){good=false;break;}}
 while(good&&c!=EOF){if(!isspace(static_cast<unsigned char>(c)))good=false;c=fgetc(f);}if(ferror(f))good=false;fclose(f);cfg={values[0],values[1],values[2],values[3]};return good&&QuietScope::configValid(cfg);
}

inline FILE* file=nullptr;inline bool initialized=false,done=false;
inline bool readFlag(const char* path,unsigned& value,bool optional=false,unsigned defaultValue=0){
 FILE* f=fopen(path,"r");if(!f){value=defaultValue;return optional;}
 int first;do{first=fgetc(f);}while(first!=EOF&&isspace(static_cast<unsigned char>(first)));
 bool good=first=='0'||first=='1';value=good?unsigned(first-'0'):defaultValue;
 int c;while((c=fgetc(f))!=EOF)if(!isspace(static_cast<unsigned char>(c)))good=false;
 if(ferror(f))good=false;
 fclose(f);return good;
}
inline void init(const char* cfg,const char* artifact){unsigned order=0;const bool good=readFlag(cfg,order);QuietCadence::init(order);QuietScope::Config scope;const bool scopeGood=readScopeConfig("host:quiet-scope.cfg",scope);QuietScope::init(scope);QuietCadence::valid=QuietCadence::valid&&scopeGood;file=fopen(artifact,"w");QuietCadence::valid=QuietCadence::valid&&good&&file;initialized=true;done=false;protoInit();}
inline void config(unsigned video,unsigned display,unsigned depth,unsigned width,unsigned height,unsigned refreshMilliHz,unsigned requested,unsigned night,unsigned rasterWidth,unsigned rasterHeight,unsigned scaleX,unsigned scaleY){
 printf("LOG: SCOPECONFIG schema=3 mode=%u selector=%u ordinal=%u expectedCount=%u recordBytes=%u ringBytes=%u commonBytes=%u scopeReadsPerHit=2 scopeReadsPerSampleLoop=%u dynamicScopeReads=%u scopeSampleFirst=900 scopeSampleCount=128 warmExcluded=1 commonRecordsAllPhases=1\n",QuietScope::config.mode,QuietScope::config.selector,QuietScope::config.ordinal,QuietScope::config.expectedCount,unsigned(sizeof(QuietScope::Record)),unsigned(sizeof(QuietScope::records)),unsigned(sizeof(QuietScope::records)+sizeof(QuietCadence::samples)+sizeof(QuietCadence::chunks)),QuietScope::readsPerSample(),QuietScope::config.mode==3?1u:0u);fflush(stdout);
 printf("LOG: QUIETCONFIG order=%u valid=%u profile=0 hardwareTrace=0 ringBytes=0 ordinaryClocks=1 fixedDt=0 sampleBytes=%u chunkBytes=%u phases=3 phaseFrames=1800 taxFirst=800 taxLoops=320 chunkFrames=64 sampleFirst=900 sampleCount=128 video=%u display=%u depth=%u width=%u height=%u refreshMilliHz=%u requested=%u night=%u\n",QuietCadence::order,QuietCadence::valid?1u:0u,unsigned(sizeof(QuietCadence::samples)),unsigned(sizeof(QuietCadence::chunks)),video,display,depth,width,height,refreshMilliHz,requested,night);fflush(stdout);
 printf("LOG: QUIETQUALITY rasterWidth=%u rasterHeight=%u scaleX=%u scaleY=%u\n",rasterWidth,rasterHeight,scaleX,scaleY);fflush(stdout);
}
inline void phaseReads(unsigned p,unsigned scopeReads){printf("LOG: MINPHASE phase=%u cadenceCountReads=%u scopeCountReads=%u totalCountReads=%u scopePtr=%08x\n",p,QuietCadence::mode(p)?262u:6u,QuietCadence::mode(p)?scopeReads:0u,QuietCadence::mode(p)?262u+scopeReads:6u,unsigned(reinterpret_cast<uintptr_t>(QuietScope::records)));fflush(stdout);}
inline void begin(unsigned cumulativePacing){
 if(!initialized)return;
 const unsigned index=QuietCadence::expectedIndex,p=index/1800,o=index%1800;
 protoBeforeLoop(index);
 if(p<3&&o==0){if(QuietScope::config.mode!=3)phaseReads(p,QuietScope::readsPerOnPhase());printf("LOG: QUIETPHASE phase=%u first=%u sampler=%u countReads=%u samplePtr=%08x\n",p,index,QuietCadence::mode(p)?1u:0u,QuietCadence::mode(p)?262u:6u,unsigned(reinterpret_cast<uintptr_t>(QuietCadence::samples)));fflush(stdout);}
 QuietCadence::beginLoop(index,cumulativePacing);QuietScope::begin(index,QuietCadence::enabled);
}
inline void afterLoop(unsigned cumulativePacing){
 if(!initialized)return;
 using namespace QuietCadence;
 QuietScope::finish();valid=valid&&QuietScope::valid;endLoop(cumulativePacing);const unsigned p=frame/1800,o=frame%1800;
 protoAfterLoop(frame);
 if(p<3&&o==1155&&QuietScope::config.mode>=2){printf("LOG: MINAGG phase=%u offset=1155 samples=%u vertexSumMin=%u vertexSumMax=%u coreCallsMin=%u coreCallsMax=%u invalidSamples=%u\n",p,QuietScope::rows[p],QuietScope::vertexMin[p],QuietScope::vertexMax[p],QuietScope::callsMin[p],QuietScope::callsMax[p],QuietScope::invalidSamples[p]);fflush(stdout);}
 if(p<3&&o==1155&&QuietScope::config.mode==3){phaseReads(p,256u+2u*QuietScope::workTotal[p]);printf("LOG: MINWORK phase=%u offset=1155 samples=%u workCallsMin=%u workCallsMax=%u workCallsTotal=%u\n",p,QuietScope::rows[p],QuietScope::workMin[p],QuietScope::workMax[p],QuietScope::workTotal[p]);fflush(stdout);}
 if(p<3&&o>=1200&&o<1328){
  if(!file){valid=false;return;}const unsigned i=o-1200;
  if(i<5){const auto& c=chunks[p][i];fprintf(file,"LOG: QUIETCHUNK phase=%u i=%u first=%u loops=%u elapsed=%u pacing=%u totalFlips=%u renderedFlips=%u syntheticFlips=%u\n",p,i,c.first,c.loops,c.elapsed,c.pacing,c.totalFlips,c.renderedFlips,c.syntheticFlips);}
  {const auto& r=QuietScope::records[i];fprintf(file,"LOG: SCOPERAW phase=%u i=%u frame=%u duration=%u bagCount=%u packed=%u\n",p,i,p*1800+900+i,r.duration,r.bagCount,r.packed);}
  if(mode(p)){const auto& s=samples[i];fprintf(file,"LOG: QUIETRAW phase=%u i=%u frame=%u entryPeriod=%u wholeLoop=%u pacing=%u totalFlips=%u renderedFlips=%u syntheticFlips=%u presentPeriod=%u renderedPeriod=%u lastKind=%u sequence=%u context=%u\n",p,i,s.frame,s.entryPeriod,s.wholeLoop,s.pacing,s.totalFlips,s.renderedFlips,s.syntheticFlips,s.presentPeriod,s.renderedPeriod,s.lastKind,s.sequence,s.context);}
  if(ferror(file))valid=false;
  if(i==127){if(QuietScope::rows[p]!=128)valid=false;markExported(p);fprintf(file,"LOG: QUIETWINDOW phase=%u sampler=%u raw=%u chunks=%u valid=%u\n",p,mode(p)?1u:0u,counts[p],chunkCounts[p],valid?1u:0u);if(fflush(file)!=0)valid=false;}
 }
 if(frame==5399&&!done){done=true;if(file){if(fclose(file)!=0)valid=false;file=nullptr;}printf("LOG: QUIETDONE order=%u valid=%u loops=5400\n",order,complete()?1u:0u);fflush(stdout);}
}
}

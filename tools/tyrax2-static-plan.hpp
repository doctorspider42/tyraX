#pragma once
// Private fixed-view intervention; never included by production sources.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
namespace MotionProbe {
using U=unsigned;
enum Counter { Bags, Outside, Direct, Replay, Partial, Packages, ExpandVerts,
  CopyBytes, SnapshotBytes, BakeMiss, Churn, ProbeCapture, Paint, Rebuild,
  MissNew, MissBBox, MissContent, MissPrim, MissStreams, MissProgram,
  MissDepth, MissSize, Expired, Evicted, ReserveFailed, CachePeakBytes,
  CachePeakEntries, PartialReplay, PartialBytes, PartialBuild,
  ExpandCachedVerts, ExpandBuilds, Count };
inline U frame=0,phase=0,offset=0,counters[Count]{},start=0,paceStart=0;
inline U previous=0,renderStamp=0,flips=0,flipStart=0;
inline bool sample=false,enabled=false,valid=true,target=false;
inline U coreTicks=0,coreCalls=0,descHits=0,viewHits=0,misses=0,checks=0,errors=0;
inline U packagesIn=0,packagesOut=0,packagesPartial=0,slots=0,resident=0;
inline U cumulativeChecks=0,cumulativeErrors=0;
inline const void* owners[128]{};
inline U ownerCount=0;
inline FILE* output=nullptr;
inline constexpr U phaseLength=540,sampleStart=300,sampleCount=180,phaseCount=36;
inline U variant(){const U modes[12]={0,1,0,2,0,2,1,0,1,0,2,0};return modes[phase%12];}
inline bool observed(){return phase%12<6;}
inline U view(){return phase/12;}
inline U ticks(){U t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
inline void add(Counter c,U n=1){if(enabled&&target)counters[c]+=n;}
inline void occupancy(U,U){}
inline void onPresent(U,U,U,U){++flips;}
inline void registerOwner(const void* p){
  for(U i=0;i<ownerCount;++i)if(owners[i]==p)return;
  if(ownerCount==128){valid=false;return;}owners[ownerCount++]=p;
}
struct CoreScope {
  bool previousTarget;U stamp=0;
  CoreScope(const void* p):previousTarget(target){
    target=false;for(U i=0;i<ownerCount;++i)if(owners[i]==p){target=true;break;}
    if(target&&enabled){++coreCalls;stamp=ticks();}
  }
  ~CoreScope(){if(stamp)coreTicks+=ticks()-stamp;target=previousTarget;}
};
struct Row{U whole,pacing,period,update,render,flips,coreTicks,coreCalls,descHits,viewHits,misses,checks,errors,in,out,partial,slots,resident,c[Count];};
inline Row rows[sampleCount]{};
inline void init(){
  output=fopen("host:static-plan.csv","w");valid=output!=nullptr;
  if(output)fprintf(output,"phase,view,variant,observed,frame,whole,pacing,period,update,render,flips,coreTicks,coreCalls,descHits,viewHits,misses,checks,errors,in,out,partial,slots,resident,bags,outside,direct,replay,partialBags,packages,expandVerts,copyBytes,snapshotBytes,bakeMiss,churn,probeCapture,paint,rebuild,missNew,missBBox,missContent,missPrim,missStreams,missProgram,missDepth,missSize,expired,evicted,reserveFailed,cachePeakBytes,cachePeakEntries,partialReplay,partialBytes,partialBuild,expandCachedVerts,expandBuilds\n");
  printf("STATICINIT rowsBytes=%u profile=0 hardwareTrace=0\n",unsigned(sizeof(rows)));fflush(stdout);
}
inline void begin(U pacing){
  phase=frame/phaseLength;offset=frame%phaseLength;
  sample=phase<phaseCount&&offset>=sampleStart&&offset<sampleStart+sampleCount;
  enabled=sample&&observed();target=false;slots=0;
  coreTicks=coreCalls=descHits=viewHits=misses=checks=errors=0;
  packagesIn=packagesOut=packagesPartial=0;memset(counters,0,sizeof(counters));
  if(sample){paceStart=pacing;flipStart=flips;renderStamp=0;start=ticks();}
  else if(offset==sampleStart-1)previous=ticks();
}
inline void renderBegin(){if(sample)renderStamp=ticks();}
inline void end(U pacing){
  if(sample){const U end=ticks();auto& r=rows[offset-sampleStart];
    r={end-start,pacing-paceStart,start-previous,renderStamp-start,end-renderStamp,flips-flipStart,
       coreTicks,coreCalls,descHits,viewHits,misses,checks,errors,packagesIn,packagesOut,packagesPartial,slots,resident,{}};
    memcpy(r.c,counters,sizeof(counters));previous=start;
    if(!renderStamp||r.pacing>r.whole||r.whole>0x7fffffffu||errors)valid=false;
  }
  enabled=false;
  if(phase<phaseCount&&offset==sampleStart+sampleCount+5){
    if(output){for(U i=0;i<sampleCount;++i){const auto&r=rows[i];
      fprintf(output,"%u,%u,%u,%u,%u",phase,view(),variant(),unsigned(observed()),phase*phaseLength+sampleStart+i);
      const U* p=&r.whole;for(U k=0;k<18+Count;++k)fprintf(output,",%u",p[k]);fputc('\n',output);
    }if(fflush(output)!=0||ferror(output))valid=false;}
    printf("STATICPHASE phase=%u view=%u variant=%u valid=%u checks=%u errors=%u owners=%u resident=%u\n",phase,view(),variant(),unsigned(valid),cumulativeChecks,cumulativeErrors,ownerCount,resident);fflush(stdout);
  }
  if(frame==phaseCount*phaseLength-1){if(output){if(fclose(output))valid=false;output=nullptr;}
    printf("STATICDONE valid=%u checks=%u errors=%u\n",unsigned(valid),cumulativeChecks,cumulativeErrors);fflush(stdout);}
  ++frame;
}
}

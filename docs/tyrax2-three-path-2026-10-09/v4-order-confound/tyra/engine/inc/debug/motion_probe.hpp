#define MOTION_FREEZE_TRIAL 1
#pragma once
#include <stdio.h>
#include <string.h>
namespace TargetAudit { inline unsigned tested=0,rejected=0; }
namespace MotionProbe {
using U=unsigned int;
static_assert(sizeof(U)==4,"32-bit EE counters required");
enum Counter { Bags, Outside, Direct, Replay, Partial, Packages, ExpandVerts,
  CopyBytes, SnapshotBytes, BakeMiss, Churn, ProbeCapture, Paint, Rebuild,
  MissNew, MissBBox, MissContent, MissPrim, MissStreams, MissProgram,
  MissDepth, MissSize, Expired, Evicted, ReserveFailed, CachePeakBytes,
  CachePeakEntries, CaptureBlocked, FusedSubmit, PartialReplay, PartialBytes, PartialBuild,
  ExpandCachedVerts, ExpandBuilds, EnvRecorded, EnvFallback, BarrierOmitted, EnvEligible, EnvDrawn, EnvSkipped, PaintBlocked, PaintVu, Count };
struct Pose { float x,y,z,speed,yaw,dt,damage,eyeX,eyeY,eyeZ,lookX,lookY,lookZ; int driver,vehicles; };
enum Timing { CoreTicks, CoreWaitTicks, PartialTicks, PartialWaitTicks,
  CoreCalls, PartialCalls, WaitCalls, ScopeErrors,
  CreateTicks, ClassifyTicks, FillTicks, PacketTicks, SubmitTicks, PayloadTicks,
  CreateCalls, ClassifyCalls, FillCalls, PacketCalls, SubmitCalls, PayloadCalls,
  TimingCount };
struct Row { U frame,offset,whole,pacing,period,update,render,flips,gas,c[Count]; Pose pose; U frozen,t[TimingCount]; };
inline Row rows[1200]{};
inline Pose current{};
inline U frame=0,offset=0,phase=0,size=0,counters[Count]{},start=0,paceStart=0,previous=0,renderStamp=0,flips=0,flipStart=0;
inline bool sample=false,enabled=false,valid=true,finishPending=false,resetRequested=false;
inline FILE* output=nullptr;
inline bool captureFrozen=false;
inline U timing[TimingCount]{},coreDepth=0,partialDepth=0,waitDepth=0;
inline bool clocks=false;
inline constexpr U sampleStart=1200,gasStart=sampleStart+120;
#ifdef MOTION_FIX_TRIAL
inline constexpr U phaseCount=18;
#elif defined(MOTION_FOLLOWUP_TRIAL)
inline constexpr U phaseCount=8;
#else
inline constexpr U phaseCount=6;
#endif
inline U variant() {
  constexpr U modes[]={0,6,0,0,6,0};
  return phase<phaseCount?modes[phase]:0;
}
inline bool observing(){return false;}
inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }
inline U lifetime() {
#if defined(MOTION_FREEZE_TRIAL) || defined(MOTION_SCOPE_TRIAL) || defined(MOTION_FIX_TRIAL) || defined(MOTION_PARTIAL_TRIAL) || defined(MOTION_FOLLOWUP_TRIAL)
  return 250;
#else
  return phase==1 || phase==4 ? 1000 : 250;
#endif
}
inline bool gas() { return phase<phaseCount && offset>=gasStart && !finishPending; }
inline bool freezeCapture() { return false; }
// Private elapsed scopes: subtract only measured wait leaves, not all EE stalls.
struct Scope {
  bool active,partial; U stamp;
  Scope(bool isPartial=false,bool selected=true):active(clocks && selected),partial(isPartial),stamp(0) {
    if(!active)return;
    U& depth=partial?partialDepth:coreDepth;
    if(depth || (partial && !coreDepth))++timing[ScopeErrors];
    ++depth;++timing[partial?PartialCalls:CoreCalls];stamp=ticks();
  }
  ~Scope(){if(active){timing[partial?PartialTicks:CoreTicks]+=ticks()-stamp;--(partial?partialDepth:coreDepth);}}
};
struct Wait {
  bool active,outer; U stamp;
  Wait():active(clocks && coreDepth),outer(false),stamp(0) {
    if(!active)return;
    outer=waitDepth++==0;
    if(outer){++timing[WaitCalls];stamp=ticks();}
  }
  ~Wait(){if(active){if(outer){const U dt=ticks()-stamp;timing[CoreWaitTicks]+=dt;if(partialDepth)timing[PartialWaitTicks]+=dt;}--waitDepth;}}
};
struct Bucket;
inline Bucket* currentBucket=nullptr;
// Child intervals are deducted from their parent bucket, not from Partial.
struct Bucket {
  bool active; Timing kind; U stamp,children; Bucket* parent;
  explicit Bucket(Timing k):active(clocks && partialDepth),kind(k),stamp(0),children(0),parent(nullptr) {
    if(!active)return;
    parent=currentBucket;currentBucket=this;
    ++timing[CreateCalls+(kind-CreateTicks)];stamp=ticks();
  }
  ~Bucket(){if(active){const U elapsed=ticks()-stamp;
    if(currentBucket!=this || children>elapsed || !partialDepth)++timing[ScopeErrors];
    timing[kind]+=elapsed-children;if(parent)parent->children+=elapsed;currentBucket=parent;
  }}
};
inline void add(Counter c,U n=1) {
  if(sample && (c==ProbeCapture || c==Paint || c==CaptureBlocked || c>=EnvEligible)) counters[c]+=n;
}
inline bool skipBodyEnv(){return false;}

inline void occupancy(U qw,U entries) {
  if(!enabled)return;
  if(qw*16>counters[CachePeakBytes])counters[CachePeakBytes]=qw*16;
  if(entries>counters[CachePeakEntries])counters[CachePeakEntries]=entries;
}
inline void onPresent(U,U,U,U) { ++flips; }
inline void init() {
  output=fopen("host:drive-probe.csv","w");valid=output!=nullptr;
  if(output)fprintf(output,"phase,lifetime,observed,frame,offset,whole,pacing,period,update,render,flips,gas,x,y,z,speed,yaw,dt,damage,eyeX,eyeY,eyeZ,lookX,lookY,lookZ,driver,vehicles,bags,outside,direct,replay,partial,packages,expandVerts,copyBytes,snapshotBytes,bakeMiss,churn,probeCapture,paint,rebuild,missNew,missBBox,missContent,missPrim,missStreams,missProgram,missDepth,missSize,expired,evicted,reserveFailed,cachePeakBytes,cachePeakEntries,captureBlocked,fusedSubmit,partialReplay,partialBytes,partialBuild,expandCachedVerts,expandBuilds,envRecorded,envFallback,barrierOmitted,envEligible,envDrawn,envSkipped,paintBlocked,paintVu,captureFrozen,coreTicks,coreWaitTicks,partialTicks,partialWaitTicks,coreCalls,partialCalls,waitCalls,scopeErrors,createTicks,classifyTicks,fillTicks,packetTicks,submitTicks,payloadTicks,createCalls,classifyCalls,fillCalls,packetCalls,submitCalls,payloadCalls\n");
  printf("LOG: DRIVEINIT rowsBytes=%u profile=0 hardwareTrace=0\n",unsigned(sizeof(rows)));
}
inline void begin(U pacing) {
  current.driver=-1;
  captureFrozen=false;
  sample=phase<phaseCount && offset>=sampleStart && !finishPending;
  enabled=sample && observing();
  clocks=false;
#ifdef MOTION_SCOPE_TRIAL
  clocks=sample && (phase==1 || phase==3 || phase==5);
#endif
  memset(timing,0,sizeof(timing));
  if(sample){memset(counters,0,sizeof(counters));paceStart=pacing;flipStart=flips;renderStamp=0;start=ticks();}
  else if(phase<phaseCount && offset==sampleStart-1)previous=ticks();
}
inline void renderBegin() { if(sample)renderStamp=ticks(); }
inline void pose(const Pose& p) { current=p; }
inline void end(U pacing) {
  if(sample) {
    const U end=ticks(); auto& r=rows[size++];
    r={frame,offset,end-start,pacing-paceStart,start-previous,renderStamp-start,end-renderStamp,flips-flipStart,U(gas()),{},current,U(captureFrozen),{}};
    memcpy(r.c,counters,sizeof(counters));previous=start;
    if(coreDepth || partialDepth || waitDepth || currentBucket)++timing[ScopeErrors];
    memcpy(r.t,timing,sizeof(timing));
    if(timing[ScopeErrors])valid=false;
    if(!renderStamp || r.pacing>r.whole || current.driver<0 || current.vehicles<3)valid=false;
    if(current.z>=108.0F || size==1200) { finishPending=true; if(current.z<108.0F)valid=false; }
  } else if(finishPending) {
    if(output) {
      for(U i=0;i<size;++i){const auto& r=rows[i];const auto& p=r.pose;
        fprintf(output,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",phase,lifetime(),unsigned(observing()),r.frame,r.offset,r.whole,r.pacing,r.period,r.update,r.render,r.flips,r.gas);
        fprintf(output,",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d",p.x,p.y,p.z,p.speed,p.yaw,p.dt,p.damage,p.eyeX,p.eyeY,p.eyeZ,p.lookX,p.lookY,p.lookZ,p.driver,p.vehicles);
        for(U v:r.c) { fprintf(output,",%u",v); }
        fprintf(output,",%u",r.frozen);
        for(U v:r.t) { fprintf(output,",%u",v); }
        fprintf(output,"\n");
      }
      if(fflush(output)!=0 || ferror(output))valid=false;
    }
    printf("TARGETCOUNTS phase=%u tested=%u rejected=%u\n",phase,TargetAudit::tested,TargetAudit::rejected);
    TargetAudit::tested=TargetAudit::rejected=0;
    printf("LOG: DRIVEPHASE phase=%u ttl=%u observed=%u valid=%u rows=%u z=%f speed=%f variant=%u\n",phase,lifetime(),unsigned(observing()),unsigned(valid),size,current.z,current.speed,variant());fflush(stdout);
    ++phase;offset=~0U;size=0;finishPending=false;resetRequested=phase<phaseCount;
    if(phase==phaseCount){if(output){if(fclose(output)!=0)valid=false;output=nullptr;}printf("LOG: DRIVEDONE valid=%u\n",unsigned(valid));fflush(stdout);}
  }
  enabled=false;clocks=false;++frame;++offset;
}
}

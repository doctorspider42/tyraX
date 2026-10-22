#define MOTION_FREEZE_TRIAL 1
#pragma once
#include <stdio.h>
#include <string.h>
namespace MotionProbe {
using U=unsigned int;
static_assert(sizeof(U)==4,"32-bit EE counters required");
enum Counter { Bags, Outside, Direct, Replay, Partial, Packages, ExpandVerts,
  CopyBytes, SnapshotBytes, BakeMiss, Churn, ProbeCapture, Paint, Rebuild,
  MissNew, MissBBox, MissContent, MissPrim, MissStreams, MissProgram,
  MissDepth, MissSize, Expired, Evicted, ReserveFailed, CachePeakBytes,
  CachePeakEntries, CaptureBlocked, FusedSubmit, PartialReplay, PartialBytes, PartialBuild,
  ExpandCachedVerts, ExpandBuilds, EnvRecorded, EnvFallback, BarrierOmitted, CullChunks, CullVerts, ClipChunks, ClipVerts, EnvEligible, EnvDrawn, EnvSkipped, PaintBlocked, PaintVu, Count };
struct Pose { float x,y,z,speed,yaw,dt,damage,eyeX,eyeY,eyeZ,lookX,lookY,lookZ; int driver,vehicles; };
enum Timing { CoreTicks, CoreWaitTicks, PartialTicks, PartialWaitTicks,
  CoreCalls, PartialCalls, WaitCalls, ScopeErrors,
  CreateTicks, ClassifyTicks, FillTicks, PacketTicks, SubmitTicks, PayloadTicks,
  CreateCalls, ClassifyCalls, FillCalls, PacketCalls, SubmitCalls, PayloadCalls,
  PrefixHead, PrefixBounds, PrefixTexture, PrefixLight, PrefixObject, TimingCount };
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
inline constexpr U phaseCount=3;
#endif
inline U variant() {
  constexpr U modes[]={0,6,0};
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
inline bool freezeCapture() {
#ifdef MOTION_FREEZE_TRIAL
  return (variant()==1 || variant()==2) && gas();
#else
  return false;
#endif
}
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
struct Prefix {
  bool active; Timing kind=PrefixHead; U stamp=0;
  explicit Prefix(bool selected):active(clocks && selected) { if(active)stamp=ticks(); }
  void next(Timing k) { if(active){const U now=ticks();timing[kind]+=now-stamp;stamp=now;kind=k;} }
  void finish(){if(active){timing[kind]+=ticks()-stamp;active=false;}}
  ~Prefix(){finish();}
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
  explicit Bucket(Timing k):active(clocks && coreDepth),kind(k),stamp(0),children(0),parent(nullptr) {
    if(!active)return;
    parent=currentBucket;currentBucket=this;
    ++timing[CreateCalls+(kind-CreateTicks)];stamp=ticks();
  }
  ~Bucket(){if(active){const U elapsed=ticks()-stamp;
    if(currentBucket!=this || children>elapsed || !coreDepth)++timing[ScopeErrors];
    timing[kind]+=elapsed-children;if(parent)parent->children+=elapsed;currentBucket=parent;
  }}
};
// Injected inside MotionProbe, after its clock and timing definitions.
namespace RoadTrace {
enum Metric { VifWait, GsWait, OtherWait, WaitCalls, SubmitCalls, SubmitQwords,
  FlushCalls, FlushQwords, CapacityFlush, FallbackFlush, DrainCalls,
  BankWaitCalls, SequenceWaitCalls, BarrierFlush, ArenaRetry, Errors, MetricCount };
inline const char* metricNames="vifWait,gsWait,otherWait,waitCalls,submitCalls,submitQwords,flushCalls,flushQwords,capacityFlush,fallbackFlush,drainCalls,bankWaitCalls,sequenceWaitCalls,barrierFlush,arenaRetry,errors";
inline U metrics[MetricCount]{}, frameMetrics[1200][MetricCount]{}, view=0, count=0, depth=0;
struct Record { U offset,id,view,vertices,elapsed,wait,c[Count]; };
inline Record records[4096]{};
inline Record* currentRecord=nullptr;
inline FILE *framesFile=nullptr,*bagsFile=nullptr;
inline bool window() {
  if(offset<gasStart)return false;
  const U t=(offset-gasStart)%240;
  return (t>=12 && t<20) || (t>=24 && t<32) || (t>=180 && t<188);
}
inline void note(Counter c,U n) { if(currentRecord)currentRecord->c[c]+=n; }
inline void metric(Metric m,U n=1) { if(clocks)metrics[m]+=n; }
struct Camera { U old; explicit Camera(U v):old(view){view=v;} ~Camera(){view=old;} };
struct Bag {
  Record* row=nullptr;U start=0;
  Bag(bool selected,U id,U vertices) {
    if(!clocks || !selected || !window())return;
    if(currentRecord || !id || count==4096){metric(Errors);valid=false;return;}
    row=&records[count++];*row={offset,id,view,vertices,0,0,{}};
    currentRecord=row;start=ticks();
  }
  ~Bag(){if(row){row->elapsed=ticks()-start;currentRecord=nullptr;}}
};
// Leaf waits only. No vsync/presentation pacing is included here.
struct Wait {
  bool active;U start=0;Metric kind;
  explicit Wait(Metric k):active(clocks),kind(k){
    if(active){if(depth++)metric(Errors);metric(WaitCalls);start=ticks();}
  }
  ~Wait(){if(active){U dt=ticks()-start;metrics[kind]+=dt;if(currentRecord)currentRecord->wait+=dt;--depth;}}
};
inline void open() {
  framesFile=fopen("host:road-frames.csv","w");bagsFile=fopen("host:road-bags.csv","w");
  if(!framesFile || !bagsFile){valid=false;return;}
  fprintf(framesFile,"phase,offset,%s\n",metricNames);
  fprintf(bagsFile,"phase,offset,id,view,vertices,elapsed,wait");
  for(U i=0;i<Count;++i)fprintf(bagsFile,",c%u",i);
  fprintf(bagsFile,"\n");
}
inline void begin(){memset(metrics,0,sizeof(metrics));}
inline void save(U index){
  if(depth || currentRecord || index>=1200){metric(Errors);valid=false;return;}
  memcpy(frameMetrics[index],metrics,sizeof(metrics));if(metrics[Errors])valid=false;
}
inline void dump(){
  if(framesFile && bagsFile){
    for(U i=0;i<size;++i){fprintf(framesFile,"%u,%u",phase,rows[i].offset);
      for(U v:frameMetrics[i])fprintf(framesFile,",%u",v);fprintf(framesFile,"\n");}
    for(U i=0;i<count;++i){const auto& r=records[i];
      fprintf(bagsFile,"%u,%u,%u,%u,%u,%u,%u",phase,r.offset,r.id,r.view,r.vertices,r.elapsed,r.wait);
      for(U v:r.c)fprintf(bagsFile,",%u",v);fprintf(bagsFile,"\n");}
    if(fflush(framesFile) || fflush(bagsFile) || ferror(framesFile) || ferror(bagsFile))valid=false;
  }else valid=false;
  count=0;
  if(phase+1==phaseCount){
    if(framesFile && fclose(framesFile))valid=false;
    if(bagsFile && fclose(bagsFile))valid=false;
    framesFile=bagsFile=nullptr;
  }
}
}

inline void add(Counter c,U n=1) {
  RoadTrace::note(c,n);
  if(sample && (c==ProbeCapture || c==Paint || c==CaptureBlocked || c>=EnvEligible)) counters[c]+=n;
}
inline bool skipBodyEnv(){return gas() && (variant()==2 || variant()==3);}

inline void occupancy(U qw,U entries) {
  if(!enabled)return;
  if(qw*16>counters[CachePeakBytes])counters[CachePeakBytes]=qw*16;
  if(entries>counters[CachePeakEntries])counters[CachePeakEntries]=entries;
}
inline void onPresent(U,U,U,U) { ++flips; }
inline void init() {
  RoadTrace::open();
  output=fopen("host:drive-probe.csv","w");valid=output!=nullptr;
  if(output)fprintf(output,"phase,lifetime,observed,frame,offset,whole,pacing,period,update,render,flips,gas,x,y,z,speed,yaw,dt,damage,eyeX,eyeY,eyeZ,lookX,lookY,lookZ,driver,vehicles,bags,outside,direct,replay,partial,packages,expandVerts,copyBytes,snapshotBytes,bakeMiss,churn,probeCapture,paint,rebuild,missNew,missBBox,missContent,missPrim,missStreams,missProgram,missDepth,missSize,expired,evicted,reserveFailed,cachePeakBytes,cachePeakEntries,captureBlocked,fusedSubmit,partialReplay,partialBytes,partialBuild,expandCachedVerts,expandBuilds,envRecorded,envFallback,barrierOmitted,cullChunks,cullVerts,clipChunks,clipVerts,envEligible,envDrawn,envSkipped,paintBlocked,paintVu,captureFrozen,coreTicks,coreWaitTicks,partialTicks,partialWaitTicks,coreCalls,partialCalls,waitCalls,scopeErrors,createTicks,classifyTicks,fillTicks,packetTicks,submitTicks,payloadTicks,createCalls,classifyCalls,fillCalls,packetCalls,submitCalls,payloadCalls,prefixHead,prefixBounds,prefixTexture,prefixLight,prefixObject\n");
  printf("LOG: DRIVEINIT rowsBytes=%u profile=0 hardwareTrace=0\n",unsigned(sizeof(rows)));
}
inline void begin(U pacing) {
  current.driver=-1;
  captureFrozen=false;
  sample=phase<phaseCount && offset>=sampleStart && !finishPending;
  enabled=sample && observing();
  clocks=sample && variant()==6;
#ifdef MOTION_SCOPE_TRIAL
  clocks=sample && (phase==1 || phase==3 || phase==5);
#endif
  RoadTrace::begin();
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
    RoadTrace::save(size-1);
    if(timing[ScopeErrors])valid=false;
    if(!renderStamp || r.pacing>r.whole || current.driver<0 || current.vehicles<3)valid=false;
    if(offset==gasStart+719) { finishPending=true; }
  } else if(finishPending) {
    RoadTrace::dump();
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
    printf("LOG: DRIVEPHASE phase=%u ttl=%u observed=%u valid=%u rows=%u z=%f speed=%f variant=%u\n",phase,lifetime(),unsigned(observing()),unsigned(valid),size,current.z,current.speed,variant());fflush(stdout);
    ++phase;offset=~0U;size=0;finishPending=false;resetRequested=phase<phaseCount;
    if(phase==phaseCount){if(output){if(fclose(output)!=0)valid=false;output=nullptr;}printf("LOG: DRIVEDONE valid=%u\n",unsigned(valid));fflush(stdout);}
  }
  enabled=false;clocks=false;++frame;++offset;
}
}

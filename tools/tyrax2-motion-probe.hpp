#pragma once
// Private diagnostic only: no synchronization and no output in sampled loops.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
namespace MotionProbe {
using U = unsigned int;
static_assert(sizeof(U)==4,"EE diagnostic requires 32-bit unsigned int");
enum Counter { Bags, Outside, Direct, Replay, Partial, Packages, ExpandVerts,
  CopyBytes, SnapshotBytes, BakeMiss, Churn, ProbeCapture, Paint, Rebuild,
  MissNew, MissBBox, MissContent, MissPrim, MissStreams, MissProgram,
  MissDepth, MissSize, Expired, Evicted, ReserveFailed, CachePeakBytes,
  CachePeakEntries, PartialReplay, PartialBytes, PartialBuild,
  ExpandCachedVerts, ExpandBuilds, Count };
struct Row { U frame, angle, whole, pacing, period, update, render, flips, c[Count]; };
inline Row rows[360]{};
inline U frame=0, phase=0, offset=0, counters[Count]{}, start=0, paceStart=0;
inline U previous=0, renderStamp=0, flips=0, flipStart=0, worstAngle=0, worstWork=0;
inline bool sample=false, enabled=false, valid=true;
inline FILE* output=nullptr;
#ifdef MOTION_GEOMETRY_SWEEP
// Correctness-only camera sweep: both candidates remain on in every phase.
inline U variant() { return 1; }
#endif
#ifdef MOTION_LIFETIME_TRIAL
inline constexpr U phaseLength=1800, sampleStart=1200;
inline U lifetime() { return phase==1 || phase==4 ? 1000 : 250; }
inline bool observed() { return phase<3; }
inline const char* filename="host:motion-lifetime.csv";
#else
inline constexpr U phaseLength=1200, sampleStart=300;
inline bool observed() { return phase!=3; }
inline const char* filename="host:motion-probe.csv";
#endif
inline U ticks() { U t; asm volatile("mfc0 %0, $9":"=r"(t)::"memory"); return t; }
inline void add(Counter c, U n=1) { if(enabled) counters[c]+=n; }
inline void occupancy(U qwords, U entries) {
  if(!enabled) return;
  if(qwords*16>counters[CachePeakBytes]) counters[CachePeakBytes]=qwords*16;
  if(entries>counters[CachePeakEntries]) counters[CachePeakEntries]=entries;
}
inline bool rotating() {
#ifdef MOTION_LIFETIME_TRIAL
  return phase<6;
#else
  return phase==1 || phase==3 || phase==4;
#endif
}
inline U angle() { return rotating() ? (offset+360-sampleStart%360)%360 : (phase>=2 ? worstAngle : 0); }
inline void onPresent(U, U, U, U) { ++flips; }
inline void init() {
  output=fopen(filename,"w"); valid=output!=nullptr;
  if(output) fprintf(output,"phase,frame,angle,whole,pacing,period,update,render,flips,bags,outside,direct,replay,partial,packages,expandVerts,copyBytes,snapshotBytes,bakeMiss,churn,probeCapture,paint,rebuild,missNew,missBBox,missContent,missPrim,missStreams,missProgram,missDepth,missSize,expired,evicted,reserveFailed,cachePeakBytes,cachePeakEntries,partialReplay,partialBytes,partialBuild,expandCachedVerts,expandBuilds\n");
  printf("LOG: MOTIONINIT rowsBytes=%u vehicles=0 profile=0 hardwareTrace=0\n",unsigned(sizeof(rows)));
}
inline void begin(U pacing) {
  phase=frame/phaseLength; offset=frame%phaseLength;
  sample=phase<6 && offset>=sampleStart && offset<sampleStart+360;
  enabled=sample && observed();
  if(sample) { memset(counters,0,sizeof(counters)); paceStart=pacing; flipStart=flips; renderStamp=0; start=ticks(); }
  else if(phase<6 && offset==sampleStart-1) previous=ticks();
}
inline void renderBegin() { if(sample) renderStamp=ticks(); }
inline void end(U pacing) {
  if(sample) {
    const U end=ticks(); auto& r=rows[offset-sampleStart];
    r={frame,angle(),end-start,pacing-paceStart,start-previous,renderStamp-start,end-renderStamp,flips-flipStart,{}};
    memcpy(r.c,counters,sizeof(counters)); previous=start;
    if(!renderStamp || r.pacing>r.whole || r.whole>0x7fffffffu) valid=false;
    if(phase==1 && r.whole-r.pacing>worstWork) { worstWork=r.whole-r.pacing; worstAngle=r.angle; }
  }
  enabled=false;
  if(phase<6 && offset==sampleStart+400) {
    if(output) {
      for(const auto& r:rows) {
        fprintf(output,"%u,%u,%u,%u,%u,%u,%u,%u,%u",phase,r.frame,r.angle,r.whole,r.pacing,r.period,r.update,r.render,r.flips);
        for(U v:r.c) fprintf(output,",%u",v);
        fputc('\n',output);
      }
      if(fflush(output)!=0 || ferror(output)) valid=false;
    }
    printf("LOG: MOTIONPHASE phase=%u observed=%u valid=%u worstAngle=%u worstWork=%u\n",phase,unsigned(observed()),unsigned(valid),worstAngle,worstWork); fflush(stdout);
#ifdef MOTION_LIFETIME_TRIAL
    printf("LOG: MOTIONTTL phase=%u lifetime=%u\n",phase,lifetime()); fflush(stdout);
#endif
  }
  if(frame==6*phaseLength-1) {
    if(output) { if(fclose(output)!=0) valid=false; output=nullptr; }
    printf("LOG: MOTIONDONE valid=%u loops=%u\n",unsigned(valid),6*phaseLength); fflush(stdout);
  }
  ++frame;
}
}

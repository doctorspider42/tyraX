#pragma once
// Private attribution. No production synchronization or rendering policy changes.
#include <stdio.h>
#include "debug/strip_subranges.hpp"
#include <string.h>
namespace MotionProbe {
using U=unsigned;using W=unsigned long long;
enum Producer{Other,Terrain,Roads,Batches,Objects,Vehicles,Effects,Sky,Procedural,ProducerCount};
enum View{Main,Reflection,Secondary,Unassigned,ViewCount};
enum Counter{Bags,Outside,Direct,Replay,Partial,Packages,ExpandVerts,CopyBytes,SnapshotBytes,BakeMiss,Churn,ProbeCapture,Paint,Rebuild,
 MissNew,MissBBox,MissContent,MissPrim,MissStreams,MissProgram,MissDepth,MissSize,Expired,Evicted,ReserveFailed,CachePeakBytes,CachePeakEntries,
 CaptureBlocked,FusedSubmit,PartialReplay,PartialBytes,PartialBuild,ExpandCachedVerts,ExpandBuilds,EnvRecorded,EnvFallback,BarrierOmitted,
 CullChunks,ClipChunks,CullVerts,ClipVerts,DeferredPush,DeferredPop,SubCalls,SubOutsideTris,SubCullTris,SubClipTris,SubBuild,SubHits,Count};
enum Metric{CoreTicks=Count,CoreCalls,WaitTicks,WaitCalls,MetricCount};
inline const char* names[]={"bags","outside","direct","replay","partial","packages","expandVerts","copyBytes","snapshotBytes","bakeMiss","churn","probeCapture","paint","rebuild","missNew","missBBox","missContent","missPrim","missStreams","missProgram","missDepth","missSize","expired","evicted","reserveFailed","cachePeakBytes","cachePeakEntries","captureBlocked","fusedSubmit","partialReplay","partialBytes","partialBuild","expandCachedVerts","expandBuilds","envRecorded","envFallback","barrierOmitted","cullChunks","clipChunks","cullVerts","clipVerts","deferredPush","deferredPop","subCalls","subOutsideTris","subCullTris","subClipTris","subBuild","subHits","coreTicks","coreCalls","waitTicks","waitCalls"};
struct Pose{float x,y,z,speed,yaw,dt,damage,eyeX,eyeY,eyeZ,lookX,lookY,lookZ;int driver,vehicles;};
struct Row{U frame,offset,whole,pacing,period,update,render,flips,segment,angle,errors;Pose pose;U c[MetricCount];};
inline Row rows[1200]{};
inline U frame=0,offset=0,phase=0,size=0,producer=Other,view=Main,coreDepth=0,waitDepth=0,scopeErrors=0;
inline U start=0,previous=0,renderStamp=0,paceStart=0,flips=0,flipStart=0;
inline U values[ProducerCount*ViewCount][MetricCount]{};
inline W sums[14][ProducerCount*ViewCount][MetricCount]{};
inline U binRows[14]{},active[ProducerCount*ViewCount]{};
inline bool sample=false,enabled=false,clocks=false,valid=true,finishPending=false,resetRequested=false;
inline Pose current{};
inline FILE* output=nullptr;inline FILE* aggregate=nullptr;
inline constexpr U phaseCount=12;
inline bool driving(){return phase>=6;}
inline U mode(){return phase%6;}
inline U variant(){return 1;}
inline U lifetime(){return 250;}
inline bool candidate(){return mode()==1||mode()==3||mode()==5;}
inline U sampleStart(){return driving()||phase==0?1200:360;}
inline U angle(){if(driving()||offset<sampleStart()+120||offset>=sampleStart()+480)return 0;return offset-sampleStart()-120;}
inline bool gas(){return phase<phaseCount&&driving()&&offset>=sampleStart()+120&&!finishPending;}
inline U segment(){if(offset<sampleStart()+120)return 0;if(!driving())return offset<sampleStart()+480?1:2;return 1;}
inline U bin(){if(!driving())return segment()==0?0:segment()==2?13:1+angle()/30;int n=int((current.z+80.0F)/20.0F);return segment()==0?0:1+unsigned(n<0?0:n>11?11:n);}
inline U ticks(){U t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
inline U tag(){return view*ProducerCount+producer;}
struct Owner{U old;explicit Owner(U p):old(producer){producer=p;}~Owner(){producer=old;}};
struct Camera{U old;explicit Camera(U v):old(view){view=v;}~Camera(){view=old;}};
struct Restore{U oldP,oldV;explicit Restore(U t):oldP(producer),oldV(view){producer=t%ProducerCount;view=t/ProducerCount;}~Restore(){producer=oldP;view=oldV;}};
inline bool selected(){switch(mode()){case 3:return producer==Terrain;case 4:return producer==Roads;case 5:return producer==Objects;case 6:return producer==Vehicles;case 7:return producer==Batches;case 8:return producer==Other||producer>=Effects;default:return false;}}
inline void add(Counter c,U n=1){if(enabled){const U t=c==SnapshotBytes?Unassigned*ProducerCount:tag();values[t][c]+=n;active[t]=1;}}
inline void occupancy(U,U){}
struct CoreScope{bool on;U at,stamp;
 CoreScope():on(clocks&&selected()),at(tag()),stamp(0){if(on){if(coreDepth++)++scopeErrors;active[at]=1;++values[at][CoreCalls];stamp=ticks();}}
 ~CoreScope(){if(on){values[at][CoreTicks]+=ticks()-stamp;--coreDepth;}}
};
struct Wait{bool on,outer;U at,stamp;Wait():on(clocks&&coreDepth),outer(false),at(tag()),stamp(0){if(on){outer=waitDepth++==0;if(outer){++values[at][WaitCalls];stamp=ticks();}}}~Wait(){if(on){if(outer)values[at][WaitTicks]+=ticks()-stamp;--waitDepth;}}};
inline void onPresent(U,U,U,U){++flips;}
inline void pose(const Pose&p){current=p;}
inline void renderBegin(){if(sample)renderStamp=ticks();}
inline void init(){
 output=fopen("host:producer-probe.csv","w");aggregate=fopen("host:producer-bins.csv","w");valid=output&&aggregate;
 if(output){fprintf(output,"phase,workload,mode,frame,offset,whole,pacing,period,update,render,flips,segment,angle,errors,x,y,z,speed,yaw,dt,damage,eyeX,eyeY,eyeZ,lookX,lookY,lookZ,driver,vehicles");for(auto n:names)fprintf(output,",%s",n);fputc('\n',output);}
 if(aggregate){fprintf(aggregate,"phase,bin,producer,view,rows");for(auto n:names)fprintf(aggregate,",%s",n);fputc('\n',aggregate);}
 printf("PRODUCERINIT rowsBytes=%u aggregateBytes=%u profile=0 hardwareTrace=0\n",unsigned(sizeof(rows)),unsigned(sizeof(sums)));fflush(stdout);
}
inline void begin(U pacing){
 sample=phase<phaseCount&&offset>=sampleStart()&&!finishPending;
 enabled=sample&&mode()<3;clocks=false;
 producer=Other;view=Main;scopeErrors=0;current.driver=-1;
 if(sample){memset(values,0,sizeof(values));memset(active,0,sizeof(active));paceStart=pacing;flipStart=flips;renderStamp=0;start=ticks();}
 else if(offset==sampleStart()-1)previous=ticks();
}
inline void end(U pacing){
 if(sample){auto&r=rows[size++];U totals[MetricCount]{};const U b=bin();++binRows[b];
  for(U t=0;t<ProducerCount*ViewCount;++t)if(active[t])for(U c=0;c<MetricCount;++c){sums[b][t][c]+=values[t][c];totals[c]+=values[t][c];}
  if(coreDepth||waitDepth||producer!=Other||view!=Main)++scopeErrors;
  const U end=ticks();r={frame,offset,end-start,pacing-paceStart,start-previous,renderStamp-start,end-renderStamp,flips-flipStart,segment(),angle(),scopeErrors,current,{}};
  memcpy(r.c,totals,sizeof(totals));previous=start;
  if(!renderStamp||r.pacing>r.whole||scopeErrors||(driving()?(current.driver<0||current.vehicles!=3):(current.vehicles!=0)))valid=false;
  if(driving()?current.z>=108.0F:offset==sampleStart()+599)finishPending=true;
  if(size==1200){if(!finishPending)valid=false;finishPending=true;}
 }else if(finishPending){
  if(output){for(U i=0;i<size;++i){auto&r=rows[i];auto&p=r.pose;
   fprintf(output,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",phase,U(driving()),mode(),r.frame,r.offset,r.whole,r.pacing,r.period,r.update,r.render,r.flips,r.segment,r.angle,r.errors);
   fprintf(output,",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d",p.x,p.y,p.z,p.speed,p.yaw,p.dt,p.damage,p.eyeX,p.eyeY,p.eyeZ,p.lookX,p.lookY,p.lookZ,p.driver,p.vehicles);
   for(U n:r.c)fprintf(output,",%u",n);fputc('\n',output);
  }if(fflush(output)||ferror(output))valid=false;}
  if(aggregate){for(U b=0;b<14;++b)if(binRows[b])for(U t=0;t<ProducerCount*ViewCount;++t){fprintf(aggregate,"%u,%u,%u,%u,%u",phase,b,t%ProducerCount,t/ProducerCount,binRows[b]);for(W n:sums[b][t])fprintf(aggregate,",%llu",n);fputc('\n',aggregate);}if(fflush(aggregate)||ferror(aggregate))valid=false;}
  printf("PRODUCERPHASE phase=%u workload=%u mode=%u valid=%u rows=%u z=%f vehicles=%d\n",phase,U(driving()),mode(),unsigned(valid),size,current.z,current.vehicles);fflush(stdout);
  printf("SUBPHASE phase=%u candidate=%u checks=%u failed=%u bytes=%u\n",phase,unsigned(candidate()),StripSubranges::checks,StripSubranges::failures,unsigned(sizeof(StripSubranges::entries)));fflush(stdout);
  if(StripSubranges::failures)valid=false;
  ++phase;offset=~0U;size=0;finishPending=false;resetRequested=phase<phaseCount;
  memset(sums,0,sizeof(sums));memset(binRows,0,sizeof(binRows));
  if(phase==phaseCount){if(output){if(fclose(output))valid=false;output=nullptr;}if(aggregate){if(fclose(aggregate))valid=false;aggregate=nullptr;}printf("PRODUCERDONE valid=%u\n",unsigned(valid));fflush(stdout);}
 }
 enabled=false;clocks=false;++frame;++offset;
}
}

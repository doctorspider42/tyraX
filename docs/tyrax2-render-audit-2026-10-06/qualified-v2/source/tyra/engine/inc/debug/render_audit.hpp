#pragma once
// PRIVATE two-axis elapsed observer. Adds clocks/counters, never fences or waits.
#include <stdio.h>
namespace RenderAudit {
using U=unsigned;
enum StageId:U {Other=0,Setup,DayNight,ScenePrep,Reflection,Portal,Sky,Terrain,Occlusion,StaticBatches,Roads,Procedural,Objects,WheelsAnim,Shadows,LightPools,Particles,HudFx,EndFrame,Highlight,StageCount};
enum WorkId:U {RenderOther=0,CorePrep,Bounds,Packages,GeometryCopy,Clip,Texture,ObjectState,Packet,Submit,Cache,WorkCount};
enum Counter:U {Bags=0,BagVertices,PackagesSeen,ClassifiedVertices,CopyCalls,EeClipInputs,TextureUploads,TextureChainQw,SubmitSourceQw,CounterCount};
struct Row {U frame=0,enabled=0,ok=1,reads=0,edges=0;U stages[StageCount]{},work[WorkCount]{},wait=0,pacing=0,counters[CounterCount]{};};
inline Row samples[128]{},current{};
static_assert(sizeof(samples)==24064,"private render audit row size changed");
inline U stageCalls[3][StageCount]{},workCalls[3][WorkCount]{};
inline U stage=Other,work=RenderOther,stageDepth=0,workDepth=0,last=0,excludeBegin=0,total=0;
inline bool active=false,paused=false,valid=true;
inline U ticks(){++current.reads;U t;asm volatile("mfc0 %0, $9":"=r"(t)::"memory");return t;}
inline void advance(U t){
 U d=t-last;if(d>=0x80000000u){current.ok=0;valid=false;return;}
 current.stages[stage]+=d;current.work[work]+=d;last=t;
}
inline void begin(U frame,U existingStamp){
 if(active){valid=false;return;}
 current=Row{};current.frame=frame;current.enabled=1;
 stage=Other;work=RenderOther;stageDepth=workDepth=0;last=existingStamp;total=0;paused=false;active=true;
}
inline void excludeStart(U t){if(!active)return;if(paused){current.ok=0;valid=false;return;}advance(t);excludeBegin=t;paused=true;}
inline void excludeEnd(U t,bool pacing){
 if(!active)return;
 if(!paused){current.ok=0;valid=false;return;}
 U d=t-excludeBegin;if(d>=0x80000000u)current.ok=0;
 if(pacing)current.pacing+=d;else current.wait+=d;
 last=t;paused=false;
}
inline void end(U t){
 if(!active){valid=false;return;}
 if(paused||stageDepth||workDepth)current.ok=0;
 advance(t);active=false;
 for(U j=0;j<StageCount;++j)total+=current.stages[j];
}
inline void finish(U span,U wait,U pacing){
 U w=0;for(U j=0;j<WorkCount;++j)w+=current.work[j];
 if(active||!current.enabled||!current.ok||current.reads!=2*current.edges||current.wait!=wait||current.pacing!=pacing||total!=w||total+wait+pacing!=span){current.ok=0;valid=false;}
 samples[current.frame%1800-900]=current;
}
inline void storeOff(U frame){Row off{};off.frame=frame;samples[frame%1800-900]=off;}
inline void rejectCapture(U seq){if(active&&seq){current.ok=0;valid=false;}}
inline void count(Counter c,U n=1){if(active&&!paused)current.counters[c]+=n;}
class Stage {
 bool on=false,timed=false;U previous=Other,id=Other,depth=0;
 public:
 explicit Stage(StageId tag):on(active&&!paused),id(tag){
  if(!on)return;
  previous=stage;depth=++stageDepth;++stageCalls[current.frame/1800][id];timed=previous!=id;
  if(timed){++current.edges;advance(ticks());stage=id;}
 }
 void finish(){
  if(!on)return;
  if(!active||paused||stageDepth!=depth||stage!=id){current.ok=0;valid=false;on=false;return;}
  if(timed){advance(ticks());stage=previous;}
  --stageDepth;on=false;
 }
 ~Stage(){finish();}
 Stage(const Stage&)=delete;Stage& operator=(const Stage&)=delete;
};
class Work {
 bool on=false,timed=false;U previous=RenderOther,id=RenderOther,depth=0;
 public:
 explicit Work(WorkId tag):on(active&&!paused),id(tag){
  if(!on)return;
  previous=work;depth=++workDepth;++workCalls[current.frame/1800][id];timed=previous!=id;
  if(timed){++current.edges;advance(ticks());work=id;}
 }
 void finish(){
  if(!on)return;
  if(!active||paused||workDepth!=depth||work!=id){current.ok=0;valid=false;on=false;return;}
  if(timed){advance(ticks());work=previous;}
  --workDepth;on=false;
 }
 ~Work(){finish();}
 Work(const Work&)=delete;Work& operator=(const Work&)=delete;
};
inline void write(FILE* file,U p,U i){
 const Row&r=samples[i];fprintf(file,"LOG: CLOSEOUTRENDER phase=%u i=%u frame=%u enabled=%u ok=%u reads=%u edges=%u wait=%u pacing=%u",p,i,r.frame,r.enabled,r.ok,r.reads,r.edges,r.wait,r.pacing);
 for(U j=0;j<StageCount;++j)fprintf(file," s%u=%u",j,r.stages[j]);
 for(U j=0;j<WorkCount;++j)fprintf(file," w%u=%u",j,r.work[j]);
 for(U j=0;j<CounterCount;++j)fprintf(file," n%u=%u",j,r.counters[j]);
 fprintf(file,"\n");
 if(i==127){fprintf(file,"LOG: CLOSEOUTRENDERCALLS phase=%u",p);for(U j=0;j<StageCount;++j)fprintf(file," s%u=%u",j,stageCalls[p][j]);for(U j=0;j<WorkCount;++j)fprintf(file," w%u=%u",j,workCalls[p][j]);fprintf(file,"\n");}
}
}

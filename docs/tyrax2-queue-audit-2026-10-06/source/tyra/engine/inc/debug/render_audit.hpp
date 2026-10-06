#pragma once
// PRIVATE selective queue observer. Dense v2 tags compile to no-ops.
#include <stdio.h>
namespace RenderAudit {
using U=unsigned;
enum StageId:U {Other=0,Setup,DayNight,ScenePrep,Reflection,Portal,Sky,Terrain,Occlusion,StaticBatches,Roads,Procedural,Objects,WheelsAnim,Shadows,LightPools,Particles,HudFx,EndFrame,Highlight,StageCount};
enum WorkId:U {RenderOther=0,CorePrep,Bounds,Packages,GeometryCopy,Clip,Texture,ObjectState,Packet,Submit,Cache,WorkCount};
enum Counter:U {Bags=0,BagVertices,PackagesSeen,ClassifiedVertices,CopyCalls,EeClipInputs,TextureUploads,TextureChainQw,SubmitSourceQw,CounterCount};
class Stage {public: explicit Stage(StageId){} void finish(){} };
class Work {public: explicit Work(WorkId){} void finish(){} };
inline void count(Counter,U=1){}
enum QueueId:U {Unobserved=0,ProducerSend,SubmitRemainder,CloseChain,Snapshot,Emission,Sizing,Seal,Enqueue,Writeback,QueueCount};
enum Event:U {Sends=0,Submits,SourceQw,Snapshots,CopiedBytes,BorrowedBytes,SnapshotFailures,SnapshotRetries,CountedSnapshots,EmissionCalls,EmissionRecords,RecordedReturns,Enqueues,Flushes,Closers,Preparations,SizingLoops,GifCalls,EventCount};
struct Row {U frame=0,enabled=0,ok=1,reads=0,edges=0;U queue[QueueCount]{},wait=0,pacing=0,events[EventCount]{};};
inline Row samples[128]{},current{};
static_assert(sizeof(Row)==140,"queue audit row changed");
inline U calls[3][QueueCount]{};
inline U tag=Unobserved,depth=0,last=0,excludeBegin=0,total=0;
inline bool active=false,paused=false,valid=true;
inline U ticks(){++current.reads;U t;asm volatile("mfc0 %0, $9":"=r"(t)::"memory");return t;}
inline void advance(U t){U d=t-last;if(d>=0x80000000u){current.ok=0;valid=false;return;}current.queue[tag]+=d;last=t;}
inline void begin(U frame,U stamp){if(active){valid=false;return;}current=Row{};current.frame=frame;current.enabled=1;tag=Unobserved;depth=0;last=stamp;total=0;paused=false;active=true;}
inline void excludeStart(U t){if(!active)return;if(paused){current.ok=0;valid=false;return;}advance(t);excludeBegin=t;paused=true;}
inline void excludeEnd(U t,bool pace){if(!active)return;if(!paused){current.ok=0;valid=false;return;}U d=t-excludeBegin;if(d>=0x80000000u)current.ok=0;if(pace)current.pacing+=d;else current.wait+=d;last=t;paused=false;}
inline void end(U t){if(!active){valid=false;return;}if(paused||depth)current.ok=0;advance(t);active=false;for(U j=0;j<QueueCount;++j)total+=current.queue[j];}
inline void finish(U span,U wait,U pace){if(active||!current.enabled||!current.ok||current.reads!=2*current.edges||current.wait!=wait||current.pacing!=pace||total+wait+pace!=span){current.ok=0;valid=false;}samples[current.frame%1800-900]=current;}
inline void storeOff(U frame){Row off{};off.frame=frame;samples[frame%1800-900]=off;}
inline void rejectCapture(U seq){if(active&&seq){current.ok=0;valid=false;}}
inline void event(Event e,U n=1){if(active&&!paused)current.events[e]+=n;}
class QueueScope {
 bool on=false,timed=false;U previous=Unobserved,id=Unobserved,level=0;
 public:
 explicit QueueScope(QueueId q):on(active&&!paused),id(q){if(!on)return;previous=tag;level=++depth;++calls[current.frame/1800][id];timed=previous!=id;if(timed){++current.edges;advance(ticks());tag=id;}}
 void finish(){if(!on)return;if(!active||paused||depth!=level||tag!=id){current.ok=0;valid=false;on=false;return;}if(timed){advance(ticks());tag=previous;}--depth;on=false;}
 ~QueueScope(){finish();}
 QueueScope(const QueueScope&)=delete;QueueScope&operator=(const QueueScope&)=delete;
};
inline void write(FILE*f,U p,U i){const Row&r=samples[i];fprintf(f,"LOG: CLOSEOUTQUEUE phase=%u i=%u frame=%u enabled=%u ok=%u reads=%u edges=%u wait=%u pacing=%u",p,i,r.frame,r.enabled,r.ok,r.reads,r.edges,r.wait,r.pacing);for(U j=0;j<QueueCount;++j)fprintf(f," q%u=%u",j,r.queue[j]);for(U j=0;j<EventCount;++j)fprintf(f," n%u=%u",j,r.events[j]);fprintf(f,"\n");if(i==127){fprintf(f,"LOG: CLOSEOUTQUEUECALLS phase=%u",p);for(U j=0;j<QueueCount;++j)fprintf(f," q%u=%u",j,calls[p][j]);fprintf(f,"\n");}}
}

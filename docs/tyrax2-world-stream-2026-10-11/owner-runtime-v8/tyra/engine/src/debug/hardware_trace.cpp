// Modified by TyraX: bounded hierarchy; legacy CSV plus schema-2 sidecar.
#include "debug/hardware_trace.hpp"
#if TYRA_HARDWARE_TRACE
#include "file/file_utils.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
namespace Tyra { namespace HardwareTrace {
bool active=false, detailed=false;
struct Event { const char* label; u32 start,duration,frame,value,id,parent;
  Kind kind; Context owner; u32 epoch; };
static Event* events=nullptr;
static u32 capacity=0,count=0,dropped=0,invalid=0,frame=0,first=120,frames=4;
static u32 nextId=0,top=0,root=0,nextJob=0,lastTick=0,elapsed=0,epoch=0;
static bool enabled=false,states=true,retain=false,haveClock=false;
static Context owner;
static u32 generation=0;
static u32 stack[64],depth=0;
u32 ticks(){u32 t;asm volatile("mfc0 %0, $9":"=r"(t)::"memory");return t;}
static u32 sample(){const u32 t=ticks();if(!haveClock){lastTick=t;haveClock=true;}
  const u32 delta=t-lastTick;lastTick=t;const u32 old=elapsed;elapsed+=delta;
  if(elapsed<old)++epoch;
  return elapsed;}
static u32 open(const char* label,Kind kind,u32 value){
  if(!active)return 0;
  const u32 start=sample();
  if(count==capacity||depth==64){++dropped;return 0;}
  const u32 index=count++;const u32 id=++nextId;
  events[index]={label,start,0,frame,value,id,top,kind,owner,epoch};
  stack[depth++]=index;top=id;return index+1;
}
u32 beginScope(const char* label,Kind kind,bool detail){
  return active&&(!detail||detailed)?open(label,kind,0):0;}
void endScope(u32 token){if(!token)return;
  if(!active||!depth||stack[depth-1]!=token-1){++invalid;return;}
  Event& e=events[token-1];const u32 end=sample();e.duration=end-e.start;
  // Scope duration must be below half a COP0 wrap; reject ambiguous clocks.
  if(e.duration>=0x80000000u)++invalid;
  --depth;top=e.parent;
}
Context context(){return active?owner:Context{};}
void setContext(const Context& value){if(active)owner=value;}
u32 beginJob(u32 target,bool requested,bool effective){if(!active)return 0;
  owner={++nextJob,0,0,target,u32(requested),u32(effective)};return nextJob;}
void record(const char* label,u32 start,u32 end,u32 value){
  if(!active||!detailed)return;
  // Compatibility callers pass raw COP0 stamps; observe current time once,
  // then reconstruct their start relative to it without assuming call order.
  const u32 now=sample(),rawNow=lastTick;
  if(count==capacity){++dropped;return;}
  const u32 offset=rawNow-start;const u32 relative=now-offset;
  const u32 startEpoch=epoch-(relative>now?1u:0u);
  const u32 duration=end-start;if(duration>=0x80000000u)++invalid;
  events[count++]={label,relative,duration,frame,value,++nextId,duration?~u32(0):top,
    duration?Kind::Legacy:Kind::Marker,owner,startEpoch};
}
void state(const char* label){if(!active||!states)return;
  const u32 token=open(label,Kind::Snapshot,*(volatile u32*)0x10003c00);
  if(token){endScope(token);events[token-1].duration=0;}
  const u32 gif=open("GIF_STATE",Kind::Snapshot,*(volatile u32*)0x10003020);
  if(gif){endScope(gif);events[gif-1].duration=0;}
}
bool reserveCapture(u32 cap){if(active||cap<128||cap>32768)return false;
  if(capacity>=cap&&events){retain=true;return true;}
  Event* fresh=static_cast<Event*>(malloc(sizeof(Event)*cap));
  if(!fresh){printf("HWTRACE allocation_failed capacity=%u\n",unsigned(cap));return false;}
  free(events);events=fresh;capacity=cap;retain=true;return true;
}
bool configureCapture(u32 start,u32 length,bool stateFlag,bool detail,u32 cap){
  if(active||start<frame||start>1000000||length<1||length>32)return false;
  const bool keep=retain;if(!reserveCapture(cap))return false;retain=keep;
  first=start;frames=length;states=stateFlag;detailed=detail;enabled=true;
  count=dropped=invalid=nextId=top=root=depth=nextJob=elapsed=epoch=0;
  owner={};haveClock=false;++generation;return true;
}
void disable(){if(active){++invalid;return;}enabled=false;}
void release(){if(active)return;enabled=false;free(events);events=nullptr;capacity=0;retain=false;}
u32 loopIndex(){return frame;}
u32 captureEpoch(){return generation;}
void configure(){FILE* f=fopen(FileUtils::fromCwd("hardware-trace.cfg").c_str(),"r");
  if(!f)return;
  unsigned start=120,length=4,stateFlag=1,detail=0,cap=8192;
  const int n=fscanf(f,"%u %u %u %u %u",&start,&length,&stateFlag,&detail,&cap);
  bool clean=true;int tail;while((tail=fgetc(f))!=EOF)
    if(!std::isspace(static_cast<unsigned char>(tail)))clean=false;
  fclose(f);
  if(n<3||!clean||stateFlag>1||detail>1||!configureCapture(start,length,stateFlag,detail,cap)){
    printf("HWTRACE configuration_rejected\n");return;}
  remove(FileUtils::fromCwd("hardware-trace.csv").c_str());
  remove(FileUtils::fromCwd("hardware-trace-v2.csv").c_str());
  printf("HWTRACE armed first=%u frames=%u detail=%u capacity=%u bytes=%u\n",
    unsigned(first),unsigned(frames),unsigned(detailed),unsigned(capacity),unsigned(sizeof(Event)*capacity));
}
void beginFrame(){if(!enabled)return;
  active=frame>=first&&frame-first<frames;if(!active)return;
  owner={};root=open("Frame",Kind::Span,0);state("VIF1_FRAME_START");}
static const char* name(Kind kind){switch(kind){case Kind::Wait:return "wait";
 case Kind::Pacing:return "pacing";case Kind::Snapshot:return "snapshot";
 case Kind::Marker:return "marker";case Kind::Legacy:return "legacy";default:return "span";}}
static bool exportFile(bool v2,bool archive=false){
 char filename[64];snprintf(filename,sizeof(filename),v2?"hardware-trace-v2-%u.csv":"hardware-trace-%u.csv",unsigned(first));
 FILE* f=fopen(FileUtils::fromCwd(archive?filename:(v2?"hardware-trace-v2.csv":"hardware-trace.csv")).c_str(),"w");
 if(!f)return false;
 fprintf(f,"label,start_ticks,duration_ticks,frame,value%s\n",v2?",event_id,parent_id,kind,record_job,present_job,sequence,context,requested,effective,clock_epoch":"");
 for(u32 i=0;i<count;++i){const Event& e=events[i];
  fprintf(f,"%s,%u,%u,%u,%u",e.label,unsigned(e.start),unsigned(e.duration),unsigned(e.frame),unsigned(e.value));
  if(v2)fprintf(f,",%u,%u,%s,%u,%u,%u,%u,%u,%u,%u",unsigned(e.id),unsigned(e.parent),name(e.kind),unsigned(e.owner.recordJob),unsigned(e.owner.presentJob),unsigned(e.owner.sequence),unsigned(e.owner.context),unsigned(e.owner.requested),unsigned(e.owner.effective),unsigned(e.epoch));
  fprintf(f,"\n");}
 fprintf(f,"END,%u,%u,%u,%u",unsigned(count),unsigned(dropped),unsigned(frames),unsigned(first));
 if(v2)fprintf(f,",0,%u,end,0,0,0,4294967295,0,0,0",unsigned(invalid));
 fprintf(f,"\n");const bool ok=!ferror(f);return fclose(f)==0&&ok;
}
void endFrame(){if(!enabled){++frame;return;}
 if(active){if(depth!=1)++invalid;endScope(root);}
 active=false;++frame;if(frame<first+frames)return;
 enabled=false;const bool legacy=exportFile(false),v2=exportFile(true);
 const bool archive=exportFile(true,true);
 printf("HWTRACE complete first=%u frames=%u events=%u dropped=%u invalid=%u legacy=%u v2=%u archive=%u\n",unsigned(first),unsigned(frames),unsigned(count),unsigned(dropped),unsigned(invalid),unsigned(legacy),unsigned(v2),unsigned(archive));
 if(!retain){free(events);events=nullptr;capacity=0;}
}
} }
#endif

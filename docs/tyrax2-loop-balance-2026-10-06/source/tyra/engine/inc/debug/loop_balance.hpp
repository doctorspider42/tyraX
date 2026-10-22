#pragma once
// PRIVATE diagnostic only. Every wait below already exists in the producer.
// Count is elapsed time (includes interrupt/thread preemption), not retired EE work.
#include <stdint.h>
#include <stdio.h>
namespace LoopBalance {
using U=unsigned;
static_assert(sizeof(U)==4,"private Count words must be 32 bit");
enum Kind:U {Vif=0,Gif=1,Gs=2};
enum Phase:U {Misc=0,Logic=1,Render=2};
struct Row {
 U frame=0,enabled=0,ok=1,gameStart=0,renderStart=0,gameEnd=0;
 U waits[3][3]{},pacing[3]{},calls[3]{},reads=0,nested=0;
 U jobs=0,firstSeq=0,lastSeq=0,firstCtx=0xffffffffu,lastCtx=0xffffffffu;
 U firstOwner=0xffffffffu,lastOwner=0xffffffffu,jobPhaseMask=0;
};
inline Row samples[128]{},current{};
static_assert(sizeof(samples)==15872,"unexpected private balance array size");
inline bool active=false,valid=true;
inline U origin=0,phase=Misc,depth=0,outerKind=0,markers=0;
inline U pendingSeq=0,pendingCtx=0xffffffffu,pendingOwner=0xffffffffu;
inline U ticks(){++current.reads;U t;asm volatile("mfc0 %0, $9":"=r"(t)::"memory");return t;}
inline bool mode(U f){U p=f/1800,i=f%1800-900;if(f%1800<900||i>=128||p>=3)return false;
 bool middle=i>=40&&i<88;return p==0||(p==1?middle:!middle);}
inline void begin(U f,U existingStart){
 active=false;if(f%1800<900||f%1800>=1028)return;
 current=Row{};current.frame=f;current.enabled=mode(f);origin=existingStart;
 phase=Misc;depth=markers=0;active=current.enabled;
}
inline void cancel(){active=false;}
inline void marker(U next,U bit,U& target){
 if(!active)return;
 if(depth||markers!=(bit-1))current.ok=0;
 target=ticks()-origin;phase=next;markers|=bit;
}
inline void gameStart(){marker(Logic,1,current.gameStart);}
inline void renderStart(){marker(Render,2,current.renderStart);}
inline void gameEnd(){marker(Misc,4,current.gameEnd);}
class Wait {
 bool on=false,outer=false;U start=0,owner=0,kind=0;
 public:
 explicit Wait(Kind k):on(active),kind(k){
  if(!on)return;
  owner=phase;outer=depth++==0;
  if(outer){outerKind=k;++current.calls[k];start=ticks();}
  else{++current.nested;if(outerKind!=k)current.ok=0;}
 }
 ~Wait(){if(!on)return;if(!active||!depth||owner!=phase){current.ok=0;valid=false;return;}
  if(outer){U end=ticks();current.waits[owner][kind]+=end-start;}
  --depth;
 }
 Wait(const Wait&)=delete;Wait& operator=(const Wait&)=delete;
};
inline void pace(U existingBegin,U existingEnd){
 if(!active)return;
 if(depth)current.ok=0;
 U b=existingBegin-origin,e=existingEnd-origin;
 if(b>e||e>=0x80000000u)current.ok=0;
 current.pacing[phase]+=e-b;
}
inline void submitted(U seq,U ctx,U owner){pendingSeq=seq;pendingCtx=ctx;pendingOwner=owner;}
inline void completion(U seq,U ctx){
 if(!active)return;
 if(seq!=pendingSeq||ctx!=pendingCtx||pendingOwner>current.frame)current.ok=0;
 if(!current.jobs){current.firstSeq=seq;current.firstCtx=ctx;current.firstOwner=pendingOwner;}
 ++current.jobs;current.lastSeq=seq;current.lastCtx=ctx;current.lastOwner=pendingOwner;current.jobPhaseMask|=1u<<phase;
}
inline void finish(U existingEnd,U whole,U ordinaryPacing){
 if(current.frame%1800<900||current.frame%1800>=1028)return;
 if(current.enabled){
  U sums[3]={current.gameStart+whole-current.gameEnd,current.renderStart-current.gameStart,current.gameEnd-current.renderStart};
  if(depth||markers!=7||existingEnd-origin!=whole||current.gameStart>current.renderStart||current.renderStart>current.gameEnd||current.gameEnd>whole)current.ok=0;
  U ps=0;for(U p=0;p<3;++p){U used=current.pacing[p];ps+=used;for(U k=0;k<3;++k)used+=current.waits[p][k];if(used>sums[p])current.ok=0;}
  if(ps!=ordinaryPacing||current.reads!=3+2*(current.calls[0]+current.calls[1]+current.calls[2]))current.ok=0;
 }
 if(!current.ok)valid=false;
 samples[current.frame%1800-900]=current;active=false;
}
inline void write(FILE* f,U p,U i){
 const Row&r=samples[i];
 fprintf(f,"LOG: CLOSEOUTBAL phase=%u i=%u frame=%u enabled=%u ok=%u gameStart=%u renderStart=%u gameEnd=%u reads=%u nested=%u jobs=%u firstSeq=%u lastSeq=%u firstCtx=%u lastCtx=%u firstOwner=%u lastOwner=%u jobPhaseMask=%u",p,i,r.frame,r.enabled,r.ok,r.gameStart,r.renderStart,r.gameEnd,r.reads,r.nested,r.jobs,r.firstSeq,r.lastSeq,r.firstCtx,r.lastCtx,r.firstOwner,r.lastOwner,r.jobPhaseMask);
 for(U j=0;j<3;++j){fprintf(f," pace%u=%u calls%u=%u",j,r.pacing[j],j,r.calls[j]);for(U k=0;k<3;++k)fprintf(f," wait%u%u=%u",j,k,r.waits[j][k]);}
 fprintf(f,"\n");
}
}

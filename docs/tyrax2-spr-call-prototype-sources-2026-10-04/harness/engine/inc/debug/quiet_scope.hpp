#pragma once
// PRIVATE minimal scope observer. Common layout/traversal exists in BOTH arms.
#include <stdint.h>
namespace QuietCadence { inline uint32_t ticks(); }
namespace QuietScope {
using U=uint32_t;
struct Config { U mode=0,selector=0,ordinal=0,expectedCount=0; };
// Mode2 bagCount=Core sum; Mode3 bagCount=Work sum/high15 packed=Work calls.
struct Record { U duration=0,bagCount=0,packed=0; };
static_assert(sizeof(Record)==12,"scope record must be exactly12bytes");
inline Config config{};
inline Record records[128]{};
inline U frame=0,coreCalls=0,stageCalls=0,hits=0,duration=0,bagCount=0,coreDepth=0,stageDepth=0,activeStage=0,vertexSum=0,coreSum=0,workDepth=0,workCalls=0,workSum=0;
inline bool window=false,on=false,bad=false,valid=true;
inline U rows[3]{},vertexMin[3]{},vertexMax[3]{},callsMin[3]{},callsMax[3]{},invalidSamples[3]{},workMin[3]{},workMax[3]{},workTotal[3]{};
inline bool configValid(const Config& c) {
 return (c.mode==0 && c.selector>=1 && c.selector<=8 && c.ordinal==0 && c.expectedCount==0)
     || (c.mode==1 && c.selector>=1 && c.selector<=3 && c.ordinal>=1 && c.ordinal<=32767)
     || ((c.mode==2 || c.mode==3) && c.selector==4 && c.ordinal>=1 && c.ordinal<=32767 && c.expectedCount>0);
}
inline U readsPerSample(){return config.mode==3?0U:config.mode==2?2U+2U*config.ordinal:2U;}
inline U readsPerOnPhase(){return 128U*readsPerSample();}
inline void init(const Config& c){config=c;valid=configValid(c);window=on=false;for(auto& r:records)r=Record{};for(U i=0;i<3;++i){rows[i]=vertexMax[i]=callsMax[i]=invalidSamples[i]=workMax[i]=workTotal[i]=0;vertexMin[i]=callsMin[i]=workMin[i]=0xffffffffU;}}
inline void begin(U index,bool enabled){frame=index;const U o=index%1800;window=index<5400&&o>=900&&o<1028;on=window&&enabled;coreCalls=stageCalls=hits=duration=bagCount=coreDepth=stageDepth=activeStage=vertexSum=coreSum=workDepth=workCalls=workSum=0;bad=false;}
inline void inc(U& n){if(n==32767){bad=true;}else ++n;}
inline void add(U& n,U value){if(value>0xffffffffU-n){bad=true;}else n+=value;}
inline void finish(){
 if(!window){return;}
 const U p=frame/1800,i=frame%1800-900;
 if(coreDepth||stageDepth||workDepth||activeStage||hits!=1||rows[p]!=i)bad=true;
 if(config.mode>=2){if(coreCalls!=config.ordinal||vertexSum!=config.expectedCount||stageCalls!=1||(config.mode==2?coreSum:workSum)>duration)bad=true;if(vertexSum<vertexMin[p])vertexMin[p]=vertexSum;if(vertexSum>vertexMax[p])vertexMax[p]=vertexSum;if(coreCalls<callsMin[p])callsMin[p]=coreCalls;if(coreCalls>callsMax[p])callsMax[p]=coreCalls;}
 if(config.mode==3){if(workCalls==0&&workSum!=0)bad=true;if(workCalls<workMin[p])workMin[p]=workCalls;if(workCalls>workMax[p])workMax[p]=workCalls;add(workTotal[p],workCalls);}
 if(config.mode>=2&&bad)++invalidSamples[p];
 records[i]={on?duration:0,config.mode==3?(on?workSum:0):config.mode==2?(on?coreSum:0):config.mode==1?bagCount:0,coreCalls|((config.mode==3?workCalls:stageCalls)<<15)|(hits?1U<<30:0)|(bad?1U<<31:0)};++rows[p];valid=valid&&!bad;
}
struct Stage {
 bool entered=false,selected=false,timed=false;U start=0,previous=0;
 explicit Stage(U id){if(!window)return;if(config.mode==0){if(config.selector!=id)return;}else if(config.mode==1){if(id<1||id>3)return;}else if(config.mode>=2){if(id!=4)return;}else return;entered=true;previous=activeStage;if(stageDepth||(config.mode>=2&&(coreDepth||workDepth)))bad=true;++stageDepth;activeStage=id;if(config.mode==1)return;inc(stageCalls);selected=true;if(stageCalls==1){++hits;timed=on;if(timed)start=QuietCadence::ticks();}else bad=true;}
 ~Stage(){if(!entered)return;if(timed){const U delta=QuietCadence::ticks()-start;duration=delta;if(delta>=0x80000000U)bad=true;}if(config.mode>=2&&(coreDepth||workDepth))bad=true;if(!stageDepth)bad=true;else --stageDepth;activeStage=previous;}
 Stage(const Stage&)=delete;Stage& operator=(const Stage&)=delete;
};
struct Core {
 bool entered=false,timed=false,aggregate=false;U start=0;
 template<class Bag> explicit Core(const Bag* bag){
  if(!window){return;}
  entered=true;const bool nested=coreDepth!=0;if(nested)bad=true;++coreDepth;const bool overflow=coreCalls==32767;inc(coreCalls);if(overflow)return;
  if(config.mode>=2){aggregate=config.mode==2;if(!bag)bad=true;else add(vertexSum,bag->count);if(activeStage!=4||stageDepth!=1||nested)bad=true;if(config.mode==3&&workDepth)bad=true;timed=on&&aggregate&&!nested&&activeStage==4&&stageDepth==1;if(timed)start=QuietCadence::ticks();return;}
  if(config.mode!=1||coreCalls!=config.ordinal){return;}
  ++hits;if(!bag){bad=true;bagCount=0;}else{bagCount=bag->count;if(bagCount!=config.expectedCount||activeStage!=config.selector)bad=true;}timed=on;if(timed)start=QuietCadence::ticks();
 }
 ~Core(){if(!entered)return;if(config.mode==3&&workDepth)bad=true;if(timed){const U delta=QuietCadence::ticks()-start;if(aggregate)add(coreSum,delta);else duration=delta;if(delta>=0x80000000U)bad=true;}if(!coreDepth)bad=true;else --coreDepth;}
 Core(const Core&)=delete;Core& operator=(const Core&)=delete;
};
struct Work {
 bool entered=false,timed=false;U start=0,ownerCore=0,ownerFrame=0;
 explicit Work(bool eligible=true){
  if(!eligible||!window||config.mode!=3){return;}
  entered=true;ownerCore=coreCalls;ownerFrame=frame;const bool disjoint=workDepth==0;const bool context=activeStage==4&&stageDepth==1&&coreDepth==1;
  if(!disjoint||!context)bad=true;
  ++workDepth;const bool overflow=workCalls==32767;inc(workCalls);timed=on&&disjoint&&context&&!overflow;if(timed)start=QuietCadence::ticks();
 }
 void close(){if(!entered)return;entered=false;if(ownerFrame!=frame||ownerCore!=coreCalls||activeStage!=4||stageDepth!=1||coreDepth!=1)bad=true;if(timed){const U delta=QuietCadence::ticks()-start;add(workSum,delta);if(delta>=0x80000000U)bad=true;}if(!workDepth)bad=true;else --workDepth;}
 ~Work(){close();}
 Work(const Work&)=delete;Work& operator=(const Work&)=delete;
};

}

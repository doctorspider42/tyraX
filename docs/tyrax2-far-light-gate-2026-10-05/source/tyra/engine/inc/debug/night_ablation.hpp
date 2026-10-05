#pragma once
// PRIVATE experiment only: common counters do not read clocks or submit work.
#include <stdint.h>
#include <string.h>
namespace NightAblation {
enum Group : unsigned { Live=1, LightEffects=2, Shadows=4, Particles=8,
                       PostFx=16, Env=32, Sky=64 };
struct Counter { uint32_t attempted=0, executed=0, skipped=0, submitted=0; };
inline Counter commonCounter[7]{};
enum Extra : unsigned { Pools=1, BeamsCoronas=2, VehicleLampGlow=4 };
inline Counter extraCounter[3]{};
inline unsigned extraMask=0;
inline unsigned mask=0;
inline bool collectCounters=false;
inline bool valid=true;
// PRIVATE kind12: wide-margin exponent broad phase. No state cache or score changes.
inline bool farPickSelected=false,farPickEnabled=false;
struct FarPickCounter {uint32_t calls=0,candidates=0,rejected=0,applied=0,compared=0,mismatches=0;};
inline FarPickCounter farPickCounter{};
inline bool farLightCandidate(float dx,float dy,float dz,float range,float radius){
 uint32_t x,y,z,r,w;memcpy(&x,&dx,4);memcpy(&y,&dy,4);memcpy(&z,&dz,4);memcpy(&r,&range,4);memcpy(&w,&radius,4);
 if((r>>31)||r==0||((w>>31)&&(w&0x7fffffff)))return false;
 unsigned re=r>>23,we=(w&0x7fffffff)>>23;
 unsigned se=re>we?re:we;
 x&=0x7fffffff;y&=0x7fffffff;z&=0x7fffffff;
 uint32_t mag=x>y?x:y;mag=mag>z?mag:z;unsigned de=mag>>23;
 // Supported domain keeps dominant squared distance normal and summed squares finite.
 // |axis| >= 2^(se-127+3), while range+radius < 2^(se-127+2).
 // At least a factor-two separation; no boundary candidate uses this rejection.
 return se>=80&&se<=175&&de<=180&&de>=se+3;
}

inline unsigned slot(unsigned bit) {
 for(unsigned i=0;i<7;++i) if(bit==(1U<<i)) return i;
 valid=false;return 0;
}
inline bool setMask(unsigned value) {
 // Optional Env/Sky cuts are reserved, not implemented by this first proposal.
 if(value>31){valid=false;return false;}mask=value;return true;
}
inline unsigned extraSlot(unsigned bit) {
 for(unsigned i=0;i<3;++i) if(bit==(1U<<i)) return i;
 valid=false;return 0;
}
inline bool setExtraMask(unsigned value) {
 if(value>7){valid=false;return false;}extraMask=value;return true;
}
inline bool extraDisabled(unsigned bit){return (extraMask&bit)!=0;}
inline bool extraSkip(unsigned bit){
 if(!collectCounters)return extraDisabled(bit);
 auto& c=extraCounter[extraSlot(bit)];++c.attempted;
 if(extraDisabled(bit)){++c.skipped;return true;}++c.executed;return false;
}
inline void extraSubmitted(unsigned bit){if(collectCounters)++extraCounter[extraSlot(bit)].submitted;}
inline bool disabled(unsigned bit){return (mask&bit)!=0;}
inline bool skip(unsigned bit){
 if(!collectCounters)return disabled(bit);
 auto& c=commonCounter[slot(bit)];++c.attempted;
 if(disabled(bit)){++c.skipped;return true;}++c.executed;return false;
}
inline void submitted(unsigned bit){if(collectCounters)++commonCounter[slot(bit)].submitted;}
// PRIVATE independent wild variants; no pool-cache candidate in this dialect.
inline unsigned wildVariant=0;
inline bool wildEnabled=false;
inline bool setWildVariant(unsigned variant,bool enabled){
 if((variant!=0&&variant!=5&&variant!=6)||(variant==0&&enabled)){valid=false;return false;}
 wildVariant=variant;wildEnabled=enabled;return true;
}
inline bool wildActive(unsigned variant){return wildVariant==variant&&wildEnabled;}
struct WildCounter{uint32_t invocations=0,eligible=0,applied=0,fallback=0,boundary=0,nonfinite=0,coldCompared=0,coldMismatches=0,inputUnits=0,outputUnits=0,invalid=0;};
inline WildCounter wildPlaneCounter{},wildConeCounter{};
inline void wildAdd(WildCounter& c,uint32_t& field,uint32_t value){
 if(value>~uint32_t(0)-field){if(c.invalid!=~uint32_t(0)){++c.invalid;}valid=false;return;}field+=value;
}
inline void observeWild(unsigned variant,bool eligible,bool applied,bool boundary,bool nonfinite,bool compared,bool mismatch,uint32_t inputUnits,uint32_t outputUnits){
 if(!collectCounters)return;
 if(variant!=5&&variant!=6){valid=false;return;}
 auto& c=variant==5?wildPlaneCounter:wildConeCounter;
 if((applied&&!eligible)||(applied!=bool(wildActive(variant)&&eligible))||((boundary||nonfinite)&&eligible)||(mismatch&&!compared)){
  if(c.invalid!=~uint32_t(0)){++c.invalid;}valid=false;return;
 }
 wildAdd(c,c.invocations,1);wildAdd(c,c.eligible,eligible?1u:0u);wildAdd(c,c.applied,applied?1u:0u);wildAdd(c,c.fallback,eligible?0u:1u);wildAdd(c,c.boundary,boundary?1u:0u);wildAdd(c,c.nonfinite,nonfinite?1u:0u);wildAdd(c,c.coldCompared,compared?1u:0u);wildAdd(c,c.coldMismatches,mismatch?1u:0u);wildAdd(c,c.inputUnits,inputUnits);wildAdd(c,c.outputUnits,outputUnits);
 if(mismatch){valid=false;}
}

// PRIVATE independent kind7 source color-table experiment.
inline bool poolTableSelected=false,poolTableEnabled=false;
inline bool setPoolTableEnabled(bool enabled){poolTableEnabled=enabled;return true;}
struct PoolTableCounter{uint32_t invocations=0,eligible=0,applied=0,fallback=0,sourceVertices=0,admittedVertices=0,baselineColorQwords=0,tableColorQwords=0,coldCompared=0,coldMismatches=0,invalid=0;};
inline PoolTableCounter poolTableCounter{};
inline void tableAdd(uint32_t& field,uint32_t value){auto& c=poolTableCounter;if(value>~uint32_t(0)-field){if(c.invalid!=~uint32_t(0)){++c.invalid;}valid=false;return;}field+=value;}
inline void observePoolTable(bool eligible,uint32_t sourceVertices,bool compared,bool mismatch){
 if(!poolTableSelected||!collectCounters)return;
 auto& c=poolTableCounter;
 if((eligible&&(sourceVertices==0||sourceVertices>75))||(mismatch&&!compared)){if(c.invalid!=~uint32_t(0)){++c.invalid;}valid=false;return;}
 const bool applied=poolTableEnabled&&eligible;
 tableAdd(c.invocations,1);tableAdd(c.eligible,eligible?1u:0u);tableAdd(c.applied,applied?1u:0u);tableAdd(c.fallback,eligible?0u:1u);tableAdd(c.sourceVertices,sourceVertices);tableAdd(c.admittedVertices,applied?sourceVertices:0u);tableAdd(c.baselineColorQwords,sourceVertices);tableAdd(c.tableColorQwords,applied?2u:0u);tableAdd(c.coldCompared,compared?1u:0u);tableAdd(c.coldMismatches,mismatch?1u:0u);
 if(mismatch){valid=false;}
}

// PRIVATE kind9: cold EE admission observations, never accepted VU sprites.
inline bool coronaSpriteSelected=false;
struct CoronaCounter {uint32_t coldPackets=0,eligible=0,requested=0,fallback=0,sourceVertices=0,requestedVertices=0,fogOn=0,shaderLit=0,invalid=0;};
inline CoronaCounter coronaCounter{};
inline void coronaAdd(uint32_t& field,uint32_t value){auto& c=coronaCounter;if(value>~uint32_t(0)-field){if(c.invalid!=~uint32_t(0))++c.invalid;valid=false;return;}field+=value;}
inline void observeCorona(bool eligible,bool requested,bool enabled,uint32_t count,bool fogOn,bool shaderLit){
 if(!coronaSpriteSelected||!collectCounters)return;
 auto& c=coronaCounter;
 if(requested!=(enabled&&eligible)||(eligible&&(count==0||count>72||count%6!=0))){if(c.invalid!=~uint32_t(0))++c.invalid;valid=false;return;}
 coronaAdd(c.coldPackets,1);coronaAdd(c.eligible,eligible?1u:0u);coronaAdd(c.requested,requested?1u:0u);coronaAdd(c.fallback,eligible?0u:1u);coronaAdd(c.sourceVertices,count);coronaAdd(c.requestedVertices,requested?count:0u);coronaAdd(c.fogOn,fogOn?1u:0u);coronaAdd(c.shaderLit,shaderLit?1u:0u);
}
}

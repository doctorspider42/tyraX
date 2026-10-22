#pragma once
// PRIVATE experiment only: common counters do not read clocks or submit work.
#include <stdint.h>
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
// Modified by TyraX: private independent kind8 lattice switch.
inline bool poolLatticeSelected=false,poolLatticeEnabled=false;
struct LatticeCounter {uint32_t invocations=0,eligible=0,applied=0,fallback=0,source=0,unique=0,invalid=0;};
inline LatticeCounter poolLatticeCounter{};
inline void observeLattice(bool eligible,bool applied,uint32_t count,uint32_t unique) {
 if(!poolLatticeSelected || !collectCounters)return;
 auto& c=poolLatticeCounter;
 if((applied&&!eligible)||(eligible&&(count==0||unique>=count))) {++c.invalid;valid=false;return;}
 ++c.invocations;c.eligible+=eligible;c.applied+=applied;c.fallback+=!eligible;
 c.source+=count;c.unique+=applied?unique:count;
}
inline bool poolTableSelected=false,poolTableEnabled=false;
inline bool poolReplayBypass(){return (poolLatticeSelected&&poolLatticeEnabled) || (poolTableSelected&&collectCounters);}
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
}

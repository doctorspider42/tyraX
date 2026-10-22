#pragma once
// PRIVATE experiment only: common counters do not read clocks or submit work.
#include <stdint.h>
#include <vector>
#include <cstddef>
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

// PRIVATE same-ELF pool cache diagnostic; common reason work is unpriced.
inline bool poolColorSplitCandidate=false;
inline bool setPoolColorSplitCandidate(bool value){poolColorSplitCandidate=value;return true;}
struct PoolCacheCounter {
 uint32_t flushes=0,unchanged=0,initial=0,geometryChanged=0,colorOnlyEligible=0;
 uint32_t geometryRebuilds=0,colorOnlyUpdates=0,eligibleBatchVerts=0,invalid=0;
};
inline PoolCacheCounter poolCacheCounter{};
enum PoolReason : unsigned { PoolUnchanged=0,PoolGeometryChanged=1,PoolColorOnly=2,PoolInvalid=3 };
inline PoolReason poolReason(const std::vector<unsigned int>& key,
                             const std::vector<unsigned int>& last){
 if(key.empty()||key.size()%7||last.size()%7)return PoolInvalid;
 if(key==last)return PoolUnchanged;
 if(key.size()!=last.size())return PoolGeometryChanged;
 for(std::size_t k=0;k<key.size();k+=7)
  if(key[k]!=last[k]||key[k+1]!=last[k+1]||key[k+2]!=last[k+2])return PoolGeometryChanged;
 return PoolColorOnly;
}
inline void poolAdd(uint32_t& value,uint32_t delta){
 if(delta>~uint32_t(0)-value){if(poolCacheCounter.invalid!=~uint32_t(0)){++poolCacheCounter.invalid;}
  valid=false;return;}
 value+=delta;
}
inline void observePoolCache(PoolReason reason,bool initial,std::size_t vertices){
 if(!collectCounters)return;
 if(reason>PoolColorOnly||vertices>0xffffffffu||(initial&&reason!=PoolGeometryChanged)){
  if(poolCacheCounter.invalid!=~uint32_t(0)){++poolCacheCounter.invalid;}
  valid=false;return;
 }
 auto& c=poolCacheCounter;poolAdd(c.flushes,1);poolAdd(c.eligibleBatchVerts,static_cast<uint32_t>(vertices));
 if(initial)poolAdd(c.initial,1);
 if(reason==PoolUnchanged)poolAdd(c.unchanged,1);
 else if(reason==PoolGeometryChanged){poolAdd(c.geometryChanged,1);poolAdd(c.geometryRebuilds,1);}
 else{poolAdd(c.colorOnlyEligible,1);if(poolColorSplitCandidate)poolAdd(c.colorOnlyUpdates,1);else poolAdd(c.geometryRebuilds,1);}
}
}

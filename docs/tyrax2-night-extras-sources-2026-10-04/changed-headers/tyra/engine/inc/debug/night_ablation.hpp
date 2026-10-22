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
}

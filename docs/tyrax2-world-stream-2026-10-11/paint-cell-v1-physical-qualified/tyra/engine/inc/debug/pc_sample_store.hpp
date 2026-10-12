#pragma once
// Private bounded PC record store. No SDK, timer or interrupt ownership here.
#include <cstdint>
namespace PcSample {
struct Row {
  uint32_t pc, epc, phase, region;
  uint64_t scheduled, actual;
};
template<unsigned Capacity> struct Store {
  static_assert(Capacity>0,"Nonempty sample buffer required");
  Row rows[Capacity]{};
  unsigned count=0,dropped=0;
  bool armed=false;
  void push(uint32_t pc,uint32_t epc,uint32_t phase,uint32_t region,
            uint64_t scheduled,uint64_t actual){
    if(!armed)return;
    if(count==Capacity){if(dropped!=UINT32_MAX)++dropped;return;}
    rows[count++]={pc,epc,phase,region,scheduled,actual};
  }
  // Called only with IRQs stopped by the future adapter.
  void reset(){count=dropped=0;armed=false;}
};
// SDK advances scheduled by the callback result. Schedule from actual time
// after a delayed interrupt; do not generate fake catch-up samples at one PC.
inline bool nextDelay(uint64_t scheduled,uint64_t actual,uint64_t interval,uint64_t& result){
  if(!interval || actual<scheduled || actual>UINT64_MAX-interval ||
     actual-scheduled>UINT64_MAX-interval)return false;
  result=actual-scheduled+interval;
  return result!=UINT64_MAX; // SDK's free-counter sentinel, not a period.
}
}

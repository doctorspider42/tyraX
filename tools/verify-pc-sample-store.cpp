#include "tyrax2-pc-sample-store.hpp"
#include <cassert>
#include <cstdio>
int main(){
  PcSample::Store<4> s;
  s.push(1,2,3,4,10,11);assert(!s.count && !s.dropped);
  s.armed=true;
  for(unsigned i=0;i<4;++i)s.push(0x1000+4*i,0x1004+4*i,i,7,100+i,101+i);
  assert(s.count==4 && !s.dropped);
  for(unsigned i=0;i<4;++i){const auto& r=s.rows[i];
    assert(r.pc==0x1000+4*i && r.epc==0x1004+4*i && r.phase==i && r.region==7);
    assert(r.scheduled==100+i && r.actual==101+i);
  }
  s.push(99,99,99,99,99,99);assert(s.count==4 && s.dropped==1 && s.rows[3].pc==0x100c);
  s.armed=false;s.push(99,99,99,99,99,99);assert(s.dropped==1);
  s.armed=true;s.dropped=UINT32_MAX;s.push(99,99,99,99,99,99);assert(s.dropped==UINT32_MAX);
  s.reset();assert(!s.armed && !s.count && !s.dropped);
  uint64_t delay=0;
  assert(PcSample::nextDelay(100,100,10,delay) && delay==10);
  assert(PcSample::nextDelay(100,1100,10,delay) && delay==1010);
  assert(100+delay==1110); // deadline follows actual, no catch-up burst.
  assert(!PcSample::nextDelay(100,99,10,delay));
  assert(!PcSample::nextDelay(0,100,0,delay));
  assert(!PcSample::nextDelay(0,UINT64_MAX,10,delay));
  assert(!PcSample::nextDelay(0,UINT64_MAX-10,10,delay));
  assert(!PcSample::nextDelay(UINT64_MAX-20,UINT64_MAX-1,5,delay));
  assert(PcSample::nextDelay(UINT64_MAX-20,UINT64_MAX-10,5,delay) && delay==15);
  std::puts("PCSTORE bounded rows/disabled capture/overflow/reset/delayed IRQ controls PASS");
}

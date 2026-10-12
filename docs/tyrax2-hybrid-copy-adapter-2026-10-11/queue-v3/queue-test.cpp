#include <cstdint>
#include <cassert>
#include <cstdio>
using u32=std::uint32_t;
static bool framePipelined=false;
static volatile u32 nativeInFlight=0,head=0,tail=0,runningSequence=0,completed=0;
static volatile bool running=false;
static volatile u32 chcr=0;
static volatile u32* const kChcr=&chcr;
static constexpr u32 kStr=0x100;
class Vif1Queue { public: static bool canCopyCompletedNativeFrame(u32); };
#define TYRA_FRAME_PIPELINE_SUPPORT 1
bool Vif1Queue::canCopyCompletedNativeFrame(u32 sequence) {
#if TYRA_FRAME_PIPELINE_SUPPORT
  if (!sequence || !framePipelined || nativeInFlight != sequence ||
      head != tail || (*kChcr & kStr) || runningSequence != sequence)
    return false;
  // A non-ISR queue may still mark the now-idle old DMA as running. Its
  // exact last-started identity is sufficient; do not advance it from ISR.
  return running || completed == sequence;
#else
  (void)sequence;
  return false;
#endif
}

#ifdef __mips__
void actualQueueSetup(bool pipelined) { framePipelined=pipelined; }
bool actualQueueProof(u32 job) { return Vif1Queue::canCopyCompletedNativeFrame(job); }
#else
int main() {
  unsigned cases=0,accepted=0;
  constexpr u32 ids[]={0,1,42,0xffffffff};
  for(u32 job:ids)for(u32 inFlight:ids)for(u32 started:ids)for(u32 done:ids)
    for(unsigned flags=0;flags<16;++flags)for(u32 ringBase:ids) {
      nativeInFlight=inFlight;runningSequence=started;completed=done;
      framePipelined=flags&1;running=flags&2;chcr=(flags&4)?0x100:0;
      head=ringBase;tail=ringBase+((flags&8)?1:0);
      const bool expected=job!=0 && (flags&1) && !(flags&4) && !(flags&8) &&
                          inFlight==job && started==job && ((flags&2)||done==job);
      bool result=Vif1Queue::canCopyCompletedNativeFrame(job);
      assert(result==expected);accepted+=result;++cases;
      assert(nativeInFlight==inFlight&&runningSequence==started&&completed==done&&
             head==ringBase&&tail==ringBase+((flags&8)?1:0)&&
             framePipelined==bool(flags&1)&&running==bool(flags&2)&&chcr==((flags&4)?0x100u:0));
    }
  assert(cases==16384&&accepted==60);
  std::printf("HYBRIDQUEUE cases=%u accepted=%u no-flush-no-advance=PASS\n",cases,accepted);
}
#endif

#pragma once
#include <cstdio>
#include <cstring>
#include <kernel.h>
#include <timer.h>
#include <graph.h>
#include <draw.h>
#include <packet2.h>
#include "tyrax2-hybrid-copy-ee.hpp"
#include "tyrax2-hybrid-copy-window.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include "debug/triple_trial.hpp"

// Private A41 full-game fixture, deliberately NOT a production setting.
namespace Tyra::HybridCopyTrial {
using U = unsigned;
inline volatile U& r32(U a) { return *reinterpret_cast<volatile U*>(a); }
inline volatile u64& r64(U a) { return *reinterpret_cast<volatile u64*>(a); }
inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }
inline volatile bool enabled = false, installed = false, copying = false;
inline constexpr bool lateReadySupport = false;
inline volatile bool lateReady = false;
inline constexpr bool retainReadySupport = false;
inline bool retainNativeReady = false;
inline U retainedWaits = 0;
inline bool heldQualification = false, lateKick = false;
inline int nativeHandler = -1;
inline volatile U nativeVifEvents = 0, nativeGsEvents = 0, nativeCopies = 0;
inline bool building = false, skipNext = false;
inline volatile bool observing = false;
inline U epoch = 0, packetVersion = 0, packetWords = 0;
alignas(64) inline qword_t immutable[128];
inline U savedCount[2], savedCompare[2], savedMask;
inline u64 savedIMR, ownedIMR;
inline int handler[3] = {-1,-1,-1};
struct Row { U job, tick, kind, a0, b, a1, valid; };
inline Row rows[4096];
inline volatile U count = 0, dropped = 0, armed = 0, isrCopies = 0, fallbacks = 0;
inline volatile U completed = 0, outside = 0, refused = 0;
inline volatile U currentJob = 0;
inline U lateBaseline = 0;
inline bool reportedReservation = false;
// Conservative EXPERIMENTAL admission based on physical preflight, not WCET.
// In-game completion witnesses must validate every admitted copy separately.
inline HybridWindowBounds bounds{0,800,10000,8700,8900,288};
inline HybridWindowSample sample() {
  HybridWindowSample s{}; s.epoch = epoch;
  s.startModeBefore=*T0_MODE;s.endModeBefore=*T1_MODE;
  s.startAgeBefore=*T0_COUNT;s.endAge=*T1_COUNT;s.startAgeAfter=*T0_COUNT;
  s.startModeAfter=*T0_MODE;s.endModeAfter=*T1_MODE;
  return s;
}
inline bool window() { return installed && hybridCopyWindowReady(bounds,sample()); }
inline bool inBlank(const HybridWindowSample& s) {
  auto b=bounds;b.copyAndDispatchBudget=1;
  return hybridCopyWindowReady(b,s);
}
inline void record(U kind) {
  if(!observing)return;
  const auto s=sample();const U n=count;
  if(n>=4096){++dropped;return;}
  rows[n]={currentJob,ticks(),kind,s.startAgeBefore,s.endAge,s.startAgeAfter,U(inBlank(s))};
  count=n+1;
}
inline bool queueProof(std::uint32_t job) { return Vif1Queue::canCopyCompletedNativeFrame(job); }
struct Hardware : HybridCopyEE {
  Hardware():HybridCopyEE(HybridCopyTrial::queueProof,HybridCopyTrial::window){}
  void clearFinish() {
    if(copying){
      r64(0x12001010)=ownedIMR;
      record(2);++completed;
      if(!inBlank(sample()))++outside;
      copying=false;
    }
    HybridCopyEE::clearFinish();
  }
  void startCopy(const HybridCopyPacket& p) {
    copying=true;
    // FINISH is exclusive only for this immutable packet until release.
    r64(0x12001010)=ownedIMR&~0x200ULL;
    asm volatile("sync.l" ::: "memory");
    record(lateKick?3:1);
    HybridCopyEE::startCopy(p);
    TripleTrial::record(1);
  }
};
inline Hardware hardware;
inline HybridCopyAdapter<Hardware> adapter(hardware);
inline bool tryLateReadyISR() {
  if(!lateReadySupport || !installed || !lateReady)return false;
  const bool was=TripleTrial::inVBlank;TripleTrial::inVBlank=true;lateKick=true;
  const bool copied=adapter.nativeReadyISR();
  lateKick=false;TripleTrial::inVBlank=was;
  if(copied){++isrCopies;++nativeCopies;}
  return copied;
}
inline int nativeISR(int) {
  if(installed && lateReady){
    if(observing)++nativeVifEvents;
    tryLateReadyISR();
  }
  return 0;
}
inline int startISR(int) {
  const bool was=TripleTrial::inVBlank;TripleTrial::inVBlank=true;
  if(lateReadySupport && lateReady && heldQualification){
    // HELD-only positive entry qualification; no delay in moving prices.
    if(adapter.beginBlankISR() && !copying)r64(0x12001010)=ownedIMR&~0x200ULL;
  }else if(adapter.blankStartISR())++isrCopies;
  TripleTrial::inVBlank=was;return 0;
}
inline int endISR(int) { adapter.blankEndISR();return 0; }
inline int finishISR(int) {
  // One shot even when FINISH arrives before GIF STR clears; no IRQ spin.
  r64(0x12001010)=ownedIMR;
  if(copying)adapter.copyFinishISR();
  else if(lateReadySupport && lateReady){if(observing)++nativeGsEvents;tryLateReadyISR();}
  return 0;
}
inline void setLateReady(bool on) {
  TYRA_ASSERT(lateReadySupport && !adapter.sourceProtected(),"A42 mode switch while source owned");
  const bool restore=hardware.enterCritical();
  lateReady=on;
  if(installed){
    r64(0x12001010)=ownedIMR;
    if(on)EnableDmac(DMAC_VIF1);else DisableDmac(DMAC_VIF1);
  }
  hardware.leaveCritical(restore);
}
inline bool install() {
  if(installed)return true;
  const bool restore=hardware.enterCritical();
  savedMask=r32(0x1000f010);const U pending=r32(0x1000f000);
  // IMR is write-only: a direct read mirrors CSR, not the interrupt mask.
  // Read the kernel's mask on MAIN, then use the frozen owned value in ISR.
  savedIMR=GsGetIMR();
  // A configured/pending/enabled timer or GS interrupt is another owner's.
  if(*T0_MODE || *T1_MODE || ((savedMask|pending)&((1u<<9)|(1u<<10))) ||
     (savedMask&1) || (savedIMR!=0x7700 && savedIMR!=0x7f00) ||
     (lateReadySupport && (r32(0x1000e010)&(1u<<17)))){
    const U m0=*T0_MODE,m1=*T1_MODE;
    hardware.leaveCritical(restore);++refused;
    if(!reportedReservation){
      reportedReservation=true;
      std::printf("HYBRIDOWN refused=1 m0=%x m1=%x mask=%x pending=%x imr=%llx\n",
          m0,m1,savedMask,pending,static_cast<unsigned long long>(savedIMR));
      std::fflush(stdout);
    }
    return false;
  }
  savedCount[0]=*T0_COUNT;savedCount[1]=*T1_COUNT;
  savedCompare[0]=*T0_COMP;savedCompare[1]=*T1_COMP;
  handler[0]=AddIntcHandler(INTC_VBLANK_S,startISR,0);
  handler[1]=AddIntcHandler(INTC_VBLANK_E,endISR,0);
  handler[2]=AddIntcHandler(INTC_GS,finishISR,0);
  if(lateReadySupport){
    static_assert(!TYRA_VIF1_QUEUE_ISR,"A42 observer must not advance the VIF queue");
    nativeHandler=AddDmacHandler(DMAC_VIF1,nativeISR,0);
  }
  if(handler[0]<0 || handler[1]<0 || handler[2]<0 || (lateReadySupport && nativeHandler<0)){
    const int sources[3]={INTC_VBLANK_S,INTC_VBLANK_E,INTC_GS};
    for(U i=0;i<3;++i)if(handler[i]>=0){RemoveIntcHandler(sources[i],handler[i]);handler[i]=-1;}
    if(nativeHandler>=0){RemoveDmacHandler(DMAC_VIF1,nativeHandler);nativeHandler=-1;}
    hardware.leaveCritical(restore);++refused;return false;
  }
  *T0_COUNT=0;*T1_COUNT=0;*T0_MODE=0x9e;*T1_MODE=0xae;
  // SDK leaves VSINT unmasked with GS INTC disabled (0x7700). Mask VSINT
  // before owning GS IRQ; its CSR latch and VBlank INTC remain available.
  ownedIMR=savedIMR|0x800;
  GsPutIMR(ownedIMR);
  bounds.epoch=++epoch;installed=true;
  EnableIntc(INTC_VBLANK_S);EnableIntc(INTC_VBLANK_E);EnableIntc(INTC_GS);
  if(lateReadySupport && lateReady)EnableDmac(DMAC_VIF1);
  hardware.leaveCritical(restore);
  for(U i=0;i<8;++i){
    // SDK wait ORs the whole CSR: that can erase this pending native FINISH.
    r64(0x12001000)=8;
    asm volatile("sync.l" ::: "memory");
    while(!(r64(0x12001000)&8)){}
  }
  return true;
}
inline void uninstall() {
  if(!installed)return;
  TYRA_ASSERT(!adapter.sourceProtected() && !copying,"A41 timer teardown while source owned");
  const bool restore=hardware.enterCritical();
  installed=false;bounds.epoch=0;++epoch;
  if(nativeHandler>=0){DisableDmac(DMAC_VIF1);RemoveDmacHandler(DMAC_VIF1,nativeHandler);nativeHandler=-1;}
  GsPutIMR(savedIMR);
  *T0_MODE=0;*T1_MODE=0;
  *T0_COUNT=savedCount[0];*T1_COUNT=savedCount[1];
  *T0_COMP=savedCompare[0];*T1_COMP=savedCompare[1];
  const int sources[3]={INTC_VBLANK_S,INTC_VBLANK_E,INTC_GS};
  const U masks[3]={1u<<2,1u<<3,1u};
  for(U i=0;i<3;++i){
    if(!(savedMask&masks[i]))DisableIntc(sources[i]);
    RemoveIntcHandler(sources[i],handler[i]);handler[i]=-1;
  }
  hardware.leaveCritical(restore);
  if(lateReadySupport){
    const U dm=r32(0x1000e010)&(1u<<17);
    const U im=r32(0x1000f010)&((1u<<2)|(1u<<3)|1u);
    TYRA_ASSERT(!dm && !*T0_MODE && !*T1_MODE && GsGetIMR()==savedIMR &&
        im==(savedMask&((1u<<2)|(1u<<3)|1u)),"A42 IRQ/timer restore mismatch");
    std::printf("HYBRIDRESTORE dmac=0 timers=0 imr=1 intc=1\n");std::fflush(stdout);
  }
}
inline bool capture(packet2_t* p) {
  if(!building)return false;
  TYRA_ASSERT(!adapter.sourceProtected(),"A41 immutable copy still owned");
  const U n=U(p->next-p->base);
  TYRA_ASSERT(n+2<=128,"A41 copy packet overflow");
  std::memcpy(immutable,p->base,n*sizeof(qword_t));
  auto* end=draw_finish(immutable+n);
  packetWords=U(end-immutable);++packetVersion;
  return true;
}
inline bool arm(U job) {
  if(!enabled || !installed || !packetWords)return false;
  const bool restore=lateReadySupport?hardware.enterCritical():false;
  currentJob=job;
  const bool result=adapter.armMain(job,{std::uintptr_t(immutable),packetWords,packetVersion},true);
  if(result)++armed;else ++refused;
  if(result && lateReady)r64(0x12001010)=ownedIMR&~0x200ULL;
  if(lateReadySupport)hardware.leaveCritical(restore);
  return result;
}
inline bool complete(U job) {
  auto action=adapter.takeMain(job,retainReadySupport && retainNativeReady);
  using M=HybridCopyAdapter<Hardware>::MainAction;
  if(action==M::Unarmed)return false;
  TYRA_ASSERT(action!=M::WrongJob,"A41 completion belongs to another native job");
  const U start=ticks();
  if(retainReadySupport && action==M::WaitNative){
    ++retainedWaits;
    do{
      TYRA_ASSERT(ticks()-start<589824000u,"A43 retained native FINISH timeout");
      action=adapter.takeMain(job,true);
    }while(action==M::WaitNative);
    TYRA_ASSERT(action!=M::WrongJob && action!=M::Unarmed,"A43 retained owner lost");
  }
  if(action==M::Fallback){
    while(!adapter.consumeOldMain(job)){
      TYRA_ASSERT(ticks()-start<589824000u,"A41 old native FINISH timeout");
    }
    do{graph_wait_vsync();
      TYRA_ASSERT(ticks()-start<589824000u,"A41 fresh physical blank timeout");
    }while(!adapter.fallbackMain(job));
    ++fallbacks;
  }
  while(!adapter.finishMain(job)){
    TYRA_ASSERT(ticks()-start<589824000u,"A41 copy FINISH timeout");
  }
  TYRA_ASSERT(adapter.releaseMain(job),"A41 source release before copy completion");
  skipNext=true;return true;
}
inline void begin(U phase) {
  TYRA_ASSERT(!adapter.sourceProtected(),"A41 capture reset while frame owned");
  count=dropped=armed=isrCopies=fallbacks=completed=outside=refused=0;
  nativeVifEvents=nativeGsEvents=nativeCopies=0;
  retainedWaits=0;
  lateBaseline=adapter.late();
  observing=true;
  (void)phase;
}
inline void exportRows(U phase,U held,U armIndex) {
  TYRA_ASSERT(!adapter.sourceProtected() && !copying,"A41 incomplete capture tail");
  observing=false;
  char name[96];std::snprintf(name,sizeof(name),"host:hybrid-copy-%u-%u-%u.csv",phase,held,armIndex);
  FILE* f=std::fopen(name,"w");TYRA_ASSERT(f,"A41 witness file unavailable");
  std::fprintf(f,"job,tick,kind,a0,b,a1,valid\n");
  for(U i=0;i<count;++i){const auto& r=rows[i];std::fprintf(f,"%u,%u,%u,%u,%u,%u,%u\n",r.job,r.tick,r.kind,r.a0,r.b,r.a1,r.valid);}
  std::fclose(f);
  std::printf("HYBRIDCOPY phase=%u held=%u arm=%u enabled=%u installed=%u armed=%u isr=%u fallback=%u completed=%u outside=%u late=%u refused=%u rows=%u dropped=%u words=%u\n",
      phase,held,armIndex,U(enabled),U(installed),armed,isrCopies,fallbacks,completed,outside,adapter.late()-lateBaseline,refused,count,dropped,packetWords);
  std::fflush(stdout);
  if(lateReadySupport){
    std::printf("HYBRIDREADY phase=%u held=%u arm=%u enabled=%u vif=%u gs=%u copies=%u\n",
        phase,held,armIndex,U(lateReady),nativeVifEvents,nativeGsEvents,nativeCopies);
    std::fflush(stdout);
  }
  if(retainReadySupport){
    std::printf("HYBRIDRETAIN phase=%u held=%u arm=%u enabled=%u waits=%u\n",
        phase,held,armIndex,U(retainNativeReady),retainedWaits);
    std::fflush(stdout);
  }
}
}  // namespace Tyra::HybridCopyTrial

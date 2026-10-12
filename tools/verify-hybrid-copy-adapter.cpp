#include <array>
#include <cassert>
#include <cstdio>
#include "tyrax2-hybrid-copy-adapter.hpp"
#include "tyrax2-hybrid-copy-ee.hpp"
struct Hardware {
  bool interrupts = true, inISR = false, oldDmaDone = true, queueBound = true;
  bool busy = false, published = false, valid = true;
  bool windowReady = true;
  unsigned csr = 10, copies = 0, publications = 0, clearCount = 0, criticalCount = 0;
  std::uint32_t sequence = 42;
  std::array<char,64> trace{};
  unsigned used = 0;
  void log(char op) { assert(used < trace.size()); trace[used++] = op; }
  bool enterCritical() { ++criticalCount;bool was=interrupts;interrupts=false;return was; }
  void leaveCritical(bool restore) { if(restore)interrupts=true; }
  bool validPacket(const Tyra::HybridCopyPacket&) { return valid; }
  void publishPacket(const Tyra::HybridCopyPacket&) {
    assert(!interrupts && !inISR);published=true;++publications;log('P');
  }
  bool oldJobReady(std::uint32_t job) { return oldDmaDone && queueBound && job==sequence; }
  bool gifIdle() { return !busy; }
  bool copyWindowReady() { return windowReady; }
  bool finishSet() { return csr&2; }
  void clearFinish() { assert(!interrupts);csr&=~2u;++clearCount;log('C'); }
  void startCopy(const Tyra::HybridCopyPacket&) {
    assert(!interrupts && published && !(csr&2) && !busy);
    busy=true;++copies;log('S');
  }
};
using A=Tyra::HybridCopyAdapter<Hardware>;
using M=A::MainAction;
constexpr Tyra::HybridCopyPacket packet{0x100000,80,7};
template<class F>void irq(Hardware& h,F f){
  bool was=h.interrupts;h.interrupts=false;h.inISR=true;f();h.inISR=false;h.interrupts=was;
}
int main(){
  unsigned guards=0;
  for(unsigned mask=0;mask<32;++mask){
    Hardware h;A a(h);assert(a.armMain(42,packet,true));
    h.oldDmaDone=mask&1;h.queueBound=mask&2;h.busy=!(mask&4);h.csr=(mask&8)?10:8;
    h.windowReady=mask&16;
    irq(h,[&]{assert(a.blankStartISR()==(mask==31));});
    assert(h.publications==1 && h.copies==unsigned(mask==31));
    assert((h.csr&8)==8 && a.sourceProtected());
    if(mask==31){
      assert(h.trace[0]=='P' && h.trace[1]=='C' && h.trace[2]=='S');
      assert(!a.finishMain(42));h.busy=false;assert(!a.finishMain(42));
      h.busy=true;h.csr|=2;assert(!a.finishMain(42));h.busy=false;
      assert(a.finishMain(42) && a.takeMain(42)==M::CopyDone);
      assert(h.clearCount==2 && !a.releaseMain(43) && a.releaseMain(42));
    }else{
      assert(a.takeMain(43)==M::WrongJob && a.takeMain(42)==M::Fallback);
      h.oldDmaDone=h.queueBound=true;h.busy=false;h.csr|=2;
      assert(a.consumeOldMain(42) && a.consumeOldMain(42));
      assert(!a.fallbackMain(42));
      irq(h,[&]{assert(a.blankEndISR() && !a.blankStartISR());});
      h.windowReady=false;assert(!a.fallbackMain(42));h.windowReady=true;
      assert(a.fallbackMain(42));
      assert(!a.fallbackMain(42) && !a.consumeOldMain(42));
      h.busy=false;h.csr|=2;
      irq(h,[&]{assert(a.blankEndISR());});
      assert(a.takeMain(42)==M::CopyDone && a.releaseMain(42));
    }
    assert(h.copies==1 && h.interrupts && !a.sourceProtected() && a.late()==0);
    ++guards;
  }
  for(unsigned mask=0;mask<8;++mask){
    Hardware h;A a(h);h.valid=mask&1;
    assert(a.armMain((mask&2)?42:0,packet,mask&4)==(mask==7));
    assert(h.publications==unsigned(mask==7));++guards;
  }
  for(unsigned ownerFirst=0;ownerFirst<2;++ownerFirst){
    Hardware h;A a(h);assert(a.armMain(42,packet,true));
    if(ownerFirst){
      irq(h,[&]{assert(a.blankStartISR());});
      assert(a.takeMain(42)==M::WaitCopy);
    }else{
      assert(a.takeMain(42)==M::Fallback && a.consumeOldMain(42));
      irq(h,[&]{assert(!a.blankStartISR());});
      assert(a.fallbackMain(42));
    }
    assert(!a.releaseMain(42) && !a.armMain(43,packet,true));
    irq(h,[&]{assert(a.blankEndISR());});assert(a.late()==1);
    h.busy=false;h.csr|=2;
    assert(a.finishMain(42) && a.releaseMain(42));
    assert(h.copies==1 && h.publications==1);++guards;
  }
  Hardware h;h.interrupts=false;A a(h);assert(a.armMain(42,packet,true));
  assert(!h.interrupts && a.takeMain(42)==M::Fallback && !h.interrupts);
  for(unsigned mask=0;mask<4;++mask){
    Hardware f;A candidate(f);
    irq(f,[&]{assert(!candidate.copyFinishISR());});
    assert(candidate.armMain(42,packet,true));
    irq(f,[&]{assert(!candidate.copyFinishISR() && candidate.blankStartISR());});
    f.busy=!(mask&1);f.csr=(mask&2)?10:8;
    irq(f,[&]{assert(candidate.copyFinishISR()==(mask==3));});
    assert(candidate.sourceProtected() && f.clearCount==1u+unsigned(mask==3));
    assert(!candidate.releaseMain(43));
    f.busy=false;f.csr|=2;
    irq(f,[&]{assert(candidate.copyFinishISR() && candidate.copyFinishISR());});
    assert(f.clearCount==2 && candidate.releaseMain(42));
    irq(f,[&]{assert(candidate.blankEndISR() && !candidate.copyFinishISR());});
    assert(candidate.late()==0 && f.copies==1 && f.publications==1);
    ++guards;
  }
  for(unsigned mask=0;mask<32;++mask){
    Hardware f;A candidate(f);f.oldDmaDone=false;f.csr=8;
    assert(candidate.armMain(42,packet,true));
    irq(f,[&]{assert(!candidate.nativeReadyISR() && !candidate.blankStartISR());});
    f.oldDmaDone=mask&1;f.queueBound=mask&2;f.busy=!(mask&4);
    f.csr=(mask&8)?10:8;f.windowReady=mask&16;
    const unsigned clears=f.clearCount;
    irq(f,[&]{assert(candidate.nativeReadyISR()==(mask==31));});
    assert(f.publications==1 && f.copies==unsigned(mask==31));
    assert(f.clearCount==clears+unsigned(mask==31));
    irq(f,[&]{assert(!candidate.nativeReadyISR());});
    if(mask==31){
      f.busy=false;f.csr|=2;
      irq(f,[&]{assert(candidate.copyFinishISR());});
      assert(candidate.releaseMain(42));
    }else{
      assert(candidate.takeMain(42)==M::Fallback);
      f.oldDmaDone=f.queueBound=f.windowReady=true;f.busy=false;f.csr|=2;
      irq(f,[&]{assert(!candidate.nativeReadyISR());});
      assert(candidate.consumeOldMain(42));
      irq(f,[&]{assert(candidate.blankEndISR() && !candidate.blankStartISR());});
      assert(candidate.fallbackMain(42));
      f.busy=false;f.csr|=2;assert(candidate.finishMain(42) && candidate.releaseMain(42));
    }
    assert(f.copies==1 && candidate.late()==0);++guards;
  }
  const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
  for(const auto& order:orders)for(unsigned expired=0;expired<2;++expired){
    Hardware f;A candidate(f);f.oldDmaDone=false;f.csr=8;
    assert(candidate.armMain(42,packet,true));
    for(unsigned i=0;i<3;++i){
      if(expired && i==2)f.windowReady=false;
      irq(f,[&]{
        if(order[i]==0)candidate.blankStartISR();
        else{
          if(order[i]==1)f.oldDmaDone=true;
          else f.csr|=2;
          candidate.nativeReadyISR();
        }
      });
      assert(f.copies==unsigned(!expired && i==2));
    }
    assert(f.publications==1);
    irq(f,[&]{assert(!candidate.nativeReadyISR());});
    if(expired){
      assert(f.copies==0 && f.clearCount==0 && (f.csr&2));
      assert(candidate.takeMain(42)==M::Fallback && candidate.consumeOldMain(42));
      irq(f,[&]{assert(candidate.blankEndISR() && !candidate.blankStartISR());});
      f.windowReady=true;assert(candidate.fallbackMain(42));
    }
    f.busy=false;f.csr|=2;
    irq(f,[&]{assert(candidate.copyFinishISR());});
    assert(candidate.releaseMain(42));
    irq(f,[&]{assert(candidate.blankEndISR() && !candidate.nativeReadyISR());});
    assert(f.copies==1 && candidate.late()==0);++guards;
  }
  {
    Hardware f;A candidate(f);assert(candidate.armMain(42,packet,true));
    irq(f,[&]{assert(candidate.beginBlankISR() && !candidate.beginBlankISR());});
    assert(f.copies==0 && f.clearCount==0 && f.publications==1);
    irq(f,[&]{assert(candidate.nativeReadyISR() && !candidate.nativeReadyISR());});
    f.busy=false;f.csr|=2;
    irq(f,[&]{assert(candidate.copyFinishISR());});
    assert(candidate.releaseMain(42));
    irq(f,[&]{assert(candidate.blankEndISR() && !candidate.nativeReadyISR());});
    assert(candidate.late()==0);++guards;
  }
  for(unsigned mask=0;mask<32;++mask){
    Hardware f;A candidate(f);assert(candidate.armMain(42,packet,true));
    f.oldDmaDone=mask&1;f.queueBound=mask&2;f.busy=!(mask&4);
    f.csr=(mask&8)?10:8;f.windowReady=mask&16;
    assert(candidate.takeMain(43,true)==M::WrongJob);
    const bool ready=(mask&15)==15;
    assert(candidate.takeMain(42,true)==(ready?M::Fallback:M::WaitNative));
    assert(f.clearCount==0 && f.copies==0 && candidate.sourceProtected());
    assert(!candidate.releaseMain(42) && !candidate.armMain(43,packet,true));
    if(!ready){
      assert(candidate.takeMain(42,true)==M::WaitNative);
      f.oldDmaDone=f.queueBound=f.windowReady=true;f.busy=false;f.csr|=2;
      irq(f,[&]{assert(candidate.blankStartISR());});
      assert(candidate.takeMain(42,true)==M::WaitCopy);
    }else{
      assert(candidate.consumeOldMain(42));
      irq(f,[&]{assert(!candidate.blankStartISR());});
      f.windowReady=true;assert(candidate.fallbackMain(42));
    }
    f.busy=false;f.csr|=2;
    irq(f,[&]{assert(candidate.copyFinishISR());});
    assert(candidate.takeMain(42,true)==M::CopyDone && candidate.releaseMain(42));
    assert(candidate.takeMain(42,true)==M::Unarmed && f.interrupts);
    irq(f,[&]{assert(candidate.blankEndISR());});
    assert(f.copies==1 && f.publications==1 && f.clearCount==(ready?3u:2u) && candidate.late()==0);
    ++guards;
  }
  for(unsigned policy=0;policy<2;++policy)for(unsigned order=0;order<4;++order){
    Hardware f;A candidate(f);f.oldDmaDone=false;f.csr=8;
    assert(candidate.armMain(42,packet,true));
    irq(f,[&]{assert(!candidate.blankStartISR());});
    assert(candidate.takeMain(42,policy)==(policy?M::WaitNative:M::Fallback));
    if(order==0){
      f.oldDmaDone=true;
      irq(f,[&]{assert(!candidate.nativeReadyISR());});
      assert(candidate.takeMain(42,policy)==(policy?M::WaitNative:M::Fallback));
      f.csr|=2;
    }else if(order==1){
      f.csr|=2;
      irq(f,[&]{assert(!candidate.nativeReadyISR());});
      assert(candidate.takeMain(42,policy)==(policy?M::WaitNative:M::Fallback));
      f.oldDmaDone=true;
    }else{
      if(order==2)f.windowReady=false;
      else irq(f,[&]{assert(candidate.blankEndISR());});
      f.oldDmaDone=true;f.csr|=2;
    }
    const bool claimed=policy && order<2;
    irq(f,[&]{assert(candidate.nativeReadyISR()==claimed);});
    assert(f.clearCount==unsigned(claimed));
    if(claimed)assert(candidate.takeMain(42,true)==M::WaitCopy);
    else{
      assert(candidate.takeMain(42,policy)==M::Fallback);
      assert(candidate.consumeOldMain(42));
      assert(!candidate.fallbackMain(42));
      if(order!=3)irq(f,[&]{assert(candidate.blankEndISR());});
      irq(f,[&]{assert(!candidate.blankStartISR());});
      f.windowReady=true;assert(candidate.fallbackMain(42));
    }
    f.busy=false;f.csr|=2;
    irq(f,[&]{assert(candidate.copyFinishISR());});
    assert(candidate.releaseMain(42));
    irq(f,[&]{assert(candidate.blankEndISR());});
    assert(f.copies==1 && f.publications==1 && f.clearCount==(claimed?2u:3u) && candidate.late()==0);
    ++guards;
  }
  std::printf("HYBRIDADAPTER guards=%u old-copy-FINISH=PASS IRQ-publication=PASS fallback=PASS\n",guards);
}
#ifdef __mips__
static_assert(sizeof(Tyra::HybridCopyAdapter<Tyra::HybridCopyEE>)==60,"EE adapter ABI");
template class Tyra::HybridCopyAdapter<Tyra::HybridCopyEE>;
#endif

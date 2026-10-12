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
  std::printf("HYBRIDADAPTER guards=%u old-copy-FINISH=PASS IRQ-publication=PASS fallback=PASS\n",guards);
}
#ifdef __mips__
static_assert(sizeof(Tyra::HybridCopyAdapter<Tyra::HybridCopyEE>)==60,"EE adapter ABI");
template class Tyra::HybridCopyAdapter<Tyra::HybridCopyEE>;
#endif

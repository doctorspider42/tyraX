#define main foundationMain
#include "verify-prepared-front.cpp"
#undef main
#include "debug/prepared_runtime.hpp"

int main(){
  assert(foundationMain()==0);
  unsigned rounds=0;
  for(unsigned r=0;r<64;++r){
    assert(FrontRuntime::liveParts==0 && FrontRuntime::liveOwners==0);
    FrontRuntime::peakParts=0;
    std::vector<Tyra::Vec4> xyz(4095,Tyra::Vec4(1,2,3,1));
    PreparedFront::Key k;
    k.generation=1;k.streams[0]=reinterpret_cast<std::uintptr_t>(xyz.data());
    k.resources[1]=0x1000;k.resources[2]=0x2000;
    k.count=4095;k.capacity=3;k.programEpoch=1;k.programClass=1;
    k.programAddress=128;k.shape=PreparedFront::SingleColor|PreparedFront::Precise|
      PreparedFront::FullClip|PreparedFront::MVP;
    FrontRuntime::Attachment a,b,c;
    a.value=std::make_unique<FrontRuntime::Owner>();
    b.value=std::make_unique<FrontRuntime::Owner>();
    c.value=std::make_unique<FrontRuntime::Owner>();
    assert(a.value->compile(k) && b.value->compile(k));
    assert(FrontRuntime::liveParts==8190 && FrontRuntime::liveOwners==2);
    auto small=k;small.count=3;
    assert(!c.value->compile(small));
    auto next=small;next.generation=2;
    auto* prior=a.value->admit(k);
    assert(!a.value->compile(next) && a.value->admit(k)==prior);
    assert(!a.value->admit(next) && FrontRuntime::peakParts==8190);
    b.value.reset();
    next=k;next.generation=2;next.capacity=6;
    assert(a.value->compile(next));
    assert(FrontRuntime::liveParts==2048 && FrontRuntime::liveOwners==1);
    assert(FrontRuntime::peakParts==8190);
    assert(c.value->compile(small));
    FrontRuntime::Attachment copied(a);assert(!copied.value);
    c=a;assert(!c.value && FrontRuntime::liveParts==2048);
    a=a;assert(a.value && a.value->admit(next));
    FrontRuntime::Attachment moved(std::move(a));
    assert(!a.value && moved.value && FrontRuntime::liveParts==2048);
    moved.value.reset();
    assert(FrontRuntime::liveParts==0 && FrontRuntime::liveOwners==0);
    ++rounds;
  }
  std::printf("PASS tracked actual runtime owner: %u budget/replacement/copy/move rounds; no target timing\n",rounds);
}

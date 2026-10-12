#define WORLD_BODY_HOST
#include "tyrax2-world-owner.hpp"
#include "renderer/core/paths/path1/frame_chain_arena.hpp"
#include <iostream>
#include <map>
struct Spans {
  static inline Tyra::ImmutableSpanTable table;
  static inline std::map<const void*,unsigned> addresses;
  static inline unsigned next = 0x100000;
  static inline unsigned waited = 0;
  static inline const WorldOwner::Quad* rawReader = nullptr;
  static bool add(const void* p,unsigned size) {
    const unsigned a = next; next += 0x100000;
    if (!table.add(a,size)) return false;
    addresses.emplace(p,a); return true;
  }
  static void retire(const void* p) { table.retire(addresses.at(p)); }
  static bool collect(const void* p) {
    if (!table.collectRetired(addresses.at(p))) return false;
    addresses.erase(p); return true;
  }
  static void drain() { table.releaseBanks(3); }
  static void waitFor(unsigned sequence) {
    assert(rawReader && rawReader[0].words[0]==123);
    waited=sequence; rawReader=nullptr;
  }
};
using Pool = WorldOwner::Pool<Spans>;
using Quad = WorldOwner::Quad;
static WorldOwner::Key key(unsigned generation) {
  WorldOwner::Key k; k.generation=generation;k.programEpoch=1;
  k.vertices=66;k.run=66;k.primitive=0x101;k.depthScale=123;
  return k;
}
int main() {
  unsigned admissionControls=0;
  Pool pool(8);
  assert(pool.remainingQwords()==8);
  {
    Pool::Owner owner(pool);
    auto k=key(1);
    assert(owner.publish(k,std::vector<Quad>(4,Quad{{1,2,3,4}})));
    const auto* old=owner.admit(k); assert(old);
    const auto* oldData=old->data();
    const auto oldAddress=Spans::addresses.at(oldData);
    assert(Spans::table.borrow(oldAddress,4,3));
    auto next=key(2);
    assert(owner.publish(next,std::vector<Quad>(4,Quad{{5,6,7,8}})));
    assert(!owner.admit(k) && owner.admit(next));
    assert(!Spans::table.borrow(oldAddress,4,1));
    pool.collect(); assert(pool.usedQwords()==8 && oldData[0].words[0]==1);
    assert(pool.remainingQwords()==0);
    Spans::table.releaseBanks(1); pool.collect();
    assert(pool.usedQwords()==8 && oldData[0].words[0]==1);
    assert(!owner.publish(key(3),std::vector<Quad>(1)));
    assert(owner.admit(next));
    assert(!owner.publish(k,std::vector<Quad>(1)));
    Spans::table.releaseBanks(2); pool.collect();
    assert(pool.usedQwords()==4 && !Spans::addresses.count(oldData));
    for(unsigned i=0;i<13;++i) {
      auto bad=next;
      if(i==0)++bad.generation;
      if(i==1)++bad.programEpoch;
      if(i==2)++bad.programAddress;
      if(i==3)++bad.primitive;
      if(i==4)++bad.depthScale;
      if(i==5)++bad.vertices;
      if(i==6)++bad.run;
      if(i>=7 && i<11)++bad.content[i-7];
      if(i==11)bad.generation=0;
      if(i==12)++bad.bboxVersion;
      assert(!owner.admit(bad)); ++admissionControls;
    }
    const auto* current=owner.admit(next);
    assert(Spans::table.borrow(Spans::addresses.at(current->data()),4,2));
  }
  pool.collect(); assert(pool.usedQwords()==4);
  Spans::table.releaseBanks(2); pool.collect();
  assert(pool.usedQwords()==0 && Spans::addresses.empty());
  for(unsigned i=0;i<512;++i)assert(Spans::table.add(0x40000000+i*16,1));
  {
    Pool::Owner owner(pool);
    assert(!owner.publish(key(1),std::vector<Quad>(1)));
    assert(!owner.admit(key(1)) && pool.usedQwords()==0);
  }
  for(unsigned i=0;i<512;++i)Spans::table.remove(0x40000000+i*16);
  {
    WorldOwner::Attachment<Pool::Owner> source;
    source.reset(new Pool::Owner(pool));
    assert(source->publish(key(1),std::vector<Quad>(1)));
    WorldOwner::Attachment<Pool::Owner> copy(source);
    assert(!copy && source);
    copy.reset(new Pool::Owner(pool));
    assert(copy->publish(key(2),std::vector<Quad>(1)));
    copy=source; assert(!copy && source);
    source=source; assert(source);
  }
  pool.collect(); assert(pool.usedQwords()==0);
  {
    Pool::Owner owner(pool);
    assert(owner.publish(key(1),std::vector<Quad>(1,Quad{{123,0,0,0}})));
    Spans::rawReader=owner.admit(key(1))->data();
    owner.submitted(key(1),77,false);
    owner.invalidate(); assert(Spans::waited==77 && !Spans::rawReader);
  }
  pool.collect(); assert(pool.usedQwords()==0);
  {
    Pool::Owner owner(pool);
    assert(owner.publish(key(2),std::vector<Quad>(1)));
    owner.invalidate(); pool.collect();
    assert(!owner.publish(key(1),std::vector<Quad>(1)));
    assert(!owner.publish(key(2),std::vector<Quad>(1)));
    assert(owner.publish(key(3),std::vector<Quad>(1)));
  }
  pool.collect(); assert(pool.usedQwords()==0);
  {
    Pool::Owner owner(pool);
    std::vector<Quad> oversized(1); oversized.reserve(9);
    assert(!owner.publish(key(1),std::move(oversized)));
    assert(pool.usedQwords()==0);
  }
  std::cout << "PASS A31 compiled owner actual-bank-table two-banks=1 replacement=1"
            << " destruction=1 budget=1 saturation=1 capacity-budget=1 monotonic=1 raw-fence=1 copy-reset=1 admission-controls="
            << admissionControls << "\n";
}

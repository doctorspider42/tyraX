// TyraX2 arena ownership and bounded-fallback regression harness.
#include <cstdio>
#include <cstring>
#include "../vendor/tyra/engine/inc/renderer/core/paths/path1/frame_chain_arena.hpp"
namespace {
alignas(16) uint32_t payload[4] = {0, 0, 0, 0};
alignas(16) uint32_t storage[32] = {};
const uint32_t* resolve(uint32_t address, uint32_t qwords, void*) {
  if (address == 0x1000 && qwords == 1) return payload;
  if (address >= 0x8000 && address <= 0x8000 + sizeof(storage) &&
      qwords <= (0x8000 + sizeof(storage) - address) / 16)
    return storage + (address - 0x8000) / 4;
  return nullptr;
}
bool immutable(uint32_t address, uint32_t qwords) {
  return address == 0x1000 && qwords == 1;
}
// Engine-known memo controls execute the actual arena; callbacks are stable
// for each copy, and bank leases may only idempotently OR the selected bit.
unsigned memoReads=0,memoLeases=0,memoTraceCount=0,memoTrace[256];
bool memoBorrow=false;uint32_t memoBank=1;
Tyra::ImmutableSpanTable* memoReaders=nullptr;
const uint32_t* memoResolve(uint32_t address,uint32_t qwords,void* context) {
  ++memoReads;memoTrace[memoTraceCount++]=1;
  return resolve(address,qwords,context);
}
bool memoImmutable(uint32_t address,uint32_t qwords) {
  ++memoLeases;memoTrace[memoTraceCount++]=2;
  return memoBorrow && (memoReaders ? memoReaders->borrow(address,qwords,memoBank)
                                   : immutable(address,qwords));
}
void memoResetCounters(){memoReads=memoLeases=memoTraceCount=0;}

}
int main() {
  using namespace Tyra;
  unsigned checks = 0;
  auto expect = [&](bool value) { ++checks; if (!value) {
    std::fprintf(stderr, "Arena case %u failed\n", checks); return false;
  } return true; };
#define CHECK(x) do { if (!expect(x)) return 1; } while (0)
  alignas(16) uint32_t chain[8] = { (3u << 28) | 1, 0x1000, 0, 0,
                                   7u << 28, 0, 0, 0 };
  FrameChainArena arena(storage, sizeof(storage));
  auto a = arena.copy(chain, 2, resolve, nullptr, 0x8000);
  CHECK(a.chain == storage && a.bytes == 48 && arena.used() == 48);
  CHECK(storage[1] == 0x8020);
  CHECK(static_cast<bool>(Vif1ChainCheck::validate(a.chain, 2, resolve)));
  // The original packet AND referenced array can immediately change.
  payload[0] = 0xdeadbeef; chain[1] = 0x2000;
  CHECK(storage[8] == 0 && storage[1] == 0x8020);
  const uint32_t before = arena.used();
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  CHECK(arena.used() == before);
  chain[1] = 0x1000; payload[0] = 0;
  auto b = arena.copy(chain, 2, resolve, nullptr, 0x8000);
  CHECK(b.chain == storage + 12 && arena.used() == 96);
  CHECK(storage[13] == 0x8050);
  CHECK(static_cast<bool>(Vif1ChainCheck::validate(b.chain, 2, resolve)));
  CHECK(storage[1] == 0x8020); // later allocation never edits earlier readers
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0xfffffff0).chain);
  CHECK(arena.used() == 96);
  arena.reset(); CHECK(arena.used() == 0);
  FrameChainArena shortArena(storage, 47);
  uint32_t preserved[32]; std::memcpy(preserved, storage, sizeof(storage));
  CHECK(!shortArena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  CHECK(shortArena.used() == 0 && !std::memcmp(preserved,storage,sizeof(storage)));
  chain[4] = (7u << 28) | 1; // truncated inline END
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  chain[4] = 7u << 28; chain[0] = (2u << 28); // no jumping to another chain
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  chain[0] = (3u << 28) | 1; chain[1] = 0x1001;
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  chain[1] = 0x1000; chain[0] |= 0x80000000;
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  chain[0] &= ~0x80000000u; chain[0] |= 0x03ff0000;
  CHECK(arena.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  CHECK(!arena.copy(chain, 0, resolve, nullptr, 0x8000).chain);
  CHECK(!arena.copy(chain, 65537, resolve, nullptr, 0x8000).chain);
  CHECK(!arena.copy(chain, 2, resolve, nullptr, 0x8001).chain);
  CHECK(!arena.copy(chain + 1, 1, resolve, nullptr, 0x8000).chain);
  FrameChainArena missing(nullptr, 128);
  CHECK(!missing.copy(chain, 2, resolve, nullptr, 0x8000).chain);
  chain[0] = (3u << 28) | 1; chain[1] = 0x1000;
  FrameChainArena pinned(storage, 32);
  auto lease = pinned.copy(chain, 2, resolve, nullptr, 0x8000, immutable);
  CHECK(lease.chain == storage && lease.bytes == 32 && lease.borrowedBytes == 16);
  CHECK(storage[1] == 0x1000); // immutable owner, no redundant data copy
  CHECK(static_cast<bool>(Vif1ChainCheck::validate(lease.chain, 2, resolve)));
  pinned.reset(); chain[1] = 0x2000;
  CHECK(!pinned.copy(chain, 2, resolve, nullptr, 0x8000, immutable).chain);
  chain[1] = 0x1000;
  ImmutableSpanTable table;
  CHECK(table.add(0x3000,2) && table.add(0x1000,4) && table.add(0x2000,1));
  CHECK(table.contains(0x1010,3) && !table.contains(0x1010,4));
  CHECK(!table.contains(0x0ff0,1) && !table.contains(0x1040,1));
  CHECK(!table.add(0x1020,1) && !table.add(0x1000,4));
  CHECK(!table.add(0xfffffff0,2) && !table.add(0x4001,1));
  table.remove(0x2000);
  CHECK(!table.hasBase(0x2000) && table.contains(0x1000,4) && table.contains(0x3000,2));
  table.remove(0x5000); CHECK(table.hasBase(0x3000));
  ImmutableSpanTable full;
  for (unsigned i=512;i>0;--i) CHECK(full.add(0x1000+i*32,1));
  for (unsigned i=1;i<=512;++i) CHECK(full.contains(0x1000+i*32,1));
  CHECK(!full.add(0x1000,1));
  full.remove(0x1000+256*32); CHECK(full.add(0x1000,1));
  CHECK(!full.contains(0x1000+256*32,1) && full.contains(0x1000,1));
  ImmutableSpanTable readers;
  CHECK(readers.add(0x1000,4));
  CHECK(!readers.collectRetired(0x1000)); // an active owner is not collectible
  CHECK(!readers.borrow(0x1000,4,0) && !readers.borrow(0x1000,4,4));
  CHECK(readers.borrow(0x1000,4,1));
  CHECK(readers.borrow(0x1010,2,2));
  readers.retire(0x1000);
  CHECK(!readers.contains(0x1000,1) && !readers.borrow(0x1000,1,1));
  CHECK(!readers.reclaimable(0x1000) && !readers.collectRetired(0x1000));
  readers.releaseBanks(1);
  CHECK(!readers.reclaimable(0x1000) && !readers.collectRetired(0x1000));
  readers.releaseBanks(2);
  CHECK(readers.reclaimable(0x1000) && readers.collectRetired(0x1000));
  CHECK(!readers.hasBase(0x1000) && readers.collectRetired(0x4000));
  CHECK(readers.add(0x1000,4)); // address reuse only after every reader retired
  // Native sizing needs the number of root tags, including zero-length REFs
  // and END's inline payload. It must not count copied payload as more tags.
  alignas(16) uint32_t mixed[24] = {
    (1u << 28) | 1, 0, 0, 0, 0x12345678, 0, 0, 0,
    (3u << 28) | 1, 0x1000, 0, 0, 3u << 28, 0, 0, 0,
    (7u << 28) | 1, 0, 0, 0, 0xabcdef01, 0, 0, 0
  };
  alignas(16) uint32_t countedStorage[32] = {};
  FrameChainArena countedArena(countedStorage, sizeof(countedStorage));
  arena.reset();
  auto ordinary = arena.copy(mixed, 6, resolve, nullptr, 0x8000);
  auto counted = countedArena.copyCounted(mixed, 6, resolve, nullptr, 0x8000);
  CHECK(ordinary.chain && counted.chain && counted.records == 4);
  CHECK(ordinary.bytes == counted.bytes && ordinary.borrowedBytes == counted.borrowedBytes);
  CHECK(arena.used() == countedArena.used() &&
        !std::memcmp(storage, countedStorage, arena.used()));
  countedArena.reset();
  auto countedLease = countedArena.copyCounted(mixed, 6, resolve, nullptr, 0x8000, immutable);
  CHECK(countedLease.chain && countedLease.records == 4 &&
        countedLease.bytes == 96 && countedLease.borrowedBytes == 16);
  CHECK(countedStorage[9] == 0x1000);
  countedArena.reset();
  std::memcpy(preserved, countedStorage, sizeof(preserved));
  auto truncatedCount = countedArena.copyCounted(mixed, 5, resolve, nullptr, 0x8000);
  CHECK(!truncatedCount.chain && truncatedCount.records == 0 && countedArena.used() == 0);
  CHECK(!std::memcmp(preserved, countedStorage, sizeof(preserved)));
  FrameChainArena countedShort(countedStorage, 111);
  auto rejectedCount = countedShort.copyCounted(mixed, 6, resolve, nullptr, 0x8000);
  CHECK(!rejectedCount.chain && rejectedCount.records == 0 && countedShort.used() == 0);
  // REFE terminates the root stream and also contributes exactly one record.
  mixed[0] = 1; mixed[1] = 0x1000;
  auto refeCount = countedArena.copyCounted(mixed, 1, resolve, nullptr, 0x8000);
  CHECK(refeCount.chain && refeCount.records == 1 && refeCount.bytes == 32);
  alignas(16) uint32_t fuzz[64]; uint32_t rng = 7;
  for (unsigned trial = 0; trial < 10000; ++trial) {
    for (auto& word : fuzz) { rng = rng * 1664525u + 1013904223u; word = rng; }
    arena.reset();
    (void)arena.copy(fuzz, 16, resolve, nullptr, 0x8000);
  }
  // Counted preflight must preserve the ordinary copy, including failures.
  // Count is independently known while constructing headers; inline payload
  // words intentionally resemble END tags and must never be counted as roots.
  alignas(16) uint32_t tagChain[256], plainData[512], countedData[512];
  uint32_t random = 19;
  auto next = [&]() { random = random * 1664525u + 1013904223u; return random; };
  for (unsigned trial=0; trial<3000; ++trial) {
    const unsigned headers = 1 + next()%12;
    unsigned qw=0;
    for (unsigned h=0; h<headers; ++h) {
      const bool last=h+1==headers;
      const unsigned id=last ? (next()&1 ? 7 : 0) : (next()&1 ? 1 : 3);
      const unsigned n=next()%2;
      tagChain[qw*4]=(id<<28)|n;tagChain[qw*4+1]=0x1000;
      tagChain[qw*4+2]=tagChain[qw*4+3]=0;++qw;
      if(id==1||id==7) for(unsigned k=0;k<n;++k){
        for(unsigned j=0;j<4;++j)tagChain[qw*4+j]=7u<<28;++qw;
      }
    }
    const uint32_t capacity=(trial%7==0) ? qw*16 : sizeof(plainData);
    std::memset(plainData,0xa5,sizeof plainData);
    std::memset(countedData,0xa5,sizeof countedData);
    FrameChainArena plain(plainData,capacity),counted(countedData,capacity);
    auto x=plain.copy(tagChain,qw,resolve,nullptr,0x8000,trial%2?immutable:nullptr);
    auto y=counted.copyCounted(tagChain,qw,resolve,nullptr,0x8000,trial%2?immutable:nullptr);
    CHECK(bool(x.chain)==bool(y.chain));
    CHECK(x.bytes==y.bytes&&x.borrowedBytes==y.borrowedBytes&&plain.used()==counted.used());
    CHECK(!std::memcmp(plainData,countedData,sizeof plainData));
    CHECK(y.records==(y.chain?headers:0));
    counted.reset();tagChain[0]=2u<<28;
    auto failed=counted.copyCounted(tagChain,qw,resolve,nullptr,0x8000);
    CHECK(!failed.chain&&failed.records==0&&failed.bytes==0&&counted.used()==0);
  }

  // Compare the actual generic counted result and engine-known memo output.
  // Inline fake END payload is not a root tag. Empty refs do not use scratch.
  alignas(16) uint32_t memoChain[176],memoPlain[512],memoFast[512];
  const unsigned refCases[]={0,1,15,16,17,40};
  for(unsigned n:refCases) for(unsigned borrowed=0;borrowed<2;++borrowed) {
    std::memset(memoChain,0,sizeof memoChain);
    memoChain[0]=(1u<<28)|1;memoChain[4]=7u<<28;
    for(unsigned i=0;i<n;++i){memoChain[(i+2)*4]=(3u<<28)|1;memoChain[(i+2)*4+1]=0x1000;}
    // An empty ref has an intentionally invalid address: no callback needed.
    memoChain[(n+2)*4]=3u<<28;memoChain[(n+2)*4+1]=1;
    memoChain[(n+3)*4]=7u<<28;const unsigned qw=n+4;
    memoBorrow=borrowed!=0;memoReaders=nullptr;
    FrameChainArena baseline(memoPlain,sizeof memoPlain),fast(memoFast,sizeof memoFast);
    memoResetCounters();auto plain=baseline.copyCounted(memoChain,qw,memoResolve,nullptr,0x8000,memoImmutable);
    const unsigned oldReads=memoReads,oldLeases=memoLeases,traceCount=memoTraceCount;
    unsigned trace[256];std::memcpy(trace,memoTrace,sizeof trace);
    memoResetCounters();auto actual=fast.copyKnownRefsCounted<true>(memoChain,qw,memoResolve,nullptr,0x8000,memoImmutable);
    CHECK(plain.chain&&actual.chain&&plain.records==n+3&&actual.records==plain.records);
    CHECK(plain.bytes==actual.bytes&&plain.borrowedBytes==actual.borrowedBytes&&baseline.used()==fast.used());
    CHECK(!std::memcmp(memoPlain,memoFast,plain.bytes));
    const unsigned prefix=n<16?n:16;
    CHECK(oldLeases==2*n&&memoLeases==2*n-prefix);
    CHECK(oldReads==n*(borrowed?1:2)&&memoReads==oldReads-(borrowed?0:prefix));
    // Public generic copy and copyCounted retain their exact callback trace.
    baseline.reset();memoResetCounters();auto ordinary=baseline.copy(memoChain,qw,memoResolve,nullptr,0x8000,memoImmutable);
    CHECK(ordinary.chain&&ordinary.bytes==plain.bytes&&ordinary.borrowedBytes==plain.borrowedBytes);
    CHECK(traceCount==memoTraceCount&&!std::memcmp(trace,memoTrace,traceCount*sizeof(unsigned)));
    CHECK(!std::memcmp(memoPlain,memoFast,plain.bytes));
    fast.reset();FrameChainArena tooShort(memoFast,plain.bytes-1);
    auto fail=tooShort.copyKnownRefsCounted<true>(memoChain,qw,memoResolve,nullptr,0x8000,memoImmutable);
    CHECK(!fail.chain&&!fail.records&&!fail.bytes&&!tooShort.used());
    memoChain[0]=2u<<28;auto malformed=fast.copyKnownRefsCounted<true>(memoChain,qw,memoResolve,nullptr,0x8000,memoImmutable);
    CHECK(!malformed.chain&&!malformed.records&&!fast.used());
  }
  // Reader ownership survives retirement until BOTH banks release. A new copy
  // after retirement must not reuse an earlier per-copy borrowed classification.
  ImmutableSpanTable memoOwnership;CHECK(memoOwnership.add(0x1000,1));
  memoReaders=&memoOwnership;memoBorrow=true;chain[0]=(3u<<28)|1;chain[1]=0x1000;chain[4]=7u<<28;
  FrameChainArena retiredMemo(memoFast,sizeof memoFast);
  memoBank=1;auto bankOne=retiredMemo.copyKnownRefsCounted<true>(chain,2,memoResolve,nullptr,0x8000,memoImmutable);
  CHECK(bankOne.chain&&bankOne.borrowedBytes==16);
  retiredMemo.reset();memoBank=2;auto bankTwo=retiredMemo.copyKnownRefsCounted<true>(chain,2,memoResolve,nullptr,0x8000,memoImmutable);
  CHECK(bankTwo.chain&&bankTwo.borrowedBytes==16);
  memoOwnership.retire(0x1000);CHECK(!memoOwnership.collectRetired(0x1000));
  retiredMemo.reset();auto fresh=retiredMemo.copyKnownRefsCounted<true>(chain,2,memoResolve,nullptr,0x8000,memoImmutable);
  CHECK(fresh.chain&&!fresh.borrowedBytes&&fresh.bytes==48&&fresh.records==2);
  memoOwnership.releaseBanks(1);CHECK(!memoOwnership.collectRetired(0x1000));
  memoOwnership.releaseBanks(2);CHECK(memoOwnership.collectRetired(0x1000));memoReaders=nullptr;
  std::printf("Frame arena checks passed: %u cases + 10000 malformed streams\n", checks);
}

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
  alignas(16) uint32_t fuzz[64]; uint32_t rng = 7;
  for (unsigned trial = 0; trial < 10000; ++trial) {
    for (auto& word : fuzz) { rng = rng * 1664525u + 1013904223u; word = rng; }
    arena.reset();
    (void)arena.copy(fuzz, 16, resolve, nullptr, 0x8000);
  }
  std::printf("Frame arena checks passed: %u cases + 10000 malformed streams\n", checks);
}

// Host correctness harness for the TyraX2 pre-submit validator. No PS2 SDK.
// g++ -std=c++17 -Wall -Wextra -Werror tools/verify-vif1-chain.cpp -o CHECK
// CHECK must print the success line and return zero. See docs/tyrax2.md.
#include "../vendor/tyra/engine/inc/renderer/core/paths/path1/vif1_chain_check.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace Tyra::Vif1ChainCheck;
alignas(16) static uint32_t chain[4096];
alignas(16) static uint32_t ref[1024];
static unsigned checks = 0, resolves = 0;
static const uint32_t* resolve(uint32_t addr, uint32_t qw, void*) {
  ++resolves;
  return addr == 0x1000 && qw <= 256 ? ref : nullptr;
}
alignas(16) static uint32_t largeRef[65535 * 4];
static const uint32_t* resolveLarge(uint32_t addr, uint32_t qw, void*) {
  return addr == 0x1000 && qw <= 65535 ? largeRef : nullptr;
}
static void expect(Error wanted, uint32_t qw) {
  const auto r = validate(chain, qw, resolve);
  ++checks;
  if (r.error != wanted) {
    std::fprintf(stderr, "case %u: expected %u, got %u at tag %u command %u\n",
        checks, unsigned(wanted), unsigned(r.error), r.tag, r.command);
    std::exit(1);
  }
}
static void clear() { std::memset(chain, 0, sizeof(chain)); }
int main() {
  expect(Error::Empty, 0);
  if (validate(reinterpret_cast<char*>(chain) + 1, 1, resolve).error !=
      Error::Alignment) return 1;
  expect(Error::TooLarge, 65537);
  clear(); chain[0] = 7u << 28; expect(Error::None, 1);
  chain[0] = 1u << 28; expect(Error::MissingEnd, 1);
  chain[0] = (1u << 28) | 2; expect(Error::InlineBounds, 2);
  chain[0] = 7u << 28; expect(Error::TrailingData, 2);
  chain[0] |= 0x80000000; expect(Error::UnsupportedControl, 1);
  chain[0] = (7u << 28) | 0x04000000; expect(Error::UnsupportedControl, 1);
  chain[0] = (7u << 28) | 0x03ff0000; expect(Error::None, 1);
  for (auto id : {2u, 4u, 5u, 6u}) {
    chain[0] = id << 28; expect(Error::UnsupportedTag, 1);
  }
  clear(); chain[0] = (3u << 28) | 1; chain[1] = 0x1001;
  const auto before = resolves;
  expect(Error::Reference, 1);
  if (resolves != before) return 1; // fail before an unsafe pointer read
  chain[1] = 0x80001000; expect(Error::Reference, 1);
  chain[1] = 0x2000; expect(Error::Reference, 1);
  chain[1] = 0x1000; chain[2] = 0x6c010000;
  chain[4] = 7u << 28; expect(Error::None, 2);
  chain[0] = 1; expect(Error::None, 1); // REFE terminates after its data
  clear(); chain[0] = 7u << 28; chain[2] = 0x08000000;
  expect(Error::UnknownCommand, 1);
  chain[2] = 0x63010000; expect(Error::InvalidUnpack, 1);
  chain[2] = 0x6c010000; expect(Error::TruncatedPayload, 1);
  clear(); chain[0] = (7u << 28) | 1;
  chain[3] = 0x50000001; // HUD DIRECT, one GIF quadword
  chain[4] = 0xff123456; expect(Error::None, 2); // payload isn't a VIFcode
  chain[3] = 0x50000002; expect(Error::TruncatedPayload, 2);
  chain[3] = 0x50000000; expect(Error::TruncatedPayload, 2);
  clear(); chain[0] = (7u << 28) | 256;
  chain[3] = 0xec000000; // IRQ + V4_32, NUM=0 means 256 vectors
  expect(Error::None, 257);
  clear(); chain[0] = (7u << 28) | 1;
  chain[2] = 0x01000402; chain[3] = 0x6c040000; // CL=2, WL=4
  expect(Error::TruncatedPayload, 2); // 2 input vectors = 2 qwords
  chain[0] = (7u << 28) | 2; expect(Error::None, 3);
  chain[2] = 0x01000204; chain[0] = (7u << 28) | 4;
  expect(Error::None, 5); // skip mode still consumes all four input vectors
  clear(); chain[0] = (7u << 28) | 1;
  chain[3] = 0x6f080000; expect(Error::None, 2); // V4_5 is 16 bits/vector
  chain[3] = 0x4a020000; expect(Error::None, 2); // MPG 2 instructions
  chain[3] = 0x30000000; expect(Error::None, 2); // STROW 4 words
  clear();
  for (unsigned i = 0; i < 17; ++i) {
    chain[i*4] = (3u << 28) | 65535; chain[i*4+1] = 0x1000;
  }
  chain[17*4] = 7u << 28;
  if (validate(chain, 18, resolveLarge).error != Error::PayloadBudget) return 1;
  ++checks;
  // Random malformed streams exercise bounded termination and address checks.
  uint32_t rng = 1;
  for (unsigned trial = 0; trial < 10000; ++trial) {
    for (unsigned i = 0; i < 64; ++i) {
      rng = rng * 1664525u + 1013904223u; chain[i] = rng;
    }
    (void)validate(chain, 16, resolve);
  }
  std::printf("VIF1 chain checks passed: %u cases + 10000 malformed streams\n",checks);
}

#include <cassert>
#include <cstdio>
#include <vector>
#include "renderer/3d/pipeline/static/core/stapip_baked_body.hpp"

int main() {
  uint32_t seed = 917;
  auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
  unsigned checks = 0;
  for (unsigned trial = 0; trial < 10000; ++trial) {
    std::vector<uint32_t> payload(512), chain(12), baked(12);
    for (auto& w : payload) w = next();
    for (unsigned i = 0; i < 12; ++i) chain[i] = baked[i] = next();
    chain[0] = 0x10000002; chain[1] = baked[0] = baked[1] = 0;
    for (unsigned t = 0, count = 1 + next()%8; t < count; ++t) {
      unsigned n = next()%24, id = next()%2 ? 1 : 3;
      uint32_t first = next(), second = next();
      chain.insert(chain.end(), {(id<<28)|n, 0x1000, first, second});
      baked.insert(baked.end(), {0, 0, first, second});
      if (id == 1) chain.insert(chain.end(),payload.begin(),payload.begin()+n*4);
      baked.insert(baked.end(),payload.begin(),payload.begin()+n*4);
    }
    chain.insert(chain.end(),{0x10000000,0,0x13000000,0x14000000});
    baked.insert(baked.end(),{0,0,0x13000000,0x17000000});
    baked[7] ^= 0x8000;
    auto resolve = [&](uint32_t address,uint32_t n) -> const uint32_t* {
      return address==0x1000 && n<=payload.size()/4 ? payload.data() : nullptr;
    };
    auto equal = [&]() { return Tyra::BakedBody::matches(chain.data(),chain.size()/4,baked.data(),baked.size()/4,0x8000,resolve); };
    assert(equal()); ++checks;
    unsigned bad = 12 + next()%(baked.size()-16);
    baked[bad] ^= 1; assert(!equal()); ++checks; baked[bad] ^= 1;
    baked[7] ^= 1; assert(!equal()); ++checks; baked[7] ^= 1;
    chain[0] = 0x10000003; assert(!equal()); ++checks; chain[0] = 0x10000002;
    assert(!Tyra::BakedBody::matches(chain.data(),chain.size()/4-1,baked.data(),baked.size()/4,0x8000,resolve)); ++checks;
  }
  std::printf("Partial baked body checks passed: %u\n",checks);
}

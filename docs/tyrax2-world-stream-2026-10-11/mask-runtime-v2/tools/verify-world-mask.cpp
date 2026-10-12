#define WORLD_BODY_HOST
#include "tyrax2-world-mask.hpp"
#include <cassert>
#include <iostream>
int main() {
  using Quad = WorldStripPlan::Quad;
  std::vector<Quad> ordinary{{{0,0,0x01000101,0x6c02c000}},
                            {{1,2,3,66U | (1U << 10)}},{{4,5,6,7}}};
  auto full = ordinary;
  full[1].words[3] |= 63U << 10;
  assert(WorldMask::sameBody(ordinary, full));
  unsigned rejected = 0;
  for (unsigned q = 0; q < full.size(); ++q)
    for (unsigned w = 0; w < 4; ++w) {
      auto bad = full; bad[q].words[w] ^= 1;
      assert(!WorldMask::sameBody(ordinary, bad)); ++rejected;
    }
  auto bad = full; bad[1].words[3] ^= 1U << 10;
  assert(!WorldMask::sameBody(ordinary,bad)); ++rejected;
  bad = full; bad.pop_back();
  assert(!WorldMask::sameBody(ordinary,bad)); ++rejected;
  auto badOriginal = ordinary; badOriginal[0].words[3] ^= 1U << 14;
  assert(!WorldMask::sameBody(badOriginal,full)); ++rejected;
  std::cout << "PASS A31 all-plane word contract positive=1 controls="
            << rejected << "\n";
}

#pragma once
#ifdef WORLD_BODY_HOST
#include "tyrax2-world-strip-plan.hpp"
#else
#include "debug/world_strip_plan.hpp"
#endif

namespace WorldMask {
inline unsigned changed = 0;
// The actual SDK writer emits scale/count as the first CNT payload quadword.
inline bool sameBody(const std::vector<WorldStripPlan::Quad>& ordinary,
                     const std::vector<WorldStripPlan::Quad>& full) {
  constexpr unsigned mask = 63U << 10;
  if (ordinary.size() < 3 || ordinary.size() != full.size() ||
      ordinary[0].words[2] != 0x01000101U ||
      ordinary[0].words[3] != 0x6c02c000U ||
      (full[1].words[3] & mask) != mask) return false;
  for (unsigned q = 0; q < ordinary.size(); ++q)
    for (unsigned w = 0; w < 4; ++w) {
      const unsigned allowed = q == 1 && w == 3 ? mask : 0;
      if (((ordinary[q].words[w] ^ full[q].words[w]) & ~allowed) != 0)
        return false;
    }
  return true;
}
}

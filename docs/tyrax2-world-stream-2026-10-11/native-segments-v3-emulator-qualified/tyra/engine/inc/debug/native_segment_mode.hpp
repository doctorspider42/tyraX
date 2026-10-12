#pragma once
#include "debug/native_segments.hpp"
#include "debug/debug.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
namespace NativeSegments {
inline bool enabled = false, earlyThisFrame = false;
inline unsigned earlyPublishes = 0, gasPublishes = 0, gasFrames = 0;
inline bool audit = false;
inline const void* auditFirst = nullptr;
inline unsigned auditWindows = 0, auditChecks = 0;
inline std::vector<uint32_t> auditBytes;
inline void startAudit() {
  audit = true; auditFirst = nullptr; auditWindows = auditChecks = 0;
  auditBytes.clear();
}
inline void auditPrefix(const void* data, unsigned qwords) {
  if (!audit) return;
  if (auditFirst) {
    TYRA_ASSERT(reinterpret_cast<uintptr_t>(data) >=
        reinterpret_cast<uintptr_t>(auditFirst) + auditBytes.size()*4,
        "Native audit windows overlap");
    TYRA_ASSERT(!std::memcmp(auditFirst, auditBytes.data(), auditBytes.size()*4),
                "Native submitted window changed during later preparation");
    ++auditChecks;
  } else {
    auditFirst = data;
    const auto* words = static_cast<const uint32_t*>(data);
    auditBytes.assign(words, words + qwords*4);
  }
  ++auditWindows;
}
inline void finishAudit(unsigned phase, unsigned arm) {
  TYRA_ASSERT(auditWindows >= (enabled ? 2u : 1u), "Native audit publication missing");
  TYRA_ASSERT(!enabled || auditChecks, "Native prior window protection untested");
  printf("NATIVESEGMENTAUDIT phase=%u arm=%u enabled=%u windows=%u protected=%u equal=1\n",
         phase, arm, unsigned(enabled), auditWindows, auditChecks);
  audit = false;
}
inline void reset() { earlyPublishes = gasPublishes = gasFrames = 0; }
inline void witness(unsigned phase, int arm = -1, unsigned baseline = 0) {
  printf("NATIVESEGMENT phase=%u arm=%d enabled=%u gasFrames=%u gasEarly=%u heldEarly=%u\n",
         phase, arm, unsigned(enabled), gasFrames, gasPublishes,
         arm < 0 ? 0 : earlyPublishes - baseline);
}
}

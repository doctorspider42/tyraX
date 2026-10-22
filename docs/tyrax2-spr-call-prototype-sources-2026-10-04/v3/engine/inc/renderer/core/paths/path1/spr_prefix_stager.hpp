/* Modified by TyraX: private finalized-prefix SPR experiment, Apache 2.0. */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Tyra {
// The backend owns an exclusively reserved 16 KiB SPR window and channel 8.
// Destination storage belongs to the existing frame bank. Completion here
// releases only the SPR window, never the destination's VIF1 reader lease.
struct SprPrefixStats {
  uint32_t prefixes = 0, qwords = 0, chunks = 0, completions = 0;
  uint32_t refused = 0, guardFailures = 0;
};
class SprPrefixStager {
 public:
  static constexpr uint32_t windowQwords = 1024;
  template<class Backend>
  static bool stage(const void* source, void* destination, uint32_t qwords,
                    uint32_t capacity, Backend& backend, SprPrefixStats& stats) {
    if (!source || !destination || !qwords || qwords > capacity ||
        (reinterpret_cast<uintptr_t>(source) & 15) ||
        (reinterpret_cast<uintptr_t>(destination) & 15) ||
        qwords > UINT32_MAX / 16 || !backend.acquire()) {
      ++stats.refused; return false;
    }
    const auto* src = static_cast<const uint8_t*>(source);
    auto* dst = static_cast<uint8_t*>(destination);
    for (uint32_t at = 0; at < qwords;) {
      const uint32_t chunk = qwords - at < windowQwords ?
          qwords - at : windowQwords;
      // Backend transfer is synchronous and must stop diagnostically on timeout:
      // returning while DMA is active would permit unsafe SPR reuse/fallback.
      backend.transfer(src + at * 16, dst + at * 16, chunk);
      ++stats.chunks; ++stats.completions; at += chunk;
    }
    backend.release(); ++stats.prefixes; stats.qwords += qwords;
    return true;
  }
};
} // namespace Tyra

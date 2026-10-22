#pragma once
#include <cstdint>

namespace CommandTape {
// The native capture accepts only CNT/REF/END. Payload validation is separate.
inline uint32_t closureAllocation(const uint32_t* root, uint32_t qwords,
                                  uint32_t limit) {
  if (!root || !qwords || qwords > 65536 || qwords > limit / 16) return 0;
  uint32_t needed = qwords * 16;
  for (uint32_t at = 0; at < qwords;) {
    const uint32_t* tag = root + at * 4;
    const uint32_t id = (tag[0] >> 28) & 7, n = tag[0] & 65535;
    if (tag[0] & 0x8c000000u) return 0;
    if (id == 3) {
      if ((tag[1] & 0x8000000fu) || n > (limit - needed) / 16) return 0;
      needed += n * 16;
      ++at;
    } else if (id == 1 || id == 7) {
      if (n > qwords - at - 1) return 0;
      at += n + 1;
      if (id == 7) {
        if (at != qwords || needed > UINT32_MAX - 63) return 0;
        const uint32_t aligned = (needed + 63) & ~uint32_t(63);
        return aligned <= limit ? aligned : 0;
      }
    } else return 0;
  }
  return 0;
}
}

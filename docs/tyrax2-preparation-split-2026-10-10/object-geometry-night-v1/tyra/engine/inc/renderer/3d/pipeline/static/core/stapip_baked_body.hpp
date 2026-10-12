/* Modified by TyraX: actual-writer comparison for partial baked replay.
 * Licensed under Apache 2.0. */
#pragma once
#include <stdint.h>
#include <string.h>

namespace Tyra {
namespace BakedBody {
// Ignore only the regenerated state bit and final program kick. Geometry,
// primitive, counts and all intervening VIF commands must match exactly.
template<class Resolver>
bool matches(const uint32_t* chain, uint32_t chainQw,
             const uint32_t* cached, uint32_t cachedQw,
             uint32_t stateMask, Resolver resolve) {
  if (!chain || !cached || chainQw < 4 || cachedQw < 4 ||
      chain[0] != 0x10000002 || cached[0] || cached[1] ||
      chain[(chainQw - 1) * 4] != 0x10000000) return false;
  for (uint32_t i = 2; i < 12; ++i) {
    const uint32_t mask = i == 7 ? ~stateMask : ~0u;
    if ((chain[i] & mask) != (cached[i] & mask)) return false;
  }
  uint32_t at = 3, out = 3;
  while (at < chainQw - 1) {
    const auto* tag = chain + at * 4;
    const uint32_t n = tag[0] & 65535, id = (tag[0] >> 28) & 7;
    if ((id != 1 && id != 3) || out >= cachedQw - 1 ||
        n > cachedQw - 2 - out) return false;
    const uint32_t header[] = {0, 0, tag[2], tag[3]};
    if (memcmp(header, cached + out * 4, 16)) return false;
    const uint32_t* data = nullptr;
    if (id == 1) {
      if (n > chainQw - 2 - at) return false;
      data = tag + 4;
      at += 1 + n;
    } else {
      if (tag[1] & 0x8000000f) return false;
      data = resolve(tag[1], n);
      ++at;
    }
    if (n && (!data || memcmp(data, cached + (out + 1) * 4, n * 16))) return false;
    out += 1 + n;
  }
  return at == chainQw - 1 && out == cachedQw - 1;
}
}  // namespace BakedBody
}  // namespace Tyra

#pragma once
#ifdef WORLD_BODY_HOST
#include "tyrax2-world-strip-plan.hpp"
#else
#include "debug/world_strip_plan.hpp"
#endif
#include <cstring>
namespace WorldBody {
using Quad = WorldStripPlan::Quad;
using Resolver = const Quad* (*)(unsigned, unsigned, void*);
inline unsigned checks = 0, words = 0;
inline std::vector<std::vector<Quad>> storage;
inline void reset() { storage.clear(); checks = words = 0; }

// Consume an actual SDK-written CNT/REF fragment, not a reconstructed format.
inline std::vector<Quad> flatten(const void* chain, unsigned count,
                                 Resolver resolve, void* context = nullptr) {
  if (!chain || !count || count > 65535)
    throw std::invalid_argument("A31 invalid command fragment");
  std::vector<Quad> out;
  const auto* bytes = static_cast<const unsigned char*>(chain);
  for (unsigned at = 0; at < count;) {
    Quad record;
    std::memcpy(&record, bytes + 16 * at++, sizeof(record));
    const auto& tag = record.words;
    const unsigned size = tag[0] & 65535, id = (tag[0] >> 28) & 7;
    if ((tag[0] & 0x8fff0000U) || (tag[1] & 0x8000000fU))
      throw std::invalid_argument("A31 unsupported tag flags/alignment");
    const Quad* payload = nullptr;
    if (id == 1) {
      if (tag[1] || size > count - at)
        throw std::invalid_argument("A31 invalid inline payload");
      payload = reinterpret_cast<const Quad*>(bytes + 16 * at); at += size;
    } else if (id == 3) {
      if (size && (!resolve || !(payload = resolve(tag[1], size, context))))
        throw std::invalid_argument("A31 unreadable REF");
    } else throw std::invalid_argument("A31 unsupported fragment tag");
    out.push_back(Quad{{0, 0, tag[2], tag[3]}});
    if (size) {
      const auto offset = out.size();
      out.resize(offset + size);
      std::memcpy(out.data() + offset, payload, size * sizeof(Quad));
    }
  }
  if (out.empty() || out.size() > 65535)
    throw std::invalid_argument("A31 compiled body exceeds one REF");
  return out;
}
}

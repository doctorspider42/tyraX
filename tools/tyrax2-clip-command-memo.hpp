#pragma once
#include <cstdint>
#include <cstring>

namespace ClipCommandMemo {
struct Key {
  uint32_t vertices, sts, colors, count, single;
};
struct Entry {
  Key key{};
  alignas(16) uint32_t words[12]{};
  uint32_t qw = 0;
};
struct Result { bool hit, equal; uint32_t qw; bool paddingDifferent = false; };
struct Cache {
  Entry entries[128]{};
  template<class Writer>
  Result emit(const Key& key, uint32_t* out, bool verify, Writer writer) {
    const uint32_t hash = ((key.vertices >> 4) ^ (key.vertices >> 11) ^
        (key.sts >> 7) ^ (key.colors >> 9) ^ key.count ^ key.single) & 127;
    auto& e = entries[hash];
    const bool hit = e.qw && std::memcmp(&key, &e.key, sizeof(Key)) == 0;
    if (hit && !verify) {
      for (uint32_t q=0; q<e.qw; ++q) {
        // PS2SDK's bitfield writer leaves DMA-tag bits 16..25 untouched.
        const uint32_t padding = out[q*4] & 0x03FF0000u;
        std::memcpy(out+q*4, e.words+q*4, 16);
        out[q*4] = (out[q*4] & ~0x03FF0000u) | padding;
      }
      return {true, true, e.qw};
    }
    const uint32_t qw = writer();
    if (!qw || qw > 3) return {hit, false, qw};
    for (uint32_t q=0; q<qw; ++q)
      if ((out[q*4] & 0x70000000u) != 0x30000000u) return {hit, false, qw};
    if (hit) {
      bool equal = qw == e.qw, paddingDifferent = false;
      alignas(16) uint32_t replay[12]{};
      std::memcpy(replay,e.words,e.qw*16);
      for (uint32_t q=0; q<qw; ++q) {
        paddingDifferent |= ((replay[q*4]^out[q*4]) & 0x03FF0000u) != 0;
        replay[q*4] = (replay[q*4] & ~0x03FF0000u) | (out[q*4] & 0x03FF0000u);
      }
      equal &= std::memcmp(out,replay,qw*16)==0;
      if (equal) std::memcpy(out,replay,qw*16);
      return {true,equal,qw,paddingDifferent};
    }
    e.key = key;
    e.qw = qw;
    std::memcpy(e.words, out, qw * 16);
    return {false, true, qw};
  }
};
}

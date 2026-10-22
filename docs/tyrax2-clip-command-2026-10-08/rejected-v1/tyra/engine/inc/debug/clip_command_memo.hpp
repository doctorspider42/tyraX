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
struct Result { bool hit, equal; uint32_t qw; };
struct Cache {
  Entry entries[128]{};
  template<class Writer>
  Result emit(const Key& key, uint32_t* out, bool verify, Writer writer) {
    const uint32_t hash = ((key.vertices >> 4) ^ (key.vertices >> 11) ^
        (key.sts >> 7) ^ (key.colors >> 9) ^ key.count ^ key.single) & 127;
    auto& e = entries[hash];
    const bool hit = e.qw && std::memcmp(&key, &e.key, sizeof(Key)) == 0;
    if (hit && !verify) {
      std::memcpy(out, e.words, e.qw * 16);
      return {true, true, e.qw};
    }
    const uint32_t qw = writer();
    if (!qw || qw > 3) return {hit, false, qw};
    if (hit) {
      const bool equal = qw == e.qw && std::memcmp(out, e.words, qw * 16) == 0;
      if (equal) std::memcpy(out, e.words, qw * 16);
      return {true, equal, qw};
    }
    e.key = key;
    e.qw = qw;
    std::memcpy(e.words, out, qw * 16);
    return {false, true, qw};
  }
};
}

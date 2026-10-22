// Private A19 qualification. The original gather runs before this oracle.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace ListJoin {
inline bool active = false;
inline unsigned calls = 0, accepted = 0, bytes = 0;
inline void reset(bool enabled) { active = enabled; calls = accepted = bytes = 0; }

template <class Package>
bool describe(const Package* const* parts, unsigned count, Package& joined) {
  if (count != 2 && count != 3) return false;
  const auto& first = *parts[0];
  const auto* bag = first.bag;
  if (!bag || bag->stripped || !bag->color || !first.vertices) return false;
  unsigned size = 0, mask = 0;
  const void* base[] = {first.vertices, first.sts, first.colors, first.normals};
  const bool used[] = {true, bag->texture != nullptr,
                      bag->color->many != nullptr, bag->lighting != nullptr};
  for (unsigned i = 0; i < count; ++i) {
    const auto& part = *parts[i];
    if (part.bag != bag || part.size == 0 || part.size % 3) return false;
    const void* data[] = {part.vertices, part.sts, part.colors, part.normals};
    for (unsigned s = 0; s < 4; ++s) {
      if (used[s] && (!base[s] || !data[s] ||
          reinterpret_cast<uintptr_t>(data[s]) !=
          reinterpret_cast<uintptr_t>(base[s]) + size * 16u)) return false;
    }
    size += part.size;
    mask |= part.clipPlaneMask;
  }
  if (size > 65535u) return false;
  joined = first;
  joined.size = size;
  joined.clipPlaneMask = mask;
  return true;
}

template <class Buffer, class Package>
void checkAndApply(Buffer& buffer, const Package* const* parts, unsigned count) {
  if (!active) return;
  ++calls;
  Package joined;
  if (!describe(parts, count, joined)) return;
  const auto* bag = joined.bag;
  const void* copied[] = {buffer.vertices, buffer.sts, buffer.colors, buffer.normals};
  const void* original[] = {joined.vertices, joined.sts, joined.colors, joined.normals};
  const bool used[] = {true, bag->texture != nullptr,
                      bag->color->many != nullptr, bag->lighting != nullptr};
  if (buffer.size != joined.size || buffer.bag != bag || buffer.stripped ||
      buffer.clipPlaneMask != joined.clipPlaneMask) std::abort();
  for (unsigned s = 0; s < 4; ++s) {
    if (!used[s]) continue;
    if (std::memcmp(copied[s], original[s], joined.size * 16u)) std::abort();
    bytes += joined.size * 16u;
  }
  // fillByPointer keeps the ordinary queue's snapshot/lease protection.
  buffer.fillByPointer(joined);
  ++accepted;
}
}

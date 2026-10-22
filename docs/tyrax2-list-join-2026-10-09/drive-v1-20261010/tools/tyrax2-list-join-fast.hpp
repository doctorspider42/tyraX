// A22 private intervention: no gather for an already contiguous list group.
#pragma once
#include "list_join.hpp"
namespace FastJoin {
inline bool active = false, oracle = false;
inline bool mode(unsigned stage) { return stage == 2 || stage == 4 || stage == 6 || stage == 8; }
template<class Buffer, class Package>
bool apply(Buffer& buffer, const Package* const* parts, unsigned count) {
  if (!active || oracle) return false;
  Package joined;
  if (!ListJoin::describe(parts, count, joined)) return false;
  buffer.fillByPointer(joined);
  return true;
}
}

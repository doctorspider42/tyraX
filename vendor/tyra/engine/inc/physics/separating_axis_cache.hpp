// Added by TyraX: separating-axis hints, rechecked against current geometry.
#pragma once
#include "convex_collision.hpp"
#include <algorithm>
#include <vector>
namespace Tyra {
class SeparatingAxisHistory {
  struct Entry {
    int a, b;
    CollisionV3 axis;
  };
  std::vector<Entry> previous_, current_;
  unsigned generation_ = ~0u;

public:
  void begin(unsigned generation) {
    previous_.swap(current_);
    current_.clear();
    if (generation_ != generation)
      previous_.clear();
    generation_ = generation;
    previous_.erase(std::remove_if(previous_.begin(), previous_.end(),
                                   [](const Entry &e) {
                                     return cvDot(e.axis, e.axis) < 1e-10f;
                                   }),
                    previous_.end());
    std::sort(previous_.begin(), previous_.end(),
              [](const Entry &a, const Entry &b) {
                return a.a < b.a || (a.a == b.a && a.b < b.b);
              });
  }
  CollisionV3 &acquire(int a, int b) {
    const Entry key{a, b, {}};
    auto it = std::lower_bound(previous_.begin(), previous_.end(), key,
                               [](const Entry &x, const Entry &y) {
                                 return x.a < y.a || (x.a == y.a && x.b < y.b);
                               });
    CollisionV3 axis{};
    if (it != previous_.end() && it->a == a && it->b == b)
      axis = it->axis;
    current_.push_back({a, b, axis});
    return current_.back().axis;
  }
};
} // namespace Tyra

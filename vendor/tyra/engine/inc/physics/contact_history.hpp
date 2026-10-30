// Added by TyraX: portable contact history and deterministic dynamic islands.
#pragma once
#include "convex_collision.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
#ifndef TYRA_PHYSICS_COHERENCE
#define TYRA_PHYSICS_COHERENCE 1
#endif
#ifndef TYRA_PHYSICS_WORLD_INDEX
// Cached static world and current movers; retain exact tests and contact order.
#define TYRA_PHYSICS_WORLD_INDEX 1
#endif
#ifndef TYRA_PHYSICS_WARM_START
#define TYRA_PHYSICS_WARM_START 0
#endif
#ifndef TYRA_PHYSICS_ISLANDS
#define TYRA_PHYSICS_ISLANDS 0
#endif
namespace Tyra {
struct ContactMemory {
  int a, b;
  unsigned revisionA, revisionB, state;
  CollisionV3 anchorA, anchorB, normal, friction;
  float normalImpulse;
};
// Only the immediately preceding simulation step is eligible. The hash index
// grows with actual contacts; collisions chain rather than dropping records.
class ContactHistory {
  std::vector<ContactMemory> previous_, current_;
  std::vector<int> buckets_, next_;
  std::vector<unsigned char> used_;
  unsigned generation_ = ~0u;
  float dt_ = 0;
  static unsigned hash(int a, int b) {
    unsigned h = unsigned(a) * 73856093u ^ unsigned(b) * 19349663u;
    return h ^ (h >> 16);
  }

public:
  unsigned hits = 0;
  void begin(unsigned generation, float dt) {
    previous_.swap(current_);
    current_.clear();
    // Per-frame velocity units require a compatible timestep. Do not seed
    // an old gravity impulse across a pacing change, rewind or scene reload.
    if (generation != generation_ || dt <= 0 || dt_ <= 0 ||
        std::fabs(dt - dt_) > dt_ * .05f)
      previous_.clear();
    generation_ = generation;
    dt_ = dt;
    size_t n = 16;
    while (n < previous_.size() * 2)
      n *= 2;
    buckets_.assign(n, -1);
    next_.resize(previous_.size());
    used_.assign(previous_.size(), 0);
    for (size_t i = 0; i < previous_.size(); ++i) {
      unsigned slot = hash(previous_[i].a, previous_[i].b) & unsigned(n - 1);
      next_[i] = buckets_[slot];
      buckets_[slot] = int(i);
    }
    hits = 0;
  }
  int add(ContactMemory c, bool allowWarm) {
    float best = .03f * .03f * 2;
    int pick = -1;
    if (allowWarm && !buckets_.empty()) {
      unsigned slot = hash(c.a, c.b) & unsigned(buckets_.size() - 1);
      for (int i = buckets_[slot]; i >= 0; i = next_[i]) {
        const auto &p = previous_[i];
        if (used_[i] || p.a != c.a || p.b != c.b ||
            p.revisionA != c.revisionA || p.revisionB != c.revisionB ||
            p.state != c.state || cvDot(p.normal, c.normal) < .98f)
          continue;
        const auto da = cvSub(p.anchorA, c.anchorA);
        const auto db = cvSub(p.anchorB, c.anchorB);
        const float aa = cvDot(da, da), bb = cvDot(db, db);
        if (aa > .03f * .03f || bb > .03f * .03f)
          continue;
        if (aa + bb < best)
          best = aa + bb, pick = i;
      }
    }
    c.normalImpulse = 0;
    c.friction = {};
    if (pick >= 0) {
      used_[pick] = 1;
      const auto &p = previous_[pick];
      c.normalImpulse = std::max(0.f, p.normalImpulse);
      c.friction =
          cvSub(p.friction, cvMul(c.normal, cvDot(p.friction, c.normal)));
      ++hits;
    }
    current_.push_back(c);
    return int(current_.size() - 1);
  }
  ContactMemory &operator[](int i) { return current_[i]; }
  const ContactMemory &operator[](int i) const { return current_[i]; }
};

class ContactIslands {
  std::vector<int> parent_, counts_, offsets_, cursor_;

public:
  std::vector<int> order;
  unsigned count = 0;
  void begin(int bodies) {
    parent_.resize(bodies);
    for (int i = 0; i < bodies; ++i)
      parent_[i] = i;
  }
  int root(int a) {
    while (parent_[a] != a) {
      parent_[a] = parent_[parent_[a]];
      a = parent_[a];
    }
    return a;
  }
  // Only dynamic bodies are linked by the caller. A shared floor or sleeping
  // immovable partner must not merge otherwise independent components.
  void link(int a, int b) {
    a = root(a);
    b = root(b);
    if (a != b)
      parent_[std::max(a, b)] = std::min(a, b);
  }
  template <class Contacts> void arrange(const Contacts &contacts) {
    counts_.assign(parent_.size(), 0);
    offsets_.resize(parent_.size());
    cursor_.resize(parent_.size());
    for (const auto &c : contacts)
      ++counts_[root(c.a)];
    int offset = 0;
    count = 0;
    for (size_t i = 0; i < parent_.size(); ++i) {
      offsets_[i] = cursor_[i] = offset;
      offset += counts_[i];
      if (counts_[i])
        ++count;
    }
    order.resize(contacts.size());
    for (size_t i = 0; i < contacts.size(); ++i)
      order[cursor_[root(contacts[i].a)]++] = int(i);
  }
};
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

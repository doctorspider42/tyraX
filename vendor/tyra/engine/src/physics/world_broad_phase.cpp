// Added by TyraX: retain world index geometry until its inputs change.
#include "physics/world_broad_phase.hpp"
#include <algorithm>

namespace Tyra {
namespace {
bool overlaps(const BroadBox& a, const BroadBox& b) {
  for (int k = 0; k < 3; ++k)
    if (a.mn[k] > b.mx[k] || a.mx[k] < b.mn[k]) return false;
  return true;
}
constexpr size_t kLinearMoving = 8;
}
void WorldBroadPhase::clear() {
  staticIndex_.clear(); movingIndex_.clear();
  staticBoxes_.clear(); roles_.clear(); moving_.clear(); hits_.clear();
  initialized_ = false; staticDirty_ = movingDirty_ = true;
}
void WorldBroadPhase::begin(int count, BroadPhase::Kind kind) {
  if (count < 0) count = 0;
  if (staticIndex_.kind() != kind) {
    staticIndex_ = BroadPhase(kind);
    staticDirty_ = true;
  }
  if (roles_.size() != size_t(count)) {
    roles_.assign(size_t(count), Absent);
    staticBoxes_.resize(size_t(count));
    staticDirty_ = true;
  }
  moving_.clear();
  movingDirty_ = true;
  initialized_ = true;
}
void WorldBroadPhase::queryBox(const BroadBox& area, std::vector<int>& out) {
  if (!initialized_) { out.clear(); return; }
  if (staticDirty_) {
    staticIndex_.begin(int(roles_.size()));
    for (size_t i = 0; i < roles_.size(); ++i)
      if (roles_[i] == Static) staticIndex_.set(int(i), staticBoxes_[i], false);
    staticIndex_.commit();
    staticDirty_ = false;
    ++staticBuilds_;
  }
  staticIndex_.queryBox(area, out);
  const size_t staticHits = out.size();
  if (moving_.size() <= kLinearMoving) {
    for (const auto& item : moving_)
      if (overlaps(area, item.box)) out.push_back(item.index);
  } else {
    if (movingDirty_) {
      movingIndex_.begin(int(moving_.size()));
      for (size_t i = 0; i < moving_.size(); ++i)
        movingIndex_.set(int(i), moving_[i].box, false);
      movingIndex_.commit();
      movingDirty_ = false;
      ++movingBuilds_;
    }
    movingIndex_.queryBox(area, hits_);
    for (int i : hits_) out.push_back(moving_[size_t(i)].index);
  }
  // Moving submissions need not be ordered; static/dynamic roles are disjoint.
  if (out.size() != staticHits) std::sort(out.begin(), out.end());
}
}  // namespace Tyra

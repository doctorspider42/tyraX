// Added by TyraX: cached world-only broad phase with compact moving colliders.
#pragma once
#include "broad_phase.hpp"

namespace Tyra {
/** A subset index for rigid-body/world contacts. Submit every scene slot once
 *  after begin(), using set() for world solids and remove() for all other slots.
 *  changedBox reports a recomputed box from the authoritative transform cache.
 *  Static boxes are published only on a change; moving boxes are current on
 *  every refresh. Queries retain every overlapping slot in ascending order. */
class WorldBroadPhase {
 public:
  bool initialized() const { return initialized_; }
  void clear();
  void begin(int count, BroadPhase::Kind kind);
  void set(int index, const BroadBox& box, bool moving, bool changedBox) {
    if (index < 0 || std::size_t(index) >= roles_.size()) return;
    auto& role = roles_[std::size_t(index)];
    if (moving) {
      if (role == Static) staticDirty_ = true;
      role = Dynamic;
      moving_.push_back({index, box});
    } else if (role != Static || changedBox) {
      staticBoxes_[std::size_t(index)] = box;
      role = Static;
      staticDirty_ = true;
    }
  }
  void remove(int index) {
    if (index < 0 || std::size_t(index) >= roles_.size()) return;
    auto& role = roles_[std::size_t(index)];
    if (role == Static) staticDirty_ = true;
    if (role != Absent) role = Absent;
  }
  void queryBox(const BroadBox& area, std::vector<int>& out);
  unsigned staticBuildCount() const { return staticBuilds_; }
  unsigned movingBuildCount() const { return movingBuilds_; }

 private:
  struct Moving { int index; BroadBox box; };
  enum Role : unsigned char { Absent, Static, Dynamic };
  BroadPhase staticIndex_, movingIndex_;
  std::vector<BroadBox> staticBoxes_;
  std::vector<unsigned char> roles_;
  std::vector<Moving> moving_;
  std::vector<int> hits_;
  bool initialized_ = false, staticDirty_ = true, movingDirty_ = true;
  unsigned staticBuilds_ = 0, movingBuilds_ = 0;
};
}  // namespace Tyra

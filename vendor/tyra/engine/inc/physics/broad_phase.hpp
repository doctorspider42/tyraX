/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: the scene broad phase - a height-aware grid or a loose
# octree behind one interface (docs/collision-boxes.md, "The broad phase").
*/

#pragma once

#include <vector>

namespace Tyra {

/** A world-space axis-aligned box. */
struct BroadBox {
  float mn[3] = {0.0F, 0.0F, 0.0F};
  float mx[3] = {0.0F, 0.0F, 0.0F};
};

/**
 * Which objects MAY touch an area or a segment - never fewer - so a caller
 * runs its exact tests on those alone instead of on every object it has.
 *
 * The caller owns the objects (indices 0..count-1) and their boxes; once per
 * refresh it hands over every object that takes part (begin / set / commit),
 * and queries return indices in ascending order (the order a full walk over
 * the objects would visit them, which keeps order-dependent callers exact).
 *
 * Two backends, chosen per project:
 * - kGrid: a uniform grid over X and Z, rebuilt each commit; every box still
 *   carries its height, so a query's Y range filters the candidates. Fastest
 *   for flat worlds of similar-sized props (one counting pass to rebuild).
 * - kOctree: a loose octree (each node's bounds twice its cell), kept across
 *   commits - only objects whose box changed are moved. Splits in height too,
 *   and adapts to uneven density (a dense block next to open ground).
 *
 * A `moving` object is never placed in the structure: it is a candidate of
 * every query (a box from the last commit would lag it) until it settles.
 * There are few of them; physics bodies and anything that moved last frame.
 */
class BroadPhase {
 public:
  enum Kind : int { kGrid = 0, kOctree = 1 };

  explicit BroadPhase(Kind kind = kGrid) : kind_(kind) {}
  Kind kind() const { return kind_; }

  /** Start a refresh over `count` objects; one not set() before commit() is
   *  absent from every query until it is set() again. */
  void begin(int count);
  /** Object `index` takes part, with world box `box`. */
  void set(int index, const BroadBox& box, bool moving);
  /** Apply the refresh: the grid rebuilds, the octree moves only what changed. */
  void commit();
  /** Forget everything (a new scene). The next commit builds from scratch. */
  void clear();

  /** Every object whose box overlaps `area` (inclusive). Infinite bounds are
   *  fine: a column is +-1e30 in Y. */
  void queryBox(const BroadBox& area, std::vector<int>& out);
  /** Every object whose box, padded by `radius`, the segment a -> b crosses. */
  void querySegment(const float a[3], const float b[3], float radius,
                    std::vector<int>& out);

  /** Telemetry: candidates of every query (moving or out of bounds), and the
   *  octree's node count (0 for the grid). */
  int alwaysCount() const { return (int)always_.size(); }
  int nodeCount() const { return (int)nodes_.size(); }

 private:
  struct Node {
    float c[3] = {0.0F, 0.0F, 0.0F};  // centre
    float h = 0.0F;                   // tight half size (loose = 2h)
    int child[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int parent = -1;
    int head = -1;   // first object stored here
    int count = 0;   // objects stored here
    int total = 0;   // objects in this whole subtree (0 = skip it)
    // The union of every box in the subtree: what a query tests, tighter
    // than the loose cube. Grows on insert; a removal leaves it (still a
    // bound, only looser) until the subtree empties and it resets.
    float bmn[3] = {1e30F, 1e30F, 1e30F};
    float bmx[3] = {-1e30F, -1e30F, -1e30F};
  };

  void rebuildGrid();
  void gridQuery(const BroadBox& area, const float* segA, const float* segD,
                 float radius, std::vector<int>& out);
  void buildOctreeRoot();
  bool octreeInsert(int i);
  void octreeRemove(int i);
  void octreeLink(int i, int n);
  void octreeUnlink(int i);
  int octreeChildFor(int n, int i, bool create);
  void octreeSplit(int n, int depth);
  void octreeQuery(const BroadBox& area, const float* segA, const float* segD,
                   float radius, std::vector<int>& out);
  void finishQuery(std::vector<int>& out);

  Kind kind_;
  int count_ = 0;
  // Per object: the box handed over, and whether it takes part / moves.
  std::vector<BroadBox> box_, next_;
  std::vector<unsigned char> present_, moving_, nextPresent_, nextMoving_;
  std::vector<int> always_;
  std::vector<unsigned> mark_;
  unsigned stamp_ = 0;

  // Grid (kGrid): cell c's objects are items_[start_[c] .. start_[c + 1]).
  std::vector<int> start_, cursor_, items_;
  float minX_ = 0.0F, minZ_ = 0.0F, invCell_ = 0.125F;
  int nx_ = 0, nz_ = 0;

  // Octree (kOctree): nodes_[0] is the root; objects form an intrusive
  // doubly linked list per node.
  std::vector<Node> nodes_;
  std::vector<int> objNode_, objNext_, objPrev_;
  std::vector<int> stack_;
  bool rootBuilt_ = false;
};

}  // namespace Tyra

/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: the scene broad phase (see broad_phase.hpp).
*/

#include "physics/broad_phase.hpp"

#include <algorithm>
#include <cmath>

namespace Tyra {

namespace {

// Grid: cells of at least kGridCell metres, at most kGridMaxSide a side (a
// bigger scene gets bigger cells); an object covering more than kGridMaxSpan
// cells is a candidate of every query rather than copied into each.
constexpr float kGridCell = 8.0F;
constexpr int kGridMaxSide = 64;
constexpr int kGridMaxSpan = 64;
// Octree: a node splits only once it holds more than kOctSplit objects (so a
// sparse area stays one shallow node, not a chain down to the smallest cell);
// no deeper than kOctMaxDepth, no cell smaller than kOctMinHalf a half side,
// at most kOctMaxNodes nodes (an object that cannot get a deeper node stays
// where it is - correct, only less pruned).
constexpr int kOctSplit = 8;
constexpr int kOctMaxDepth = 8;
constexpr float kOctMinHalf = 2.0F;
constexpr int kOctMaxNodes = 8192;

bool overlaps(const BroadBox& a, const float* mn, const float* mx) {
  return a.mn[0] <= mx[0] && a.mx[0] >= mn[0] && a.mn[1] <= mx[1] &&
         a.mx[1] >= mn[1] && a.mn[2] <= mx[2] && a.mx[2] >= mn[2];
}

// Does the segment a -> a + d cross the box padded by `pad`? (slabs)
bool segmentHits(const float* a, const float* d, const float* mn,
                 const float* mx, float pad) {
  float t0 = 0.0F, t1 = 1.0F;
  for (int k = 0; k < 3; ++k) {
    const float lo = mn[k] - pad, hi = mx[k] + pad;
    if (std::fabs(d[k]) < 1e-9F) {
      if (a[k] < lo || a[k] > hi) return false;
      continue;
    }
    const float inv = 1.0F / d[k];
    float ta = (lo - a[k]) * inv, tb = (hi - a[k]) * inv;
    if (ta > tb) std::swap(ta, tb);
    if (ta > t0) t0 = ta;
    if (tb < t1) t1 = tb;
    if (t0 > t1) return false;
  }
  return true;
}

// A cell coordinate, clamped in FLOAT before the conversion: an unbounded
// query (+-1e30) must not overflow an int (undefined behaviour).
int cellOf(float v, float origin, float inv, int n) {
  const float f = (v - origin) * inv;
  if (f < 0.0F) return f < -1.0F ? -1 : 0;
  if (f >= (float)n) return n;
  return (int)f;
}

bool sameBox(const BroadBox& a, const BroadBox& b) {
  for (int k = 0; k < 3; ++k)
    if (a.mn[k] != b.mn[k] || a.mx[k] != b.mx[k]) return false;
  return true;
}

}  // namespace

void BroadPhase::clear() {
  count_ = 0;
  box_.clear();
  next_.clear();
  present_.clear();
  moving_.clear();
  nextPresent_.clear();
  nextMoving_.clear();
  always_.clear();
  mark_.clear();
  start_.clear();
  items_.clear();
  nx_ = nz_ = 0;
  nodes_.clear();
  objNode_.clear();
  objNext_.clear();
  objPrev_.clear();
  rootBuilt_ = false;
}

void BroadPhase::begin(int count) {
  if (count < 0) count = 0;
  if (count != count_) {
    // Another object table (a scene load): the indices mean other objects.
    clear();
    count_ = count;
    box_.assign((size_t)count, BroadBox());
    next_.assign((size_t)count, BroadBox());
    present_.assign((size_t)count, 0);
    moving_.assign((size_t)count, 0);
    nextMoving_.assign((size_t)count, 0);
    mark_.assign((size_t)count, 0u);
    objNode_.assign((size_t)count, -1);
    objNext_.assign((size_t)count, -1);
    objPrev_.assign((size_t)count, -1);
  }
  nextPresent_.assign((size_t)count, 0);
}

void BroadPhase::set(int index, const BroadBox& box, bool moving) {
  if (index < 0 || index >= count_) return;
  next_[(size_t)index] = box;
  nextPresent_[(size_t)index] = 1;
  nextMoving_[(size_t)index] = moving ? 1 : 0;
}

void BroadPhase::commit() {
  always_.clear();
  if (kind_ == kGrid) {
    box_.swap(next_);
    present_.swap(nextPresent_);
    moving_.swap(nextMoving_);
    nextPresent_.resize(present_.size());
    nextMoving_.resize(moving_.size());
    next_.resize(box_.size());
    rebuildGrid();
    return;
  }
  if (!rootBuilt_) {
    for (int i = 0; i < count_; ++i) {
      box_[(size_t)i] = next_[(size_t)i];
      present_[(size_t)i] = nextPresent_[(size_t)i];
      moving_[(size_t)i] = nextMoving_[(size_t)i];
    }
    buildOctreeRoot();
    rootBuilt_ = true;
    for (int i = 0; i < count_; ++i) {
      if (!present_[(size_t)i]) continue;
      if (moving_[(size_t)i] || !octreeInsert(i)) always_.push_back(i);
    }
    return;
  }
  // Kept across commits: only what appeared, left, moved or settled changes.
  for (int i = 0; i < count_; ++i) {
    const size_t k = (size_t)i;
    const bool wantStatic = nextPresent_[k] && !nextMoving_[k];
    if (objNode_[k] >= 0 && (!wantStatic || !sameBox(box_[k], next_[k])))
      octreeRemove(i);
    box_[k] = next_[k];
    present_[k] = nextPresent_[k];
    moving_[k] = nextMoving_[k];
    if (!present_[k]) continue;
    if (moving_[k]) {
      always_.push_back(i);
      continue;
    }
    if (objNode_[k] < 0 && !octreeInsert(i)) always_.push_back(i);
  }
}

// --- grid --------------------------------------------------------------------

void BroadPhase::rebuildGrid() {
  float mnX = 1e30F, mnZ = 1e30F, mxX = -1e30F, mxZ = -1e30F;
  for (int i = 0; i < count_; ++i) {
    const size_t k = (size_t)i;
    if (!present_[k]) continue;
    if (moving_[k]) {
      always_.push_back(i);
      continue;
    }
    const BroadBox& b = box_[k];
    if (b.mn[0] < mnX) mnX = b.mn[0];
    if (b.mn[2] < mnZ) mnZ = b.mn[2];
    if (b.mx[0] > mxX) mxX = b.mx[0];
    if (b.mx[2] > mxZ) mxZ = b.mx[2];
  }
  nx_ = nz_ = 0;
  if (mnX > mxX) return;  // nothing static to bucket
  // Sized so span / cell < side - 1: every box falls inside the grid.
  float cell = kGridCell;
  const float spanX = mxX - mnX, spanZ = mxZ - mnZ;
  const float span = spanX > spanZ ? spanX : spanZ;
  if (span / cell > (float)(kGridMaxSide - 2)) cell = span / (float)(kGridMaxSide - 2);
  invCell_ = 1.0F / cell;
  minX_ = mnX;
  minZ_ = mnZ;
  nx_ = std::min((int)(spanX * invCell_) + 1, kGridMaxSide);
  nz_ = std::min((int)(spanZ * invCell_) + 1, kGridMaxSide);
  const auto cx = [&](float x) {
    const int c = (int)((x - minX_) * invCell_);
    return c < 0 ? 0 : (c >= nx_ ? nx_ - 1 : c);
  };
  const auto cz = [&](float z) {
    const int c = (int)((z - minZ_) * invCell_);
    return c < 0 ? 0 : (c >= nz_ ? nz_ - 1 : c);
  };
  start_.assign((size_t)(nx_ * nz_ + 1), 0);
  // Pass 0 counts each cell's objects (over-wide ones go to always_); then
  // prefix sums; pass 1 fills.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < count_; ++i) {
      const size_t k = (size_t)i;
      if (!present_[k] || moving_[k]) continue;
      const BroadBox& b = box_[k];
      const int x0 = cx(b.mn[0]), x1 = cx(b.mx[0]);
      const int z0 = cz(b.mn[2]), z1 = cz(b.mx[2]);
      if ((x1 - x0 + 1) * (z1 - z0 + 1) > kGridMaxSpan) {
        if (pass == 0) always_.push_back(i);
        continue;
      }
      for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
          const int c = z * nx_ + x;
          if (pass == 0)
            ++start_[(size_t)c + 1];
          else
            items_[(size_t)cursor_[(size_t)c]++] = i;
        }
    }
    if (pass == 0) {
      for (size_t c = 1; c < start_.size(); ++c) start_[c] += start_[c - 1];
      items_.resize((size_t)start_.back());
      cursor_.assign(start_.begin(), start_.end() - 1);
    }
  }
}

void BroadPhase::gridQuery(const BroadBox& area, const float* segA,
                           const float* segD, float radius,
                           std::vector<int>& out) {
  if (nx_ == 0) return;
  if (++stamp_ == 0) {  // wrapped: no stale mark may equal a live stamp
    std::fill(mark_.begin(), mark_.end(), 0u);
    stamp_ = 1;
  }
  const auto visitCell = [&](int x, int z) {
    const int c = z * nx_ + x;
    for (int k = start_[(size_t)c]; k < start_[(size_t)c + 1]; ++k) {
      const int i = items_[(size_t)k];
      if (mark_[(size_t)i] == stamp_) continue;
      mark_[(size_t)i] = stamp_;
      const BroadBox& b = box_[(size_t)i];
      const bool hit = segA ? segmentHits(segA, segD, b.mn, b.mx, radius)
                            : overlaps(area, b.mn, b.mx);
      if (hit) out.push_back(i);
    }
  };
  // Every box lies inside the grid: an area off it on a side finds nothing.
  int x0 = cellOf(area.mn[0], minX_, invCell_, nx_);
  int x1 = cellOf(area.mx[0], minX_, invCell_, nx_);
  if (x1 < 0 || x0 >= nx_) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= nx_) x1 = nx_ - 1;
  const bool walk = segA && std::fabs(segD[0]) * invCell_ >= 2.0F;
  for (int x = x0; x <= x1; ++x) {
    float zlo = area.mn[2], zhi = area.mx[2];
    if (walk) {
      // Only the Z cells the (padded) segment crosses inside this column: a
      // long diagonal trace touches O(length) cells, not O(length^2).
      const float cell = 1.0F / invCell_;
      const float sx0 = minX_ + (float)x * cell - radius;
      const float sx1 = sx0 + cell + 2.0F * radius;
      float t0 = (sx0 - segA[0]) / segD[0], t1 = (sx1 - segA[0]) / segD[0];
      if (t0 > t1) std::swap(t0, t1);
      if (t0 < 0.0F) t0 = 0.0F;
      if (t1 > 1.0F) t1 = 1.0F;
      if (t0 > t1) continue;
      const float za = segA[2] + segD[2] * t0, zb = segA[2] + segD[2] * t1;
      zlo = std::min(za, zb) - radius;
      zhi = std::max(za, zb) + radius;
    }
    int z0 = cellOf(zlo, minZ_, invCell_, nz_);
    int z1 = cellOf(zhi, minZ_, invCell_, nz_);
    if (z1 < 0 || z0 >= nz_) continue;
    if (z0 < 0) z0 = 0;
    if (z1 >= nz_) z1 = nz_ - 1;
    for (int z = z0; z <= z1; ++z) visitCell(x, z);
  }
}

// --- octree ------------------------------------------------------------------

void BroadPhase::buildOctreeRoot() {
  nodes_.clear();
  std::fill(objNode_.begin(), objNode_.end(), -1);
  float mn[3] = {1e30F, 1e30F, 1e30F}, mx[3] = {-1e30F, -1e30F, -1e30F};
  for (int i = 0; i < count_; ++i) {
    const size_t k = (size_t)i;
    if (!present_[k] || moving_[k]) continue;
    for (int a = 0; a < 3; ++a) {
      mn[a] = std::min(mn[a], box_[k].mn[a]);
      mx[a] = std::max(mx[a], box_[k].mx[a]);
    }
  }
  Node root;
  if (mn[0] > mx[0]) {  // nothing static yet: a modest cube at the origin
    root.h = 64.0F;
  } else {
    // A cube holding every static box: centres inside the tight cell, and no
    // box larger than it (h >= half of the widest span).
    float span = 0.0F;
    for (int a = 0; a < 3; ++a) {
      root.c[a] = 0.5F * (mn[a] + mx[a]);
      span = std::max(span, mx[a] - mn[a]);
    }
    root.h = std::max(0.5F * span + 1.0F, kOctMinHalf);
  }
  nodes_.push_back(root);
}

// The child of node n that holds object i whole (loose bounds), or -1 when it
// is too big for a child or n may not split further; `create` makes it.
int BroadPhase::octreeChildFor(int n, int i, bool create) {
  const BroadBox& b = box_[(size_t)i];
  float cen[3], half = 0.0F;
  for (int a = 0; a < 3; ++a) {
    cen[a] = 0.5F * (b.mn[a] + b.mx[a]);
    half = std::max(half, 0.5F * (b.mx[a] - b.mn[a]));
  }
  const float ch = nodes_[(size_t)n].h * 0.5F;
  // Fits a child when no larger than its cell and the child is the one
  // holding the centre: then the child's loose bounds hold the whole box.
  if (ch < kOctMinHalf || half > ch) return -1;
  const Node& nd = nodes_[(size_t)n];
  const int oct = (cen[0] >= nd.c[0] ? 1 : 0) | (cen[1] >= nd.c[1] ? 2 : 0) |
                  (cen[2] >= nd.c[2] ? 4 : 0);
  int c = nd.child[oct];
  if (c < 0 && create && (int)nodes_.size() < kOctMaxNodes) {
    Node child;
    child.h = ch;
    child.parent = n;
    child.c[0] = nd.c[0] + ((oct & 1) ? ch : -ch);
    child.c[1] = nd.c[1] + ((oct & 2) ? ch : -ch);
    child.c[2] = nd.c[2] + ((oct & 4) ? ch : -ch);
    nodes_.push_back(child);  // `nd` is invalid past this line
    c = (int)nodes_.size() - 1;
    nodes_[(size_t)n].child[oct] = c;
  }
  return c;
}

void BroadPhase::octreeLink(int i, int n) {
  Node& home = nodes_[(size_t)n];
  objNode_[(size_t)i] = n;
  objPrev_[(size_t)i] = -1;
  objNext_[(size_t)i] = home.head;
  if (home.head >= 0) objPrev_[(size_t)home.head] = i;
  home.head = i;
  ++home.count;
  const BroadBox& b = box_[(size_t)i];
  for (int p = n; p >= 0; p = nodes_[(size_t)p].parent) {
    Node& nd = nodes_[(size_t)p];
    ++nd.total;
    for (int a = 0; a < 3; ++a) {
      if (b.mn[a] < nd.bmn[a]) nd.bmn[a] = b.mn[a];
      if (b.mx[a] > nd.bmx[a]) nd.bmx[a] = b.mx[a];
    }
  }
}

void BroadPhase::octreeUnlink(int i) {
  const size_t k = (size_t)i;
  const int n = objNode_[k];
  if (n < 0) return;
  const int prev = objPrev_[k], next = objNext_[k];
  if (prev >= 0)
    objNext_[(size_t)prev] = next;
  else
    nodes_[(size_t)n].head = next;
  if (next >= 0) objPrev_[(size_t)next] = prev;
  objNode_[k] = objNext_[k] = objPrev_[k] = -1;
  --nodes_[(size_t)n].count;
  for (int p = n; p >= 0; p = nodes_[(size_t)p].parent) {
    Node& nd = nodes_[(size_t)p];
    if (--nd.total == 0)  // empty: its bounds start over
      for (int a = 0; a < 3; ++a) nd.bmn[a] = 1e30F, nd.bmx[a] = -1e30F;
  }
}

// Push node n's objects that fit a child one level down, then split any
// child that is now over the threshold.
void BroadPhase::octreeSplit(int n, int depth) {
  if (depth >= kOctMaxDepth) return;
  int i = nodes_[(size_t)n].head;
  while (i >= 0) {
    const int next = objNext_[(size_t)i];
    const int c = octreeChildFor(n, i, true);
    if (c >= 0) {
      octreeUnlink(i);
      octreeLink(i, c);
    }
    i = next;
  }
  for (int k = 0; k < 8; ++k) {
    const int c = nodes_[(size_t)n].child[k];
    if (c >= 0 && nodes_[(size_t)c].count > kOctSplit) octreeSplit(c, depth + 1);
  }
}

bool BroadPhase::octreeInsert(int i) {
  if (nodes_.empty()) return false;
  // The root takes a box whose centre is in its cell and that fits its loose
  // bounds; anything else is a candidate of every query (outside the scene
  // the root was sized for - rare, and still correct).
  {
    const BroadBox& b = box_[(size_t)i];
    const Node& r = nodes_[0];
    for (int a = 0; a < 3; ++a) {
      const float cen = 0.5F * (b.mn[a] + b.mx[a]);
      if (0.5F * (b.mx[a] - b.mn[a]) > r.h || std::fabs(cen - r.c[a]) > r.h)
        return false;
    }
  }
  // Down through EXISTING children only; a node grows children when it splits.
  int n = 0, depth = 0;
  for (;; ++depth) {
    const int c = octreeChildFor(n, i, false);
    if (c < 0) break;
    n = c;
  }
  octreeLink(i, n);
  if (nodes_[(size_t)n].count > kOctSplit) octreeSplit(n, depth);
  return true;
}

void BroadPhase::octreeRemove(int i) { octreeUnlink(i); }

void BroadPhase::octreeQuery(const BroadBox& area, const float* segA,
                             const float* segD, float radius,
                             std::vector<int>& out) {
  if (nodes_.empty()) return;
  stack_.clear();
  stack_.push_back(0);
  while (!stack_.empty()) {
    const int n = stack_.back();
    stack_.pop_back();
    const Node& nd = nodes_[(size_t)n];
    if (nd.total == 0) continue;  // nothing below: no bounds test, no children
    // The subtree's content bounds (inside the loose cube, and holding every
    // box below): a miss here rules out the whole subtree.
    const bool in = segA ? segmentHits(segA, segD, nd.bmn, nd.bmx, radius)
                         : overlaps(area, nd.bmn, nd.bmx);
    if (!in) continue;
    for (int i = nd.head; i >= 0; i = objNext_[(size_t)i]) {
      const BroadBox& b = box_[(size_t)i];
      const bool hit = segA ? segmentHits(segA, segD, b.mn, b.mx, radius)
                            : overlaps(area, b.mn, b.mx);
      if (hit) out.push_back(i);
    }
    for (int c : nd.child)
      if (c >= 0) stack_.push_back(c);
  }
}

// --- queries -----------------------------------------------------------------

void BroadPhase::finishQuery(std::vector<int>& out) {
  // Ascending: the order a full walk over the objects visits them.
  std::sort(out.begin(), out.end());
}

void BroadPhase::queryBox(const BroadBox& area, std::vector<int>& out) {
  out.clear();
  out.insert(out.end(), always_.begin(), always_.end());
  if (kind_ == kGrid)
    gridQuery(area, nullptr, nullptr, 0.0F, out);
  else
    octreeQuery(area, nullptr, nullptr, 0.0F, out);
  finishQuery(out);
}

void BroadPhase::querySegment(const float a[3], const float b[3], float radius,
                              std::vector<int>& out) {
  out.clear();
  out.insert(out.end(), always_.begin(), always_.end());
  const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  BroadBox area;  // the padded segment's bounds (the grid's columns)
  for (int k = 0; k < 3; ++k) {
    area.mn[k] = std::min(a[k], b[k]) - radius;
    area.mx[k] = std::max(a[k], b[k]) + radius;
  }
  if (kind_ == kGrid)
    gridQuery(area, a, d, radius, out);
  else
    octreeQuery(area, a, d, radius, out);
  finishQuery(out);
}

}  // namespace Tyra

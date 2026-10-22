#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// Private A31 source contract, not a VIF compiler or a production renderer.
namespace WorldStripPlan {
struct alignas(16) Quad {
  std::array<std::uint32_t, 4> words;
  bool operator==(const Quad& rhs) const { return words == rhs.words; }
};
struct Input {
  const Quad* data = nullptr;
  std::uint32_t count = 0;
};
struct Source {
  std::array<Input, 3> streams{};  // XYZ, many-colour, optional ST.
  std::uint32_t count = 0, run = 0, capacity = 0;
  bool textured = false;
};
struct Slice {
  std::uint32_t runFirst = 0, first = 0, count = 0, parity = 0;
  std::array<std::vector<Quad>, 3> streams;
};
struct Plan {
  std::uint64_t generation = 0;
  std::uint32_t sourceVertices = 0, sourceRun = 0;
  bool textured = false;
  std::vector<Slice> slices;
};

inline std::shared_ptr<const Plan> build(const Source& s,
                                        std::uint64_t generation) {
  if (!generation || !s.count || s.run < 3 || s.run % 3 ||
      s.capacity < 3 || s.capacity > 72)
    throw std::invalid_argument("A31 invalid source/capacity");
  for (unsigned stream = 0; stream < (s.textured ? 3U : 2U); ++stream)
    if (!s.streams[stream].data || s.streams[stream].count < s.count)
      throw std::invalid_argument("A31 absent/short stream");
  if (s.count % s.run && s.count % s.run < 3)
    throw std::invalid_argument("A31 incomplete strip tail");
  auto result = std::make_shared<Plan>();
  result->generation = generation;
  result->sourceVertices = s.count;
  result->sourceRun = s.run;
  result->textured = s.textured;
  for (std::uint32_t runFirst = 0; runFirst < s.count;) {
    const auto runCount = std::min(s.run, s.count - runFirst);
    // Runs are independent GS strips; do not fabricate triangles at a seam.
    for (std::uint32_t first = 0; first < runCount - 2;) {
      const auto triangles = std::min(s.capacity - 2, runCount - 2 - first);
      Slice slice;
      slice.runFirst = runFirst;
      slice.first = first;
      slice.count = triangles + 2;
      slice.parity = first & 1;
      for (unsigned stream = 0; stream < (s.textured ? 3U : 2U); ++stream) {
        const auto* begin = s.streams[stream].data + runFirst + first;
        slice.streams[stream].assign(begin, begin + slice.count);
      }
      result->slices.push_back(std::move(slice));
      first += triangles;
    }
    runFirst += runCount;
  }
  return result;
}

class Owner {
 public:
  void replace(const Source& s) {
    if (generation_ == UINT64_MAX)
      throw std::overflow_error("A31 generation exhausted");
    // Publish only a complete new plan. Existing frame leases keep old bytes.
    auto next = build(s, generation_ + 1);
    plan_ = std::move(next);
    ++generation_;
  }
  std::shared_ptr<const Plan> lease() const { return plan_; }
  bool admits(const std::shared_ptr<const Plan>& p) const {
    return p && p == plan_ && p->generation == generation_;
  }
  void retire() { plan_.reset(); }
 private:
  std::uint64_t generation_ = 0;
  std::shared_ptr<const Plan> plan_;
};
}

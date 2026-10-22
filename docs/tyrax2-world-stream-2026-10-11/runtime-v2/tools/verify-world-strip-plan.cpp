#include "tyrax2-world-strip-plan.hpp"
#include <cassert>
#include <iostream>
using namespace WorldStripPlan;
using Triangle = std::array<Quad, 3>;
using Triangles = std::array<std::vector<Triangle>, 3>;

static Triangles original(const Source& s) {
  Triangles out;
  for (unsigned base = 0; base < s.count; base += s.run) {
    const unsigned count = std::min(s.run, s.count - base);
    for (unsigned t = 0; t + 2 < count; ++t)
      for (unsigned stream = 0; stream < (s.textured ? 3U : 2U); ++stream) {
        const unsigned a = base + t, b = a + 1, c = a + 2;
        const auto* v = s.streams[stream].data;
        out[stream].push_back(t % 2 ? Triangle{v[b], v[a], v[c]} :
          Triangle{v[a], v[b], v[c]});
      }
  }
  return out;
}
static Triangles delivered(const Plan& p) {
  Triangles out;
  for (const auto& slice : p.slices) {
    assert(slice.count >= 3 && slice.count <= 72);
    assert(slice.first + slice.count <=
      std::min(p.sourceRun, p.sourceVertices - slice.runFirst));
    for (unsigned t = 0; t + 2 < slice.count; ++t)
      for (unsigned stream = 0; stream < (p.textured ? 3U : 2U); ++stream) {
        const auto& v = slice.streams[stream];
        assert(v.size() == slice.count);
        const bool odd = (t + slice.parity) % 2;
        out[stream].push_back({v[t + unsigned(odd)], v[t + unsigned(!odd)], v[t+2]});
      }
  }
  return out;
}
int main() {
  unsigned cases = 0;
  std::array<bool, 6> corruptions{};
  for (bool textured : {false, true})
    for (unsigned run : {3U, 6U, 72U, 75U, 111U})
      for (unsigned runs : {1U, 2U, 7U})
        for (unsigned tail : {0U, 3U, 6U})
          for (unsigned capacity : {3U, 4U, 5U, 71U, 72U}) {
            if (tail >= run) continue;
            std::array<std::vector<Quad>, 3> arrays;
            const unsigned count = run * runs + tail;
            for (unsigned s = 0; s < 3; ++s) {
              arrays[s].resize(count);
              for (unsigned i = 0; i < count; ++i)
                arrays[s][i].words = {i, 1000+s, i*17+s, 0x7fc00000U+i};
            }
            Source source{{Input{arrays[0].data(),count}, Input{arrays[1].data(),count},
                          Input{arrays[2].data(),count}}, count, run, capacity, textured};
            const auto expected = original(source);
            Owner owner; owner.replace(source);
            auto old = owner.lease();
            assert(owner.admits(old) && delivered(*old) == expected);
            Plan bad = *old;
            bad.slices.front().parity ^= 1;
            corruptions[0] |= delivered(bad) != expected;
            bad = *old; bad.slices.pop_back();
            corruptions[1] |= delivered(bad) != expected;
            bad = *old; bad.slices.push_back(bad.slices.back());
            corruptions[2] |= delivered(bad) != expected;
            bad = *old; bad.slices.front().streams[1][0].words[0] ^= 1;
            corruptions[3] |= delivered(bad) != expected;
            if (textured) {
              bad = *old; bad.slices.front().streams[2][0].words[0] ^= 1;
              corruptions[4] |= delivered(bad) != expected;
            }
            // Changing/reallocating source buffers cannot change in-flight data.
            arrays[0][0].words[0] ^= 99;
            owner.replace(source);
            assert(!owner.admits(old) && owner.admits(owner.lease()));
            assert(delivered(*old) == expected);
            auto next = owner.lease();
            assert(next->generation == old->generation + 1);
            const auto newExpected = original(source);
            auto invalid = source; invalid.count = 0;
            bool failedReplace = false;
            try { owner.replace(invalid); }
            catch (const std::invalid_argument&) { failedReplace = true; }
            assert(failedReplace && owner.lease() == next && owner.admits(next));
            for (auto& array : arrays) { array.clear(); array.shrink_to_fit(); }
            assert(delivered(*old) == expected && delivered(*next) == newExpected);
            owner.retire();
            corruptions[5] |= !owner.admits(next) && !owner.admits(old);
            assert(delivered(*next) == newExpected);
            ++cases;
          }
  assert(std::all_of(corruptions.begin(), corruptions.end(), [](bool v){return v;}));
  unsigned rejects = 0;
  std::array<Quad,4> q{};
  const Source good{{Input{q.data(),4},Input{q.data(),4},Input{q.data(),4}}, 3, 3, 3, true};
  for (unsigned i = 0; i < 13; ++i) {
    auto bad = good;
    if (i == 0) bad.capacity = 2;
    if (i == 1) bad.capacity = 73;
    if (i == 2) bad.run = 4;
    if (i == 3) bad.count = 4;
    if (i == 4) bad.streams[0].data = nullptr;
    if (i == 5) bad.streams[1].data = nullptr;
    if (i == 6) bad.streams[2].data = nullptr;
    if (i >= 7 && i < 10) bad.streams[i-7].count = 2;
    if (i == 10) bad.count = 0;
    if (i == 11) bad.run = 0;
    try { (void)build(bad, i == 12 ? 0 : 1); }
    catch (const std::invalid_argument&) { ++rejects; }
  }
  assert(rejects == 13);
  std::cout << "PASS A31 owned strip plan cases=" << cases
            << " corruptions=6 admission-rejections=13 failed-replace-preserved=360\n";
}

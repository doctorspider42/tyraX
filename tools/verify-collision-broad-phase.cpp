// Regression oracle: broad-phase queries must contain all overlapping boxes,
// including moving objects, after insert/move/remove and across scene changes.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

#include "physics/broad_phase.hpp"
int main() {
  std::mt19937 rng(81231);
  std::uniform_real_distribution<float> pos(-100, 100), size(.01f, 8);
  for (int backend = 0; backend < 2; ++backend) {
    Tyra::BroadPhase bp((Tyra::BroadPhase::Kind)backend);
    std::vector<Tyra::BroadBox> boxes(400);
    std::vector<bool> present(400);
    for (int frame = 0; frame < 50; ++frame) {
      bp.begin(400);
      for (int i = 0; i < 400; ++i) {
        present[i] = (rng() % 7) != 0;
        for (int k = 0; k < 3; ++k) {
          float p = pos(rng);
          boxes[i].mn[k] = p;
          boxes[i].mx[k] = p + size(rng);
        }
        if (present[i]) bp.set(i, boxes[i], (i % 2) == 0);
      }
      bp.commit();
      for (int q = 0; q < 100; ++q) {
        Tyra::BroadBox area;
        for (int k = 0; k < 3; ++k) {
          area.mn[k] = pos(rng);
          area.mx[k] = area.mn[k] + size(rng) * 3;
        }
        std::vector<int> out;
        bp.queryBox(area, out);
        assert(std::is_sorted(out.begin(), out.end()));
        for (int i = 0; i < 400; ++i)
          if (present[i]) {
            bool hit = true;
            for (int k = 0; k < 3; ++k)
              hit &=
                  boxes[i].mn[k] <= area.mx[k] && boxes[i].mx[k] >= area.mn[k];
            if (hit) assert(std::binary_search(out.begin(), out.end(), i));
          }
      }
    }
    // Moving objects far away must not all leak into local queries.
    bp.clear();
    bp.begin(400);
    for (int i = 0; i < 400; ++i) {
      for (int k = 0; k < 3; ++k) {
        boxes[i].mn[k] = i * 10.f;
        boxes[i].mx[k] = i * 10.f + 1;
      }
      bp.set(i, boxes[i], true);
    }
    bp.commit();
    std::vector<int> out;
    bp.queryBox(boxes[0], out);
    assert(out.size() == 1 && out[0] == 0);
  }
  puts(
      "broad phase: 10,000 randomized queries + moving-population rejection "
      "passed");
}

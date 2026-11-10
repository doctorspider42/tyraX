#include <cassert>
#include <cstdio>
#include <random>

#include "physics/triangle_bvh.hpp"
int main() {
  std::mt19937 rng(89342);
  std::uniform_real_distribution<float> p(-50, 50);
  std::vector<float> verts;
  for (int i = 0; i < 3000; ++i) verts.push_back(p(rng));
  auto tris =
      Tyra::collisionTriangles(verts.data(), (uint32_t)verts.size() / 3, 3);
  Tyra::TriangleBvh tree;
  tree.build(tris);
  auto data = tree.encode(tris);
  Tyra::TriangleBvh copy;
  std::vector<float> decoded;
  assert(copy.decode(data, decoded));
  assert(decoded == tris);
  assert(copy.encode(decoded) == data);
  for (int q = 0; q < 2000; ++q) {
    float mn[3], mx[3];
    for (int k = 0; k < 3; ++k) {
      mn[k] = p(rng);
      mx[k] = mn[k] + 5;
    }
    std::vector<bool> got(tris.size() / 12);
    tree.box(mn, mx, [&](uint32_t id) { got[id] = true; });
    for (size_t t = 0; t < got.size(); ++t) {
      bool hit = true;
      for (int k = 0; k < 3; ++k) {
        float lo = 1e30f, hi = -1e30f;
        for (int v = 0; v < 3; ++v) {
          lo = std::min(lo, tris[t * 12 + v * 3 + k]);
          hi = std::max(hi, tris[t * 12 + v * 3 + k]);
        }
        hit &= lo <= mx[k] && hi >= mn[k];
      }
      if (hit) assert(got[t]);
    }
  }
  for (size_t n = 0; n < data.size(); n += 31)
    assert(!copy.decode(data.substr(0, n), decoded));
  auto bad = data;
  bad[4] = 2;
  assert(!copy.decode(bad, decoded));
  // Stacked floors: a local query must prune other heights.
  verts.clear();
  for (int y = 0; y < 100; ++y)
    for (int x = 0; x < 8; ++x) {
      float v[] = {float(x), float(y * 10), 0, float(x + 1), float(y * 10), 0,
                   float(x), float(y * 10), 1};
      verts.insert(verts.end(), v, v + 9);
    }
  tris = Tyra::collisionTriangles(verts.data(), (uint32_t)verts.size() / 3, 3);
  tree.build(tris);
  float mn[] = {0, -1, 0}, mx[] = {8, 1, 1};
  int count = 0;
  tree.box(mn, mx, [&](uint32_t) { ++count; });
  assert(count < 32);
  puts(
      "triangle BVH: 2,000 oracle queries, binary roundtrip/corruption, "
      "stacked-floor pruning passed");
}

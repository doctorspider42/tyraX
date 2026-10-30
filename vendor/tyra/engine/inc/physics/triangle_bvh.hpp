// Added by TyraX: shared, host-baked triangle BVH (no PS2 SDK dependencies).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>
namespace Tyra {
class TriangleBvh {
 public:
  struct Node {
    float mn[3], mx[3];
    uint32_t first = 0, count = 0, escape = 0;
  };
  std::vector<Node> nodes;
  std::vector<uint32_t> order;
  void build(const std::vector<float>& tris) {
    nodes.clear();
    order.resize(tris.size() / 12);
    std::iota(order.begin(), order.end(), 0u);
    if (!order.empty()) {
      nodes.reserve(order.size() / 2 + 1);
      split(tris, 0, (uint32_t)order.size());
    }
  }
  template <class Visit>
  void box(const float* mn, const float* mx, Visit visit) const {
    for (uint32_t i = 0; i < nodes.size();) {
      const Node& n = nodes[i];
      bool hit = true;
      for (int k = 0; k < 3; ++k)
        if (mn[k] > n.mx[k] || mx[k] < n.mn[k]) hit = false;
      if (!hit) {
        i = n.escape;
        continue;
      }
      if (n.count)
        for (uint32_t j = 0; j < n.count; ++j) visit(order[n.first + j]);
      ++i;
    }
  }
  template <class Visit>
  void ray(const float* p, const float* d, float& best, Visit visit,
           float padding = 0) const {
    for (uint32_t i = 0; i < nodes.size();) {
      const Node& n = nodes[i];
      float lo = 0, hi = best;
      bool hit = true;
      for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-12f) {
          if (p[k] < n.mn[k] - padding || p[k] > n.mx[k] + padding) hit = false;
        } else {
          float a = (n.mn[k] - padding - p[k]) / d[k],
                b = (n.mx[k] + padding - p[k]) / d[k];
          if (a > b) std::swap(a, b);
          lo = std::max(lo, a);
          hi = std::min(hi, b);
        }
      }
      if (!hit || lo > hi) {
        i = n.escape;
        continue;
      }
      if (n.count)
        for (uint32_t j = 0; j < n.count; ++j) visit(order[n.first + j]);
      ++i;
    }
  }
  // TCOL v1: explicitly little-endian words, independent of host ABI/padding.
  std::string encode(const std::vector<float>& tris) const {
    std::string out("TCOL", 4);
    auto word = [&](uint32_t v) {
      for (int k = 0; k < 4; ++k) out.push_back(char(v >> (8 * k)));
    };
    auto flt = [&](float v) {
      uint32_t b;
      std::memcpy(&b, &v, 4);
      word(b);
    };
    word(1);
    word((uint32_t)order.size());
    word((uint32_t)nodes.size());
    for (float v : tris) flt(v);
    for (const Node& n : nodes) {
      for (float v : n.mn) flt(v);
      for (float v : n.mx) flt(v);
      word(n.first);
      word(n.count);
      word(n.escape);
    }
    for (uint32_t v : order) word(v);
    return out;
  }
  bool decode(const std::string& bytes, std::vector<float>& tris) {
    if (bytes.size() < 16 || bytes.compare(0, 4, "TCOL") != 0) return false;
    size_t at = 4;
    auto word = [&]() {
      uint32_t v = 0;
      for (int k = 0; k < 4; ++k)
        v |= uint32_t((unsigned char)bytes[at++]) << (8 * k);
      return v;
    };
    if (word() != 1) return false;
    uint32_t nt = word(), nn = word();
    if (nt == 0 && nn == 0 && bytes.size() == 16) {
      tris.clear();
      nodes.clear();
      order.clear();
      return true;
    }
    if (!nt || nt > (1u << 21) || !nn || nn > 2 * nt ||
        bytes.size() != 16ull + 52ull * nt + 36ull * nn)
      return false;
    auto flt = [&]() {
      uint32_t b = word();
      float v;
      std::memcpy(&v, &b, 4);
      return v;
    };
    std::vector<float> t(size_t(nt) * 12);
    std::vector<Node> ns(nn);
    std::vector<uint32_t> ids(nt);
    for (float& v : t) {
      v = flt();
      if (!std::isfinite(v)) return false;
    }
    for (uint32_t i = 0; i < nn; ++i) {
      Node& n = ns[i];
      for (float& v : n.mn) v = flt();
      for (float& v : n.mx) v = flt();
      n.first = word();
      n.count = word();
      n.escape = word();
      if (n.escape <= i || n.escape > nn || n.first > nt ||
          n.count > nt - n.first)
        return false;
      for (int k = 0; k < 3; ++k)
        if (!std::isfinite(n.mn[k]) || !std::isfinite(n.mx[k]) ||
            n.mn[k] > n.mx[k])
          return false;
    }
    if (ns[0].escape != nn) return false;
    for (auto& v : ids) {
      v = word();
      if (v >= nt) return false;
    }
    tris.swap(t);
    nodes.swap(ns);
    order.swap(ids);
    return true;
  }

 private:
  void split(const std::vector<float>& t, uint32_t first, uint32_t count) {
    uint32_t index = (uint32_t)nodes.size();
    Node n;
    for (int k = 0; k < 3; ++k) {
      n.mn[k] = 1e30f;
      n.mx[k] = -1e30f;
    }
    for (uint32_t j = first; j < first + count; ++j)
      for (int v = 0; v < 3; ++v)
        for (int k = 0; k < 3; ++k) {
          float x = t[order[j] * 12 + v * 3 + k];
          n.mn[k] = std::min(n.mn[k], x);
          n.mx[k] = std::max(n.mx[k], x);
        }
    nodes.push_back(n);
    if (count <= 8) {
      nodes[index].first = first;
      nodes[index].count = count;
    } else {
      int axis = 0;
      for (int k = 1; k < 3; ++k)
        if (n.mx[k] - n.mn[k] > n.mx[axis] - n.mn[axis]) axis = k;
      auto centre = [&](uint32_t id) {
        return t[id * 12 + axis] + t[id * 12 + 3 + axis] +
               t[id * 12 + 6 + axis];
      };
      uint32_t half = count / 2;
      std::nth_element(order.begin() + first, order.begin() + first + half,
                       order.begin() + first + count,
                       [&](uint32_t a, uint32_t b) {
                         float x = centre(a), y = centre(b);
                         return x == y ? a < b : x < y;
                       });
      split(t, first, half);
      split(t, first + half, count - half);
    }
    nodes[index].escape = (uint32_t)nodes.size();
  }
};
// Pack triangle positions and unit face normals, identical on host and EE.
inline std::vector<float> collisionTriangles(const float* verts, uint32_t count,
                                             uint32_t stride) {
  std::vector<float> out;
  if (!verts || stride < 3) return out;
  out.reserve(size_t(count / 3) * 12);
  for (uint32_t i = 0; i + 2 < count; i += 3) {
    const float* a = verts + i * stride;
    const float* b = a + stride;
    const float* c = b + stride;
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2],
          vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz,
          nz = ux * vy - uy * vx;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 1e-8f) continue;
    for (const float* p : {a, b, c}) {
      for (int k = 0; k < 3; ++k) out.push_back(p[k]);
    }
    out.push_back(nx / len);
    out.push_back(ny / len);
    out.push_back(nz / len);
  }
  return out;
}
}  // namespace Tyra

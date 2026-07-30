#include "bvh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bvh {

namespace {

inline void v3sub(const float* a, const float* b, float* o) {
    o[0] = a[0] - b[0], o[1] = a[1] - b[1], o[2] = a[2] - b[2];
}
inline void v3cross(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
inline float v3dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void buildRange(Tree& b, std::vector<float>& cent, int nodeIdx, int first,
                int count) {
    {
        Node& n0 = b.nodes[nodeIdx];
        float bmin[3] = {1e30f, 1e30f, 1e30f};
        float bmax[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = first; i < first + count; ++i) {
            const float* v = &b.tv[(size_t)b.order[i] * 9];
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) {
                    bmin[k] = std::min(bmin[k], v[c * 3 + k]);
                    bmax[k] = std::max(bmax[k], v[c * 3 + k]);
                }
        }
        std::memcpy(n0.bmin, bmin, sizeof bmin);
        std::memcpy(n0.bmax, bmax, sizeof bmax);
        if (count <= 4) {
            n0.left = first;
            n0.count = count;
            return;
        }
    }
    // centroid extents pick the split axis
    float cmin[3] = {1e30f, 1e30f, 1e30f}, cmax[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = first; i < first + count; ++i) {
        const float* c = &cent[(size_t)b.order[i] * 3];
        for (int k = 0; k < 3; ++k) {
            cmin[k] = std::min(cmin[k], c[k]);
            cmax[k] = std::max(cmax[k], c[k]);
        }
    }
    int axis = 0;
    float ext = cmax[0] - cmin[0];
    for (int k = 1; k < 3; ++k)
        if (cmax[k] - cmin[k] > ext) ext = cmax[k] - cmin[k], axis = k;

    int lc = count / 2;  // fallback: median split
    if (ext > 1e-9f) {
        // 8-bin SAH along the chosen axis
        constexpr int kBins = 8;
        struct Bin {
            float bmin[3] = {1e30f, 1e30f, 1e30f};
            float bmax[3] = {-1e30f, -1e30f, -1e30f};
            int n = 0;
        } bins[kBins];
        const float invExt = kBins / ext;
        auto binOf = [&](int tri) {
            const int bi =
                (int)((cent[(size_t)tri * 3 + axis] - cmin[axis]) * invExt);
            return bi < 0 ? 0 : bi >= kBins ? kBins - 1 : bi;
        };
        for (int i = first; i < first + count; ++i) {
            const int tri = b.order[i];
            Bin& bn = bins[binOf(tri)];
            ++bn.n;
            const float* v = &b.tv[(size_t)tri * 9];
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) {
                    bn.bmin[k] = std::min(bn.bmin[k], v[c * 3 + k]);
                    bn.bmax[k] = std::max(bn.bmax[k], v[c * 3 + k]);
                }
        }
        auto area = [](const float* mn, const float* mx) {
            const float x = std::max(0.0f, mx[0] - mn[0]);
            const float y = std::max(0.0f, mx[1] - mn[1]);
            const float z = std::max(0.0f, mx[2] - mn[2]);
            return x * y + y * z + z * x;
        };
        float bestCost = 1e30f;
        int bestSplit = -1;
        for (int s = 1; s < kBins; ++s) {
            float lmin[3] = {1e30f, 1e30f, 1e30f}, lmax[3] = {-1e30f, -1e30f, -1e30f};
            float rmin[3] = {1e30f, 1e30f, 1e30f}, rmax[3] = {-1e30f, -1e30f, -1e30f};
            int ln = 0, rn = 0;
            for (int i = 0; i < kBins; ++i) {
                if (!bins[i].n) continue;
                float* mn = i < s ? lmin : rmin;
                float* mx = i < s ? lmax : rmax;
                (i < s ? ln : rn) += bins[i].n;
                for (int k = 0; k < 3; ++k) {
                    mn[k] = std::min(mn[k], bins[i].bmin[k]);
                    mx[k] = std::max(mx[k], bins[i].bmax[k]);
                }
            }
            if (!ln || !rn) continue;
            const float cost = ln * area(lmin, lmax) + rn * area(rmin, rmax);
            if (cost < bestCost) bestCost = cost, bestSplit = s;
        }
        if (bestSplit >= 0) {
            auto mid = std::partition(
                b.order.begin() + first, b.order.begin() + first + count,
                [&](int tri) { return binOf(tri) < bestSplit; });
            lc = (int)(mid - (b.order.begin() + first));
            if (lc == 0 || lc == count) lc = count / 2;
        }
    }
    const int l = (int)b.nodes.size();
    b.nodes[nodeIdx].left = l;
    b.nodes[nodeIdx].count = 0;
    b.nodes.emplace_back();
    b.nodes.emplace_back();
    buildRange(b, cent, l, first, lc);
    buildRange(b, cent, l + 1, first + lc, count - lc);
}

}  // namespace

void build(Tree& b) {
    const int triCount = (int)(b.tv.size() / 9);
    b.nodes.clear();
    b.order.resize(triCount);
    for (int i = 0; i < triCount; ++i) b.order[i] = i;
    if (!triCount) return;
    std::vector<float> cent((size_t)triCount * 3);
    for (int t = 0; t < triCount; ++t) {
        const float* v = &b.tv[(size_t)t * 9];
        for (int k = 0; k < 3; ++k)
            cent[(size_t)t * 3 + k] = (v[k] + v[3 + k] + v[6 + k]) / 3.0f;
    }
    b.nodes.reserve((size_t)triCount * 2);
    b.nodes.emplace_back();
    buildRange(b, cent, 0, 0, triCount);
    std::memcpy(b.bmin, b.nodes[0].bmin, sizeof b.bmin);
    std::memcpy(b.bmax, b.nodes[0].bmax, sizeof b.bmax);
}

bool rayTri(const float* o, const float* dir, const float* a, const float* b,
            const float* c, float tMax, Hit& h) {
    float e1[3], e2[3], p[3], tv[3], q[3];
    v3sub(b, a, e1);
    v3sub(c, a, e2);
    v3cross(dir, e2, p);
    const float det = v3dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    v3sub(o, a, tv);
    const float u = v3dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    v3cross(tv, e1, q);
    const float v = v3dot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = v3dot(e2, q) * inv;
    if (t <= 0.0f || t >= tMax) return false;
    h.t = t, h.u = u, h.v = v;
    h.back = det < 0.0f;  // CCW winding faces det > 0
    return true;
}

bool trace(const Tree& b, const float* o, const float* dir, float tMax,
           bool acceptBack, Hit& best) {
    if (b.nodes.empty()) return false;
    auto safe = [](float d) {
        return std::fabs(d) < 1e-12f ? (d < 0 ? -1e-12f : 1e-12f) : d;
    };
    const float inv[3] = {1.0f / safe(dir[0]), 1.0f / safe(dir[1]),
                          1.0f / safe(dir[2])};
    best.tri = -1;
    best.t = tMax;
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node& n = b.nodes[stack[--sp]];
        float t0 = 0.0f, t1 = best.t;
        bool out = false;
        for (int k = 0; k < 3; ++k) {
            float ta = (n.bmin[k] - o[k]) * inv[k];
            float tb = (n.bmax[k] - o[k]) * inv[k];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) {
                out = true;
                break;
            }
        }
        if (out) continue;
        if (n.count) {
            for (int i = n.left; i < n.left + n.count; ++i) {
                const int tri = b.order[i];
                const float* v = &b.tv[(size_t)tri * 9];
                Hit h;
                if (rayTri(o, dir, v, v + 3, v + 6, best.t, h)) {
                    if (!acceptBack && h.back) continue;
                    best = h;
                    best.tri = tri;
                }
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = n.left;
            stack[sp++] = n.left + 1;
        }
    }
    return best.tri >= 0;
}

}  // namespace bvh

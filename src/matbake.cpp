#include "matbake.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

#include "primmesh.hpp"

namespace matbake {

namespace {

constexpr float kPi = 3.14159265358979f;

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
inline float v3len(const float* a) { return std::sqrt(v3dot(a, a)); }
inline bool v3norm(float* a) {
    const float l = v3len(a);
    if (l < 1e-12f) return false;
    a[0] /= l, a[1] /= l, a[2] /= l;
    return true;
}

// --- BVH ---------------------------------------------------------------------
// Flat binned-SAH BVH over a triangle soup. Nodes live in one array; a leaf
// holds count > 0 triangles starting at slot `left` of the reordered index
// list, an inner node holds count == 0 and its children at left / left + 1.

struct BvhNode {
    float bmin[3], bmax[3];
    int32_t left = 0;
    int32_t count = 0;
};

struct Bvh {
    std::vector<BvhNode> nodes;
    std::vector<int32_t> order;  // triangle indices, leaf-contiguous
    // the triangle soup the BVH indexes: 9 floats position + 9 floats
    // per-corner smoothed normal per triangle (hit-normal interpolation)
    std::vector<float> tv;
    std::vector<float> tn;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
};

void bvhBuildRange(Bvh& b, std::vector<float>& cent, int nodeIdx, int first,
                   int count) {
    {
        BvhNode& n0 = b.nodes[nodeIdx];
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
    bvhBuildRange(b, cent, l, first, lc);
    bvhBuildRange(b, cent, l + 1, first + lc, count - lc);
}

// tv/tn must be filled before the call.
void bvhBuild(Bvh& b) {
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
    bvhBuildRange(b, cent, 0, 0, triCount);
    std::memcpy(b.bmin, b.nodes[0].bmin, sizeof b.bmin);
    std::memcpy(b.bmax, b.nodes[0].bmax, sizeof b.bmax);
}

// Moller-Trumbore. Unlike aobake's per-vertex variant this KEEPS grazing
// hits: texel origins are epsilon-offset off the surface, so the origin's
// own tangent faces never self-hit, and rejecting grazers would poke light
// leaks into shallow crevices.
struct Hit {
    float t = 0.0f, u = 0.0f, v = 0.0f;
    int tri = -1;
    bool back = false;  // struck the winding's back side
};

inline bool rayTri(const float* o, const float* dir, const float* a,
                   const float* b, const float* c, float tMax, Hit& h) {
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

// Closest hit within tMax. acceptBack = false makes back sides transparent
// (traversal continues past them).
bool bvhTrace(const Bvh& b, const float* o, const float* dir, float tMax,
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
        const BvhNode& n = b.nodes[stack[--sp]];
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

// --- smooth normals + curvature ----------------------------------------------

// Weld key per corner: posIdx when the mesh carries it, else the quantized
// position (primitives; seams weld fine at this resolution).
std::vector<int> weldCorners(const MeshInput& m) {
    const size_t corners = m.verts.size() / 8;
    std::vector<int> weld(corners);
    if (m.posIdx.size() == corners) {
        for (size_t i = 0; i < corners; ++i) weld[i] = m.posIdx[i];
        return weld;
    }
    std::map<std::tuple<int, int, int>, int> keys;
    for (size_t i = 0; i < corners; ++i) {
        const float* p = &m.verts[i * 8];
        const auto key = std::make_tuple((int)std::lround(p[0] * 8192.0f),
                                         (int)std::lround(p[1] * 8192.0f),
                                         (int)std::lround(p[2] * 8192.0f));
        auto it = keys.find(key);
        if (it == keys.end()) it = keys.emplace(key, (int)keys.size()).first;
        weld[i] = it->second;
    }
    return weld;
}

// Area-weighted position-averaged normals (the cage/projection directions)
// plus a per-position mean-curvature estimate from edge normal deltas
// (dot(nB - nA, pB - pA) / |pB - pA|^2, the standard discrete form) - the
// "wear on edges / dirt in cavities" driver, no rays involved. Curvature is
// normalized so the 90th-percentile magnitude maps to 0.75: deterministic
// and independent of the model's absolute size.
void smoothNormalsAndCurvature(const MeshInput& m, const std::vector<int>& weld,
                               std::vector<float>& outNrm,
                               std::vector<float>& outCurv) {
    int posCount = 0;
    for (int w : weld) posCount = std::max(posCount, w + 1);
    outNrm.assign((size_t)posCount * 3, 0.0f);
    std::vector<float> pos((size_t)posCount * 3, 0.0f);
    std::vector<char> seen(posCount, 0);
    const int tris = m.triCount();
    for (int t = 0; t < tris; ++t) {
        const float* a = &m.verts[(size_t)(t * 3 + 0) * 8];
        const float* b = &m.verts[(size_t)(t * 3 + 1) * 8];
        const float* c = &m.verts[(size_t)(t * 3 + 2) * 8];
        float e1[3], e2[3], fn[3];
        v3sub(b, a, e1);
        v3sub(c, a, e2);
        v3cross(e1, e2, fn);  // length = 2x area: area weighting for free
        for (int k = 0; k < 3; ++k) {
            const int w = weld[t * 3 + k];
            for (int j = 0; j < 3; ++j) outNrm[(size_t)w * 3 + j] += fn[j];
            if (!seen[w]) {
                seen[w] = 1;
                const float* p = &m.verts[(size_t)(t * 3 + k) * 8];
                for (int j = 0; j < 3; ++j) pos[(size_t)w * 3 + j] = p[j];
            }
        }
    }
    for (int i = 0; i < posCount; ++i)
        if (!v3norm(&outNrm[(size_t)i * 3])) outNrm[(size_t)i * 3 + 1] = 1.0f;

    std::vector<float> curv(posCount, 0.0f);
    std::vector<int> curvN(posCount, 0);
    for (int t = 0; t < tris; ++t) {
        for (int k = 0; k < 3; ++k) {
            const int wa = weld[t * 3 + k], wb = weld[t * 3 + (k + 1) % 3];
            if (wa == wb) continue;
            float dp[3], dn[3];
            v3sub(&pos[(size_t)wb * 3], &pos[(size_t)wa * 3], dp);
            v3sub(&outNrm[(size_t)wb * 3], &outNrm[(size_t)wa * 3], dn);
            const float l2 = v3dot(dp, dp);
            if (l2 < 1e-12f) continue;
            const float kv = v3dot(dn, dp) / l2;
            curv[wa] += kv, curv[wb] += kv;
            ++curvN[wa], ++curvN[wb];
        }
    }
    std::vector<float> mags;
    mags.reserve(posCount);
    for (int i = 0; i < posCount; ++i) {
        if (curvN[i]) curv[i] /= (float)curvN[i];
        if (curvN[i] && curv[i] != 0.0f) mags.push_back(std::fabs(curv[i]));
    }
    float scale = 1.0f;
    if (!mags.empty()) {
        const size_t p90i = (mags.size() * 9) / 10;
        std::nth_element(mags.begin(), mags.begin() + p90i, mags.end());
        if (mags[p90i] > 1e-9f) scale = 0.75f / mags[p90i];
    }
    outCurv.assign(posCount, 0.0f);
    for (int i = 0; i < posCount; ++i) {
        const float c = curv[i] * scale;
        outCurv[i] = c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c;
    }
}

// --- geometry buffer -----------------------------------------------------------

// One surface sample of a texel: interpolated position + shading normal, and
// the triangle + barycentrics it came from (smooth attributes interpolate
// through them after the raster).
struct GSample {
    int texel;
    float pos[3], nrm[3];
    float curv = 0.0f;
    int tri;
    float bu, bv;
};

struct GBuffer {
    int w = 0, h = 0;
    std::vector<GSample> samples;  // sorted by texel
    std::vector<int> offset;       // w*h+1 prefix offsets into samples
    std::vector<uint8_t> mask;     // 255 = covered (pre-dilate)
    int count(int texel) const { return offset[texel + 1] - offset[texel]; }
};

inline int wrapc(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

// Conservative UV rasterization at `size` with an ss x ss subsample grid.
// Pass 1 samples the grid points inside each triangle; pass 2 gives every
// still-empty texel whose square the triangle grazes one sample at the
// nearest interior point - a texel touched only by a corner still bakes, so
// island borders have no coverage gaps for dilate to guess at. UVs wrap
// (the GS repeats textures).
void rasterize(const MeshInput& m, int size, int ss, GBuffer& g) {
    g.w = g.h = size;
    g.samples.clear();
    g.mask.assign((size_t)size * size, 0);
    const int tris = m.triCount();
    const float W = (float)size;
    std::vector<uint8_t> occupied((size_t)size * size, 0);

    struct TriUv {
        float ax, ay, bx, by, cx, cy, inv;
        int x0, x1, y0, y1;
        bool skip;
    };
    auto triUv = [&](int t) {
        TriUv u;
        const float* a = &m.verts[(size_t)(t * 3 + 0) * 8];
        const float* b = &m.verts[(size_t)(t * 3 + 1) * 8];
        const float* c = &m.verts[(size_t)(t * 3 + 2) * 8];
        u.ax = a[6] * W, u.ay = a[7] * W;
        u.bx = b[6] * W, u.by = b[7] * W;
        u.cx = c[6] * W, u.cy = c[7] * W;
        const float area2 =
            (u.bx - u.ax) * (u.cy - u.ay) - (u.by - u.ay) * (u.cx - u.ax);
        u.x0 = (int)std::floor(std::min({u.ax, u.bx, u.cx}));
        u.x1 = (int)std::ceil(std::max({u.ax, u.bx, u.cx}));
        u.y0 = (int)std::floor(std::min({u.ay, u.by, u.cy}));
        u.y1 = (int)std::ceil(std::max({u.ay, u.by, u.cy}));
        // degenerate UVs / runaway mappings are the validator's department
        u.skip = std::fabs(area2) < 1e-6f ||
                 (int64_t)(u.x1 - u.x0) * (u.y1 - u.y0) >
                     (int64_t)size * size * 8;
        u.inv = u.skip ? 0.0f : 1.0f / area2;
        return u;
    };
    auto baryAt = [](const TriUv& u, float px, float py, float& bu, float& bv) {
        bu = ((px - u.ax) * (u.cy - u.ay) - (py - u.ay) * (u.cx - u.ax)) * u.inv;
        bv = ((u.bx - u.ax) * (py - u.ay) - (u.by - u.ay) * (px - u.ax)) * u.inv;
    };
    auto emit = [&](int t, int tx, int ty, float bu, float bv) {
        const float* a = &m.verts[(size_t)(t * 3 + 0) * 8];
        const float* b = &m.verts[(size_t)(t * 3 + 1) * 8];
        const float* c = &m.verts[(size_t)(t * 3 + 2) * 8];
        GSample s;
        s.texel = ty * size + tx;
        s.tri = t;
        s.bu = bu, s.bv = bv;
        const float bw = 1.0f - bu - bv;
        for (int k = 0; k < 3; ++k) {
            s.pos[k] = a[k] * bw + b[k] * bu + c[k] * bv;
            s.nrm[k] = a[3 + k] * bw + b[3 + k] * bu + c[3 + k] * bv;
        }
        if (!v3norm(s.nrm)) s.nrm[1] = 1.0f;
        g.samples.push_back(s);
        occupied[(size_t)s.texel] = 1;
        g.mask[(size_t)s.texel] = 255;
    };

    for (int t = 0; t < tris; ++t) {
        if (!m.paintTri.empty() && !m.paintTri[t]) continue;
        const TriUv u = triUv(t);
        if (u.skip) continue;
        for (int y = u.y0; y <= u.y1; ++y) {
            for (int x = u.x0; x <= u.x1; ++x) {
                const int tx = wrapc(x, size), ty = wrapc(y, size);
                for (int sy = 0; sy < ss; ++sy)
                    for (int sx = 0; sx < ss; ++sx) {
                        float bu, bv;
                        baryAt(u, x + (sx + 0.5f) / ss, y + (sy + 0.5f) / ss,
                               bu, bv);
                        if (bu < 0.0f || bv < 0.0f || bu + bv > 1.0f) continue;
                        emit(t, tx, ty, bu, bv);
                    }
            }
        }
    }

    // pass 2: nearest-point conservative fill of empty grazed texels
    for (int t = 0; t < tris; ++t) {
        if (!m.paintTri.empty() && !m.paintTri[t]) continue;
        const TriUv u = triUv(t);
        if (u.skip) continue;
        for (int y = u.y0; y <= u.y1; ++y) {
            for (int x = u.x0; x <= u.x1; ++x) {
                const int tx = wrapc(x, size), ty = wrapc(y, size);
                if (occupied[(size_t)ty * size + tx]) continue;
                const float px = x + 0.5f, py = y + 0.5f;
                float bu, bv;
                baryAt(u, px, py, bu, bv);
                if (!(bu >= 0.0f && bv >= 0.0f && bu + bv <= 1.0f)) {
                    // nearest point on the triangle boundary to the center
                    float bestD = 1e30f, cx = u.ax, cy = u.ay;
                    const float ex[3] = {u.ax, u.bx, u.cx};
                    const float ey[3] = {u.ay, u.by, u.cy};
                    for (int e = 0; e < 3; ++e) {
                        const float x1 = ex[e], y1 = ey[e];
                        const float x2 = ex[(e + 1) % 3], y2 = ey[(e + 1) % 3];
                        const float dx = x2 - x1, dy = y2 - y1;
                        const float l2 = dx * dx + dy * dy;
                        float s = l2 < 1e-12f
                                      ? 0.0f
                                      : ((px - x1) * dx + (py - y1) * dy) / l2;
                        s = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
                        const float qx = x1 + dx * s, qy = y1 + dy * s;
                        const float d =
                            (qx - px) * (qx - px) + (qy - py) * (qy - py);
                        if (d < bestD) bestD = d, cx = qx, cy = qy;
                    }
                    // half a texel (squared) reach: the square is grazed
                    if (bestD > 0.5f) continue;
                    baryAt(u, cx, cy, bu, bv);
                }
                bu = bu < 0.0f ? 0.0f : bu;
                bv = bv < 0.0f ? 0.0f : bv;
                if (bu + bv > 1.0f) {
                    const float s = 1.0f / (bu + bv);
                    bu *= s, bv *= s;
                }
                emit(t, tx, ty, bu, bv);
            }
        }
    }

    std::stable_sort(g.samples.begin(), g.samples.end(),
                     [](const GSample& a, const GSample& b) {
                         return a.texel < b.texel;
                     });
    g.offset.assign((size_t)size * size + 1, 0);
    for (const GSample& s : g.samples) ++g.offset[(size_t)s.texel + 1];
    for (size_t i = 1; i < g.offset.size(); ++i) g.offset[i] += g.offset[i - 1];
}

// --- hemisphere sampling -------------------------------------------------------

inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t s) {
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ s * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x9e3779b1u;
    h ^= h >> 16;
    return h;
}

// Deterministic cosine-weighted golden-angle spiral: direction i of K in
// tangent space (z up). The per-texel rotation phi0 decorrelates neighbors:
// the spiral alone bands, random alone hisses, spiral + rotation is clean at
// low ray counts (the aobake recipe). Any prefix of the sequence is itself
// well distributed - that is what makes the progressive rounds honest.
inline void spiralDir(int i, int K, float phi0, float* out) {
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    const float u = (i + 0.5f) / K;
    const float r = std::sqrt(u);
    const float phi = golden * i + phi0;
    out[0] = r * std::cos(phi);
    out[1] = r * std::sin(phi);
    out[2] = std::sqrt(1.0f - u);
}

inline void tangentFrame(const float* n, float* tx, float* ty) {
    if (std::fabs(n[1]) < 0.9f) {
        tx[0] = n[2], tx[1] = 0.0f, tx[2] = -n[0];
    } else {
        tx[0] = 1.0f - n[0] * n[0];
        tx[1] = -n[0] * n[1];
        tx[2] = -n[0] * n[2];
    }
    v3norm(tx);
    v3cross(n, tx, ty);
}

// --- dilate --------------------------------------------------------------------

// Flood-dilate every map `padding` texels out from the mask: each ring
// averages its filled 8-neighbors (wrapping - islands may hug the border).
// Beyond the ring the neutral background stays.
void dilateMaps(Maps& m, int padding) {
    const int w = m.w, h = m.h;
    std::vector<uint8_t> filled = m.mask;
    std::vector<int> ring;
    for (int pass = 0; pass < padding; ++pass) {
        ring.clear();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (filled[(size_t)y * w + x]) continue;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx)
                        if (filled[(size_t)wrapc(y + dy, h) * w +
                                   wrapc(x + dx, w)]) {
                            ring.push_back(y * w + x);
                            dy = 2;
                            break;
                        }
                }
            }
        if (ring.empty()) break;
        for (int idx : ring) {
            const int x = idx % w, y = idx / w;
            int n = 0, ao = 0, th = 0, cv = 0;
            int be[3] = {0, 0, 0}, po[3] = {0, 0, 0}, no[3] = {0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const size_t j =
                        (size_t)wrapc(y + dy, h) * w + wrapc(x + dx, w);
                    if (!filled[j]) continue;
                    ++n;
                    ao += m.ao[j];
                    th += m.thickness[j];
                    cv += m.curvature[j];
                    for (int k = 0; k < 3; ++k) {
                        be[k] += m.bent[j * 3 + k];
                        po[k] += m.position[j * 3 + k];
                        no[k] += m.normal[j * 3 + k];
                    }
                }
            if (!n) continue;
            const size_t j = (size_t)idx;
            m.ao[j] = (uint8_t)(ao / n);
            m.thickness[j] = (uint8_t)(th / n);
            m.curvature[j] = (uint8_t)(cv / n);
            for (int k = 0; k < 3; ++k) {
                m.bent[j * 3 + k] = (uint8_t)(be[k] / n);
                m.position[j * 3 + k] = (uint8_t)(po[k] / n);
                m.normal[j * 3 + k] = (uint8_t)(no[k] / n);
            }
        }
        for (int idx : ring) filled[(size_t)idx] = 255;
    }
}

}  // namespace

// --- mesh input builders --------------------------------------------------------

MeshInput fromModel(const objparser::Model& m, const std::string& entryName) {
    MeshInput out;
    bool posIdxOk = true;
    for (const objparser::Submesh& s : m.submeshes) {
        const size_t n = (s.verts.size() / 24) * 3;  // whole triangles only
        const bool paint = entryName.empty() || s.material == entryName;
        out.verts.insert(out.verts.end(), s.verts.begin(),
                         s.verts.begin() + n * 8);
        if (s.posIdx.size() >= n)
            out.posIdx.insert(out.posIdx.end(), s.posIdx.begin(),
                              s.posIdx.begin() + n);
        else
            posIdxOk = false;
        out.paintTri.insert(out.paintTri.end(), n / 3, paint ? 1 : 0);
    }
    if (!posIdxOk || out.posIdx.size() != out.verts.size() / 8)
        out.posIdx.clear();  // weld by position instead
    return out;
}

MeshInput fromPrimitive(int shape, int detail) {
    MeshInput out;
    switch (shape) {
        case 0: out.verts = primmesh::unitBox(detail); break;
        case 2: out.verts = primmesh::unitCylinder(detail); break;
        case 3: out.verts = primmesh::unitCone(detail); break;
        default: out.verts = primmesh::unitSphere(detail); break;
    }
    return out;
}

// --- Baker -----------------------------------------------------------------------

// Everything derived from (mesh, high, size, supersample, cage): the
// rasterized + projected gbuffer, the occluder BVH and the ray-free static
// maps. Sampling-only param changes (rays, distance, seed, backface,
// padding) reuse it - that is what makes dragging the distance slider
// feel instant.
struct Baker::Prepared {
    uint64_t key = 0;
    GBuffer g;
    Bvh bvh;            // occluder geometry: high mesh when present, else low
    float diag = 1.0f;  // occluder AABB diagonal
    Maps statics;       // mask/position/normal/curvature + neutral ray maps
};

namespace {

uint64_t prepKey(const MeshInput& mesh, const MeshInput& high, int size, int ss,
                 float cage) {
    if (!mesh.signature) return 0;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(mesh.signature);
    mix(high.signature + 1);
    mix((uint64_t)size);
    mix((uint64_t)ss);
    uint32_t cbits;
    std::memcpy(&cbits, &cage, 4);
    mix(cbits);
    return h;
}

void fillBvh(const MeshInput& m, const std::vector<int>& weld,
             const std::vector<float>& smoothNrm, Bvh& b) {
    const int tris = m.triCount();
    b.tv.resize((size_t)tris * 9);
    b.tn.resize((size_t)tris * 9);
    for (int t = 0; t < tris; ++t)
        for (int c = 0; c < 3; ++c) {
            std::memcpy(&b.tv[((size_t)t * 3 + c) * 3],
                        &m.verts[(size_t)(t * 3 + c) * 8], 12);
            std::memcpy(&b.tn[((size_t)t * 3 + c) * 3],
                        &smoothNrm[(size_t)weld[t * 3 + c] * 3], 12);
        }
    bvhBuild(b);
}

// Interpolates a welded per-position attribute at a sample's barycentrics.
inline float interpWeld(const std::vector<float>& attr,
                        const std::vector<int>& weld, int tri, float bu,
                        float bv) {
    const float bw = 1.0f - bu - bv;
    return attr[weld[tri * 3]] * bw + attr[weld[tri * 3 + 1]] * bu +
           attr[weld[tri * 3 + 2]] * bv;
}

inline void interpWeld3(const std::vector<float>& attr,
                        const std::vector<int>& weld, int tri, float bu,
                        float bv, float* out) {
    const float bw = 1.0f - bu - bv;
    for (int k = 0; k < 3; ++k)
        out[k] = attr[(size_t)weld[tri * 3] * 3 + k] * bw +
                 attr[(size_t)weld[tri * 3 + 1] * 3 + k] * bu +
                 attr[(size_t)weld[tri * 3 + 2] * 3 + k] * bv;
}

}  // namespace

void Baker::start(MeshInput mesh, MeshInput high, const Params& p) {
    cancel();
    cancel_.store(false);
    running_.store(true);
    samplesDone_.store(0);
    samplesTarget_.store(std::max(1, p.samples));
    {
        std::lock_guard<std::mutex> lk(mapsMutex_);
        error_.clear();
    }
    worker_ = std::thread(
        [this, m = std::move(mesh), hi = std::move(high), p]() mutable {
            run(std::move(m), std::move(hi), p);
            running_.store(false);
        });
}

void Baker::cancel() {
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
    running_.store(false);
}

float Baker::progress() const {
    return (float)samplesDone_.load() / (float)samplesTarget_.load();
}

Maps Baker::snapshot() const {
    std::lock_guard<std::mutex> lk(mapsMutex_);
    return maps_;
}

std::string Baker::error() const {
    std::lock_guard<std::mutex> lk(mapsMutex_);
    return error_;
}

void Baker::run(MeshInput mesh, MeshInput high, Params p) {
    // clamp params to sanity
    int size = 32;
    while (size < p.size && size < 1024) size <<= 1;
    p.size = size;
    p.samples = std::max(4, std::min(p.samples, 1024));
    p.supersample = p.supersample >= 4 ? 4 : p.supersample >= 2 ? 2 : 1;
    // subsample memory guard: size^2 * ss^2 within ~4M surface samples
    while (p.supersample > 1 &&
           (int64_t)p.size * p.size * p.supersample * p.supersample > 4194304)
        p.supersample >>= 1;
    p.padding = std::max(0, std::min(p.padding, 16));

    if (mesh.empty() || mesh.triCount() == 0) {
        std::lock_guard<std::mutex> lk(mapsMutex_);
        error_ = "No mesh to bake";
        return;
    }

    // --- prepare (or reuse) the gbuffer + BVH --------------------------------
    std::shared_ptr<Prepared> prep = cache_;
    const uint64_t key = prepKey(mesh, high, p.size, p.supersample, p.cageOffset);
    if (!prep || !key || prep->key != key) {
        prep = std::make_shared<Prepared>();
        prep->key = key;

        const std::vector<int> weldLow = weldCorners(mesh);
        std::vector<float> lowNrm, lowCurv;
        smoothNormalsAndCurvature(mesh, weldLow, lowNrm, lowCurv);
        if (cancel_.load()) return;

        rasterize(mesh, p.size, p.supersample, prep->g);
        if (cancel_.load()) return;
        for (GSample& s : prep->g.samples)
            s.curv = interpWeld(lowCurv, weldLow, s.tri, s.bu, s.bv);

        // occluder geometry: the high mesh when present, else the low mesh
        const bool hasHigh = !high.empty() && high.triCount() > 0;
        if (hasHigh) {
            const std::vector<int> weldHigh = weldCorners(high);
            std::vector<float> highNrm, highCurv;
            smoothNormalsAndCurvature(high, weldHigh, highNrm, highCurv);
            if (cancel_.load()) return;
            fillBvh(high, weldHigh, highNrm, prep->bvh);
            if (cancel_.load()) return;
            float ext[3];
            v3sub(prep->bvh.bmax, prep->bvh.bmin, ext);
            prep->diag = std::max(v3len(ext), 1e-6f);

            // cage projection: relocate every texel point onto the high mesh
            // along the SMOOTHED low normal (flat normals crack at hard
            // edges), from the cage inward - the first hit is the detail
            // surface the texel represents. Misses keep the low point.
            float cage = p.cageOffset;
            if (cage <= 0.0f) cage = 0.02f * prep->diag;
            std::vector<float> cornerCurv((size_t)high.triCount() * 3);
            for (int t = 0; t < high.triCount(); ++t)
                for (int c = 0; c < 3; ++c)
                    cornerCurv[(size_t)t * 3 + c] = highCurv[weldHigh[t * 3 + c]];
            size_t si = 0;
            for (GSample& s : prep->g.samples) {
                if (((si++) & 4095) == 0 && cancel_.load()) return;
                float dir[3];
                interpWeld3(lowNrm, weldLow, s.tri, s.bu, s.bv, dir);
                if (!v3norm(dir)) std::memcpy(dir, s.nrm, sizeof dir);
                const float co[3] = {s.pos[0] + dir[0] * cage,
                                     s.pos[1] + dir[1] * cage,
                                     s.pos[2] + dir[2] * cage};
                const float cd[3] = {-dir[0], -dir[1], -dir[2]};
                Hit h;
                if (!bvhTrace(prep->bvh, co, cd, 2.0f * cage, true, h)) continue;
                for (int k = 0; k < 3; ++k) s.pos[k] = co[k] + cd[k] * h.t;
                const float* n0 = &prep->bvh.tn[(size_t)h.tri * 9];
                const float bw = 1.0f - h.u - h.v;
                float hn[3];
                for (int k = 0; k < 3; ++k)
                    hn[k] = n0[k] * bw + n0[3 + k] * h.u + n0[6 + k] * h.v;
                if (v3norm(hn)) {
                    // keep the high normal on the low surface's side
                    if (v3dot(hn, dir) < 0.0f)
                        hn[0] = -hn[0], hn[1] = -hn[1], hn[2] = -hn[2];
                    std::memcpy(s.nrm, hn, sizeof s.nrm);
                }
                s.curv = cornerCurv[(size_t)h.tri * 3] * bw +
                         cornerCurv[(size_t)h.tri * 3 + 1] * h.u +
                         cornerCurv[(size_t)h.tri * 3 + 2] * h.v;
            }
        } else {
            fillBvh(mesh, weldLow, lowNrm, prep->bvh);
            if (cancel_.load()) return;
            float ext[3];
            v3sub(prep->bvh.bmax, prep->bvh.bmin, ext);
            prep->diag = std::max(v3len(ext), 1e-6f);
        }

        // static maps (ray-free): mask, position, OS normal, curvature; the
        // ray maps start at their neutral values
        Maps& st = prep->statics;
        st.w = st.h = p.size;
        const size_t px = (size_t)p.size * p.size;
        st.mask = prep->g.mask;
        st.ao.assign(px, 255);
        st.thickness.assign(px, 255);
        st.curvature.assign(px, 128);
        st.bent.assign(px * 3, 128);
        st.position.assign(px * 3, 0);
        st.normal.assign(px * 3, 128);
        for (size_t i = 0; i < px; ++i)
            st.bent[i * 3 + 2] = st.normal[i * 3 + 2] = 255;
        float extent[3];
        v3sub(prep->bvh.bmax, prep->bvh.bmin, extent);
        for (int k = 0; k < 3; ++k) extent[k] = std::max(extent[k], 1e-6f);
        for (size_t t = 0; t < px; ++t) {
            const int cnt = prep->g.count((int)t);
            if (!cnt) continue;
            float P[3] = {0, 0, 0}, N[3] = {0, 0, 0}, cv = 0.0f;
            for (int i = prep->g.offset[t]; i < prep->g.offset[t + 1]; ++i) {
                const GSample& s = prep->g.samples[i];
                for (int k = 0; k < 3; ++k) P[k] += s.pos[k], N[k] += s.nrm[k];
                cv += s.curv;
            }
            for (int k = 0; k < 3; ++k) P[k] /= cnt;
            cv /= cnt;
            if (!v3norm(N)) N[1] = 1.0f;
            for (int k = 0; k < 3; ++k) {
                const float pn =
                    (P[k] - prep->bvh.bmin[k]) / extent[k];
                st.position[t * 3 + k] = (uint8_t)(255.0f *
                    (pn < 0.0f ? 0.0f : pn > 1.0f ? 1.0f : pn) + 0.5f);
                st.normal[t * 3 + k] = (uint8_t)(127.5f + 127.0f * N[k]);
            }
            st.curvature[t] = (uint8_t)(127.5f + 127.0f * cv);
        }
        cache_ = prep;
    }

    // --- sampling rounds -------------------------------------------------------
    const GBuffer& g = prep->g;
    const size_t px = (size_t)p.size * p.size;
    const float R = p.maxDist > 0.0f ? p.maxDist : 0.5f * prep->diag;
    const float eps = 1e-3f * prep->diag;

    std::vector<float> aoAcc(px, 0.0f), thAcc(px, 0.0f);
    std::vector<float> bentAcc(px * 3, 0.0f);
    std::vector<int> live;
    live.reserve(px / 2);
    for (size_t t = 0; t < px; ++t)
        if (g.count((int)t)) live.push_back((int)t);

    const int K = p.samples;
    unsigned hw = std::thread::hardware_concurrency();
    if (hw < 1) hw = 1;
    if (hw > 32) hw = 32;

    auto sampleRange = [&](int n0, int n1, size_t li0, size_t li1) {
        float tx[3], ty[3], sd[3], d[3], dt[3], o[3], oi[3];
        for (size_t li = li0; li < li1; ++li) {
            if ((li & 255) == 0 && cancel_.load()) return;
            const int t = live[li];
            const int c0 = g.offset[t], cn = g.offset[t + 1] - g.offset[t];
            const float phi0 =
                (hash3((uint32_t)(t % p.size), (uint32_t)(t / p.size), p.seed) >>
                 8) *
                (2.0f * kPi / 16777216.0f);
            float occSum = 0.0f, thSum = 0.0f;
            float bent[3] = {0, 0, 0};
            for (int i = n0; i < n1; ++i) {
                const GSample& s = g.samples[c0 + (i % cn)];
                tangentFrame(s.nrm, tx, ty);
                spiralDir(i, K, phi0, sd);
                for (int k = 0; k < 3; ++k) {
                    d[k] = tx[k] * sd[0] + ty[k] * sd[1] + s.nrm[k] * sd[2];
                    dt[k] = tx[k] * sd[0] + ty[k] * sd[1] - s.nrm[k] * sd[2];
                    o[k] = s.pos[k] + s.nrm[k] * eps;
                    oi[k] = s.pos[k] - s.nrm[k] * eps;
                }
                Hit h;
                if (bvhTrace(prep->bvh, o, d, R, p.backface, h)) {
                    // nearby hits occlude more: linear falloff toward R
                    occSum += 1.0f - h.t / R;
                } else {
                    for (int k = 0; k < 3; ++k) bent[k] += d[k];
                }
                // thickness: the same spiral mirrored below the surface,
                // both facings solid (the ray runs inside the mesh)
                Hit ht;
                thSum +=
                    bvhTrace(prep->bvh, oi, dt, R, true, ht) ? ht.t / R : 1.0f;
            }
            aoAcc[t] += occSum;
            thAcc[t] += thSum;
            for (int k = 0; k < 3; ++k) bentAcc[(size_t)t * 3 + k] += bent[k];
        }
    };

    int done = 0;
    int round = std::min(8, K);
    while (done < K && !cancel_.load()) {
        const int n0 = done, n1 = std::min(K, done + round);
        // fixed texel ranges per thread: each texel's accumulation order is
        // core-count independent - determinism survives parallelism
        std::vector<std::thread> pool;
        const size_t chunk =
            live.empty() ? 1 : (live.size() + hw - 1) / (size_t)hw;
        for (unsigned w = 1; w < hw && chunk * w < live.size(); ++w)
            pool.emplace_back(sampleRange, n0, n1, chunk * w,
                              std::min(live.size(), chunk * (w + 1)));
        sampleRange(n0, n1, 0, std::min(live.size(), chunk));
        for (auto& th : pool) th.join();
        if (cancel_.load()) return;
        done = n1;
        samplesDone_.store(done);
        round = std::min(round * 2, 64);

        // publish: statics + the ray maps at the current sample count
        Maps snap = prep->statics;
        const float invN = 1.0f / done;
        for (int t : live) {
            float occ = aoAcc[t] * invN;
            occ = occ < 0.0f ? 0.0f : occ > 1.0f ? 1.0f : occ;
            snap.ao[t] = (uint8_t)(255.0f * (1.0f - occ) + 0.5f);
            float th = thAcc[t] * invN;
            th = th < 0.0f ? 0.0f : th > 1.0f ? 1.0f : th;
            snap.thickness[t] = (uint8_t)(255.0f * th + 0.5f);
            float bv[3] = {bentAcc[(size_t)t * 3], bentAcc[(size_t)t * 3 + 1],
                           bentAcc[(size_t)t * 3 + 2]};
            if (!v3norm(bv)) {
                // fully occluded texel: fall back to the surface normal
                for (int k = 0; k < 3; ++k)
                    bv[k] = (snap.normal[(size_t)t * 3 + k] - 127.5f) / 127.0f;
            }
            for (int k = 0; k < 3; ++k)
                snap.bent[(size_t)t * 3 + k] =
                    (uint8_t)(127.5f + 127.0f * bv[k]);
        }
        snap.samplesDone = done;
        dilateMaps(snap, p.padding);
        {
            std::lock_guard<std::mutex> lk(mapsMutex_);
            maps_ = std::move(snap);
        }
        version_.fetch_add(1);
    }
}

}  // namespace matbake

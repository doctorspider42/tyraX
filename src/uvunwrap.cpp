#include "uvunwrap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

namespace uvunwrap {

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool norm(V3& a) {
    const float l = std::sqrt(dot(a, a));
    if (l < 1e-12f) return false;
    a.x /= l, a.y /= l, a.z /= l;
    return true;
}

// Newell's method - robust plane normal for arbitrary polygons.
V3 newell(const std::vector<int>& idx, const std::vector<V3>& pos) {
    V3 n;
    for (size_t i = 0; i < idx.size(); ++i) {
        const V3& a = pos[idx[i]];
        const V3& b = pos[idx[(i + 1) % idx.size()]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

// The shared smart-project core: positions + faces (index lists) in, one UV
// per face corner out. Chart growing, planar projection, tightest-bbox
// rotation, uniform-density shelf packing - see the header's summary.
bool unwrapCore(const std::vector<V3>& pos,
                const std::vector<std::vector<int>>& faceIdx, const Params& p,
                std::vector<std::vector<std::pair<float, float>>>& outUv,
                Stats& stats, std::string& error) {
    struct FaceInfo {
        V3 normal;
        float area = 0.0f;
        int chart = -1;
    };
    std::vector<FaceInfo> faces(faceIdx.size());
    for (size_t i = 0; i < faceIdx.size(); ++i) {
        faces[i].normal = newell(faceIdx[i], pos);
        faces[i].area =
            0.5f * std::sqrt(dot(faces[i].normal, faces[i].normal));
        if (!norm(faces[i].normal)) faces[i].normal = {0, 1, 0};
    }
    if (faces.empty()) {
        error = "no faces to unwrap";
        return false;
    }

    // adjacency over shared edges
    std::map<std::pair<int, int>, std::vector<int>> edges;
    for (size_t fi = 0; fi < faceIdx.size(); ++fi)
        for (size_t k = 0; k < faceIdx[fi].size(); ++k) {
            int a = faceIdx[fi][k];
            int b = faceIdx[fi][(k + 1) % faceIdx[fi].size()];
            if (a > b) std::swap(a, b);
            if (a != b) edges[{a, b}].push_back((int)fi);
        }

    // chart growing: biggest unclaimed face seeds, BFS within the angle
    const float cosThresh =
        std::cos(std::max(1.0f, std::min(p.angleDeg, 89.0f)) * 3.14159265f /
                 180.0f);
    std::vector<int> order(faces.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return faces[a].area > faces[b].area;
    });
    int chartCount = 0;
    std::vector<V3> chartNormal;
    for (int seed : order) {
        if (faces[seed].chart >= 0) continue;
        const int chart = chartCount++;
        chartNormal.push_back(faces[seed].normal);
        std::vector<int> queue{seed};
        faces[seed].chart = chart;
        while (!queue.empty()) {
            const int fi = queue.back();
            queue.pop_back();
            for (size_t k = 0; k < faceIdx[fi].size(); ++k) {
                int a = faceIdx[fi][k];
                int b = faceIdx[fi][(k + 1) % faceIdx[fi].size()];
                if (a > b) std::swap(a, b);
                if (a == b) continue;
                for (int nf : edges[{a, b}]) {
                    if (faces[nf].chart >= 0) continue;
                    if (dot(faces[nf].normal, faces[seed].normal) < cosThresh)
                        continue;
                    faces[nf].chart = chart;
                    queue.push_back(nf);
                }
            }
        }
    }

    // per chart: planar projection + tightest-bbox rotation
    struct Chart {
        std::map<int, std::pair<float, float>> uv;  // position index -> 2D
        float w = 0, h = 0;                          // bbox (world units)
        float packX = 0, packY = 0;                  // placement (uv units)
    };
    std::vector<Chart> charts(chartCount);
    for (int c = 0; c < chartCount; ++c) {
        V3 n = chartNormal[c];
        V3 up = std::fabs(n.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0};
        V3 u = cross(up, n);
        if (!norm(u)) u = {1, 0, 0};
        const V3 v = cross(n, u);
        Chart& ch = charts[c];
        for (size_t fi = 0; fi < faceIdx.size(); ++fi) {
            if (faces[fi].chart != c) continue;
            for (int vi : faceIdx[fi])
                if (!ch.uv.count(vi))
                    ch.uv[vi] = {dot(pos[vi], u), dot(pos[vi], v)};
        }
        float bestA = 1e30f, bestRot = 0.0f;
        for (int deg = 0; deg < 90; deg += 5) {
            const float rad = deg * 3.14159265f / 180.0f;
            const float cs = std::cos(rad), sn = std::sin(rad);
            float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
            for (const auto& [vi, q] : ch.uv) {
                const float x = q.first * cs - q.second * sn;
                const float y = q.first * sn + q.second * cs;
                x0 = std::min(x0, x), x1 = std::max(x1, x);
                y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
            const float a = (x1 - x0) * (y1 - y0);
            if (a < bestA) bestA = a, bestRot = rad;
        }
        const float cs = std::cos(bestRot), sn = std::sin(bestRot);
        float x0 = 1e30f, y0 = 1e30f;
        for (auto& [vi, q] : ch.uv) {
            const float x = q.first * cs - q.second * sn;
            const float y = q.first * sn + q.second * cs;
            q = {x, y};
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
        }
        for (auto& [vi, q] : ch.uv) {
            q.first -= x0;
            q.second -= y0;
            ch.w = std::max(ch.w, q.first);
            ch.h = std::max(ch.h, q.second);
        }
        if (ch.h > ch.w) {  // wide charts pack better lying down
            for (auto& [vi, q] : ch.uv) q = {q.second, ch.w - q.first};
            std::swap(ch.w, ch.h);
        }
    }

    // pack: one global scale (uniform texel density), shelf rows
    const float margin =
        (float)p.marginPx / (float)std::max(64, p.marginRefSize);
    std::vector<int> chOrder(chartCount);
    for (int i = 0; i < chartCount; ++i) chOrder[i] = i;
    std::stable_sort(chOrder.begin(), chOrder.end(), [&](int a, int b) {
        if (charts[a].h != charts[b].h) return charts[a].h > charts[b].h;
        return charts[a].w > charts[b].w;
    });
    auto tryPack = [&](float s) {
        float shelfY = margin, shelfH = 0, x = margin;
        for (int c : chOrder) {
            const float w = charts[c].w * s, h = charts[c].h * s;
            if (w > 1.0f - 2 * margin || h > 1.0f - 2 * margin) return false;
            if (x + w + margin > 1.0f) {
                shelfY += shelfH + margin;
                x = margin;
                shelfH = 0;
            }
            if (shelfY + h + margin > 1.0f) return false;
            charts[c].packX = x;
            charts[c].packY = shelfY;
            shelfH = std::max(shelfH, h);
            x += w + margin;
        }
        return true;
    };
    float maxDim = 1e-6f;
    for (const Chart& c : charts) maxDim = std::max({maxDim, c.w, c.h});
    float lo = 0.0f, hi = (1.0f - 2 * margin) / maxDim, scale = 0.0f;
    for (int it = 0; it < 24; ++it) {
        const float mid = (lo + hi) * 0.5f;
        if (tryPack(mid)) {
            scale = mid;
            lo = mid;
        } else {
            hi = mid;
        }
    }
    if (scale <= 0.0f || !tryPack(scale)) {
        error = "packing failed (degenerate geometry?)";
        return false;
    }

    outUv.resize(faceIdx.size());
    for (size_t fi = 0; fi < faceIdx.size(); ++fi) {
        outUv[fi].resize(faceIdx[fi].size());
        const Chart& ch = charts[faces[fi].chart];
        for (size_t k = 0; k < faceIdx[fi].size(); ++k) {
            const auto& q = ch.uv.at(faceIdx[fi][k]);
            outUv[fi][k] = {ch.packX + q.first * scale,
                            ch.packY + q.second * scale};
        }
    }
    stats.faces = (int)faceIdx.size();
    stats.charts = chartCount;
    stats.coverage = 0.0f;
    for (const Chart& c : charts)
        stats.coverage += (c.w * scale) * (c.h * scale);
    return true;
}

}  // namespace

bool unwrapObjFile(const std::string& path, const Params& p,
                   std::string& error, Stats* stats) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read the file";
        return false;
    }
    std::vector<std::string> lines;
    {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    // parse: positions + faces (token structure preserved)
    struct Face {
        size_t line;
        std::vector<int> v;
        std::vector<std::string> tokens;
    };
    std::vector<V3> pos;
    std::vector<Face> faces;
    std::vector<char> isVt(lines.size(), 0);
    size_t firstFaceLine = lines.size();
    for (size_t li = 0; li < lines.size(); ++li) {
        std::istringstream ss(lines[li]);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            V3 v;
            ss >> v.x >> v.y >> v.z;
            pos.push_back(v);
        } else if (tag == "vt") {
            isVt[li] = 1;  // the old pool - dropped on rewrite
        } else if (tag == "f") {
            if (firstFaceLine == lines.size()) firstFaceLine = li;
            Face f;
            f.line = li;
            std::string tok;
            bool ok = true;
            while (ss >> tok) {
                const int vi = std::atoi(tok.c_str());
                // negative = relative to the positions read SO FAR
                const int abs = vi > 0 ? vi - 1 : (int)pos.size() + vi;
                if (abs < 0 || abs >= (int)pos.size()) {
                    ok = false;
                    break;
                }
                f.v.push_back(abs);
                f.tokens.push_back(tok);
            }
            if (!ok || f.v.size() < 3) continue;  // left verbatim
            faces.push_back(std::move(f));
        }
    }
    if (faces.empty()) {
        error = "no faces to unwrap";
        return false;
    }

    std::vector<std::vector<int>> faceIdx(faces.size());
    for (size_t i = 0; i < faces.size(); ++i) faceIdx[i] = faces[i].v;
    std::vector<std::vector<std::pair<float, float>>> uv;
    Stats st;
    if (!unwrapCore(pos, faceIdx, p, uv, st, error)) return false;

    // vt pool: dedupe identical pairs (corners of one chart position share)
    std::map<std::pair<float, float>, int> vtIndex;
    std::vector<std::pair<float, float>> vts;
    auto vtOf = [&](const std::pair<float, float>& q) {
        auto it = vtIndex.find(q);
        if (it != vtIndex.end()) return it->second;
        vts.push_back(q);
        vtIndex[q] = (int)vts.size();
        return (int)vts.size();
    };

    std::map<size_t, std::string> newFaceLine;
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const Face& f = faces[fi];
        std::string out = "f";
        for (size_t k = 0; k < f.tokens.size(); ++k) {
            const std::string& tok = f.tokens[k];
            const size_t s1 = tok.find('/');
            const std::string vPart =
                s1 == std::string::npos ? tok : tok.substr(0, s1);
            std::string vnPart;
            if (s1 != std::string::npos) {
                const size_t s2 = tok.find('/', s1 + 1);
                if (s2 != std::string::npos) vnPart = tok.substr(s2 + 1);
            }
            out += " " + vPart + "/" + std::to_string(vtOf(uv[fi][k]));
            if (!vnPart.empty()) out += "/" + vnPart;
        }
        newFaceLine[f.line] = std::move(out);
    }

    std::ostringstream body;
    for (size_t li = 0; li < lines.size(); ++li) {
        if (isVt[li]) continue;
        if (li == firstFaceLine) {
            char buf[64];
            for (const auto& [u, v] : vts) {
                std::snprintf(buf, sizeof(buf), "vt %.6g %.6g", u, v);
                body << buf << "\n";
            }
        }
        auto it = newFaceLine.find(li);
        body << (it != newFaceLine.end() ? it->second : lines[li]) << "\n";
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write the file";
        return false;
    }
    out << body.str();

    if (stats) *stats = st;
    return true;
}

bool unwrapTriangles(const std::vector<float>& corners,
                     std::vector<float>& outUv, const Params& p,
                     std::string& error, Stats* stats) {
    const size_t cornerCount = corners.size() / 3;
    if (cornerCount < 3) {
        error = "no triangles to unwrap";
        return false;
    }
    // weld corners by quantized position so charts grow across the soup's
    // duplicated vertices (matbake's welding recipe)
    std::map<std::tuple<int, int, int>, int> keys;
    std::vector<int> weld(cornerCount);
    std::vector<V3> pos;
    for (size_t i = 0; i < cornerCount; ++i) {
        const float* c = &corners[i * 3];
        const auto key = std::make_tuple((int)std::lround(c[0] * 8192.0f),
                                         (int)std::lround(c[1] * 8192.0f),
                                         (int)std::lround(c[2] * 8192.0f));
        auto it = keys.find(key);
        if (it == keys.end()) {
            it = keys.emplace(key, (int)pos.size()).first;
            pos.push_back({c[0], c[1], c[2]});
        }
        weld[i] = it->second;
    }
    const size_t triCount = cornerCount / 3;
    std::vector<std::vector<int>> faceIdx(triCount);
    for (size_t t = 0; t < triCount; ++t)
        faceIdx[t] = {weld[t * 3], weld[t * 3 + 1], weld[t * 3 + 2]};

    std::vector<std::vector<std::pair<float, float>>> uv;
    Stats st;
    if (!unwrapCore(pos, faceIdx, p, uv, st, error)) return false;
    outUv.resize(cornerCount * 2);
    for (size_t t = 0; t < triCount; ++t)
        for (int k = 0; k < 3; ++k) {
            outUv[(t * 3 + k) * 2] = uv[t][k].first;
            outUv[(t * 3 + k) * 2 + 1] = uv[t][k].second;
        }
    if (stats) *stats = st;
    return true;
}

}  // namespace uvunwrap

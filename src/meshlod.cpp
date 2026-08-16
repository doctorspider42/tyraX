#include "meshlod.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

namespace meshlod {

namespace {

struct Quadric {
    double m[10] = {};  // symmetric 4x4: xx xy xz xw yy yz yw zz zw ww
    void addPlane(double a, double b, double c, double d) {
        m[0] += a * a; m[1] += a * b; m[2] += a * c; m[3] += a * d;
        m[4] += b * b; m[5] += b * c; m[6] += b * d;
        m[7] += c * c; m[8] += c * d; m[9] += d * d;
    }
    void add(const Quadric& q) {
        for (int i = 0; i < 10; ++i) m[i] += q.m[i];
    }
    double error(const float* v) const {
        const double x = v[0], y = v[1], z = v[2];
        return m[0] * x * x + 2 * m[1] * x * y + 2 * m[2] * x * z +
               2 * m[3] * x + m[4] * y * y + 2 * m[5] * y * z + 2 * m[6] * y +
               m[7] * z * z + 2 * m[8] * z + m[9];
    }
};

}  // namespace

Mesh weld(const float* positions, const float* normals, const float* uvs,
          const unsigned char* joints, const unsigned char* weights,
          size_t count, bool keyNormals) {
    Mesh w;
    w.hasUv = uvs != nullptr;
    w.hasSkin = joints != nullptr && weights != nullptr;
    std::map<std::string, uint32_t> lookup;  // attr bytes -> welded index
    std::string key;
    for (size_t v = 0; v < count; ++v) {
        key.assign(reinterpret_cast<const char*>(&positions[v * 3]),
                   3 * sizeof(float));
        if (keyNormals)
            key.append(reinterpret_cast<const char*>(&normals[v * 3]),
                       3 * sizeof(float));
        if (w.hasUv)
            key.append(reinterpret_cast<const char*>(&uvs[v * 2]),
                       2 * sizeof(float));
        if (w.hasSkin) {
            key.append(reinterpret_cast<const char*>(&joints[v * 4]), 4);
            key.append(reinterpret_cast<const char*>(&weights[v * 4]), 4);
        }
        auto it = lookup.find(key);
        uint32_t idx;
        if (it == lookup.end()) {
            idx = (uint32_t)(w.pos.size() / 3);
            lookup.emplace(key, idx);
            w.pos.insert(w.pos.end(), &positions[v * 3], &positions[v * 3] + 3);
            w.nrm.insert(w.nrm.end(), &normals[v * 3], &normals[v * 3] + 3);
            if (w.hasUv)
                w.uv.insert(w.uv.end(), &uvs[v * 2], &uvs[v * 2] + 2);
            if (w.hasSkin) {
                w.joints.insert(w.joints.end(), &joints[v * 4],
                                &joints[v * 4] + 4);
                w.weights.insert(w.weights.end(), &weights[v * 4],
                                 &weights[v * 4] + 4);
            }
        } else {
            idx = it->second;
        }
        w.tris.push_back(idx);
    }
    return w;
}

Mesh weldInterleaved(const float* verts, size_t count, bool keyNormals) {
    // De-interleave into the separate arrays weld() keys on. Cheap next to
    // the collapse rounds, and it keeps one welding implementation.
    std::vector<float> pos(count * 3), nrm(count * 3), uv(count * 2);
    for (size_t v = 0; v < count; ++v) {
        const float* s = &verts[v * 8];
        pos[v * 3] = s[0];
        pos[v * 3 + 1] = s[1];
        pos[v * 3 + 2] = s[2];
        nrm[v * 3] = s[3];
        nrm[v * 3 + 1] = s[4];
        nrm[v * 3 + 2] = s[5];
        uv[v * 2] = s[6];
        uv[v * 2 + 1] = s[7];
    }
    return weld(pos.data(), nrm.data(), uv.data(), nullptr, nullptr, count,
                keyNormals);
}

void recomputeFaceNormals(std::vector<float>& verts) {
    for (size_t t = 0; t + 23 < verts.size(); t += 24) {
        const float* a = &verts[t];
        const float* b = &verts[t + 8];
        const float* c = &verts[t + 16];
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f)
            nx /= len, ny /= len, nz /= len;
        else
            nx = 0.0f, ny = 1.0f, nz = 0.0f;
        for (int k = 0; k < 3; ++k) {
            verts[t + k * 8 + 3] = nx;
            verts[t + k * 8 + 4] = ny;
            verts[t + k * 8 + 5] = nz;
        }
    }
}

void smoothNormals(std::vector<float>* const* parts, size_t partCount,
                   float creaseDeg) {
    struct Face {
        float n[3];  // normalized face normal
    };
    // One vote per triangle CORNER, weighted by that corner's interior angle
    // (Thuermer/Wuethrich): a quad fan-triangulates into two triangles that
    // both touch some of its corners and only one of the others, and a
    // per-triangle vote would tilt the pooled normal toward the double-voting
    // side (measured 11 degrees off radial on a hexagonal pole). The two
    // triangles' corner angles SUM to the quad's own, so angle weighting makes
    // the result independent of how faces were triangulated - and a sliver's
    // sharp corners vote almost nothing.
    struct Vote {
        uint32_t part, tri;
        float ang;
    };
    std::vector<std::vector<Face>> faces(partCount);
    std::map<std::string, std::vector<Vote>> atPos;  // position bits -> votes
    std::string key;
    for (size_t pi = 0; pi < partCount; ++pi) {
        const std::vector<float>& v = *parts[pi];
        const size_t triCount = v.size() / 24;
        faces[pi].resize(triCount);
        for (size_t t = 0; t < triCount; ++t) {
            const float* p[3] = {&v[t * 24], &v[t * 24 + 8], &v[t * 24 + 16]};
            const float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1],
                        uz = p[1][2] - p[0][2];
            const float vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1],
                        vz = p[2][2] - p[0][2];
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            Face& f = faces[pi][t];
            if (len > 1e-8f) {
                f.n[0] = nx / len, f.n[1] = ny / len, f.n[2] = nz / len;
            } else {
                f.n[0] = 0.0f, f.n[1] = 1.0f, f.n[2] = 0.0f;
            }
            for (int k = 0; k < 3; ++k) {
                // interior angle at corner k, between the edges to the other
                // two corners
                const float* o1 = p[(k + 1) % 3];
                const float* o2 = p[(k + 2) % 3];
                float e1[3] = {o1[0] - p[k][0], o1[1] - p[k][1],
                               o1[2] - p[k][2]};
                float e2[3] = {o2[0] - p[k][0], o2[1] - p[k][1],
                               o2[2] - p[k][2]};
                const float l1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] +
                                           e1[2] * e1[2]);
                const float l2 = std::sqrt(e2[0] * e2[0] + e2[1] * e2[1] +
                                           e2[2] * e2[2]);
                float ang = 0.0f;
                if (l1 > 1e-8f && l2 > 1e-8f) {
                    float d = (e1[0] * e2[0] + e1[1] * e2[1] + e1[2] * e2[2]) /
                              (l1 * l2);
                    d = d < -1.0f ? -1.0f : (d > 1.0f ? 1.0f : d);
                    ang = std::acos(d);
                }
                key.assign(reinterpret_cast<const char*>(&v[t * 24 + k * 8]),
                           3 * sizeof(float));
                atPos[key].push_back({(uint32_t)pi, (uint32_t)t, ang});
            }
        }
    }
    // The slack keeps a surface whose faces meet at EXACTLY the crease angle
    // (a hexagonal pole at 60) on the smooth side instead of flickering on
    // float noise. Opposite-facing coplanar faces (double-sided quads) read
    // dot -1 and never pool, so they cannot cancel each other to zero.
    const float cosT = std::cos(creaseDeg * 3.14159265358979f / 180.0f) - 1e-3f;
    for (size_t pi = 0; pi < partCount; ++pi) {
        std::vector<float>& v = *parts[pi];
        const size_t triCount = v.size() / 24;
        for (size_t t = 0; t < triCount; ++t) {
            const Face& self = faces[pi][t];
            for (int k = 0; k < 3; ++k) {
                key.assign(reinterpret_cast<const char*>(&v[t * 24 + k * 8]),
                           3 * sizeof(float));
                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                for (const Vote& q : atPos.find(key)->second) {
                    const Face& g = faces[q.part][q.tri];
                    if (self.n[0] * g.n[0] + self.n[1] * g.n[1] +
                            self.n[2] * g.n[2] <
                        cosT)
                        continue;
                    sx += g.n[0] * q.ang;
                    sy += g.n[1] * q.ang;
                    sz += g.n[2] * q.ang;
                }
                const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
                float* n = &v[t * 24 + k * 8 + 3];
                if (len > 1e-8f) {
                    n[0] = sx / len, n[1] = sy / len, n[2] = sz / len;
                } else {
                    n[0] = self.n[0], n[1] = self.n[1], n[2] = self.n[2];
                }
            }
        }
    }
}

void smoothNormals(std::vector<float>& verts, float creaseDeg) {
    std::vector<float>* p = &verts;
    smoothNormals(&p, 1, creaseDeg);
}

void decimate(Mesh& w, size_t targetVerts) {
    const size_t vertCount = w.pos.size() / 3;
    std::vector<uint32_t> remap(vertCount);
    for (size_t i = 0; i < vertCount; ++i) remap[i] = (uint32_t)i;
    auto resolve = [&](uint32_t v) {
        while (remap[v] != v) v = remap[v];
        return v;
    };
    size_t alive = vertCount;

    // "position twin" seams: the same position with different attributes
    // (uv/normal seams). Locked - moving one copy but not its twin would
    // crack the surface open.
    std::vector<uint8_t> locked(vertCount, 0);
    {
        std::map<std::string, uint32_t> firstAt;
        std::string pkey;
        for (size_t i = 0; i < vertCount; ++i) {
            pkey.assign(reinterpret_cast<const char*>(&w.pos[i * 3]),
                        3 * sizeof(float));
            auto it = firstAt.find(pkey);
            if (it == firstAt.end()) {
                firstAt.emplace(pkey, (uint32_t)i);
            } else {
                locked[i] = locked[it->second] = 1;
            }
        }
    }

    for (int round = 0; round < 64 && alive > targetVerts; ++round) {
        // live triangles + per-vertex quadrics + edge -> use count
        std::vector<Quadric> quadrics(vertCount);
        std::map<std::pair<uint32_t, uint32_t>, int> edgeUses;
        for (size_t t = 0; t + 2 < w.tris.size() + 1 && t < w.tris.size();
             t += 3) {
            uint32_t a = resolve(w.tris[t]), b = resolve(w.tris[t + 1]),
                     c = resolve(w.tris[t + 2]);
            if (a == b || b == c || a == c) continue;  // degenerate
            const float* pa = &w.pos[a * 3];
            const float* pb = &w.pos[b * 3];
            const float* pc = &w.pos[c * 3];
            const double ux = pb[0] - pa[0], uy = pb[1] - pa[1],
                         uz = pb[2] - pa[2];
            const double vx = pc[0] - pa[0], vy = pc[1] - pa[1],
                         vz = pc[2] - pa[2];
            double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz,
                   nz = ux * vy - uy * vx;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len < 1e-12) continue;
            nx /= len, ny /= len, nz /= len;
            const double d = -(nx * pa[0] + ny * pa[1] + nz * pa[2]);
            Quadric q;
            q.addPlane(nx, ny, nz, d);
            quadrics[a].add(q);
            quadrics[b].add(q);
            quadrics[c].add(q);
            const uint32_t e[3][2] = {{a, b}, {b, c}, {c, a}};
            for (auto& ed : e)
                edgeUses[{std::min(ed[0], ed[1]), std::max(ed[0], ed[1])}]++;
        }
        if (edgeUses.empty()) break;

        // open-border vertices (edge used by a single triangle) are locked
        // for this round so silhouette outlines and part borders hold still
        std::vector<uint8_t> border(vertCount, 0);
        for (const auto& eu : edgeUses)
            if (eu.second == 1)
                border[eu.first.first] = border[eu.first.second] = 1;

        struct Candidate {
            double cost;
            uint32_t from, to;
        };
        std::vector<Candidate> cands;
        cands.reserve(edgeUses.size());
        for (const auto& eu : edgeUses) {
            const uint32_t a = eu.first.first, b = eu.first.second;
            const bool aMovable = !locked[a] && !border[a];
            const bool bMovable = !locked[b] && !border[b];
            const double costAtoB = quadrics[a].error(&w.pos[b * 3]);
            const double costBtoA = quadrics[b].error(&w.pos[a * 3]);
            if (aMovable && (costAtoB <= costBtoA || !bMovable))
                cands.push_back({costAtoB, a, b});
            else if (bMovable)
                cands.push_back({costBtoA, b, a});
        }
        if (cands.empty()) break;
        std::sort(cands.begin(), cands.end(),
                  [](const Candidate& x, const Candidate& y) {
                      return x.cost < y.cost;
                  });

        // cap the round so quadrics never go too stale between rebuilds
        size_t budget = alive > targetVerts ? alive - targetVerts : 0;
        if (budget > alive / 4 + 1) budget = alive / 4 + 1;
        std::vector<uint8_t> touched(vertCount, 0);
        size_t done = 0;
        for (const Candidate& c : cands) {
            if (done >= budget) break;
            const uint32_t from = resolve(c.from), to = resolve(c.to);
            if (from == to || touched[from] || touched[to]) continue;
            remap[from] = to;
            touched[from] = touched[to] = 1;
            --alive;
            ++done;
        }
        if (done == 0) break;
    }

    // rewrite the index list through the remap, dropping degenerates
    std::vector<uint32_t> tris;
    tris.reserve(w.tris.size());
    for (size_t t = 0; t + 2 < w.tris.size(); t += 3) {
        const uint32_t a = resolve(w.tris[t]), b = resolve(w.tris[t + 1]),
                       c = resolve(w.tris[t + 2]);
        if (a == b || b == c || a == c) continue;
        tris.push_back(a);
        tris.push_back(b);
        tris.push_back(c);
    }
    w.tris.swap(tris);
}

std::vector<std::vector<float>> generateTiers(const std::vector<float>& verts) {
    std::vector<std::vector<float>> tiers;
    const size_t corners = verts.size() / 8;
    if (corners < kMinCorners) return tiers;
    for (float ratio : kRatios) {
        // Weld by position+uv, not by normal: these normals are DERIVED (per
        // face, then crease-smoothed), so keying on them would lock crease
        // corners as seam twins (see weld()). The tier's own normals are
        // re-derived after the collapse the same way the parser derives the
        // full mesh's - face normals, then the same crease smoothing - so a
        // LOD switch does not pop a curved surface back to flat.
        Mesh w = weldInterleaved(verts.data(), corners, false);
        const size_t target = (size_t)(w.vertexCount() * ratio + 0.5f);
        decimate(w, target < 3 ? 3 : target);
        std::vector<float> tier = unweldInterleaved(w);
        recomputeFaceNormals(tier);
        smoothNormals(tier, kCreaseAngleDeg);
        // The target is in welded vertices while this check is in corners -
        // sound because a collapse removes triangles roughly in proportion.
        const size_t tierCorners = tier.size() / 8;
        if (tierCorners == 0 ||
            (float)tierCorners > (float)corners * (ratio + kShrinkSlack))
            break;
        tiers.push_back(std::move(tier));
    }
    return tiers;
}

std::vector<float> unweldInterleaved(const Mesh& w) {
    std::vector<float> out;
    out.reserve(w.tris.size() * 8);
    for (uint32_t idx : w.tris) {
        out.insert(out.end(), &w.pos[idx * 3], &w.pos[idx * 3] + 3);
        out.insert(out.end(), &w.nrm[idx * 3], &w.nrm[idx * 3] + 3);
        if (w.hasUv) {
            out.insert(out.end(), &w.uv[idx * 2], &w.uv[idx * 2] + 2);
        } else {
            out.push_back(0.0f);
            out.push_back(0.0f);
        }
    }
    return out;
}

}  // namespace meshlod

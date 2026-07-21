#include "aobake.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace aobake {

namespace {

constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x, y, z;
};

// Rotation order X, then Y, then Z - the exact twin of templates.cpp
// rotated() and the viewport's model matrix. Keep in sync.
V3 rotated(const V3& v, const float* rotDeg) {
    V3 r = v;
    const float rx = rotDeg[0] * kPi / 180.0f;
    const float ry = rotDeg[1] * kPi / 180.0f;
    const float rz = rotDeg[2] * kPi / 180.0f;
    {
        const float c = std::cos(rx), s = std::sin(rx);
        const float y = r.y * c - r.z * s, z = r.y * s + r.z * c;
        r.y = y, r.z = z;
    }
    {
        const float c = std::cos(ry), s = std::sin(ry);
        const float x = r.x * c + r.z * s, z = -r.x * s + r.z * c;
        r.x = x, r.z = z;
    }
    {
        const float c = std::cos(rz), s = std::sin(rz);
        const float x = r.x * c - r.y * s, y = r.x * s + r.y * c;
        r.x = x, r.y = y;
    }
    return r;
}

bool animatedModelPath(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = p.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".glb" || ext == ".gltf" || ext == ".fbx";
}

}  // namespace

std::vector<Occluder> collectOccluders(const std::vector<SceneObject>& objects,
                                       const ModelAabbFn& modelAabb) {
    std::vector<Occluder> out;
    for (int i = 0; i < (int)objects.size(); ++i) {
        const SceneObject& o = objects[i];
        float cLocal[3] = {0, 0, 0};  // occluder center in object-local units
        float half[3];
        bool sphere = false;
        switch (o.type) {
            case PrimitiveType::Sphere:
                sphere = true;
                half[0] = 0.5f * std::max(std::fabs(o.scale[0]),
                                          std::max(std::fabs(o.scale[1]),
                                                   std::fabs(o.scale[2])));
                half[1] = half[2] = half[0];
                break;
            case PrimitiveType::Box:
            case PrimitiveType::SavePoint:
            case PrimitiveType::Cylinder:
            case PrimitiveType::Cone:
                for (int k = 0; k < 3; ++k) half[k] = 0.5f * std::fabs(o.scale[k]);
                break;
            case PrimitiveType::Plane:
                // Unit XZ square: a thin slab (rotated planes make walls).
                half[0] = 0.5f * std::fabs(o.scale[0]);
                half[1] = 0.02f;
                half[2] = 0.5f * std::fabs(o.scale[2]);
                break;
            case PrimitiveType::Model: {
                if (animatedModelPath(o.modelPath)) continue;
                float mn[3], mx[3];
                if (!modelAabb || !modelAabb(o, mn, mx)) continue;
                for (int k = 0; k < 3; ++k) {
                    cLocal[k] = 0.5f * (mn[k] + mx[k]) * o.scale[k];
                    half[k] = 0.5f * std::fabs((mx[k] - mn[k]) * o.scale[k]);
                }
                break;
            }
            default:
                continue;  // markers, lights, decals, mirrors, portals...
        }
        Occluder oc;
        oc.sphere = sphere;
        oc.objIndex = i;
        for (int k = 0; k < 3; ++k) oc.half[k] = std::max(half[k], 0.005f);
        const V3 axX = rotated({1, 0, 0}, o.rotation);
        const V3 axY = rotated({0, 1, 0}, o.rotation);
        const V3 axZ = rotated({0, 0, 1}, o.rotation);
        oc.axis[0][0] = axX.x, oc.axis[0][1] = axX.y, oc.axis[0][2] = axX.z;
        oc.axis[1][0] = axY.x, oc.axis[1][1] = axY.y, oc.axis[1][2] = axY.z;
        oc.axis[2][0] = axZ.x, oc.axis[2][1] = axZ.y, oc.axis[2][2] = axZ.z;
        const V3 cw = rotated({cLocal[0], cLocal[1], cLocal[2]}, o.rotation);
        oc.pos[0] = o.position[0] + cw.x;
        oc.pos[1] = o.position[1] + cw.y;
        oc.pos[2] = o.position[2] + cw.z;
        out.push_back(oc);
    }
    return out;
}

std::vector<uint8_t> terrainAO(const std::vector<float>& heights, int w, int d,
                               float stepX, float stepZ, float radiusWorld) {
    std::vector<uint8_t> out;
    if (w < 2 || d < 2 || (int)heights.size() < w * d) return out;
    out.assign((size_t)w * d, 255);

    // 8 horizon directions: 4 axis + 4 diagonal grid walks.
    const int dirs[8][2] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1},
                            {1, 1},  {1, -1}, {-1, 1}, {-1, -1}};
    const float stepLen[8] = {
        stepX, stepX, stepZ, stepZ,
        std::sqrt(stepX * stepX + stepZ * stepZ),
        std::sqrt(stepX * stepX + stepZ * stepZ),
        std::sqrt(stepX * stepX + stepZ * stepZ),
        std::sqrt(stepX * stepX + stepZ * stepZ)};
    const float minStep = std::min(stepX, stepZ);
    int maxSteps = (int)std::ceil(radiusWorld / std::max(minStep, 0.001f));
    if (maxSteps < 2) maxSteps = 2;
    if (maxSteps > 48) maxSteps = 48;

    for (int z = 0; z < d; ++z) {
        for (int x = 0; x < w; ++x) {
            const float h0 = heights[(size_t)z * w + x];
            float occ = 0.0f;
            for (int dir = 0; dir < 8; ++dir) {
                float maxSin = 0.0f;
                for (int k = 1; k <= maxSteps; ++k) {
                    const int sx = x + dirs[dir][0] * k;
                    const int sz = z + dirs[dir][1] * k;
                    if (sx < 0 || sz < 0 || sx >= w || sz >= d) break;
                    const float dist = stepLen[dir] * k;
                    if (dist > radiusWorld) break;
                    const float dh = heights[(size_t)sz * w + sx] - h0;
                    if (dh <= 0.0f) continue;
                    const float s = dh / std::sqrt(dh * dh + dist * dist);
                    if (s > maxSin) maxSin = s;
                }
                occ += maxSin;
            }
            occ /= 8.0f;
            if (occ < 0.0f) occ = 0.0f;
            if (occ > 1.0f) occ = 1.0f;
            out[(size_t)z * w + x] = (uint8_t)(255.0f * (1.0f - occ) + 0.5f);
        }
    }
    return out;
}

namespace {

// Watertight enough for AO: Moller-Trumbore, double-sided, t in (tMin, tMax).
// Grazing hits (ray nearly parallel to the triangle) are rejected: they are
// the false self-hits of a vertex's own tangent faces at convex corners,
// while every real concave-corner occluder is struck at a steeper angle.
// This is what lets adjacent faces occlude their shared corners (a low-poly
// wall is two huge triangles - excluding "triangles containing the vertex"
// would remove the entire wall and no interior corner would ever darken).
bool rayTri(const float o[3], const float dir[3], const float* a,
            const float* b, const float* c, float tMax, float& tOut) {
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                        e1[2] * e2[0] - e1[0] * e2[2],
                        e1[0] * e2[1] - e1[1] * e2[0]};
    const float p[3] = {dir[1] * e2[2] - dir[2] * e2[1],
                        dir[2] * e2[0] - dir[0] * e2[2],
                        dir[0] * e2[1] - dir[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-9f) return false;
    const float nLen = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nLen < 1e-12f || std::fabs(det) < 0.15f * nLen) return false;  // grazing
    const float inv = 1.0f / det;
    const float tv[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = {tv[1] * e1[2] - tv[2] * e1[1],
                        tv[2] * e1[0] - tv[0] * e1[2],
                        tv[0] * e1[1] - tv[1] * e1[0]};
    const float v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    if (t <= 1e-5f || t >= tMax) return false;
    tOut = t;
    return true;
}

}  // namespace

std::vector<uint8_t> modelAO(const objparser::Model& m) {
    // Flatten triangles + find the position count.
    std::vector<float> corners;   // xyz per corner, 3 corners per tri
    std::vector<int> cornerPos;   // obj position index per corner
    std::vector<float> cornerNrm;
    int posCount = 0;
    for (const objparser::Submesh& s : m.submeshes) {
        const size_t n = s.verts.size() / 8;
        if (s.posIdx.size() < n) continue;  // parser predates posIdx - bail
        for (size_t i = 0; i < n; ++i) {
            const float* v = &s.verts[i * 8];
            corners.insert(corners.end(), {v[0], v[1], v[2]});
            cornerNrm.insert(cornerNrm.end(), {v[3], v[4], v[5]});
            cornerPos.push_back(s.posIdx[i]);
            if (s.posIdx[i] + 1 > posCount) posCount = s.posIdx[i] + 1;
        }
    }
    const int triCount = (int)(cornerPos.size() / 3);
    std::vector<uint8_t> out;
    if (triCount == 0 || posCount == 0) return out;

    // Per obj position: representative point + averaged normal.
    std::vector<float> pos((size_t)posCount * 3, 0.0f);
    std::vector<float> nrm((size_t)posCount * 3, 0.0f);
    std::vector<char> seen(posCount, 0);
    for (size_t i = 0; i < cornerPos.size(); ++i) {
        const int pi = cornerPos[i];
        if (pi < 0 || pi >= posCount) continue;
        if (!seen[pi]) {
            seen[pi] = 1;
            for (int k = 0; k < 3; ++k) pos[(size_t)pi * 3 + k] = corners[i * 3 + k];
        }
        for (int k = 0; k < 3; ++k) nrm[(size_t)pi * 3 + k] += cornerNrm[i * 3 + k];
    }

    // Model extents -> ray length + XZ acceleration grid.
    float mn[3] = {m.min[0], m.min[1], m.min[2]};
    float mx[3] = {m.max[0], m.max[1], m.max[2]};
    const float ex = mx[0] - mn[0], ey = mx[1] - mn[1], ez = mx[2] - mn[2];
    const float diag = std::sqrt(ex * ex + ey * ey + ez * ez);
    if (diag < 1e-6f) return out;
    const float maxDist = 0.5f * diag;
    const float eps = 1e-3f * diag;

    constexpr int kGrid = 48;
    const float invCx = kGrid / std::max(ex, 1e-6f);
    const float invCz = kGrid / std::max(ez, 1e-6f);
    auto cellOf = [&](float x, float z, int& cx, int& cz) {
        cx = (int)((x - mn[0]) * invCx);
        cz = (int)((z - mn[2]) * invCz);
        if (cx < 0) cx = 0;
        if (cz < 0) cz = 0;
        if (cx >= kGrid) cx = kGrid - 1;
        if (cz >= kGrid) cz = kGrid - 1;
    };
    std::vector<std::vector<int>> cells((size_t)kGrid * kGrid);
    for (int t = 0; t < triCount; ++t) {
        float tmnx = 1e30f, tmxx = -1e30f, tmnz = 1e30f, tmxz = -1e30f;
        for (int c = 0; c < 3; ++c) {
            const float* p = &corners[((size_t)t * 3 + c) * 3];
            tmnx = std::min(tmnx, p[0]), tmxx = std::max(tmxx, p[0]);
            tmnz = std::min(tmnz, p[2]), tmxz = std::max(tmxz, p[2]);
        }
        int c0x, c0z, c1x, c1z;
        cellOf(tmnx, tmnz, c0x, c0z);
        cellOf(tmxx, tmxz, c1x, c1z);
        for (int cz = c0z; cz <= c1z; ++cz)
            for (int cx = c0x; cx <= c1x; ++cx)
                cells[(size_t)cz * kGrid + cx].push_back(t);
    }
    std::vector<int> stamp((size_t)triCount, -1);

    // Deterministic cosine-weighted hemisphere (golden-angle spiral).
    constexpr int kRays = 24;
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    float rays[kRays][3];
    for (int i = 0; i < kRays; ++i) {
        const float u = (i + 0.5f) / kRays;
        const float r = std::sqrt(u);
        const float zc = std::sqrt(1.0f - u);
        const float phi = golden * i;
        rays[i][0] = r * std::cos(phi);
        rays[i][1] = r * std::sin(phi);
        rays[i][2] = zc;
    }

    out.assign(posCount, 255);
    int rayStamp = 0;
    for (int pi = 0; pi < posCount; ++pi) {
        if (!seen[pi]) continue;
        float n[3] = {nrm[(size_t)pi * 3], nrm[(size_t)pi * 3 + 1],
                      nrm[(size_t)pi * 3 + 2]};
        const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl < 1e-6f) continue;
        n[0] /= nl, n[1] /= nl, n[2] /= nl;
        // Tangent frame around n.
        float tx[3];
        if (std::fabs(n[1]) < 0.9f) {
            // cross((0,1,0), n)
            tx[0] = n[2];
            tx[1] = 0.0f;
            tx[2] = -n[0];
        } else {
            // n is near +-Y: start from +X and orthogonalize against n
            tx[0] = 1.0f - n[0] * n[0];
            tx[1] = -n[0] * n[1];
            tx[2] = -n[0] * n[2];
        }
        {
            const float tl = std::sqrt(tx[0] * tx[0] + tx[1] * tx[1] + tx[2] * tx[2]);
            tx[0] /= tl, tx[1] /= tl, tx[2] /= tl;
        }
        const float ty[3] = {n[1] * tx[2] - n[2] * tx[1],
                             n[2] * tx[0] - n[0] * tx[2],
                             n[0] * tx[1] - n[1] * tx[0]};
        const float* P = &pos[(size_t)pi * 3];
        const float o[3] = {P[0] + n[0] * eps, P[1] + n[1] * eps,
                            P[2] + n[2] * eps};

        float occ = 0.0f;
        for (int ri = 0; ri < kRays; ++ri) {
            const float* rd = rays[ri];
            const float dir[3] = {
                tx[0] * rd[0] + ty[0] * rd[1] + n[0] * rd[2],
                tx[1] * rd[0] + ty[1] * rd[1] + n[1] * rd[2],
                tx[2] * rd[0] + ty[2] * rd[1] + n[2] * rd[2]};
            // Cells the XZ segment covers (bbox walk - segments are short).
            const float x1 = o[0] + dir[0] * maxDist;
            const float z1 = o[2] + dir[2] * maxDist;
            int c0x, c0z, c1x, c1z;
            cellOf(std::min(o[0], x1), std::min(o[2], z1), c0x, c0z);
            cellOf(std::max(o[0], x1), std::max(o[2], z1), c1x, c1z);
            ++rayStamp;
            float best = maxDist;
            bool hit = false;
            for (int cz = c0z; cz <= c1z; ++cz) {
                for (int cx = c0x; cx <= c1x; ++cx) {
                    for (int t : cells[(size_t)cz * kGrid + cx]) {
                        if (stamp[t] == rayStamp) continue;
                        stamp[t] = rayStamp;
                        const int b = t * 3;
                        float th;
                        if (rayTri(o, dir, &corners[(size_t)b * 3],
                                   &corners[(size_t)(b + 1) * 3],
                                   &corners[(size_t)(b + 2) * 3], best, th)) {
                            best = th;
                            hit = true;
                        }
                    }
                }
            }
            if (hit) occ += 1.0f - best / maxDist;
        }
        occ /= (float)kRays;
        if (occ < 0.0f) occ = 0.0f;
        if (occ > 1.0f) occ = 1.0f;
        out[pi] = (uint8_t)(255.0f * (1.0f - occ) + 0.5f);
    }
    return out;
}

bool writeModelAoSidecar(const std::string& path,
                         const std::vector<uint8_t>& ao) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const uint32_t count = (uint32_t)ao.size();
    f.write("TXAO", 4);
    f.write(reinterpret_cast<const char*>(&count), 4);
    if (count) f.write(reinterpret_cast<const char*>(ao.data()), count);
    return (bool)f;
}

bool objAabb(const std::string& path, float mn[3], float mx[3]) {
    std::ifstream f(path);
    if (!f) return false;
    bool any = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 3 || line[0] != 'v' || !std::isspace((unsigned char)line[1]))
            continue;
        float x, y, z;
        if (std::sscanf(line.c_str() + 1, "%f %f %f", &x, &y, &z) != 3) continue;
        if (!any) {
            mn[0] = mx[0] = x, mn[1] = mx[1] = y, mn[2] = mx[2] = z;
            any = true;
        } else {
            mn[0] = std::min(mn[0], x), mx[0] = std::max(mx[0], x);
            mn[1] = std::min(mn[1], y), mx[1] = std::max(mx[1], y);
            mn[2] = std::min(mn[2], z), mx[2] = std::max(mx[2], z);
        }
    }
    return any;
}

}  // namespace aobake

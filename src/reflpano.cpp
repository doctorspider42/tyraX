#include "reflpano.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "reflscenery.hpp"
#include "roadgen.hpp"

namespace reflpano {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Aabb {
    float mn[3], mx[3];
    float center[3];
    float skipR;  // the live loop's "probe stands at it" radius
};

// Slab test; t of the entry point (or 0 inside), < 0 = miss.
float hitAabb(const Aabb& b, const float o[3], const float d[3], float tMax) {
    float t0 = 0.0f, t1 = tMax;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-8f) {
            if (o[k] < b.mn[k] || o[k] > b.mx[k]) return -1.0f;
            continue;
        }
        const float inv = 1.0f / d[k];
        float a = (b.mn[k] - o[k]) * inv, c = (b.mx[k] - o[k]) * inv;
        if (a > c) std::swap(a, c);
        t0 = std::max(t0, a);
        t1 = std::min(t1, c);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

// Median cut over RGB, n colours. Deterministic.
std::vector<std::array<unsigned char, 3>> medianCut(
    const std::vector<std::array<unsigned char, 3>>& px, int n) {
    std::vector<std::array<unsigned char, 3>> out;
    if (px.empty() || n <= 0) return out;
    struct Box {
        std::vector<int> idx;
    };
    std::vector<Box> boxes(1);
    boxes[0].idx.resize(px.size());
    for (size_t i = 0; i < px.size(); ++i) boxes[0].idx[i] = (int)i;
    auto range = [&](const Box& b, int& axis) {
        int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        for (int i : b.idx)
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], (int)px[(size_t)i][k]);
                hi[k] = std::max(hi[k], (int)px[(size_t)i][k]);
            }
        int best = -1;
        axis = 0;
        for (int k = 0; k < 3; ++k)
            if (hi[k] - lo[k] > best) best = hi[k] - lo[k], axis = k;
        return best;
    };
    while ((int)boxes.size() < n) {
        int pick = -1, pickAxis = 0, pickRange = 0;
        for (size_t b = 0; b < boxes.size(); ++b) {
            if (boxes[b].idx.size() < 2) continue;
            int axis = 0;
            const int r = range(boxes[b], axis);
            // Weight by population so a big flat area is split before a rare speck.
            const long long score = (long long)r * (long long)boxes[b].idx.size();
            if (r > 0 && (pick < 0 || score > (long long)pickRange)) {
                pick = (int)b, pickAxis = axis, pickRange = (int)std::min<long long>(score, 0x7fffffff);
            }
        }
        if (pick < 0) break;
        std::vector<int>& v = boxes[(size_t)pick].idx;
        std::sort(v.begin(), v.end(), [&](int a, int b) {
            if (px[(size_t)a][pickAxis] != px[(size_t)b][pickAxis])
                return px[(size_t)a][pickAxis] < px[(size_t)b][pickAxis];
            return a < b;
        });
        Box hi;
        hi.idx.assign(v.begin() + (long)(v.size() / 2), v.end());
        v.resize(v.size() / 2);
        boxes.push_back(std::move(hi));
    }
    for (const Box& b : boxes) {
        long long acc[3] = {0, 0, 0};
        for (int i : b.idx)
            for (int k = 0; k < 3; ++k) acc[k] += px[(size_t)i][k];
        std::array<unsigned char, 3> c{};
        for (int k = 0; k < 3; ++k)
            c[k] = (unsigned char)(b.idx.empty() ? 0 : (acc[k] + (long long)b.idx.size() / 2) /
                                                           (long long)b.idx.size());
        out.push_back(c);
    }
    return out;
}

}  // namespace

bool wanted(const Project& p, const SceneData& sc) {
    return p.settings.reflectionPanorama && sc.terrain.enabled && sc.hmW >= 2 &&
           sc.hmD >= 2;
}

std::vector<int> bakedObjects(const Project& p, const SceneData& sc) {
    std::vector<int> out;
    for (const reflscenery::Box& b : reflscenery::collectProxies(p, sc)) {
        if ((int)out.size() >= kMaxBoxes) break;
        out.push_back(b.object);
    }
    return out;
}

Bake bake(const Project& p, const SceneData& sc) {
    Bake out;
    if (!wanted(p, sc)) return out;
    const reflscenery::Ground g = reflscenery::ground(p, sc);
    if (!g.ok) return out;
    const ProjectSettings rs = project::resolvedSettings(p, sc);

    // The scene's baked directional light - the shadeOf() every terrain vertex
    // is coloured with (templates.cpp emits the same numbers as SCENE_LIGHT_*).
    float L[3] = {rs.lightDir[0], rs.lightDir[1], rs.lightDir[2]};
    {
        const float len = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        if (len > 1e-5f) L[0] /= len, L[1] /= len, L[2] /= len;
        else L[0] = 0, L[1] = 1, L[2] = 0;
    }
    auto shade = [&](const float n[3], int k) {
        float d = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        if (d < 0) d = 0;
        const float s = rs.brightness * (rs.ambient + rs.diffuse * d * rs.lightColor[k]);
        return std::min(s, 1.0f);
    };

    const float w = (float)sc.terrain.width, d = (float)sc.terrain.depth;
    auto height = [&](float x, float z) {
        return roadgen::terrainHeight(sc.heights, sc.hmW, sc.hmD, w, d, x, z);
    };
    auto albedo = [&](float x, float z, float out3[3]) {
        float u = (x - g.minX) / g.sizeX * reflscenery::kGroundGrid - 0.5f;
        float v = (z - g.minZ) / g.sizeZ * reflscenery::kGroundGrid - 0.5f;
        const int n = reflscenery::kGroundGrid;
        u = std::min(std::max(u, 0.0f), (float)n - 1.001f);
        v = std::min(std::max(v, 0.0f), (float)n - 1.001f);
        const int iu = (int)u, iv = (int)v;
        const float fu = u - iu, fv = v - iv;
        for (int k = 0; k < 3; ++k) {
            auto at = [&](int a, int b) {
                return g.rgb[((size_t)b * n + a) * 3 + k] / 255.0f;
            };
            const float top = at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu;
            const float bot = at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu;
            out3[k] = top * (1 - fv) + bot * fv;
        }
    };

    // The baked proxies, as the world-space AABB the game's renderReflectionProxy
    // draws (the box of the object's world vertices).
    std::vector<Aabb> boxes;
    for (const reflscenery::Box& b : reflscenery::collectProxies(p, sc)) {
        if ((int)out.boxObjects.size() >= kMaxBoxes) break;
        Aabb a;
        for (int k = 0; k < 3; ++k) {
            const float h = std::fabs(b.axis[0][k]) + std::fabs(b.axis[1][k]) +
                            std::fabs(b.axis[2][k]);
            a.mn[k] = b.center[k] - h;
            a.mx[k] = b.center[k] + h;
            a.center[k] = b.center[k];
        }
        const SceneObject& o = sc.objects[(size_t)b.object];
        const float half = std::max(o.scale[0], std::max(o.scale[1], o.scale[2]));
        a.skipR = 0.87f * half * 1.9f;
        boxes.push_back(a);
        out.boxObjects.push_back(b.object);
    }
    const int boxCount = (int)boxes.size();

    // Cells: kCellSize squares over the terrain, baked where a road is near -
    // the car is the only thing that carries a reflective paint about.
    out.minX = -w * 0.5f;
    out.minZ = -d * 0.5f;
    out.cellsX = std::max(1, (int)std::ceil(w / kCellSize));
    out.cellsZ = std::max(1, (int)std::ceil(d / kCellSize));
    out.slot.assign((size_t)out.cellsX * out.cellsZ, -1);
    std::vector<char> nearRoad(out.slot.size(), 0);
    for (const SceneObject& o : sc.objects) {
        if (o.type != PrimitiveType::Road || o.roadPoints.size() < 4) continue;
        std::vector<roadgen::Vertex> tris;
        roadgen::tessellate(o.roadPoints, o.roadWidth, [](float, float) { return 0.0f; },
                            tris, {}, o.roadSampleStep);
        for (const roadgen::Vertex& v : tris) {
            const int x0 = (int)std::floor((v.x - kRoadReach - out.minX) / kCellSize);
            const int x1 = (int)std::floor((v.x + kRoadReach - out.minX) / kCellSize);
            const int z0 = (int)std::floor((v.z - kRoadReach - out.minZ) / kCellSize);
            const int z1 = (int)std::floor((v.z + kRoadReach - out.minZ) / kCellSize);
            for (int z = std::max(0, z0); z <= std::min(out.cellsZ - 1, z1); ++z)
                for (int x = std::max(0, x0); x <= std::min(out.cellsX - 1, x1); ++x)
                    nearRoad[(size_t)z * out.cellsX + x] = 1;
        }
    }

    // The probe draws terrain and road CHUNKS whose box lies within the ground
    // radius; past it the target keeps its clear colour (the sky). A chunk is
    // ~20 units, so the reach is the radius plus about half of one.
    const float groundReach =
        rs.reflectionGroundRadius > 0.0f ? rs.reflectionGroundRadius + 10.0f : 1e9f;

    // Pass 1: per texel either a box entry (1..boxes), transparent (0), or a
    // ground colour to quantize (kGround, colour kept aside).
    constexpr int kGround = -1;
    std::vector<int> idx;
    std::vector<std::array<unsigned char, 3>> groundPx;
    std::vector<int> groundAt;  // texel -> index into groundPx
    for (int cz = 0; cz < out.cellsZ; ++cz)
        for (int cx = 0; cx < out.cellsX; ++cx) {
            if (!nearRoad[(size_t)cz * out.cellsX + cx]) continue;
            out.slot[(size_t)cz * out.cellsX + cx] = (int16_t)(out.slots());
            const float ex = out.minX + (cx + 0.5f) * kCellSize;
            const float ez = out.minZ + (cz + 0.5f) * kCellSize;
            const float eye[3] = {ex, height(ex, ez) + kEyeHeight, ez};
            for (int r = 0; r < kHeight; ++r) {
                const float el = (kElevTop - (r + 0.5f) * (kElevTop - kElevBottom) / kHeight) *
                                 (kPi / 180.0f);
                for (int c = 0; c < kWidth; ++c) {
                    const float az = 2.0f * kPi * (c + 0.5f) / kWidth;
                    const float dir[3] = {std::cos(el) * std::cos(az), std::sin(el),
                                          std::cos(el) * std::sin(az)};
                    // Nearest box (skipping one the eye stands at, as the live loop).
                    float tBox = 1e9f;
                    int box = -1;
                    for (int b = 0; b < boxCount; ++b) {
                        const Aabb& a = boxes[(size_t)b];
                        const float dx = a.center[0] - eye[0], dy = a.center[1] - eye[1],
                                    dz = a.center[2] - eye[2];
                        if (dx * dx + dy * dy + dz * dz < a.skipR * a.skipR) continue;
                        const float t = hitAabb(a, eye, dir, tBox);
                        if (t >= 0.0f && t < tBox) tBox = t, box = b;
                    }
                    // Ground: march, then bisect. Only downward rays reach it.
                    float tGround = -1.0f;
                    if (dir[1] < -0.002f) {
                        const float horiz = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
                        const float tMax = std::min(
                            tBox, horiz > 1e-4f ? groundReach / horiz : 1e9f);
                        float prev = 0.0f;
                        for (float t = 0.25f; t <= std::min(tMax, 400.0f); t += 0.25f + t * 0.02f) {
                            const float x = eye[0] + dir[0] * t, z = eye[2] + dir[2] * t;
                            if (eye[1] + dir[1] * t <= height(x, z)) {
                                float lo = prev, hi = t;
                                for (int it = 0; it < 12; ++it) {
                                    const float m = 0.5f * (lo + hi);
                                    const float mx = eye[0] + dir[0] * m, mz = eye[2] + dir[2] * m;
                                    if (eye[1] + dir[1] * m <= height(mx, mz)) hi = m;
                                    else lo = m;
                                }
                                tGround = hi;
                                break;
                            }
                            prev = t;
                        }
                        if (tGround > tMax) tGround = -1.0f;
                    }
                    if (tGround >= 0.0f && tGround < tBox) {
                        const float x = eye[0] + dir[0] * tGround, z = eye[2] + dir[2] * tGround;
                        const float e = 0.5f;
                        float n[3] = {height(x - e, z) - height(x + e, z), 2.0f * e,
                                      height(x, z - e) - height(x, z + e)};
                        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        for (float& v : n) v /= len;
                        float a[3];
                        albedo(x, z, a);
                        std::array<unsigned char, 3> col{};
                        for (int k = 0; k < 3; ++k)
                            col[k] = (unsigned char)std::lround(
                                std::min(1.0f, a[k] * shade(n, k)) * 255.0f);
                        groundAt.push_back((int)groundPx.size());
                        groundPx.push_back(col);
                        idx.push_back(kGround);
                    } else if (box >= 0) {
                        idx.push_back(1 + box);
                    } else {
                        idx.push_back(0);
                    }
                }
            }
            out.pixels.resize(idx.size());
        }

    // Pass 2: one palette for the scene's ground, after the box entries.
    const int groundColours = std::max(1, 255 - boxCount);
    const std::vector<std::array<unsigned char, 3>> pal = medianCut(groundPx, groundColours);
    out.palette.assign(256 * 4, 0);
    for (size_t i = 0; i < pal.size(); ++i) {
        unsigned char* e = &out.palette[(1 + boxCount + i) * 4];
        e[0] = pal[i][0], e[1] = pal[i][1], e[2] = pal[i][2], e[3] = 0x80;
    }
    // Nearest palette entry per ground colour, memoised on the 15-bit colour.
    std::vector<int16_t> memo(32768, -1);
    size_t gi = 0;
    for (size_t t = 0; t < idx.size(); ++t) {
        if (idx[t] != kGround) {
            out.pixels[t] = (unsigned char)idx[t];
            continue;
        }
        const std::array<unsigned char, 3>& c = groundPx[(size_t)groundAt[gi++]];
        const int key = (c[0] >> 3) << 10 | (c[1] >> 3) << 5 | (c[2] >> 3);
        if (memo[(size_t)key] < 0) {
            int best = 0;
            long bestD = -1;
            for (size_t i = 0; i < pal.size(); ++i) {
                const long dr = (long)c[0] - pal[i][0], dg = (long)c[1] - pal[i][1],
                           db = (long)c[2] - pal[i][2];
                const long dd = dr * dr + dg * dg + db * db;
                if (bestD < 0 || dd < bestD) bestD = dd, best = (int)i;
            }
            memo[(size_t)key] = (int16_t)best;
        }
        out.pixels[t] = (unsigned char)(1 + boxCount + memo[(size_t)key]);
    }
    out.ok = out.slots() > 0;
    return out;
}

// Layout (little endian), 16-byte aligned sections so the game can point a
// texture straight at a slot after one aligned load:
//   0  char[4] "RPAN"   4 u32 version (1)
//   8  u16 width, u16 height, u16 cellsX, u16 cellsZ
//   16 f32 minX, minZ, cellSize, eyeHeight
//   32 u16 slots, u16 boxes, u16 elevTop*10, s16 elevBottom*10, u32 pad x2
//   48 s16 slot[cellsX*cellsZ], padded to 16
//      s32 boxObject[boxes], padded to 16
//      u8 palette[256*4] (RGBA, logical order; alpha 0..0x80)
//      u32 offset[slots + 1] into the run stream below
//      the run stream: per slot, (count 1..255, index) byte pairs that expand
//      to height x width indices, row-major. The panoramas are long runs - the
//      sky, a box face, a quantized ground band - so the Motor District's
//      299 cells are ~80 KB of EE RAM instead of 1.2 MB; the game expands one
//      cell (4 KB) when the probe's eye enters it.
std::vector<unsigned char> serialize(const Bake& b) {
    std::vector<unsigned char> out;
    auto put = [&](const void* p, size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        out.insert(out.end(), c, c + n);
    };
    auto u16 = [&](unsigned v) { const uint16_t x = (uint16_t)v; put(&x, 2); };
    auto s16 = [&](int v) { const int16_t x = (int16_t)v; put(&x, 2); };
    auto u32 = [&](unsigned v) { const uint32_t x = v; put(&x, 4); };
    auto f32 = [&](float v) { put(&v, 4); };
    auto pad = [&]() { while (out.size() % 16) out.push_back(0); };
    put("RPAN", 4);
    u32(2);
    u16(kWidth), u16(kHeight), u16((unsigned)b.cellsX), u16((unsigned)b.cellsZ);
    f32(b.minX), f32(b.minZ), f32(kCellSize), f32(kEyeHeight);
    u16((unsigned)b.slots()), u16((unsigned)b.boxObjects.size());
    s16((int)std::lround(kElevTop * 10)), s16((int)std::lround(kElevBottom * 10));
    u32(0), u32(0);
    for (int16_t s : b.slot) s16(s);
    pad();
    for (int32_t o : b.boxObjects) put(&o, 4);
    pad();
    put(b.palette.data(), b.palette.size());
    std::vector<unsigned char> runs;
    std::vector<uint32_t> offsets;
    const size_t cellBytes = (size_t)kWidth * kHeight;
    for (int s = 0; s < b.slots(); ++s) {
        offsets.push_back((uint32_t)runs.size());
        const unsigned char* px = b.pixels.data() + (size_t)s * cellBytes;
        for (size_t i = 0; i < cellBytes;) {
            size_t j = i;
            while (j < cellBytes && px[j] == px[i] && j - i < 255) ++j;
            runs.push_back((unsigned char)(j - i));
            runs.push_back(px[i]);
            i = j;
        }
    }
    offsets.push_back((uint32_t)runs.size());
    for (uint32_t o : offsets) u32(o);
    put(runs.data(), runs.size());
    return out;
}

}  // namespace reflpano

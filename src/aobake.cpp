#include "aobake.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "objparser.hpp"

namespace aobake {

namespace {

constexpr float kPi = 3.14159265358979f;

// Ordered 4x4 Bayer, remapped to [-0.5, 0.5) of one output level. The baked
// light is a wide, low-amplitude ramp (a pool spanning half the screen may
// only cover 30 of the 255 levels), and an 8-bit framebuffer turns each level
// into a broad plateau whose bilinear-magnified edges read as irregular
// blocks. A sub-level ripple moves those edges apart pixel by pixel and the
// eye integrates it back into a smooth gradient. Keyed on the ATLAS texel, so
// it is deterministic and never crawls between rebuilds.
inline float bayerDither(int x, int y) {
    static const int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return m[(y & 3) * 4 + (x & 3)] * (1.0f / 16.0f) - 0.5f;
}

// Reads an object's assigned .mtl (docs/emissive-materials.md). Cached per
// path - the sweeps below hit every object in the scene, and this touches disk.
const MaterialGlow& materialGlow(const std::string& projectDir,
                                 const std::string& materialPath,
                                 GlowCache& cache) {
    auto it = cache.find(materialPath);
    if (it != cache.end()) return it->second;
    MaterialGlow g;
    std::vector<objparser::MtlMaterial> mats;
    if (!materialPath.empty() &&
        objparser::loadMtl(
            (std::filesystem::path(projectDir) / materialPath).string(), mats) &&
        !mats.empty()) {
        for (int i = 0; i < 3; ++i) g.ke[i] = mats.front().ke[i];
        for (int i = 0; i < 3; ++i) g.kd[i] = mats.front().kd[i];
        for (int i = 0; i < 3; ++i) g.color[i] = mats.front().glowColor[i];
        g.textured = !mats.front().texture.empty();
        g.range = mats.front().glowRange;
        g.light = mats.front().glowLight;
    }
    return cache.emplace(materialPath, g).first->second;
}

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

// The analytic box/sphere standing in for one scene object, in world space.
// Returns false for shapes that have no volume worth approximating (markers,
// lights, decals, mirrors, portals, animated models). Shared by the occluder
// and the emissive-light collection - both need the exact same solid.
static bool objectShape(const SceneObject& o, int index,
                        const ModelAabbFn& modelAabb, Occluder& out) {
    {
        float cLocal[3] = {0, 0, 0};  // shape center in object-local units
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
                if (animatedModelPath(o.modelPath)) return false;
                float mn[3], mx[3];
                if (!modelAabb || !modelAabb(o, mn, mx)) return false;
                for (int k = 0; k < 3; ++k) {
                    cLocal[k] = 0.5f * (mn[k] + mx[k]) * o.scale[k];
                    half[k] = 0.5f * std::fabs((mx[k] - mn[k]) * o.scale[k]);
                }
                break;
            }
            default:
                return false;  // markers, lights, decals, mirrors, portals...
        }
        Occluder oc;
        oc.sphere = sphere;
        oc.objIndex = index;
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
        out = oc;
        return true;
    }
}

std::vector<Occluder> collectOccluders(const std::vector<SceneObject>& objects,
                                       const ModelAabbFn& modelAabb) {
    std::vector<Occluder> out;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (!objects[i].castShadow) continue;  // per-object opt-out (Properties)
        Occluder oc;
        if (objectShape(objects[i], i, modelAabb, oc)) out.push_back(oc);
    }
    return out;
}

// Distance from a world point to the analytic shape's SURFACE (negative-ish
// clamped to <= 0 inside) plus the unit direction toward it. Both the AO
// response and the emissive-light response are built on this one query - the
// generated game (occShapeAt in templates.cpp) and the viewport shader are
// twins of it.
static void occShapeAt(const Occluder& oc, const float wp[3], float& dist,
                float toOcc[3]) {
    const float rel[3] = {wp[0] - oc.pos[0], wp[1] - oc.pos[1],
                          wp[2] - oc.pos[2]};
    if (oc.sphere) {
        const float d =
            std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
        dist = d - oc.half[0];
        if (d > 0.0001f) {
            toOcc[0] = -rel[0] / d, toOcc[1] = -rel[1] / d, toOcc[2] = -rel[2] / d;
        } else {
            toOcc[0] = 0, toOcc[1] = 1, toOcc[2] = 0;
        }
    } else {
        float l[3], q[3], dv[3];
        for (int k = 0; k < 3; ++k) {
            l[k] = rel[0] * oc.axis[k][0] + rel[1] * oc.axis[k][1] +
                   rel[2] * oc.axis[k][2];
            q[k] = l[k] < -oc.half[k] ? -oc.half[k]
                                      : (l[k] > oc.half[k] ? oc.half[k] : l[k]);
            dv[k] = l[k] - q[k];
        }
        dist = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
        if (dist > 0.0001f) {
            for (int k = 0; k < 3; ++k) {
                const float w = dv[0] * oc.axis[0][k] + dv[1] * oc.axis[1][k] +
                                dv[2] * oc.axis[2][k];
                toOcc[k] = -w / dist;
            }
        } else {
            toOcc[0] = 0, toOcc[1] = 1, toOcc[2] = 0;
        }
    }
}

float occluderOcclusionAt(const Occluder& oc, const float wp[3],
                          const float n[3], float range) {
    float dist;
    float toOcc[3];  // direction from the point toward the occluder surface
    occShapeAt(oc, wp, dist, toOcc);
    if (dist <= 0.0f) return 1.0f;  // touching / inside
    float fade = 1.0f - dist / range;
    if (fade <= 0.0f) return 0.0f;
    fade *= fade;
    // full occlusion facing the occluder, ~0.35 side-on, zero facing away
    float w = 0.35f + 0.65f * (n[0] * toOcc[0] + n[1] * toOcc[1] + n[2] * toOcc[2]);
    if (w <= 0.0f) return 0.0f;
    if (w > 1.0f) w = 1.0f;
    return fade * w;
}

std::vector<Emitter> collectEmitters(const std::string& projectDir,
                                     const std::vector<SceneObject>& objects,
                                     const ModelAabbFn& modelAabb,
                                     GlowCache* cache) {
    std::vector<Emitter> out;
    GlowCache own;
    GlowCache& c = cache ? *cache : own;
    for (int i = 0; i < (int)objects.size(); ++i) {
        const SceneObject& o = objects[i];
        // Note castShadow is deliberately NOT consulted: a glowing sign that
        // casts no shadow still lights the wall behind it.
        const MaterialGlow& g = materialGlow(projectDir, o.materialPath, c);
        if (!g.glows() || g.range <= 0.0f || g.light <= 0.0f) continue;
        Emitter em;
        if (!objectShape(o, i, modelAabb, em.shape)) continue;
        // The AUTHORED glow color, not Ke: Ke has the white-hot core folded
        // in, which is an exposure effect on the emitter's own surface - a
        // green lamp still casts green light however blown out it looks.
        for (int k = 0; k < 3; ++k) em.color[k] = g.color[k];
        em.range = g.range;
        em.bright = g.light;
        out.push_back(em);
    }
    return out;
}

bool shapeBlocksRay(const Occluder& oc, const float origin[3],
                    const float dir[3], float maxT) {
    const float rel[3] = {origin[0] - oc.pos[0], origin[1] - oc.pos[1],
                          origin[2] - oc.pos[2]};
    if (oc.sphere) {
        const float b = rel[0] * dir[0] + rel[1] * dir[1] + rel[2] * dir[2];
        const float c =
            rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2] -
            oc.half[0] * oc.half[0];
        if (c < 0.0f) return true;   // origin inside the sphere
        if (b > 0.0f) return false;  // sphere is behind the ray
        const float disc = b * b - c;
        if (disc < 0.0f) return false;
        const float t = -b - std::sqrt(disc);
        return t >= 0.0f && t <= maxT;
    }
    // Oriented box: the standard slab test, in the box's own frame.
    float t0 = 0.0f, t1 = maxT;
    for (int k = 0; k < 3; ++k) {
        const float e = rel[0] * oc.axis[k][0] + rel[1] * oc.axis[k][1] +
                        rel[2] * oc.axis[k][2];
        const float f = dir[0] * oc.axis[k][0] + dir[1] * oc.axis[k][1] +
                        dir[2] * oc.axis[k][2];
        if (std::fabs(f) < 1e-6f) {
            if (e < -oc.half[k] || e > oc.half[k]) return false;  // parallel, outside
            continue;
        }
        float ta = (-oc.half[k] - e) / f;
        float tb = (oc.half[k] - e) / f;
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    return true;
}

// Visibility of the emitter from `wp`: the fraction of the shadow rays that
// reach it. Ray 0 goes to the nearest surface point (`toEm` * `dist`, the old
// single hard ray); rays 1..samples-1 spread over the emitter's silhouette -
// its extent projected onto the plane perpendicular to ray 0 - which is what
// turns the edge into a penumbra. Rays aimed below the receiver's horizon are
// left out of the vote entirely: that part of the source is invisible for
// geometric reasons the facing term already accounts for, and counting it as
// blocked would darken surfaces no occluder touches.
static float emitterVisibility(const Emitter& em, const float wp[3],
                               const float n[3], float dist,
                               const float toEm[3],
                               const std::vector<const Occluder*>& blockers,
                               int samples) {
    const float bias = 0.02f;
    const float o[3] = {wp[0] + toEm[0] * bias, wp[1] + toEm[1] * bias,
                        wp[2] + toEm[2] * bias};
    auto rayReaches = [&](const float dir[3], float maxT) {
        for (const Occluder* oc : blockers) {
            if (oc->objIndex == em.shape.objIndex) continue;  // the source
            if (shapeBlocksRay(*oc, o, dir, maxT)) return false;
        }
        return true;
    };
    int hits = 0, votes = 0;
    // ray 0: the nearest surface point
    ++votes;
    if (rayReaches(toEm, dist - 2.0f * bias)) ++hits;
    if (samples > 1) {
        // Orthonormal basis around ray 0. Seeded from the world axis least
        // aligned with it, so it is stable and identical in every twin.
        float t[3], b[3];
        {
            const float ax = std::fabs(toEm[0]), ay = std::fabs(toEm[1]);
            const float az = std::fabs(toEm[2]);
            float up[3] = {0, 0, 1};
            if (ax <= ay && ax <= az)
                up[0] = 1, up[1] = 0, up[2] = 0;
            else if (ay <= az)
                up[0] = 0, up[1] = 1, up[2] = 0;
            t[0] = toEm[1] * up[2] - toEm[2] * up[1];
            t[1] = toEm[2] * up[0] - toEm[0] * up[2];
            t[2] = toEm[0] * up[1] - toEm[1] * up[0];
            const float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            const float inv = tl > 1e-6f ? 1.0f / tl : 0.0f;
            t[0] *= inv, t[1] *= inv, t[2] *= inv;
            b[0] = toEm[1] * t[2] - toEm[2] * t[1];
            b[1] = toEm[2] * t[0] - toEm[0] * t[2];
            b[2] = toEm[0] * t[1] - toEm[1] * t[0];
        }
        // Half-extents of the shape's silhouette along t and b (the support
        // function of an oriented box; a sphere projects to its radius).
        float rt = em.shape.half[0], rb = rt;
        if (!em.shape.sphere) {
            rt = rb = 0.0f;
            for (int k = 0; k < 3; ++k) {
                const float* a = em.shape.axis[k];
                rt += em.shape.half[k] *
                      std::fabs(a[0] * t[0] + a[1] * t[1] + a[2] * t[2]);
                rb += em.shape.half[k] *
                      std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
            }
        }
        const int extra = std::min(samples - 1, 7);
        for (int s = 0; s < extra; ++s) {
            const float du = kEmisShadowDisk[s][0] * rt;
            const float dv = kEmisShadowDisk[s][1] * rb;
            // Sample point on the emitter's silhouette, around its CENTER -
            // the nearest point is already ray 0.
            const float sp[3] = {em.shape.pos[0] + t[0] * du + b[0] * dv,
                                 em.shape.pos[1] + t[1] * du + b[1] * dv,
                                 em.shape.pos[2] + t[2] * du + b[2] * dv};
            float d[3] = {sp[0] - o[0], sp[1] - o[1], sp[2] - o[2]};
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len <= 2.0f * bias) continue;
            const float inv = 1.0f / len;
            d[0] *= inv, d[1] *= inv, d[2] *= inv;
            if (n[0] * d[0] + n[1] * d[1] + n[2] * d[2] <= 0.0f) continue;
            ++votes;
            if (rayReaches(d, len - bias)) ++hits;
        }
    }
    return votes > 0 ? (float)hits / (float)votes : 1.0f;
}

void emitterLightAt(const Emitter& em, const float wp[3], const float n[3],
                    float outRgb[3],
                    const std::vector<const Occluder*>* blockers,
                    int shadowSamples) {
    outRgb[0] = outRgb[1] = outRgb[2] = 0.0f;
    float dist;
    float toEm[3];
    occShapeAt(em.shape, wp, dist, toEm);
    if (dist >= em.range) return;
    float vis = 1.0f;
    if (blockers && !blockers->empty() && dist > 0.02f) {
        // The origin is nudged off the surface (inside emitterVisibility) so a
        // solid touching the receiver - a prop resting on the floor - does not
        // shadow it with its own contact face; the rays also stop just short of
        // the emitter.
        vis = emitterVisibility(em, wp, n, dist, toEm, *blockers, shadowSamples);
        if (vis <= 0.0f) return;
    }
    if (dist < 0.0f) dist = 0.0f;  // touching / inside: full strength
    float fade = 1.0f - dist / em.range;
    fade *= fade;  // same rounder pool as the point lights
    // Facing weight: half-Lambert SQUARED. These emitters are area sources (a
    // lava plate, a neon strip), not points, so a plain max(0, N.L) lights one
    // face of a box fully and its neighbour not at all - a seam right on the
    // corner. A linear wrap (what the occluder term uses) fixes that but still
    // reaches zero at a finite angle, and in a dark scene that angle reads as
    // a hard shading edge. ((1 + N.L) / 2)^2 is smooth everywhere, hits zero
    // ONLY at N.L = -1, and stays near-zero across the back hemisphere - the
    // faint fill a real bounce would give. Side-on lands at 0.25.
    float w = 0.5f + 0.5f * (n[0] * toEm[0] + n[1] * toEm[1] + n[2] * toEm[2]);
    if (w <= 0.0f) return;
    w *= w;
    const float k = em.bright * fade * w * vis;
    for (int c = 0; c < 3; ++c) outRgb[c] = k * em.color[c];
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

namespace {

// Bilinear height over the vertex grid, clamped at the borders - the host
// twin of the generated terrainHeightAtScene (keep the sampling identical).
float heightAtWorld(const std::vector<float>& heights, int w, int d,
                    float width, float depth, float x, float z) {
    if (w < 2 || d < 2 || (int)heights.size() < w * d) return 0.0f;
    float gx = (x + width * 0.5f) / width * (w - 1);
    float gz = (z + depth * 0.5f) / depth * (d - 1);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > w - 1.001f) gx = w - 1.001f;
    if (gz > d - 1.001f) gz = d - 1.001f;
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return heights[(size_t)b * w + a]; };
    const float t = h(ix, iz) * (1 - fx) + h(ix + 1, iz) * fx;
    const float b = h(ix, iz + 1) * (1 - fx) + h(ix + 1, iz + 1) * fx;
    return t * (1 - fz) + b * fz;
}

// The wall-base-softened ground contact term - the host copy of the
// generated aoShadeMul ground branch (and the viewport shader's). Sync all
// three when the formula moves.
float groundOcclusion(float dy, float ny, float range) {
    if (dy < 0.0f) dy = 0.0f;
    if (dy >= range) return 0.0f;
    float fade = 1.0f - dy / range;
    fade *= fade;
    float horiz = 0.5f - 0.5f * ny;
    if (horiz < 0.0f) horiz = 0.0f;
    return 0.7f * fade * horiz;
}

int pow2Up(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

}  // namespace

AoImage terrainAOMap(const std::vector<float>& heights, int w, int d,
                     float width, float depth,
                     const std::vector<Occluder>& occs,
                     const std::vector<Emitter>& ems, float radiusWorld,
                     float strength, bool aoOn, const LightFn* gi) {
    AoImage out;
    if (width <= 0 || depth <= 0) return out;
    const bool bakeOcc = aoOn && strength > 0.0f;
    // With a GI light source the RGB channel always has content - "no
    // emitters" no longer means "no light", it means daylight.
    const bool bakeLight = gi != nullptr || !ems.empty();
    out.gi = gi != nullptr;
    if (!bakeOcc && !bakeLight) return out;
    const bool hasHeights = w >= 2 && d >= 2 && (int)heights.size() == w * d;
    // ~4 texels per terrain unit, power of two. Capped at 256: the maps ship
    // as RGBA32 (see texbake - the engine's palettized alpha path can't carry
    // the gradient), so 256x256 = 256 KB of the ~1.33 MB GS texture budget.
    int size = pow2Up((int)std::max(width, depth) * 4);
    if (size < 64) size = 64;
    if (size > 256) size = 256;
    out.size = size;
    if (bakeOcc) out.alpha.assign((size_t)size * size, 0);
    if (bakeLight) out.light.assign((size_t)size * size * 3, 0);

    // Shadow casters per emitter, pruned ONCE. Every shadow ray for emitter e
    // ends on its surface and is at most `range` long, so it stays inside a
    // ball of radius (range + the emitter's half-diagonal) around the emitter
    // - only occluders overlapping that ball can ever block it. Without this
    // the terrain would run size*size*|ems|*|occs| slab tests.
    std::vector<std::vector<const Occluder*>> emBlock(ems.size());
    if (bakeLight) {
        for (size_t e = 0; e < ems.size(); ++e) {
            const Emitter& em = ems[e];
            const float emR =
                em.range + std::sqrt(em.shape.half[0] * em.shape.half[0] +
                                     em.shape.half[1] * em.shape.half[1] +
                                     em.shape.half[2] * em.shape.half[2]);
            for (const Occluder& oc : occs) {
                if (oc.objIndex == em.shape.objIndex) continue;  // the source
                const float dx = oc.pos[0] - em.shape.pos[0];
                const float dy = oc.pos[1] - em.shape.pos[1];
                const float dz = oc.pos[2] - em.shape.pos[2];
                const float reach =
                    emR + std::sqrt(oc.half[0] * oc.half[0] +
                                    oc.half[1] * oc.half[1] +
                                    oc.half[2] * oc.half[2]);
                if (dx * dx + dy * dy + dz * dz <= reach * reach)
                    emBlock[e].push_back(&oc);
            }
        }
    }

    const float stepX = hasHeights ? width / (w - 1) : width;
    const float stepZ = hasHeights ? depth / (d - 1) : depth;
    const float scanRadius = radiusWorld * 3.0f;  // matches terrainAO's caller
    const float minStep = std::min(stepX, stepZ);
    int maxSteps = (int)std::ceil(scanRadius / std::max(minStep, 0.001f));
    if (maxSteps < 2) maxSteps = 2;
    if (maxSteps > 48) maxSteps = 48;
    const float dirs[8][2] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1},
                              {0.7071f, 0.7071f},  {0.7071f, -0.7071f},
                              {-0.7071f, 0.7071f}, {-0.7071f, -0.7071f}};

    bool anyOcc = false, anyLight = false;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const float x = ((i + 0.5f) / size - 0.5f) * width;
            const float z = ((j + 0.5f) / size - 0.5f) * depth;
            float occ = 0.0f;
            float h0 = 0.0f;
            float n[3] = {0, 1, 0};
            if (hasHeights) {
                h0 = heightAtWorld(heights, w, d, width, depth, x, z);
                // central-difference normal, same spirit as the chunk builders
                const float hx0 = heightAtWorld(heights, w, d, width, depth, x - stepX, z);
                const float hx1 = heightAtWorld(heights, w, d, width, depth, x + stepX, z);
                const float hz0 = heightAtWorld(heights, w, d, width, depth, x, z - stepZ);
                const float hz1 = heightAtWorld(heights, w, d, width, depth, x, z + stepZ);
                n[0] = hx0 - hx1;
                n[1] = 2.0f * minStep;
                n[2] = hz0 - hz1;
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 1e-5f) n[0] /= len, n[1] /= len, n[2] /= len;
                // heightmap self-occlusion: the same horizon scan as
                // terrainAO, on bilinear heights (alpha only - an
                // emitters-only map skips the whole scan)
                for (int dir = 0; bakeOcc && dir < 8; ++dir) {
                    float maxSin = 0.0f;
                    for (int k = 1; k <= maxSteps; ++k) {
                        const float dist = minStep * k;
                        if (dist > scanRadius) break;
                        const float sx = x + dirs[dir][0] * dist;
                        const float sz = z + dirs[dir][1] * dist;
                        if (sx < -width * 0.5f || sx > width * 0.5f ||
                            sz < -depth * 0.5f || sz > depth * 0.5f)
                            break;
                        const float dh =
                            heightAtWorld(heights, w, d, width, depth, sx, sz) - h0;
                        if (dh <= 0.0f) continue;
                        const float s = dh / std::sqrt(dh * dh + dist * dist);
                        if (s > maxSin) maxSin = s;
                    }
                    occ += maxSin / 8.0f;
                }
            }
            const float wp[3] = {x, h0, z};
            const size_t texel = (size_t)j * size + i;
            // Sub-sample positions inside this texel's footprint. The horizon
            // scan above stays ONE sample - it is a slow function of position
            // and the scan is the expensive part - but the occluder contact
            // term and the emissive light both carry near-hard edges, and a
            // point sample of those aliases into the staircase bilinear then
            // reconstructs (see kSuper).
            const float sxStep = width / size, szStep = depth / size;
            const float invSuper = 1.0f / (kSuper * kSuper);
            auto subPoint = [&](int sx, int sy, float p[3]) {
                p[0] = x + ((sx + 0.5f) / kSuper - 0.5f) * sxStep;
                p[1] = h0;
                p[2] = z + ((sy + 0.5f) / kSuper - 0.5f) * szStep;
            };
            if (bakeOcc) {
                float occAcc = 0.0f;
                for (int sy = 0; sy < kSuper; ++sy)
                    for (int sx = 0; sx < kSuper; ++sx) {
                        float sp[3];
                        subPoint(sx, sy, sp);
                        for (const Occluder& oc : occs)
                            occAcc += occluderOcclusionAt(oc, sp, n, radiusWorld);
                    }
                occ += occAcc * invSuper;
                if (occ > 1.0f) occ = 1.0f;
                const uint8_t a = (uint8_t)(255.0f * strength * occ + 0.5f);
                out.alpha[texel] = a;
                anyOcc |= (a != 0);
            }
            // Emissive light, in framebuffer units - the additive pass adds
            // these bytes straight onto the frame, modulated by the terrain's
            // own base tint (which rides in that pass's vertex colors, so the
            // bake stays independent of the terrain material).
            if (!bakeLight) continue;
            float add[3] = {0, 0, 0};
            // GI samples on a coarser sub-grid - see kSuperGi.
            const int sup = gi ? kSuperGi : kSuper;
            const float invSup = 1.0f / (sup * sup);
            for (int sy = 0; sy < sup; ++sy)
                for (int sx = 0; sx < sup; ++sx) {
                    float sp[3];
                    // subPoint divides by kSuper; re-derive for `sup`.
                    sp[0] = x + ((sx + 0.5f) / sup - 0.5f) * sxStep;
                    sp[1] = h0;
                    sp[2] = z + ((sy + 0.5f) / sup - 0.5f) * szStep;
                    if (gi) {
                        // Seeded by the texel AND its sub-sample, so the
                        // sub-samples of one texel do not share a ray set and
                        // waste the supersampling.
                        float l[3];
                        (*gi)(sp, n,
                              (uint32_t)(texel * 17u + (uint32_t)(sy * sup + sx)),
                              l);
                        for (int k = 0; k < 3; ++k) add[k] += l[k] * invSup;
                        continue;
                    }
                    for (size_t e = 0; e < ems.size(); ++e) {
                        float l[3];
                        emitterLightAt(ems[e], sp, n, l, &emBlock[e]);
                        for (int k = 0; k < 3; ++k) add[k] += l[k] * invSup;
                    }
                }
            const float dz = bayerDither(i, j);
            for (int k = 0; k < 3; ++k) {
                float v = 255.0f * add[k] + dz;
                if (v <= 0.0f) continue;  // stays 0, no dither noise
                if (v > 255.0f) v = 255.0f;
                const uint8_t b = (uint8_t)(v + 0.5f);
                out.light[texel * 3 + k] = b;
                if (b) anyLight = true;
            }
        }
    }
    out.hasAlpha = anyOcc;
    out.hasLight = anyLight;
    if (!anyOcc) out.alpha.clear();
    if (!anyLight) out.light.clear();
    if (!anyOcc && !anyLight) out.size = 0;
    // Alpha floor (kMinLightmapAlpha), applied AFTER the content decisions
    // above: without it the GS alpha test discards the additive light pass
    // wherever this map has no occlusion, which is most of an open field. An
    // emitters-only map has no alpha array at all, so give it a flat one.
    if (out.size > 0) {
        if (out.alpha.empty())
            out.alpha.assign((size_t)out.size * out.size, kMinLightmapAlpha);
        else
            for (uint8_t& a : out.alpha)
                if (a < kMinLightmapAlpha) a = kMinLightmapAlpha;
    }
    return out;
}

namespace {

// (u,v) -> local position + local normal for one atlas region of a
// primitive. The forward mapping (the generated builders' UV emission and
// primmesh's) inverted - keep in sync with addBox/addSphere/addCylinder/
// addCone/addPlane in templates.cpp.
void regionPoint(PrimitiveType type, int region, float u, float v,
                 float pos[3], float nrm[3]) {
    const float h = 0.5f;
    auto set = [&](float px, float py, float pz, float nx, float ny, float nz) {
        pos[0] = px, pos[1] = py, pos[2] = pz;
        nrm[0] = nx, nrm[1] = ny, nrm[2] = nz;
    };
    switch (type) {
        default:  // Box / SavePoint: face order +X,-X,+Y,-Y,+Z,-Z (addBox)
            switch (region) {
                case 0: set(h, -h + u, -h + v, 1, 0, 0); break;
                case 1: set(-h, -h + u, h - v, -1, 0, 0); break;
                case 2: set(-h + v, h, -h + u, 0, 1, 0); break;
                case 3: set(-h + v, -h, h - u, 0, -1, 0); break;
                case 4: set(-h + u, -h + v, h, 0, 0, 1); break;
                default: set(h - u, -h + v, -h, 0, 0, -1); break;
            }
            break;
        case PrimitiveType::Sphere: {
            // u -> longitude, v -> latitude (addSphere: tu = sl/slices,
            // tv = st/stacks; theta 0 = +Y pole)
            const float phi = 2.0f * kPi * u;
            const float theta = kPi * v;
            const float sx = std::sin(theta) * std::cos(phi);
            const float sy = std::cos(theta);
            const float sz = std::sin(theta) * std::sin(phi);
            set(h * sx, h * sy, h * sz, sx, sy, sz);
            break;
        }
        case PrimitiveType::Cylinder:
            if (region == 0) {  // side: u wraps, v 0 = top, 1 = bottom
                const float a = 2.0f * kPi * u;
                set(h * std::cos(a), h * (1.0f - 2.0f * v), h * std::sin(a),
                    std::cos(a), 0, std::sin(a));
            } else if (region == 1) {  // +Y cap, planar (x+0.5, z+0.5)
                set(u - 0.5f, h, v - 0.5f, 0, 1, 0);
            } else {  // -Y cap
                set(u - 0.5f, -h, v - 0.5f, 0, -1, 0);
            }
            break;
        case PrimitiveType::Cone:
            if (region == 0) {  // side: v 0 = apex, 1 = base rim
                const float a = 2.0f * kPi * u;
                const float rv = h * v;
                const float nl = 0.894f, ny = 0.447f;
                set(rv * std::cos(a), h * (1.0f - 2.0f * v), rv * std::sin(a),
                    nl * std::cos(a), ny, nl * std::sin(a));
            } else {  // base, planar
                set(u - 0.5f, -h, v - 0.5f, 0, -1, 0);
            }
            break;
        case PrimitiveType::Plane:
            // addPlane via pushQuad: top face u -> +Z, v -> +X; bottom
            // mirrored (u -> -Z)
            if (region == 0)
                set(-h + v, 0, -h + u, 0, 1, 0);
            else
                set(-h + v, 0, h - u, 0, -1, 0);
            break;
    }
}

// World-size estimate (u, v) of one region - drives its texel allocation.
void regionWorldSize(PrimitiveType type, int region, const float scale[3],
                     float& su, float& sv) {
    const float sx = std::fabs(scale[0]);
    const float sy = std::fabs(scale[1]);
    const float sz = std::fabs(scale[2]);
    switch (type) {
        default:  // box faces
            switch (region) {
                case 0:
                case 1: su = sy, sv = sz; break;
                case 2:
                case 3: su = sz, sv = sx; break;
                default: su = sx, sv = sy; break;
            }
            break;
        case PrimitiveType::Sphere: {
            const float s = std::max(sx, std::max(sy, sz));
            su = 3.14f * s * 0.5f + s, sv = 1.57f * s * 0.5f + s;
            break;
        }
        case PrimitiveType::Cylinder:
            if (region == 0)
                su = 3.14f * std::max(sx, sz), sv = sy;
            else
                su = sx, sv = sz;
            break;
        case PrimitiveType::Cone:
            if (region == 0)
                su = 3.14f * std::max(sx, sz), sv = sy;
            else
                su = sx, sv = sz;
            break;
        case PrimitiveType::Plane: su = sz, sv = sx; break;
    }
}

int regionCountFor(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box:
        case PrimitiveType::SavePoint: return 6;
        case PrimitiveType::Sphere: return 1;
        case PrimitiveType::Cylinder: return 3;
        case PrimitiveType::Cone: return 2;
        case PrimitiveType::Plane: return 2;
        default: return 0;
    }
}

}  // namespace

SceneLightAtlas bakeSceneLightAtlas(const Project& p, const SceneData& sc,
                              const ModelAabbFn& modelAabb, const LightFn* gi) {
    SceneLightAtlas out;
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    out.firstRegion.assign(sc.objects.size(), -1);
    out.lit.assign(sc.objects.size(), 0);
    out.gi = gi != nullptr;
    // Three independent reasons to build an atlas; any one is enough. A GI
    // light source is the third: with it every eligible surface has a light
    // channel whether or not anything in the scene glows.
    const bool aoOn = rs.aoEnabled && rs.aoStrength > 0.0f;
    const std::vector<Emitter> ems = collectEmitters(p.dir, sc.objects, modelAabb);
    if (!aoOn && ems.empty() && !gi) return out;

    // Collected regardless of the AO preference: with emitters present these
    // same shapes also SHADOW the baked light (a wall must not be lit through).
    const std::vector<Occluder> occs =
        (aoOn || !ems.empty()) ? collectOccluders(sc.objects, modelAabb)
                               : std::vector<Occluder>();

    // Region list with pixel sizes; shelf-packed into the atlas. `weight`
    // scales the texel density per region (see the importance pre-pass below);
    // su/sv is the region's world size, cached because the sizing is now run
    // once per bisection step rather than once per halving.
    struct Region {
        int obj, idx;
        int px, py, w, h;
        float su, sv;
        float wu, wv;  // per-AXIS density weights (see the pre-pass)
    };
    std::vector<Region> regions;
    GlowCache glowCache;
    // Collected once: the set of object names any flow graph can move.
    const std::set<std::string> movableRefs = project::runtimeRefNames(p, sc.objects);
    for (int oi = 0; oi < (int)sc.objects.size(); ++oi) {
        const SceneObject& o = sc.objects[oi];
        const int rc = regionCountFor(o.type);
        if (rc == 0) continue;
        // Runtime movers keep the vertex bake (which re-bakes on rebuild);
        // an atlas region would GLUE the baked light to the moved surface -
        // tip a lightmapped cylinder over and it carries a contact shadow that
        // matches nothing. project::objectRuntimeMovable is the same predicate
        // static batching and the live catch areas already use: physics,
        // pickable, usable, save-state, streamed, owning a graph, or named by
        // one. `bakedLighting` is the manual override on top, for the channels
        // no build-time scan can see (Live Link, a Raycast latch, a custom
        // node's object output).
        if (!o.bakedLighting || project::objectRuntimeMovable(o, movableRefs))
            continue;
        // An emissive surface takes neither bake: the atlas passes multiply and
        // add per pixel AFTER the emissive floor is already in the vertex
        // colors, so occlusion would darken a surface that is supposed to be
        // self-lit and extra light would only wash it out. Emitters keep the
        // per-vertex path, where the floor wins.
        if (materialGlow(p.dir, o.materialPath, glowCache).glows()) continue;
        for (int r = 0; r < rc; ++r) {
            Region rg{oi, r, 0, 0, 0, 0, 0.0f, 0.0f, 1.0f, 1.0f};
            regionWorldSize(o.type, r, o.scale, rg.su, rg.sv);
            regions.push_back(rg);
        }
    }
    if (regions.empty()) return out;

    // --- per-object pruning, shared by the pre-pass and the rasterizer -------
    // The dcache lesson: the local lists are collected ONCE per object, never
    // per texel. Both passes walk the regions in object order, so one call per
    // object boundary is enough for each.
    std::vector<const Occluder*> local;
    std::vector<const Emitter*> localEm;
    // Shadow casters for this object's emitters. Pruned by the EMITTER reach,
    // which is unrelated to aoRadius, so it cannot share `local`.
    std::vector<const Occluder*> localBlock;
    // The additive pass adds a flat color, so the surface's own diffuse has to
    // be folded in here - light * Kd * the object tint, in framebuffer units.
    float recvTint[3] = {1, 1, 1};
    bool recvTextured = false;
    auto prepareObject = [&](int oi) {
        const SceneObject& o = sc.objects[oi];
        local.clear();
        localEm.clear();
        localBlock.clear();
        {
            const MaterialGlow& mg = materialGlow(p.dir, o.materialPath, glowCache);
            for (int k = 0; k < 3; ++k) recvTint[k] = o.color[k] * mg.kd[k];
            recvTextured = mg.textured;
        }
        const float emReach =
            0.87f * std::sqrt(o.scale[0] * o.scale[0] + o.scale[1] * o.scale[1] +
                              o.scale[2] * o.scale[2]);
        for (const Emitter& em : ems) {
            if (em.shape.objIndex == oi) continue;
            const float dx = em.shape.pos[0] - o.position[0];
            const float dy = em.shape.pos[1] - o.position[1];
            const float dz = em.shape.pos[2] - o.position[2];
            const float reach = emReach + em.range +
                                std::sqrt(em.shape.half[0] * em.shape.half[0] +
                                          em.shape.half[1] * em.shape.half[1] +
                                          em.shape.half[2] * em.shape.half[2]);
            if (dx * dx + dy * dy + dz * dz <= reach * reach) localEm.push_back(&em);
        }
        if (!localEm.empty()) {
            float maxRange = 0.0f;
            for (const Emitter* e : localEm)
                if (e->range > maxRange) maxRange = e->range;
            for (const Occluder& oc : occs) {
                if (oc.objIndex == oi) continue;  // the receiver itself
                const float dx = oc.pos[0] - o.position[0];
                const float dy = oc.pos[1] - o.position[1];
                const float dz = oc.pos[2] - o.position[2];
                const float reach =
                    emReach + maxRange +
                    std::sqrt(oc.half[0] * oc.half[0] + oc.half[1] * oc.half[1] +
                              oc.half[2] * oc.half[2]);
                if (dx * dx + dy * dy + dz * dz <= reach * reach)
                    localBlock.push_back(&oc);
            }
        }
        const float reachBase = emReach + rs.aoRadius;
        for (const Occluder& oc : occs) {
            if (oc.objIndex == oi) continue;
            const float dx = oc.pos[0] - o.position[0];
            const float dy = oc.pos[1] - o.position[1];
            const float dz = oc.pos[2] - o.position[2];
            const float reach =
                reachBase + std::sqrt(oc.half[0] * oc.half[0] +
                                      oc.half[1] * oc.half[1] +
                                      oc.half[2] * oc.half[2]);
            if (dx * dx + dy * dy + dz * dz <= reach * reach) local.push_back(&oc);
        }
    };
    // (u, v) in a region -> world point + world normal (the pushVert transform:
    // scale, rotate, translate).
    auto surfaceAt = [&](const SceneObject& o, int region, float u, float v,
                         float wp[3], float n[3]) {
        float lp[3], ln[3];
        regionPoint(o.type, region, u, v, lp, ln);
        lp[0] *= o.scale[0], lp[1] *= o.scale[1], lp[2] *= o.scale[2];
        const V3 rp = rotated({lp[0], lp[1], lp[2]}, o.rotation);
        const V3 rn = rotated({ln[0], ln[1], ln[2]}, o.rotation);
        wp[0] = rp.x + o.position[0], wp[1] = rp.y + o.position[1];
        wp[2] = rp.z + o.position[2];
        n[0] = rn.x, n[1] = rn.y, n[2] = rn.z;
    };
    auto occlusionAt = [&](const float wp[3], const float n[3]) {
        float occ = 0.0f;
        for (const Occluder* oc : local)
            occ += occluderOcclusionAt(*oc, wp, n, rs.aoRadius);
        // ground term; an empty heightmap samples the y = 0 plane
        const float ground =
            heightAtWorld(sc.heights, sc.hmW, sc.hmD, (float)sc.terrain.width,
                          (float)sc.terrain.depth, wp[0], wp[2]);
        occ += groundOcclusion(wp[1] - ground, n[1], rs.aoRadius);
        return occ > 1.0f ? 1.0f : occ;
    };
    auto lightAt = [&](const float wp[3], const float n[3], uint32_t seed,
                       float add[3]) {
        if (gi) {
            (*gi)(wp, n, seed, add);
            return;
        }
        add[0] = add[1] = add[2] = 0.0f;
        for (const Emitter* em : localEm) {
            float l[3];
            emitterLightAt(*em, wp, n, l, &localBlock);
            for (int k = 0; k < 3; ++k) add[k] += l[k];
        }
    };
    // "Does this receiver have a light channel at all?" Without GI that is
    // "an emitter reaches it"; with GI every untextured surface has one,
    // because daylight and bounce reach everything. A TEXTURED receiver never
    // does, GI or not - a flat additive term blows out its dark texels, so it
    // stays on the vertex path (which multiplies the texture instead) and
    // takes its GI from the probe grid.
    auto hasLight = [&] { return !recvTextured && (gi || !localEm.empty()); };

    // --- importance pre-pass ------------------------------------------------
    // Sizing every region by world area alone spends most of the atlas on
    // surfaces that receive nothing - pedestal undersides, wall backs, faces
    // turned away from every lamp - while the handful of lit faces stay blocky.
    // A coarse probe grid says how strong a signal each region can actually
    // carry, and the texel density is scaled by its square root, so the AREA a
    // region gets is proportional to what it receives.
    constexpr int kProbe = 6;             // kProbe^2 probes per region
    constexpr float kMinWeight = 0.12f;   // floor: a region never disappears
    {
        int lastObj = -1;
        std::vector<float> sig((size_t)kProbe * kProbe, 0.0f);
        for (Region& rg : regions) {
            if (rg.obj != lastObj) prepareObject(lastObj = rg.obj);
            const SceneObject& o = sc.objects[rg.obj];
            float peak = 0.0f;
            for (int j = 0; j < kProbe; ++j)
                for (int i = 0; i < kProbe; ++i) {
                    float wp[3], n[3];
                    surfaceAt(o, rg.idx, (i + 0.5f) / kProbe, (j + 0.5f) / kProbe,
                              wp, n);
                    float s = 0.0f;
                    if (aoOn) s = rs.aoStrength * occlusionAt(wp, n);
                    if (hasLight()) {
                        float add[3];
                        // The pre-pass only decides texel BUDGET, so it may
                        // sample coarsely - but its seed still has to be
                        // stable, hence (region, probe).
                        lightAt(wp, n,
                                (uint32_t)((&rg - regions.data()) * 37 +
                                           j * kProbe + i),
                                add);
                        for (int k = 0; k < 3; ++k) {
                            const float lv = add[k] * recvTint[k];
                            if (lv > s) s = lv;
                        }
                    }
                    sig[(size_t)j * kProbe + i] = s;
                    if (s > peak) peak = s;
                }
            if (peak > 1.0f) peak = 1.0f;
            const float w = peak > 0.002f ? std::max(kMinWeight, std::sqrt(peak))
                                          : kMinWeight;
            // How fast the signal moves ALONG each axis, per world unit. A
            // region's two axes rarely deserve the same density: an alley wall
            // 18 units long and 5 high carries a steep ramp up its height and a
            // lazy one down its length, and spending texels uniformly wastes
            // them on the lazy axis. Sizing by area alone (or capping both axes
            // at one number) got this backwards - measured.
            float du = 0.0f, dv = 0.0f;
            for (int j = 0; j < kProbe; ++j)
                for (int i = 0; i + 1 < kProbe; ++i)
                    du += std::fabs(sig[(size_t)j * kProbe + i + 1] -
                                    sig[(size_t)j * kProbe + i]);
            for (int j = 0; j + 1 < kProbe; ++j)
                for (int i = 0; i < kProbe; ++i)
                    dv += std::fabs(sig[(size_t)(j + 1) * kProbe + i] -
                                    sig[(size_t)j * kProbe + i]);
            const float spanU = std::max(rg.su, 0.001f) / kProbe;
            const float spanV = std::max(rg.sv, 0.001f) / kProbe;
            const float gu = du / (kProbe * (kProbe - 1)) / spanU + 1e-5f;
            const float gv = dv / (kProbe * (kProbe - 1)) / spanV + 1e-5f;
            // Shift density toward the steeper axis while KEEPING the area the
            // peak earned (wu * wv is w^2 either way), and clamp the shift so a
            // near-flat axis never collapses.
            float k = std::sqrt(std::sqrt(gu / gv));
            if (k < 0.5f) k = 0.5f;
            if (k > 2.0f) k = 2.0f;
            rg.wu = w * k;
            rg.wv = w / k;
        }
    }

    // --- sizing + packing ---------------------------------------------------
    constexpr float kTpu = 6.0f;  // texels per world unit at density 1
    // Interior cap per region dimension. 128 while ESTIMATING the atlas
    // dimension, so that estimate matches what it has always been; once the
    // dimension is fixed the only real limit is the image itself (+2 padding).
    // The old flat 128 bit hardest exactly where it hurt most - a long alley
    // wall would sit at half the density along its length while its short axis
    // had room to spare.
    int dimCap = 128;
    auto sizeAll = [&](float g, bool weighted) {
        long long area = 0;
        for (Region& rg : regions) {
            const float du = g * (weighted ? rg.wu : 1.0f);
            const float dv = g * (weighted ? rg.wv : 1.0f);
            rg.w = std::min(dimCap, std::max(2, (int)(rg.su * kTpu * du + 0.5f))) + 2;
            rg.h = std::min(dimCap, std::max(2, (int)(rg.sv * kTpu * dv + 0.5f))) + 2;
            area += (long long)rg.w * rg.h;
        }
        return area;
    };
    // shelf pack, tallest first
    std::vector<int> order(regions.size());
    auto packAll = [&](int size) {
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return regions[a].h > regions[b].h; });
        int cx = 0, cy = 0, shelfH = 0;
        for (int i : order) {
            Region& rg = regions[i];
            if (cx + rg.w > size) {
                cx = 0;
                cy += shelfH;
                shelfH = 0;
            }
            if (cy + rg.h > size) return false;
            rg.px = cx, rg.py = cy;
            cx += rg.w;
            if (rg.h > shelfH) shelfH = rg.h;
        }
        return true;
    };
    // The atlas DIMENSION still comes from the unweighted area, so weighting
    // never changes a project's VRAM either way - it only redistributes texels
    // inside the same image. 256 cap for the same RGBA32 reason as the terrain
    // map (an atlas that would need more just gets a lower density).
    int atlasSize =
        pow2Up((int)std::ceil(std::sqrt((double)sizeAll(1.0f, false) * 1.35)));
    if (atlasSize < 64) atlasSize = 64;
    if (atlasSize > 256) atlasSize = 256;
    dimCap = atlasSize - 2;  // a region + its padding ring must still fit
    // Largest weighted density that still packs, by a fixed-step bisection
    // (deterministic, and it uses the whole image instead of leaving the ~60%
    // a power-of-two round-up used to waste).
    float lo = 0.02f, hi = 8.0f;
    sizeAll(lo, true);
    if (!packAll(atlasSize)) return out;  // hopeless - whole scene falls back
    for (int it = 0; it < 20; ++it) {
        const float mid = 0.5f * (lo + hi);
        sizeAll(mid, true);
        if (packAll(atlasSize))
            lo = mid;
        else
            hi = mid;
    }
    sizeAll(lo, true);
    packAll(atlasSize);  // final placement at the accepted density

    out.size = atlasSize;
    out.alpha.assign((size_t)atlasSize * atlasSize, 0);
    out.light.assign((size_t)atlasSize * atlasSize * 3, 0);

    // Rasterize each region: (u,v) -> local -> world (the pushVert transform:
    // scale, rotate, translate) -> occluders (excluding self) + ground term
    // into the alpha, emitters into the RGB. Objects whose every texel comes
    // out fully lit AND unlit-by-emitters (an isolated prop in the open) are
    // dropped from the atlas afterwards - no extra passes, and they stay
    // eligible for static batching.
    std::vector<char> objHasBake(sc.objects.size(), 0);
    int lastObj = -1;
    for (const Region& rg : regions) {
        const SceneObject& o = sc.objects[rg.obj];
        if (rg.obj != lastObj) prepareObject(lastObj = rg.obj);
        // interior (the +2 padding ring stays, filled by the dilation below)
        const int iw = rg.w - 2, ih = rg.h - 2;
        for (int j = 0; j < ih; ++j) {
            for (int i = 0; i < iw; ++i) {
                // Each texel is the AVERAGE over its own footprint, not a
                // point sample at its centre. A shadow edge is very nearly
                // hard here - a neon strip 0.3 units off a wall casts a
                // penumbra of a few centimetres, well under one texel - and
                // point-sampling it writes a full-amplitude step between
                // neighbouring texels. Bilinear then reconstructs that step as
                // a one-texel ramp, which is exactly the staircase you see on
                // a wall. Averaging band-limits the edge to what the texel
                // grid can actually carry, so the reconstruction is smooth.
                const size_t texel =
                    (size_t)(rg.py + 1 + j) * atlasSize + rg.px + 1 + i;
                if (aoOn) {
                    float occAcc = 0.0f;
                    for (int sy = 0; sy < kSuper; ++sy)
                        for (int sx = 0; sx < kSuper; ++sx) {
                            const float u = (i + (sx + 0.5f) / kSuper) / iw;
                            const float v = (j + (sy + 0.5f) / kSuper) / ih;
                            float wp[3], n[3];
                            surfaceAt(o, rg.idx, u, v, wp, n);
                            occAcc += occlusionAt(wp, n);
                        }
                    const uint8_t a =
                        (uint8_t)(255.0f * rs.aoStrength * occAcc /
                                      (kSuper * kSuper) +
                                  0.5f);
                    out.alpha[texel] = a;
                    if (a) objHasBake[rg.obj] = 1;
                }
                // The light channel, folded with the receiver's own diffuse
                // and scaled to framebuffer units (255 = full white) - the
                // additive pass adds these bytes straight onto the frame.
                // GI samples on a coarser sub-grid: every one of ITS samples
                // is already an average over `rays` hemisphere directions, so
                // the 4x4 the analytic emitters need would multiply a bake
                // that is already the expensive half by four for no visible
                // gain.
                if (!hasLight()) continue;
                const int sup = gi ? kSuperGi : kSuper;
                float addAcc[3] = {0, 0, 0};
                for (int sy = 0; sy < sup; ++sy)
                    for (int sx = 0; sx < sup; ++sx) {
                        const float u = (i + (sx + 0.5f) / sup) / iw;
                        const float v = (j + (sy + 0.5f) / sup) / ih;
                        float wp[3], n[3];
                        surfaceAt(o, rg.idx, u, v, wp, n);
                        float add[3];
                        lightAt(wp, n,
                                (uint32_t)(texel * 17u +
                                           (uint32_t)(sy * sup + sx)),
                                add);
                        for (int k = 0; k < 3; ++k) addAcc[k] += add[k];
                    }
                const float invS = 1.0f / (sup * sup);
                // GI owns this surface's whole shade, so its region must be
                // kept even where the answer is (nearly) black - a dark
                // corner is a RESULT here, not an absence. Without this the
                // corner would fall back to the vertex path and come out
                // BRIGHTER than the lit wall beside it.
                if (gi) objHasBake[rg.obj] = 1, out.lit[rg.obj] = 1;
                {
                    const float dz = bayerDither(rg.px + 1 + i, rg.py + 1 + j);
                    for (int k = 0; k < 3; ++k) {
                        float v = 255.0f * addAcc[k] * invS * recvTint[k] + dz;
                        if (v <= 0.0f) continue;  // stays 0, no dither noise
                        if (v > 255.0f) v = 255.0f;
                        const uint8_t b = (uint8_t)(v + 0.5f);
                        out.light[texel * 3 + k] = b;
                        if (b) objHasBake[rg.obj] = 1, out.lit[rg.obj] = 1;
                    }
                }
            }
        }
        // dilate the interior into the 1-texel padding ring (bilinear guard)
        auto at = [&](int x, int y) -> uint8_t& {
            return out.alpha[(size_t)(rg.py + y) * atlasSize + rg.px + x];
        };
        auto lit = [&](int x, int y, int c) -> uint8_t& {
            return out.light[((size_t)(rg.py + y) * atlasSize + rg.px + x) * 3 + c];
        };
        for (int i = 0; i < rg.w; ++i) {
            const int ci = i < 1 ? 1 : (i > rg.w - 2 ? rg.w - 2 : i);
            at(i, 0) = at(ci, 1);
            at(i, rg.h - 1) = at(ci, rg.h - 2);
            for (int c = 0; c < 3; ++c) {
                lit(i, 0, c) = lit(ci, 1, c);
                lit(i, rg.h - 1, c) = lit(ci, rg.h - 2, c);
            }
        }
        for (int j = 0; j < rg.h; ++j) {
            const int cj = j < 1 ? 1 : (j > rg.h - 2 ? rg.h - 2 : j);
            at(0, j) = at(1, cj);
            at(rg.w - 1, j) = at(rg.w - 2, cj);
            for (int c = 0; c < 3; ++c) {
                lit(0, j, c) = lit(1, cj, c);
                lit(rg.w - 1, j, c) = lit(rg.w - 2, cj, c);
            }
        }
    }

    // Emit rects (interior only, inset half a texel) + per-object firsts.
    // regions[] is already in object order, region idx ascending; objects that
    // came out neither occluded nor lit keep firstRegion = -1 (their packed
    // space just goes unused).
    out.rects.reserve(regions.size());
    const float inv = 1.0f / atlasSize;
    bool anyBake = false;
    for (const Region& rg : regions) {
        if (!objHasBake[rg.obj]) continue;
        anyBake = true;
        if (out.firstRegion[rg.obj] < 0)
            out.firstRegion[rg.obj] = (int)out.rects.size();
        AtlasRect rc;
        rc.u0 = (rg.px + 1.5f) * inv;
        rc.v0 = (rg.py + 1.5f) * inv;
        rc.du = (rg.w - 3.0f) * inv;
        rc.dv = (rg.h - 3.0f) * inv;
        out.rects.push_back(rc);
    }
    if (!anyBake) {
        out.size = 0;
        out.alpha.clear();
        out.light.clear();
        out.rects.clear();
        std::fill(out.lit.begin(), out.lit.end(), (char)0);
    } else {
        // Alpha floor - see kMinLightmapAlpha. Applied AFTER the content
        // decisions above, which key on "alpha != 0" meaning "has occlusion".
        for (uint8_t& a : out.alpha)
            if (a < kMinLightmapAlpha) a = kMinLightmapAlpha;
    }
    // An object whose region was dropped keeps neither channel.
    for (size_t i = 0; i < out.lit.size(); ++i)
        if (out.firstRegion[i] < 0) out.lit[i] = 0;
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

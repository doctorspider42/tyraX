#include "navmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "objparser.hpp"

namespace navmesh {

namespace {

// Bilinear terrain height - the exact mapping of the generated
// terrainHeightAtScene (templates.cpp): origin at -size/2, step size/(hm-1),
// clamped sampling. The game walks on this surface, so the bake must too.
float terrainHeight(const SceneData& s, float x, float z) {
    const bool hasData =
        s.hmW >= 2 && s.hmD >= 2 && (int)s.heights.size() == s.hmW * s.hmD;
    if (!hasData) return 0.0f;
    const float stepX = (float)s.terrain.width / (s.hmW - 1);
    const float stepZ = (float)s.terrain.depth / (s.hmD - 1);
    float gx = (x + (float)s.terrain.width * 0.5f) / stepX;
    float gz = (z + (float)s.terrain.depth * 0.5f) / stepZ;
    gx = std::clamp(gx, 0.0f, s.hmW - 1.001f);
    gz = std::clamp(gz, 0.0f, s.hmD - 1.001f);
    const int ix = (int)gx;
    const int iz = (int)gz;
    const float fx = gx - ix;
    const float fz = gz - iz;
    const float* hm = s.heights.data();
    const float t = hm[iz * s.hmW + ix] * (1.0f - fx) + hm[iz * s.hmW + ix + 1] * fx;
    const float b =
        hm[(iz + 1) * s.hmW + ix] * (1.0f - fx) + hm[(iz + 1) * s.hmW + ix + 1] * fx;
    return t * (1.0f - fz) + b * fz;
}

// A blocking object's axis-aligned box, collidePlayer box-mode semantics:
// center = position + mid(model AABB) * scale, extent = half(model AABB) *
// scale (primitives: the unit box). Rotation is ignored - so is it in the
// game's box collision.
struct Blocker {
    float minX, maxX, minZ, maxZ;  // XZ footprint, already agent-inflated
    float bottom, top;             // Y span
};

bool blocksNavigation(const SceneObject& o) {
    switch (o.type) {
        case PrimitiveType::SpawnPoint:
        case PrimitiveType::Player:
        case PrimitiveType::Emitter:
        case PrimitiveType::SoundEmitter:
        case PrimitiveType::PointLight:
        case PrimitiveType::Empty:
        case PrimitiveType::Decal:
        case PrimitiveType::Camera:
            return false;  // markers / visual-only, collidePlayer's skip list
        default:
            break;
    }
    if (o.collisionMode == 2) return false;  // no collision at all
    if (o.collisionMode == 1) return false;  // mesh mode = walkable ramps/stairs
    return true;
}

}  // namespace

NavGrid bake(const Project& p, const SceneData& s) {
    NavGrid g;
    const float width = (float)s.terrain.width;
    const float depth = (float)s.terrain.depth;
    if (width <= 0.0f || depth <= 0.0f) return g;

    float cell = p.settings.navCellSize;
    if (cell < 0.25f) cell = 0.25f;
    g.w = std::clamp((int)std::lround(width / cell), 2, kMaxCellsPerAxis);
    g.d = std::clamp((int)std::lround(depth / cell), 2, kMaxCellsPerAxis);
    g.cellW = width / g.w;
    g.cellD = depth / g.d;
    g.originX = -width * 0.5f;
    g.originZ = -depth * 0.5f;
    g.walkable.assign((size_t)g.w * g.d, 0);

    const float maxGrad = std::tan(std::clamp(p.settings.navMaxSlope, 1.0f, 89.0f) *
                                   3.14159265f / 180.0f);
    const float inflate = std::max(0.0f, p.settings.navAgentRadius);

    // Blocking objects, model AABBs parsed once per path within this bake.
    // Animated .glb models keep the unit-box fallback (their pose AABB lives
    // in the build-time anim bake, not here) - see docs/navigation-ai.md.
    std::map<std::string, std::array<float, 6>> modelAabbs;
    std::vector<Blocker> blockers;
    for (const SceneObject& o : s.objects) {
        if (!blocksNavigation(o)) continue;
        float mn[3] = {-0.5f, -0.5f, -0.5f};
        float mx[3] = {0.5f, 0.5f, 0.5f};
        if (o.type == PrimitiveType::Model && !o.modelPath.empty() &&
            !isAnimatedModelPath(o.modelPath)) {
            auto it = modelAabbs.find(o.modelPath);
            if (it == modelAabbs.end()) {
                std::array<float, 6> box = {-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
                objparser::Model m;
                if (objparser::load(p.dir + "\\" + o.modelPath, m))
                    box = {m.min[0], m.min[1], m.min[2], m.max[0], m.max[1], m.max[2]};
                it = modelAabbs.emplace(o.modelPath, box).first;
            }
            for (int a = 0; a < 3; ++a) mn[a] = it->second[a], mx[a] = it->second[a + 3];
        }
        Blocker b;
        const float cx = o.position[0] + 0.5f * (mn[0] + mx[0]) * o.scale[0];
        const float cy = o.position[1] + 0.5f * (mn[1] + mx[1]) * o.scale[1];
        const float cz = o.position[2] + 0.5f * (mn[2] + mx[2]) * o.scale[2];
        const float ex = 0.5f * (mx[0] - mn[0]) * std::fabs(o.scale[0]);
        const float ey = 0.5f * (mx[1] - mn[1]) * std::fabs(o.scale[1]);
        const float ez = 0.5f * (mx[2] - mn[2]) * std::fabs(o.scale[2]);
        b.minX = cx - ex - inflate;
        b.maxX = cx + ex + inflate;
        b.minZ = cz - ez - inflate;
        b.maxZ = cz + ez + inflate;
        b.bottom = cy - ey;
        b.top = cy + ey;
        blockers.push_back(b);
    }

    // The game clamps every walker to size/2 - 1 from the edge.
    const float limX = width * 0.5f - 1.0f;
    const float limZ = depth * 0.5f - 1.0f;

    for (int z = 0; z < g.d; ++z) {
        for (int x = 0; x < g.w; ++x) {
            const float cx = g.originX + (x + 0.5f) * g.cellW;
            const float cz = g.originZ + (z + 0.5f) * g.cellD;
            if (cx < -limX || cx > limX || cz < -limZ || cz > limZ) continue;

            // Slope from central differences half a cell out - the same
            // surface the walkers stand on, at nav resolution.
            const float hx = (terrainHeight(s, cx + g.cellW * 0.5f, cz) -
                              terrainHeight(s, cx - g.cellW * 0.5f, cz)) /
                             g.cellW;
            const float hz = (terrainHeight(s, cx, cz + g.cellD * 0.5f) -
                              terrainHeight(s, cx, cz - g.cellD * 0.5f)) /
                             g.cellD;
            if (hx * hx + hz * hz > maxGrad * maxGrad) continue;

            const float ground = terrainHeight(s, cx, cz);
            bool blocked = false;
            for (const Blocker& b : blockers) {
                if (cx < b.minX || cx > b.maxX || cz < b.minZ || cz > b.maxZ) continue;
                // Step-onto and walk-under rules, collidePlayer semantics.
                if (b.top <= ground + kStepHeight) continue;
                if (b.bottom >= ground + kAgentHeight) continue;
                blocked = true;
                break;
            }
            if (!blocked) g.walkable[(size_t)z * g.w + x] = 1;
        }
    }
    return g;
}

}  // namespace navmesh

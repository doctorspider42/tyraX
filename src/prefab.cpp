#include "prefab.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace prefab {

namespace {
constexpr float kDeg = 3.14159265358979323846f / 180.0f;

// Half-extents of one member's box in the prefab's own frame. A primitive is
// its scale, rotated (which is exact for the axis-aligned architecture prefabs
// are usually made of and conservative otherwise); a model falls back to the
// largest axis, because its real mesh bounds are a GL-free lookup this module
// deliberately does not have. Using the largest axis for EVERYTHING - the first
// version - reported a 14-unit room as 27.5 units across, which is the kind of
// wrong number that makes a readout worse than none.
void memberExtent(const SceneObject& o, float out[3]) {
    float e[3] = {0.5f * std::fabs(o.scale[0]), 0.5f * std::fabs(o.scale[1]),
                  0.5f * std::fabs(o.scale[2])};
    if (o.type == PrimitiveType::Model) {
        const float r = std::max(std::max(e[0], e[1]), e[2]);
        out[0] = out[1] = out[2] = r;
        return;
    }
    const float cx = std::fabs(std::cos(o.rotation[0] * kDeg));
    const float sx = std::fabs(std::sin(o.rotation[0] * kDeg));
    const float cy = std::fabs(std::cos(o.rotation[1] * kDeg));
    const float sy = std::fabs(std::sin(o.rotation[1] * kDeg));
    const float cz = std::fabs(std::cos(o.rotation[2] * kDeg));
    const float sz = std::fabs(std::sin(o.rotation[2] * kDeg));
    // Conservative axis-aligned bound of the rotated box: each world axis takes
    // the sum of the projections of the three local half-extents.
    out[0] = e[0] * cy * cz + e[1] * cy * sz + e[2] * sy;
    out[1] = e[0] * (sx * sy * cz + cx * sz) + e[1] * (sx * sy * sz + cx * cz) +
             e[2] * sx * cy;
    out[2] = e[0] * (cx * sy * cz + sx * sz) + e[1] * (cx * sy * sz + sx * cz) +
             e[2] * cx * cy;
}
}  // namespace

bool memberIsMarker(const SceneObject& o) {
    switch (o.type) {
        case PrimitiveType::SpawnPoint:
        case PrimitiveType::Player:
        case PrimitiveType::Empty:
        case PrimitiveType::Camera:
        case PrimitiveType::Area:
        case PrimitiveType::Scatter:
            return true;
        default:
            return false;
    }
}

bool memberMerges(const SceneObject& o) {
    // Anything that can be addressed, animated, streamed or simulated keeps
    // its own object: the merged bag has no per-member identity at all (the
    // same deal the static batcher and the procedural bake make).
    if (!o.flowGraph.nodes.empty() || !o.scripts.empty()) return false;
    if (o.physics || o.usable || o.pickable || o.saveState) return false;
    if (!o.layer.empty()) return false;
    if (o.projShadow || o.reflected || o.dynamicLighting) return false;
    if (!o.textureFeed.empty()) return false;
    switch (o.type) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
            return true;
        case PrimitiveType::Model:
            // Animated models carry a skeleton instance - nothing to merge.
            return !isAnimatedModelPath(o.modelPath);
        default:
            return false;  // lights, emitters, decals, mirrors, portals, ...
    }
}

Prefab capture(const SceneData& s, const std::vector<int>& sel,
               const std::string& name) {
    Prefab pf;
    pf.name = name;
    pf.id = project::newObjectId();
    if (sel.empty()) return pf;

    // Origin: horizontal centre of the selection at its lowest point. A prefab
    // is placed by clicking the ground, so the origin has to sit where the
    // thing meets the ground - a centroid would bury half of it.
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int i : sel) {
        if (i < 0 || i >= (int)s.objects.size()) continue;
        const SceneObject& o = s.objects[i];
        float e[3];
        memberExtent(o, e);
        for (int a = 0; a < 3; ++a) {
            mn[a] = std::min(mn[a], o.position[a] - e[a]);
            mx[a] = std::max(mx[a], o.position[a] + e[a]);
        }
    }
    const float ox = 0.5f * (mn[0] + mx[0]);
    const float oy = mn[1];
    const float oz = 0.5f * (mn[2] + mx[2]);

    for (int i : sel) {
        if (i < 0 || i >= (int)s.objects.size()) continue;
        SceneObject o = s.objects[i];
        o.id.clear();      // an instance gets its own identity
        o.procSource.clear();  // never capture build output as authored content
        // A prefab does not record which prefab its members were taken from -
        // it IS the prefab now, and the outliner grouping would otherwise put
        // every instance of the new one under the old one's name.
        o.prefabSource.clear();
        o.position[0] -= ox;
        o.position[1] -= oy;
        o.position[2] -= oz;
        pf.objects.push_back(std::move(o));
    }
    return pf;
}

std::vector<SceneObject> instantiate(const Prefab& pf, float x, float y,
                                     float z, float yaw, float scale,
                                     const std::string& suffix) {
    std::vector<SceneObject> out;
    out.reserve(pf.objects.size());
    const float s = scale > 0.0001f ? scale : 1.0f;
    const float c = std::cos(yaw * kDeg), sn = std::sin(yaw * kDeg);
    for (const SceneObject& m : pf.objects) {
        SceneObject o = m;
        o.id.clear();
        // Provenance for the outliner (SceneObject::prefabSource) - the members
        // are otherwise indistinguishable from hand-placed ones, which is the
        // whole point of a prefab and also what makes a scene built from them
        // unreadable in a flat list.
        o.prefabSource = pf.name;
        if (!suffix.empty()) o.name += suffix;
        const float lx = m.position[0] * s, ly = m.position[1] * s,
                    lz = m.position[2] * s;
        // Yaw about the prefab's own Y axis: the same convention the runtime
        // spawner uses, so an instance placed in the editor and one spawned by
        // a flow node land in the same place.
        o.position[0] = x + lx * c + lz * sn;
        o.position[1] = y + ly;
        o.position[2] = z - lx * sn + lz * c;
        o.rotation[1] += yaw;
        for (int a = 0; a < 3; ++a) o.scale[a] *= s;
        out.push_back(std::move(o));
    }
    return out;
}

void bounds(const Prefab& pf, float outMin[3], float outMax[3]) {
    for (int a = 0; a < 3; ++a) {
        outMin[a] = 0.0f;
        outMax[a] = 0.0f;
    }
    if (pf.objects.empty()) return;
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const SceneObject& o : pf.objects) {
        float e[3];
        memberExtent(o, e);
        for (int a = 0; a < 3; ++a) {
            mn[a] = std::min(mn[a], o.position[a] - e[a]);
            mx[a] = std::max(mx[a], o.position[a] + e[a]);
        }
    }
    for (int a = 0; a < 3; ++a) {
        outMin[a] = mn[a];
        outMax[a] = mx[a];
    }
}

const Prefab* find(const Project& p, const std::string& name) {
    if (name.empty()) return nullptr;
    for (const Prefab& pf : p.prefabs)
        if (pf.name == name) return &pf;
    return nullptr;
}

Prefab* find(Project& p, const std::string& name) {
    return const_cast<Prefab*>(find((const Project&)p, name));
}

std::string uniqueName(const Project& p, const std::string& wanted) {
    std::string base = wanted.empty() ? std::string("prefab") : wanted;
    if (!find(p, base)) return base;
    for (int n = 2; n < 10000; ++n) {
        const std::string cand = base + " " + std::to_string(n);
        if (!find(p, cand)) return cand;
    }
    return base;
}

bool rename(Project& p, const std::string& from, const std::string& to) {
    if (to.empty() || from == to) return false;
    if (find(p, to)) return false;
    Prefab* pf = find(p, from);
    if (!pf) return false;
    pf->name = to;
    for (SceneData& s : p.scenes)
        for (SceneObject& o : s.objects) {
            // Provenance is a reference too: an already-placed instance would
            // otherwise keep grouping under a prefab name that no longer exists.
            if (o.prefabSource == from) o.prefabSource = to;
            for (FlowNode& n : o.flowGraph.nodes) {
                const FlowNodeType* t = flowNodeType(n.type);
                if (t && t->strKind == FlowParamKind::PrefabName && n.str == from)
                    n.str = to;
            }
            for (ProcNode& n : o.procGraph.nodes) {
                if (n.type != "PickPrefab") continue;
                for (ProcRow& r : n.rows)
                    if (r.s == from) r.s = to;
            }
        }
    return true;
}

std::vector<std::string> referencedBy(const Project& p, const SceneData& s) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    auto add = [&](const std::string& n) {
        if (n.empty() || !find(p, n)) return;
        if (seen.insert(n).second) out.push_back(n);
    };
    for (const SceneObject& o : s.objects) {
        for (const FlowNode& n : o.flowGraph.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (t && t->strKind == FlowParamKind::PrefabName) add(n.str);
        }
        for (const ProcNode& n : o.procGraph.nodes) {
            if (n.type != "PickPrefab") continue;
            for (const ProcRow& r : n.rows) add(r.s);
        }
    }
    return out;
}

}  // namespace prefab

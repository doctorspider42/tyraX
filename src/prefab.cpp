#include "prefab.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "objparser.hpp"
#include "primmesh.hpp"

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

namespace {

namespace fs = std::filesystem;

// Rz*Ry*Rx - the order Viewport::modelMatrix and procbake both use. A member
// baked with any other order lands somewhere the editor never showed it.
void rotateVec(const float in[3], const float rotDeg[3], float out[3]) {
    const float rx = rotDeg[0] * kDeg, ry = rotDeg[1] * kDeg, rz = rotDeg[2] * kDeg;
    float x = in[0], y = in[1], z = in[2];
    float t = y * std::cos(rx) - z * std::sin(rx);
    z = y * std::sin(rx) + z * std::cos(rx);
    y = t;
    t = x * std::cos(ry) + z * std::sin(ry);
    z = -x * std::sin(ry) + z * std::cos(ry);
    x = t;
    t = x * std::cos(rz) - y * std::sin(rz);
    y = x * std::sin(rz) + y * std::cos(rz);
    x = t;
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// A file-name-safe stem, the rule procbake's stemOf follows.
std::string safeStem(const std::string& in) {
    std::string s;
    for (char c : in)
        s += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return s.empty() ? std::string("prefab") : s;
}

// Did a bake write this file? The stem comes from the prefab's NAME, and
// "box" is exactly what a hand-made model is also called - so both the bake
// (before overwriting) and the take-back (before deleting) ask this first.
bool isBakeOutput(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    std::getline(f, line);
    return line.rfind("# Generated by TyraX from the prefab", 0) == 0;
}

// One material of the baked library: a colour, and optionally a texture that
// already sits next to the output.
struct BakedMat {
    std::string name;
    float kd[3] = {1, 1, 1};
    std::string texture;  // file name, relative to the output directory
};

// The unit mesh of a primitive: pos(3) + normal(3) + uv(2) per vertex - the
// same 8-float layout objparser uses, so one writer serves both.
std::vector<float> unitMeshFor(const SceneObject& o) {
    const int d = clampPrimDetail(o.type, o.primDetail);
    switch (o.type) {
        case PrimitiveType::Sphere: return primmesh::unitSphere(d);
        case PrimitiveType::Cylinder: return primmesh::unitCylinder(d, o.primRings);
        case PrimitiveType::Cone: return primmesh::unitCone(d);
        case PrimitiveType::Plane: return primmesh::unitPlane();
        default: return primmesh::unitBox(d);
    }
}

}  // namespace

BakeReport bakeToModel(const Project& p, const Prefab& pf) {
    BakeReport rep;
    if (pf.objects.empty()) {
        rep.error = "the prefab is empty";
        return rep;
    }

    const std::string stem = safeStem(pf.name);
    const fs::path outDir = fs::path(p.dir) / "res" / "models";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // Re-baking over a PREVIOUS bake is the normal update path; anything the
    // bake did not write is refused (see isBakeOutput).
    for (const char* ext : {".obj", ".mtl"}) {
        const fs::path out = outDir / (stem + ext);
        if (fs::exists(out, ec) && !isBakeOutput(out)) {
            rep.error = "res/models/" + stem + ext +
                        " already exists and is not a bake output - rename the "
                        "prefab so the bake does not overwrite it";
            return rep;
        }
    }

    // Triangles grouped by material, so the .obj carries one usemtl run per
    // distinct look rather than one per member - that grouping is the whole
    // point, since a submit costs the same whatever it contains.
    std::vector<BakedMat> mats;
    std::vector<std::vector<float>> tris;  // parallel to mats

    auto materialSlot = [&](const SceneObject& o) -> int {
        BakedMat m;
        for (int a = 0; a < 3; ++a) m.kd[a] = o.color[a];
        if (!o.materialPath.empty()) {
            // The convention the viewport and the game share: a primitive takes
            // the assigned library's FIRST entry as its Kd tint and map_Kd.
            std::vector<objparser::MtlMaterial> lib;
            const fs::path mtlAbs =
                (fs::path(p.dir) / fs::path(o.materialPath)).lexically_normal();
            if (objparser::loadMtl(mtlAbs.string(), lib) && !lib.empty()) {
                for (int a = 0; a < 3; ++a) m.kd[a] *= lib[0].kd[a];
                if (!lib[0].texture.empty()) {
                    // The PS2 cannot walk "..", so a texture has to sit beside
                    // the .mtl naming it - copy it in rather than write a path
                    // that only resolves on a PC.
                    const fs::path src =
                        (mtlAbs.parent_path() / lib[0].texture).lexically_normal();
                    const std::string file = src.filename().string();
                    std::error_code cec;
                    if (fs::exists(src, cec)) {
                        const fs::path dst = outDir / file;
                        if (fs::weakly_canonical(src, cec) !=
                            fs::weakly_canonical(dst, cec))
                            fs::copy_file(src, dst,
                                          fs::copy_options::overwrite_existing, cec);
                        m.texture = file;
                    } else {
                        rep.warnings.push_back("missing texture " + file);
                    }
                }
            }
        }
        for (size_t i = 0; i < mats.size(); ++i) {
            const BakedMat& e = mats[i];
            if (e.texture == m.texture && e.kd[0] == m.kd[0] &&
                e.kd[1] == m.kd[1] && e.kd[2] == m.kd[2])
                return (int)i;
        }
        char nm[80];
        std::snprintf(nm, sizeof(nm), "%s-m%d", stem.c_str(), (int)mats.size());
        m.name = nm;
        mats.push_back(m);
        tris.emplace_back();
        return (int)mats.size() - 1;
    };

    // Appends one member's triangles, transformed into the prefab's own frame.
    auto addMesh = [&](const SceneObject& o, const std::vector<float>& verts,
                       int slot) {
        std::vector<float>& dst = tris[(size_t)slot];
        const size_t n = verts.size() / 8;
        for (size_t v = 0; v < n; ++v) {
            const float* s = &verts[v * 8];
            const float sc[3] = {s[0] * o.scale[0], s[1] * o.scale[1],
                                 s[2] * o.scale[2]};
            float rt[3];
            rotateVec(sc, o.rotation, rt);
            // Normals take the same rotation with the scale INVERTED first: a
            // squashed box whose normals were merely rotated shades as though
            // it had never been squashed.
            const float ns[3] = {o.scale[0] != 0.0f ? s[3] / o.scale[0] : s[3],
                                 o.scale[1] != 0.0f ? s[4] / o.scale[1] : s[4],
                                 o.scale[2] != 0.0f ? s[5] / o.scale[2] : s[5]};
            float nr[3];
            rotateVec(ns, o.rotation, nr);
            const float len =
                std::sqrt(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
            const float inv = len > 1e-6f ? 1.0f / len : 1.0f;
            dst.push_back(rt[0] + o.position[0]);
            dst.push_back(rt[1] + o.position[1]);
            dst.push_back(rt[2] + o.position[2]);
            dst.push_back(nr[0] * inv);
            dst.push_back(nr[1] * inv);
            dst.push_back(nr[2] * inv);
            dst.push_back(s[6]);
            dst.push_back(s[7]);
        }
    };

    for (const SceneObject& o : pf.objects) {
        if (!memberMerges(o)) {
            const char* why =
                memberIsMarker(o)              ? "a marker, no geometry"
                : !o.flowGraph.nodes.empty()   ? "has a flow graph"
                : !o.scripts.empty()           ? "has a script"
                : o.physics                    ? "is a physics body"
                : (o.type == PrimitiveType::Model &&
                   isAnimatedModelPath(o.modelPath))
                                               ? "is an animated model"
                                               : "keeps a runtime identity";
            rep.skipped.push_back(o.name + " (" + why + ")");
            continue;
        }
        if (o.type == PrimitiveType::Model) {
            objparser::Model src;
            const fs::path abs =
                (fs::path(p.dir) / fs::path(o.modelPath)).lexically_normal();
            if (!objparser::load(abs.string(), src, o.materialPath.empty()
                                     ? std::string()
                                     : (fs::path(p.dir) /
                                        fs::path(o.materialPath))
                                           .lexically_normal()
                                           .string())) {
                rep.skipped.push_back(o.name + " (could not read " + o.modelPath +
                                      ")");
                continue;
            }
            for (const objparser::Submesh& sm : src.submeshes) {
                if (sm.verts.empty()) continue;
                // The member's own colour tints its material, exactly as the
                // viewport and the runtime multiply the two.
                SceneObject tinted = o;
                tinted.materialPath.clear();
                for (int a = 0; a < 3; ++a) tinted.color[a] = o.color[a] * sm.kd[a];
                const int slot = materialSlot(tinted);
                if (!sm.texture.empty())
                    rep.warnings.push_back(o.name +
                                           ": a textured model part bakes as flat "
                                           "colour");
                addMesh(o, sm.verts, slot);
            }
            ++rep.members;
            continue;
        }
        addMesh(o, unitMeshFor(o), materialSlot(o));
        ++rep.members;
    }

    size_t total = 0;
    for (const std::vector<float>& t : tris) total += t.size() / 24;
    if (total == 0) {
        rep.error =
            "nothing mergeable here - every member keeps a runtime identity, so "
            "there is no geometry to bake";
        return rep;
    }

    std::ostringstream obj, mtl;
    obj << "# Generated by TyraX from the prefab \"" << pf.name
        << "\". Re-bake to update; edits here are lost.\n";
    obj << "mtllib " << stem << ".mtl\n";
    mtl << "# Generated by TyraX from the prefab \"" << pf.name << "\".\n";

    char buf[192];
    int base = 1;  // .obj indices are 1-based and file-global
    for (size_t mi = 0; mi < mats.size(); ++mi) {
        const std::vector<float>& t = tris[mi];
        if (t.empty()) continue;
        const BakedMat& m = mats[mi];
        mtl << "newmtl " << m.name << "\n";
        std::snprintf(buf, sizeof(buf), "Kd %.4f %.4f %.4f\n", m.kd[0], m.kd[1],
                      m.kd[2]);
        mtl << buf;
        if (!m.texture.empty()) mtl << "map_Kd " << m.texture << "\n";
        ++rep.materials;

        std::ostringstream vs, ns, ts, faces;
        const size_t verts = t.size() / 8;
        for (size_t v = 0; v < verts; ++v) {
            const float* q = &t[v * 8];
            std::snprintf(buf, sizeof(buf), "v %.4f %.4f %.4f\n", q[0], q[1], q[2]);
            vs << buf;
            std::snprintf(buf, sizeof(buf), "vn %.4f %.4f %.4f\n", q[3], q[4], q[5]);
            ns << buf;
            // objparser flips V into image space on read, so write the
            // file-space value back or a re-read would mirror the texture.
            std::snprintf(buf, sizeof(buf), "vt %.5f %.5f\n", q[6], 1.0f - q[7]);
            ts << buf;
        }
        obj << vs.str() << ns.str() << ts.str();
        obj << "usemtl " << m.name << "\n";
        for (size_t f = 0; f < verts / 3; ++f) {
            const int a = base + (int)(f * 3);
            std::snprintf(buf, sizeof(buf), "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a,
                          a, a + 1, a + 1, a + 1, a + 2, a + 2, a + 2);
            faces << buf;
        }
        obj << faces.str();
        base += (int)verts;
        rep.triangles += (int)(verts / 3);
    }

    auto write = [&](const fs::path& path, const std::string& text) {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f.write(text.data(), (std::streamsize)text.size());
        return (bool)f;
    };
    if (!write(outDir / (stem + ".obj"), obj.str()) ||
        !write(outDir / (stem + ".mtl"), mtl.str())) {
        rep.error = "could not write res/models/" + stem + ".obj";
        return rep;
    }
    rep.modelPath = "res/models/" + stem + ".obj";
    rep.mtlPath = "res/models/" + stem + ".mtl";
    return rep;
}

std::string bakeOnDisk(const Project& p, const Prefab& pf) {
    const std::string stem = safeStem(pf.name);
    const fs::path obj = fs::path(p.dir) / "res" / "models" / (stem + ".obj");
    std::error_code ec;
    if (!fs::exists(obj, ec) || !isBakeOutput(obj)) return std::string();
    return "res/models/" + stem + ".obj";
}

std::string deleteBake(const Project& p, const Prefab& pf) {
    const std::string stem = safeStem(pf.name);
    const fs::path outDir = fs::path(p.dir) / "res" / "models";
    std::error_code ec;
    const fs::path obj = outDir / (stem + ".obj");
    if (!fs::exists(obj, ec)) return "res/models/" + stem + ".obj is not there";
    if (!isBakeOutput(obj))
        return "res/models/" + stem + ".obj was not written by a bake";
    fs::remove(obj, ec);
    if (ec) return "could not delete res/models/" + stem + ".obj";
    // The .mtl only if the bake wrote it; the .tmdl unconditionally - it is
    // derived from the .obj at build time and would only dangle.
    const fs::path mtl = outDir / (stem + ".mtl");
    if (fs::exists(mtl, ec) && isBakeOutput(mtl)) fs::remove(mtl, ec);
    fs::remove(outDir / (stem + ".tmdl"), ec);
    return std::string();
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

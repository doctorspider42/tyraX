#include "procbake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "meshlod.hpp"
#include "prefab.hpp"

namespace fs = std::filesystem;

namespace procbake {

namespace {

constexpr float kDeg = 3.14159265358979f / 180.0f;
// Rough PS2 cost of one merged vertex: a Vec4 position + a Vec4 texture
// coordinate + a colour, plus the copy the pipeline keeps. Deliberately
// pessimistic - the budget readout should scare the author a little early
// rather than a little late.
constexpr size_t kBytesPerVertex = 40;

std::string stemOf(const std::string& relPath) {
    std::string s = relPath;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    for (char& c : s)
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    return s.empty() ? std::string("asset") : s;
}

std::string dirOf(const std::string& relPath) {
    const size_t slash = relPath.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : relPath.substr(0, slash);
}

// -3 -> "m3": chunk coordinates go into file names, and a minus sign there
// reads as a separator.
std::string cellTag(int cx, int cz) {
    auto one = [](int v) {
        return v < 0 ? "m" + std::to_string(-v) : std::to_string(v);
    };
    return "x" + one(cx) + "z" + one(cz);
}

std::string shortId(const std::string& id) {
    return id.size() > 8 ? id.substr(0, 8) : id;
}

// The graph's Object Settings rows -> fields on one generated chunk object.
// The twin of procObjectProps(): a property offered there and not handled here
// is a switch that does nothing, so keep the two in one edit.
void applySettings(const ProcGraph& g, SceneObject& o) {
    const ProcNode* sn = procgraph::settingsNode(g);
    if (!sn) return;
    for (const ProcRow& r : sn->rows) {
        const ProcObjProp* prop = procObjectProp(r.s);
        if (!prop) continue;  // an unknown key is reported by procgraph::validate
        const float v = std::clamp(r.v[0], prop->lo, prop->hi);
        if (r.s == "meshLod") o.meshLodOverride = v;
        else if (r.s == "bakedLighting") o.bakedLighting = v >= 0.5f;
        else if (r.s == "reflected") o.reflected = v >= 0.5f;
    }
}

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

struct Chunk {
    int cx = 0, cz = 0;
    int asset = 0;
    std::vector<const procgen::Instance*> insts;
};

struct ChunkFile {
    std::string objectName;
    std::string modelPath;  // project-relative, forward slashes
    float center[3] = {0, 0, 0};
    int triangles = 0;
};

// The source mesh a bake merges: the asset as authored (detail 0) or a
// decimated copy of it (1 = half the welded vertices, 2 = a quarter). Cached
// per (asset, detail) because both the budget readout and the bake ask for it,
// and decimation is the expensive part.
//
// The welding trap that cost the .tmdl bake a day: a static mesh's normals
// are DERIVED (per face, crease-smoothed since), so keying the weld on them
// makes crease corners seam twins, the border lock fires and little
// decimates. Weld by position+uv, then recompute the face normals - and stop
// there: the chunk .obj writer below drops normals anyway, and the re-parse
// at .tmdl bake time re-derives (and re-smooths) them.
std::shared_ptr<const procgen::AssetMesh> sourceMesh(const Project& p,
                                                     const std::string& rel,
                                                     int detail) {
    auto full = procgen::assetMesh(p, rel);
    if (!full || detail <= 0) return full;
    static std::mutex mu;
    static std::map<std::string, std::shared_ptr<const procgen::AssetMesh>> cache;
    const std::string key = p.dir + "|" + rel + "|" + std::to_string(detail);
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    const float ratio = detail == 1 ? 0.5f : 0.25f;
    auto out = std::make_shared<procgen::AssetMesh>(*full);
    for (procgen::AssetMesh::Part& part : out->parts) {
        if (part.verts.size() / 8 < 24) continue;  // too small to gain anything
        meshlod::Mesh m = meshlod::weldInterleaved(part.verts.data(),
                                                   part.verts.size() / 8, false);
        meshlod::decimate(m, (size_t)((float)m.vertexCount() * ratio));
        std::vector<float> tris = meshlod::unweldInterleaved(m);
        if (tris.size() >= 24 && tris.size() < part.verts.size()) {
            meshlod::recomputeFaceNormals(tris);
            part.verts = std::move(tris);
        }
    }
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = out;
    return out;
}

const SceneObject* findById(const SceneData& s, const std::string& id) {
    for (const SceneObject& o : s.objects)
        if (o.id == id) return &o;
    return nullptr;
}

}  // namespace

Report estimate(const Project& p, const SceneObject& volume,
                const procgen::Result& r) {
    Report rep;
    rep.volumes = 1;
    const ProcNode* out = procgraph::outputNode(volume.procGraph);
    const float cell = out ? std::max(8.0f, procgraph::num(*out, "cell")) : 48.0f;
    const int budget = out ? std::max(1, procgraph::inum(*out, "budget")) : 20000;
    const int detail = out ? procgraph::inum(*out, "detail") : 0;

    std::map<uint64_t, int> chunkIds;  // (asset, cell) -> triangles, for the count
    int noContent = 0;
    for (const procgen::Instance& inst : r.instances) {
        const int cx = (int)std::floor(inst.pos[0] / cell);
        const int cz = (int)std::floor(inst.pos[2] / cell);
        // A point carries an asset OR a prefab. Counting only the asset half
        // made a prefab-scattering graph report zero triangles and "fits the
        // budget" for a world of 27 rooms - a budget readout that is silent
        // about the thing being placed is worse than none.
        if (inst.prefab >= 0 && inst.prefab < (int)r.prefabs.size()) {
            const Prefab* pf = prefab::find(p, r.prefabs[inst.prefab]);
            if (!pf || pf->objects.empty()) {
                ++noContent;
                continue;
            }
            int tris = 0;
            for (const SceneObject& m : pf->objects) {
                // Only merged members land in the chunk geometry; the rest are
                // spawned objects with their own submits, counted by the
                // Prefabs window rather than by a triangle budget.
                if (!prefab::memberMerges(m)) continue;
                if (m.type == PrimitiveType::Model) {
                    if (auto mesh = sourceMesh(p, m.modelPath, detail))
                        tris += mesh->triangles();
                } else {
                    tris += primTriangleCount(m.type, m.primDetail);
                }
            }
            const uint64_t key = 0x8000000000000000ULL ^
                                 ((uint64_t)(uint32_t)inst.prefab << 40) ^
                                 ((uint64_t)(uint32_t)cx << 20) ^ (uint32_t)cz;
            chunkIds[key] += tris;
            rep.triangles += tris;
            ++rep.instances;
            continue;
        }
        if (inst.asset < 0 || inst.asset >= (int)r.assets.size()) {
            ++noContent;
            continue;
        }
        auto mesh = sourceMesh(p, r.assets[inst.asset], detail);
        if (!mesh) {
            ++noContent;
            continue;
        }
        const uint64_t key = ((uint64_t)(uint32_t)inst.asset << 40) ^
                             ((uint64_t)(uint32_t)cx << 20) ^ (uint32_t)cz;
        chunkIds[key] += mesh->triangles();
        rep.triangles += mesh->triangles();
        ++rep.instances;
    }
    rep.chunks = (int)chunkIds.size();
    rep.vertexBytes = (size_t)rep.triangles * 3 * kBytesPerVertex;
    rep.overBudget = rep.triangles > budget;
    if (noContent > 0)
        rep.warnings.push_back(
            std::to_string(noContent) +
            " instance(s) have nothing to place - add a Pick Asset or Pick "
            "Prefab node, or check that the model file still exists");
    return rep;
}

Report bakeVolume(Project& p, SceneData& s, const std::string& volumeId,
                  procgen::Cache* cache) {
    Report rep;
    const SceneObject* volPtr = findById(s, volumeId);
    if (!volPtr || volPtr->type != PrimitiveType::Scatter) {
        rep.error = "no scatter volume with that id";
        return rep;
    }
    // A copy: writing chunk objects into s.objects reallocates the vector.
    const SceneObject vol = *volPtr;
    rep.volumes = 1;

    procgen::Options opt;
    opt.fraction = 1.0f;
    opt.contextSerial = procgen::bakeHash(p, s, vol);
    const procgen::Result r = procgen::evaluate(p, s, vol, opt, cache);
    for (const std::string& w : r.warnings) rep.warnings.push_back(w);

    const ProcNode* outNode = procgraph::outputNode(vol.procGraph);
    const float cell = outNode ? std::max(8.0f, procgraph::num(*outNode, "cell")) : 48.0f;
    const float drawDist = outNode ? procgraph::num(*outNode, "draw") : 0.0f;
    const bool shadow = outNode ? procgraph::flag(*outNode, "shadow") : false;
    const int collide = outNode ? procgraph::inum(*outNode, "collide") : 0;
    const int budget = outNode ? std::max(1, procgraph::inum(*outNode, "budget")) : 20000;
    const int detail = outNode ? procgraph::inum(*outNode, "detail") : 0;

    // Group by (asset, chunk). One file per group keeps the material library
    // of the source asset valid verbatim - the merged .obj sits in the source's
    // own directory and reuses its mtllib line, so textures, texture quality
    // overrides and atlasing all resolve exactly as they do for the source.
    std::map<uint64_t, Chunk> groups;
    for (const procgen::Instance& inst : r.instances) {
        if (inst.asset < 0 || inst.asset >= (int)r.assets.size()) continue;
        const int cx = (int)std::floor(inst.pos[0] / cell);
        const int cz = (int)std::floor(inst.pos[2] / cell);
        const uint64_t key = ((uint64_t)(uint32_t)inst.asset << 40) ^
                             ((uint64_t)(uint32_t)(cx + 0x80000) << 20) ^
                             (uint32_t)(cz + 0x80000);
        Chunk& c = groups[key];
        c.cx = cx;
        c.cz = cz;
        c.asset = inst.asset;
        c.insts.push_back(&inst);
    }

    std::vector<ChunkFile> written;
    std::vector<std::string> writtenFiles;  // relative paths, for the sweep
    std::error_code ec;

    for (const auto& kv : groups) {
        const Chunk& c = kv.second;
        const std::string& assetRel = r.assets[c.asset];
        auto mesh = sourceMesh(p, assetRel, detail);
        if (!mesh || mesh->parts.empty()) continue;

        const std::string dir = dirOf(assetRel);
        const std::string file = "procgen-" + shortId(vol.id) + "-" +
                                stemOf(assetRel) + "-" + cellTag(c.cx, c.cz) + ".obj";
        const std::string rel = dir.empty() ? file : dir + "/" + file;

        // Chunk-local geometry around the chunk centre: the object's POSITION
        // is that centre, which is what per-object draw distance and the
        // engine's bbox culling measure against. World-space vertices on an
        // object at the origin would cull and fade by the wrong distance.
        const float centre[3] = {((float)c.cx + 0.5f) * cell, 0.0f,
                                 ((float)c.cz + 0.5f) * cell};

        std::ostringstream obj;
        obj << "# Generated by TyraX. Do not edit - rebaked from the procedural "
               "graph of '"
            << vol.name << "'.\n";
        obj << "# " << c.insts.size() << " instances of " << assetRel << "\n";
        for (const std::string& lib : mesh->mtlLibs) obj << "mtllib " << lib << "\n";
        int vertBase = 1;  // .obj indices are 1-based and file-global
        int tris = 0;
        char buf[160];
        for (const procgen::AssetMesh::Part& part : mesh->parts) {
            const size_t partTris = part.verts.size() / 24;
            if (!partTris) continue;
            std::ostringstream vs, ts, fs_;
            for (const procgen::Instance* inst : c.insts) {
                for (size_t t = 0; t < partTris; ++t) {
                    for (int corner = 0; corner < 3; ++corner) {
                        const float* v = &part.verts[(t * 3 + corner) * 8];
                        float sc[3] = {v[0] * inst->scale, v[1] * inst->scale,
                                       v[2] * inst->scale};
                        float rt[3];
                        rotateVec(sc, inst->rot, rt);
                        std::snprintf(buf, sizeof(buf), "v %.4f %.4f %.4f\n",
                                      rt[0] + inst->pos[0] - centre[0],
                                      rt[1] + inst->pos[1] - centre[1],
                                      rt[2] + inst->pos[2] - centre[2]);
                        vs << buf;
                        // objparser flips V into image space on read, so write
                        // the file-space value back or every re-read would
                        // mirror the texture.
                        std::snprintf(buf, sizeof(buf), "vt %.5f %.5f\n", v[6],
                                      1.0f - v[7]);
                        ts << buf;
                    }
                    const int a = vertBase + (int)(tris * 3);
                    std::snprintf(buf, sizeof(buf), "f %d/%d %d/%d %d/%d\n", a, a,
                                  a + 1, a + 1, a + 2, a + 2);
                    fs_ << buf;
                    ++tris;
                }
            }
            obj << vs.str() << ts.str();
            if (!part.material.empty()) obj << "usemtl " << part.material << "\n";
            obj << fs_.str();
        }
        if (tris == 0) continue;

        const fs::path outPath = fs::path(p.dir) / fs::path(rel).make_preferred();
        fs::create_directories(outPath.parent_path(), ec);
        std::ofstream f(outPath, std::ios::binary);
        if (!f) {
            rep.warnings.push_back("could not write " + rel);
            continue;
        }
        const std::string text = obj.str();
        f.write(text.data(), (std::streamsize)text.size());
        f.close();

        ChunkFile cf;
        cf.objectName = vol.name + "#" + stemOf(assetRel) + "-" + cellTag(c.cx, c.cz);
        cf.modelPath = rel;
        cf.center[0] = centre[0];
        cf.center[1] = centre[1];
        cf.center[2] = centre[2];
        cf.triangles = tris;
        written.push_back(cf);
        writtenFiles.push_back(rel);
        rep.triangles += tris;
        rep.instances += (int)c.insts.size();
    }
    rep.chunks = (int)written.size();
    rep.vertexBytes = (size_t)rep.triangles * 3 * kBytesPerVertex;
    rep.overBudget = rep.triangles > budget;
    if (rep.overBudget)
        rep.warnings.push_back(
            "'" + vol.name + "' bakes " + std::to_string(rep.triangles) +
            " triangles, over its budget of " + std::to_string(budget) +
            " - lower the density, use a simpler asset, or raise the budget "
            "deliberately");

    // Reconcile the generated objects. Matching by NAME (which encodes asset +
    // chunk) keeps each chunk's object id stable across bakes, so live link,
    // collaboration and the undo history see an edit, not a delete + insert.
    std::map<std::string, SceneObject> existing;
    std::vector<std::string> oldFiles;
    for (const SceneObject& o : s.objects) {
        if (o.procSource != vol.id) continue;
        existing[o.name] = o;
        if (!o.modelPath.empty()) oldFiles.push_back(o.modelPath);
    }
    s.objects.erase(std::remove_if(s.objects.begin(), s.objects.end(),
                                   [&](const SceneObject& o) {
                                       return o.procSource == vol.id;
                                   }),
                    s.objects.end());
    for (const ChunkFile& cf : written) {
        SceneObject o;
        auto it = existing.find(cf.objectName);
        if (it != existing.end()) o = it->second;  // keep its id
        // A fresh chunk needs its own identity NOW: the merge-friendly file
        // layout stores one object per objects/<id>.json, so an object saved
        // with an empty id is written to nowhere and lost on the next load.
        if (o.id.empty()) o.id = project::newObjectId();
        o.name = cf.objectName;
        o.type = PrimitiveType::Model;
        o.modelPath = cf.modelPath;
        o.procSource = vol.id;
        for (int a = 0; a < 3; ++a) {
            o.position[a] = cf.center[a];
            o.rotation[a] = 0.0f;
            o.scale[a] = 1.0f;
        }
        o.castShadow = shadow;
        o.collisionMode = collide == 1 ? 0 : 2;  // Box | None
        o.drawDistance = drawDist;
        o.layer = vol.layer;  // a streamed volume streams its output
        // ...and whatever the graph's Object Settings node says about every
        // object it generates. Applied AFTER the fixed fields, because that
        // node is the explicit statement and these are the defaults.
        applySettings(vol.procGraph, o);
        s.objects.push_back(o);
    }

    // Sweep superseded files: anything this volume wrote in an earlier bake
    // that is not part of the current one. The name prefix is the ownership
    // marker, so a hand-made model in the same folder is never touched.
    const std::string prefix = "procgen-" + shortId(vol.id) + "-";
    for (const std::string& old : oldFiles) {
        if (std::find(writtenFiles.begin(), writtenFiles.end(), old) !=
            writtenFiles.end())
            continue;
        const std::string base = old.substr(old.find_last_of("/\\") + 1);
        if (base.rfind(prefix, 0) != 0) continue;
        fs::remove(fs::path(p.dir) / fs::path(old).make_preferred(), ec);
    }

    // Stamp the hash on the live object (the copy above is stale by now).
    for (SceneObject& o : s.objects)
        if (o.id == vol.id) o.procGraph.bakedHash = opt.contextSerial;
    return rep;
}

Report bakeAll(Project& p, bool force) {
    Report total;
    procgen::Cache cache;
    for (SceneData& s : p.scenes) {
        // Collect the ids first: baking mutates s.objects.
        std::vector<std::string> ids;
        for (const SceneObject& o : s.objects)
            // A RUNTIME volume has nothing to bake: its graph is compiled into
            // the game and evaluated on the console (docs/procedural-runtime.md).
            if (o.type == PrimitiveType::Scatter && !o.procGraph.empty() &&
                !o.procGraph.runtime)
                ids.push_back(o.id);
        for (const std::string& id : ids) {
            const SceneObject* o = findById(s, id);
            if (!o) continue;
            if (!force && o->procGraph.bakedHash == procgen::bakeHash(p, s, *o))
                continue;
            const Report r = bakeVolume(p, s, id, &cache);
            total.volumes += r.volumes;
            total.chunks += r.chunks;
            total.instances += r.instances;
            total.triangles += r.triangles;
            total.vertexBytes += r.vertexBytes;
            total.overBudget |= r.overBudget;
            for (const std::string& w : r.warnings) total.warnings.push_back(w);
            if (!r.error.empty()) total.error = r.error;
        }
    }
    return total;
}

bool anyStale(const Project& p) {
    for (const SceneData& s : p.scenes)
        for (const SceneObject& o : s.objects)
            if (o.type == PrimitiveType::Scatter && !o.procGraph.empty() &&
                !o.procGraph.runtime &&
                o.procGraph.bakedHash != procgen::bakeHash(p, s, o))
                return true;
    return false;
}

void clearVolume(Project& p, SceneData& s, const std::string& volumeId) {
    std::error_code ec;
    for (const SceneObject& o : s.objects) {
        if (o.procSource != volumeId || o.modelPath.empty()) continue;
        const std::string base = o.modelPath.substr(o.modelPath.find_last_of("/\\") + 1);
        if (base.rfind("procgen-" + shortId(volumeId) + "-", 0) != 0) continue;
        fs::remove(fs::path(p.dir) / fs::path(o.modelPath).make_preferred(), ec);
    }
    s.objects.erase(std::remove_if(s.objects.begin(), s.objects.end(),
                                   [&](const SceneObject& o) {
                                       return o.procSource == volumeId;
                                   }),
                    s.objects.end());
    for (SceneObject& o : s.objects)
        if (o.id == volumeId) o.procGraph.bakedHash = 0;
}

}  // namespace procbake

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "objparser.hpp"
#include "primmesh.hpp"
#include "staticbatch.hpp"
#include "templates.hpp"
#include "tmdl.hpp"

// The disk-backed half of staticbatch, kept OUT of staticbatch.cpp so the
// twin itself links with nothing but project.hpp. verify-batch-twins.py
// compiles that file alone and injects its own fixtures; if the two lived
// together, the oracle would have to drag in templates.cpp - the very thing
// the twin exists to stay independent of.
namespace staticbatch {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Vertices a primitive tessellates to, from the SAME primmesh call the
// generated game's builders use - so the package figure beside a box is that
// box's real cost and not a guess. 8 floats per vertex.
unsigned int primVertexCount(const SceneObject& o) {
    std::vector<float> v;
    switch (o.type) {
        case PrimitiveType::Box: v = primmesh::unitBox(o.primDetail); break;
        case PrimitiveType::Sphere: v = primmesh::unitSphere(o.primDetail); break;
        case PrimitiveType::Cylinder:
            v = primmesh::unitCylinder(o.primDetail, o.primRings);
            break;
        case PrimitiveType::Cone: v = primmesh::unitCone(o.primDetail); break;
        case PrimitiveType::Plane: v = primmesh::unitPlane(); break;
        default: return 0u;
    }
    return (unsigned int)(v.size() / 8);
}

}  // namespace

Inputs diskInputs(const Project& p, std::vector<std::string>* warnings) {
    // Both lookups are memoised per call: a scene asks about the same model
    // and the same .mtl once per object, and a .tmdl is megabytes.
    auto models = std::make_shared<std::map<std::string, ModelInfo>>();
    auto mats = std::make_shared<std::map<std::string, MaterialInfo>>();
    // The material override a model carries changes which .tmdl it bakes to,
    // and an object states it. Resolved per object by the caller through the
    // model path alone, so the map is keyed by the model path and the
    // override is looked up here - matching how codegen keys model identity.
    auto overrideFor = std::make_shared<std::map<std::string, std::string>>();
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects)
            if (o.type == PrimitiveType::Model && !o.modelPath.empty())
                (*overrideFor)[o.modelPath] = o.materialPath;

    const std::string dir = p.dir;
    // `warnings` is captured by pointer and must outlive the Inputs, which is
    // true of every caller (the panel owns a vector for the frame, the CLI a
    // local). Said out loud because a lambda outliving its warning sink would
    // be a silent write into freed memory rather than a missing message.
    Inputs in;

    in.model = [models, overrideFor, dir, warnings](
                   const std::string& modelPath) -> ModelInfo {
        auto it = models->find(modelPath);
        if (it != models->end()) return it->second;
        ModelInfo mi;
        std::string ovr;
        if (auto o = overrideFor->find(modelPath); o != overrideFor->end())
            ovr = o->second;
        // Exactly the artifact the game loads, named by codegen's own rule.
        const std::string rel = templates::bakedModelPath(modelPath, ovr);
        const std::string abs =
            (std::filesystem::path(dir) / rel).make_preferred().string();
        const std::string bytes = readFile(abs);
        tmdl::Info info;
        if (!bytes.empty() && tmdl::readInfo(bytes, info)) {
            mi.baked = true;
            for (int a = 0; a < 3; ++a) {
                mi.min[a] = info.min[a];
                mi.max[a] = info.max[a];
            }
            for (const tmdl::PartInfo& pi : info.parts) {
                ModelPart mp;
                // The .tmdl already carries the resolved, bin-relative name,
                // which is what acquireTexture is keyed by - so an empty one
                // is the shared null-texture class and must stay empty.
                mp.texture = pi.texture.empty() ? kNoTexture : pi.texture;
                mp.reflective = !pi.reflTexture.empty();
                mp.stripRun = pi.stripRun;
                mp.vertexCount = pi.vertexCount;
                mp.stripVertexCount = pi.stripVertexCount;
                mi.parts.push_back(mp);
            }
        } else if (warnings) {
            warnings->push_back(
                modelPath + ": no baked " + rel +
                " yet - build the project (or Build > Refresh generated "
                "files) for this model's batching to be known");
        }
        (*models)[modelPath] = mi;
        return mi;
    };

    in.material = [mats, dir, warnings](
                      const std::string& matPath) -> MaterialInfo {
        auto it = mats->find(matPath);
        if (it != mats->end()) return it->second;
        MaterialInfo mi;
        std::vector<objparser::MtlMaterial> lib;
        const std::string abs =
            (std::filesystem::path(dir) / matPath).make_preferred().string();
        if (objparser::loadMtl(abs, lib) && !lib.empty()) {
            mi.known = true;
            // A primitive binds ONE material - the library's first entry, the
            // same one gameMaterials records per material path.
            const objparser::MtlMaterial& m = lib.front();
            mi.reflective = !m.refl.empty();
            if (m.texture.empty()) {
                mi.texture = kNoTexture;
            } else {
                // Resolved against the .mtl's own directory (the Wavefront
                // rule the PS2 loader applies), then normalized: the console
                // cannot walk "..", so a path that needs to is a path the
                // game will not open - and one that does not exist binds the
                // same null texture an untextured object does.
                std::filesystem::path t =
                    std::filesystem::path(matPath).parent_path() / m.texture;
                t = t.lexically_normal();
                const std::string texRel = t.generic_string();
                const std::string texAbs =
                    (std::filesystem::path(dir) / texRel).make_preferred().string();
                if (std::filesystem::exists(texAbs)) {
                    mi.texture = texRel;
                } else {
                    // THE TRAP, reproduced on purpose. acquireTexture hands
                    // back a null pointer for a file that is not there, and a
                    // bag groups by that pointer - so a missing texture is
                    // the SAME group as an untextured object. Do not give it
                    // an identity of its own; the game does not.
                    mi.texture = kNoTexture;
                    if (warnings)
                        warnings->push_back(
                            matPath + ": texture " + texRel +
                            " is missing, so the game binds no texture - it "
                            "batches with untextured objects");
                }
            }
        } else if (warnings) {
            warnings->push_back(matPath + ": could not be read");
        }
        (*mats)[matPath] = mi;
        return mi;
    };

    in.primVertices = [](const SceneObject& o) { return primVertexCount(o); };
    return in;
}

}  // namespace staticbatch

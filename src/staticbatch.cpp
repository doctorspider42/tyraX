#include "staticbatch.hpp"

#include <cmath>

#include "meshstrip.hpp"

// THE TWIN. Every rule below has a counterpart in templates.cpp - the
// build-time half in `staticBatchEligible` (and the lightmap check at its
// call site), the grouping half in the generated `TerrainGame::
// buildStaticBatchList`. Change either and run
// examples/vehicle-playground/authoring/verify-batch-twins.py, which lifts
// the generated function verbatim and diffs both against this one.
namespace staticbatch {

const char* const kNoTexture = "";

unsigned int packageSize() { return meshstrip::kRun; }

namespace {

// Packages a bag of `verts` vertices submits. A STRIPPED bag is chopped into
// whole runs of `stripRun` and each run is exactly one package, so the count
// is a division and not a ceiling - the bake already padded to whole runs. A
// LIST bag is cut at the program's derived package size, and its last package
// is usually short, so that one ceilings.
int packagesFor(unsigned int verts, unsigned int stripRun) {
    if (verts == 0) return 0;
    if (stripRun > 0) return (int)((verts + stripRun - 1) / stripRun);
    const unsigned int n = packageSize();
    return (int)((verts + n - 1) / n);
}

void growBox(float mn[3], float mx[3], const float p[3], bool& first) {
    for (int a = 0; a < 3; ++a) {
        if (first || p[a] < mn[a]) mn[a] = p[a];
        if (first || p[a] > mx[a]) mx[a] = p[a];
    }
    first = false;
}

// The world AABB of one object's geometry, used only for the batch's FRUSTUM
// box. A model takes its baked bounds scaled about its own origin; a
// primitive is the unit cube scaled. Rotation is folded in conservatively by
// taking the scaled half-extent's largest axis, which over-states a rotated
// box slightly - the honest direction for a box whose whole purpose is to say
// "this batch reaches this far".
void objectGeomBox(const SceneObject& o, const ModelInfo* mi, float mn[3],
                   float mx[3]) {
    float half[3] = {0.5f * std::fabs(o.scale[0]), 0.5f * std::fabs(o.scale[1]),
                     0.5f * std::fabs(o.scale[2])};
    float centre[3] = {o.position[0], o.position[1], o.position[2]};
    if (mi && mi->baked) {
        for (int a = 0; a < 3; ++a) {
            const float lo = mi->min[a] * o.scale[a];
            const float hi = mi->max[a] * o.scale[a];
            const float a0 = lo < hi ? lo : hi;
            const float a1 = lo < hi ? hi : lo;
            half[a] = 0.5f * (a1 - a0);
            centre[a] = o.position[a] + 0.5f * (a0 + a1);
        }
    }
    const bool rotated = o.rotation[0] != 0.0f || o.rotation[1] != 0.0f ||
                         o.rotation[2] != 0.0f;
    if (rotated) {
        const float r =
            std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
        half[0] = half[1] = half[2] = r;
    }
    for (int a = 0; a < 3; ++a) {
        mn[a] = centre[a] - half[a];
        mx[a] = centre[a] + half[a];
    }
}

}  // namespace

const char* reasonLabel(Reason r) {
    switch (r) {
        case Reason::Batched: return "batched";
        case Reason::NotABatchableShape: return "not a batchable shape";
        case Reason::InvisibleWall: return "invisible wall";
        case Reason::Physics: return "physics body";
        case Reason::Usable: return "usable";
        case Reason::Pickable: return "pickable";
        case Reason::SaveState: return "save state";
        case Reason::Reflected: return "reflected";
        case Reason::DynamicLighting: return "dynamic lighting";
        case Reason::TextureFeed: return "texture feed";
        case Reason::VuParams: return "VU parameters";
        case Reason::ModelLodOrImpostor: return "mesh LOD / impostor";
        case Reason::StreamingLayer: return "streaming layer";
        case Reason::GraphOrScripts: return "flow graph / script";
        case Reason::RuntimeReferenced: return "referenced at runtime";
        case Reason::LightmapRegion: return "baked lightmap region";
        case Reason::ExcludedByAuthor: return "excluded by author";
        case Reason::BatchingDisabled: return "batching off";
        case Reason::ModelNotBaked: return "model not baked";
        case Reason::ModelHasNoParts: return "model has no parts";
        case Reason::ReflectiveMaterial: return "reflective material";
        case Reason::FootprintTooBigForCell: return "too big for its cell";
        case Reason::SingletonGroup: return "alone in its group";
    }
    return "?";
}

const char* reasonDetail(Reason r) {
    switch (r) {
        case Reason::Batched:
            return "Merged into a shared bag with the other members.";
        case Reason::NotABatchableShape:
            return "Only Box, Sphere, Cylinder, Cone, Plane and Model carry "
                   "static geometry a batch can merge.";
        case Reason::InvisibleWall:
            return "Collision set to invisible: the object emits no geometry "
                   "at all, so there is nothing to batch.";
        case Reason::Physics:
            return "A rigid body is repositioned every frame while it moves, "
                   "which would re-bake its whole batch.";
        case Reason::Usable:
            return "The USE highlight re-submits the body on its own, which a "
                   "merged member has no bag for.";
        case Reason::Pickable:
            return "Carried and thrown objects move at runtime.";
        case Reason::SaveState:
            return "Loading a save repositions it, so its baked vertices "
                   "would be stale.";
        case Reason::Reflected:
            return "Re-submitted into the reflection pass, which needs the "
                   "object's own bag.";
        case Reason::DynamicLighting:
            return "Wants its own VU1-lit bag refilled every frame. A batch "
                   "is one baked bag and cannot carry it.";
        case Reason::TextureFeed:
            return "A live camera feed rebinds this object's texture, and a "
                   "batch groups BY texture.";
        case Reason::VuParams:
            return "Per-mesh VU numbers are uploaded once per bag, so a "
                   "batched member would get the batch's, not its own.";
        case Reason::ModelLodOrImpostor:
            return "Mesh LOD or an impostor can switch this model's "
                   "representation at runtime; a merged bag has only one.";
        case Reason::StreamingLayer:
            return "Streamed in and out with its layer, independently of the "
                   "rest of the batch.";
        case Reason::GraphOrScripts:
            return "Per-object logic can move or hide it.";
        case Reason::RuntimeReferenced:
            return "Named by a flow node, mirror, portal, cutscene track or "
                   "catch area - anything that can re-submit or move it.";
        case Reason::LightmapRegion:
            return "Owns a region in the baked lighting atlas, which is drawn "
                   "as extra per-object passes.";
        case Reason::ExcludedByAuthor:
            return "Excluded by hand (Properties > Exclude from static "
                   "batch).";
        case Reason::BatchingDisabled:
            return "Static batching is off for the whole project (Project "
                   "Preferences > Rendering).";
        case Reason::ModelNotBaked:
            return "No baked .tmdl for this model yet - build the project, or "
                   "run Build > Refresh generated files.";
        case Reason::ModelHasNoParts:
            return "The baked model has no material parts to draw.";
        case Reason::ReflectiveMaterial:
            return "A reflective material draws a second additive pass per "
                   "bag, which only a solo bag can do.";
        case Reason::FootprintTooBigForCell:
            return "Its footprint is over half its grouping cell. Merging it "
                   "would widen the batch's box enough to defeat the "
                   "whole-bag frustum cut.";
        case Reason::SingletonGroup:
            return "Nothing else shares its texture, cell, draw distance, "
                   "lamp and strip run. A batch of one saves no submit and "
                   "only duplicates geometry, so it is dropped.";
    }
    return "";
}

const char* reasonStage(Reason r) {
    switch (r) {
        case Reason::Batched: return "";
        case Reason::BatchingDisabled:
        case Reason::ModelNotBaked:
        case Reason::ModelHasNoParts:
        case Reason::ReflectiveMaterial:
        case Reason::FootprintTooBigForCell:
        case Reason::SingletonGroup:
            return "runtime";
        default: return "build";
    }
}

std::set<std::string> blockedNames(const Project& p, const SceneData& sc) {
    // The twin of templates.cpp `batchBlockedNames`. Over-excluding is safe
    // here too - the cost is one solo bag - but UNDER-excluding would make
    // the panel promise a batch the build refuses.
    std::set<std::string> refs = project::runtimeRefNames(p, sc.objects);
    std::set<std::string> extra;
    for (const SceneObject& o : sc.objects) {
        if (o.catchArea.empty()) continue;
        for (int ti : project::areaCaughtObjects(sc.objects, o.catchArea, -1))
            extra.insert(sc.objects[ti].name);
        if (!o.catchAreaLive) continue;
        for (int ci : project::areaLiveCandidates(sc.objects, -1, refs))
            extra.insert(sc.objects[ci].name);
    }
    refs.insert(extra.begin(), extra.end());
    return refs;
}

Reason eligibility(const Project& p, const SceneObject& o,
                   const std::set<std::string>& blocked,
                   bool hasLightmapRegion) {
    // Order follows staticBatchEligible so the two read side by side. The
    // author's own opt-out is tested FIRST: when somebody has excluded an
    // object deliberately, that is the answer they want to see, not whichever
    // incidental rule would also have caught it.
    if (o.batchExclude) return Reason::ExcludedByAuthor;

    const bool shape =
        o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
        o.type == PrimitiveType::Plane || o.type == PrimitiveType::Model;
    if (!shape) return Reason::NotABatchableShape;
    if (o.collisionMode == 3) return Reason::InvisibleWall;
    if (o.physics) return Reason::Physics;
    if (o.usable) return Reason::Usable;
    if (o.pickable) return Reason::Pickable;
    if (o.saveState) return Reason::SaveState;
    if (o.reflected) return Reason::Reflected;
    if (o.dynamicLighting) return Reason::DynamicLighting;
    if (!o.textureFeed.empty()) return Reason::TextureFeed;
    if (o.vuParams[0] != 0.0f || o.vuParams[1] != 0.0f ||
        o.vuParams[2] != 0.0f || o.vuParams[3] != 0.0f)
        return Reason::VuParams;
    if (o.type == PrimitiveType::Model &&
        (o.impostorDistance > 0.0f ||
         (o.meshLodOverride < 0.0f ? p.settings.meshLodDistance
                                   : o.meshLodOverride) > 0.0f))
        return Reason::ModelLodOrImpostor;
    if (!o.layer.empty()) return Reason::StreamingLayer;
    if (!o.flowGraph.nodes.empty() || !o.scripts.empty())
        return Reason::GraphOrScripts;
    if (blocked.find(o.name) != blocked.end()) return Reason::RuntimeReferenced;
    // Not part of staticBatchEligible itself, but ANDed with it where the
    // batchStatic column is written (templates.cpp), so it belongs in the
    // same verdict - an object with an atlas region really is not batchable.
    if (hasLightmapRegion) return Reason::LightmapRegion;
    return Reason::Batched;
}

Result compute(const Project& p, const SceneData& sc, const Inputs& in,
               const std::vector<char>& lightmapRegion) {
    Result res;
    const int count = (int)sc.objects.size();
    res.objects.assign((size_t)count, ObjectVerdict());

    // --- stage 1 ------------------------------------------------------------
    const std::set<std::string> blocked = blockedNames(p, sc);
    std::vector<char> flagged((size_t)count, 0);
    for (int i = 0; i < count; ++i) {
        const bool region =
            i < (int)lightmapRegion.size() && lightmapRegion[(size_t)i] != 0;
        const Reason r = eligibility(p, sc.objects[(size_t)i], blocked, region);
        res.objects[(size_t)i].reason = r;
        if (r == Reason::Batched) {
            flagged[(size_t)i] = 1;
            ++res.eligible;
        }
    }

    // The project-wide switch is stage 2, not stage 1: the flag is still
    // baked into scene_data.hpp, the runtime simply never groups. Reporting
    // it per object is what stops the panel looking broken when it is off.
    if (!p.settings.staticBatching) {
        for (int i = 0; i < count; ++i)
            if (flagged[(size_t)i])
                res.objects[(size_t)i].reason = Reason::BatchingDisabled;
        return res;
    }

    // --- the grid -----------------------------------------------------------
    // mapW and baseCellW exactly as the runtime derives them. Terrain that is
    // switched off still states a size, and so does the runtime (the table is
    // emitted either way), so this does not branch on it.
    const float mapW = sc.terrain.width > sc.terrain.depth
                           ? (float)sc.terrain.width
                           : (float)sc.terrain.depth;
    const float baseCellW = mapW * 0.25f > 48.0f ? mapW * 0.25f : 48.0f;
    res.mapW = mapW;
    res.baseCellW = baseCellW;
    auto cellFor = [&](float drawDistance) -> float {
        if (drawDistance <= 0.0f) return baseCellW;
        return drawDistance < baseCellW ? drawDistance : baseCellW;
    };

    // The dynamic lights, in the order collectScenePointLights builds them:
    // type 9 with lightDynamic, scene order, capped at the engine's
    // DYN_LIGHTS_MAX. The cap matters - a ninth lamp reaches nothing as far
    // as the grouping is concerned, and dropping it here would key objects
    // apart that the game keys together.
    constexpr int kDynLightsMax = 8;  // Tyra::RendererCore::DYN_LIGHTS_MAX
    std::vector<int> dynLights;
    for (int i = 0; i < count && (int)dynLights.size() < kDynLightsMax; ++i) {
        const SceneObject& L = sc.objects[(size_t)i];
        if (L.type == PrimitiveType::PointLight && L.lightDynamic)
            dynLights.push_back(i);
    }
    // Nearest reaching lamp, centre against radius - the twin of `lampOf`.
    // Not the engine's brightness x falloff score: the key only has to keep
    // "a lamp reaches this" apart from "nothing reaches this".
    auto lampOf = [&](const SceneObject& d) -> int {
        int best = -1;
        float bestD2 = 0.0f;
        for (size_t li = 0; li < dynLights.size(); ++li) {
            const SceneObject& L = sc.objects[(size_t)dynLights[li]];
            if (L.lightRadius <= 0.0f) continue;
            const float dx = L.position[0] - d.position[0];
            const float dy = L.position[1] - d.position[1];
            const float dz = L.position[2] - d.position[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > L.lightRadius * L.lightRadius) continue;
            if (best < 0 || d2 < bestD2) {
                best = (int)li;
                bestD2 = d2;
            }
        }
        // Reported as the LAMP'S OBJECT INDEX rather than its slot, because a
        // panel naming "light-3" is useful and "lamp 1" is not. Equivalence
        // classes are identical either way.
        return best < 0 ? -1 : dynLights[(size_t)best];
    };

    // --- stage 2: grouping --------------------------------------------------
    std::vector<int> keyX, keyZ;
    auto addMember = [&](int object, int part, const std::string& texture,
                         int cx, int cz, float cellW, float dd, int lamp,
                         unsigned int stripRun, unsigned int verts) {
        int bi = -1;
        for (int b = 0; b < (int)res.batches.size(); ++b)
            if (res.batches[(size_t)b].texture == texture && keyX[(size_t)b] == cx &&
                keyZ[(size_t)b] == cz && res.batches[(size_t)b].drawDistance == dd &&
                res.batches[(size_t)b].lamp == lamp &&
                res.batches[(size_t)b].stripRun == stripRun) {
                bi = b;
                break;
            }
        if (bi < 0) {
            Batch nb;
            nb.texture = texture;
            nb.cellX = cx;
            nb.cellZ = cz;
            nb.cellW = cellW;
            nb.drawDistance = dd;
            nb.lamp = lamp;
            nb.stripRun = stripRun;
            res.batches.push_back(nb);
            keyX.push_back(cx);
            keyZ.push_back(cz);
            bi = (int)res.batches.size() - 1;
        }
        res.batches[(size_t)bi].members.push_back({object, part});
        // A batch's own package count is the merged vertex run, so it is
        // accumulated and divided once at the end; the solo figure is the sum
        // of what each member would submit ALONE, which is the comparison
        // that says whether the merge pays.
        res.batches[(size_t)bi].packages += (int)verts;  // vertices for now
        res.batches[(size_t)bi].soloPackages += packagesFor(verts, stripRun);
    };

    std::vector<Reason> runtimeReason((size_t)count, Reason::Batched);
    for (int i = 0; i < count; ++i) {
        if (!flagged[(size_t)i]) continue;
        const SceneObject& d = sc.objects[(size_t)i];
        const float cellW = cellFor(d.drawDistance);
        const int cx = (int)std::floor((d.position[0] + 0.5f * mapW) / cellW);
        const int cz = (int)std::floor((d.position[2] + 0.5f * mapW) / cellW);
        const int lamp = lampOf(d);

        if (d.type == PrimitiveType::Model) {
            ModelInfo mi = in.model ? in.model(d.modelPath) : ModelInfo();
            if (!mi.baked) {
                runtimeReason[(size_t)i] = Reason::ModelNotBaked;
                res.anyModelUnbaked = true;
                continue;
            }
            // Footprint against the cell, before anything else looks at the
            // parts - the runtime tests it in that order too.
            const float spanX = (mi.max[0] - mi.min[0]) * std::fabs(d.scale[0]);
            const float spanZ = (mi.max[2] - mi.min[2]) * std::fabs(d.scale[2]);
            if (std::sqrt(spanX * spanX + spanZ * spanZ) > 0.5f * cellW) {
                runtimeReason[(size_t)i] = Reason::FootprintTooBigForCell;
                continue;
            }
            if (mi.parts.empty()) {
                runtimeReason[(size_t)i] = Reason::ModelHasNoParts;
                continue;
            }
            bool reflective = false;
            for (const ModelPart& mp : mi.parts)
                if (mp.reflective) { reflective = true; break; }
            if (reflective) {
                runtimeReason[(size_t)i] = Reason::ReflectiveMaterial;
                continue;
            }
            for (int pi = 0; pi < (int)mi.parts.size(); ++pi) {
                const ModelPart& mp = mi.parts[(size_t)pi];
                // The bag renders the STRIP when the bake produced one, and
                // the list otherwise - which is also what decides the
                // stripRun key, so an unstripped part groups with other
                // unstripped parts and never with a stripped one.
                const bool stripped = mp.stripVertexCount > 0 && mp.stripRun > 0;
                addMember(i, pi, mp.texture, cx, cz, cellW, d.drawDistance,
                          lamp, stripped ? mp.stripRun : 0u,
                          stripped ? mp.stripVertexCount : mp.vertexCount);
            }
        } else {
            MaterialInfo mat =
                d.materialPath.empty() ? MaterialInfo() : in.material
                    ? in.material(d.materialPath)
                    : MaterialInfo();
            if (!d.materialPath.empty() && mat.reflective) {
                runtimeReason[(size_t)i] = Reason::ReflectiveMaterial;
                continue;
            }
            // An untextured primitive and a primitive whose texture file is
            // MISSING both bind the null texture in the game, so they share
            // one group. Reproduced deliberately: acquireTexture caches by
            // path and hands back nullptr for a file that is not there.
            const std::string texture =
                (!d.materialPath.empty() && mat.known) ? mat.texture : kNoTexture;
            const unsigned int verts =
                in.primVertices ? in.primVertices(d) : 0u;
            addMember(i, -1, texture, cx, cz, cellW, d.drawDistance, lamp, 0u,
                      verts);
        }
    }

    // --- singletons ---------------------------------------------------------
    // Dropped exactly as the runtime drops them, and the members of a dropped
    // batch get the reason that explains it rather than falling through to a
    // blank cell.
    std::vector<Batch> kept;
    kept.reserve(res.batches.size());
    for (Batch& b : res.batches) {
        if (b.members.size() < 2) {
            for (const Member& m : b.members)
                if (runtimeReason[(size_t)m.object] == Reason::Batched)
                    runtimeReason[(size_t)m.object] = Reason::SingletonGroup;
            continue;
        }
        kept.push_back(std::move(b));
    }
    res.batches = std::move(kept);

    // A multi-part model can have some parts in surviving batches and others
    // dropped as singletons. The object is BATCHED if any part survived -
    // matching objectBatchOf, which is != -1 in that case - so the singleton
    // note above is only kept for objects that landed nowhere at all.
    for (int bi = 0; bi < (int)res.batches.size(); ++bi)
        for (const Member& m : res.batches[(size_t)bi].members) {
            runtimeReason[(size_t)m.object] = Reason::Batched;
            res.objects[(size_t)m.object].batches.push_back(bi);
        }

    for (int i = 0; i < count; ++i) {
        if (!flagged[(size_t)i]) continue;
        res.objects[(size_t)i].reason = runtimeReason[(size_t)i];
        if (runtimeReason[(size_t)i] == Reason::Batched) ++res.batched;
    }

    // --- boxes and packages -------------------------------------------------
    // Every shape that reached the grouping gets its own world box, whether
    // it ended up batched or solo: the overlay draws the solo ones too, and
    // "why is this one not in the batch beside it" is a question about where
    // it sits.
    for (int i = 0; i < count; ++i) {
        if (!flagged[(size_t)i]) continue;
        const SceneObject& o = sc.objects[(size_t)i];
        ModelInfo mi;
        const ModelInfo* mip = nullptr;
        if (o.type == PrimitiveType::Model && in.model) {
            mi = in.model(o.modelPath);
            if (mi.baked) mip = &mi;
        }
        ObjectVerdict& v = res.objects[(size_t)i];
        objectGeomBox(o, mip, v.geomMin, v.geomMax);
        v.hasGeom = true;
    }
    for (Batch& b : res.batches) {
        bool firstDd = true, firstGeom = true;
        for (const Member& m : b.members) {
            const SceneObject& o = sc.objects[(size_t)m.object];
            growBox(b.ddMin, b.ddMax, o.position, firstDd);
            const ObjectVerdict& v = res.objects[(size_t)m.object];
            growBox(b.geomMin, b.geomMax, v.geomMin, firstGeom);
            growBox(b.geomMin, b.geomMax, v.geomMax, firstGeom);
        }
        // `packages` accumulated VERTICES above; turn it into the merged
        // bag's package count now that every member is in.
        b.packages = packagesFor((unsigned int)b.packages, b.stripRun);
    }
    return res;
}

}  // namespace staticbatch

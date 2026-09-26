#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "project.hpp"

// staticbatch: the HOST TWIN of the generated game's static-batch grouping.
//
// WHY THIS IS A TWIN AND NOT A SHARED FUNCTION. The grouping itself is not
// host code and cannot become host code. It lives in `templates.cpp` as the
// raw-string body of `TerrainGame::buildStaticBatchList()`, i.e. as C++ the
// EE runs at scene load, and it reads things that only exist then: the loaded
// `gameModels`/`gameMaterials`, the live `g_dynLights` list, and - the one
// that decides the whole grouping - the `Tyra::Texture*` POINTERS the engine
// handed back. Moving it host-side would mean baking a batch table into
// `inc/scene_data.hpp`, which changes the generated output of every project
// that exists. So the answer here is the repo's standing one for a formula
// with two homes (scrollsim/scroller.gen.cpp, livelogic, menulayout,
// daynight.gen.hpp): ONE implementation per side, both flagged as twins, and
// an ORACLE that compares them rather than trust.
//
// THE ORACLE IS NOT OPTIONAL and it is not a unit test of this file:
// `examples/vehicle-playground/authoring/verify-batch-twins.py` lifts
// `buildStaticBatchList` VERBATIM out of templates.cpp, compiles it against
// small stubs beside this module, runs both on the same fixtures and diffs
// the batch assignment member for member. That is the roadgen arrangement
// (`verify-road-twins.py`) and it exists for the same reason: a preview that
// silently disagrees with the console is worse than no preview, because it
// is believed. Change either side and run it.
//
// The eligibility half gets a second, free check. `batchStatic` in a
// generated `inc/scene_data.hpp` is exactly `eligibility()`'s verdict, so any
// regenerated project is an oracle for it - 87 of the 142 objects on
// examples/vehicle-playground, which is the number `src/version.hpp` records
// for that scene.
//
// Host-only: no GL, no ImGui, no templates.cpp. Everything the runtime reads
// from loaded assets arrives through the two callbacks below, which is what
// keeps this exercisable from a 40-line harness.
namespace staticbatch {

// ---------------------------------------------------------------------------
// Why an object is not in a batch
// ---------------------------------------------------------------------------
// ENUMERATED FROM THE CODE, in the two stages that really exist - and the
// split is the point. A build-time reason is a property of the object the
// author can act on; a runtime reason depends on what else is in the cell and
// on assets that are only loaded in the game. Reporting them as one list is
// what made "111 of 142 objects were batchable shapes and 27 carried the
// flag" take three rounds of measurement to notice (docs/static-batching.md).
enum class Reason {
    Batched = 0,  // it IS in a batch - not a reason at all

    // -- stage 1, build time: templates.cpp `staticBatchEligible` + the
    //    lightmap check at its call site. These decide the `batchStatic`
    //    column baked into inc/scene_data.hpp.
    NotABatchableShape,  // not Box/Sphere/Cylinder/Cone/Plane/Model
    InvisibleWall,       // collisionMode == 3: no geometry is emitted at all
    Physics,             // a rigid body moves every frame while it falls
    Usable,              // the USE highlight defers and re-submits the body
    Pickable,            // carried and thrown, so it moves at runtime
    SaveState,           // a loaded save repositions it
    Reflected,           // re-submitted into the env-map pass
    DynamicLighting,     // wants its own lit bag, which a batch cannot carry
    TextureFeed,         // a live feed rebinds its texture
    VuParams,            // per-mesh VU numbers are uploaded per BAG
    ModelLodOrImpostor,  // its representation can switch at runtime
    StreamingLayer,      // streamed in and out with its layer
    GraphOrScripts,      // per-object logic can move it
    RuntimeReferenced,   // named by a flow node, mirror, portal, cutscene or
                         // catch area - anything that can re-submit it
    LightmapRegion,      // owns a baked AO/GI atlas region, so it draws solo
    ExcludedByAuthor,    // SceneObject::batchExclude - the manual opt-out

    // -- stage 2, runtime: templates.cpp `buildStaticBatchList`, among the
    //    objects that DID carry the flag.
    BatchingDisabled,     // ProjectSettings::staticBatching is off project-wide
    ModelNotBaked,        // no .tmdl yet, or its index is out of range
    ModelHasNoParts,      // nothing to draw
    ReflectiveMaterial,   // draws a second additive env pass per bag
    FootprintTooBigForCell,  // span > half its cell: merging would defeat the
                             // whole-bag frustum cut
    SingletonGroup,       // the only member of its key - a batch of one saves
                          // no submit and only duplicates geometry
};

// A short label for a panel cell, and the one-line reason behind it. Two
// functions rather than one string so the list stays scannable and the
// explanation lands in a tooltip (the repo's terse-panel rule).
const char* reasonLabel(Reason r);
const char* reasonDetail(Reason r);
// Which stage decided it: "build" or "runtime" ("" for Batched). An author
// can act on a build reason immediately; a runtime one usually means moving
// the object or giving it company in its cell.
const char* reasonStage(Reason r);

// ---------------------------------------------------------------------------
// What the caller has to supply about loaded assets
// ---------------------------------------------------------------------------
// The runtime groups by the `Texture*` the engine handed back, and that
// pointer is `texCache` keyed by PATH (templates.cpp `acquireTexture`). So a
// path string is an exact stand-in for it, with ONE trap that the twin must
// reproduce rather than tidy away: a texture whose file is MISSING gets a
// null pointer, and so does an untextured primitive - so every missing
// texture merges with every untextured object into a single group. That is
// what the engine does, it is easy to "fix" into a bug, and the oracle
// carries a deliberate missing-texture fixture because of it.
//
// `kNoTexture` is that shared class, spelled once.
extern const char* const kNoTexture;

// One model asset, as the grouping sees it. Filled from the BAKED .tmdl
// (tmdl::readInfo) rather than re-derived from the .obj: `stripRun` and the
// resolved texture names are products of the bake, and a second derivation of
// them is the drift the oracle exists to catch.
struct ModelPart {
    std::string texture;      // resolved, or kNoTexture
    bool reflective = false;  // any refl map -> the whole model stays solo
    unsigned int stripRun = 0;
    unsigned int vertexCount = 0;       // tier-0 triangle list
    unsigned int stripVertexCount = 0;  // 0 = ships no strip
};

struct ModelInfo {
    bool baked = false;  // false = no .tmdl on disk yet; report, never guess
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    std::vector<ModelPart> parts;
};

// modelPath (project-relative, as SceneObject::modelPath stores it) -> info.
using ModelInfoFn = std::function<ModelInfo(const std::string& modelPath)>;

// materialPath -> the texture it binds and whether it is reflective. The
// primitive half of the same question.
struct MaterialInfo {
    bool known = false;
    std::string texture;      // resolved, or kNoTexture
    bool reflective = false;
};

// Vertices one primitive tessellates to at its authored detail. Primitives
// are built by the generated game from primmesh's own tessellation, so this
// is the caller's to answer (the editor asks primmesh); it exists only to
// price the object in VU1 packages.
using PrimVertexFn = std::function<unsigned int(const SceneObject&)>;
using MaterialInfoFn = std::function<MaterialInfo(const std::string& matPath)>;

struct Inputs {
    ModelInfoFn model;
    MaterialInfoFn material;
    PrimVertexFn primVertices;  // optional; without it primitives price as 0
};

// Vertices per VU1 package for the class a static batch bag takes (textured,
// Gouraud, per-vertex colour). It is `meshstrip::kRun` because that constant
// is pinned to exactly this: StaPipVU1Program::getMaxVertCount derives 75 for
// this class, and a baked strip is chopped into runs of that size so a
// package boundary never falls inside a run. Anything that moves one moves
// both - see meshstrip.hpp and docs/model-pipeline.md.
unsigned int packageSize();

// ---------------------------------------------------------------------------
// The result
// ---------------------------------------------------------------------------
struct Member {
    int object = -1;  // index into SceneData::objects
    int part = -1;    // model material part, -1 for a primitive
};

struct Batch {
    // The group key, in full. Two objects share a batch exactly when all six
    // agree - texture, cell, draw distance, reaching lamp and strip run.
    std::string texture;  // kNoTexture for the untextured/missing class
    int cellX = 0;
    int cellZ = 0;
    float cellW = 0.0f;       // this group's own cell, capped by drawDistance
    float drawDistance = 0.0f;  // 0 = unlimited
    int lamp = -1;            // object index of the reaching dynamic light
    unsigned int stripRun = 0;

    std::vector<Member> members;

    // THE TWO BOXES, and they answer different questions. Keeping them apart
    // is the whole reason this struct is worth having: the regression that
    // motivated the overlay is a batch drawing geometry the unbatched scene
    // culls, and which box did it depends on which test.
    //
    // ddMin/ddMax is the box over member POSITIONS - what renderStaticBatches
    // measures the draw distance against, once for the whole batch. A member
    // can outlive its own cut-off by up to the spread of this box.
    float ddMin[3] = {0, 0, 0};
    float ddMax[3] = {0, 0, 0};
    // geomMin/geomMax is the box over member GEOMETRY - what the frustum
    // classifies. This is the one that grows when a big object joins a batch
    // of small ones.
    float geomMin[3] = {0, 0, 0};
    float geomMax[3] = {0, 0, 0};

    // VU1 packages this batch submits, and what its members would submit
    // drawn solo. The difference is the batch's whole reason to exist, and
    // packages are the unit the EE actually pays for (docs/vu1-packages.md).
    int packages = 0;
    int soloPackages = 0;
};

struct ObjectVerdict {
    Reason reason = Reason::Batched;
    // Batch indices this object's parts landed in. A multi-part model can
    // legitimately span several (the runtime's objectBatchOf == -2 case), so
    // this is a list and not an index.
    std::vector<int> batches;
    // This object's own world AABB - the same box Batch::geomMin/geomMax is
    // the union of. Published rather than left private so the overlay draws
    // members and merged boxes from ONE derivation: a viewport that computed
    // its own would be a third answer to "how big is this object", and the
    // member boxes could then fail to sit inside the merged box they are
    // supposed to explain. Only filled for shapes that reached the grouping.
    float geomMin[3] = {0, 0, 0};
    float geomMax[3] = {0, 0, 0};
    bool hasGeom = false;
};

struct Result {
    std::vector<Batch> batches;
    std::vector<ObjectVerdict> objects;  // parallel to SceneData::objects

    float baseCellW = 0.0f;  // the cell before any draw-distance cap
    float mapW = 0.0f;
    int eligible = 0;  // objects carrying batchStatic
    int batched = 0;   // objects that really landed in a surviving batch
    // eligible - batched is the runtime's own `solo` figure. The generated
    // game already LOGS both totals ("Static batching: eligible N, solo M");
    // what it cannot say is WHICH objects and why each one, which is what
    // `objects` above adds.

    bool anyModelUnbaked = false;  // a .tmdl was missing: counts are provisional
};

// ---------------------------------------------------------------------------
// The two entry points
// ---------------------------------------------------------------------------

// Stage 1 alone, for one object: the twin of `staticBatchEligible` plus the
// lightmap-region check. `blocked` is project::runtimeRefNames expanded with
// catch-area contents - pass the set `blockedNames` below returns.
//
// Returns Reason::Batched when the object is ELIGIBLE (nothing is decided
// about grouping yet), or the build-time reason it is not.
Reason eligibility(const Project& p, const SceneObject& o,
                   const std::set<std::string>& blocked, bool hasLightmapRegion);

// The name set stage 1 tests against - runtime references plus catch-area
// contents. Split out because it is one walk over the scene and every object
// tests against the same answer.
std::set<std::string> blockedNames(const Project& p, const SceneData& sc);

// The real project's assets, read off disk: baked .tmdl per model identity
// (via templates::bakedModelPath, so the panel asks for exactly the artifact
// the game loads) and .mtl per primitive material. Defined in its OWN
// translation unit - staticbatchdisk.cpp - so the twin above stays linkable
// on its own, which is what lets verify-batch-twins.py compile it without
// dragging in templates.cpp.
//
// `warnings` collects per-asset trouble (a model with no .tmdl yet, an
// unreadable .mtl) for the panel to show; pass nullptr to ignore it.
Inputs diskInputs(const Project& p, std::vector<std::string>* warnings);

// Stage 1 + stage 2: the whole grouping, the twin of buildStaticBatchList.
// `lightmapRegion` is per authored object (aobake's firstRegion >= 0), empty
// = none; it is passed in rather than computed because the atlas bake is the
// caller's to own and this module must not pull it in.
Result compute(const Project& p, const SceneData& sc, const Inputs& in,
               const std::vector<char>& lightmapRegion);

}  // namespace staticbatch

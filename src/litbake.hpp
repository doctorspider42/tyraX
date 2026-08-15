#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gibake.hpp"
#include "project.hpp"

// Scene light baked INTO one placed object's texture (docs/prelit-models.md).
// Host-only, no GL - the aobake/matbake/decalproj pattern.
//
// WHY THIS EXISTS, in one paragraph. A textured surface can never take this
// engine's lightmap: the atlas pass is ADDITIVE, and a flat additive term over
// a texture blows out its dark texels. That is not an implementation choice
// that can be revisited - the GS blend unit computes Cv = (A - B) * C + D with
// A, B, D chosen from {Cs, Cd, 0} and C always an ALPHA (As, Ad or FIX), so
// "texture times lightmap" is not expressible in a second pass at all. Which
// leaves exactly one way to put per-pixel light on a textured model, and it is
// the way the survival-horror games of the era did it: bake the light INTO the
// albedo and ship a unique, pre-lit texture for that surface.
//
// So this module is the missing half of a machine that was already here. gibake
// computes the light (sky, sun behind a shadow ray, emissive materials, baked
// point lights, bounces, all over a triangle BVH of the whole scene); matbake
// showed how to rasterize a model's UV space. What was missing was the join:
// walk the object's UV islands, ask gibake what light arrives at each texel's
// WORLD position, multiply it into the albedo there, and write the result as
// that object's own material.
//
// The price is the one the era paid too: a pre-lit surface needs its own
// texture, so this trades GS VRAM for per-pixel light. It is a per-OBJECT
// operation for that reason - you spend it on the wall the player walks up to,
// not on everything.
namespace litbake {

struct Params {
    int size = 128;   // output texture width = height, pow2, 32..512
    int rays = 96;    // hemisphere rays per texel (gibake::gather)
    int padding = 4;  // dilate ring in texels: bilinear/mip seam guard
    // Multiplies the baked light before it hits the albedo. 1.0 = physical.
    // Below 1 keeps more of the texture's own value in the dark parts, which
    // is what you want when the scene's dynamic lights do most of the work.
    float strength = 1.0f;
    // Never darken past this, per channel. A surface the bake finds no light
    // for goes black, and a black texture is indistinguishable from a bug when
    // the player's torch is pointed straight at it.
    float floorLevel = 0.12f;
    uint32_t seed = 1;
};

// What the bake produced, ready to be written by the caller.
struct Result {
    std::vector<uint8_t> rgba;  // size*size*4, the pre-lit albedo
    int size = 0;
    int litTexels = 0;    // texels covered by a UV island
    float meanLight = 0;  // average gathered light, for the report line
    // The model's OWN material names, in submesh order. applyToObject writes
    // one .mtl entry per name, because a material override binds by usemtl
    // NAME: an override library that does not carry the model's names replaces
    // nothing, the parts silently lose their textures, and a prelit object
    // with no texture renders as a pure white block (found in PCSX2, not in
    // review).
    std::vector<std::string> materials;
};

// Bakes ONE placed object. `scene` must already be built and solved for this
// scene (gibake::build + gibake::solve) - it is passed in rather than built
// here so baking several objects pays for the BVH and the bounce solve once.
//
// Model objects only, for now: a primitive that is untextured already has the
// per-texel route through the scene lightmap atlas, which costs no extra
// texture at all and is the better answer where it applies.
bool bakeObject(const Project& p, const SceneData& sc, int objectIndex,
                const gibake::Scene& scene, const Params& prm, Result& out,
                std::string& err);

// Writes the result as `<name>-lit.png` + a one-entry `<name>-lit.mtl` under
// res/materials/, and points the object at it (materialPath) with prelit set.
// It ALSO stamps the bookkeeping the management layer runs on: prelitWanted,
// prelitSig, and - on the FIRST bake only - prelitSource, the material to go
// back to. `sig` is litbake::signature for this object (0 = do not stamp).
// Returns "" on success or the reason it failed.
std::string applyToObject(Project& p, SceneData& sc, int objectIndex,
                          const Result& r, uint64_t sig = 0);

// Where applyToObject writes an object's pre-lit image, project-relative. The
// ONE derivation of that name - the writer and the panel's VRAM readout both
// call it, so the editor cannot report on a file the bake does not produce.
std::string outputPngRel(const SceneObject& o);
std::string outputMtlRel(const SceneObject& o);

// Everything that can change what a bake of THIS object produces, in one
// number: the scene's whole light (gibake::signature - sky, sun, emissives,
// baked point lights, every contributing object's transform and material file),
// this object's own transform and model, the CONTENT of the source material it
// reads its albedo from, the bake parameters, and - when Model AO resolves on
// for the asset - that map's signature, because litbake multiplies it into the
// albedo (docs/ambient-occlusion.md, "Model AO").
//
// Fresh = SceneObject::prelitSig == signature(...). Content hashes, never
// mtimes: the gibake rule, so a checkout or a copy is not an edit.
uint64_t signature(const Project& p, const SceneData& sc, int objectIndex,
                   const Params& prm);

// The same thing split in two, for anything asking about SEVERAL objects of
// one scene: the scene half reads every file the GI bake reads, so it is
// hashed once and handed to each object.
//
// It hashes the scene AS AUTHORED - every pre-lit override normalized back to
// its prelitSource. Without that, applying a bake would change the scene
// signature and make the object it just baked (and every other pre-lit object
// beside it) read stale on the very next frame.
uint64_t sceneSignature(const Project& p, const SceneData& sc);
uint64_t signature(const Project& p, const SceneData& sc, int objectIndex,
                   const Params& prm, uint64_t sceneSig);

// Is this object's baked texture still what the scene would produce now?
bool fresh(const Project& p, const SceneData& sc, int objectIndex,
           const Params& prm);

// The same question for a WHOLE scene, one entry per object (0 for anything
// never baked). Use this and not a loop over fresh(): the scene half of the
// signature reads every file the GI bake reads, so asking per object pays for
// it N times - and a scene with nothing pre-lit pays nothing at all.
std::vector<char> freshFlags(const Project& p, const SceneData& sc,
                             const Params& prm);

// The way back: point the object at the material it had before its first bake
// and forget the whole thing. The -lit.png / -lit.mtl are deliberately LEFT ON
// DISK - they are ordinary assets, the Asset Browser already reports unused
// ones, and deleting a file behind the user's back on an undoable edit is the
// worse failure. The caller commits.
void revertObject(SceneObject& o);

// The MANAGED bake, synchronous: every prelitWanted object whose texture no
// longer matches the scene is re-baked and applied IN PLACE (materialPath,
// prelit, prelitSig), the fresh ones are left alone. One gibake::build + solve
// per scene that has any work, however many objects come out of it. Three
// callers, one loop: `--bake-prelit`, the pre-build step of `--build` and of
// App::projectForBuild when ProjectSettings::prelitAutoBake is on. It does not
// save and does not commit - the caller owns that, because what "the edit
// landed" means differs between a CLI (project::save) and the editor
// (commitChange, so it is undoable and reaches session peers).
//
// `sceneName` empty = every scene. `log` receives one line per object
// (`baked ...` / `fresh ...` / `error ...`) plus the summary; may be null.
struct StaleReport {
    int wanted = 0, baked = 0, kept = 0, failed = 0;
    bool sceneFound = false;  // false only when sceneName named nothing
    std::string firstError;
};
StaleReport bakeStale(Project& p, const Params& prm, const std::string& sceneName,
                      const std::function<void(const std::string&)>& log);

// Asynchronous baker for the editor - the gibake::Baker pattern (worker thread,
// polled from the UI). The scene build and the bounce solve are what make this
// worth a thread: the gather itself is seconds, the solve is the rest.
//
// It deliberately does NOT touch the Project. The worker takes a copy, and the
// finished Result is handed back through take() for the UI thread to apply,
// because applying it edits the object and that has to go through the editor's
// own commit/undo path like every other edit.
class Baker {
public:
    // One finished object, ready for the UI thread to apply. `sig` is the
    // signature the bake ran under - stamped onto the object by applyToObject,
    // and computed on the WORKER because it hashes half the project's files.
    struct Done {
        int objectIndex = -1;
        uint64_t sig = 0;
        Result result;
    };

    ~Baker() { cancel(); }
    // One object (the Properties button).
    void start(const Project& p, int sceneIndex, int objectIndex,
               const Params& prm);
    // A BATCH in one scene (the Baked lighting tab, and --bake-prelit's shape).
    // The whole reason this exists: gibake::build + gibake::solve are the
    // expensive half and are per SCENE, so N objects cost one solve, not N.
    void start(const Project& p, int sceneIndex, std::vector<int> objects,
               const Params& prm);
    void cancel();
    bool running() const { return running_.load(); }
    float progress() const { return progress_.load(); }
    std::string status() const;
    // Which scene the run in flight (or the finished one) belongs to.
    int sceneIndex() const { return scene_.load(); }
    // The single object a one-object run was started for, -1 for a batch. It is
    // what tells the Properties panel a batch's results are not its to apply.
    int objectIndex() const { return object_.load(); }
    // Moves every finished result out - true once per run that produced any.
    bool take(std::vector<Done>& out);
    std::string error() const;

private:
    void run(Project p, int sceneIndex, std::vector<int> objects, Params prm);

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<int> scene_{-1}, object_{-1};
    mutable std::mutex mutex_;
    std::string status_, error_;
    std::vector<Done> done_;
};

}  // namespace litbake

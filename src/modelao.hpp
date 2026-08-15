#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "project.hpp"

// Automatic model ambient occlusion (docs/ambient-occlusion.md, "Model AO").
// Host-only, no GL - the aobake/matbake/litbake pattern.
//
// WHY THIS EXISTS. In a real game 99% of the objects are TEXTURED models, and
// for those every automatic mechanism in this engine does the least: the scene
// lightmap atlas refuses textured surfaces (it is additive, and an additive
// term over a texture blows out its dark texels), and baked GI reaches them
// only as flat per-vertex probe light. So an imported model has never had any
// self-occlusion at all - no darkening under an eave, in a doorway, between
// two boxes of the same mesh.
//
// The Material Editor could already bake exactly that, per material, by hand,
// every time (matbake). This runs the same bake AUTOMATICALLY, per model
// ASSET, and multiplies the result into the texture that model ships anyway.
//
// TWO PROPERTIES MAKE THAT AFFORDABLE, and both come from what is being baked:
//  - it is a model's OWN surface occlusion, so it is TRANSFORM-INVARIANT. Every
//    instance of the asset shares one map, wherever and however it is placed.
//  - the pixels ride in the model's existing texture, so it costs ZERO extra
//    GS VRAM (docs/gs-vram.md) - unlike the pre-lit route (docs/prelit-models.md),
//    which buys per-pixel SCENE light at one unique texture per object.
//
// The map itself is never shipped. It lives in .res-baked/modelao/ and is
// multiplied into the mirrored PNG at build (texbake) and into the uploaded
// pixels in the editor viewport - which is why the multiply and the strength
// remap live in ONE place here (applyToRgba) and are called from both.
namespace modelao {

// Project-wide bake knobs, resolved from ProjectSettings. Deliberately NOT part
// of the ambience-preset overlay: a preset changes what the light LOOKS like,
// this is bake quality - the GI-knobs precedent.
struct Params {
    bool enabled = false;
    // ao' = 1 - strength * (1 - ao). An APPLY-time remap, not part of the bake:
    // the cache stores raw occlusion, so dragging this slider re-multiplies
    // instead of re-baking (and the signature below leaves it out).
    float strength = 0.7f;
    int rays = 64;      // hemisphere rays per texel (matbake::Params::samples)
    float dist = 0.0f;  // occlusion reach in world units; 0 = 25% of the
                        // model's own bounding-box diagonal
};
Params paramsOf(const ProjectSettings& s);

// Project::modelAoMode values (per model asset path).
enum Mode { Follow = 0, ForceOn = 1, ForceOff = 2 };

// Does this model asset bake? The project default, overridden per asset.
bool resolveFor(const Project& p, const std::string& modelRel,
                const Params& prm);

// One (model asset, texture) pair the bake covers. A texture used by several
// materials of ONE model is a single target - they share the model's UV layout.
struct Target {
    std::string modelRel;    // "res/models/shed.obj"
    std::string textureRel;  // "res/models/shed.png", project-relative, normal
    int texW = 0, texH = 0;  // the source texture's own size (sets the map size)
};

// A (model, texture) pair deliberately left out, with the reason a panel or a
// build log shows. Naming them is the point: an AO map that silently does not
// exist is indistinguishable from a broken feature.
struct Skipped {
    std::string modelRel;
    std::string textureRel;
    std::string reason;
};

struct Plan {
    std::vector<Target> targets;
    std::vector<Skipped> skipped;
};

// Walks res/models/**.obj and works out what may be baked. Eligibility:
//  - static .obj only. Animated .glb/.fbx relight dynamically and their
//    textures go down the animBakedTextureRel path - skipped in v1.
//  - the material must have a texture; an untextured part already has the
//    scene lightmap route, which costs no texture at all.
//  - a texture referenced by MORE THAN ONE model asset is skipped: two UV
//    layouts over one image make a single multiply wrong for both.
//  - a pre-lit material (res/materials/*-lit.png, the litbake output) is
//    skipped: its gather already contains occlusion, and multiplying AO in
//    again would double-darken.
Plan plan(const Project& p, const Params& prm);

// The project-relative path a model's texture reference resolves to - the same
// resolution the plan uses, so a caller (litbake) can look its map up by it.
std::string textureRel(const Project& p, const std::string& modelRel,
                       const std::string& texRef);

// The plan narrowed to one model asset. litbake uses it to fold that model's
// AO into the albedo it reads, without planning the whole project.
Plan planFor(const Project& p, const std::string& modelRel, const Params& prm);

// Everything that can change what the bake produces: the .obj CONTENT, the
// content of every .mtl it resolves, the texture's DIMENSIONS (they set the
// map size), the ray/distance knobs and the module version.
//
// Never the texture's PIXELS - AO is a function of geometry and UVs alone, so
// repainting a texture must not throw the bake away. Never mtimes either, for
// the reason gibake gives: a checkout or a copy is not an edit.
uint64_t signature(const Project& p, const Target& t, const Params& prm);

std::string cacheDir(const Project& p);
// .res-baked/modelao/<pair>-<signature>.png - the signature is IN the name, so
// "is it fresh" is one fs::exists and a stale map is a file with another name.
std::string cachePath(const Project& p, const Target& t, const Params& prm);
bool fresh(const Project& p, const Target& t, const Params& prm);

// Bakes the target's AO map into the cache unless a fresh one is already
// there. Returns "" on success or why it failed. Synchronous - it is seconds
// at these resolutions, which is what lets texbake call it from a build.
std::string ensure(const Project& p, const Target& t, const Params& prm);

// THE MULTIPLY - the one formula, called by texbake (the shipped PNG) and by
// the editor viewport (the uploaded pixels), so the console and the preview
// cannot drift. The map is resampled to the image; ALPHA IS UNTOUCHED (StaPip
// discards alpha == 0 texels, so writing occlusion into alpha would delete a
// pass - docs/ambient-occlusion.md).
void applyToRgba(const unsigned char* ao, int aoW, int aoH,
                 unsigned char* rgba, int w, int h, float strength);
// Same, reading the map from a cache PNG. False when it cannot be decoded.
bool applyMapFile(const std::string& mapPath, unsigned char* rgba, int w, int h,
                  float strength);

// Ensures every eligible target's map, logging one line each (log may be
// empty). texbake, litbake and the editor's baker all go through it. Returns
// "texture rel path -> the AO map to multiply into it" for the targets whose
// bake succeeded - a target that failed is simply absent, so every consumer
// degrades to "no AO" rather than to a stale one.
std::map<std::string, std::string> ensureAll(
    const Project& p, const Params& prm, const Plan& pl,
    const std::function<void(const std::string&)>& log);

// --- the editor's background baker -------------------------------------------

// One row of the panel table: what the plan found, plus whether its map is on
// disk. Computed on the worker, because the plan parses every .obj.
struct Row {
    std::string modelRel;
    std::string textureRel;
    // "baked" / "stale" / "off" / a Skipped::reason
    std::string status;
    bool eligible = false;
    bool baked = false;
};
struct Report {
    std::vector<Row> rows;
    int baked = 0, pending = 0, skipped = 0;
};

// The litbake::Baker pattern: worker thread, polled from the UI. It takes a
// COPY of the Project and never touches the live one - all it produces is
// files in the cache plus a Report and the texture -> map table.
class Baker {
public:
    ~Baker() { cancel(); }
    void start(const Project& p, const Params& prm);
    void cancel();
    bool running() const { return running_.load(); }
    float progress() const { return progress_.load(); }
    std::string status() const;
    // Bumped when the run ends - the UI polls it to refresh the table and the
    // viewport re-uploads whatever changed.
    uint64_t version() const { return version_.load(); }
    Report report() const;
    std::map<std::string, std::string> maps() const;

private:
    void run(Project p, Params prm);

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<uint64_t> version_{0};
    mutable std::mutex mutex_;
    std::string status_;
    Report report_;
    std::map<std::string, std::string> maps_;
};

}  // namespace modelao

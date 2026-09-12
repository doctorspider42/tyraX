#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "project.hpp"

// Baked shadow decals (docs/shadows.md, "Baked (decal)") - the static
// directional shadow. For every object whose `shadowMode` is 4 the host traces
// the shadow that object throws along the scene's own sun, writes it into a
// small tile, packs the tiles into shared 256x256 atlas pages and projects
// them onto the receivers with decalproj. The console then draws ordinary
// static triangles through one blended pass: no silhouette render, no caster
// slot, nothing per frame.
//
// Host-only: no GL, no ImGui, no templates.cpp - the aobake / gibake /
// decalproj shape, so the whole thing runs from a 40-line harness.
//
// THE LOAD-BEARING DECISION IS THE ATLAS, and it is not about VRAM. A bag is
// one texture, so one shared page is what lets every shadow of a layer merge
// into ONE submit; unmerged they would cost ~1 ms of EE each (docs/prefabs.md)
// and the feature would top out at three or four hero objects. Folding a
// tile's rect into the UVs is free at run time as well - decalproj clips to
// the projector's unit cube, so its UVs are in [0,1] by construction and the
// rect is an affine remap here, not a runtime multiply like texatlas needs.
//
// Determinism, on the same terms as every other bake in this repo: a texel's
// sample spiral is rotated by a hash of ITS OWN coordinates, never by shared
// RNG state, so the bake is bit-identical at any core count. Check that with
// `cmp` on the cache file, never with an assertion.
namespace shadowbake {

// --- what a bake is asked to do ---------------------------------------------

struct Options {
    int tileRes = 64;          // 32 / 64 / 128; page is kPageSize
    float sunAngleDeg = 2.0f;  // angular diameter of the light -> penumbra
    float strength = 0.55f;    // how dark a fully occluded texel gets, 0..1
                               // (matches ProjectSettings; rides the tile's
                               // alpha, so 0 really means no shadow)
    float maxLength = 4.0f;    // shadow reach in caster heights; 0 = no limit
    int samples = 24;          // shadow rays per texel across the sun disk
};
Options optionsOf(const ProjectSettings& s);

// One page edge, in texels. 256 is the same ceiling the scene lightmaps sit
// at and for the same reason: the tiles ship as RGBA32 (the engine's
// palettized alpha path loses a gradient - texbake.cpp says where that was
// measured), so one page is 256 KB, i.e. 23% of the 32-bit GS texture heap
// (docs/gs-vram.md). A project that wants more shadows buys another page and
// is told what it costs, rather than silently getting a blurrier one.
constexpr int kPageSize = 256;

// Triangles one caster's projection may emit. decalproj's own cap is 4096,
// which at 60 bytes a triangle is 245 KB of ELF for ONE shadow - fine for an
// authored decal somebody placed by hand, far too generous for something a
// checkbox turns on. A caster that hits this is reported by name.
constexpr int kMaxTrisPerCaster = 512;

// Pages one scene may spend. Eight 256x256 pages is already twice the texture
// heap; the cap exists so a runaway scene reports instead of baking for
// minutes and shipping something that cannot be resident.
constexpr int kMaxPages = 8;

// --- the plan ---------------------------------------------------------------

// One caster that will be baked. `layer` is the caster's streaming layer and
// decides which merged bag the shadow lands in - so a shadow disappears with
// the object that throws it rather than outliving it.
struct Caster {
    int object = -1;  // index into SceneData::objects
    std::string name;
    std::string layer;
    // The projector, as a synthetic decal: position, Euler rotation and scale
    // in the project's own convention, so decalproj reads it exactly the way
    // it reads an authored one. Its local +Z points AT the sun.
    float position[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
    // The caster's own extent along the light, measured from the projector's
    // +Z face. The receiver search starts past it, which is what keeps a
    // caster from catching its own shadow.
    float casterDepth = 0.0f;
};

struct Refusal {
    std::string name;
    std::string why;
};

struct Plan {
    std::vector<Caster> casters;
    std::vector<Refusal> refused;  // asked for a baked shadow, cannot have one
    float sunDir[3] = {0, 1, 0};   // normalized, pointing AT the light
    std::string note;              // why the whole scene bakes nothing
    // Something the author should know while the bake still goes ahead. Today
    // that is exactly one thing: a running day/night cycle, which moves the sun
    // while these shadows stay where the baked hour put them.
    std::string warning;
    bool empty() const { return casters.empty(); }
};

// Who casts, from where, onto what. Every object that ASKED for a baked shadow
// and cannot have one lands in `refused` with a sentence saying why - a
// silently skipped caster is indistinguishable from a broken bake.
Plan plan(const Project& p, const SceneData& sc, const Options& opt);

// The refusals that can be answered from the OBJECT ALONE, with no geometry
// read and no scene: everything that can move. Empty = nothing obviously
// wrong. `plan` asks this first, so the Properties panel can say the same
// sentence the instant the combo moves rather than carrying a second copy of
// the rule that would drift from it.
std::string quickRefusal(const SceneObject& o);

// --- the bake ---------------------------------------------------------------

// One atlas page: kPageSize^2 alpha texels. The RGB the game samples is a
// single scene-wide tint (see Bake::tint) that texbake writes in, so the cache
// carries one byte per texel instead of four.
struct Page {
    std::vector<uint8_t> alpha;
};

// One merged draw: every shadow of one layer that landed on one page, as a
// single world-space triangle list (5 floats per vertex: pos3 + uv2, the
// decalproj layout) already carrying atlas UVs. This is the unit the game
// submits - one bag, whatever the shadow count.
struct Group {
    std::string layer;  // empty = always resident
    int page = 0;
    std::vector<float> verts;
    int casters = 0;
};

struct Bake {
    bool valid = false;
    uint64_t signature = 0;
    std::vector<Page> pages;
    std::vector<Group> groups;
    int tileRes = 64;
    // The tile's own colour, 0..255. NEAR BLACK, carrying only the sky's hue
    // at a low value - which is what makes the alpha-over blend an exact
    // per-pixel DARKENING (the scene lightmap's occlusion trick) rather than a
    // wash. A bright colour here replaces the receiver instead of shading it,
    // so every surface converges on one grey and the scene goes flat; that was
    // this field's first version and it looked exactly as bad as it sounds.
    // `strength` is not in here - it rides the tile's alpha, so 0 means no
    // shadow at all.
    uint8_t tint[3] = {0, 0, 0};
    // Reported, never silently applied: casters whose projection hit
    // kMaxTrisPerCaster, and casters that did not fit in kMaxPages.
    std::vector<Refusal> truncated;
    int triangles() const;
    // GS words the pages occupy, for the panel's budget line.
    int vramWords() const;
    // Bytes the merged meshes add to the ELF (60 per triangle).
    int elfBytes() const { return triangles() * 60; }
};

using ProgressFn = std::function<void(float)>;

// The whole bake for one scene.
//
// There is deliberately NO GPU backend here, and that is a measurement rather
// than an omission - `--bake-shadows` prints its own wall clock so the
// decision can be re-run. Unlike the GI gather, which fires `giRays`
// hemisphere rays per texel against the WHOLE scene tree, a shadow texel
// fires `samples` rays against ONE caster's own triangles: a box is twelve of
// them. The work is three orders of magnitude smaller, and a compute backend
// would add a second answer to "what does this caster occlude" plus a context
// to create, to save a fraction of a second (see docs/shadows.md, "What the
// bake costs").
Bake bakeScene(const Project& p, int sceneIndex, const std::atomic<bool>* cancel,
               const ProgressFn& progress);

// --- signature + cache ------------------------------------------------------

// Everything that can change what the bake produces: the sun at the baked
// hour, the options, every object's transform/type/shadow mode, the heightmap,
// and the CONTENT of every model file a caster or receiver reads. Content and
// never mtime - the gibake rule: a checkout must not throw a cache away.
uint64_t signature(const Project& p, const SceneData& sc, const Options& opt);

std::string cachePath(const Project& p, int sceneIndex);
bool write(const std::string& path, const Bake& b);
bool read(const std::string& path, Bake& b);

// Reads one scene's cache and checks it against the live model. valid == false
// means "absent or stale", never "empty" - a stale cache drops the scene back
// to no baked shadows at all, the way a stale GI cache drops it back to the
// pre-GI lighting.
Bake load(const Project& p, int sceneIndex);

// Re-bake every scene whose cache is absent or stale; leave the fresh ones
// alone and say so. Two callers, one loop: the pre-build step of `--build` and
// App::projectForBuild when bakedShadowAutoBake is on (the gibake::bakeStale
// arrangement). Refuses to do anything while `bakedShadows` is off.
struct StaleReport {
    int baked = 0, kept = 0, failed = 0;
};
StaleReport bakeStale(const Project& p,
                      const std::function<void(const std::string&)>& log);

// --- the asynchronous baker -------------------------------------------------

// gibake::Baker, verbatim in shape: worker thread, polled from the UI.
class Baker {
public:
    ~Baker() { cancel(); }
    // scenes: indices to bake; empty = every scene.
    void start(const Project& p, std::vector<int> scenes);
    void cancel();
    bool running() const { return running_.load(); }
    float progress() const { return progress_.load(); }
    std::string status() const;
    // Bumped once per finished scene and once when the run ends; the UI polls
    // it to refresh its staleness readout and the viewport preview.
    uint64_t version() const { return version_.load(); }

private:
    void run(Project p, std::vector<int> scenes);

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<uint64_t> version_{0};
    mutable std::mutex mutex_;
    std::string status_;
};

}  // namespace shadowbake

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aobake.hpp"
#include "bvh.hpp"
#include "project.hpp"

// Baked global illumination + light probes (docs/global-illumination.md).
// Host-only, no GL - the aobake/decalproj pattern, and like them it is a pure
// function of the model plus the files it reads, so a 40-line harness can
// exercise the whole thing.
//
// What it is, in one sentence: static geometry gets a baked multi-bounce
// lightmap, everything that moves gets its light from a probe grid.
//
// The pieces it does NOT own, because they already existed:
//   - the delivery path. A scene lightmap is one 256^2 RGBA32 image whose
//     ALPHA is occlusion (an alpha-over pass = an exact per-pixel multiply)
//     and whose RGB is light (an additive pass). GI does not add a channel or
//     a pass: it REPLACES what the RGB channel means. It used to be "baked
//     emissive light"; with GI on it is "incoming light, all sources, all
//     bounces". That is the whole reason this fits in the PS2's ~1.33 MB of
//     VRAM (gs-vram.md) - the image cannot grow.
//   - the atlas layout, the region packing, the bake cache, the texbake
//     plumbing and the codegen tables: aobake::bakeSceneLightAtlas and
//     aobake::terrainAOMap, called here with a light source override.
//   - the BVH: bvh.cpp, shared with matbake.
//
// The three things that are new: a scene-level BVH over REAL triangles (the
// analytic box/sphere occluders could only ever cast box-shaped shadows), an
// integrator that treats sky, sun, emissive materials and baked point lights
// as one hemisphere gather with bounces, and the probe grid.
namespace gibake {

// Quality knobs, resolved from ProjectSettings (project-wide - GI is not part
// of the ambience-preset overlay: a preset changes the LOOK of the light, the
// bake quality is a project decision).
struct Settings {
    bool enabled = false;
    int rays = 128;     // hemisphere rays per lightmap texel / per probe
    int bounces = 2;    // interreflection passes over the triangle set
    float skyLight = 1.0f;    // multiplier on the sky dome as a light source
    float sunLight = 1.0f;    // multiplier on the directional sun
    float ambientFloor = 0.03f;  // constant added to every gather - a sealed
                                 // room with no light source is pitch black
                                 // under real GI, which reads as "broken"
    float probeSpacing = 3.0f;   // world units between probes, XZ
    float probeHeight = 2.0f;    // ...and vertically
    int probeLevels = 4;         // vertical levels above the ground
    bool probes = true;
};
Settings settingsOf(const ProjectSettings& s);

// One scene as real triangles, with the material response and the light
// sources that act on them. Everything the integrator needs, and nothing that
// needs a Project.
struct Scene {
    bvh::Tree tree;
    std::vector<float> albedo;     // 3 per triangle, the colour a bounce picks up
    std::vector<float> emission;   // 3 per triangle, radiance the surface emits
    std::vector<float> radiosity;  // 3 per triangle, filled by solve()

    // Sky, as a hemisphere light. skyRadiance() is the host twin of the
    // generated dome build (templates.cpp skyAt/domeVert) and the viewport's
    // sky shader - the colour a ray that escapes the scene comes back with.
    bool skyDome = true;
    float skyHorizon[3] = {0, 0, 0};
    float skyTop[3] = {0, 0, 0};
    float skyExp = 1.0f;  // zenith-size bias, pow(elevation, skyExp)

    // Sun: the scene's directional light, already carrying brightness *
    // diffuse * lightColor, so a fully exposed surface facing it receives
    // exactly what the per-vertex path used to bake - only now it is shadowed.
    float sunDir[3] = {0, 1, 0};
    float sunColor[3] = {0, 0, 0};

    // Baked point lights (type 9, lightDynamic == 0). Dynamic ones are
    // dynamic on purpose and never enter a bake.
    struct PointLight {
        float pos[3];
        float color[3];
        float radius, bright;
    };
    std::vector<PointLight> lights;

    float ambientFloor = 0.0f;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
    // The terrain's world extent - the probe grid's XZ footprint.
    float terrainMinX = 0, terrainMaxX = 0, terrainMinZ = 0, terrainMaxZ = 0;
    float groundY = 0.0f;  // fallback ground for probes off the heightmap
    std::vector<float> heights;  // scene heightmap (for probe placement)
    int hmW = 0, hmD = 0;
    float hmWidth = 0, hmDepth = 0;
    // The ground as the BVH actually has it: a DECIMATED grid of (cells+1)^2
    // corner heights, split into triangles the way `build` splits them. The
    // bake hands out points on the fine bilinear surface the game walks on,
    // which sits above or below these triangles wherever the decimation cut a
    // bump - and a gather ray fired from under them starts inside the ground.
    // groundSurfaceY re-derives the traced height so an origin can be snapped
    // onto it. Empty when the scene has no terrain.
    std::vector<float> coarseH;
    int coarseCells = 0;

    bool empty() const { return tree.empty(); }
};

// Tessellates the scene: primitives through primmesh (the same source the
// viewport draws from), static .obj models through objparser, the terrain as
// a heightfield. Animated models, markers, decals, mirrors and portals
// contribute nothing, and an object with "Cast shadow" off is left out
// entirely - that switch already means "light passes through me".
Scene build(const Project& p, const SceneData& sc, const Settings& st);

// The colour a ray that escapes the scene comes back with.
void skyRadiance(const Scene& s, const float dir[3], float out[3]);

// One hemisphere gather at a surface point: sky through whatever openings
// exist, the sun (one shadow ray), the baked point lights (one shadow ray
// each), and the emission + solved bounce radiosity of every surface the rays
// land on. Deterministic - the sample spiral is rotated by a hash of `seed`,
// never by a shared RNG, so the same bake twice is the same bytes twice at
// any core count.
// Height of the ground as the BVH holds it (see Scene::coarseH), -1e30f off
// the terrain. Snap a ground-bake origin onto this, not onto the fine
// bilinear surface, or the ray starts inside the mesh it is meant to leave.
float groundSurfaceY(const Scene& s, float x, float z);

void gather(const Scene& s, const float wp[3], const float n[3], uint32_t seed,
            int rays, float out[3]);

using ProgressFn = std::function<void(float)>;

// Interreflection: iterate the gather over the triangle set, each pass
// feeding the previous pass's radiosity back in. This is the milestone the
// whole thing is for - it is what makes a red wall tint the floor beside it.
void solve(Scene& s, const Settings& st, const std::atomic<bool>* cancel,
           const ProgressFn& progress);

// --- probes -----------------------------------------------------------------

// L1 spherical harmonics, 4 coefficients x 3 channels. L2 is not worth 9
// coefficients at this grid density. Reconstruction (the runtime twin lives in
// the generated game and the viewport shader):
//
//     shade(n) = L0 + (2/3) * dot(L1, n)
//
// which is the exact clamped-cosine convolution for an L1 environment: a
// uniform environment of radiance L gives shade = L for every normal, and a
// bright upper hemisphere gives 1 looking up and 0 looking down.
struct ProbeGrid {
    float origin[3] = {0, 0, 0};
    float step[3] = {0, 0, 0};
    int dim[3] = {0, 0, 0};
    float scale = 1.0f;         // decode: coefficient = byte / 127 * scale
    std::vector<int8_t> sh;     // 12 per probe: L0.rgb, L1x.rgb, L1y, L1z
    std::vector<uint8_t> live;  // 0 = the probe sits inside solid geometry
    bool empty() const { return dim[0] <= 0 || sh.empty(); }
    int count() const { return dim[0] * dim[1] * dim[2]; }
};

ProbeGrid bakeProbes(const Scene& s, const Settings& st,
                     const std::atomic<bool>* cancel,
                     const ProgressFn& progress);

// Host reference of the runtime probe lookup: weighted trilinear over the 8
// surrounding probes (dead ones weigh nothing), then the L1 evaluation above.
// Twin of giProbeAt in the generated game and giProbe() in the viewport
// fragment shader - change one, change all three.
void sampleProbes(const ProbeGrid& g, const float wp[3], const float n[3],
                  float out[3]);

// --- the cached bake --------------------------------------------------------

// Everything one scene's GI bake produces. Cached wholesale in
// .res-baked/gi/scene<N>.gi: bake time is a feature, not part of the build,
// so codegen and texbake READ this file and a stale or missing one simply
// means the scene falls back to the pre-GI emissive-only bake.
struct Bake {
    bool valid = false;
    uint64_t signature = 0;
    aobake::SceneLightAtlas atlas;
    aobake::AoImage terrain;
    ProbeGrid probes;
};

// Everything that can change what the bake produces, hashed: object
// transforms/types/colours/materials, the material FILES (size + mtime), the
// heightmap, the resolved lighting/sky/AO settings and the GI quality knobs.
uint64_t signature(const Project& p, const SceneData& sc, const Settings& st);

std::string cachePath(const Project& p, int sceneIndex);
bool write(const std::string& path, const Bake& b);
bool read(const std::string& path, Bake& b);

// Reads the cache for one scene and checks its signature against the live
// model. valid == false means "absent or stale" - never "empty".
Bake load(const Project& p, int sceneIndex);

// The whole bake for one scene: tessellate -> bounce -> lightmap atlas ->
// terrain map -> probes.
Bake bakeScene(const Project& p, int sceneIndex,
               const std::atomic<bool>* cancel, const ProgressFn& progress);

// The managed bake, synchronous: every scene whose cache is absent or STALE is
// re-baked and written, the fresh ones are left alone and said so. Nothing
// else - a scene whose cache still matches costs one signature pass. Two
// callers, one loop: the pre-build step of `--build` and of
// App::projectForBuild when ProjectSettings::giAutoBake is on (the litbake
// bakeStale arrangement). It does not touch the Project - the cache lives on
// disk - so the caller only has to re-read it (the viewport, refreshGenerated).
// `log` receives one line per scene (`baked GI ...` / `fresh ...` / `error
// ...`) plus a summary; may be null. Refuses to do anything while giEnabled is
// off, which is what "GI never bakes silently" means: only a project that
// asked for both switches gets it.
struct StaleReport {
    int baked = 0, kept = 0, failed = 0;
};
StaleReport bakeStale(const Project& p,
                      const std::function<void(const std::string&)>& log);

// Progressive asynchronous baker over a set of scenes - the matbake::Baker
// pattern (worker thread, polled from the UI). start() on a running bake
// cancels it first.
class Baker {
public:
    ~Baker() { cancel(); }
    // scenes: indices to bake; empty = every scene in the project.
    void start(const Project& p, std::vector<int> scenes);
    void cancel();
    bool running() const { return running_.load(); }
    float progress() const { return progress_.load(); }
    std::string status() const;
    // Bumped once per finished scene and once when the run ends - the UI
    // polls it to refresh its staleness readout.
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

}  // namespace gibake

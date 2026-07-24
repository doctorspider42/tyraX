#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "objparser.hpp"

// matbake: UV-space raytraced map baker for the Material Editor
// (docs/material-baking.md). Host-only, no GL - the decalproj pattern.
//
// The bake rasterizes the preview mesh's triangles in UV space (conservative:
// a texel touched by any part of a triangle gets a sample), interpolating 3D
// position + normal per texel, then fires cosine-weighted hemisphere rays at
// every covered texel through a SAH BVH. One pass yields the whole map set:
// ambient occlusion, bent normals, thickness, curvature (geometric, not
// raytraced), position and object-space normals. With a high-poly mesh the
// texel points are first projected onto it along the smoothed low-poly
// normals (cage projection), so the maps carry the dense geometry's detail.
//
// Everything is deterministic: a fixed golden-angle sample spiral rotated
// per texel by a seeded hash, threads partitioned by texel ranges - the same
// inputs produce bit-identical maps regardless of core count or progressive
// round boundaries.
namespace matbake {

// Triangle soup in model space, 8 floats per corner (pos3 + nrm3 + uv2),
// 3 corners per triangle. Every triangle occludes; paintTri marks the ones
// whose UVs land on the baked texture (empty = all). posIdx (one entry per
// corner, objparser semantics) welds corners for smooth normals + curvature;
// empty = weld by quantized position (primitives).
struct MeshInput {
    std::vector<float> verts;
    std::vector<int> posIdx;
    std::vector<char> paintTri;
    // Caller-provided identity of (geometry + UV set): the Baker reuses the
    // rasterized geometry buffer + BVH across starts while it matches.
    // 0 = never cache.
    uint64_t signature = 0;
    int triCount() const { return (int)(verts.size() / 24); }
    bool empty() const { return verts.empty(); }
};

// Model triangles for the bake: parts whose usemtl matches entryName are
// paintable (empty entryName = all), every part occludes.
MeshInput fromModel(const objparser::Model& m, const std::string& entryName);
// Unit primitive (0 box, 1 sphere, 2 cylinder, 3 cone) at the given detail.
MeshInput fromPrimitive(int shape, int detail);

// --- UV validation -------------------------------------------------------

// One finding of validateUv, anchored to the offending triangle(s) so the
// UI can highlight them in the UV panel and on the mesh.
struct UvIssue {
    enum class Kind {
        Overlap,      // tri and otherTri claim the same texels (mod 1 -
                      // painting one paints the other)
        OutOfRange,   // UVs leave 0..1 (wraps on the GS - intentional
                      // tiling or a stray island)
        Flipped,      // minority UV winding (mirrored island - brush
                      // strokes and text appear mirrored)
        Degenerate,   // zero UV area over real surface (bakes/paints as a
                      // single smeared texel)
        DensityLow,   // far fewer texels per world unit than the average
        DensityHigh,  // far more
    };
    Kind kind;
    int tri = -1;
    int otherTri = -1;  // Overlap only
    float value = 0.0f; // Overlap: shared texels; Density: ratio to average
};

// Inspects the paintable triangles' UV mapping at the given texture size.
// Deterministic; capped at a few hundred findings (worst first is not
// attempted - triangle order is source order).
std::vector<UvIssue> validateUv(const MeshInput& m, int texSize);

// A procedural mask driven by the baked map set: "wear on edges", "dirt in
// cavities", altitude streaks, world-space noise... The value per texel is
// a 0..1 source signal remapped through a smoothstep window, optionally
// inverted and broken up by Perlin noise. Noise sources sample 3D noise at
// the texel's baked POSITION (AABB-normalized), so patterns continue across
// UV island seams instead of restarting at them. (generateMask below, after
// the Maps type it consumes.)
struct MaskParams {
    enum class Source {
        Edges = 0,     // convex curvature - wear/scratch highlights
        Cavities,      // concave curvature - grime lines
        Occlusion,     // 1 - AO: dirt where light can't reach
        Thinness,      // 1 - thickness: rims and thin fins
        Height,        // baked position Y, bottom 0 .. top 1
        FacingUp,      // upward-facing normals - dust/snow catch
        Perlin,        // 3D gradient noise at the baked position
        Worley,        // 3D cellular noise (F1) at the baked position
        Bricks,        // UV-space brick pattern (1 = mortar lines)
    };
    Source source = Source::Edges;
    float rangeLo = 0.35f;   // smoothstep window over the source signal
    float rangeHi = 0.75f;
    bool invert = false;
    float scale = 4.0f;      // noise/brick frequency (per AABB cube / per UV)
    uint32_t seed = 1;
    float breakupAmount = 0.0f;  // 0..1: multiply by Perlin for organic wear
    float breakupScale = 6.0f;
    float mortar = 0.06f;    // Bricks: mortar line width, fraction of a brick
};

struct Params {
    int size = 256;        // output width = height, clamped to pow2 32..1024
    int samples = 64;      // hemisphere rays per texel at full quality
    float maxDist = 0.0f;  // occlusion reach, world units (<= 0 = auto: half
                           // the occluder AABB diagonal)
    int supersample = 2;   // subsample grid per texel axis (1/2/4)
    bool backface = true;  // rays hitting triangle back sides count as
                           // occluders (thin unwelded geometry needs this)
    int padding = 4;       // dilate ring in texels (bilinear/mip seam guard)
    uint32_t seed = 1;     // per-texel rotation hash seed
    float cageOffset = 0.0f;  // high-poly search distance along the smoothed
                              // low normal (<= 0 = auto: 2% of the diagonal)
};

// The bake output. All maps are size*size; texels outside the dilated
// islands hold the neutral value (AO/thickness white, bent/normal flat +Z,
// curvature mid-gray, position black).
struct Maps {
    int w = 0, h = 0;
    std::vector<uint8_t> mask;       // 255 = island texel (pre-dilate)
    std::vector<uint8_t> ao;         // gray, white = fully open
    std::vector<uint8_t> bent;       // rgb, object-space bent normal
    std::vector<uint8_t> thickness;  // gray, white = thick / open below
    std::vector<uint8_t> curvature;  // gray, 128 flat, >128 convex edges
    std::vector<uint8_t> position;   // rgb, AABB-normalized position
    std::vector<uint8_t> normal;     // rgb, object-space normal
    int samplesDone = 0;             // rays per texel accumulated so far
    bool empty() const { return w == 0; }
};

// Renders a MaskParams mask at w x h from the bake's map set (position/
// normal/curvature/ao/thickness resampled bilinearly). Returns w*h bytes,
// 255 = full mask. Purely a function of (maps, p, w, h) - deterministic.
std::vector<uint8_t> generateMask(const Maps& maps, const MaskParams& p,
                                  int w, int h);

// Progressive asynchronous baker. start() kicks a worker thread that
// prepares the geometry buffer + BVH (cached across starts while the mesh
// signature and raster params hold), then accumulates rays in growing
// rounds, publishing a full map snapshot after each - the UI polls
// version()/snapshot() and shows occlusion within the first round (a few
// rays per texel), refining to `samples` in place. start() on a running
// bake cancels it first; sampling-only param changes restart cheaply.
class Baker {
public:
    ~Baker() { cancel(); }
    // high may be empty (bake against the low mesh itself).
    void start(MeshInput mesh, MeshInput high, const Params& p);
    void cancel();  // stop the worker and join (fast - tiles poll the flag)
    bool running() const { return running_.load(); }
    float progress() const;      // 0..1 across the sampling schedule
    uint64_t version() const { return version_.load(); }
    Maps snapshot() const;       // copy of the latest published maps
    std::string error() const;   // non-empty = the last bake failed (why)

private:
    struct Prepared;  // gbuffer + BVH cache (matbake.cpp)
    void run(MeshInput mesh, MeshInput high, Params p);

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> version_{0};
    std::atomic<int> samplesDone_{0};
    std::atomic<int> samplesTarget_{1};
    mutable std::mutex mapsMutex_;
    Maps maps_;
    std::string error_;
    std::shared_ptr<Prepared> cache_;  // survives across start() calls
};

}  // namespace matbake

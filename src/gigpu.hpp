#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "gibake.hpp"

// GPU hemisphere gather for the GI bake (docs/global-illumination.md).
//
// This is an ACCELERATOR WITH A REFERENCE, never a replacement, and the
// distinction is structural rather than cautious:
//
//  - `--bake-gi` runs headless on build servers and inside the Docker build,
//    where there is no display and no GL context at all. The CPU integrator in
//    gibake.cpp therefore cannot be deleted, and every path must survive
//    `available()` answering false.
//  - the kernel is a TWIN of gibake::gather / directAt / skyRadiance and
//    bvh::trace - the fourth twin in a codebase that already tracks host / EE /
//    viewport-shader triplets. It is the only one with an AUTOMATIC ORACLE: a
//    bake is a pure function, so the two answers can be diffed by machine
//    (`--gi-gpu-check`) instead of judged by eye. Keep it that way; a change to
//    the C++ gather that is not mirrored here is caught only by running that.
//
// What it deliberately does NOT promise is bit-identity. Every other bake in
// this repo is bit-identical at any core count and that property is load
// bearing, but it holds because one implementation runs everywhere. A GPU has
// its own transcendental units, its own FMA contraction and its own rounding,
// so `sin` and `pow` here are simply not the host's - and no arrangement of
// this code changes that. So:
//
//    a GPU bake and a CPU bake are compared with a TOLERANCE, never with cmp,
//    and a cache must record which one produced it.
//
// The unit of work is a BATCH. A per-point entry point cannot fill a GPU - the
// dispatch latency alone exceeds the CPU cost of one gather - which is why
// aobake grew a batched light seam beside its per-point LightFn.
namespace gigpu {

// True when this process can create a GL 4.3 core context. Cheap after the
// first call (the answer is cached). `why` receives a one-line reason when it
// is false, for the log - "no GPU" and "GPU refused 4.3" are different problems
// and a build server should be able to tell them apart.
// MUST BE FIRST CALLED FROM THE MAIN THREAD. It creates the context, and GLFW
// only allows window creation there - gibake::Baker runs its bake on a worker,
// so the caller has to prime this before starting one (Baker::start does). The
// context itself is then made current on whichever thread gathers, which GLFW
// does allow; a context may simply not be current in two places at once, and
// nothing else in the editor touches this one.
bool available(std::string* why = nullptr);

// One solved scene, resident on the GPU. Build it once per bake and reuse it
// for every batch - the upload is the expensive part.
class Gather {
public:
    Gather();
    ~Gather();
    Gather(const Gather&) = delete;
    Gather& operator=(const Gather&) = delete;

    // Uploads the BVH, the triangle radiance (emission + solved radiosity,
    // summed here because the kernel only ever reads the sum) and the light
    // sources. Call AFTER gibake::solve, or the bounce is uploaded as zero.
    // false = unavailable or failed; `err` says which.
    bool upload(const gibake::Scene& s, std::string* err = nullptr);

    // `count` surface points -> `count` RGB triples, in the units
    // gibake::gather writes. wp/n are 3 floats each per point; seed is the
    // per-point identity whose hash rotates the sample spiral, exactly as on
    // the host. Safe to call repeatedly; the batch is chunked internally.
    bool run(const float* wp, const float* n, const uint32_t* seed, int count,
             int rays, float* outRgb, std::string* err = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// --- the oracle --------------------------------------------------------------

// How far the GPU answer sits from the CPU one over `count` sample points
// spread across the scene's own surfaces. This is the check that makes the twin
// maintainable, so it reports the SHAPE of the error and not just a number:
// `maxAbs` catches a kernel that is wrong somewhere specific (a missed light, a
// sign flip), while `meanAbs` staying near zero with a large `maxAbs` is the
// signature of a divergent-ray disagreement rather than a broken formula.
struct Compare {
    bool ran = false;
    int points = 0;
    double meanAbs = 0.0;   // mean |gpu - cpu| over every channel
    double maxAbs = 0.0;    // worst single channel
    double meanRef = 0.0;   // mean |cpu|, so maxAbs can be read as a fraction
    double cpuSeconds = 0.0;
    double gpuSeconds = 0.0;
    std::string note;       // why it did not run, when ran == false
};

// Builds the sample set from the scene's own triangles (deterministic: one
// point per triangle centroid, walked in index order), gathers it both ways and
// reports. `rays` is the per-point ray count - the project's own setting is
// what a real bake uses, so pass that rather than a round number.
Compare compare(const gibake::Scene& s, int rays, int maxPoints);

}  // namespace gigpu

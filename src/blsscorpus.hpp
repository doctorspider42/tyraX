// BLSS training corpus (docs/neural-upscaler.md). A self-contained software
// rasteriser that manufactures exactly what the console will have in VRAM, plus
// the ground truth the console can never have.
//
// Why a rasteriser and not screenshots: the network is trained on GEOMETRIC
// features (depth, motion, silhouette, coverage), so the trainer has to know
// the scene, not just the picture. Rendering it here means every feature is
// exact and comes out of the same buildFeatures() the runtime calls - there is
// no "training-time approximation of a runtime feature" to drift.
//
// Host-only: no GL, no ImGui (the aobake/dronegen pattern), so --blss-train
// runs headless in CI. Deterministic by seed.
//
// TWO SCENE SOURCES, ONE RASTERISER. `CorpusConfig::projectDir` swaps the
// bestiary below for the USER'S OWN PROJECT (blssscene.{hpp,cpp} walks it), and
// that is the whole extent of the difference: the rasteriser, the jitter, the
// supersampled ground truth, bagOf(), accumulate() and buildFeatures() are the
// same code either way, so a project-trained net and a procedurally-trained one
// are comparable by construction. A project that will not load, or loads with
// nothing to draw, falls back to the bestiary rather than producing no corpus.
//
// The procedural corpus is procedural on purpose. It is a bestiary of the cases that
// actually alias on a PS2 - a checkerboard floor running to the horizon, thin
// poles, cutout foliage, a curved silhouette, high-frequency textures at
// grazing angles - shot with camera moves (still, pan, orbit, dolly) so the
// temporal channel sees both easy and hopeless reprojection.
//
// THIRTEEN shots, and the second six are there because a measurement asked for
// them. `--blss-eval --cv` (leave-one-shot-out) showed the network scoring
// +0.31 dB over bilinear when it trains on six shots and +0.10 when it trains on
// five - one shot of training data was worth 0.2 dB and most of the run-to-run
// spread the docs had been blaming on the seed. The binding constraint was the
// corpus, not the objective or the topology, so buildShots() gained the content
// and camera classes the first seven had no representative of. See the comment
// above shot 7 in blsscorpus.cpp; shots 0..6 are untouched and render
// bit-identically, which is what lets a fold table be compared across the change.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "blss.hpp"

namespace blss {

struct CorpusConfig {
    int outW = 512;               // output (display) resolution
    int outH = 448;
    Scale scale = Scale::X2Y2;    // so the low-res render is outW/2 x outH/2
    int frames = 48;              // rendered frames; each yields cols*rows samples
    int supersample = 4;          // ground truth is rendered at 4x and box-resolved
    uint32_t seed = 0xB1557u;
    std::string assetDir;         // examples/ tree scanned for real PNG materials
    bool verbose = true;
    // Non-empty: render the PROJECT in this directory instead of the bestiary
    // (`--blss-train <projectDir>`). Everything else in this struct still
    // applies - the resolution, the jitter, the supersample, the frame budget.
    std::string projectDir;
    // ONE PROXY PER PACKAGE, the way the engine submits them. StaPipCore hands
    // BLSS one bounding box per run of maxVertCount/3 consecutive vertices
    // (StaPipBagPackagesBBox), capped at 32 per bag, because a bag carries one
    // bbox and one w range and a whole mesh cannot describe itself with those.
    // The corpus does the same to every object it draws.
    //
    // OFF reproduces the pre-split behaviour - one proxy per object, which is
    // what every fold table published before this existed was measured on, and
    // the bestiary's hand-chunked floors and walls were its stand-in for. Kept
    // as `--no-package-split` so those tables stay reproducible.
    bool packageSplit = true;
    // HOW MANY THREADS RENDER THE FRAMES. 0 = every core the machine has.
    //
    // It changes the WALL CLOCK AND NOTHING ELSE, and that is a requirement
    // rather than a hope: generate() computes frame i from i alone (its camera,
    // its jitter phase, and its predecessor's camera - never from a buffer the
    // previous iteration happened to leave behind), so the corpus is
    // bit-identical at 1 thread and at 32. Every number this feature has
    // published came off a seeded run; a corpus that depended on the core count
    // would have silently unmade all of them. `--threads 1` is how that is
    // checked - see the determinism note above generate() in blsscorpus.cpp.
    //
    // Each worker owns ~30 MB of raster scratch at the shipped 4x supersample
    // (a 2048x1792 colour target and its z-buffer), so this is also the knob for
    // a machine with many cores and little memory.
    int threads = 0;
};

// One training/eval item: what the console would hold, and what it should have
// produced. `frame` is fully populated - low, prevLow, phase, reproj, features,
// cols/rows/outW/outH - so composite() and oracle() can be called directly.
struct CorpusFrame {
    Frame frame;
    Image truth;      // outW x outH, box-resolved from the supersampled render
    Image native;     // outW x outH rendered at 1 sample - the "no BLSS" baseline
    int shot = 0;     // which camera move this frame belongs to (held-out split)
    // What that shot IS. Carried per frame rather than looked up, because the
    // consumer that needs it most - leave-one-shot-out cross-validation - reports
    // one row per shot and "fold 4 lost 0.3 dB" is useless without "fold 4 is the
    // grazing wall". OWNED, not a literal: a project's shots are named from its
    // own scene names at run time, and a pointer into the shot table would
    // dangle the moment generate() returned.
    std::string shotName;
    std::string moveName;
};

// Renders the whole corpus, one worker per frame (CorpusConfig::threads). Frame
// N's `prevLow` is frame N-1 of the same shot (the first frame of a shot reuses
// its own render, which is what the console does on a scene cut), and the
// jitter phase alternates per frame.
//
// That predecessor is RE-RENDERED from its own camera and phase rather than
// carried over from the previous iteration, which is what makes frame N a pure
// function of N and the loop parallel at all - see the note above generate() in
// the .cpp. The frames come back in the same order a serial run produced them,
// so a corpus index still means what every held-out split assumes it means.
std::vector<CorpusFrame> generate(const CorpusConfig&);

}  // namespace blss

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
// Host-only: no GL, no ImGui, no Project (the aobake/dronegen pattern), so
// --blss-train runs headless in CI. Deterministic by seed.
//
// The corpus is procedural on purpose. It is a bestiary of the cases that
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
    // grazing wall". Static string literals owned by the shot table.
    const char* shotName = "";
    const char* moveName = "";
};

// Renders the whole corpus. Frame N's `prevLow` is frame N-1 of the same shot
// (the first frame of a shot reuses its own render, which is what the console
// does on a scene cut), and the jitter phase alternates per frame.
std::vector<CorpusFrame> generate(const CorpusConfig&);

}  // namespace blss

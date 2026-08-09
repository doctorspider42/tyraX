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

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "blss.hpp"

namespace blss {

struct CorpusConfig {
    int outW = 512;               // output (display) resolution
    int outH = 448;
    // The raster scale, arbitrary since `--scale WxH` (blss.hpp, struct Scale):
    // the low-res render is outW/scale.x x outH/scale.y and everything below -
    // the jitter, the reprojection field, the composite's sampling - reads it
    // from here rather than assuming 2. It must DIVIDE outW/outH; generate()
    // says so out loud when it does not.
    Scale scale = Scale::X2Y2;
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
    // ANIMATED MODELS ARE PART OF THE FRAME. On the console a skinned mesh is
    // submitted through StaPipCore like any other static bag, so it is drawn
    // AND it describes tiles; this corpus used to do neither. `--no-anim`
    // restores the old behaviour so a fold table measured before the change is
    // still reproducible - it is not a shipping configuration.
    bool animated = true;
    // SUB-PIXEL JITTER: -1 follow the project's own `blssJitter` (and default ON
    // for the bestiary, which has no project to ask), 0 force off, 1 force on.
    //
    // It is here rather than assumed because jitter stopped being a constant. On
    // real hardware a frozen camera leaves 30.8% of the picture alternating
    // between two images with jitter on and 0.03/255 - the noise floor - with it
    // off, so a shipping project may well have it off; and a net fitted against
    // the jittered sampler then runs on frames the console never draws.
    // generate() resolves this into blss::setJitter() before the first frame.
    int jitter = -1;
    // THE STILL FIXTURE - the host twin of the console's frozen-camera
    // experiment, and a MEASUREMENT configuration rather than a corpus.
    //
    // The bob was found on hardware by freezing the camera and differencing
    // consecutive fields: with one pose, the only thing left between frame N and
    // frame N-1 is the jitter phase, so whatever alternates IS the artefact.
    // period2Alternation() could not be given that. Every corpus frame comes
    // from its own camera, so the metric has to motion-compensate two
    // predecessors into the current view, and it carries the warp's own
    // resampling error plus anything the warp cannot describe - which on a
    // project with animated models is the models, since the reprojection field
    // is camera-derived and no part of it knows a mesh deformed. Measured, that
    // pushed the metric's own floor from 0.075 to 2.614 levels, i.e. 35x the
    // artefact it is meant to resolve, and `--no-anim` was the only way to get a
    // reading at all.
    //
    // With this on, every frame of a shot uses the shot's FIRST camera and its
    // FIRST pose, and only the jitter phase advances. The warp becomes the
    // identity, the animation contributes nothing to compensate, and the metric
    // measures the alternation and nothing else - on the animated corpus, which
    // is the one the network is actually trained on.
    //
    // It is NOT a training corpus and must never be used as one: a shot's frames
    // are all the same frame, so the fit would see `shots` distinct examples
    // repeated. generate() says so out loud.
    bool still = false;
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

// ------------------------------------------------------------- coverage ------

// HOW MUCH FILL THE SCENE ASKS THE GS FOR, which is the only input the measured
// speed model has (docs/profiling.md, "The EE floor"). BLSS trades GS fill for
// EE work at a fixed price - 5.02 ms of EE against 74.1 % of the fill - so
// "will this get faster" is entirely a question about overdraw, and overdraw is
// a counter in a rasteriser this file already owns.
//
// It is DELIBERATELY NOT the corpus. generate() renders the ground truth at 4x
// supersample, labels every frame with the oracle and builds features; none of
// that says anything about fill, and all of it is minutes. This walks the same
// scenes with the same cameras and counts fragments, which is seconds - so the
// window can answer the speed question without a training run, exactly as the
// net-free `--blss-eval` answers the quality one without a network.
//
// WHAT IT COUNTS: every fragment the rasteriser produces inside the screen,
// z-test or not. That is what the GS pays for - it has no early-z and no
// backface culling (Vec4::shouldBeBackfaceCulled exists in the engine and is
// called by nothing), so a hidden wall and the far side of a closed box are
// both real fill. Divided by the output's pixel count, that IS "full-screen
// coverages", the unit the break-even is stated in.
struct CoverageConfig {
    std::string projectDir;   // required: this only answers about a project
    int outW = 512, outH = 448;  // the display raster the coverages are per
    // WIDTH THE COUNTER ACTUALLY RASTERISES AT. Coverage is a ratio, so it is
    // very nearly invariant to this, and a quarter-linear raster is a sixteenth
    // of the fragment loop - which is what makes a 3 000-sprite haze bank
    // countable in a second rather than a minute. Sub-pixel triangles are lost
    // at any resolution and are not where overdraw comes from.
    int raster = 256;
    int framesPerShot = 6;
    int threads = 0;          // 0 = every core
    bool verbose = false;
};

// One camera move's worth. `emit` is the estimated emitter half, kept separate
// from `geom` all the way to the UI because one is counted and the other is
// modelled, and a reader is entitled to know which is which.
struct CoverageShot {
    std::string scene, name, move;
    double geom = 0, emit = 0;  // mean full-screen coverages over the shot
    double peak = 0;            // the worst single frame (geom + emit)
    int frames = 0;
};

struct CoverageReport {
    bool ok = false;
    std::string err;
    int scenes = 0, frames = 0;
    size_t triangles = 0;
    // Means and 95th percentiles over every frame of every shot. `mean` and
    // `p95` are the sum of the two halves; the halves are carried because the
    // honest sentence about them is different.
    double geomMean = 0, emitMean = 0, mean = 0, p95 = 0;
    int emitters = 0, billboards = 0;
    // What the count could NOT see, so the UI can say so instead of quietly
    // under-reporting. `cutout` matters because an alpha-tested texel is
    // rasterised and then thrown away - counted here, not drawn there.
    bool sawAnimated = false, sawCutout = false, sawDisabledEmitter = false;
    std::vector<CoverageShot> shots;
};

// Walks `projectDir` (blssscene::loadProject - the same walk the corpus uses,
// so the two cannot disagree about what the scene contains) and counts. Returns
// `ok == false` with `err` set when the directory is not a loadable project.
// `cancel`, when set, is polled between frames - an atomic because the caller
// is a UI thread abandoning a worker (App::closeProject).
CoverageReport measureCoverage(const CoverageConfig&, const std::atomic<bool>* cancel = nullptr);

}  // namespace blss

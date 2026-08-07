// BLSS - "Bieda-Level Super Sampling", the neural upscaler
// (docs/neural-upscaler.md). Host side: the feature vector, the network, the
// exact composite the GS will execute, the oracle that says what the network
// SHOULD have answered, and the trainer that fits one to the other.
//
// The split to keep in mind: this file owns the MATH, and the engine
// (vendor/tyra/.../renderer/core/blss) owns the GS packets. They are twins -
// composite() here reproduces, in 8-bit integer arithmetic with the GS's own
// (A-B)*C>>7 truncation, exactly what RendererCoreBlss draws. That is the
// point: the oracle optimises the real hardware formula, so the weights the
// network learns are the weights the console wants. If you change one, change
// both, and re-run --blss-eval - the PSNR table is the regression test.
//
// Nothing here touches GL, ImGui or a Project. --blss-train / --blss-eval /
// --blss-emit run headless (the aobake/navmesh pattern), so the network can be
// retrained in CI without a display.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace blss {

struct ReprojField;  // defined below, referenced by the feature builders

// ---------------------------------------------------------------- geometry ---

// Output-space edge of one decision tile, in pixels. 32 gives 16x14 tiles at
// 512x448: coarse enough that the per-tile MLP is free (224 evaluations) and
// the Gouraud weight field is smooth over 32 px, fine enough to separate a
// foliage silhouette from the sky next to it.
constexpr int kTile = 32;

// The upscale factors the runtime supports. Scale2x2 quarters the 3D fill;
// Scale1x2 halves only the height, which keeps horizontal detail (and matches
// what the PS2's own InterlacedField mode already does to the raster).
enum class Scale { X2Y2, X1Y2 };

inline int scaleX(Scale s) { return s == Scale::X2Y2 ? 2 : 1; }
inline int scaleY(Scale) { return 2; }

// The two sub-pixel jitter phases, in LOW-RES pixels. For a 2x2 upscale these
// are exactly two of the four output-pixel centres inside one low-res pixel, so
// current + previous frame carry two DISTINCT output samples - a real quincunx
// pair, not an approximation. Quantised to sixteenths because that is what the
// GS XYOFFSET register stores (12.4 fixed point), so the console can reproduce
// these offsets bit-exactly: -4/16 and +4/16.
constexpr int kJitterPhases = 2;
inline float jitterX(int phase) { return phase == 0 ? -0.25f : 0.25f; }
inline float jitterY(int phase) { return phase == 0 ? -0.25f : 0.25f; }

// ---------------------------------------------------------------- features ---

constexpr int kFeatures = 8;
constexpr int kHidden = 12;
constexpr int kOutputs = 3;  // wA (point), wC (temporal), wD (sharpen)

// Per-tile network input. Every channel is normalised to roughly 0..1 by the
// producer, because the net is small enough that input scaling is the
// difference between converging and not. Order is load-bearing: it is the
// order the generated PS2 code fills the array in, and the order
// kFeatureNames documents.
struct Features {
    float v[kFeatures] = {0, 0, 0, 0, 0, 0, 0, 0};

    // Named accessors - use these, not v[3], everywhere outside the emitter.
    float& motion() { return v[0]; }     // reprojection length / tile edge
    float& depth() { return v[1]; }      // representative 1/w, 0 = far
    float& depthGrad() { return v[2]; }  // max |depth| delta vs 4-neighbours
    float& edgeDens() { return v[3]; }   // bag screen-bbox outline in the tile
    float& texDetail() { return v[4]; }  // baked high-frequency texture energy
    float& coverage() { return v[5]; }   // fraction of tile with geometry
    float& luma() { return v[6]; }       // mean brightness of the tile
    float& histAge() { return v[7]; }    // frames stable, /8, clamped - recurrent

    float motion() const { return v[0]; }
    float depth() const { return v[1]; }
    float depthGrad() const { return v[2]; }
    float edgeDens() const { return v[3]; }
    float texDetail() const { return v[4]; }
    float coverage() const { return v[5]; }
    float luma() const { return v[6]; }
    float histAge() const { return v[7]; }
};

extern const char* const kFeatureNames[kFeatures];
extern const char* const kOutputNames[kOutputs];

// Depth normalisation reference, in world units: a surface at w = kDepthRef
// reads as depth 1.0. Shared so the trainer and the runtime agree.
constexpr float kDepthRef = 8.0f;

// The temporal pass's ceiling, as a GS alpha byte (128 = keep all of the
// history). The history is the previous frame's own composite, so this is the
// retention of an exponential accumulator, and its time constant is what damps
// the alternating sub-pixel jitter. At 64 (a flat 50% mix) the accumulator
// tracks the alternation instead of averaging it, and the image visibly bobs;
// 115 is ~0.9 retention, about a 10-frame constant. Twinned with the engine.
constexpr float kTemporalMax = 115.0f;

// How hard the oracle is penalised for a frame that differs from the previous
// one, relative to how hard it is penalised for differing from the truth.
//
// This exists because "measured is not optimised" cost this feature three
// rounds of debugging. Flicker was reported by --blss-eval and still absent
// from the objective, so asking for history was free in the labels and the
// network never learned to fuse the jitter - the picture oscillated on a
// television while every number looked good. The penalty is the difference
// between the output and the REPROJECTED history, which the composite has
// already sampled, so it costs nothing to evaluate.
//
// 0 reproduces the old PSNR-only objective; raise it and the oracle buys
// stability with sharpness. Check BOTH columns of --blss-eval after changing it.
//
// IT IS 0 AFTER MEASURING, AND THAT IS NOT THE SAME AS DELETING IT. Swept
// jointly against kFillWeight below, 84 frames, 600 epochs, 3-6 training seeds
// per point (held-out PSNR moves +-0.4 dB with the seed alone, so one run
// decides nothing), at fill 6:
//
//   flicker weight   0.00    0.02    0.05    0.15
//   held-out PSNR   23.38   23.16   22.90   22.37     (bilinear 23.26)
//   training flick  21.01   20.86   20.80   20.20     (bilinear 23.41)
//
// so every non-zero setting pays 0.2 .. 1.0 dB of OUT-OF-DISTRIBUTION quality
// for a few percent of flicker, and 0.02 upwards already scores below plain
// bilinear on the held-out shots. The reason is visible in the split: the
// penalty is MSE against the reprojected history, which is minimised by
// out == history, i.e. by FREEZING - free on the near-static training shots,
// ghosting on the held-out orbit and dolly. It does not distinguish "stable
// because the jitter got fused" from "stable because nothing moved".
//
// What replaced it is the fill term: charging for kernels culls the point and
// sharpen passes, which are exactly the two that alternate with the jitter, so
// stability now comes out of the cost model for free (flicker 21.49 -> 21.01
// training, 27.12 -> 26.62 held-out, at fill 0 -> 6 with this weight at zero).
// If the console still oscillates, `--flicker-weight` is the knob and the
// numbers above are its price - but fix the FORM first: gate the penalty on
// reprojection confidence so it cannot be paid by freezing.
constexpr float kFlickerWeight = 0.0f;

// What one full-screen composite pass costs the oracle, in the same units as
// the error it is trading against: mean squared 8-bit level over the region.
//
// This is the SAME mistake as the flicker one, a level further up. The whole
// performance story of BLSS is sparsity - passes 2..5 are emitted per grid cell
// and a cell whose alpha byte rounds to zero is skipped - but nothing in the
// objective ever charged for a kernel, so the oracle asked for every kernel
// everywhere (blssDebugView showed the temporal channel saturated across the
// whole frame, sky included) and the composite degenerated to five full-screen
// passes. Anything absent from the objective does not exist for the network.
//
// The charge is a STEP ON THE QUANTISED ALPHA BYTE, not a smooth function of
// the weight, because the byte is what the engine's skip test reads: a weight
// that rounds to alpha 1 costs exactly as much fill as a weight of 1.0. A
// smooth penalty would leave the oracle sitting at a weight that pays for a
// whole pass and buys nothing.
//
// Scale: an all-kernels-everywhere frame costs 4 * kFillWeight (point +
// temporal + the TWO sharpen passes), against a whole-corpus MSE of ~70 at the
// PSNR the trained net reaches. 0 reproduces the old fill-blind objective.
// Check the occupancy columns of --blss-eval after changing it.
//
// 6 is the knee of the sweep (84 frames, 600 epochs, 3-6 seeds per point,
// flicker weight 0), and the knee is sharp:
//
//   fill weight        0      2      4      6     7.5     9     12     24
//   held-out PSNR   23.26  23.44  23.44  23.38  22.85  22.69  22.45  22.86
//   mean passes      4.25   3.99   3.84   3.39   3.29   2.81   2.58   2.43
//
// Up to 6 the quality is flat and the fill comes down; past it the network
// stops generalising - 7.5 costs half a decibel out of distribution and buys a
// tenth of a pass, and 12 is a full decibel BELOW plain bilinear. (Bilinear is
// 23.26 dB at exactly 1.00 passes, and the worst case is 5.00.)
constexpr float kFillWeight = 6.0f;

// The oracle's objective in one struct, because a term that is not in here is a
// term the network is structurally unable to learn - which is the bug this
// feature shipped three times. `--blss-train` / `--blss-eval` take
// `--flicker-weight` and `--fill-weight` so the two can be swept jointly
// without a rebuild; the defaults are the swept-and-chosen configuration.
struct Objective {
    float flicker = kFlickerWeight;  // vs the reprojected history: stability
    float fill = kFillWeight;        // per full-screen pass drawn: cost
};

// A tile with no geometry has nothing to reconstruct: whatever the network says
// about it is unsupervised noise, because the oracle's importance weighting
// gives "tiles where every kernel is identical" no vote during training. Both
// twins force such tiles to zero rather than trusting the net there.
constexpr float kMinCoverage = 0.02f;

// ONE SUBMITTED DRAW, AS THE EE SEES IT. This struct is the honesty contract of
// the whole feature pipeline: the PS2 runtime never reads the framebuffer back,
// so it knows a frame only as a list of bags with a screen bounding box, a
// depth range, and material constants the editor baked at build time. The
// training corpus is therefore forbidden from knowing more - it renders real
// images for the ground truth, but it describes the frame to the network
// through exactly this keyhole.
//
// If you are tempted to add a channel here that a bag submission cannot cheaply
// produce on the EE, you are about to train a network the console cannot run.
struct BagProxy {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // screen bbox, output pixels
    float wNear = 1, wFar = 1;             // view-space w range over the bbox
    float texDetail = 0;                   // 0..1 baked HF energy of the material
    float luma = 0;                        // 0..1 material brightness x light
};

// A pinhole camera, the form both producers can supply and the only thing
// reprojection needs. `right`/`up`/`fwd` orthonormal; the ray through output
// pixel (px, py) is fwd + right*((2px/outW - 1)*tanHalfFovX)
//                        + up   *((1 - 2py/outH)*tanHalfFovY),
// and a point at view depth w sits at pos + w * that ray.
struct Pinhole {
    float pos[3] = {0, 0, 0};
    float right[3] = {1, 0, 0};
    float up[3] = {0, 1, 0};
    float fwd[3] = {0, 0, 1};
    float tanHalfFovX = 0.577f, tanHalfFovY = 0.433f;
};

// What a renderer accumulates per tile while it submits the frame. This is the
// seam between "who is drawing" and "what the net sees": accumulate() below
// fills it from BagProxy lists (both producers), and buildFeatures() turns it
// into the normalised vector. Keep all normalisation, neighbour differencing
// and clamping in buildFeatures() so the two producers cannot drift.
struct TileStats {
    float cover = 0;      // 0..1 fraction of the tile that has any geometry
    float depthMean = 0;  // 1/w of the covered part, 0 = infinitely far
    float depthMin = 0;   // over the covered part, for the silhouette measure
    float depthMax = 0;
    float luma = 0;       // 0..1 mean brightness
    float texDetail = 0;  // 0..1 baked high-frequency energy of what is visible
    float edge = 0;       // 0..1 how much geometric outline crosses the tile
};

// BagProxy list -> per-tile accumulators. Rasterises each bbox over the tile
// grid: `cover` is the covered fraction, `depth*` the w range weighted by
// coverage, `luma`/`texDetail` coverage-weighted material means, and `edge` the
// bbox OUTLINE length crossing the tile over the tile perimeter. Shared by the
// corpus and (mirrored) by the engine, so the two cannot disagree about what
// "an edge" means.
std::vector<TileStats> accumulate(int cols, int rows, int outW, int outH,
                                  const std::vector<BagProxy>&);

// Per-corner reprojection: unproject the corner with `cur` at the tile's
// representative depth, project it with `prev`, and store the offset in
// PREVIOUS-frame texels of a lowW x lowH buffer (pass the display size when the
// history is the other display buffer, which is what the runtime does).
ReprojField buildReproj(int cols, int rows, int outW, int outH, int lowW, int lowH,
                        const Pinhole& cur, const Pinhole& prev,
                        const std::vector<TileStats>& stats);

// TileStats + reprojection + per-tile stability counters -> the network input.
// `histAge` is frames-since-this-tile-last-changed-a-lot, clamped by the caller
// to 0..255; it is the recurrent channel, so the caller owns its state.
std::vector<Features> buildFeatures(int cols, int rows,
                                    const std::vector<TileStats>& stats,
                                    const ReprojField& reproj,
                                    const std::vector<uint8_t>& histAge);

// ----------------------------------------------------------------- network ---

// MLP 8 -> 12 -> 3, tanh hidden, logistic outputs. 147 weights; the whole
// frame is 1812 MACs, which is why it runs on the EE FPU and not on VU1 (whose
// micro memory has nothing left - see the tyra-engine-dev skill).
struct Net {
    float w1[kHidden][kFeatures] = {};
    float b1[kHidden] = {};
    float w2[kOutputs][kHidden] = {};
    float b2[kOutputs] = {};

    void randomize(uint32_t seed);
    void forward(const Features& f, float out[kOutputs]) const;
};

// blss.net: "BLSS" + u32 version + the floats in declaration order.
bool save(const Net&, const std::string& path, std::string* err = nullptr);
bool load(Net&, const std::string& path, std::string* err = nullptr);

// The C++ the generated game compiles: a BLSS_NET_* constant table plus the
// forward pass. Returns the file body (templates.cpp writes it).
std::string emitGeneratedSource(const Net&);

// ------------------------------------------------------------------ images ---

// RGBA8, row-major, top-down (GS raster order).
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;

    Image() = default;
    Image(int width, int height) { resize(width, height); }
    void resize(int width, int height);
    uint8_t* at(int x, int y) { return &px[(size_t)(y * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[(size_t)(y * w + x) * 4]; }
};

bool writePng(const Image&, const std::string& path);
bool readPng(Image&, const std::string& path);

// Box-downsample by an integer factor - how the supersampled ground truth is
// resolved, and the only place in this file that is allowed to be prettier than
// the GS.
Image boxDown(const Image& src, int factor);

double psnr(const Image& a, const Image& b);

// ------------------------------------------------------------ weight field ---

// The network's answer for one frame: three weights per tile. The runtime
// averages these onto grid CORNERS and ships them as vertex alpha; corners()
// does the same averaging here so the host models the Gouraud field the
// rasteriser will interpolate.
struct WeightField {
    int cols = 0, rows = 0;
    std::vector<std::array<float, kOutputs>> tile;  // cols*rows

    void resize(int c, int r);
    std::array<float, kOutputs>& at(int cx, int cy) { return tile[(size_t)cy * cols + cx]; }
    const std::array<float, kOutputs>& at(int cx, int cy) const {
        return tile[(size_t)cy * cols + cx];
    }
    // Bilinear sample of the corner-averaged field at an output pixel centre.
    std::array<float, kOutputs> sample(float px, float py) const;
};

// Per-tile-corner reprojection: where in the PREVIOUS low-res target this
// corner's content was, as an offset in low-res texels. The runtime computes
// these from the previous/current view-projection at the tile's representative
// depth; the corpus computes them the same way from known camera matrices.
struct ReprojField {
    int cols = 0, rows = 0;              // tile counts (corners are cols+1 x rows+1)
    std::vector<float> du, dv;           // (cols+1)*(rows+1)
    void resize(int c, int r);
    void sample(float px, float py, int outW, int outH, float* du, float* dv) const;
};

// --------------------------------------------------------------- composite ---

// One frame's worth of what the console has in VRAM: the jittered low-res
// render and its predecessor, plus the phase each was drawn with.
struct Frame {
    Image low;       // this frame's low-res target
    Image prevLow;   // last frame's low-res target
    int phase = 0;   // jitter phase of `low`; prevLow used the other one

    // The temporal pass's history. On the console this is the other DISPLAY
    // buffer - the previous frame's finished composite, full resolution, free.
    // Set it and the composite samples it directly; leave it null and the
    // composite falls back to sampling `prevLow` upscaled, which is what the
    // frame would have looked like with every weight at zero.
    //
    // The fallback is deliberate for TRAINING: the true history depends on the
    // previous frame's weights, which depend on the net being trained, and
    // unrolling that is out of scope for a PoC. --blss-eval closes the loop
    // instead - it feeds each frame the real previous composite - so the
    // reported numbers are the recurrent ones even though the labels are not.
    const Image* history = nullptr;

    ReprojField reproj;
    std::vector<Features> features;  // cols*rows, tile order
    int cols = 0, rows = 0;
    int outW = 0, outH = 0;
    Scale scale = Scale::X2Y2;
};

// THE formula. Reproduces the engine's five GS passes in 8-bit integer
// arithmetic, including (A-B)*C>>7 truncation and the 0..255 clamps:
//
//   base   = bilinear(low)                     pass 1, opaque
//   x      = lerp(base, nearest(low), wA)      pass 2
//   x      = lerp(x, prevWarped, wC/2)         pass 3
//   out    = x + k*wD*(base - box(base))       passes 4 (add) and 5 (subtract)
//
// `sharpen` is the project's k (0..1 -> 0..127 as the GS alpha byte).
void composite(const Frame&, const WeightField&, float sharpen, Image& out);

// The individual kernels, for the PSNR table's baseline rows.
enum class Kernel { Point, Bilinear, Temporal, Sharpen };
void kernelOnly(const Frame&, Kernel, float sharpen, Image& out);

// HOW MUCH OF THE SCREEN EACH PASS ACTUALLY COVERS - the direct read-out of
// whether sparsity works, and the reason --blss-eval prints it next to PSNR.
//
// Measured through the ENGINE's own skip rule, not through the weights: a grid
// cell is drawn when ANY of its four corner alpha bytes is non-zero
// (`emitGrid` in renderer_core_blss.cpp breaks the strip otherwise), so a
// single tile asking for a kernel lights up the nine cells that touch its
// corners. That bleed is real fill and the number has to show it.
struct Occupancy {
    float point = 0;     // fraction of grid cells drawing pass 2
    float temporal = 0;  // ... pass 3
    float sharpen = 0;   // ... passes 4 AND 5
    // Mean full-screen passes per frame, base included:
    // 1 + point + temporal + 2*sharpen. 1 is bilinear, 5 is the worst case.
    float passes = 0;
};
Occupancy occupancy(const WeightField&, float sharpen);

// ------------------------------------------------------------------ oracle ---

// Per tile, the (wA, wC, wD) that minimise MSE against `truth` under
// composite(). Coordinate descent on the real formula - three scalars, a
// handful of sweeps, and it is the label the network is trained on.
//
// `importance` (optional, cols*rows) receives how much the choice mattered in
// that tile: MSE(worst weights) - MSE(best). Tiles where every kernel is
// equally good get a near-zero value and are down-weighted during training, so
// the net spends its 147 weights on the tiles that actually differ.
//
// The score is NOT plain MSE - see Objective: it is MSE against the truth, plus
// a flicker penalty against the reprojected history, plus the fill the
// candidate would cost the GS. All three, because the network only ever learns
// what the labels were scored on.
WeightField oracle(const Frame&, const Image& truth, float sharpen,
                   std::vector<float>* importance = nullptr,
                   const Objective& obj = Objective{});

// ----------------------------------------------------------------- trainer ---

struct Sample {
    Features f;
    std::array<float, kOutputs> target{};
    float importance = 1.0f;
};

struct TrainConfig {
    int epochs = 400;
    int batch = 64;
    float lr = 0.01f;
    float weightDecay = 1e-5f;
    uint32_t seed = 0x5AFE1234u;
    bool verbose = true;
};

// Adam, MSE weighted by Sample::importance. Returns final training loss.
float train(Net&, const std::vector<Sample>&, const TrainConfig&);

// ------------------------------------------------------------- CLI entries ---

// --blss-train [-o blss.net] [--frames N] [--epochs N] [--dump <dir>]
int trainMain(int argc, char** argv);
// --blss-eval [-i blss.net] [--frames N] [--dump <dir>]
int evalMain(int argc, char** argv);
// --blss-emit [-i blss.net] [-o blss_net.gen.hpp]
int emitMain(int argc, char** argv);

}  // namespace blss

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
#include <cmath>
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
//
// THIS IS THE SHIPPED VALUE AND THE ONLY ONE THE ENGINE CAN RUN. It is a
// compile-time constant on the twin (RendererCoreBlss::kTile), so anything
// other than 32 here is a MEASUREMENT configuration, not a shipping one -
// changing it for real means changing both sides in one commit.
constexpr int kTile = 32;

// ...and the value THIS RUN uses, so `--tile N` can sweep it without a rebuild.
//
// It is a global rather than a parameter because kTile reaches WeightField::
// sample(), ReprojField::sample() and the oracle's innermost loop, none of
// which has a config to thread it through, and because a per-call tile size
// would be a second place for the two producers to disagree. Set it ONCE, from
// the CLI, before any corpus is built; nothing below reads it lazily and no
// worker thread ever writes it.
//
// SWEPT, AND 32 STAYS - the measurement is in docs/neural-upscaler.md
// ("The tile size, swept"). 64 quarters the inference (224 tiles -> 56) and the
// packet build (255 corners -> 72) and costs more quality than the fold spread
// can absorb, while making the frame draw MORE fill, not less: a lit cell is
// four times the area, so the same decision buys four times the pixels.
namespace detail {
extern int gTile;
}
inline int tileSize() { return detail::gTile; }
// Rejects anything that is not a positive power of two (the grid, the corner
// averaging and the engine's 12.4 UV arithmetic all assume the tile divides
// the frame; a non-power-of-two that happens to divide 512x448 would still be
// a size the console cannot be configured to). Returns false and leaves the
// tile alone when it refuses.
bool setTileSize(int px);

// THE UPSCALE FACTOR, AND IT IS A PAIR OF INTEGERS RATHER THAN A MENU.
//
// It was `enum class Scale { X2Y2, X1Y2 }` with a `scaleY()` that returned a
// literal 2, i.e. the host could express exactly two configurations. THE ENGINE
// NEVER HAD THAT RESTRICTION: RendererSettings::setRasterScale(sx, sy) takes
// arbitrary positive ints and every derived size divides through it, and
// RendererCoreBlss::configure() only clamps them to >= 1. So the two-item menu
// was a HOST limit standing in front of a generic runtime, and the only thing
// that could not be measured because of it was whether going below half
// resolution is worth anything - which is now `--scale WxH`.
//
// The two named constants stay, spelled exactly as they were, because
// templates.cpp writes `blss::Scale::X1Y2` into the generated game and the
// project's `blssScale` int still means "0 = 2x2, 1 = 1x2". Nothing about the
// shipped configuration moves here; what moves is what the tool can measure.
//
// SWEPT, AND 2x2 STAYS - docs/neural-upscaler.md, "Below half resolution,
// swept", carries the tables. Three things it found, because they are the
// reasons not to reach for this knob:
//
//  - THE NETWORK DOES NOT DEGRADE, THE PICTURE DOES. The held-out margin over
//    each scale's OWN bilinear is flat across the sweep (+0.33 at 2x2, +0.45 at
//    4x4, 30 fold-runs each) and so is the oracle's ceiling (+1.02 -> +0.99),
//    while the ABSOLUTE PSNR falls up to 2.6 dB - eight times the whole margin.
//  - THE SPEED PRIZE DIMINISHES AND THE MEMORY PRIZE DOES NOT. Every term of
//    the 5.02 ms EE bill is an output-resolution quantity (the 16x14 grid, the
//    net, the proxies, the 255-corner packet), so break-even moves only
//    ~12.6 -> ~10.0 coverages, while VRAM returned goes 448 -> 784 KB.
//  - 2x4 IS THE WORST OF THE TWO 1/8 MODES, NOT THE BEST. It has identical fill
//    and identical VRAM to 4x2 and measures 1.25 dB below it, so the "vertical
//    detail is already compromised by interlace" intuition is refuted.
struct Scale {
    int x = 2, y = 2;

    constexpr Scale() = default;
    constexpr Scale(int sx, int sy) : x(sx < 1 ? 1 : sx), y(sy < 1 ? 1 : sy) {}

    // The two the project format can currently express. Declared here and
    // defined below because a class cannot hold a static member of its own
    // (still incomplete) type inline.
    static const Scale X2Y2;  // blssScale 0 - the shipped default
    static const Scale X1Y2;  // blssScale 1 - half height only
};
inline const Scale Scale::X2Y2{2, 2};
inline const Scale Scale::X1Y2{1, 2};

inline constexpr bool operator==(Scale a, Scale b) { return a.x == b.x && a.y == b.y; }
inline constexpr bool operator!=(Scale a, Scale b) { return !(a == b); }

inline constexpr int scaleX(Scale s) { return s.x; }
inline constexpr int scaleY(Scale s) { return s.y; }

// "2x2", for the header lines and the announcements. A table of decibels whose
// raster scale is not on the page is a table nobody can reproduce.
std::string scaleName(Scale s);
// "4x4" / "2x4" -> Scale. Returns false and leaves `out` alone on anything that
// is not two positive integers separated by an 'x'.
bool parseScale(const std::string& text, Scale* out);

// The two sub-pixel jitter phases, in LOW-RES pixels. For a 2x2 upscale these
// are exactly two of the four output-pixel centres inside one low-res pixel, so
// current + previous frame carry two DISTINCT output samples - a real quincunx
// pair, not an approximation. Quantised to sixteenths because that is what the
// GS XYOFFSET register stores (12.4 fixed point), so the console can reproduce
// these offsets bit-exactly: -4/16 and +4/16.
//
// AND THE QUINCUNX IS A PROPERTY OF 2x2, NOT OF THE JITTER. The offset is
// +-4/16 of a LOW-RES pixel at every raster scale, on both twins - the engine
// writes a literal `phase == 0 ? -4 : 4` into XYOFFSET and divides nothing by
// scaleX/scaleY - so what those offsets MEAN moves with the scale:
//
//   scale   output px per low-res px   what +-1/4 low-res px lands on
//   2x2     2 x 2  = 4 sub-positions   +-1/2 output px: two of the four centres
//   4x4     4 x 4  = 16                +-1 whole output px, and 1/4 and 3/4 of
//                                      a low-res pixel are not among the four
//                                      output-pixel centres {1/8,3/8,5/8,7/8}
//
// So at 4x the two phases are neither a quincunx nor even aligned to the output
// grid, and a one-frame-deep history could cover 2 of 16 sub-positions at best.
// That is an argument, not a measurement; the measurement is in
// docs/neural-upscaler.md ("Below half resolution") and it agrees - every scale
// below 2x2 is a jitter-off mode.
//
// JITTER IS A MODE NOW, NOT A CONSTANT, and the reason is a hardware
// measurement rather than a preference: with jitter on, a frozen camera on real
// PS2 hardware leaves 30.8% of the picture alternating between two images every
// frame (amplitude 1.42/255), and with it off that falls to the noise floor
// (0.03/255, identical to BLSS off). The project setting is `blssJitter`
// (ProjectSettings, format v5, default true) and codegen bakes it into the
// generated game.
//
// This is the host twin of it. A net fitted against the jittered sampler and
// run in a jitter-off build is fitted to a distribution the console does not
// produce - the same class of mismatch as the whole-bag proxy and the animated
// models - so `--blss-train <projectDir>` follows the PROJECT's own setting and
// `--no-jitter` forces it off for the bestiary. With it off both phases are the
// same zero offset, so the corpus renders one image per camera position, the
// composite's sampling undoes nothing, and the temporal pass sees a genuine
// still frame instead of a two-phase alternation.
//
// Set ONCE, before any corpus is rendered, and never afterwards - see gTile.
namespace detail {
extern bool gJitter;
}
inline bool jitterEnabled() { return detail::gJitter; }
void setJitter(bool on);

constexpr int kJitterPhases = 2;
inline float jitterX(int phase) {
    return detail::gJitter ? (phase == 0 ? -0.25f : 0.25f) : 0.0f;
}
inline float jitterY(int phase) {
    return detail::gJitter ? (phase == 0 ? -0.25f : 0.25f) : 0.0f;
}

// ---------------------------------------------------------------- features ---

// SIX, AND IT WAS EIGHT. Two channels have been deleted, both after the same
// measurement rather than because either looked redundant, and the way to read
// this block is that `--cv --drop-feature` with a CONTROL is the only thing
// that has ever settled a question about this vector.
//
// `histAge` - frames since this tile last changed - went first. It was the one
// RECURRENT channel and the one piece of state either twin kept between frames,
// so it carried the contract's most drift-prone rule (an earlier draft left the
// update to "the caller" and the two sides promptly implemented different
// thresholds, feeding the network a channel at training time the console would
// never reproduce). Removing it took the whole recurrent path with it - no
// per-tile counters, no prevDepth/prevCover, no ordering hazard between
// building the features and ageing the tiles - and buildFeatures() became a
// pure function of one frame on both twins.
//
// `luma` - the tile's mean brightness - went second, and it went for the
// reason 6a4cbead flagged it: THE ENGINE CANNOT PRODUCE IT. A bag hands BLSS
// one scalar for its brightness, and stapip_core can only fill it when the bag
// has a SINGLE colour; a per-vertex-lit mesh, which is every static mesh a
// generated game submits, has no cheap mean, so the channel read a constant
// 0.5 on the console while the corpus - which multiplied its own vertex
// brightness by its material's - spread it over 0..0.48. The network was
// therefore fitted on a photometric feature and run on a constant, which is
// the same class of bug as the whole-bag proxy and one level worse: the
// constant was OUT of the corpus' range.
//
// There were two ways out - give the EE a real value, or delete the channel -
// and the measurement chose. `--cv --cv-seeds 3 --drop-feature`, 13 folds x 3
// seeds = 39 fold-runs per row, everything else at the shipped defaults, on the
// procedural corpus with one proxy per object (the configuration the rows
// above it were measured on):
//
//   held at zero      (nothing)     luma    edgeDens
//   held-out margin     +0.41      +0.43      +0.35   dB over bilinear
//   mean passes          1.83       1.80       1.75
//   folds below bil      5/39       5/39       4/39
//
// edgeDens is the CONTROL and it is what makes the luma column mean something:
// dropping a channel that IS pulling its weight costs 0.06 dB, so the
// instrument resolves a difference of that size, and luma is 0.02 on the other
// side of it - i.e. the channel was not neutral, it was mildly harmful, and it
// cost eight tiles' worth of accumulator and twelve weights to be so. (The
// earlier histAge row was measured the same way at kFeatures = 8 and read
// +0.38 / +0.41 / +0.36 against an edgeDens control worth 0.02 dB.)
//
// Deleting it removes the last quantity the two sides computed DIFFERENTLY:
// BagProxy::luma, TileStats::luma, the per-bag colour average on the EE, one
// kMaxTiles accumulator in the engine's fixed arrays, and the corpus'
// measureLuma() over every material it loads. What is left is six channels that
// are all geometry, all cheap, and all computed the same way on both twins.
// It is also one input cheaper on the EE - 123 weights instead of 135, 108 MACs
// per tile instead of 121, ~24 200 per frame instead of ~27 100.
//
// If you ever want a photometric channel back, the honest form is a per-mesh
// mean brightness BAKED BY THE EDITOR into the bag, not sampled at run time -
// the same "editor bake" follow-up docs/neural-upscaler.md flags for texDetail.
constexpr int kFeatures = 6;

// THE HIDDEN LAYER, AND IT IS NOT WHAT LIMITS THIS NETWORK - measured, after
// the corpus grew to 13 shots dropped the in-distribution margin from ~+1.0 dB
// to ~+0.6 and that was read as "12 units is now the constraint rather than the
// data". It is not. Leave-one-shot-out cross-validation, 13 shots, 156 frames,
// 3 seeds = 39 fold-runs per row, everything else at the shipped defaults
// (decay 1e-4, fill 16), held-out margin over plain bilinear:
//
// (The weight and MAC counts in this table are the EIGHT-input ones it was
// measured with; at today's kFeatures = 6 they are 123 / 163 / 243 / 323 and
// 9*kHidden per tile. The conclusion is about the SHAPE of the held-out and
// in-dist rows, which the input count does not touch.)
//
//   kHidden           12      16      24      32
//   weights          147     195     291     387
//   MACs / frame  29 568  39 424  59 136  78 848    (11*kHidden over 224 tiles)
//   held-out, dz 0  +0.40   +0.41   +0.42   +0.41   dB
//   held-out, dz 8  +0.38   +0.39   +0.40   +0.39
//   mean passes      1.85    1.82    1.78    1.80   (at the shipped deadzone)
//   IN-DIST margin  +0.56   +0.56   +0.57   +0.56
//   folds below bil 4/39    4/39    4/39    4/39
//
// Read the IN-DIST row first: it is the one a capacity limit has to move, and
// it does not move at all - 0.556, 0.555, 0.572, 0.560 across a network that
// nearly tripled. The held-out row spans 0.02 dB, which is the size of the sd
// of the per-seed fold-mean (0.01), and it is NOT MONOTONIC: 32 scores below
// 24. A net that was short of units cannot get worse by being given more.
//
// So the ~0.6 dB is a ceiling of the FEATURES, the objective, or what a
// per-tile decision can express - not of the layer. Widening is the cheap thing
// to try and it has now been tried; the expensive things (a photometric
// feature, the editor-baked high-frequency energy, a cost model the net can
// actually learn) are what is left. 12 stays: 291 weights for +0.02 dB would
// double the EE inference for a difference this corpus cannot resolve, and the
// weight table is a generated header the game compiles.
//
// If you do move it: it lives on BOTH twins (here and RendererCoreBlss's
// kHidden), the emitter's table sizes and the engine's fixed-size arrays follow
// it automatically, and every existing blss.net becomes unreadable - the file
// carries no topology, so bump kNetVersion with it.
constexpr int kHidden = 12;
constexpr int kOutputs = 3;  // wA (point), wC (temporal), wD (sharpen)

// Per-tile network input. Every channel is normalised to roughly 0..1 by the
// producer, because the net is small enough that input scaling is the
// difference between converging and not. Order is load-bearing: it is the
// order the generated PS2 code fills the array in, and the order
// kFeatureNames documents.
struct Features {
    float v[kFeatures] = {0, 0, 0, 0, 0, 0};

    // Named accessors - use these, not v[3], everywhere outside the emitter.
    float& motion() { return v[0]; }     // reprojection length / tile edge
    float& depth() { return v[1]; }      // representative 1/w, 0 = far
    float& depthGrad() { return v[2]; }  // max |depth| delta vs 4-neighbours
    float& edgeDens() { return v[3]; }   // bag screen-bbox outline in the tile
    float& texDetail() { return v[4]; }  // baked high-frequency texture energy
    float& coverage() { return v[5]; }   // fraction of tile with geometry

    float motion() const { return v[0]; }
    float depth() const { return v[1]; }
    float depthGrad() const { return v[2]; }
    float edgeDens() const { return v[3]; }
    float texDetail() const { return v[4]; }
    float coverage() const { return v[5]; }
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

// THE THREE QUANTISERS, IN ONE PLACE. alphaBytes() turns a weight into the GS
// alpha byte the composite blends with, and each output has its OWN scale:
// point is a straight 0..128 factor, temporal is capped at kTemporalMax
// (the accumulator's retention, above), and sharpen carries the project's `k`
// as well. Anything that needs to reason about "what byte would this weight
// become" - the deadzone below, the fill term's step, the engine's
// cornerAlpha() - reads them from here, so there is exactly one definition to
// keep twinned with the engine.
inline void alphaScales(float sharpen, float out[kOutputs]) {
    const float k = sharpen < 0.0f ? 0.0f : (sharpen > 1.0f ? 1.0f : sharpen);
    out[0] = 128.0f;         // wA: aA = wA * 128
    out[1] = kTemporalMax;   // wC: aC = wC * kTemporalMax
    out[2] = k * 128.0f;     // wD: aD = wD * sharpen * 128
}

// THE DEADZONE, in GS alpha bytes, and the single largest performance knob left
// in this feature.
//
// A logistic output cannot emit zero. Where the oracle says "draw nothing here"
// the trained net says 0.02, which is worth 0.02 * 128 = alpha 2 - invisible in
// the picture, negligible in the trainer's MSE, and A WHOLE FULL-SCREEN GS PASS
// on the console, because emitGrid draws a cell as soon as ONE of its four
// corner alpha bytes is non-zero. That is the entire gap between the oracle's
// ~1.5-1.9 passes per frame and the network's ~2.85: not worse weights, a
// weight that rounds to "barely on" instead of "off".
//
// So at INFERENCE - on both twins, never in the labels - an output whose alpha
// byte would be at most kDeadzoneAlpha is snapped to exactly 0. The thresholds
// on the WEIGHT are therefore different for the three outputs and one of them
// depends on a project setting: kDeadzoneAlpha/128, kDeadzoneAlpha/kTemporalMax
// and kDeadzoneAlpha/(sharpen*128). Derive them from alphaScales() - three
// hardcoded constants would drift the moment sharpen or kTemporalMax moved, and
// a drifting deadzone is a twin divergence that no PSNR column can see.
//
// A sharpen of 0 makes the third scale 0, so every wD is dead - which is
// correct: the composite's passes 4/5 multiply by that same zero, so they were
// always going to draw nothing.
//
// 8 is the knee. Leave-one-shot-out cross-validation, 13 shots, 156 frames, 3
// seeds = 39 fold-runs per row, all of them THE SAME NETS - the deadzone never
// reaches the labels, so one training run measures the whole sweep
// (`--blss-eval --cv --deadzone-sweep 0,1,2,3,4,6,8,12,16,24`):
//
//   deadzone alpha     0     1     2     3     4     6     8    12    16    24
//   held-out margin  +.40  +.40  +.40  +.40  +.40  +.39  +.38  +.36  +.35  +.32
//   mean passes      2.85  2.83  2.70  2.55  2.48  2.12  1.85  1.53  1.41  1.30
//   point occupancy   83%   83%   82%   71%   65%   33%   15%    2%    0%    0%
//   temporal          85%   84%   84%   84%   84%   79%   69%   51%   41%   30%
//   sharpen          8.7%  7.7%  1.6%    0%    0%    0%    0%    0%    0%    0%
//   folds below bil  5/39  5/39  5/39  5/39  5/39  5/39  4/39  3/39  4/39  3/39
//
// Read it as the MARGINAL PRICE OF A PASS, which is the only way this decides
// anything: 0 -> 4 is free, 4 -> 8 costs 0.03 dB per pass saved, 8 -> 12 costs
// 0.06, 12 -> 16 costs 0.08 and 16 -> 24 costs 0.27. The price doubles at 8 and
// goes vertical past 16. So 8 buys A WHOLE FULL-SCREEN PASS (2.85 -> 1.85) for
// 0.02 dB - one twentieth of the 0.40 sd across folds, i.e. free at the
// resolution this corpus can measure - and closes most of the distance to the
// oracle's own 1.53.
//
// The sharpen channel is the first to go because its threshold is the loosest:
// at sharpen 0.5 the scale is 64, so alpha 4 is a weight of 0.0625 and by
// deadzone 3 the net is asking for no sharpen at all. That is the fill term and
// this working on the same tiles, not a bug - the sharpen occupancy was already
// down to 8.7% before the deadzone existed.
//
// What this does NOT do is make the network better. The margin is flat, then it
// erodes: the deadzone removes fill the net was asking for and not using, which
// is exactly the diagnosis ("the net fails to generalise the cost model") and
// not a fix for it. A net that had learned the cost model would not need this.
constexpr float kDeadzoneAlpha = 8.0f;

// Snap the three outputs of one tile to zero where the console would draw them
// at a negligible alpha. INFERENCE ONLY - the oracle is free to pick any weight
// it likes and its labels are unchanged; this is what both twins do to the
// NETWORK's answer before it becomes vertex alpha.
// The engine divides rather than multiplying (one division per output per
// frame instead of one multiply per tile), so this does too - the two sides
// have to agree on the boundary case, not merely on the intent.
inline void applyDeadzone(float w[kOutputs], float sharpen,
                          float deadAlpha = kDeadzoneAlpha) {
    float s[kOutputs];
    alphaScales(sharpen, s);
    for (int m = 0; m < kOutputs; ++m) {
        const float dead = s[m] > 0.0f ? deadAlpha / s[m] : 1e30f;
        if (w[m] <= dead) w[m] = 0.0f;
    }
}

// WHICH QUANTITY THE FLICKER PENALTY CHARGES FOR. Two forms, and the second
// exists because the first was measured to be unfixable in its own terms - see
// kFlickerWeight below for the measurement and docs/neural-upscaler.md
// ("The oscillation") for the hardware numbers that forced the rewrite.
enum class FlickerForm {
    // MSE between the output and the reprojected history. THE ORIGINAL, AND IT
    // IS MINIMISED BY out == history - by the picture FREEZING, which is free on
    // near-static training shots and is ghosting on an orbit or a dolly. Kept
    // reachable (`--flicker-form lag1`) because its sweep is recorded below and
    // setting a weight to zero is not the same as deleting the term.
    //
    // It is also structurally blind to the artefact: a lag-1 difference cannot
    // tell "alternating between two images" from "moving smoothly", and the
    // measured artefact is period 2.
    Lag1,
    // The PERIOD-2 ALTERNATION AMPLITUDE the candidate weights would leave in a
    // steady state, gated on reprojection confidence. Derived in blss.cpp above
    // altAmplitude(); the short version is that the temporal pass is an
    // exponential accumulator over two alternating jitter phases, so its
    // stationary alternation has a closed form in the candidate's own alpha
    // bytes and the phase pair the frame already holds. It charges for the bob
    // and for nothing else, it cannot be paid by freezing (freezing is not
    // expressible - the only ways down are fusing the phases or not amplifying
    // them), and it is switched off wherever the history is not trustworthy.
    Period2,
};

// The shipped form. Period2 is the default because Lag1 was measured to buy no
// stability at any weight (kFlickerWeight, measurement (b)) while the console
// went on bobbing.
constexpr FlickerForm kFlickerForm = FlickerForm::Period2;

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
// IT IS 0 AFTER MEASURING, AND THAT IS NOT THE SAME AS DELETING IT. Measured
// twice, and the second measurement is the one to trust because the first was
// read off a single 2-of-7 split:
//
// (a) the original sweep, 84 frames, 600 epochs, one split, 3-6 seeds per point,
//     at fill 6 - held-out PSNR 23.38 / 23.16 / 22.90 / 22.37 at flicker weight
//     0 / 0.02 / 0.05 / 0.15, i.e. "0.02 already scores below bilinear";
//
// (b) leave-one-shot-out cross-validation (`--blss-eval --cv --cv-seeds 3`, 13
//     shots, 156 frames, decay 1e-4, fill 16) - the same 21 fold-runs with and
//     without it:
//
//       flicker weight        0.00     0.02
//       held-out margin      +0.55    +0.53   dB over bilinear
//       mean passes           3.00     2.89
//       held-out flicker     33.3 / 44.9 / 28.7 / 6.5 / 7.8 / 41.6  (per fold)
//                            33.0 / 44.7 / 28.8 / 6.5 / 7.8 / 41.6
//
// So (a) OVERSTATED the price - it is 0.02 dB, not 0.22 - and understated
// nothing: the flicker column does not move at all. The term buys no measurable
// stability, which is the actual reason it ships at zero.
//
// The reason it cannot work in this form is unchanged: the penalty is MSE
// against the reprojected history, which is minimised by out == history, i.e. by
// FREEZING - free on near-static shots, ghosting on an orbit or a dolly. It does
// not distinguish "stable because the jitter got fused" from "stable because
// nothing moved". AND IT IS BLIND TO THE ARTEFACT ON TOP OF THAT: a lag-1
// quantity cannot separate "alternating between two images" from "moving
// smoothly", and the bob is period 2. That is the most likely reason the old
// flicker column reported an improvement while the television bobbed.
//
// TWO BELIEFS THAT WERE HELD HERE AND ARE NOW DISPROVEN ON HARDWARE. Both used
// to be written down as the reason the term could stay at zero:
//
//   (i) "the fill term replaced it - charging for kernels culls the point and
//       sharpen passes, which are exactly the two that alternate with the
//       jitter, so stability comes out of the cost model for free". IT DID NOT.
//       With the fill term in and a project-trained net, a frozen camera on real
//       PS2 hardware leaves 30.8% of the picture alternating every frame at
//       1.42/255, against a 0.03/255 floor with the jitter off. The mechanism
//       the belief missed is in altAmplitude() (blss.cpp): the accumulator is
//       the ONLY thing that damps the base pass's own phase difference, the fill
//       term culls the accumulator too, and the sharpen pass is applied AFTER
//       the accumulator so no amount of temporal weight damps it at all.
//
//  (ii) "it bobs because the trained net puts 0% weight on temporal, so nothing
//       fuses the two phases". ALSO WRONG: examples/upscaler-lab's net puts
//       73.7% temporal occupancy on the screen and bobs at the same magnitude.
//       Occupancy is not retention - a cell drawn at alpha 20 is drawn, and
//       (1-c)/(1+c) at c = 20/128 is 0.73, i.e. it removes a quarter of the
//       alternation and keeps three.
//
// So the form was fixed rather than the weight, and FlickerForm::Period2 is what
// `--flicker-weight` charges for now. `--flicker-form lag1` still reaches the
// term measured above.
//
// (c) AND THE NEW FORM WAS SWEPT TOO, AND IT IS ALSO A BAD TRADE - which is the
// measurement that closes this question rather than moving it. Full account in
// docs/neural-upscaler.md ("The trade curve"); the summary is that the weight
// which restores stability costs more than the cure it is competing with.
// examples/upscaler-lab, --cv --cv-seeds 5, 120 frames, 400 epochs, decay 1e-4,
// fill 16, deadzone 8, jitter ON except the last row - 30 fold-runs per row.
// "alt" is the fraction of gated pixels alternating by >= 2/255; its floor (the
// native render, which has no alternation to have) is 12.4%:
//
//     weight    0    0.05   0.2   0.5   1.5     2     3     4     5   | OFF, 0
//     margin +0.61  +0.62 +0.63 +0.61 +0.55 +0.48 +0.44 +0.35 +0.29   |  +0.33
//     sd      0.51   0.52  0.53  0.55  0.85  1.01  1.13  1.32  1.40   |   0.34
//     below   1/30   1/30  0/30  0/30  6/30  6/30 12/30 15/30 15/30   |   2/30
//     passes  1.73   1.73  1.73  1.72  1.73  1.74  1.74  1.75  1.75   |   1.65
//     alt    14.8%  14.8% 14.9% 14.9% 14.9% 14.9% 14.4% 13.0% 12.4%   |  12.3%
//
// Read it in this order. The alternation reaches the jitter-off floor ONLY at
// weight 5, and at weight 5 the margin is +0.29 against the +0.33 that turning
// the jitter off buys for nothing. Mean passes never moves (1.73 -> 1.75), so
// none of this is a fill trade. What moves is the SPREAD: sd 0.51 -> 1.40 and
// folds below plain bilinear 1/30 -> 15/30, i.e. at the setting that fixes the
// bob, half the content is worse off than not running the feature. And below
// 1.5 the knob does nothing at all - it has no cheap setting.
//
// The form change was still worth making and this is its evidence: at the same
// weight 0.2, lag1 loses 11 folds of 30 to bilinear and period2 loses none, at
// the same margin. It did not change the verdict.
constexpr float kFlickerWeight = 0.0f;

// WHETHER THE HISTORY EXISTS AT ALL in this tile - the only per-tile test the
// period-2 term still applies, and the story of why it is not more than that is
// the useful half.
//
// The period-2 model reads the previous frame's low-res render, reprojected, as
// "the same content at the other jitter phase". That reading is true exactly
// when the reprojection is: on a disocclusion, on a silhouette, or under a
// reprojection long enough that the tile's single depth cannot describe it, the
// two samples are different CONTENT and their difference is not an alternation
// amplitude at all. Charging for it there would teach the oracle to buy temporal
// weight where temporal weight ghosts - the old term's failure mode arriving by
// a new route. So a gate was designed, and it read:
//
//   conf = coverage >= kMinCoverage ? (1 - motion) * (1 - depthGrad) : 0
//
// on the reasoning that `motion` at 1.0 puts the history a whole tile away and
// `depthGrad` at 1.0 is a full-range depth step, i.e. a silhouette. Both
// readings are correct about what the channels MEAN and wrong about what they
// CONTAIN. On examples/upscaler-lab (`--blss-eval --features`, 5376 tiles):
//
//     motion     49.1% of tiles at EXACTLY 1.0
//     depthGrad  41.0% at exactly 1.0
//     coverage   66.8% at 1.0
//
// so that product is ZERO on most of the frame - and zero specifically on the
// moving, geometrically busy part, which is where the console's difference image
// lights up and the flat sky stays black. A gate built out of saturated channels
// is a gate that is always shut, and the term would have swept as "buys
// nothing", which is the same non-answer --flicker-weight already gave twice.
// (It is the same defect as the open `depth` item in docs/neural-upscaler.md:
// these channels are clamped ratios and they spend most of their time at the
// clamp.)
//
// What remains here is the one test that is not a proxy for something else: a
// tile with no geometry in it has no history to reproject. Outliers are handled
// where they belong instead - kAltClamp (blss.cpp) bounds the charge AND
// flattens its gradient, so a disocclusion costs a constant and therefore buys
// the oracle nothing. A per-SAMPLE test on top of this drops any pixel whose
// reprojected position leaves the frame, where there is no history to have
// fused.
//
// This is host-only arithmetic: the objective has no engine twin (see
// docs/blss-reconstruction.md, "What is NOT part of this contract").
float reprojConfidence(const Features& f);

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
// IT WAS 6, AND 6 WAS AN ARTEFACT OF THE SPLIT IT WAS CHOSEN ON. The old sweep
// read a sharp knee at 6 off a SINGLE 2-of-7 held-out split ("12 is a full
// decibel below plain bilinear"). Under leave-one-shot-out cross-validation -
// seven folds instead of one draw, `--blss-eval --cv` - that cliff does not
// exist: on the same seven shots, fill 12 scores +0.27 dB over bilinear where
// fill 6 scores +0.30, a difference inside the fold noise, while spending half a
// pass less. The cliff was one unlucky pair of shots.
//
// 16 is the knee of the sweep that replaced it: 13 shots, 156 frames, 400
// epochs, weight decay 1e-4, flicker 0, 7 folds x 3 seeds = 21 fold-runs per
// point, holding out each of the ORIGINAL seven shots in turn so every column is
// the same held-out content:
//
//   fill weight       6      12     16     24     40
//   held-out margin  +0.36  +0.53  +0.55  +0.50  +0.43   dB over bilinear
//   mean passes       3.92   3.59   3.00   2.76   2.67
//   folds below bil   3/21   2/21   2/21   1/21   1/21
//
// (at decay 1e-5 the same corpus reads +0.36 at fill 6 and +0.33 at fill 12, so
// the fill term and the decay have to be set TOGETHER - see TrainConfig.)
//
// The shape is a plateau, not a cliff: 12..24 are one measurement, and the fill
// falls by a whole pass across it. 16 sits in the middle of the plateau at
// 3.00 passes, which is what a knee looks like when it is measured over seven
// held-out shots instead of two. Past 24 the network starts giving up kernels it
// should have kept and the margin goes with it.
//
// THAT SWEEP PREDATES kDeadzoneAlpha, and the two do overlapping work: the fill
// weight teaches the net to want fewer kernels, the deadzone throws away the
// ones it wants at an alpha the eye cannot see. The pass counts above are
// therefore the deadzone-0 ones (16 -> 3.00; it is 1.85 with the shipped
// deadzone), and the two have NOT been swept jointly. That is the open
// experiment: with the deadzone taking the "barely on" fill for free, a LOWER
// fill weight might now buy back quality at the same pass count. Sweep them as
// a pair before moving either.
constexpr float kFillWeight = 16.0f;

// The oracle's objective in one struct, because a term that is not in here is a
// term the network is structurally unable to learn - which is the bug this
// feature shipped three times. `--blss-train` / `--blss-eval` take
// `--flicker-weight` and `--fill-weight` so the two can be swept jointly
// without a rebuild; the defaults are the swept-and-chosen configuration.
struct Objective {
    float flicker = kFlickerWeight;  // the stability term's weight
    // WHICH stability quantity that weight buys - and the distinction is the
    // whole term, not a variant. See FlickerForm.
    FlickerForm flickerForm = kFlickerForm;
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
// It carried a `luma` too, and losing it is the point of the kFeatures note
// above: a brightness the EE can only fill for single-coloured bags is a
// channel the network is trained on and never given.
struct BagProxy {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // screen bbox, output pixels
    float wNear = 1, wFar = 1;             // view-space w range over the bbox
    float texDetail = 0;                   // 0..1 baked HF energy of the material
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
    float texDetail = 0;  // 0..1 baked high-frequency energy of what is visible
    float edge = 0;       // 0..1 how much geometric outline crosses the tile
};

// BagProxy list -> per-tile accumulators. Rasterises each bbox over the tile
// grid: `cover` is the covered fraction, `depth*` the w range weighted by
// coverage, `texDetail` the coverage-weighted material mean, and `edge` the
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

// TileStats + reprojection -> the network input. PURE: no per-tile state
// survives a frame on either twin any more (see kFeatures - the recurrent
// `histAge` channel was measured and removed), so this is a function of the
// frame and nothing else, and there is no ordering hazard between building the
// features and updating a counter.
std::vector<Features> buildFeatures(int cols, int rows,
                                    const std::vector<TileStats>& stats,
                                    const ReprojField& reproj);

// ------------------------------------------------------------- activations ---

// THE TRANSCENDENTALS, AND WHY THEY ARE A TABLE.
//
// The net evaluates 12 tanh + 3 logistic per tile, and the engine builds with
// no -ffast-math (vendor/tyra/Makefile.base), so on the console that is
// 12 tanhf + 3 expf + 3 fdiv per tile - 3 360 libm calls per frame at 224
// tiles. Every one of those results is then thrown away down to a BYTE:
// cornerAlpha() truncates the output to 0..128 and the deadzone snaps anything
// at or below alpha 8 to zero. Full libm precision is computed and discarded.
//
// ONE TABLE SERVES BOTH, because logistic(z) == (1 + tanh(z/2)) / 2 exactly.
// So the whole activation budget is 15 table lookups: no libm, and no divide
// either - the logistic's `1 / (1 + e)` is gone, not merely cheapened.
//
// A TABLE IS THE RIGHT SHAPE FOR A TWIN, and that is the argument for it over
// a polynomial. A minimax polynomial has to be *matched* between two compilers
// - same coefficients, same association order, same FMA contraction decisions -
// and any mismatch is a silent drift in the objective the network was fitted
// against. A table of integers is the same integers on both sides or it is
// visibly not, and the reconstruction from an integer is exact: an int16 -> float
// conversion and a multiply by 2^-15 are both exactly representable, so the
// activation step contributes ZERO divergence between the twins. (The MACs
// around it are float and remain subject to whatever the EE FPU does, exactly
// as they already are - the table does not fix that and does not claim to.)
//
// The exact definition, index mapping, clamping and rounding rule are written
// down in docs/blss-reconstruction.md section 5, which is the contract the
// engine implements against. Do not paraphrase it here; change it there.
//
// OFF BY DEFAULT AFTER MEASURING, and the measurement is on the upscaler page:
// at 512 entries the cross-validated held-out margin is indistinguishable from
// libm's, so the table costs nothing. It ships off because the ENGINE TWIN DOES
// NOT EXIST YET - a host that fits against a table while the console evaluates
// libm is the twin drift this whole file is arranged to prevent. Turn it on in
// the same commit as RendererCoreBlss's copy, never before.
constexpr float kActRange = 4.0f;   // table domain: a is clamped to [-4, +4]
constexpr int kActScale = 32768;    // entries are round(tanh(a) * 32768), Q15

namespace detail {
extern int gActN;              // 0 = libm; otherwise the table has gActN+1 entries
extern const float* gActTab;   // gActN+1 dequantised entries, or null
}

// Build (or drop) the table. `n` is the number of INTERVALS - the table holds
// n+1 entries, both endpoints included - and 0 restores std::tanh / std::exp.
// Must be a positive even number so the midpoint entry is exactly tanh(0) = 0
// and the table is odd-symmetric; returns false and changes nothing otherwise.
bool setActTable(int n);
// FNV-1a over the table's int16 entries, little-endian. THE CONTRACT'S
// CHECKSUM: both twins generate the table from the formula in the math doc, and
// this is how they check that their two libms rounded every entry the same way
// rather than assuming it. 0 when the table is off.
uint32_t actTableHash();
// The table as C++ the engine can paste (`--blss-emit-table`), exploiting
// tanh's odd symmetry so only the upper half is spelled out.
std::string emitActTable();

// tanh and the logistic, through the table when one is built. Both twins call
// exactly these two.
inline float actTanh(float a) {
    const int n = detail::gActN;
    if (n <= 0) return std::tanh(a);
    if (a <= -kActRange) return detail::gActTab[0];
    if (a >= kActRange) return detail::gActTab[n];
    // Nearest entry, no interpolation: the reconstruction is then a table value
    // EXACTLY, which is what makes the activation step twin-identical. Rounding
    // (rather than truncating) the index halves the worst-case error for free.
    const float x = (a + kActRange) * (static_cast<float>(n) / (2.0f * kActRange));
    return detail::gActTab[static_cast<int>(x + 0.5f)];
}
inline float actLogistic(float z) {
    if (detail::gActN <= 0) return 1.0f / (1.0f + std::exp(-z));
    // logistic(z) = (1 + tanh(z/2)) / 2, exactly. Both constants are powers of
    // two, so this costs one multiply and one multiply-add and loses nothing.
    return 0.5f + 0.5f * actTanh(0.5f * z);
}

// ----------------------------------------------------------------- network ---

// MLP kFeatures -> kHidden -> kOutputs, tanh hidden, logistic outputs. At the
// shipped 6 -> 12 -> 3 that is 123 weights and 108 MACs per tile, so ~24 200
// MACs plus ~3 400 transcendentals over a 16x14 grid - small enough to run on
// the EE FPU rather than VU1 (whose micro memory has nothing left - see the
// tyra-engine-dev skill). The two figures are (kFeatures+kOutputs+1)*kHidden +
// kOutputs and (kFeatures+kOutputs)*kHidden, the second times 224 tiles; the
// width sweep that says 12 is the right one is up at kHidden.
// NEITHER HAS EVER BEEN TIMED, in PCSX2 or on hardware - "far too small to
// matter" is arithmetic and a design argument, not a measurement.
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

// Does the emitter still produce compilable C++? Checks the literal formatter
// against the values that broke it - exact zeros and exact ones, which `%.9g`
// renders as "0" and "1", so the header said `0F` and no compiler accepts that.
// Every bias of an untrained net is exactly 0 and templates.cpp emits an
// untrained net whenever a project has BLSS on and no blss.net, so the documented
// "missing net is not a build failure" path was broken from the day it was
// written and nothing on the host ever compiled the file to notice.
// --blss-train and --blss-emit both run it; `err` gets the offending literal.
bool selfTestEmitter(std::string* err = nullptr);

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
// ...and from memory, because an animated model's texture is EMBEDDED in the
// .glb and never exists as a file on the host. The build writes it out next to
// the ELF; the corpus decodes the same bytes without a temp file.
bool readPngMemory(Image&, const unsigned char* data, size_t n);

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

// ------------------------------------------------------------------- the bob ---

// THE FROZEN-CAMERA PROBE IS NOT HERE, and the reason is worth a paragraph
// because it is the first thing the next person will reach for.
//
// The hardware experiment that found the bob froze the camera and compared
// consecutive fields, so the obvious host twin is a Frame holding BOTH jitter
// phases of ONE pose. The corpus cannot supply that: it renders frame N at
// phase N&1 from camera N, so the only "other phase" any Frame owns is
// `prevLow`, which is a different pose as well as a different phase. Adding the
// second render is a change to blss::generate() (blsscorpus.cpp), not to this
// file.
//
// What stands in for it is MOTION COMPENSATION - period2Alternation() below
// warps the two predecessors into the current view before differencing, which
// removes the camera and leaves the phase. That is a weaker instrument than
// freezing (it carries the warp's own resampling error), and the `native` row of
// its table is exactly that residual, printed so every other row can be read
// against it.
// ------------------------------------------------------------------ oracle ---

// Per tile, the (wA, wC, wD) that minimise MSE against `truth` under
// composite(). Coordinate descent on the real formula - three scalars, a
// handful of sweeps, and it is the label the network is trained on.
//
// `importance` (optional, cols*rows) receives how much the choice mattered in
// that tile: MSE(worst weights) - MSE(best). Tiles where every kernel is
// equally good get a near-zero value and are down-weighted during training, so
// the net spends its 123 weights on the tiles that actually differ.
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
    // 123 weights over ~13 000 supervised tiles sounds like a lot of data per
    // weight, and it is not: the tiles come from 12 camera moves, so the number
    // that matters is a dozen scenes, and this network's whole failure mode is
    // variance. Decay was 1e-5, which is barely regularisation at all. Under
    // leave-one-shot-out cross-validation (13 shots, 156 frames, 7 folds x 3
    // seeds, held-out margin over bilinear at the fill weight that matches its
    // pass count):
    //
    //   weight decay     1e-5     1e-4     1e-3
    //   held-out margin  +0.36    +0.55    +0.40   dB
    //   mean passes       3.92     3.00     4.34
    //
    // 1e-4 is worth a fifth of a decibel AND a whole pass; 1e-3 over-smooths the
    // weight field into asking for every kernel everywhere again. Set with
    // kFillWeight, never alone - decay pulls the outputs toward the mean oracle
    // answer, which is nonzero, so more decay means more fill unless the
    // objective charges for it.
    float weightDecay = 1e-4f;
    uint32_t seed = 0x5AFE1234u;
    bool verbose = true;
    // Standardise the inputs while fitting, then fold the affine map back
    // into w1/b1 so the net that comes out still eats the RAW features the
    // engine computes. OFF, AFTER MEASURING - it fits the training shots better
    // and generalises worse, which is the whole diagnosis of this network in one
    // switch; train() carries the numbers. `--standardise` turns it back on.
    bool standardise = false;
};

// Adam, MSE weighted by Sample::importance. Returns final training loss.
float train(Net&, const std::vector<Sample>&, const TrainConfig&);

// ------------------------------------------------------------- CLI entries ---

// TRAIN ON YOUR OWN PROJECT. Both entry points take an optional POSITIONAL
// project directory:
//
//   tyrax-editor --blss-train <projectDir> [-o blss.net] ...
//   tyrax-editor --blss-eval  <projectDir> --cv ...
//
// With it, the corpus is the project's own scenes - real geometry, real
// materials, real terrain, walked and orbited and whipped by cameras derived
// from the scene's bounds and its player start, plus any authored Cutscene
// Director camera track (blssscene.{hpp,cpp}). Without it, the procedural
// bestiary of blsscorpus.cpp, which is also the fallback when a project has
// nothing to draw.
//
// Why it matters, in one line: the console runs the network on the project, and
// a net fitted to the bestiary is fitted to a distribution of DIFFICULTY the
// project does not have. Which of the two nets is better ON a given project is
// a measurement, not an assumption - `--blss-eval <projectDir> --cv` against
// `--blss-eval <projectDir> -i <a bestiary-trained net>`, per shot.
//
// `--no-package-split` reverts the bag proxies to one per object instead of one
// per VU1 package - see CorpusConfig::packageSplit. It exists to reproduce the
// fold tables measured before the split, not to ship.
//
// `--threads N` (0 = every core) bounds the parallel phases: the corpus
// renderer, the oracle, and --blss-eval's per-method evaluation loop. IT MOVES
// THE WALL CLOCK AND NOTHING ELSE - each hands item i to a fixed worker that may
// touch only item i - and `--threads 1` against `--threads N` producing
// byte-identical blss.net files and character-identical tables is how that is
// checked rather than believed. The FIT is sequential SGD and ignores it, which
// is why --blss-train prints its three phases separately: on a project
// corpus at the shipped defaults the fit is the largest of the three.
// --blss-eval and --cv print the same kind of line, because that is how anyone
// checks a claim that one of them got faster.
//
// --blss-train [<projectDir>] [-o blss.net] [--frames N] [--epochs N]
//              [--weight-decay W] [--dump <dir>] [--all-shots] [--threads N]
int trainMain(int argc, char** argv);
// --blss-eval [<projectDir>] [-i blss.net] [--frames N] [--dump <dir>]
//             [--deadzone A] [--deadzone-sweep a,b,c]
//
// `-i` IS OPTIONAL, AND THAT IS THE POINT OF THIS VERB. The row that answers
// "should this project have the upscaler on at all" is the ORACLE row - the best
// any per-tile weighting can reach under the exact GS composite - and no part of
// it involves a trained network. This entry point used to load blss.net FIRST
// and bail with "cannot open blss.net", which made the first step the settings
// panel prescribes ("evaluate your project before turning this on") impossible
// to perform on a fresh project. Now a missing DEFAULT net drops the
// `half-res + BLSS (trained)` row and nothing else; only an EXPLICIT `-i` that
// cannot be opened is an error.
//
// Two lines exist for a caller rather than a reader, and both are printed
// unconditionally so nothing has to be inferred from the table's shape:
//
//   [blss] verdict headroom=<dB> passes=<f> bilinear=<dB> oracle=<dB> native=<dB>
//       Every --blss-eval, net or not. `headroom` is the oracle's margin over
//       plain bilinear - the scene's CEILING, under +0.10 dB meaning there is
//       nothing to reconstruct - and `passes` is what the oracle pays for it.
//       Frame-weighted over both splits, the same arithmetic blssui::summarise()
//       does on the parsed table.
//   [blss] fold <k> of <n>
//       --cv only, as each fold finishes (completion order - folds run in
//       parallel). The fold loop turns the trainer's verbosity off and otherwise
//       prints nothing until the table at the end, so a progress bar driven off
//       this tool used to go blank for minutes.
//
// `--deadzone A` overrides kDeadzoneAlpha for the run, and `--deadzone-sweep`
// measures a whole list of them inside ONE --cv run. Both are cheap because the
// deadzone is an INFERENCE knob: it never reaches the labels, so N deadzones
// cost N evaluations of the same trained folds rather than N trainings.
//
// Two modes train their own nets and therefore ignore -i:
//
//   --cv [--cv-seeds N] [--cv-folds N]
//       LEAVE-ONE-SHOT-OUT CROSS-VALIDATION, and it is the number to quote for
//       anything out of distribution. Each shot is held out in turn while the
//       net trains on all the others, so the table is one row per KIND of
//       content ("which shots does it fail on") rather than one average over an
//       arbitrary pair. --cv-seeds repeats it on N independently generated
//       corpora, because a mean without a spread is how this feature published
//       noise twice. --cv-folds N holds out only the first N shots, which is how
//       a before/after survives the corpus growing under it.
//       It prints the margin over bilinear per fold AND the mean pass count:
//       a decibel bought at 5.00 passes is not a win, it is bilinear plus every
//       kernel everywhere. With --deadzone-sweep it also prints one row per
//       deadzone, both columns, over the same folds.
//
//   --features [--probe "<BLSSFEAT line>"]
//       What the six input channels actually look like over the corpus -
//       distribution, saturation, per-shot means, and correlation with what the
//       oracle asked for. A channel that is constant is a channel the 135
//       weights cannot use, and nothing printed that until this existed.
//
//       --probe IS THE OTHER HALF, and it is the instrument this feature spent
//       eleven commits without: it takes a feature vector MEASURED ON THE
//       CONSOLE and places it in that distribution. Run a game with the
//       project's BLSS debug view set to 2, take the `BLSSFEAT` line out of
//       bin/log.txt and paste it in whole - the engine prints min/mean/max per
//       channel over the tile grid in the same names and the same order, so
//       the tool reports, per channel, the console's spread, its percentile in
//       the corpus, how much of the corpus falls inside the console's own
//       band, and a verdict: out of range, a <1% tail the net can only
//       extrapolate into, or constant across the frame (which is a network
//       making no per-tile decision at all).
//
//   --drop-feature <name|index>[,...]
//       Hold those input channels at zero over the whole corpus - training,
//       labelling and evaluation - which is what deleting them from the vector
//       would do to a trained net. Ask "does this channel earn its keep" with
//       `--cv --drop-feature histAge` BEFORE paying for it on both twins.
int evalMain(int argc, char** argv);
// --blss-emit [-i blss.net] [-o blss_net.gen.hpp]
int emitMain(int argc, char** argv);

}  // namespace blss

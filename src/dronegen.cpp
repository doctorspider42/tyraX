#include "dronegen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

namespace dronegen {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

// Control-rate block. Every parameter smoothing, LFO, glide and filter
// coefficient update happens once per kCtrl samples; only the oscillators and
// the delay networks run per sample. At 22050 Hz that is a 1.5 ms grain - finer
// than any modulation this synth produces, and it keeps 120 voices cheap.
constexpr int kCtrl = 32;

constexpr int kMaxBellVoices = 16;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float dbGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float centsMul(float cents) { return std::pow(2.0f, cents * (1.0f / 1200.0f)); }

// xorshift32. Every random stream in the synth is seeded from Params::seed, so
// an offline render is bit-reproducible - which is what lets the tool promise
// that a preset is a piece and not a dice roll.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
};

// Mixes two ints into a well-distributed seed, so every stream (voice drift,
// bell scheduling, dither) can derive from Params::seed without correlating.
inline uint32_t mix32(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9E3779B1u + b * 0x85EBCA77u + 0xC2B2AE3Du;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x ? x : 1u;
}

// --- oscillators -----------------------------------------------------------

// PolyBLEP residual: cancels the first-order discontinuity of a naive saw or
// pulse. Without it a 55 Hz saw at 22 kHz folds a full spectrum of garbage back
// down into the audible range, and a drone is exactly the signal that gives you
// all day to hear it.
inline float polyBlep(float t, float dt) {
    if (t < dt) { t = t / dt - 1.0f; return -t * t; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt + 1.0f; return t * t; }
    return 0.0f;
}

inline float wrap01(float x) { return x - std::floor(x); }

// One oscillator sample. `phase` is 0..1, `dt` the per-sample increment.
inline float oscSample(int wave, float phase, float dt, float pw, float fmRatio,
                       float fmIndex, float nyqRatio) {
    switch (wave) {
        case WaveTriangle:
            return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
        case WaveSaw:
            return (2.0f * phase - 1.0f) - polyBlep(phase, dt);
        case WaveSquare: {
            float v = phase < 0.5f ? 1.0f : -1.0f;
            v += polyBlep(phase, dt);
            v -= polyBlep(wrap01(phase + 0.5f), dt);
            return v;
        }
        case WavePulse: {
            const float w = clampf(pw, 0.05f, 0.95f);
            float v = phase < w ? 1.0f : -1.0f;
            v += polyBlep(phase, dt);
            v -= polyBlep(wrap01(phase + (1.0f - w)), dt);
            return v * 0.8f;
        }
        case WaveFm:
            return std::sin(kTau * phase +
                            fmIndex * std::sin(kTau * phase * fmRatio));
        case WaveOrgan: {
            // Drawbar-ish partial set. Partials past Nyquist are dropped rather
            // than folded - `nyqRatio` is the oscillator frequency over the
            // Nyquist rate, so the test is one multiply.
            static const int part[6] = {1, 2, 3, 4, 6, 8};
            static const float amp[6] = {1.0f, 0.5f, 0.34f, 0.2f, 0.12f, 0.08f};
            float v = 0.0f, norm = 0.0f;
            for (int i = 0; i < 6; ++i) {
                if ((float)part[i] * nyqRatio > 0.9f) break;
                v += amp[i] * std::sin(kTau * phase * (float)part[i]);
                norm += amp[i];
            }
            return norm > 0.0f ? v / norm : 0.0f;
        }
        default:
            return std::sin(kTau * phase);
    }
}

// --- filters ---------------------------------------------------------------

struct OnePole {
    float z = 0.0f;
    inline float lp(float x, float c) { z += c * (x - z); return z; }
    inline float hp(float x, float c) { z += c * (x - z); return x - z; }
};

// Cytomic (Andrew Simper) topology-preserving SVF: stable under fast cutoff
// modulation, which matters because an LFO sweeps it all day.
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
    void set(float cutoff, float res, float sr) {
        const float g = std::tan(kPi * clampf(cutoff, 10.0f, sr * 0.45f) / sr);
        k = 2.0f - 2.0f * clampf(res, 0.0f, 0.97f);
        if (k < 0.06f) k = 0.06f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void step(float v0, float& low, float& band, float& high) {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = v0 - k * v1 - v2;
    }
    inline float process(float v0, int type) {
        float lo, ba, hi;
        step(v0, lo, ba, hi);
        if (type == FilterBandPass) return ba * (1.0f + k);  // unity-ish peak
        if (type == FilterHighPass) return hi;
        return lo;
    }
};

// --- delays ----------------------------------------------------------------

// Fixed-capacity fractional delay. Allocated once for the worst-case setting so
// nothing in the audio path ever calls the allocator.
struct Delay {
    std::vector<float> buf;
    int w = 0;
    void init(int maxSamples) {
        buf.assign((size_t)std::max(4, maxSamples), 0.0f);
        w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void write(float x) {
        buf[(size_t)w] = x;
        if (++w >= (int)buf.size()) w = 0;
    }
    inline float read(float d) const {
        const int n = (int)buf.size();
        float fd = clampf(d, 1.0f, (float)(n - 2));
        float rp = (float)w - fd;
        while (rp < 0.0f) rp += (float)n;
        const int i0 = (int)rp;
        const float fr = rp - (float)i0;
        const int i1 = (i0 + 1 >= n) ? 0 : i0 + 1;
        return lerpf(buf[(size_t)i0], buf[(size_t)i1], fr);
    }
};

// Schroeder allpass - the reverb's input diffuser.
struct Allpass {
    Delay d;
    float len = 1.0f, g = 0.5f;
    void init(int maxSamples) { d.init(maxSamples); }
    inline float process(float x) {
        const float y = d.read(len);
        const float v = x + (-g) * y;
        d.write(v);
        return y + g * v;
    }
};

// Two-head granular pitch shifter. Only ever used inside the reverb's feedback
// path (shimmer), where the tail masks the window seams that make this cheap
// approach unusable on a dry signal.
struct PitchShift {
    Delay d;
    float phase = 0.0f;
    float win = 1024.0f;
    void init(int sr) {
        win = (float)sr * 0.075f;          // 75 ms grain
        d.init((int)(win * 2.5f) + 64);
    }
    void clear() { d.clear(); phase = 0.0f; }
    inline float process(float x, float ratio) {
        d.write(x);
        float out = 0.0f;
        for (int k = 0; k < 2; ++k) {
            float ph = phase + (k ? win * 0.5f : 0.0f);
            if (ph >= win) ph -= win;
            const float g = 0.5f * (1.0f - std::cos(kTau * ph / win));
            out += g * d.read(win - ph + 2.0f);
        }
        phase += ratio - 1.0f;
        if (phase >= win) phase -= win;
        if (phase < 0.0f) phase += win;
        return out;
    }
};

// 8-line feedback delay network with a normalized Hadamard mixing matrix. An
// FDN (rather than a Freeverb comb bank) because the whole point here is a
// 30-second tail: comb filters ring metallic long before that, an orthogonal
// matrix stays smooth.
struct Fdn {
    static constexpr int N = 8;
    Delay line[N];
    OnePole damp[N], cut[N];
    Allpass diff[4];
    Delay pre;
    PitchShift shift;
    float lenSamp[N] = {};
    float g[N] = {};
    float modPhase[N] = {};
    float sr = 22050.0f;
    float shiftState = 0.0f;

    // Prime-ish base lengths (ms). Mutually incommensurate so modes spread.
    static const float* baseMs() {
        static const float m[N] = {23.1f, 29.7f, 35.3f, 41.9f,
                                   47.3f, 53.7f, 61.1f, 67.9f};
        return m;
    }
    static constexpr float kMaxScale = 2.0f;

    void init(float sampleRate) {
        sr = sampleRate;
        for (int i = 0; i < N; ++i) {
            line[i].init((int)(baseMs()[i] * 0.001f * sr * kMaxScale) + 128);
            modPhase[i] = (float)i / (float)N;
        }
        static const float apMs[4] = {5.3f, 7.9f, 11.3f, 13.7f};
        for (int i = 0; i < 4; ++i) {
            diff[i].init((int)(apMs[i] * 0.001f * sr) + 32);
            diff[i].len = apMs[i] * 0.001f * sr;
        }
        pre.init((int)(0.35f * sr) + 32);
        shift.init((int)sr);
        clear();
    }
    void clear() {
        for (int i = 0; i < N; ++i) {
            line[i].clear();
            damp[i].z = cut[i].z = 0.0f;
        }
        for (int i = 0; i < 4; ++i) diff[i].d.clear();
        pre.clear();
        shift.clear();
        shiftState = 0.0f;
    }
    // Control-rate: resolve size + RT60 into per-line lengths and gains.
    void configure(float size, float decaySec, float diffusion) {
        const float scale = 0.35f + clampf(size, 0.0f, 1.0f) * (kMaxScale - 0.35f);
        const float rt60 = std::max(0.15f, decaySec);
        for (int i = 0; i < N; ++i) {
            lenSamp[i] = baseMs()[i] * 0.001f * sr * scale;
            const float sec = lenSamp[i] / sr;
            g[i] = std::pow(10.0f, -3.0f * sec / rt60);
            if (g[i] > 0.9995f) g[i] = 0.9995f;
        }
        for (int i = 0; i < 4; ++i) diff[i].g = 0.35f + 0.4f * clampf(diffusion, 0.0f, 1.0f);
    }
    // One stereo sample. `dampC`/`cutC` are one-pole coefficients, `mod` the
    // line-length wobble in samples (kills the last of the metallic ring).
    inline void step(float in, float dampC, float cutC, float mod, float shimmer,
                     float shiftRatio, float& outL, float& outR) {
        float x = in;
        for (int i = 0; i < 4; ++i) x = diff[i].process(x);
        if (shimmer > 0.0001f) x += shiftState * shimmer;

        float v[N];
        for (int i = 0; i < N; ++i) {
            modPhase[i] += 0.37f / sr * (1.0f + 0.13f * (float)i);
            if (modPhase[i] >= 1.0f) modPhase[i] -= 1.0f;
            const float wob = mod * std::sin(kTau * modPhase[i]);
            float y = line[i].read(lenSamp[i] + wob);
            y = damp[i].lp(y, dampC);
            y = cut[i].hp(y, cutC);
            v[i] = y * g[i];
        }
        // Fast Walsh-Hadamard transform, then 1/sqrt(8) -> orthogonal.
        for (int s = 1; s < N; s <<= 1)
            for (int i = 0; i < N; i += s << 1)
                for (int j = i; j < i + s; ++j) {
                    const float a = v[j], b = v[j + s];
                    v[j] = a + b;
                    v[j + s] = a - b;
                }
        constexpr float norm = 0.35355339f;  // 1/sqrt(8)
        for (int i = 0; i < N; ++i) line[i].write(x + v[i] * norm);

        outL = (v[0] + v[2] + v[4] + v[6]) * norm * 0.5f;
        outR = (v[1] + v[3] + v[5] + v[7]) * norm * 0.5f;
        if (shimmer > 0.0001f)
            shiftState = shift.process((outL + outR) * 0.5f, shiftRatio);
    }
};

// --- scales ----------------------------------------------------------------

struct ScaleDef { int n; int step[7]; };
const ScaleDef& scaleDef(int scale) {
    static const ScaleDef defs[ScaleCount] = {
        {7, {0, 2, 3, 5, 7, 8, 10}},   // natural minor
        {7, {0, 2, 4, 5, 7, 9, 11}},   // major
        {7, {0, 2, 3, 5, 7, 9, 10}},   // dorian
        {7, {0, 1, 3, 5, 7, 8, 10}},   // phrygian
        {5, {0, 3, 5, 7, 10, 0, 0}},   // minor pentatonic
        {6, {0, 2, 4, 6, 8, 10, 0}},   // whole tone
        {7, {0, 1, 2, 3, 4, 5, 6}},    // chromatic (see semitoneFromScale)
    };
    return defs[clampi(scale, 0, ScaleCount - 1)];
}

// Scale degree `i` (may exceed the scale, octaves wrap) -> semitones.
int semitoneFromScale(int scale, int i) {
    if (scale == ScaleChromatic) return i;
    const ScaleDef& d = scaleDef(scale);
    int oct = i / d.n;
    int idx = i % d.n;
    if (idx < 0) { idx += d.n; --oct; }
    return d.step[idx] + 12 * oct;
}

}  // namespace

// ---------------------------------------------------------------------------
// Name tables
// ---------------------------------------------------------------------------

const char* const* waveNames() {
    static const char* n[WaveCount] = {"Sine", "Triangle", "Saw", "Square",
                                       "Pulse", "FM", "Organ"};
    return n;
}
const char* const* lfoShapeNames() {
    static const char* n[LfoShapeCount] = {"Sine", "Triangle", "Ramp", "Square",
                                            "Sample & hold", "Smooth random"};
    return n;
}
const char* const* filterNames() {
    static const char* n[FilterTypeCount] = {"Low pass", "Band pass", "High pass"};
    return n;
}
const char* const* bellNames() {
    static const char* n[BellTimbreCount] = {"Bell (FM)", "Pluck (string)",
                                              "Glass"};
    return n;
}
const char* const* scaleNames() {
    static const char* n[ScaleCount] = {"Minor",      "Major",      "Dorian",
                                         "Phrygian",   "Pentatonic", "Whole tone",
                                         "Chromatic"};
    return n;
}
const char* const* modSourceNames() {
    static const char* n[ModSrcCount] = {"-", "LFO 1", "LFO 2", "LFO 3", "Arc",
                                          "Random"};
    return n;
}
const char* const* modTargetNames() {
    static const char* n[ModDstTargetCount] = {
        "-", "Filter cutoff", "Resonance", "Pitch", "Layer 1 level",
        "Layer 2 level", "Layer 3 level", "Layer 4 level", "Air level",
        "Bell density", "Chorus depth", "Delay mix", "Reverb mix", "Shimmer",
        "Drive", "Width"};
    return n;
}

// ---------------------------------------------------------------------------
// Musical helpers
// ---------------------------------------------------------------------------

float noteHz(const Params& p, int midi) {
    return p.tuning * std::pow(2.0f, (float)(midi - 69) / 12.0f);
}
float barSeconds(const Params& p) {
    const float bpm = std::max(10.0f, p.bpm);
    return (float)std::max(1, p.beatsPerBar) * 60.0f / bpm;
}
float progressionBars(const Params& p) {
    float bars = 0.0f;
    for (int i = 0; i < std::max(1, std::min(p.stepCount, kMaxSteps)); ++i)
        bars += std::max(0.25f, p.steps[i].bars);
    return bars;
}
const char* noteName(int midi) {
    static const char* names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
    static char buf[16];
    midi = clampi(midi, 0, 200);
    const int n = midi % 12;
    std::snprintf(buf, sizeof(buf), "%s%d", names[n], midi / 12 - 1);
    return buf;
}
const char* delayDivName(int div) {
    static const char* n[6] = {"Free (s)", "1/4",      "1/2",
                               "1 bar",    "1.5 bars", "2 bars"};
    return n[std::max(0, std::min(div, 5))];
}
float delaySeconds(const Params& p) {
    const float bar = barSeconds(p);
    switch (p.fx.delayDiv) {
        case 1: return bar / (float)std::max(1, p.beatsPerBar);
        case 2: return bar * 0.5f;
        case 3: return bar;
        case 4: return bar * 1.5f;
        case 5: return bar * 2.0f;
        default: return clampf(p.fx.delayTime, 0.02f, 4.0f);
    }
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

Params::Params() {
    // A patch that already sounds like something the moment the window opens:
    // a sine sub plus a soft detuned saw over an Am -> Fmaj9-ish move, with a
    // long reverb. Everything else is off so the first knob you turn is audible.
    layers[0].on = true;
    layers[0].wave = WaveSine;
    layers[0].octave = 0;
    layers[0].level = 0.55f;
    layers[0].tone = 0.5f;
    layers[0].notes = 0x01;  // root only - the sub
    layers[0].unison = 1;
    layers[0].attack = 3.0f;

    layers[1].on = true;
    layers[1].wave = WaveSaw;
    layers[1].octave = 1;
    layers[1].level = 0.28f;
    layers[1].tone = 0.42f;
    layers[1].unison = 3;
    layers[1].detune = 14.0f;
    layers[1].spread = 0.8f;
    layers[1].attack = 6.0f;
    layers[1].release = 7.0f;
    layers[1].drift = 8.0f;

    layers[2].wave = WaveOrgan;
    layers[2].octave = 2;
    layers[2].level = 0.18f;
    layers[2].notes = 0x1E;
    layers[3].wave = WaveTriangle;
    layers[3].octave = -1;
    layers[3].level = 0.2f;
    layers[3].notes = 0x01;

    steps[0].count = 4;
    steps[0].notes[0] = 0;  steps[0].notes[1] = 7;
    steps[0].notes[2] = 12; steps[0].notes[3] = 15;
    steps[0].bars = 8.0f;
    steps[1].count = 4;
    steps[1].notes[0] = -4; steps[1].notes[1] = 3;
    steps[1].notes[2] = 12; steps[1].notes[3] = 14;
    steps[1].bars = 8.0f;
    for (int i = 2; i < kMaxSteps; ++i) steps[i] = steps[i % 2];
    stepCount = 2;

    lfos[0].rate = 0.05f; lfos[0].shape = LfoSine;   lfos[0].depth = 1.0f;
    lfos[1].rate = 0.13f; lfos[1].shape = LfoSmooth; lfos[1].depth = 1.0f;
    lfos[2].rate = 0.5f;  lfos[2].shape = LfoTriangle; lfos[2].depth = 1.0f;

    mods[0] = {ModSrcLfo1, ModDstCutoff, 0.35f};
    mods[1] = {ModSrcLfo2, ModDstLevel2, 0.25f};
    mods[2] = {ModSrcArc, ModDstCutoff, 0.3f};
}

// ---------------------------------------------------------------------------
// Synth
// ---------------------------------------------------------------------------

struct Synth::Impl {
    // --- oscillator bank ---------------------------------------------------
    struct Unison {
        float phase = 0.0f;
        float cents = 0.0f;      // static detune within the stack
        float amp = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        float driftVal = 0.0f, driftFrom = 0.0f, driftTo = 0.0f, driftPos = 0.0f;
        float driftRate = 0.08f;
        Rng rng;
        OnePole tone;
    };
    struct NoteSlot {
        float freq = 0.0f, target = 0.0f;
        float gain = 0.0f, gTarget = 0.0f;
        bool started = false;
        Unison uni[kMaxUnison];
    };
    NoteSlot notes[kMaxLayers][kMaxChordNotes];

    // --- air bed -----------------------------------------------------------
    Rng noiseRng;
    Svf noiseSvfL, noiseSvfR;
    float noisePhase = 0.0f, noiseFrom = 0.0f, noiseTo = 0.0f, noisePos = 0.0f;
    Rng noiseMotionRng;

    // --- bells -------------------------------------------------------------
    struct BellVoice {
        bool on = false;
        int timbre = 0;
        float freq = 0.0f, phase = 0.0f, phase2 = 0.0f;
        float env = 0.0f, envCoef = 0.0f, env2 = 0.0f, env2Coef = 0.0f;
        float amp = 0.0f, panL = 1.0f, panR = 1.0f, bright = 0.5f;
        // Karplus-Strong string (BellPluck).
        std::vector<float> ks;
        int ksLen = 0, ksIdx = 0;
        float ksLp = 0.0f, ksFb = 0.99f;
    };
    BellVoice bells[kMaxBellVoices];
    Rng bellRng;
    double nextBell = 0.0;

    // --- effects -----------------------------------------------------------
    Svf filtL, filtR;
    Delay chorus;
    float chorusPhase = 0.0f;
    Delay tape;
    float wowPhase = 0.0f, flutPhase = 0.0f;
    Rng hissRng;
    Delay delayL, delayR;
    OnePole delDampL, delDampR;
    Fdn revL, revR;
    OnePole lowCutL, lowCutR, highCutL, highCutR, tiltL, tiltR, monoL, monoR;
    float limEnv = 0.0f, limGain = 1.0f;

    // --- transport / modulation -------------------------------------------
    double samplePos = 0.0;
    float sr = 22050.0f;
    float lfoPhase[kNumLfos] = {};
    float lfoVal[kNumLfos] = {};
    float lfoSh[kNumLfos] = {};
    float lfoFrom[kNumLfos] = {}, lfoTo[kNumLfos] = {};
    Rng lfoRng[kNumLfos];
    Rng randRng;
    float randFrom = 0.0f, randTo = 0.0f, randPos = 0.0f, randVal = 0.0f;

    // Effective (post-modulation) values, refreshed once per control block.
    struct Eff {
        float layerLevel[kMaxLayers] = {};
        float pitchCents = 0.0f;
        float cutoff = 1000.0f, res = 0.2f;
        float noiseLevel = 0.0f;
        float bellDensity = 0.0f;
        float chorusDepth = 0.0f, delayMix = 0.0f, revMix = 0.0f;
        float shimmer = 0.0f, drive = 0.0f, width = 1.0f;
    } eff;

    explicit Impl(float sampleRate) : sr(sampleRate) {
        chorus.init((int)(0.08f * sr) + 64);
        tape.init((int)(0.08f * sr) + 64);
        delayL.init((int)(4.2f * sr) + 64);
        delayR.init((int)(4.2f * sr) + 64);
        revL.init(sr);
        revR.init(sr);
        for (BellVoice& b : bells) b.ks.assign((size_t)(sr / 30.0f) + 8, 0.0f);
    }
};

Synth::Synth(const Params& p) : p_(p) {
    d_.reset(new Impl((float)std::max(8000, p.sampleRate)));
    reset();
}
Synth::~Synth() = default;

void Synth::setParams(const Params& p) {
    const int keepRate = p_.sampleRate;
    p_ = p;
    p_.sampleRate = keepRate;  // structural: a new rate needs a new Synth
}

void Synth::reset() {
    Impl& d = *d_;
    d.samplePos = 0.0;
    d.nextBell = 0.0;
    d.limEnv = 0.0f;
    d.limGain = 1.0f;

    for (int l = 0; l < kMaxLayers; ++l)
        for (int n = 0; n < kMaxChordNotes; ++n) {
            Impl::NoteSlot& s = d.notes[l][n];
            s.freq = s.target = 0.0f;
            s.gain = s.gTarget = 0.0f;
            s.started = false;
            for (int u = 0; u < kMaxUnison; ++u) {
                Impl::Unison& v = s.uni[u];
                v.rng.seed(mix32(p_.seed, (uint32_t)(l * 977 + n * 61 + u * 7 + 1)));
                // A random start phase per voice: identical phases make a stack
                // of detuned saws start as one loud click.
                v.phase = v.rng.uni();
                v.driftFrom = v.rng.bi();
                v.driftTo = v.rng.bi();
                v.driftPos = v.rng.uni();
                v.driftRate = 0.03f + 0.12f * v.rng.uni();
                v.driftVal = v.driftFrom;
                v.tone.z = 0.0f;
            }
        }

    d.noiseRng.seed(mix32(p_.seed, 0x4E01u));
    d.noiseMotionRng.seed(mix32(p_.seed, 0x4E02u));
    d.noiseSvfL.ic1 = d.noiseSvfL.ic2 = d.noiseSvfR.ic1 = d.noiseSvfR.ic2 = 0.0f;
    d.noiseFrom = d.noiseMotionRng.bi();
    d.noiseTo = d.noiseMotionRng.bi();
    d.noisePos = 0.0f;

    d.bellRng.seed(mix32(p_.seed, 0xBE11u));
    for (Impl::BellVoice& b : d.bells) {
        b.on = false;
        std::fill(b.ks.begin(), b.ks.end(), 0.0f);
    }

    d.hissRng.seed(mix32(p_.seed, 0x4155u));
    d.randRng.seed(mix32(p_.seed, 0x5A5Au));
    d.randFrom = d.randRng.bi();
    d.randTo = d.randRng.bi();
    d.randPos = 0.0f;
    for (int i = 0; i < kNumLfos; ++i) {
        d.lfoRng[i].seed(mix32(p_.seed, (uint32_t)(0x1F0u + i)));
        d.lfoPhase[i] = 0.0f;
        d.lfoFrom[i] = d.lfoRng[i].bi();
        d.lfoTo[i] = d.lfoRng[i].bi();
        d.lfoSh[i] = d.lfoFrom[i];
        d.lfoVal[i] = 0.0f;
    }

    d.chorus.clear();
    d.chorusPhase = 0.0f;
    d.tape.clear();
    d.wowPhase = d.flutPhase = 0.0f;
    d.delayL.clear();
    d.delayR.clear();
    d.delDampL.z = d.delDampR.z = 0.0f;
    d.revL.clear();
    d.revR.clear();
    d.filtL.ic1 = d.filtL.ic2 = d.filtR.ic1 = d.filtR.ic2 = 0.0f;
    d.lowCutL.z = d.lowCutR.z = d.highCutL.z = d.highCutR.z = 0.0f;
    d.tiltL.z = d.tiltR.z = d.monoL.z = d.monoR.z = 0.0f;
}

double Synth::timeSec() const { return d_->samplePos / (double)d_->sr; }

namespace {

// LFO value in -1..+1 for one control block.
float lfoStep(Synth::Impl& d, const Params& p, int i, float dt) {
    const Lfo& L = p.lfos[i];
    const float hz = L.sync ? std::max(0.0f, L.rate) / barSeconds(p)
                            : std::max(0.0f, L.rate);
    const float prev = d.lfoPhase[i];
    d.lfoPhase[i] += hz * dt;
    bool wrapped = false;
    while (d.lfoPhase[i] >= 1.0f) { d.lfoPhase[i] -= 1.0f; wrapped = true; }
    const float ph = d.lfoPhase[i];
    (void)prev;
    float v = 0.0f;
    switch (L.shape) {
        case LfoTriangle: v = 4.0f * std::fabs(ph - 0.5f) - 1.0f; break;
        case LfoRamp:     v = 2.0f * ph - 1.0f; break;
        case LfoSquare:   v = ph < 0.5f ? 1.0f : -1.0f; break;
        case LfoSampleHold:
            if (wrapped) d.lfoSh[i] = d.lfoRng[i].bi();
            v = d.lfoSh[i];
            break;
        case LfoSmooth: {
            if (wrapped) { d.lfoFrom[i] = d.lfoTo[i]; d.lfoTo[i] = d.lfoRng[i].bi(); }
            // smoothstep between two draws - no corners, so it modulates a
            // filter without stepping
            const float t = ph * ph * (3.0f - 2.0f * ph);
            v = lerpf(d.lfoFrom[i], d.lfoTo[i], t);
            break;
        }
        default: v = std::sin(kTau * ph); break;
    }
    return v * clampf(L.depth, 0.0f, 1.0f);
}

// Piece-long arc envelope, 0..1, linear between breakpoints. In the live
// preview the piece length still defines the shape, it just repeats.
float arcValue(const Params& p, double tSec) {
    const float len = std::max(1.0f, p.lengthSec);
    float t = (float)std::fmod(tSec, (double)len) / len;
    t = clampf(t, 0.0f, 1.0f) * (float)(kArcPoints - 1);
    const int i = std::min((int)t, kArcPoints - 2);
    return clampf(lerpf(p.arc[i], p.arc[i + 1], t - (float)i), 0.0f, 1.0f);
}

}  // namespace

void Synth::render(float* out, int frames) {
    Impl& d = *d_;
    const Params& p = p_;
    const float sr = d.sr;
    const float bar = barSeconds(p);
    const float totalBars = progressionBars(p);
    const int stepCount = std::max(1, std::min(p.stepCount, kMaxSteps));
    const float nyq = sr * 0.5f;

    int done = 0;
    while (done < frames) {
        const int block = std::min(kCtrl, frames - done);
        const float dt = (float)block / sr;
        const double tSec = d.samplePos / (double)sr;

        // ---- modulation sources ------------------------------------------
        for (int i = 0; i < kNumLfos; ++i) d.lfoVal[i] = lfoStep(d, p, i, dt);
        d.randPos += dt * 0.11f;
        while (d.randPos >= 1.0f) {
            d.randPos -= 1.0f;
            d.randFrom = d.randTo;
            d.randTo = d.randRng.bi();
        }
        {
            const float t = d.randPos * d.randPos * (3.0f - 2.0f * d.randPos);
            d.randVal = lerpf(d.randFrom, d.randTo, t);
        }
        const float arc = arcValue(p, tSec);

        float acc[ModDstTargetCount] = {};
        for (int m = 0; m < kNumMods; ++m) {
            const ModRow& r = p.mods[m];
            if (r.src == ModSrcNone || r.dst == ModDstNone) continue;
            if (r.dst < 0 || r.dst >= ModDstTargetCount) continue;
            float s = 0.0f;
            switch (r.src) {
                case ModSrcLfo1: s = d.lfoVal[0]; break;
                case ModSrcLfo2: s = d.lfoVal[1]; break;
                case ModSrcLfo3: s = d.lfoVal[2]; break;
                case ModSrcArc: s = arc; break;
                case ModSrcRandom: s = d.randVal; break;
                default: break;
            }
            acc[r.dst] += s * clampf(r.amount, -1.0f, 1.0f);
        }

        Impl::Eff& e = d.eff;
        for (int l = 0; l < kMaxLayers; ++l)
            e.layerLevel[l] = clampf(p.layers[l].level *
                                         (1.0f + acc[ModDstLevel1 + l]),
                                     0.0f, 2.0f);
        e.pitchCents = acc[ModDstPitch] * 200.0f;
        e.cutoff = clampf(p.filter.cutoff * std::pow(2.0f, acc[ModDstCutoff] * 3.0f),
                          30.0f, nyq * 0.9f);
        e.res = clampf(p.filter.res + acc[ModDstResonance] * 0.7f, 0.0f, 0.97f);
        e.noiseLevel = clampf(p.noise.level * (1.0f + acc[ModDstNoise]), 0.0f, 2.0f);
        e.bellDensity = clampf(p.bells.density * (1.0f + acc[ModDstBellDensity]),
                               0.0f, 240.0f);
        e.chorusDepth = clampf(p.fx.chorusDepth + acc[ModDstChorusDepth] * 0.5f,
                               0.0f, 1.0f);
        e.delayMix = clampf(p.fx.delayMix + acc[ModDstDelayMix] * 0.5f, 0.0f, 1.0f);
        e.revMix = clampf(p.fx.revMix + acc[ModDstReverbMix] * 0.5f, 0.0f, 1.0f);
        e.shimmer = clampf(p.fx.shimmer + acc[ModDstShimmer] * 0.5f, 0.0f, 1.0f);
        e.drive = clampf(p.fx.drive + acc[ModDstDrive] * 0.5f, 0.0f, 1.0f);
        e.width = clampf(p.fx.width + acc[ModDstWidth] * 0.5f, 0.0f, 2.0f);

        // ---- chord / progression -----------------------------------------
        const float barPos = (float)std::fmod(tSec / (double)bar, (double)totalBars);
        int stepIdx = 0;
        {
            float accBars = 0.0f;
            for (int i = 0; i < stepCount; ++i) {
                const float b = std::max(0.25f, p.steps[i].bars);
                if (barPos < accBars + b) { stepIdx = i; break; }
                accBars += b;
                stepIdx = i;
            }
        }
        const Step& st = p.steps[stepIdx];
        const int stNotes = clampi(st.count, 0, kMaxChordNotes);

        const float glide = std::max(0.01f, p.glide);
        const float glideC = 1.0f - std::exp(-dt / glide);

        for (int l = 0; l < kMaxLayers; ++l) {
            const Layer& L = p.layers[l];
            for (int n = 0; n < kMaxChordNotes; ++n) {
                Impl::NoteSlot& s = d.notes[l][n];
                const bool present =
                    L.on && n < stNotes && (L.notes & (1 << n)) != 0;
                if (present) {
                    const int midi = p.rootNote + st.notes[n] + 12 * L.octave + L.semi;
                    s.target = noteHz(p, midi);
                    if (!s.started) { s.freq = s.target; s.started = true; }
                }
                s.gTarget = present ? 1.0f : 0.0f;
                s.freq += (s.target - s.freq) * glideC;
                const float envT = s.gTarget > s.gain ? std::max(0.01f, L.attack)
                                                      : std::max(0.01f, L.release);
                s.gain += (s.gTarget - s.gain) * (1.0f - std::exp(-dt / envT));

                // Unison layout: symmetric cents fan, matching stereo fan.
                for (int u = 0; u < kMaxUnison; ++u) {
                    Impl::Unison& v = s.uni[u];
                    const int uni = clampi(L.unison, 1, kMaxUnison);
                    if (u >= uni) { v.amp = 0.0f; continue; }
                    const float t = uni > 1 ? ((float)u / (float)(uni - 1)) * 2.0f - 1.0f
                                            : 0.0f;
                    v.cents = L.fine + t * L.detune;
                    v.amp = 1.0f / std::sqrt((float)uni);
                    const float pan = clampf(L.pan + t * L.spread, -1.0f, 1.0f);
                    // constant-power pan
                    const float a = (pan + 1.0f) * 0.25f * kPi;
                    v.panL = std::cos(a);
                    v.panR = std::sin(a);
                    // slow independent drift keeps a long chord alive
                    v.driftPos += dt * v.driftRate;
                    while (v.driftPos >= 1.0f) {
                        v.driftPos -= 1.0f;
                        v.driftFrom = v.driftTo;
                        v.driftTo = v.rng.bi();
                    }
                    const float dtn = v.driftPos * v.driftPos * (3.0f - 2.0f * v.driftPos);
                    v.driftVal = lerpf(v.driftFrom, v.driftTo, dtn);
                }
            }
        }

        // ---- filters / effect coefficients -------------------------------
        d.filtL.set(e.cutoff, e.res, sr);
        d.filtR.set(e.cutoff, e.res, sr);

        // air bed motion
        d.noisePos += dt * std::max(0.001f, p.noise.motionRate);
        while (d.noisePos >= 1.0f) {
            d.noisePos -= 1.0f;
            d.noiseFrom = d.noiseTo;
            d.noiseTo = d.noiseMotionRng.bi();
        }
        {
            const float t = d.noisePos * d.noisePos * (3.0f - 2.0f * d.noisePos);
            const float w = lerpf(d.noiseFrom, d.noiseTo, t) * p.noise.motionDepth;
            const float c = clampf(p.noise.cutoff * std::pow(2.0f, w * 2.0f),
                                   40.0f, nyq * 0.85f);
            d.noiseSvfL.set(c, p.noise.res, sr);
            d.noiseSvfR.set(c * 1.06f, p.noise.res, sr);
        }

        const float delSec = delaySeconds(p);
        const float delSamp = clampf(delSec * sr, 8.0f, (float)sr * 4.0f);
        const float delDampC =
            clampf(1.0f - p.fx.delayDamp, 0.02f, 1.0f);  // one-pole coefficient
        const float fb = clampf(p.fx.delayFeedback, 0.0f, 0.98f);

        d.revL.configure(p.fx.revSize, p.fx.revDecay, p.fx.revDiffusion);
        d.revR.configure(p.fx.revSize * 0.98f, p.fx.revDecay, p.fx.revDiffusion);
        const float revDampC = clampf(1.0f - p.fx.revDamp * 0.95f, 0.03f, 1.0f);
        const float revCutC =
            clampf(kTau * clampf(p.fx.revLowCut, 10.0f, 800.0f) / sr, 0.0005f, 0.5f);
        const float revMod = 0.6f + 3.0f * p.fx.revDiffusion;
        const float shiftRatio = std::pow(2.0f, (float)p.fx.shimmerSemi / 12.0f);
        const float preSamp = clampf(p.fx.revPredelay * sr, 1.0f, 0.3f * sr);

        const float lowCutC =
            clampf(kTau * clampf(p.fx.lowCut, 10.0f, 500.0f) / sr, 0.0005f, 0.5f);
        const float highCutC =
            clampf(kTau * clampf(p.fx.highCut, 500.0f, nyq * 0.9f) / sr, 0.001f, 0.9f);
        const float tiltC = clampf(kTau * 700.0f / sr, 0.001f, 0.9f);
        const float tiltLo = dbGain(-p.fx.tilt * 0.5f);
        const float tiltHi = dbGain(p.fx.tilt * 0.5f);
        const float monoC =
            clampf(kTau * clampf(p.fx.monoBelow, 20.0f, 600.0f) / sr, 0.001f, 0.5f);

        const float driveG = 1.0f + e.drive * 14.0f;
        const float driveNorm = 1.0f / std::tanh(driveG * 0.7f);

        // ---- bell scheduling ---------------------------------------------
        if (p.bells.on && e.bellDensity > 0.01f) {
            const double blockEnd = tSec + (double)dt;
            while (d.nextBell <= blockEnd) {
                if (d.nextBell >= tSec) {
                    // spawn on a free voice (steal the quietest otherwise)
                    int slot = -1;
                    float weakest = 1e9f;
                    for (int i = 0; i < kMaxBellVoices; ++i) {
                        if (!d.bells[i].on) { slot = i; break; }
                        const float lvl = d.bells[i].env;
                        if (lvl < weakest) { weakest = lvl; slot = i; }
                    }
                    Impl::BellVoice& b = d.bells[slot];
                    int semi;
                    if (p.bells.chordLock && stNotes > 0) {
                        semi = st.notes[(int)(d.bellRng.uni() * (float)stNotes) % stNotes];
                        // lift by whole octaves until it is inside the range
                        const int steps = std::max(1, p.bells.range) / 12 + 1;
                        semi += 12 * (int)(d.bellRng.uni() * (float)steps);
                    } else {
                        const int deg = (int)(d.bellRng.uni() * 12.0f);
                        semi = semitoneFromScale(p.scale, deg);
                        while (semi > std::max(1, p.bells.range)) semi -= 12;
                    }
                    const int midi = p.rootNote + 12 * p.bells.octave + semi;
                    b.on = true;
                    b.timbre = p.bells.timbre;
                    b.freq = noteHz(p, midi);
                    b.phase = 0.0f;
                    b.phase2 = 0.0f;
                    b.bright = clampf(p.bells.bright, 0.0f, 1.0f);
                    b.amp = 0.55f + 0.45f * d.bellRng.uni();
                    const float pan = d.bellRng.bi() * clampf(p.bells.spread, 0.0f, 1.0f);
                    const float a = (pan + 1.0f) * 0.25f * kPi;
                    b.panL = std::cos(a);
                    b.panR = std::sin(a);
                    const float dec = std::max(0.05f, p.bells.decay);
                    b.env = 1.0f;
                    b.envCoef = std::exp(-1.0f / (dec * sr));
                    b.env2 = 1.0f;
                    b.env2Coef = std::exp(-1.0f / (dec * 0.35f * sr));
                    if (b.timbre == BellPluck) {
                        b.ksLen = std::max(2, std::min((int)(sr / b.freq),
                                                       (int)b.ks.size() - 1));
                        for (int i = 0; i < b.ksLen; ++i) b.ks[(size_t)i] = d.bellRng.bi();
                        b.ksIdx = 0;
                        b.ksLp = 0.0f;
                        const float period = (float)b.ksLen / sr;
                        b.ksFb = std::pow(10.0f, -3.0f * period / dec);
                    }
                }
                // next event: exponential-ish spacing, then quantized to the grid
                const float mean = 60.0f / std::max(0.02f, e.bellDensity);
                double next = d.nextBell + (double)(mean * (0.45f + 1.1f * d.bellRng.uni()));
                if (p.bells.quantize > 0) {
                    const double grid = p.bells.quantize == 2
                                            ? (double)bar
                                            : (double)bar / std::max(1, p.beatsPerBar);
                    next = std::ceil(next / grid) * grid;
                    if (next <= d.nextBell) next += grid;
                }
                d.nextBell = next;
            }
        } else {
            d.nextBell = tSec + (double)dt;
        }

        // ---- per-sample loop ---------------------------------------------
        const float bellLevel = clampf(p.bells.level, 0.0f, 1.0f);
        const float hiss = clampf(p.fx.hiss, 0.0f, 1.0f) * 0.02f;
        const float wowHz = 0.7f, flutHz = 6.3f;
        const float chorusBase = 0.012f * sr;
        const float chorusDepthS = e.chorusDepth * 0.006f * sr;
        const float tapeBase = 0.02f * sr;
        const float wowDepth = p.fx.wow * 0.008f * sr;
        const float flutDepth = p.fx.flutter * 0.0012f * sr;

        for (int i = 0; i < block; ++i) {
            float dryL = 0.0f, dryR = 0.0f;

            // oscillator stacks
            for (int l = 0; l < kMaxLayers; ++l) {
                const Layer& L = p.layers[l];
                if (!L.on) continue;
                const float lvl = e.layerLevel[l];
                if (lvl <= 0.0001f) continue;
                const float toneC =
                    clampf(kTau * (80.0f * std::pow(150.0f, clampf(L.tone, 0.0f, 1.0f))) / sr,
                           0.002f, 0.98f);
                const int uni = clampi(L.unison, 1, kMaxUnison);
                for (int n = 0; n < kMaxChordNotes; ++n) {
                    Impl::NoteSlot& s = d.notes[l][n];
                    if (s.gain <= 0.0002f) continue;
                    const float g = s.gain * lvl;
                    for (int u = 0; u < uni; ++u) {
                        Impl::Unison& v = s.uni[u];
                        if (v.amp <= 0.0f) continue;
                        const float f = s.freq *
                            centsMul(v.cents + e.pitchCents + v.driftVal * L.drift);
                        const float inc = clampf(f / sr, 0.0f, 0.49f);
                        v.phase += inc;
                        if (v.phase >= 1.0f) v.phase -= 1.0f;
                        float o = oscSample(L.wave, v.phase, inc, L.pw, L.fmRatio,
                                            L.fmIndex, f / nyq);
                        o = v.tone.lp(o, toneC);
                        const float a = o * g * v.amp;
                        dryL += a * v.panL;
                        dryR += a * v.panR;
                    }
                }
            }

            // air bed
            if (p.noise.on && e.noiseLevel > 0.0001f) {
                const float n1 = d.noiseRng.bi();
                const float n2 = d.noiseRng.bi();
                const float st2 = clampf(p.noise.stereo, 0.0f, 1.0f);
                const float aL = d.noiseSvfL.process(n1, FilterBandPass);
                const float aR = d.noiseSvfR.process(lerpf(n1, n2, st2), FilterBandPass);
                dryL += aL * e.noiseLevel * 0.7f;
                dryR += aR * e.noiseLevel * 0.7f;
            }

            // drive -> master filter (the drone path only; bells stay bright)
            if (e.drive > 0.0005f) {
                const float wetL = std::tanh(dryL * driveG) * driveNorm;
                const float wetR = std::tanh(dryR * driveG) * driveNorm;
                const float m = clampf(p.fx.driveMix, 0.0f, 1.0f);
                dryL = lerpf(dryL, wetL, m);
                dryR = lerpf(dryR, wetR, m);
            }
            float busL = d.filtL.process(dryL, p.filter.type);
            float busR = d.filtR.process(dryR, p.filter.type);

            // bells (post-filter, with their own sends)
            float bellL = 0.0f, bellR = 0.0f;
            if (p.bells.on) {
                for (int bi = 0; bi < kMaxBellVoices; ++bi) {
                    Impl::BellVoice& b = d.bells[bi];
                    if (!b.on) continue;
                    float o = 0.0f;
                    if (b.timbre == BellPluck) {
                        const int nxt = (b.ksIdx + 1) % std::max(1, b.ksLen);
                        const float avg = 0.5f * (b.ks[(size_t)b.ksIdx] + b.ks[(size_t)nxt]);
                        b.ksLp += (0.35f + 0.6f * b.bright) * (avg - b.ksLp);
                        o = b.ks[(size_t)b.ksIdx];
                        b.ks[(size_t)b.ksIdx] = b.ksLp * b.ksFb;
                        b.ksIdx = nxt;
                        o *= b.env;
                    } else if (b.timbre == BellGlass) {
                        b.phase += b.freq / sr;
                        if (b.phase >= 1.0f) b.phase -= 1.0f;
                        b.phase2 += b.freq * 2.01f / sr;
                        if (b.phase2 >= 1.0f) b.phase2 -= 1.0f;
                        o = (std::sin(kTau * b.phase) +
                             (0.25f + 0.5f * b.bright) * std::sin(kTau * b.phase2)) *
                            0.7f * b.env;
                    } else {
                        b.phase += b.freq / sr;
                        if (b.phase >= 1.0f) b.phase -= 1.0f;
                        const float idx = (1.5f + 4.0f * b.bright) * b.env2;
                        o = std::sin(kTau * b.phase +
                                     idx * std::sin(kTau * b.phase * 3.51f)) * b.env;
                    }
                    b.env *= b.envCoef;
                    b.env2 *= b.env2Coef;
                    if (b.env < 0.0002f) b.on = false;
                    bellL += o * b.amp * b.panL;
                    bellR += o * b.amp * b.panR;
                }
                bellL *= bellLevel;
                bellR *= bellLevel;
                busL += bellL;
                busR += bellR;
            }

            // ensemble chorus: three modulated taps
            if (p.fx.chorusMix > 0.0005f && e.chorusDepth > 0.0005f) {
                d.chorusPhase += p.fx.chorusRate / sr;
                if (d.chorusPhase >= 1.0f) d.chorusPhase -= 1.0f;
                d.chorus.write((busL + busR) * 0.5f);
                float tap[3];
                for (int t = 0; t < 3; ++t) {
                    const float ph = wrap01(d.chorusPhase + (float)t / 3.0f);
                    const float m = std::sin(kTau * ph);
                    tap[t] = d.chorus.read(chorusBase * (1.0f + 0.35f * (float)t) +
                                           chorusDepthS * m + 4.0f);
                }
                const float sp = clampf(p.fx.chorusSpread, 0.0f, 1.0f);
                const float wL = tap[0] + tap[2] * (1.0f - 0.5f * sp);
                const float wR = tap[1] + tap[2] * (1.0f - 0.5f * sp);
                const float m = clampf(p.fx.chorusMix, 0.0f, 1.0f);
                busL = lerpf(busL, wL * 0.7f, m);
                busR = lerpf(busR, wR * 0.7f, m);
            }

            // tape: wow + flutter on a short line, plus hiss
            if (p.fx.wow > 0.0005f || p.fx.flutter > 0.0005f) {
                d.wowPhase += wowHz / sr;
                if (d.wowPhase >= 1.0f) d.wowPhase -= 1.0f;
                d.flutPhase += flutHz / sr;
                if (d.flutPhase >= 1.0f) d.flutPhase -= 1.0f;
                const float m = wowDepth * std::sin(kTau * d.wowPhase) +
                                flutDepth * std::sin(kTau * d.flutPhase);
                d.tape.write((busL + busR) * 0.5f);
                const float t = d.tape.read(tapeBase + m + 4.0f);
                // a mono line moved into both channels: the wobble is the point,
                // the width comes from the chorus above
                busL = lerpf(busL, t, 0.5f);
                busR = lerpf(busR, t, 0.5f);
            }
            if (hiss > 0.0f) {
                busL += d.hissRng.bi() * hiss;
                busR += d.hissRng.bi() * hiss;
            }

            // echo
            if (e.delayMix > 0.0005f || fb > 0.0005f) {
                const float inL = busL + bellL * p.bells.delaySend;
                const float inR = busR + bellR * p.bells.delaySend;
                const float wL = d.delayL.read(delSamp);
                const float wR = d.delayR.read(delSamp);
                const float dL = d.delDampL.lp(wL, delDampC);
                const float dR = d.delDampR.lp(wR, delDampC);
                if (p.fx.pingpong) {
                    d.delayL.write(inL + dR * fb);
                    d.delayR.write(inR + dL * fb);
                } else {
                    d.delayL.write(inL + dL * fb);
                    d.delayR.write(inR + dR * fb);
                }
                busL += dL * e.delayMix;
                busR += dR * e.delayMix;
            }

            // reverb
            if (e.revMix > 0.0005f) {
                const float inL = busL + bellL * p.bells.revSend;
                const float inR = busR + bellR * p.bells.revSend;
                d.revL.pre.write(inL);
                d.revR.pre.write(inR);
                const float pL = d.revL.pre.read(preSamp);
                const float pR = d.revR.pre.read(preSamp);
                float aL, aR, bL2, bR2;
                d.revL.step(pL, revDampC, revCutC, revMod, e.shimmer, shiftRatio,
                            aL, aR);
                d.revR.step(pR, revDampC, revCutC, revMod, e.shimmer, shiftRatio,
                            bL2, bR2);
                const float w = clampf(p.fx.revWidth, 0.0f, 1.0f);
                float wetL = lerpf((aL + bL2) * 0.5f, aL + bL2 * 0.3f, w);
                float wetR = lerpf((aR + bR2) * 0.5f, bR2 + aR * 0.3f, w);
                // equal-power dry/wet so a mix sweep keeps the level
                const float mx = clampf(e.revMix, 0.0f, 1.0f);
                const float gw = std::sin(mx * kPi * 0.5f) * 1.4f;
                const float gd = std::cos(mx * kPi * 0.5f);
                busL = busL * gd + wetL * gw;
                busR = busR * gd + wetR * gw;
            }

            // tone shaping
            busL = d.lowCutL.hp(busL, lowCutC);
            busR = d.lowCutR.hp(busR, lowCutC);
            busL = d.highCutL.lp(busL, highCutC);
            busR = d.highCutR.lp(busR, highCutC);
            if (std::fabs(p.fx.tilt) > 0.01f) {
                const float loL = d.tiltL.lp(busL, tiltC);
                const float loR = d.tiltR.lp(busR, tiltC);
                busL = loL * tiltLo + (busL - loL) * tiltHi;
                busR = loR * tiltLo + (busR - loR) * tiltHi;
            }

            // stereo width, with the bass summed to mono (kind to a TV speaker
            // and to the PS2's own mixdown)
            {
                const float mid = (busL + busR) * 0.5f;
                float side = (busL - busR) * 0.5f * e.width;
                const float sideLo = d.monoL.lp(side, monoC);
                side -= sideLo;  // remove the low band from the side signal
                busL = mid + side;
                busR = mid - side;
            }

            busL *= p.master.level;
            busR *= p.master.level;

            // soft limiter: fast attack, slow release, no lookahead (a drone has
            // no transients worth protecting, it just must not clip)
            if (p.master.limiter) {
                const float peak = std::max(std::fabs(busL), std::fabs(busR));
                const float coef = peak > d.limEnv ? 0.02f : 0.0004f;
                d.limEnv += coef * (peak - d.limEnv);
                const float want = d.limEnv > 0.95f ? 0.95f / d.limEnv : 1.0f;
                d.limGain += (want - d.limGain) * (want < d.limGain ? 0.05f : 0.002f);
                busL *= d.limGain;
                busR *= d.limGain;
            }

            out[(size_t)(done + i) * 2 + 0] = busL;
            out[(size_t)(done + i) * 2 + 1] = busR;
        }

        d.samplePos += block;
        done += block;
    }
}

// ---------------------------------------------------------------------------
// LiveSynth
// ---------------------------------------------------------------------------

LiveSynth::LiveSynth(const Params& p) : synth_(p), pending_(p) {}
LiveSynth::~LiveSynth() = default;

void LiveSynth::push(const Params& p) {
    std::lock_guard<std::mutex> lk(mu_);
    pending_ = p;
    dirty_.store(true, std::memory_order_release);
}

void LiveSynth::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    synth_.reset();
    time_.store(0.0);
}

void LiveSynth::render(float* out, int frames) {
    if (dirty_.load(std::memory_order_acquire)) {
        // try_lock, never lock: a UI thread stalled inside a file dialog must
        // not be able to starve the audio device - it just keeps the previous
        // parameter set for another block.
        std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
        if (lk.owns_lock()) {
            synth_.setParams(pending_);
            dirty_.store(false, std::memory_order_release);
        }
    }
    {
        std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
        if (!lk.owns_lock()) {  // reset() in flight - output silence, not garbage
            std::memset(out, 0, (size_t)frames * 2 * sizeof(float));
            return;
        }
        synth_.render(out, frames);
    }

    float pl = 0.0f, pr = 0.0f;
    uint64_t w = scopeWrite_.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        const float l = out[(size_t)i * 2], r = out[(size_t)i * 2 + 1];
        pl = std::max(pl, std::fabs(l));
        pr = std::max(pr, std::fabs(r));
        scope_[(size_t)(w % kScopeSize)] = (l + r) * 0.5f;
        ++w;
    }
    scopeWrite_.store(w, std::memory_order_release);
    // Decaying peak hold: the meter must fall smoothly between blocks.
    const float decay = 0.85f;
    peakL_.store(std::max(pl, peakL_.load(std::memory_order_relaxed) * decay));
    peakR_.store(std::max(pr, peakR_.load(std::memory_order_relaxed) * decay));
    time_.store(synth_.timeSec());
}

double LiveSynth::timeSec() const { return time_.load(); }
float LiveSynth::peakL() const { return peakL_.load(); }
float LiveSynth::peakR() const { return peakR_.load(); }

int LiveSynth::scope(float* out, int n) const {
    const int count = std::min(n, kScopeSize);
    const uint64_t w = scopeWrite_.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        const uint64_t idx = w + (uint64_t)kScopeSize - (uint64_t)(count - i);
        out[i] = scope_[(size_t)(idx % kScopeSize)];
    }
    return count;
}

// ---------------------------------------------------------------------------
// Offline render
// ---------------------------------------------------------------------------

RenderResult render(const Params& p, const std::function<bool(float)>& progress) {
    RenderResult r;
    r.sampleRate = std::max(8000, p.sampleRate);
    r.channels = p.stereo ? 2 : 1;

    const int sr = r.sampleRate;
    const int nMain = std::max(sr / 4, (int)(clampf(p.lengthSec, 1.0f, 1800.0f) * sr));
    // Seamless mode renders past the end and folds the tail back over the head:
    // in a looping player you literally hear both at once, so adding them IS
    // the correct wrap - no crossfade, and the piece keeps its own opening.
    const float tailSec =
        p.master.loopSeamless
            ? clampf(p.master.loopTail, 0.2f, std::min(30.0f, p.lengthSec * 0.9f))
            : 0.0f;
    const int nTail = (int)(tailSec * sr);

    std::vector<float> buf((size_t)(nMain + nTail) * 2, 0.0f);
    Synth synth(p);
    const int chunk = 4096;
    for (int at = 0; at < nMain + nTail; at += chunk) {
        const int n = std::min(chunk, nMain + nTail - at);
        synth.render(buf.data() + (size_t)at * 2, n);
        if (progress && !progress((float)(at + n) / (float)(nMain + nTail))) {
            r.cancelled = true;
            return r;
        }
    }

    if (nTail > 0) {
        for (int i = 0; i < nTail; ++i) {
            buf[(size_t)i * 2 + 0] += buf[(size_t)(nMain + i) * 2 + 0];
            buf[(size_t)i * 2 + 1] += buf[(size_t)(nMain + i) * 2 + 1];
        }
    } else {
        const int fi = std::min(nMain, (int)(std::max(0.0f, p.master.fadeIn) * sr));
        const int fo = std::min(nMain, (int)(std::max(0.0f, p.master.fadeOut) * sr));
        for (int i = 0; i < fi; ++i) {
            const float g = (float)i / (float)fi;
            buf[(size_t)i * 2 + 0] *= g;
            buf[(size_t)i * 2 + 1] *= g;
        }
        for (int i = 0; i < fo; ++i) {
            const float g = (float)i / (float)fo;
            const int s = nMain - 1 - i;
            buf[(size_t)s * 2 + 0] *= g;
            buf[(size_t)s * 2 + 1] *= g;
        }
    }

    // Mastering: measure, normalize to the requested peak, then hard-guard.
    float peak = 0.0f;
    for (int i = 0; i < nMain * 2; ++i) peak = std::max(peak, std::fabs(buf[(size_t)i]));
    float gain = 1.0f;
    if (p.master.normalize > 0.01f && peak > 1e-6f)
        gain = clampf(p.master.normalize, 0.05f, 1.0f) / peak;
    r.gainApplied = gain;

    r.frames = nMain;
    r.samples.resize((size_t)nMain * (size_t)r.channels);
    double sum = 0.0;
    float outPeak = 0.0f;
    for (int i = 0; i < nMain; ++i) {
        float l = buf[(size_t)i * 2 + 0] * gain;
        float rr = buf[(size_t)i * 2 + 1] * gain;
        l = clampf(l, -1.0f, 1.0f);
        rr = clampf(rr, -1.0f, 1.0f);
        if (r.channels == 2) {
            r.samples[(size_t)i * 2 + 0] = l;
            r.samples[(size_t)i * 2 + 1] = rr;
        } else {
            r.samples[(size_t)i] = (l + rr) * 0.5f;
        }
        outPeak = std::max(outPeak, std::max(std::fabs(l), std::fabs(rr)));
        sum += (double)l * l + (double)rr * rr;
    }
    r.peak = outPeak;
    r.rms = (float)std::sqrt(sum / std::max(1, nMain * 2));
    return r;
}

bool writeWav(const std::string& path, const RenderResult& r, uint32_t seed,
              bool dither, std::string& error) {
    if (r.samples.empty()) {
        error = "nothing rendered";
        return false;
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    const uint32_t dataBytes = (uint32_t)(r.samples.size() * 2);
    const uint16_t ch = (uint16_t)r.channels;
    const uint32_t rate = (uint32_t)r.sampleRate;
    const uint32_t byteRate = rate * ch * 2u;
    const uint16_t blockAlign = (uint16_t)(ch * 2);

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36u + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16u);
    u16(1);  // integer PCM - the only format Tyra's song player streams
    u16(ch);
    u32(rate);
    u32(byteRate);
    u16(blockAlign);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);

    // TPDF dither from the patch seed: audible-noise-floor insurance that stays
    // deterministic, so re-rendering a preset gives a byte-identical file.
    Rng rng;
    rng.seed(mix32(seed, 0xD1D1u));
    std::vector<int16_t> pcm(r.samples.size());
    for (size_t i = 0; i < r.samples.size(); ++i) {
        float v = r.samples[i] * 32767.0f;
        if (dither) v += (rng.uni() - rng.uni()) * 0.9f;
        v = clampf(v, -32768.0f, 32767.0f);
        pcm[i] = (int16_t)std::lrint(v);
    }
    const size_t wrote = std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    if (wrote != pcm.size()) {
        error = "short write (disk full?)";
        return false;
    }
    return true;
}

unsigned long long wavBytes(const Params& p) {
    const int sr = std::max(8000, p.sampleRate);
    const unsigned long long frames =
        (unsigned long long)(clampf(p.lengthSec, 1.0f, 1800.0f) * (float)sr);
    return 44ull + frames * (p.stereo ? 2ull : 1ull) * 2ull;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

namespace {

// Small builders so a preset reads as the patch it is, not as 130 assignments.
void setChord(Params& p, int slot, float bars, int a, int b = 99, int c = 99,
              int d = 99, int e = 99, int f = 99) {
    const int in[6] = {a, b, c, d, e, f};
    Step& s = p.steps[slot];
    s.count = 0;
    for (int i = 0; i < 6; ++i)
        if (in[i] != 99) s.notes[s.count++] = in[i];
    s.bars = bars;
}
void quietLayers(Params& p) {
    for (Layer& l : p.layers) l.on = false;
}

}  // namespace


const std::vector<Preset>& presets() {
    static std::vector<Preset> list;
    if (!list.empty()) return list;

    // --- the default patch, named ------------------------------------------
    list.push_back({"Init Drone",
                    "The default patch: sine sub plus a soft detuned saw.",
                    Params()});

    // --- Deep Space Hum ----------------------------------------------------
    {
        Params p;
        p.seed = 5150;
        p.rootNote = 26;  // D1
        p.bpm = 40.0f;
        p.glide = 6.0f;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveSine;
        p.layers[0].level = 0.6f;
        p.layers[0].notes = 0x01;
        p.layers[0].attack = 8.0f;
        p.layers[0].tone = 0.35f;
        p.layers[1].on = true;
        p.layers[1].wave = WaveTriangle;
        p.layers[1].octave = 1;
        p.layers[1].level = 0.2f;
        p.layers[1].unison = 4;
        p.layers[1].detune = 22.0f;
        p.layers[1].spread = 1.0f;
        p.layers[1].tone = 0.3f;
        p.layers[1].attack = 12.0f;
        p.layers[1].drift = 14.0f;
        p.layers[1].notes = 0x07;
        p.noise.on = true;
        p.noise.level = 0.07f;
        p.noise.cutoff = 220.0f;
        p.noise.res = 0.15f;
        p.noise.motionRate = 0.02f;
        setChord(p, 0, 16.0f, 0, 7, 12);
        setChord(p, 1, 16.0f, -5, 7, 10);
        p.stepCount = 2;
        p.filter.cutoff = 900.0f;
        p.fx.revSize = 0.95f;
        p.fx.revDecay = 18.0f;
        p.fx.revMix = 0.55f;
        p.fx.revDamp = 0.55f;
        p.fx.delayMix = 0.12f;
        p.fx.tilt = -3.0f;
        p.fx.highCut = 5000.0f;
        p.arc[0] = 0.2f; p.arc[1] = 0.6f; p.arc[2] = 1.0f;
        p.arc[3] = 0.7f; p.arc[4] = 0.25f;
        list.push_back({"Deep Space Hum",
                        "Sub-heavy, barely any motion, an 18-second tail.", p});
    }

    // --- Cathedral Pad -----------------------------------------------------
    {
        Params p;
        p.seed = 777;
        p.rootNote = 38;
        p.bpm = 52.0f;
        p.glide = 3.0f;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveOrgan;
        p.layers[0].level = 0.3f;
        p.layers[0].tone = 0.6f;
        p.layers[0].attack = 3.0f;
        p.layers[0].notes = 0x3F;
        p.layers[1].on = true;
        p.layers[1].wave = WaveSine;
        p.layers[1].octave = -1;
        p.layers[1].level = 0.4f;
        p.layers[1].notes = 0x01;
        p.layers[2].on = true;
        p.layers[2].wave = WaveSaw;
        p.layers[2].octave = 1;
        p.layers[2].level = 0.1f;
        p.layers[2].unison = 3;
        p.layers[2].detune = 9.0f;
        p.layers[2].tone = 0.5f;
        p.layers[2].notes = 0x1C;
        setChord(p, 0, 8.0f, 0, 7, 12, 16, 19);
        setChord(p, 1, 8.0f, -3, 4, 9, 16, 21);
        setChord(p, 2, 8.0f, -5, 2, 7, 14, 19);
        p.stepCount = 3;
        p.filter.cutoff = 3200.0f;
        p.fx.chorusMix = 0.4f;
        p.fx.revSize = 1.0f;
        p.fx.revDecay = 12.0f;
        p.fx.revMix = 0.5f;
        p.fx.revPredelay = 0.06f;
        p.fx.shimmer = 0.25f;
        p.fx.shimmerSemi = 12;
        p.bells.on = true;
        p.bells.timbre = BellGlass;
        p.bells.density = 5.0f;
        p.bells.level = 0.18f;
        p.bells.decay = 5.0f;
        p.bells.octave = 2;
        list.push_back({"Cathedral Pad",
                        "Organ stack, wide voicings, a hint of shimmer.", p});
    }

    // --- Glacial Shimmer ---------------------------------------------------
    {
        Params p;
        p.seed = 31337;
        p.rootNote = 40;
        p.bpm = 44.0f;
        p.glide = 5.0f;
        p.scale = ScalePentatonic;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveTriangle;
        p.layers[0].level = 0.3f;
        p.layers[0].unison = 5;
        p.layers[0].detune = 18.0f;
        p.layers[0].spread = 1.0f;
        p.layers[0].tone = 0.7f;
        p.layers[0].attack = 8.0f;
        p.layers[0].release = 9.0f;
        p.layers[0].notes = 0x1F;
        p.layers[1].on = true;
        p.layers[1].wave = WaveSine;
        p.layers[1].octave = -2;
        p.layers[1].level = 0.3f;
        p.layers[1].notes = 0x01;
        setChord(p, 0, 12.0f, 0, 7, 14, 19);
        setChord(p, 1, 12.0f, 2, 9, 16, 21);
        p.stepCount = 2;
        p.filter.cutoff = 4000.0f;
        p.fx.chorusMix = 0.45f;
        p.fx.chorusRate = 0.13f;
        p.fx.revSize = 1.0f;
        p.fx.revDecay = 22.0f;
        p.fx.revMix = 0.6f;
        p.fx.revDamp = 0.25f;
        p.fx.shimmer = 0.62f;
        p.fx.shimmerSemi = 12;
        p.fx.delayDiv = 4;
        p.fx.delayMix = 0.3f;
        p.fx.delayFeedback = 0.62f;
        p.bells.on = true;
        p.bells.timbre = BellGlass;
        p.bells.density = 9.0f;
        p.bells.level = 0.22f;
        p.bells.decay = 6.0f;
        p.bells.octave = 3;
        p.bells.bright = 0.75f;
        p.mods[0] = {ModSrcLfo1, ModDstShimmer, 0.3f};
        p.mods[1] = {ModSrcLfo2, ModDstCutoff, 0.25f};
        p.mods[2] = {ModSrcArc, ModDstReverbMix, 0.2f};
        list.push_back({"Glacial Shimmer",
                        "Octave-up feedback tail, glass bells, very bright.", p});
    }

    // --- Rust Wind ---------------------------------------------------------
    {
        Params p;
        p.seed = 909;
        p.rootNote = 31;
        p.bpm = 48.0f;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveSaw;
        p.layers[0].level = 0.24f;
        p.layers[0].unison = 4;
        p.layers[0].detune = 30.0f;
        p.layers[0].spread = 0.9f;
        p.layers[0].tone = 0.28f;
        p.layers[0].drift = 20.0f;
        p.layers[0].attack = 10.0f;
        p.layers[0].notes = 0x03;
        p.layers[1].on = true;
        p.layers[1].wave = WavePulse;
        p.layers[1].octave = 1;
        p.layers[1].pw = 0.2f;
        p.layers[1].level = 0.1f;
        p.layers[1].tone = 0.35f;
        p.layers[1].notes = 0x06;
        p.noise.on = true;
        p.noise.level = 0.3f;
        p.noise.cutoff = 700.0f;
        p.noise.res = 0.55f;
        p.noise.motionRate = 0.09f;
        p.noise.motionDepth = 0.85f;
        p.noise.stereo = 1.0f;
        setChord(p, 0, 24.0f, 0, 1, 7);
        p.stepCount = 1;
        p.filter.cutoff = 1400.0f;
        p.filter.res = 0.35f;
        p.fx.drive = 0.45f;
        p.fx.wow = 0.35f;
        p.fx.flutter = 0.2f;
        p.fx.hiss = 0.25f;
        p.fx.revDecay = 7.0f;
        p.fx.revMix = 0.38f;
        p.fx.tilt = -2.0f;
        p.bells.on = true;
        p.bells.timbre = BellPluck;
        p.bells.density = 3.0f;
        p.bells.level = 0.28f;
        p.bells.decay = 2.0f;
        p.bells.octave = 2;
        p.bells.chordLock = false;
        p.mods[0] = {ModSrcLfo2, ModDstNoise, 0.5f};
        p.mods[1] = {ModSrcLfo1, ModDstCutoff, 0.4f};
        p.mods[2] = {ModSrcRandom, ModDstPitch, 0.15f};
        list.push_back({"Rust Wind",
                        "Dirty, tape-worn, a resonant gale over a minor second.", p});
    }

    // --- Dark Cave ---------------------------------------------------------
    {
        Params p;
        p.seed = 66613;
        p.rootNote = 28;
        p.bpm = 36.0f;
        p.scale = ScalePhrygian;
        p.glide = 8.0f;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveSine;
        p.layers[0].level = 0.55f;
        p.layers[0].notes = 0x01;
        p.layers[0].attack = 14.0f;
        p.layers[1].on = true;
        p.layers[1].wave = WaveSaw;
        p.layers[1].level = 0.14f;
        p.layers[1].octave = 1;
        p.layers[1].unison = 2;
        p.layers[1].detune = 40.0f;
        p.layers[1].tone = 0.2f;
        p.layers[1].notes = 0x06;
        p.noise.on = true;
        p.noise.level = 0.12f;
        p.noise.cutoff = 320.0f;
        p.noise.res = 0.4f;
        p.noise.motionRate = 0.03f;
        setChord(p, 0, 24.0f, 0, 1, 8);
        setChord(p, 1, 24.0f, 0, 6, 13);
        p.stepCount = 2;
        p.filter.cutoff = 600.0f;
        p.filter.res = 0.5f;
        p.fx.drive = 0.25f;
        p.fx.revSize = 0.85f;
        p.fx.revDecay = 11.0f;
        p.fx.revMix = 0.5f;
        p.fx.revPredelay = 0.09f;
        p.fx.delayDiv = 2;
        p.fx.delayMix = 0.3f;
        p.fx.delayFeedback = 0.5f;
        p.fx.delayDamp = 0.7f;
        p.fx.tilt = -5.0f;
        p.fx.highCut = 3500.0f;
        p.bells.on = true;
        p.bells.timbre = BellPluck;
        p.bells.density = 4.0f;
        p.bells.level = 0.3f;
        p.bells.decay = 1.2f;
        p.bells.octave = 2;
        p.bells.bright = 0.2f;
        p.bells.revSend = 1.0f;
        p.bells.delaySend = 0.8f;
        p.mods[0] = {ModSrcLfo1, ModDstCutoff, 0.5f};
        p.mods[1] = {ModSrcLfo3, ModDstLevel2, 0.3f};
        p.mods[2] = {ModSrcNone, ModDstNone, 0.0f};
        list.push_back({"Dark Cave",
                        "Phrygian cluster, water-drip plucks, long predelay.", p});
    }

    // --- Underwater --------------------------------------------------------
    {
        Params p;
        p.seed = 2048;
        p.rootNote = 33;
        p.bpm = 50.0f;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveTriangle;
        p.layers[0].level = 0.45f;
        p.layers[0].tone = 0.18f;
        p.layers[0].unison = 3;
        p.layers[0].detune = 12.0f;
        p.layers[0].attack = 6.0f;
        p.layers[0].notes = 0x0F;
        p.layers[1].on = true;
        p.layers[1].wave = WaveSine;
        p.layers[1].octave = -1;
        p.layers[1].level = 0.35f;
        p.layers[1].notes = 0x01;
        p.noise.on = true;
        p.noise.level = 0.1f;
        p.noise.cutoff = 400.0f;
        p.noise.res = 0.2f;
        p.noise.motionRate = 0.12f;
        p.noise.motionDepth = 0.8f;
        setChord(p, 0, 8.0f, 0, 7, 12, 17);
        setChord(p, 1, 8.0f, -2, 5, 12, 15);
        p.stepCount = 2;
        p.filter.cutoff = 700.0f;
        p.filter.res = 0.3f;
        p.fx.chorusMix = 0.55f;
        p.fx.chorusRate = 0.35f;
        p.fx.chorusDepth = 0.7f;
        p.fx.wow = 0.4f;
        p.fx.revSize = 0.6f;
        p.fx.revDecay = 6.0f;
        p.fx.revMix = 0.45f;
        p.fx.highCut = 2600.0f;
        p.fx.tilt = -4.0f;
        p.fx.monoBelow = 200.0f;
        p.mods[0] = {ModSrcLfo1, ModDstCutoff, 0.55f};
        p.mods[1] = {ModSrcLfo2, ModDstChorusDepth, 0.4f};
        p.mods[2] = {ModSrcLfo3, ModDstPitch, 0.08f};
        p.lfos[0].rate = 0.09f;
        list.push_back({"Underwater",
                        "Everything muffled and swaying; heavy chorus and wow.", p});
    }

    // --- Ritual Bells ------------------------------------------------------
    {
        Params p;
        p.seed = 108;
        p.rootNote = 33;
        p.bpm = 56.0f;
        p.scale = ScalePentatonic;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveSine;
        p.layers[0].level = 0.4f;
        p.layers[0].notes = 0x03;
        p.layers[0].attack = 9.0f;
        p.layers[1].on = true;
        p.layers[1].wave = WaveOrgan;
        p.layers[1].octave = 1;
        p.layers[1].level = 0.09f;
        p.layers[1].tone = 0.45f;
        p.layers[1].notes = 0x1C;
        setChord(p, 0, 16.0f, 0, 7, 12, 19);
        p.stepCount = 1;
        p.filter.cutoff = 2600.0f;
        p.fx.revSize = 0.9f;
        p.fx.revDecay = 14.0f;
        p.fx.revMix = 0.55f;
        p.fx.delayDiv = 3;
        p.fx.delayMix = 0.35f;
        p.fx.delayFeedback = 0.6f;
        p.bells.on = true;
        p.bells.timbre = BellFm;
        p.bells.density = 22.0f;
        p.bells.level = 0.42f;
        p.bells.decay = 4.5f;
        p.bells.octave = 2;
        p.bells.range = 24;
        p.bells.quantize = 1;
        p.bells.chordLock = false;
        p.bells.bright = 0.6f;
        p.mods[0] = {ModSrcArc, ModDstBellDensity, 0.6f};
        p.mods[1] = {ModSrcLfo1, ModDstDelayMix, 0.2f};
        p.arc[0] = 0.1f; p.arc[1] = 0.5f; p.arc[2] = 0.9f;
        p.arc[3] = 1.0f; p.arc[4] = 0.2f;
        list.push_back({"Ritual Bells",
                        "Quiet drone under sparse pentatonic FM bells.", p});
    }

    // --- Machine Room ------------------------------------------------------
    {
        Params p;
        p.seed = 4004;
        p.rootNote = 29;
        p.bpm = 72.0f;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WavePulse;
        p.layers[0].pw = 0.42f;
        p.layers[0].level = 0.26f;
        p.layers[0].tone = 0.3f;
        p.layers[0].notes = 0x03;
        p.layers[0].attack = 2.0f;
        p.layers[1].on = true;
        p.layers[1].wave = WaveSine;
        p.layers[1].octave = -1;
        p.layers[1].level = 0.4f;
        p.layers[1].notes = 0x01;
        p.layers[2].on = true;
        p.layers[2].wave = WaveFm;
        p.layers[2].octave = 1;
        p.layers[2].fmRatio = 3.0f;
        p.layers[2].fmIndex = 2.4f;
        p.layers[2].level = 0.08f;
        p.layers[2].notes = 0x04;
        p.noise.on = true;
        p.noise.level = 0.16f;
        p.noise.cutoff = 1600.0f;
        p.noise.res = 0.6f;
        p.noise.motionRate = 0.22f;
        setChord(p, 0, 8.0f, 0, 7, 10);
        setChord(p, 1, 8.0f, 0, 6, 11);
        p.stepCount = 2;
        p.filter.cutoff = 1800.0f;
        p.filter.res = 0.45f;
        p.fx.drive = 0.35f;
        p.fx.delayDiv = 1;
        p.fx.delayMix = 0.22f;
        p.fx.delayFeedback = 0.4f;
        p.fx.pingpong = true;
        p.fx.revDecay = 4.0f;
        p.fx.revMix = 0.3f;
        p.lfos[2].rate = 2.9f;
        p.mods[0] = {ModSrcLfo3, ModDstLevel1, 0.35f};
        p.mods[1] = {ModSrcLfo1, ModDstCutoff, 0.3f};
        p.mods[2] = {ModSrcLfo2, ModDstNoise, 0.4f};
        list.push_back({"Machine Room",
                        "Pulsed, industrial, a tritone shift every eight bars.", p});
    }

    // --- Neon Dusk ---------------------------------------------------------
    {
        Params p;
        p.seed = 1984;
        p.rootNote = 35;
        p.bpm = 64.0f;
        p.scale = ScaleDorian;
        quietLayers(p);
        p.layers[0].on = true;
        p.layers[0].wave = WaveSaw;
        p.layers[0].level = 0.2f;
        p.layers[0].unison = 5;
        p.layers[0].detune = 16.0f;
        p.layers[0].spread = 1.0f;
        p.layers[0].tone = 0.55f;
        p.layers[0].attack = 4.0f;
        p.layers[0].notes = 0x1E;
        p.layers[1].on = true;
        p.layers[1].wave = WaveSine;
        p.layers[1].octave = -1;
        p.layers[1].level = 0.42f;
        p.layers[1].notes = 0x01;
        p.layers[2].on = true;
        p.layers[2].wave = WaveTriangle;
        p.layers[2].octave = 2;
        p.layers[2].level = 0.08f;
        p.layers[2].tone = 0.8f;
        p.layers[2].notes = 0x10;
        setChord(p, 0, 8.0f, 0, 7, 15, 22);
        setChord(p, 1, 8.0f, 5, 12, 15, 19);
        setChord(p, 2, 8.0f, 3, 10, 14, 21);
        setChord(p, 3, 8.0f, -2, 5, 12, 17);
        p.stepCount = 4;
        p.filter.cutoff = 2800.0f;
        p.fx.chorusMix = 0.35f;
        p.fx.delayDiv = 2;
        p.fx.delayMix = 0.3f;
        p.fx.delayFeedback = 0.5f;
        p.fx.pingpong = true;
        p.fx.revSize = 0.8f;
        p.fx.revDecay = 8.0f;
        p.fx.revMix = 0.42f;
        p.fx.tilt = 1.5f;
        p.bells.on = true;
        p.bells.timbre = BellGlass;
        p.bells.density = 12.0f;
        p.bells.level = 0.2f;
        p.bells.decay = 3.0f;
        p.bells.octave = 2;
        p.bells.quantize = 1;
        p.mods[0] = {ModSrcArc, ModDstCutoff, 0.35f};
        p.mods[1] = {ModSrcLfo2, ModDstLevel1, 0.2f};
        p.mods[2] = {ModSrcLfo1, ModDstWidth, 0.25f};
        list.push_back({"Neon Dusk",
                        "Four-chord dorian move with ping-pong echoes.", p});
    }

    return list;
}

// ---------------------------------------------------------------------------
// .drone text format
// ---------------------------------------------------------------------------

namespace {

// ONE list of fields, walked by both the writer and the reader - the same
// single-source trick the project's section writers use. A field added here is
// saved AND loaded; there is no second list to forget.
template <class V>
void visitParams(Params& p, V& v) {
    v.group("piece");
    v("seed", p.seed);
    v("length", p.lengthSec);
    v("rate", p.sampleRate);
    v("stereo", p.stereo);
    v("bpm", p.bpm);
    v("beatsPerBar", p.beatsPerBar);
    v("root", p.rootNote);
    v("tuning", p.tuning);
    v.enumv("scale", p.scale, scaleNames(), ScaleCount);
    v("glide", p.glide);

    v.group("progression");
    v("steps", p.stepCount);
    for (int i = 0; i < kMaxSteps; ++i) {
        const std::string pre = "step" + std::to_string(i + 1) + ".";
        v.ints(pre + "notes", p.steps[i].notes, p.steps[i].count, kMaxChordNotes);
        v(pre + "bars", p.steps[i].bars);
    }

    for (int i = 0; i < kMaxLayers; ++i) {
        Layer& L = p.layers[i];
        v.group(("layer " + std::to_string(i + 1)).c_str());
        const std::string pre = "layer" + std::to_string(i + 1) + ".";
        v(pre + "on", L.on);
        v.enumv(pre + "wave", L.wave, waveNames(), WaveCount);
        v(pre + "octave", L.octave);
        v(pre + "semi", L.semi);
        v(pre + "fine", L.fine);
        v(pre + "unison", L.unison);
        v(pre + "detune", L.detune);
        v(pre + "spread", L.spread);
        v(pre + "level", L.level);
        v(pre + "pan", L.pan);
        v(pre + "tone", L.tone);
        v(pre + "attack", L.attack);
        v(pre + "release", L.release);
        v(pre + "drift", L.drift);
        v(pre + "fmRatio", L.fmRatio);
        v(pre + "fmIndex", L.fmIndex);
        v(pre + "pulseWidth", L.pw);
        v(pre + "notes", L.notes);
    }

    v.group("air");
    v("air.on", p.noise.on);
    v("air.level", p.noise.level);
    v("air.cutoff", p.noise.cutoff);
    v("air.res", p.noise.res);
    v("air.motionRate", p.noise.motionRate);
    v("air.motionDepth", p.noise.motionDepth);
    v("air.stereo", p.noise.stereo);

    v.group("bells");
    v("bells.on", p.bells.on);
    v.enumv("bells.timbre", p.bells.timbre, bellNames(), BellTimbreCount);
    v("bells.density", p.bells.density);
    v("bells.level", p.bells.level);
    v("bells.decay", p.bells.decay);
    v("bells.octave", p.bells.octave);
    v("bells.range", p.bells.range);
    v("bells.spread", p.bells.spread);
    v("bells.chordLock", p.bells.chordLock);
    v("bells.quantize", p.bells.quantize);
    v("bells.delaySend", p.bells.delaySend);
    v("bells.reverbSend", p.bells.revSend);
    v("bells.bright", p.bells.bright);

    v.group("filter");
    v.enumv("filter.type", p.filter.type, filterNames(), FilterTypeCount);
    v("filter.cutoff", p.filter.cutoff);
    v("filter.res", p.filter.res);

    v.group("motion");
    for (int i = 0; i < kNumLfos; ++i) {
        const std::string pre = "lfo" + std::to_string(i + 1) + ".";
        v(pre + "rate", p.lfos[i].rate);
        v.enumv(pre + "shape", p.lfos[i].shape, lfoShapeNames(), LfoShapeCount);
        v(pre + "depth", p.lfos[i].depth);
        v(pre + "sync", p.lfos[i].sync);
    }
    for (int i = 0; i < kNumMods; ++i) {
        const std::string pre = "mod" + std::to_string(i + 1) + ".";
        v.enumv(pre + "src", p.mods[i].src, modSourceNames(), ModSrcCount);
        v.enumv(pre + "dst", p.mods[i].dst, modTargetNames(), ModDstTargetCount);
        v(pre + "amount", p.mods[i].amount);
    }
    v.floats("arc", p.arc, kArcPoints);

    v.group("fx");
    v("drive", p.fx.drive);
    v("driveMix", p.fx.driveMix);
    v("chorus.rate", p.fx.chorusRate);
    v("chorus.depth", p.fx.chorusDepth);
    v("chorus.mix", p.fx.chorusMix);
    v("chorus.spread", p.fx.chorusSpread);
    v("tape.wow", p.fx.wow);
    v("tape.flutter", p.fx.flutter);
    v("tape.hiss", p.fx.hiss);
    v("delay.div", p.fx.delayDiv);
    v("delay.time", p.fx.delayTime);
    v("delay.feedback", p.fx.delayFeedback);
    v("delay.mix", p.fx.delayMix);
    v("delay.damp", p.fx.delayDamp);
    v("delay.pingpong", p.fx.pingpong);
    v("reverb.size", p.fx.revSize);
    v("reverb.decay", p.fx.revDecay);
    v("reverb.damp", p.fx.revDamp);
    v("reverb.lowCut", p.fx.revLowCut);
    v("reverb.predelay", p.fx.revPredelay);
    v("reverb.diffusion", p.fx.revDiffusion);
    v("reverb.width", p.fx.revWidth);
    v("reverb.mix", p.fx.revMix);
    v("reverb.shimmer", p.fx.shimmer);
    v("reverb.shimmerSemi", p.fx.shimmerSemi);
    v("out.lowCut", p.fx.lowCut);
    v("out.highCut", p.fx.highCut);
    v("out.tilt", p.fx.tilt);
    v("out.width", p.fx.width);
    v("out.monoBelow", p.fx.monoBelow);

    v.group("master");
    v("master.level", p.master.level);
    v("master.fadeIn", p.master.fadeIn);
    v("master.fadeOut", p.master.fadeOut);
    v("master.loopSeamless", p.master.loopSeamless);
    v("master.loopTail", p.master.loopTail);
    v("master.normalize", p.master.normalize);
    v("master.limiter", p.master.limiter);
    v("master.dither", p.master.dither);
}

struct TextWriter {
    std::string out;
    void group(const char* name) {
        out += "\n# ";
        out += name;
        out += "\n";
    }
    void line(const std::string& k, const std::string& val) {
        out += k;
        out += " = ";
        out += val;
        out += "\n";
    }
    void operator()(const std::string& k, uint32_t& val) {
        line(k, std::to_string(val));
    }
    void operator()(const std::string& k, int& val) { line(k, std::to_string(val)); }
    void operator()(const std::string& k, bool& val) { line(k, val ? "true" : "false"); }
    void operator()(const std::string& k, float& val) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", (double)val);
        line(k, buf);
    }
    void enumv(const std::string& k, int& val, const char* const* names, int count) {
        line(k, (val >= 0 && val < count) ? names[val] : std::to_string(val));
    }
    void ints(const std::string& k, int* val, int count, int) {
        std::string s;
        for (int i = 0; i < count; ++i) {
            if (i) s += ", ";
            s += std::to_string(val[i]);
        }
        line(k, s);
    }
    void floats(const std::string& k, float* val, int count) {
        std::string s;
        char buf[32];
        for (int i = 0; i < count; ++i) {
            if (i) s += ", ";
            std::snprintf(buf, sizeof(buf), "%.4g", (double)val[i]);
            s += buf;
        }
        line(k, s);
    }
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}
std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

struct TextReader {
    const std::map<std::string, std::string>* kv = nullptr;
    const std::string* find(const std::string& k) const {
        auto it = kv->find(lowerStr(k));
        return it == kv->end() ? nullptr : &it->second;
    }
    void group(const char*) {}
    void operator()(const std::string& k, uint32_t& val) {
        if (const std::string* s = find(k)) val = (uint32_t)strtoul(s->c_str(), nullptr, 10);
    }
    void operator()(const std::string& k, int& val) {
        if (const std::string* s = find(k)) val = (int)strtol(s->c_str(), nullptr, 10);
    }
    void operator()(const std::string& k, bool& val) {
        if (const std::string* s = find(k)) {
            const std::string l = lowerStr(*s);
            val = (l == "true" || l == "1" || l == "yes" || l == "on");
        }
    }
    void operator()(const std::string& k, float& val) {
        if (const std::string* s = find(k)) val = (float)atof(s->c_str());
    }
    void enumv(const std::string& k, int& val, const char* const* names, int count) {
        const std::string* s = find(k);
        if (!s) return;
        const std::string want = lowerStr(trim(*s));
        for (int i = 0; i < count; ++i)
            if (lowerStr(names[i]) == want) { val = i; return; }
        // A number is accepted too: hand-edited files and future name changes.
        if (!want.empty() && (isdigit((unsigned char)want[0]) != 0)) {
            const int n = (int)strtol(want.c_str(), nullptr, 10);
            if (n >= 0 && n < count) val = n;
        }
    }
    void ints(const std::string& k, int* val, int& count, int maxCount) {
        const std::string* s = find(k);
        if (!s) return;
        std::stringstream ss(*s);
        std::string tok;
        int n = 0;
        while (std::getline(ss, tok, ',') && n < maxCount) {
            tok = trim(tok);
            if (tok.empty()) continue;
            val[n++] = (int)strtol(tok.c_str(), nullptr, 10);
        }
        count = n;
    }
    void floats(const std::string& k, float* val, int count) {
        const std::string* s = find(k);
        if (!s) return;
        std::stringstream ss(*s);
        std::string tok;
        int n = 0;
        while (std::getline(ss, tok, ',') && n < count) {
            tok = trim(tok);
            if (tok.empty()) continue;
            val[n++] = (float)atof(tok.c_str());
        }
    }
};

}  // namespace

std::string toText(const Params& p, const std::string& title) {
    Params copy = p;
    TextWriter w;
    w.out = "# TyraX drone patch - Tools > Drone Generator\n";
    w.out += "# Rendered next to the WAV it produced; edit by hand if you like,\n";
    w.out += "# unknown keys are ignored and missing ones keep their default.\n";
    w.out += "version = 1\n";
    if (!title.empty()) w.line("title", title);
    visitParams(copy, w);
    return w.out;
}

bool fromText(const std::string& text, Params& out, std::string& title,
              std::string& error) {
    std::map<std::string, std::string> kv;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = lowerStr(trim(line.substr(0, eq)));
        if (k.empty()) continue;
        kv[k] = trim(line.substr(eq + 1));
    }
    if (kv.empty()) {
        error = "no parameters found - is this a .drone file?";
        return false;
    }
    auto it = kv.find("title");
    if (it != kv.end()) title = it->second;

    Params p;  // defaults, so an old/partial file still loads
    TextReader r;
    r.kv = &kv;
    visitParams(p, r);

    // Clamp everything a hand-edited file could get wrong; the synth trusts
    // these ranges (a negative unison count or a 0 Hz bar would divide by zero).
    p.sampleRate = clampi(p.sampleRate, 8000, 48000);
    p.lengthSec = clampf(p.lengthSec, 1.0f, 1800.0f);
    p.bpm = clampf(p.bpm, 10.0f, 300.0f);
    p.beatsPerBar = clampi(p.beatsPerBar, 1, 16);
    p.rootNote = clampi(p.rootNote, 12, 84);
    p.tuning = clampf(p.tuning, 380.0f, 500.0f);
    p.scale = clampi(p.scale, 0, ScaleCount - 1);
    p.glide = clampf(p.glide, 0.0f, 30.0f);
    p.stepCount = clampi(p.stepCount, 1, kMaxSteps);
    for (Step& s : p.steps) {
        s.count = clampi(s.count, 0, kMaxChordNotes);
        s.bars = clampf(s.bars, 0.25f, 64.0f);
        for (int& n : s.notes) n = clampi(n, -36, 48);
    }
    for (Layer& L : p.layers) {
        L.wave = clampi(L.wave, 0, WaveCount - 1);
        L.octave = clampi(L.octave, -3, 3);
        L.semi = clampi(L.semi, -24, 24);
        L.unison = clampi(L.unison, 1, kMaxUnison);
        L.notes = clampi(L.notes, 0, 0x3F);
        L.level = clampf(L.level, 0.0f, 1.0f);
        L.attack = clampf(L.attack, 0.01f, 60.0f);
        L.release = clampf(L.release, 0.01f, 60.0f);
    }
    p.bells.timbre = clampi(p.bells.timbre, 0, BellTimbreCount - 1);
    p.bells.quantize = clampi(p.bells.quantize, 0, 2);
    p.bells.octave = clampi(p.bells.octave, -2, 5);
    p.bells.range = clampi(p.bells.range, 1, 48);
    p.bells.density = clampf(p.bells.density, 0.0f, 240.0f);
    p.filter.type = clampi(p.filter.type, 0, FilterTypeCount - 1);
    p.fx.delayDiv = clampi(p.fx.delayDiv, 0, 5);
    p.fx.revDecay = clampf(p.fx.revDecay, 0.15f, 40.0f);
    p.fx.shimmerSemi = clampi(p.fx.shimmerSemi, -24, 24);
    for (Lfo& l : p.lfos) {
        l.shape = clampi(l.shape, 0, LfoShapeCount - 1);
        l.rate = clampf(l.rate, 0.0f, 20.0f);
    }
    for (ModRow& m : p.mods) {
        m.src = clampi(m.src, 0, ModSrcCount - 1);
        m.dst = clampi(m.dst, 0, ModDstTargetCount - 1);
        m.amount = clampf(m.amount, -1.0f, 1.0f);
    }
    p.master.loopTail = clampf(p.master.loopTail, 0.2f, 30.0f);
    p.master.normalize = clampf(p.master.normalize, 0.0f, 1.0f);
    out = p;
    return true;
}

}  // namespace dronegen

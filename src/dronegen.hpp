#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Ambient / drone music generator (Tools > Drone Generator).
//
// Host-only, no GL, no `Project` dependency - the treegen/matbake pattern: a
// pure DSP graph over a `Params` struct, so the whole synthesizer is
// exercisable from a 40-line host harness (render a preset, measure it) instead
// of by ear through the GUI.
//
// The output is an ordinary asset: `render()` fills a stereo float buffer,
// `writeWav()` drops 16-bit PCM into `res/audio/` and the track joins the
// project music list. Nothing about the PS2 side changes - the console streams
// the WAV through `Tyra::AudioSong` like a hand-authored song, which is why the
// default render format is **22050 Hz stereo 16-bit** (what AudioSong::load
// documents) and why the loop tools matter: background music plays with
// `song.inLoop = true`, so the seam is audible forever if it is not crossfaded.
//
// Two consumers share the same code:
//   - `Synth` renders blocks in real time; the editor's audio device pulls from
//     it through `LiveSynth`, so turning a knob is heard immediately.
//   - `render()` runs the same block loop offline into a buffer, then applies
//     the loop crossfade, fades and normalization that only make sense on a
//     finished piece.
// A knob therefore cannot sound different in the preview than in the file: the
// preview IS the file's synthesizer, minus the mastering tail.
namespace dronegen {

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

constexpr int kMaxLayers = 4;      // oscillator stacks
constexpr int kMaxChordNotes = 6;  // voices per stack = degrees in a chord
constexpr int kMaxSteps = 8;       // chord progression slots
constexpr int kMaxUnison = 5;      // detuned copies per voice
constexpr int kNumLfos = 3;
constexpr int kNumMods = 6;   // modulation matrix rows
constexpr int kArcPoints = 5; // breakpoints of the piece-long "arc" envelope

enum Wave { WaveSine = 0, WaveTriangle, WaveSaw, WaveSquare, WavePulse,
            WaveFm, WaveOrgan, WaveCount };
enum LfoShape { LfoSine = 0, LfoTriangle, LfoRamp, LfoSquare, LfoSampleHold,
                LfoSmooth, LfoShapeCount };
enum FilterType { FilterLowPass = 0, FilterBandPass, FilterHighPass,
                  FilterTypeCount };
enum BellTimbre { BellFm = 0, BellPluck, BellGlass, BellTimbreCount };
enum Scale { ScaleMinor = 0, ScaleMajor, ScaleDorian, ScalePhrygian,
             ScalePentatonic, ScaleWholeTone, ScaleChromatic, ScaleCount };

// Modulation sources and destinations. Both are serialized as names, so
// inserting an entry in the middle stays compatible with existing .drone files.
enum ModSource { ModSrcNone = 0, ModSrcLfo1, ModSrcLfo2, ModSrcLfo3, ModSrcArc,
                 ModSrcRandom, ModSrcCount };
enum ModTarget { ModDstNone = 0, ModDstCutoff, ModDstResonance, ModDstPitch,
                 ModDstLevel1, ModDstLevel2, ModDstLevel3, ModDstLevel4,
                 ModDstNoise, ModDstBellDensity, ModDstChorusDepth,
                 ModDstDelayMix, ModDstReverbMix, ModDstShimmer, ModDstDrive,
                 ModDstWidth, ModDstTargetCount };

const char* const* waveNames();       // [WaveCount]
const char* const* lfoShapeNames();   // [LfoShapeCount]
const char* const* filterNames();     // [FilterTypeCount]
const char* const* bellNames();       // [BellTimbreCount]
const char* const* scaleNames();      // [ScaleCount]
const char* const* modSourceNames();  // [ModSrcCount]
const char* const* modTargetNames();  // [ModDstTargetCount]

// One oscillator stack. `notes` is a bitmask over the chord's degrees, which is
// what lets one layer hold the root as a sub while another plays the full
// chord.
struct Layer {
    bool on = false;
    int wave = WaveSine;
    int octave = 0;         // -3..+3
    int semi = 0;           // -12..+12
    float fine = 0.0f;      // cents, -100..+100
    int unison = 1;         // 1..kMaxUnison detuned copies
    float detune = 12.0f;   // cents across the unison stack
    float spread = 0.6f;    // unison stereo spread, 0..1
    float level = 0.5f;     // linear gain, 0..1
    float pan = 0.0f;       // -1..+1
    float tone = 0.6f;      // per-voice low-pass, 0..1 -> 80 Hz..12 kHz
    float attack = 4.0f;    // s, note fade-in when a chord brings it in
    float release = 5.0f;   // s, note fade-out
    float drift = 6.0f;     // cents of slow per-voice random detuning
    float fmRatio = 2.0f;   // WaveFm modulator ratio
    float fmIndex = 1.5f;   // WaveFm modulation index
    float pw = 0.35f;       // WavePulse duty, 0.05..0.95
    int notes = 0x3F;       // chord degrees this layer plays (bit 0 = root)
};

// Air / wind bed: filtered noise with its own slow motion. Not a Layer because
// it has no pitch and shares nothing with the oscillator path.
struct NoiseBed {
    bool on = false;
    float level = 0.12f;
    float cutoff = 900.0f;    // Hz, band center
    float res = 0.3f;         // 0..0.95, resonance -> whistle
    float motionRate = 0.05f; // Hz of cutoff wander
    float motionDepth = 0.5f; // 0..1, octaves of wander
    float stereo = 0.7f;      // channel decorrelation, 0..1
};

// Sparse melodic events - the "interest" on top of a drone. Deterministic:
// every event is drawn from the seeded stream at generation time, so the same
// parameters place the same notes.
struct Bells {
    bool on = false;
    int timbre = BellFm;
    float density = 8.0f;   // events per minute
    float level = 0.35f;
    float decay = 3.0f;     // s
    int octave = 2;         // octaves above the root
    int range = 12;         // semitones of reach above that
    float spread = 0.8f;    // random stereo placement, 0..1
    bool chordLock = true;  // draw from the current chord, not the whole scale
    int quantize = 1;       // 0 free, 1 beat, 2 bar
    float delaySend = 0.5f; // extra send into the delay
    float revSend = 0.8f;   // extra send into the reverb
    float bright = 0.5f;    // timbre brightness, 0..1
};

struct MasterFilter {
    int type = FilterLowPass;
    float cutoff = 2200.0f;  // Hz, 40..14000
    float res = 0.25f;       // 0..0.95
};

struct Lfo {
    float rate = 0.07f;   // Hz (or cycles per bar when `sync`)
    int shape = LfoSine;
    float depth = 1.0f;   // 0..1, scales every mod row using it
    bool sync = false;    // rate is in cycles per bar -> loops with the piece
};

struct ModRow {
    int src = ModSrcNone;
    int dst = ModDstNone;
    float amount = 0.0f;  // -1..+1
};

// One chord in the progression: semitone offsets from the root note, held for
// `bars`. Absent degrees (index >= count) fade their voice out.
struct Step {
    int notes[kMaxChordNotes] = {0, 7, 12, 19, 0, 0};
    int count = 4;
    float bars = 8.0f;
};

struct Fx {
    // Saturation before the filter - the "warmth" that keeps a stack of sines
    // from sounding like a test tone.
    float drive = 0.15f;      // 0..1
    float driveMix = 1.0f;    // 0..1

    // Ensemble chorus: three modulated taps, the classic pad widener.
    float chorusRate = 0.25f; // Hz
    float chorusDepth = 0.4f; // 0..1
    float chorusMix = 0.35f;  // 0..1
    float chorusSpread = 1.0f;

    // Tape character: wow (slow) + flutter (fast) pitch wobble and hiss.
    float wow = 0.15f;        // 0..1
    float flutter = 0.05f;    // 0..1
    float hiss = 0.0f;        // 0..1

    // Echo. `div` picks a musical division; 0 means use `delayTime`.
    int delayDiv = 3;         // 0 free, 1 1/4, 2 1/2, 3 1 bar, 4 1.5 bars, 5 2 bars
    float delayTime = 1.5f;   // s when delayDiv == 0
    float delayFeedback = 0.55f;
    float delayMix = 0.25f;
    float delayDamp = 0.5f;   // 0..1, low-pass inside the loop
    bool pingpong = true;

    // Reverb: 8-line feedback delay network. `decay` is an RT60 in seconds and
    // is allowed to be absurd on purpose - a 30 s tail IS the genre.
    float revSize = 0.7f;      // 0..1, scales the network's delay lengths
    float revDecay = 9.0f;     // s
    float revDamp = 0.45f;     // 0..1, high-frequency loss per pass
    float revLowCut = 90.0f;   // Hz, keeps the tail out of the sub range
    float revPredelay = 0.03f; // s
    float revDiffusion = 0.7f; // 0..1, input allpass amount
    float revWidth = 1.0f;     // 0..1
    float revMix = 0.45f;      // 0..1
    float shimmer = 0.0f;      // 0..1, pitch-shifted feedback into the tail
    int shimmerSemi = 12;      // +12, +7, -12 ... semitones

    // Tone shaping and stereo of the finished mix.
    float lowCut = 40.0f;      // Hz
    float highCut = 9000.0f;   // Hz (22 kHz material has nothing above 11 k)
    float tilt = 0.0f;         // dB, negative = darker
    float width = 1.0f;        // 0..2, M/S width
    float monoBelow = 140.0f;  // Hz, bass summed to mono (kind to a TV)
};

struct Master {
    float level = 0.8f;        // linear
    float fadeIn = 2.0f;       // s (ignored when loopSeamless)
    float fadeOut = 4.0f;      // s
    bool loopSeamless = true;  // crossfade the tail over the head
    float loopTail = 6.0f;     // s of extra render folded back in
    float normalize = 0.89f;   // peak target, 0 = off (0.89 ~ -1 dBFS)
    bool limiter = true;
    bool dither = true;        // TPDF at 16-bit, seeded -> still deterministic
};

struct Params {
    uint32_t seed = 20260726;

    // Format / length. 22050 stereo is what the PS2 song player wants; the
    // higher rates exist for auditioning and for exporting to other tools.
    float lengthSec = 60.0f;
    int sampleRate = 22050;
    bool stereo = true;

    // Time base and tonality.
    float bpm = 60.0f;
    int beatsPerBar = 4;
    int rootNote = 33;     // MIDI note, 33 = A1 = 55 Hz
    float tuning = 440.0f; // A4 reference
    int scale = ScaleMinor;
    float glide = 2.5f;    // s, portamento between chords

    Step steps[kMaxSteps];
    int stepCount = 2;

    Layer layers[kMaxLayers];
    NoiseBed noise;
    Bells bells;
    MasterFilter filter;
    Lfo lfos[kNumLfos];
    ModRow mods[kNumMods];
    float arc[kArcPoints] = {0.35f, 0.7f, 1.0f, 0.8f, 0.3f};
    Fx fx;
    Master master;

    Params();  // seeds the default two-chord, two-layer patch
};

// Musical helpers shared by the synth and the UI (the chord editor names the
// degrees it is editing, so it must agree with what the synth plays).
float noteHz(const Params& p, int midi);
float barSeconds(const Params& p);
float progressionBars(const Params& p);  // total bars of one pass
const char* noteName(int midi);          // "A1", "C#3", ... (static buffer)
const char* delayDivName(int div);
float delaySeconds(const Params& p);     // resolved echo time

// ---------------------------------------------------------------------------
// Synthesizer
// ---------------------------------------------------------------------------

// Live, block-based voice. Owns every delay line it needs, sized for the
// maximum settings, so `render()` never allocates and an audio callback can
// call it directly. `sampleRate` is structural: changing it needs a new Synth.
class Synth {
 public:
    explicit Synth(const Params& p);
    ~Synth();

    void setParams(const Params& p);  // live; sample rate must not change
    const Params& params() const { return p_; }

    void reset();  // rewinds to t=0 and re-seeds every random stream

    // Interleaved stereo (always 2 channels - a mono render sums afterwards).
    // `frames` is the per-channel sample count.
    void render(float* out, int frames);

    double timeSec() const;

    // Every bit of DSP state. Public only so the per-block helpers in the .cpp
    // can take it by reference; nothing outside dronegen.cpp names it.
    struct Impl;

 private:
    std::unique_ptr<Impl> d_;
    Params p_;
};

// Thread-safe wrapper for the editor's audio device: the UI thread pushes
// parameters, the audio thread renders. The copy happens in the audio thread
// under a `try_lock`, so a stalled UI can never block the device - it just
// keeps playing the previous values. The scope/meter data goes the other way
// through plain atomics: a visualizer may race, a device may not.
class LiveSynth {
 public:
    explicit LiveSynth(const Params& p);
    ~LiveSynth();

    void push(const Params& p);  // UI thread
    void render(float* out, int frames);  // audio thread
    void reset();                // UI thread; takes the lock

    double timeSec() const;
    float peakL() const;
    float peakR() const;
    // Copies the most recent mono block for the scope/analyzer. Returns the
    // number of samples written (<= n).
    int scope(float* out, int n) const;

    static constexpr int kScopeSize = 2048;

 private:
    mutable std::mutex mu_;
    Synth synth_;
    Params pending_;
    std::atomic<bool> dirty_{false};
    std::atomic<float> peakL_{0.0f}, peakR_{0.0f};
    std::atomic<double> time_{0.0};
    // Ring of the last kScopeSize mono samples + a monotonically growing write
    // index. Read without a lock on purpose (see above).
    float scope_[kScopeSize] = {};
    std::atomic<uint64_t> scopeWrite_{0};
};

// ---------------------------------------------------------------------------
// Offline render and file output
// ---------------------------------------------------------------------------

struct RenderResult {
    std::vector<float> samples;  // interleaved, `channels` per frame
    int channels = 2;
    int sampleRate = 22050;
    int frames = 0;
    float peak = 0.0f;      // after mastering
    float rms = 0.0f;
    float gainApplied = 1.0f;  // what normalization did
    bool cancelled = false;
};

// Renders the whole piece. `progress` (0..1) may return false to cancel, and is
// called a few times per second - it is the UI's progress bar and its cancel
// button. Deterministic: same Params in, same samples out.
RenderResult render(const Params& p,
                    const std::function<bool(float)>& progress = nullptr);

// 16-bit PCM RIFF/WAVE - the only thing the PS2 side streams.
bool writeWav(const std::string& path, const RenderResult& r, uint32_t seed,
              bool dither, std::string& error);

// Estimated size on disc of what writeWav() would produce.
unsigned long long wavBytes(const Params& p);

// ---------------------------------------------------------------------------
// Presets and the .drone sidecar
// ---------------------------------------------------------------------------

struct Preset {
    const char* name;
    const char* blurb;  // one line, shown as the combo tooltip
    Params params;
};
const std::vector<Preset>& presets();

// `key = value` text, one parameter per line - the .flownode / .screenfx house
// format: diff-friendly, hand-editable, and unknown keys are ignored so an
// older file still loads. Written next to the rendered WAV as `<name>.drone`,
// which is what makes a shipped track re-editable.
std::string toText(const Params& p, const std::string& title);
bool fromText(const std::string& text, Params& out, std::string& title,
              std::string& error);

}  // namespace dronegen

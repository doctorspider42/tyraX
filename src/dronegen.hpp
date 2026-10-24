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

// Automation (the timeline): a lane per parameter, a keyframe per point. Fixed
// capacity on purpose - `Params` is copied into the audio thread on every edit,
// so nothing in it may own heap memory (see LiveSynth).
// How much audio a settling seek renders and throws away, to fill the delay and
// reverb lines at the new position. Long enough for a room to establish, short
// enough that a seek feels instant - and scaled by the reverb, because that is
// the thing being filled (a 20-second tail needs more history than a dry patch).
float seekPreroll(const struct Params& p);

constexpr int kMaxAutoLanes = 12;
constexpr int kMaxAutoPoints = 24;

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

// One keyframe: a parameter value at a point in time. Linear between points,
// held flat before the first and after the last.
struct AutoPoint {
    float t = 0.0f;      // seconds from the start of the piece
    float value = 0.0f;  // in the parameter's own unit (Hz, seconds, 0..1, ...)
};

// One automated parameter. `param` indexes paramTable(); the .drone file stores
// the parameter's KEY instead, so a lane survives the table changing shape.
struct AutoLane {
    int param = -1;
    int count = 0;
    AutoPoint pts[kMaxAutoPoints];
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

    // The timeline. A lane overrides its parameter's value above - the field is
    // still what the knob edits and what a patch without automation uses, but
    // once a lane has points the synth writes over it every control block.
    AutoLane autoLanes[kMaxAutoLanes];
    int autoLaneCount = 0;

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
// Automation
// ---------------------------------------------------------------------------

// Every automatable parameter, described by where it lives inside `Params`. The
// table is derived from the SAME field list the .drone writer walks
// (`visitParams`), so a parameter is automatable the moment it is saveable -
// there is no third list to keep in step. Arrays (the chord table, the arc) and
// enums are deliberately absent: a keyframe on a waveform switch is a different
// feature, and the arc already has its own editor.
struct ParamDesc {
    const char* key;    // "filter.cutoff" - the .drone key
    const char* label;  // "Filter cutoff" - for the lane header
    uint16_t offset;    // byte offset into Params
    uint8_t type;       // 0 = float, 1 = int, 2 = bool
};
const std::vector<ParamDesc>& paramTable();

// Index into paramTable() for a field, addressed by its byte offset inside
// Params (which is how the UI turns "the knob the user just moved" into a lane -
// it has the pointer, and every knob binds straight to a Params field).
int paramIndexForOffset(size_t offset);

// Reads/writes one parameter through its descriptor.
float paramGet(const Params& p, int paramIndex);
void paramSet(Params& p, int paramIndex, float value);

// The lane's value at `t` (linear between keyframes, held outside them).
float autoValueAt(const AutoLane& lane, float t);

// Writes every automated parameter into `p` at time `t`. Called once per
// control block by the synth (its own copy) and once per frame by the editor
// (so a knob under automation shows the value that is actually playing).
void applyAutomation(Params& p, double t);

// The lane for `paramIndex`, created if there is room; nullptr when the lane
// budget is full. `autoRemoveLane` keeps the array packed.
AutoLane* autoLaneFor(Params& p, int paramIndex);
const AutoLane* autoLaneFind(const Params& p, int paramIndex);
void autoRemoveLane(Params& p, int paramIndex);

// Records `value` at time `t`. A keyframe closer than `mergeSec` is MOVED
// instead of a new one added, which is what keeps a knob dragged during
// playback from leaving a smear of points (and what makes the lane budget
// survive a long take). Returns false only when no lane could be created.
bool autoWrite(Params& p, int paramIndex, float value, float t,
               float mergeSec = 0.35f);

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

    // Jumps the transport to `sec` (the timeline's playhead).
    //
    // Moving the clock is not enough to HEAR that moment: envelopes, glide, LFO
    // phase and the reverb/delay tails at 0:50 are the product of the history
    // from 0 to 0:50, so a bare clock jump lands you in the piece's opening
    // fade-in wherever you seek. So a seek SETTLES:
    //   - held notes snap to the chord at the new position (no glide from the
    //     old one) and their envelopes to the level a chord held since its own
    //     start would have reached,
    //   - the periodic LFOs jump to their analytic phase at that time,
    //   - the bell scheduler restarts there,
    //   - and `prerollSec` of audio is rendered and thrown away, which is what
    //     fills the delay and reverb lines - the one part no formula can supply.
    // The pre-roll costs real time (roughly 1/20th of what it simulates), so the
    // caller passes 0 while dragging a playhead and kSeekPreroll on release.
    void setTime(double sec, float prerollSec = 0.0f);

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
    // UI thread; the timeline's click-to-seek. `settle` runs the pre-roll (see
    // Synth::setTime) - pass false for the intermediate frames of a drag.
    void seek(double sec, bool settle = true);

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

// ---------------------------------------------------------------------------
// The Simple view's vocabulary (docs/drone-generator.md, "Simple and
// Detailed"). No new saved field: every control READS its value back from
// `Params` and WRITES by rescaling the fields the Detailed view edits, so the
// two views can never disagree and a .drone file is unchanged. Host-only like
// the rest, so a harness can check read(write(x)) == x without the GUI.
// ---------------------------------------------------------------------------

// A mood: a whole sound named after a USE ("Dark cave", "Tense") rather than
// a timbre. Every recipe's pass (chords x bars x bar) is 60 s, so a 30 s /
// 1 / 2 / 3 min track is a whole number of passes.
struct Mood {
    const char* name;
    const char* caption;  // one plain sentence: "Low and murky, with far-off drips."
    const char* tile;     // two-word tile caption: "murky hum, far drips"
    uint32_t top, bottom; // picture gradient (0xRRGGBB)
    uint32_t line;        // glyph / accent line colour (0xRRGGBB)
    Params params;
};
const std::vector<Mood>& moods();
// The empty starting point of every recipe: Params() minus its layers, mod
// rows and beds (Params() carries default rows that would leak into a mood).
Params moodBase();

namespace simple {

// Feelings (chord tables) and arc shapes, in segment order.
enum Feeling { FeelCalm = 0, FeelOpen, FeelMysterious, FeelUneasy, FeelDread, FeelCount };
enum Shape { ShapeSteady = 0, ShapeSwell, ShapeTwoWaves, ShapeBuildUp, ShapeCount };
const char* const* feelingNames();  // [FeelCount]
const char* const* shapeNames();    // [ShapeCount]

// The six Sound sliders, 0..1. write*(p, p0, r0, r1) sets p = p0 and moves
// every field it owns by the step r1 - r0 FROM THE SNAPSHOT p0 (so a drag never
// accumulates rounding; r1 == r0 gives p0 back bit for bit). A field with a
// timeline lane gets the same transform on every keyframe, and reads as the
// mean of its keyframes.
enum Slider { SlBright = 0, SlPitch, SlMove, SlSpace, SlWind, SlChimes, SlCount };
float read(const Params& p, int slider);
void write(Params& p, const Params& p0, int slider, float r0, float r1);
// Whether the slider can move at all (Movement from "still" needs a free
// motion slot) and whether one of its fields is automated.
bool writable(const Params& p, int slider);
bool automated(const Params& p, int slider);
const char* word(const Params& p, int slider, float r);  // "murky", "deep", ...

// Movement's sum: |amount| x LFO depth over the LFO/Random rows.
float motionSum(const Params& p);
// Mod rows that are free (no source or no destination).
int freeModRows(const Params& p);
// True when an Arc row with |amount| > 0.01 exists (the arc does something).
bool arcActive(const Params& p);

// Over the track. applyShape returns false (and changes nothing) when the arc
// needs a mod row and all six are in use. readShape: the shape, `exact` false
// when it is only the nearest.
bool applyShape(Params& p, int shape);
int readShape(const Params& p, bool* exact);

// Feeling: writes every used step's notes (A, B, A, ...) and the bell scale.
void applyFeeling(Params& p, int feeling);
int readFeeling(const Params& p, bool* exact);

// Length: whole passes (bars rescaled so n passes fill `sec`), then the loop
// tail rule. fitLoopTail: loopTail = 0.8 x reverb decay within [2, 30] and
// under 90 % of the length.
void applyLength(Params& p, float sec);
void fitLoopTail(Params& p);

// Loudness: Soft / Normal / Full = master.normalize 0.45 / 0.65 / 0.89; read
// is the nearest, -1 when normalize is off.
void applyLoudness(Params& p, int level);
int readLoudness(const Params& p);

// A mood replaces the whole sound; the length, format and mastering (except
// the loop tail) stay, automation is cleared.
void applyMood(Params& p, int mood);
// Whether `p` is exactly what picking `mood` now would give.
bool sameAsMood(const Params& p, int mood);
// Same mood, new details: a new seed, and LFO rates / bell density / air
// motion nudged from a stream seeded by it (so a take is reproducible).
void anotherTake(Params& p);

}  // namespace simple

}  // namespace dronegen

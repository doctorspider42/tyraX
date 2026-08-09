// Tools > Neural Upscaler (BLSS) - the host half of docs/neural-upscaler.md,
// made reachable without a terminal. Training, evaluation, cross-validation,
// the input-channel report, the comparison images and the emit step used to be
// `--blss-train` / `--blss-eval` / `--blss-emit` and nothing else.
//
// THE WINDOW RUNS THE EDITOR'S OWN EXECUTABLE, AND THAT IS THE LOAD-BEARING
// DECISION IN THIS FILE. Everything that drives those commands lives in
// blss.cpp's ANONYMOUS namespace - buildCorpus, labelCorpus, gatherSamples,
// evalRecurrent, crossValidateOnce, featureReport, netField are every one of
// them file-static - so a window that called "the public API" would have to
// re-implement the drivers, and a re-implemented driver is a SECOND answer to
// "what does --blss-eval measure". This feature has already published five
// numbers that were measured on the wrong thing (docs/neural-upscaler.md,
// "Measured is not optimised, five times"); a panel quoting its own arithmetic
// would be the sixth, and it would be the hardest to catch because it would
// look exactly like the CLI's.
//
// So: `Job` spawns `tyrax-editor --blss-<verb> ...` in the project directory,
// streams its stdout, and every number the window draws is PARSED OUT OF THAT
// TEXT by the functions below. The raw output is always on screen underneath
// the tables, which is what makes the parse falsifiable rather than trusted -
// if a table is empty or a column is wrong, the text that produced it is right
// there. A parser that finds nothing reports that it found nothing; it never
// invents a row.
//
// Host-only: no ImGui, no GL, no Project. The parsers are pure functions of a
// string, so the whole "did we read the tool's table correctly" question is
// answerable from a harness (the aobake/menulayout pattern).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {
class Process;
}

namespace blssui {

// Which CLI verb a run is. Kept as an enum rather than inferred from the
// command line because the progress heuristics and the parser are chosen from
// it, and "which table am I looking at" must not depend on string matching an
// argument list.
//
// `Headroom` is `--blss-eval` with NO `-i`: the net-free run that answers "will
// this scene benefit at all" without needing a trained network to exist. It is
// its own Kind rather than a flag on Eval because the two produce different
// tables - the net-free one has no BLSS row - and because the window's status
// line has to name what it is doing.
enum class Kind { None, Train, Eval, Headroom, Cv, Features, Emit };

const char* kindName(Kind k);

// ------------------------------------------------------------------- cost ---

// What a verb is about to cost, in wall-clock seconds. It exists because the
// three expensive buttons in this window (train, evaluate, cross-validate) used
// to say nothing at all about their price - and cross-validation is minutes to
// tens of minutes, which is not a thing to discover by pressing a button.
//
// A pure function of (work, cores), here with the parsers rather than inside a
// draw call, for the same reason summarise() is: it is checkable from the
// host-only harness against a real run's own `blss: timing` line.
struct Cost {
    double corpus = 0, oracle = 0, fit = 0, total = 0;
    int cores = 1;
    // How many independent corpus renders + labellings the verb pays for
    // (cross-validation pays one per --cv-seeds) and how many nets it fits.
    int corpora = 1, trainings = 1;
};

// `shots` is how many camera moves the corpus is expected to have - 13 for the
// bestiary, six per scene for a project - and is only used by the
// cross-validation estimate, whose fold count defaults to "one per shot".
// `seeds`/`folds` are --cv-seeds / --cv-folds (0 = every shot).
Cost estimate(Kind kind, int frames, int epochs, int cores, int seeds, int folds, int shots);

// "about 4 minutes", "about 25 seconds", "over an hour". Deliberately coarse:
// the model is calibrated on one machine and one corpus, so a figure printed to
// the second would claim a precision it does not have.
std::string humanDuration(double seconds);

// ---------------------------------------------------------------- progress ---

// A line of the tool's output, mapped to a phase and a fraction. Pure, so the
// mapping is checkable without a subprocess.
//
// `progress` is negative for "running, but this phase cannot say how far" -
// which is honest and necessary: crossValidateOnce() sets TrainConfig::verbose
// to false and prints NOTHING between "labelling" and the finished table, so a
// determinate bar there would be a fiction. The window shows elapsed time
// instead and says so.
struct Phase {
    float progress = -1.0f;
    std::string status;
};

// True when `line` moved the phase on. `epochs` is what the run was asked for
// (the epoch lines carry a number, not a fraction) and is the only thing the
// caller has to supply: the corpus renderer prints its own total now that it is
// threaded, so a project corpus - whose shot count is not known until its
// scenes have loaded - no longer needs the window to guess one.
bool phaseOf(const std::string& line, Kind kind, int epochs, Phase& out);

// -------------------------------------------------------------------- job ---

// One CLI run in a worker thread. At most one exists at a time (every verb here
// is CPU-bound and they would fight over the machine), which the window
// enforces by refusing to start a second.
class Job {
public:
    Job() = default;
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // `exe` is the editor's own binary, `args` the arguments after it (already
    // unquoted - the job shell-quotes them), `cwd` the project directory so a
    // default `-o blss.net` lands next to the .tyra.
    void start(Kind kind, const std::string& exe, const std::vector<std::string>& args,
               const std::string& cwd, int epochs);
    // Kills the process tree and joins. Safe to call when nothing is running.
    void cancel();

    bool running() const { return running_.load(); }
    Kind kind() const { return kind_.load(); }
    float progress() const { return progress_.load(); }
    int exitCode() const { return exit_.load(); }
    // Bumped once when a run FINISHES - the poll that parses the output reads
    // this, so a run that ended while the window was shut is still picked up
    // (the giBakerPoll rule).
    uint64_t version() const { return version_.load(); }
    // Seconds since start(), frozen when the run ends.
    double seconds() const;

    std::string status() const;
    std::string log() const;
    std::string command() const;

private:
    void run(std::string cmdline, std::string cwd, int epochs);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<Kind> kind_{Kind::None};
    std::atomic<float> progress_{-1.0f};
    std::atomic<int> exit_{-1};
    std::atomic<uint64_t> version_{0};
    std::atomic<double> started_{0.0};
    std::atomic<double> ended_{0.0};
    mutable std::mutex mutex_;
    std::string status_;
    std::string log_;
    std::string command_;
    std::shared_ptr<platform::Process> proc_;  // so cancel() can reach it
};

// ---------------------------------------------------------------- --blss-eval ---

// One method's row of the PSNR table. `occ` is false for the `native full-res`
// row, which prints "-" in the four occupancy columns because a full-resolution
// render draws no composite passes at all.
struct EvalRow {
    std::string name;
    double psnr = 0, flicker = 0;
    bool occ = false;
    double point = 0, temporal = 0, sharpen = 0, passes = 0;
    std::vector<double> perShot;  // aligned with EvalSplit::shots
    bool network = false;         // the row the tool marks "<-- the network"
};

struct EvalSplit {
    bool heldOut = false;
    int frames = 0;
    std::string caption;       // the tool's own title line, verbatim
    std::vector<int> shots;    // shot ids of the per-shot columns
    std::vector<EvalRow> rows;
};

struct EvalTable {
    std::vector<EvalSplit> splits;
    bool ok() const { return !splits.empty(); }
};

EvalTable parseEval(const std::string& text);

// THE ONE-LINE ANSWER, as arithmetic. "Will this scene benefit at all" has
// always been in the PSNR table and has always been the sixth row of it, so the
// window states it - and the numbers behind that sentence live HERE, with the
// parsers, for the same reason the parsers do: a pure function of an EvalTable
// is checkable from a harness, and a calculation buried in an ImGui draw call
// is not.
//
// Everything is frame-weighted over both splits, because they partition ONE
// corpus and neither half on its own is the answer about the scene.
//
// `oracleMargin` is the load-bearing figure. The oracle is the best any per-tile
// weighting can do under the exact GS composite, so it is the scene's CEILING:
// on examples/showcase it is +0.07 dB held-out and +0.00 dB over the training
// shots, at 1.01 and 1.00 passes - soft textures, low-poly props, nothing that
// aliases - and no network can beat a bound of zero.
struct EvalSummary {
    bool ok = false;
    // False for a NET-FREE run (`--blss-eval` with no `-i`), which prints
    // native / bilinear / oracle and no BLSS row at all. The ceiling is still
    // the whole answer to "will this scene benefit"; only the "how much of it
    // did the network capture" half is missing, and a verdict that invented a
    // net margin from a table with no net row would be the sixth number this
    // feature measured on the wrong thing.
    bool haveNet = false;
    int frames = 0;
    double bilinear = 0, net = 0, oracle = 0, native = 0;  // PSNR, dB
    double netPasses = 0, oraclePasses = 0;    // mean full-screen passes, 1.00 = bilinear
    double netMargin = 0;                      // net - bilinear
    double oracleMargin = 0;                   // oracle - bilinear: the ceiling
};
EvalSummary summarise(const EvalTable&);

// The one machine-readable line every `--blss-eval` prints:
//
//   [blss] verdict headroom=<dB> passes=<f> bilinear=<dB> oracle=<dB> native=<dB>
//
// Preferred over re-deriving those numbers from the table, because it is the
// TOOL'S OWN arithmetic over the whole corpus rather than the window's over
// what its parser managed to read back. `ok` is false when the line is absent
// (an older binary), and the caller falls back to summarise(); `haveNet` is
// always false here - the line describes the scene's ceiling and says nothing
// about any network.
EvalSummary parseVerdictLine(const std::string& text);

// ------------------------------------------------------------ --blss-eval --cv ---

struct CvFold {
    int index = 0;
    std::string shot;
    std::vector<double> perSeed;  // one per --cv-seeds
    double mean = 0, sd = 0;
    // The absolutes table, filled from the second half of the output. `have`
    // stays false when only the margin table was printed.
    bool have = false;
    double native = 0, bilinear = 0, blss = 0, oracle = 0, passes = 0, flicker = 0, inDist = 0;
};

struct CvDeadzone {
    double alpha = 0, margin = 0, passes = 0, point = 0, temporal = 0, sharpen = 0;
    int below = 0, runs = 0;
    bool thisBuild = false;
};

struct CvTable {
    std::string caption;            // the four header lines, verbatim
    std::vector<std::string> seeds; // "B1557", ...
    std::vector<CvFold> folds;
    std::vector<double> seedMeans;
    double mean = 0, sd = 0, seedSd = 0, passes = 0, passesSd = 0;
    int below = 0, runs = 0;
    std::vector<CvDeadzone> deadzone;
    bool ok() const { return !folds.empty(); }
};

CvTable parseCv(const std::string& text);

// -------------------------------------------------------- --blss-eval --features ---

struct FeatureRow {
    std::string name;
    double mean = 0, sd = 0, min = 0, max = 0, at0 = 0, at1 = 0;
    double rPoint = 0, rTemporal = 0, rSharpen = 0;
};

struct FeatureShotRow {
    std::string name;
    std::vector<double> means;
    double spread = 0;
};

struct FeatureTable {
    std::string caption;
    std::vector<FeatureRow> rows;
    std::vector<std::string> shots;
    std::vector<FeatureShotRow> perShot;
    bool ok() const { return !rows.empty(); }
};

FeatureTable parseFeatures(const std::string& text);

// -------------------------------------------- --blss-eval --features --probe ---

// One channel of a CONSOLE-measured feature vector placed inside the corpus'
// own distribution - `--probe "<BLSSFEAT line>"`'s table, read back.
//
// This is the only instrument that can answer "is the network being fed what it
// was trained on", and it found both host/console divergences this feature has
// had (the whole-mesh bag proxy and the sky dome). It was also, until now, a
// chore: the user had to find a line in a log by hand and paste it into a CLI.
struct ProbeRow {
    std::string name;
    bool given = false;   // the BLSSFEAT line carried this channel at all
    double lo = 0, mid = 0, hi = 0;      // the console's own min/mean/max
    double spread = 0;                   // hi - lo, as the tool prints it
    double corpusLo = 0, corpusHi = 0;   // what the corpus covers
    double pct = 0;    // where the console's MEAN falls in the corpus, %
    double supp = 0;   // % of the corpus within +-0.05 of it
    double band = 0;   // % of the corpus inside the console's own min..max
    std::string verdict;  // the tool's own words, verbatim
    // Derived from `verdict` so the UI colours without re-deciding: the tool
    // owns the thresholds (1% support, the min/max box) and a second set of
    // them here would be a second answer to the same question.
    bool outOfRange = false, noSupport = false, constant = false;
};

struct ProbeTable {
    std::vector<ProbeRow> rows;
    int outOfRange = 0, constants = 0, tails = 0, missing = 0;
    bool ok() const { return !rows.empty(); }
};

ProbeTable parseProbe(const std::string& text);

// The one-line answer over that table, with the same refusal-to-reassure rule
// the speed verdict follows: a probe with a channel out of the corpus' range is
// stated as a mismatch, not as a percentage.
struct ProbeVerdict {
    enum class Level { Unknown, Matches, Thin, Mismatch };
    Level level = Level::Unknown;
    std::string headline, why;
};
ProbeVerdict probeVerdict(const ProbeTable&);

// The engine writes one `BLSSFEAT` line into the game's log about once a
// second (debug view 2). This pulls the LAST one out of a log's text - the
// last is the interesting one, because it describes the frame the player was
// looking at most recently. Empty when the log carries none, which is the
// honest answer for a game that was never built with the debug view on.
std::string lastFeatLine(const std::string& logText);

// ----------------------------------------------------- is the corpus any good ---

// WHAT IS WRONG WITH THIS CORPUS, IN THE USER'S TERMS. `--features` has always
// printed the numbers that say a channel is dead or pinned; nobody reading a
// column of sds knows that "texDetail sd 0.000" means "your corpus has no
// textured surfaces, so the network cannot learn the channel that predicts
// texture aliasing". That sentence is what this produces.
struct CorpusFinding {
    enum class Level { Note, Warn, Fatal };
    Level level = Level::Note;
    std::string channel;  // "" for a finding about the corpus as a whole
    std::string what;     // what the numbers say, in plain words
    std::string fix;      // what to do about it
};

struct CorpusHealth {
    bool ok = false;  // a --features report was actually read
    enum class Verdict {
        Unknown,   // nothing measured - and this must NEVER read as "fine"
        Good,      // every channel moves and none is pinned
        Thin,      // trainable, but something is not being taught
        Unusable   // a channel is constant: the net cannot learn it at all
    };
    Verdict verdict = Verdict::Unknown;
    std::string headline, why;
    std::vector<CorpusFinding> findings;
    int dead = 0, pinned = 0, flat = 0, blind = 0;
    int shots = 0;
};

// `shots` comes from the table itself (the per-shot columns). Pure, so all four
// verdicts are walkable from a harness - and they are checked against two real
// corpora with known answers: examples/upscaler-lab has real headroom and
// examples/showcase has +0.01 dB and a thin corpus, so a health report calling
// showcase's corpus Good is wrong by construction.
CorpusHealth corpusHealth(const FeatureTable&);

// What a channel is FOR, in one clause, so a finding can name the consequence
// rather than the symptom. Empty for a name that is not a channel.
const char* channelPurpose(const std::string& name);

// ------------------------------------------------ did the plan reach the tool ---

// `[blss]   scene 'x': N mesh(es) + ..., M shot(s)` - what the corpus loader
// says it found. The window compares it against the authored shot plan, which
// is the only way an author can tell "the trainer honoured my plan" from "the
// trainer is still shooting its six defaults". A parse that finds nothing
// reports nothing.
struct CorpusScene {
    std::string name;
    int shots = 0;
    size_t triangles = 0;
};
std::vector<CorpusScene> parseCorpusScenes(const std::string& text);

// -------------------------------------------------------------- will it be faster ---

// THE MEASURED SPEED MODEL, in one place, with its provenance attached. Every
// one of these came off a real PS2 and NOT off PCSX2, which under-reports GS
// fill by 76x and is why this feature published a 3.37x headline nobody could
// run. They are constants rather than settings because a knob here would let a
// reader tune the verdict until it said what they wanted.
//
// CALIBRATED AGAINST FIVE LOAD POINTS, TWO RUNS PER ARM, on 2026-08-09
// (docs/profiling.md, "Calibrating the speed model against hardware"). A
// frame-indexed script stepped `examples/upscaler-lab`'s six haze banks so the
// two arms stay paired, and a final segment shrank every particle to
// `emitSize 0.05` - every particle still simulated and submitted, raster area
// gone - which is what SEPARATES the emitters' EE cost from their fill and is
// the reason the retention term below is measured rather than inferred from a
// slope ratio that had EE in both arms. The fit over a 0.7-34 ms fill range:
//
//   saved(ms) = 0.7548 x (scene fill, ms) - 5.10,  residual RMS 0.093 ms
//
// Two independent confirmations came out of it. The retention term was RIGHT -
// 0.7548 measured against the 0.741 assumed, i.e. the shipped model was 2 %
// conservative - and the fit's intercept, 5.10 ms, reproduces the
// independently-counted 4.60 EE + 0.50 composite fill to two decimals. Two
// instruments, one number: the model's physics is sound and it was its INPUT
// that was wrong (see kAnchorCoverages).
namespace fill {
// What survives the reduced render. `blssScale 0` is Scale::X2Y2 - half in EACH
// axis - so a quarter of the pixels; the fitted slope says 24.52 % survives, so
// the extra 0.5 % over a clean quarter is measurement. This page said "half"
// for a week, and the break-even computed off that was ~70 % too pessimistic.
constexpr double kKeptFraction = 0.2452;
constexpr double kSavedFraction = 1.0 - kKeptFraction;  // 0.7548, fitted
// One full-screen alpha-blended textured pass on hardware, its own draw_finish.
// THE UNIT `coverages` IS COUNTED IN - see kAnchorCoverages for what that costs
// an estimator that counts an opaque untextured fragment as the same thing.
constexpr double kPassMs = 0.587;
// BLSS' EE bill as shipped: proxy 2.34 + reproj 0.28 + feat 0.19 + net 0.78 +
// pkt 0.50 + begin 0.41 + end 0.10.
constexpr double kEeCostMs = 4.60;
// ...and the fill the composite itself adds back, measured in the same runs.
constexpr double kCompositeGsMs = 0.50;
// THE ONE SCENE WHERE BOTH ENDS OF THIS MODEL HAVE BEEN MEASURED, and the
// number that moved most. Working back from the five-point fit,
// `examples/upscaler-lab`'s true fill is 34.46 ms, which at kPassMs is 58.7
// blended-pass equivalents - not the 75 the fragment counter reports. The
// counter is the term that over-predicts, not the model: `coverages x 0.587`
// over-states this frame by 28 %, because 0.587 ms is the cost of a BLENDED
// TEXTURED pass and an opaque untextured fragment costs a fraction of that.
// So this anchor is stated in blended-pass equivalents and the counter is
// being taught to produce them (blsscorpus.cpp).
constexpr double kAnchorCoverages = 58.7;
constexpr double kAnchorSpeedup = 1.63;
constexpr double kAnchorOffMs = 52.95, kAnchorOnMs = 32.42;
// VRAM handed back at 2x2 on a 512x448 output, and it is EXACTLY ZERO at 1x2 -
// the low-res target costs precisely what the z-buffer saves.
constexpr int kVramBackKb2x2 = 448;

// 0.7548 x 0.587 x C > 4.60 + 0.50: 11.5 full-screen coverages. DERIVED rather
// than written down, so a millisecond taken off the EE bill moves it - which is
// exactly what this round's re-measurement did (13 -> 11.5).
inline double breakEven() {
    return (kEeCostMs + kCompositeGsMs) / (kSavedFraction * kPassMs);
}
}  // namespace fill

// What the model says about a scene that rasterises `coverages` times over.
//
// A RANGE, NEVER A NUMBER, and the reason is that the model knows the GS half
// of the frame and nothing at all about the EE half. Saving X ms is a 2.6x
// speedup on a frame that is entirely fill and a 1.3x speedup on one that is
// half EE, and the estimator cannot tell those apart - so the two ends are
// stated with the assumption that produces them, and the one hardware A/B this
// feature has is quoted next to them for scale. A confident single figure here
// would be the sixth number this feature measured on the wrong thing.
struct SpeedEstimate {
    bool ok = false;
    double coverages = 0;   // what went in
    double fillMs = 0;      // coverages x kPassMs: the scene's own GS bill
    double savedMs = 0;     // net of BLSS' EE cost; negative = the frame gets longer
    double breakEven = 0;
    // Speedup multipliers. `hi` assumes the frame is ENTIRELY fill (the
    // GS-bound limit, which nothing can beat); `lo` assumes fill is 60 % of it.
    // Both are 1.0 when the estimate is a loss - a slowdown is stated in
    // milliseconds, because a "0.8x speedup" is a sentence nobody parses.
    double lo = 1.0, hi = 1.0;
    enum class Band { Loss, Marginal, Win };
    Band band = Band::Loss;
};
SpeedEstimate speedFrom(double coverages);

// ------------------------------------------------------------- one answer ---

// THE TWO VERDICTS AS ONE. A reader wants "should I turn this on", not a
// quality table beside a speed table - and the combination has cases neither
// half has on its own, the common one being "no picture to gain AND below the
// break-even", which is most scenes and is a flat no.
//
// Pure, and here rather than in the draw call for the same reason summarise()
// is: three inputs, six outcomes, and a harness can walk all of them.
struct Recommendation {
    enum class Verdict {
        Unknown,      // neither half has been measured yet
        Off,          // no headroom and no speed: the easy, common no
        SpeedOnly,    // the picture will not improve; the frame will get shorter
        QualityOnly,  // the picture improves; the frame gets LONGER
        On,           // both
        Mixed         // one half known, and it is not enough to decide alone
    };
    Verdict verdict = Verdict::Unknown;
    std::string headline;  // one sentence, the answer
    std::string why;       // the two facts behind it
};
Recommendation recommend(const EvalSummary& quality, bool haveQuality,
                         const SpeedEstimate& speed, bool haveSpeed);

// Below this many dB the ORACLE - the best any per-tile weighting can do under
// the exact GS composite - is indistinguishable from plain bilinear, and no
// network can beat a bound of zero. Measured: on examples/showcase the oracle
// scores +0.07 dB held-out and +0.02 dB on the rest.
constexpr double kNoHeadroomDb = 0.10;

// ------------------------------------------------------------------ errors ---

// The `blss: ...` lines the tool prints when it refuses to run - a missing
// blss.net, an empty corpus, an emitter self-test failure. Returned so the
// window can put the reason where the user is looking instead of only in the
// output pane.
std::vector<std::string> parseErrors(const std::string& text);

}  // namespace blssui

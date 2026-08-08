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
enum class Kind { None, Train, Eval, Cv, Features, Emit };

const char* kindName(Kind k);

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
    int frames = 0;
    double bilinear = 0, net = 0, oracle = 0;  // PSNR, dB
    double netPasses = 0, oraclePasses = 0;    // mean full-screen passes, 1.00 = bilinear
    double netMargin = 0;                      // net - bilinear
    double oracleMargin = 0;                   // oracle - bilinear: the ceiling
};
EvalSummary summarise(const EvalTable&);

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

// ------------------------------------------------------------------ errors ---

// The `blss: ...` lines the tool prints when it refuses to run - a missing
// blss.net, an empty corpus, an emitter self-test failure. Returned so the
// window can put the reason where the user is looking instead of only in the
// output pane.
std::vector<std::string> parseErrors(const std::string& text);

}  // namespace blssui

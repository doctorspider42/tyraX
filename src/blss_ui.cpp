// The host half of Tools > Neural Upscaler (BLSS): the job that drives the
// editor's own CLI, and the parsers that turn its printed tables back into
// numbers. See blss_ui.hpp for WHY this spawns a process instead of calling
// blss.cpp - the short version is that every driver in that file is
// file-static, so the alternative was a second implementation of "what
// --blss-eval measures", and this feature has already published five numbers
// that were measured on the wrong thing.
//
// No ImGui, no GL, no Project - deliberately, because the parsers are the
// risky part of that design and this way they are pure functions of a string
// that a harness can feed a captured run (the aobake/menulayout pattern). The
// window itself is blss_window.cpp.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "blss.hpp"  // kFeatureNames - read-only, never edited here
#include "blss_ui.hpp"
#include "platform.hpp"

// ===========================================================================
// blssui:: - the host half. No ImGui, no GL, no Project.
// ===========================================================================

namespace blssui {

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Train: return "--blss-train";
        case Kind::Eval: return "--blss-eval";
        case Kind::Headroom: return "--blss-eval (no network)";
        case Kind::Cv: return "--blss-eval --cv";
        case Kind::Features: return "--blss-eval --features";
        case Kind::Emit: return "--blss-emit";
        case Kind::None: break;
    }
    return "";
}

// ------------------------------------------------------------------- cost ---
//
// MEASURED, on this branch, on examples/procedural at 512x448, on one machine
// with 24 hardware threads - from the tool's own `blss: timing` line where it
// prints one and from the wall clock where it does not:
//
//   --blss-train  36 frames, 100 epochs, --threads 1 : corpus 2.7 s, oracle 11.6 s, fit 0.4 s
//   --blss-train 156 frames, 400 epochs, every core  : corpus 3.3 s, oracle  3.9 s, fit 7.4 s
//   --blss-eval  156 frames, --threads 1             : corpus 13.4 s, eval 138.0 s
//   --blss-eval  156 frames, --threads 6             : corpus  5.6 s, eval  46.6 s
//   --blss-eval  156 frames, every core              : corpus  3.3 s, eval  39.1 s
//   --blss-eval  156 frames, NET-FREE, every core    : corpus  3.6 s, eval  39.5 s
//   --blss-eval --cv  36 frames, 100 epochs, 2 folds :  6 s
//   --blss-eval --cv  36 frames, 100 epochs, 6 folds :  9 s
//   --blss-eval --cv 156 frames, 400 epochs, 6 folds : 50 s
//
// (Net-free evaluates one method row fewer and is not measurably cheaper, so
// one constant covers both.)
//
// Three things those numbers say that are not obvious from the code, and each
// is why one of the constants below exists.
//
// THE FIT IS SEQUENTIAL and at the shipped defaults it is the largest phase of
// a training run - each Adam step reads the weights the previous one wrote, so
// `--threads` cannot touch it.
//
// THE TWO PARALLEL PHASES SCALE VERY DIFFERENTLY. A corpus worker owns ~30 MB
// of raster scratch, so the render is memory-bandwidth bound and saturates at
// about 4x however many cores are thrown at it (3.5x measured at 24 threads
// here; 3.95x measured at 6 cores in docs/neural-upscaler.md - the same ceiling
// reached from both ends). The oracle's LABELLING pass is coordinate descent
// over one tile at a time and scales nearly linearly (12.9x at 24 threads,
// 5.25x at 6 in the docs).
//
// AND `--blss-eval` IS NOT `--blss-train` MINUS THE FIT - it is ten times
// slower than that, and it scales quite differently. Evaluation closes the
// temporal loop (every frame's history is the previous frame's real composite,
// which is what the console has), so the unit of parallelism is a SHOT RUN
// rather than a frame: a corpus of six camera moves has about six independent
// chains however many cores are watching. Measured here, 156 frames: 138.0 s at
// one thread, 46.6 at six, 39.1 at twenty-four - a ceiling around 3.6x, where
// the labelling pass reaches 12.9x on the same machine. A thirteen-shot
// bestiary would go further and the model does not try to predict that; it
// takes the six-move project's ceiling, which errs toward over-quoting the
// wait, which is the right direction to be wrong in.
//
// This is an ESTIMATE, the window says "about", and humanDuration() rounds. It
// is calibrated on one machine and one project; a scene with ten times the
// triangles renders its corpus proportionally slower. Treat a factor of two as
// within tolerance and never print it to the second.
namespace {
constexpr double kCorpusPerFrame = 0.075;      // s, one thread
constexpr double kOraclePerFrame = 0.32;       // s, one thread, threaded labelling
constexpr double kEvalPerFrame = 0.885;        // s, one thread, ALL the method rows
constexpr double kCvFoldEvalPerFrame = 0.008;  // s, one fold's own evaluation
constexpr double kFitPerFrameEpoch = 1.15e-4;  // s, sequential
// Loading the project, walking its scenes into triangles, reading materials.
constexpr double kStartup = 2.5;
// The two ceilings, both measured rather than guessed - see above.
constexpr double kCorpusMaxSpeedup = 4.0;
constexpr double kEvalMaxSpeedup = 3.6;

double parallelCorpus(int cores) {
    const double n = (double)std::clamp(cores, 1, 32);
    return std::clamp(1.0 + 0.60 * (n - 1.0), 1.0, kCorpusMaxSpeedup);
}
double parallelOracle(int cores) {
    return std::pow((double)std::clamp(cores, 1, 32), 0.8);
}
double parallelEval(int cores) {
    return std::min(std::pow((double)std::clamp(cores, 1, 32), 0.6), kEvalMaxSpeedup);
}
}  // namespace

Cost estimate(Kind kind, int frames, int epochs, int cores, int seeds, int folds, int shots) {
    Cost c;
    c.cores = std::clamp(cores, 1, 32);
    frames = std::max(1, frames);
    epochs = std::max(0, epochs);
    const double corpus1 = frames * kCorpusPerFrame / parallelCorpus(c.cores);
    const double oracle1 = frames * kOraclePerFrame / parallelOracle(c.cores);
    const double fit1 = (double)frames * epochs * kFitPerFrameEpoch;

    switch (kind) {
        case Kind::Train:
            c.corpora = c.trainings = 1;
            c.corpus = kStartup + corpus1;
            c.oracle = oracle1;
            c.fit = fit1;
            break;
        case Kind::Eval:
        case Kind::Headroom:
            c.corpora = 1;
            c.trainings = 0;
            c.corpus = kStartup + corpus1;
            // Its own parallel model: the chains are shot runs, not frames.
            c.oracle = frames * kEvalPerFrame / parallelEval(c.cores);
            c.fit = 0.0;
            break;
        case Kind::Features:
            // The channel report labels the corpus once and correlates; no
            // recurrent evaluation, so it is a training run minus the fit.
            c.corpora = 1;
            c.trainings = 0;
            c.corpus = kStartup + corpus1;
            c.oracle = oracle1;
            c.fit = 0.0;
            break;
        case Kind::Cv: {
            // Per --cv-seeds: one corpus render and one labelling. Then one
            // TRAINING per fold on top of that, which is what makes this the
            // expensive button in the window - and the one that never said so.
            const int s = std::max(1, seeds);
            const int f = folds > 0 ? folds : std::max(1, shots);
            c.corpora = s;
            c.trainings = s * f;
            c.corpus = kStartup + corpus1 * s;
            c.oracle = oracle1 * s;
            // A fold trains on all but one shot, so ~(f-1)/f of the samples,
            // and then evaluates the net it just fitted on both splits.
            const double share = f > 1 ? (double)(f - 1) / (double)f : 1.0;
            c.fit = fit1 * share * c.trainings +
                    (double)c.trainings * frames * kCvFoldEvalPerFrame;
            break;
        }
        case Kind::Emit:
        case Kind::None: break;
    }
    c.total = c.corpus + c.oracle + c.fit;
    return c;
}

std::string humanDuration(double seconds) {
    char buf[64];
    if (seconds <= 0.0) return "no time at all";
    if (seconds < 90.0) {
        // Round to 5 s so two neighbouring frame counts do not read as a
        // precision this model does not have.
        const int s = std::max(5, (int)(seconds / 5.0 + 0.5) * 5);
        std::snprintf(buf, sizeof(buf), "about %d seconds", s);
        return buf;
    }
    if (seconds < 3600.0) {
        const int m = (int)(seconds / 60.0 + 0.5);
        std::snprintf(buf, sizeof(buf), "about %d minute%s", m, m == 1 ? "" : "s");
        return buf;
    }
    const double h = seconds / 3600.0;
    std::snprintf(buf, sizeof(buf), "about %.1f hours", h);
    return buf;
}

namespace {

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        while (at < s.size() && std::isspace((unsigned char)s[at])) ++at;
        const size_t start = at;
        while (at < s.size() && !std::isspace((unsigned char)s[at])) ++at;
        if (at > start) out.push_back(s.substr(start, at - start));
    }
    return out;
}

// The WHOLE token has to be a number. This is what keeps a name column out of
// the value columns: "full-res", "shot0" and "boxes-sphere" all fail here, so
// the first token that passes is where the numbers start, whatever the name
// was and however far the printf's %-11s overran (it does, on two rows).
bool asNumber(const std::string& t, double& out) {
    if (t.empty()) return false;
    const char* c = t.c_str();
    char* end = nullptr;
    const double v = std::strtod(c, &end);
    if (end != c + t.size()) return false;
    out = v;
    return true;
}

bool asPercent(const std::string& t, double& out) {
    if (t.size() < 2 || t.back() != '%') return false;
    return asNumber(t.substr(0, t.size() - 1), out);
}

// "-" is what the tool prints where a column does not apply.
bool isDash(const std::string& t) { return t == "-"; }

std::string join(const std::vector<std::string>& t, size_t from, size_t to) {
    std::string s;
    for (size_t i = from; i < to && i < t.size(); ++i) {
        if (!s.empty()) s += ' ';
        s += t[i];
    }
    return s;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// Strip a trailing ")" / "," / ";" so a token lifted out of a prose summary
// line ("(sd 0.61)") still parses.
std::string unpunct(const std::string& t) {
    std::string s = t;
    while (!s.empty() && (s.back() == ')' || s.back() == ',' || s.back() == ';' || s.back() == ':'))
        s.pop_back();
    while (!s.empty() && s.front() == '(') s.erase(0, 1);
    return s;
}

double numAfter(const std::vector<std::string>& t, const char* key, double def = 0.0) {
    for (size_t i = 0; i + 1 < t.size(); ++i)
        if (unpunct(t[i]) == key) {
            double v = 0;
            if (asNumber(unpunct(t[i + 1]), v)) return v;
        }
    return def;
}

double numBefore(const std::vector<std::string>& t, const char* key, double def = 0.0) {
    for (size_t i = 1; i < t.size(); ++i)
        if (unpunct(t[i]) == key) {
            double v = 0;
            if (asNumber(unpunct(t[i - 1]), v)) return v;
        }
    return def;
}

}  // namespace

// ---------------------------------------------------------------- progress ---

bool phaseOf(const std::string& line, Kind kind, int epochs, Phase& out) {
    const std::string t = trimmed(line);
    if (t.empty()) return false;

    // The corpus rasteriser, which is the slow part of every verb.
    if (t.rfind("blss: rendering", 0) == 0) {
        out.progress = 0.0f;
        out.status = "rendering the training corpus";
        return true;
    }
    if (t.rfind("[blss] materials:", 0) == 0) {
        out.progress = 0.0f;
        out.status = "gathering corpus materials";
        return true;
    }
    // "[blss]   rendered 84 of 156 frame(s)". The corpus renderer is threaded,
    // so the unit of progress is a FRAME and the total is on the line - it no
    // longer needs the caller to know how many shots the corpus has, which a
    // project corpus only decides once it has loaded the scenes.
    if (t.rfind("[blss]   rendered ", 0) == 0) {
        const std::vector<std::string> tok = tokenize(t);
        double done = 0, total = 0;
        if (tok.size() > 4 && asNumber(tok[2], done) && asNumber(tok[4], total) && total > 0) {
            out.progress = 0.60f * (float)(done / total);
            out.status = "rendering the corpus, frame " + std::to_string((int)done) + " of " +
                         std::to_string((int)total);
            return true;
        }
        return false;
    }
    // "[blss]   shot 3 poles          pan          12 frame(s)  1234 ms cpu"
    // is now a SUMMARY, printed after every frame is rendered, so it reports
    // the phase as finished rather than winding the bar back to shot 1.
    if (t.rfind("[blss]   shot ", 0) == 0) {
        out.progress = 0.60f;
        out.status = "corpus rendered";
        return true;
    }
    if (t.rfind("[blss] corpus ready", 0) == 0) {
        out.progress = 0.60f;
        out.status = "corpus ready";
        return true;
    }
    if (t.rfind("blss: labelling", 0) == 0) {
        out.progress = 0.62f;
        out.status = "labelling frames with the oracle (coordinate descent per tile)";
        return true;
    }
    if (t.rfind("blss: training on", 0) == 0) {
        out.progress = 0.75f;
        out.status = "training";
        return true;
    }
    // "  epoch  350  loss 0.012345" - printed every 50 epochs by train() when
    // verbose. Cross-validation turns verbose OFF (crossValidateOnce sets
    // tc.verbose = false), so this never fires there and the bar stays
    // indeterminate for the whole fold loop, which is the honest answer.
    if (t.rfind("epoch ", 0) == 0) {
        const std::vector<std::string> tok = tokenize(t);
        double e = 0;
        if (tok.size() > 1 && asNumber(tok[1], e) && epochs > 0) {
            out.progress = 0.75f + 0.24f * (float)(e / epochs);
            out.status = "training, epoch " + std::to_string((int)e) + " of " +
                         std::to_string(epochs);
            return true;
        }
        return false;
    }
    if (t.rfind("blss: seed 0x", 0) == 0) {
        out.progress = -1.0f;
        out.status = "cross-validating: " + t.substr(6);
        return true;
    }
    // "[blss] fold 3 of 13" - one line per fold as it LANDS. The trainer's own
    // verbosity is off inside a fold (crossValidateOnce sets tc.verbose = false),
    // so the epoch line never fires here and this is the only progress the loop
    // emits. The count is completion order, not the fold index - folds run in
    // parallel - which is exactly what a bar wants and not what a label wants,
    // so the status says how many are done rather than naming one.
    if (t.rfind("[blss] fold ", 0) == 0) {
        const std::vector<std::string> tok = tokenize(t);
        double done = 0, total = 0;
        if (tok.size() > 4 && asNumber(tok[2], done) && asNumber(tok[4], total) && total > 0) {
            // The fold loop owns everything after labelling (0.62).
            out.progress = 0.62f + 0.37f * (float)(done / total);
            out.status = "cross-validating, " + std::to_string((int)done) + " of " +
                         std::to_string((int)total) + " fold(s) done";
            return true;
        }
        return false;
    }
    if (t.rfind("blss: final loss", 0) == 0 || t.rfind("blss: wrote", 0) == 0) {
        out.progress = 1.0f;
        out.status = t.substr(6);
        return true;
    }
    if ((kind == Kind::Eval || kind == Kind::Headroom) && t.rfind("half-res + ", 0) == 0) {
        out.progress = -1.0f;
        out.status = "evaluating: " + t.substr(11, 24);
        return true;
    }
    return false;
}

// -------------------------------------------------------------------- job ---

namespace {
double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

Job::~Job() { cancel(); }

void Job::start(Kind kind, const std::string& exe, const std::vector<std::string>& args,
                const std::string& cwd, int epochs) {
    cancel();
    std::string cmd = platform::shellArg(exe);
    for (const std::string& a : args) cmd += " " + platform::shellArg(a);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        log_.clear();
        command_ = cmd;
        status_ = "starting";
    }
    kind_.store(kind);
    progress_.store(0.0f);
    exit_.store(-1);
    cancel_.store(false);
    running_.store(true);
    started_.store(nowSeconds());
    ended_.store(0.0);
    worker_ = std::thread(&Job::run, this, cmd, cwd, epochs);
}

void Job::run(std::string cmdline, std::string cwd, int epochs) {
    platform::Process::Options opts;
    opts.cwd = cwd;
    opts.capture = true;
    std::shared_ptr<platform::Process> proc(platform::Process::start(cmdline, opts).release());
    if (!proc) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_ += "[editor] could not start: " + cmdline + "\n";
        status_ = "could not start the editor's own binary";
        running_.store(false);
        exit_.store(-1);
        ended_.store(nowSeconds());
        version_.fetch_add(1);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        proc_ = proc;
    }
    const Kind k = kind_.load();
    std::string line;
    while (proc->readLine(line)) {
        Phase ph;
        const bool moved = phaseOf(line, k, epochs, ph);
        std::lock_guard<std::mutex> lock(mutex_);
        log_ += line;
        log_ += '\n';
        if (moved) {
            progress_.store(ph.progress);
            status_ = ph.status;
        }
    }
    const int rc = proc->wait();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        proc_.reset();
        if (cancel_.load())
            status_ = "cancelled";
        else if (rc == 0)
            status_ = "done";
        else
            status_ = "the tool exited with code " + std::to_string(rc);
    }
    exit_.store(cancel_.load() ? -2 : rc);
    ended_.store(nowSeconds());
    progress_.store(1.0f);
    running_.store(false);
    version_.fetch_add(1);
}

void Job::cancel() {
    if (worker_.joinable()) {
        cancel_.store(true);
        std::shared_ptr<platform::Process> p;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            p = proc_;
        }
        if (p) p->kill();
        worker_.join();
    }
    running_.store(false);
}

double Job::seconds() const {
    const double s = started_.load();
    if (s <= 0.0) return 0.0;
    const double e = ended_.load();
    return (e > 0.0 ? e : nowSeconds()) - s;
}

std::string Job::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
std::string Job::log() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return log_;
}
std::string Job::command() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return command_;
}

// ------------------------------------------------------------ parse: eval ---

EvalTable parseEval(const std::string& text) {
    EvalTable out;
    EvalSplit* cur = nullptr;
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);
        if (line.empty()) continue;

        // "HELD-OUT shots - 48 frame(s), PSNR vs supersampled truth (...)"
        if (line.find(" shots ") != std::string::npos &&
            line.find("frame(s), PSNR") != std::string::npos) {
            EvalSplit s;
            s.heldOut = line.rfind("HELD-OUT", 0) == 0;
            s.caption = line;
            // "- 16 frame(s)," - the count is the token BEFORE the one that
            // says frame(s), and that token carries a comma, so it cannot be
            // matched literally (unpunct() eats the ')' too).
            const std::vector<std::string> cap = tokenize(line);
            for (size_t i = 1; i < cap.size(); ++i)
                if (cap[i].rfind("frame(s)", 0) == 0) {
                    double v = 0;
                    if (asNumber(cap[i - 1], v)) s.frames = (int)v;
                    break;
                }
            out.splits.push_back(s);
            cur = &out.splits.back();
            continue;
        }
        if (!cur) continue;

        std::vector<std::string> tok = tokenize(line);
        // The column header carries the shot ids of the per-shot columns.
        if (!tok.empty() && tok[0] == "overall") {
            cur->shots.clear();
            for (const std::string& t : tok)
                if (t.rfind("shot", 0) == 0) {
                    double v = 0;
                    if (asNumber(t.substr(4), v)) cur->shots.push_back((int)v);
                }
            continue;
        }

        EvalRow row;
        std::string body = line;
        const size_t marker = body.find("<--");
        if (marker != std::string::npos) {
            row.network = true;
            body = trimmed(body.substr(0, marker));
        }
        if (body.rfind("half-res + ", 0) == 0) body = body.substr(11);
        tok = tokenize(body);

        size_t i = 0;
        double v = 0;
        while (i < tok.size() && !asNumber(tok[i], v)) ++i;
        if (i == 0 || i + 5 >= tok.size()) continue;  // no name, or not enough columns

        // Shape check: three occupancy columns then the pass count, each either
        // a percentage or the tool's "-". Anything else is prose, not a row.
        const bool shaped =
            (isDash(tok[i + 2]) || tok[i + 2].back() == '%') &&
            (isDash(tok[i + 3]) || tok[i + 3].back() == '%') &&
            (isDash(tok[i + 4]) || tok[i + 4].back() == '%');
        double dummy = 0;
        if (!shaped || !asNumber(tok[i + 1], dummy)) continue;

        row.name = join(tok, 0, i);
        asNumber(tok[i], row.psnr);
        asNumber(tok[i + 1], row.flicker);
        row.occ = asPercent(tok[i + 2], row.point) && asPercent(tok[i + 3], row.temporal) &&
                  asPercent(tok[i + 4], row.sharpen) && asNumber(tok[i + 5], row.passes);
        for (size_t j = i + 6; j < tok.size(); ++j) {
            double p = 0;
            if (asNumber(tok[j], p)) row.perShot.push_back(p);
        }
        cur->rows.push_back(row);
    }
    // A split with a caption and no rows is a run that died mid-table; drop it
    // rather than drawing an empty frame around nothing.
    out.splits.erase(std::remove_if(out.splits.begin(), out.splits.end(),
                                    [](const EvalSplit& s) { return s.rows.empty(); }),
                     out.splits.end());
    return out;
}

namespace {
// A row by the tool's own label. parseEval strips the "half-res + " prefix and
// keeps everything before the first token that is wholly a number, so a prefix
// match on "bilinear" / "BLSS" / "oracle" is matching what the tool printed.
const EvalRow* rowNamed(const EvalSplit& sp, const char* prefix) {
    for (const EvalRow& r : sp.rows)
        if (r.name.rfind(prefix, 0) == 0) return &r;
    return nullptr;
}
}  // namespace

EvalSummary summarise(const EvalTable& t) {
    EvalSummary s;
    double wsum = 0;
    int splitsUsed = 0, splitsWithNet = 0;
    for (const EvalSplit& sp : t.splits) {
        const EvalRow* bil = rowNamed(sp, "bilinear");
        const EvalRow* net = rowNamed(sp, "BLSS");
        const EvalRow* orc = rowNamed(sp, "oracle");
        const EvalRow* nat = rowNamed(sp, "native");
        // The BASELINE and the CEILING or nothing: a summary built from a split
        // that is missing either would be comparing rows from different frame
        // sets. The BLSS row is optional, because a net-free `--blss-eval` does
        // not produce one and the ceiling is still the answer to "will this
        // scene benefit" - see EvalSummary::haveNet.
        if (!bil || !orc) continue;
        // A caption whose frame count did not parse still weighs something -
        // one - rather than dropping the split out of the mean silently.
        const double w = sp.frames > 0 ? (double)sp.frames : 1.0;
        s.bilinear += w * bil->psnr;
        s.oracle += w * orc->psnr;
        s.oraclePasses += w * orc->passes;
        if (nat) s.native += w * nat->psnr;
        if (net) {
            s.net += w * net->psnr;
            s.netPasses += w * net->passes;
            ++splitsWithNet;
        }
        s.frames += sp.frames;
        wsum += w;
        ++splitsUsed;
    }
    if (wsum <= 0.0) return s;
    s.bilinear /= wsum;
    s.oracle /= wsum;
    s.native /= wsum;
    s.oraclePasses /= wsum;
    s.oracleMargin = s.oracle - s.bilinear;
    // All splits or none: a mean over the held-out rows plus a net figure taken
    // from only one of them would be two different frame sets in one sentence.
    s.haveNet = splitsWithNet == splitsUsed;
    if (s.haveNet) {
        s.net /= wsum;
        s.netPasses /= wsum;
        s.netMargin = s.net - s.bilinear;
    } else {
        s.net = s.netPasses = s.netMargin = 0.0;
    }
    s.ok = true;
    return s;
}

EvalSummary parseVerdictLine(const std::string& text) {
    EvalSummary s;
    // The LAST such line wins - a run prints one, but a log the window has been
    // appending to across two runs must answer about the newer one.
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);
        if (line.rfind("[blss] verdict", 0) != 0) continue;
        EvalSummary v;
        bool any = false;
        for (const std::string& tok : tokenize(line)) {
            const size_t eq = tok.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = tok.substr(0, eq);
            double val = 0;
            if (!asNumber(tok.substr(eq + 1), val)) continue;
            if (key == "headroom") { v.oracleMargin = val; any = true; }
            else if (key == "passes") v.oraclePasses = val;
            else if (key == "bilinear") v.bilinear = val;
            else if (key == "oracle") v.oracle = val;
            else if (key == "native") v.native = val;
        }
        // `headroom` is the one field the verdict cannot be built without.
        if (!any) continue;
        v.ok = true;
        v.haveNet = false;
        s = v;
    }
    return s;
}

// -------------------------------------------------------------- parse: cv ---

CvTable parseCv(const std::string& text) {
    CvTable out;
    enum class Sec { None, Margin, Absolute, Deadzone } sec = Sec::None;
    bool caption = false;
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);

        if (line.rfind("blss: leave-one-shot-out", 0) == 0) {
            out.caption = line.substr(6);
            caption = true;
            continue;
        }
        if (caption) {
            // The header printf continues on lines indented by six spaces.
            if (raw.rfind("      ", 0) == 0 && !line.empty()) {
                out.caption += "\n" + line;
                continue;
            }
            caption = false;
        }
        if (line.empty()) continue;

        if (line.find("on the HELD-OUT shot, dB") != std::string::npos) {
            sec = Sec::Margin;
            continue;
        }
        if (line.rfind("per fold, mean over", 0) == 0) {
            sec = Sec::Absolute;
            continue;
        }
        if (line.rfind("INFERENCE DEADZONE sweep", 0) == 0) {
            sec = Sec::Deadzone;
            continue;
        }

        std::vector<std::string> tok = tokenize(line);
        if (tok.empty()) continue;

        if (sec == Sec::Margin && tok[0] == "held-out") {
            out.seeds.clear();
            for (size_t i = 0; i + 1 < tok.size(); ++i)
                if (tok[i] == "seed") out.seeds.push_back(tok[i + 1]);
            continue;
        }

        // The summary line, wherever it lands.
        if (tok[0] == "overall" && line.find("fold-run") != std::string::npos) {
            out.mean = numAfter(tok, "overall");
            out.sd = numAfter(tok, "sd");
            out.runs = (int)numAfter(tok, "over");
            out.below = (int)numBefore(tok, "of");
            out.passes = numBefore(tok, "passes");
            out.passesSd = numAfter(tok, "sd", out.passesSd);
            // "(sd 0.61)" is the second "sd" on the line - take the last one.
            for (size_t i = 0; i + 1 < tok.size(); ++i)
                if (unpunct(tok[i]) == "sd") {
                    double v = 0;
                    if (asNumber(unpunct(tok[i + 1]), v)) out.passesSd = v;
                }
            out.sd = numAfter(tok, "sd");
            continue;
        }
        if (line.find("per-seed fold-mean") != std::string::npos) {
            out.seedSd = numAfter(tok, "fold-mean");
            continue;
        }

        if (sec == Sec::Margin && tok[0] == "mean" && tok.size() > 2 && tok[1] == "over") {
            for (size_t i = 3; i < tok.size(); ++i) {
                double v = 0;
                if (asNumber(tok[i], v)) out.seedMeans.push_back(v);
            }
            // The row ends with the overall mean and its sd; the seed means are
            // whatever comes before them.
            if (out.seedMeans.size() >= 2) out.seedMeans.resize(out.seedMeans.size() - 2);
            continue;
        }

        if (sec == Sec::Deadzone) {
            double a = 0;
            if (tok.size() >= 7 && asNumber(tok[0], a)) {
                CvDeadzone d;
                d.alpha = a;
                asNumber(tok[1], d.margin);
                asNumber(tok[2], d.passes);
                asPercent(tok[3], d.point);
                asPercent(tok[4], d.temporal);
                asPercent(tok[5], d.sharpen);
                const size_t slash = tok[6].find('/');
                if (slash != std::string::npos) {
                    d.below = std::atoi(tok[6].substr(0, slash).c_str());
                    d.runs = std::atoi(tok[6].substr(slash + 1).c_str());
                }
                d.thisBuild = line.find("this build") != std::string::npos;
                out.deadzone.push_back(d);
            }
            continue;
        }

        // A fold row: "<index> <shot name...> <numbers...>".
        double idx = 0;
        if (!asNumber(tok[0], idx) || idx != std::floor(idx) || idx < 0) continue;
        size_t j = 1;
        double v = 0;
        while (j < tok.size() && !asNumber(tok[j], v)) ++j;
        if (j == 1 || j >= tok.size()) continue;
        std::vector<double> nums;
        for (size_t i = j; i < tok.size(); ++i) {
            double n = 0;
            if (asNumber(tok[i], n)) nums.push_back(n);
        }
        const int fi = (int)idx;
        if ((int)out.folds.size() <= fi) out.folds.resize((size_t)fi + 1);
        CvFold& f = out.folds[(size_t)fi];
        f.index = fi;
        if (f.shot.empty()) f.shot = join(tok, 1, j);
        if (sec == Sec::Absolute) {
            if (nums.size() >= 7) {
                f.have = true;
                f.native = nums[0];
                f.bilinear = nums[1];
                f.blss = nums[2];
                f.oracle = nums[3];
                f.passes = nums[4];
                f.flicker = nums[5];
                f.inDist = nums[6];
            }
        } else if (sec == Sec::Margin) {
            if (nums.size() >= 3) {
                f.sd = nums.back();
                f.mean = nums[nums.size() - 2];
                f.perSeed.assign(nums.begin(), nums.end() - 2);
            }
        }
    }
    // Folds that were resized into existence but never filled would draw as a
    // row of zeros, which is a number the tool did not produce.
    out.folds.erase(std::remove_if(out.folds.begin(), out.folds.end(),
                                   [](const CvFold& f) { return f.shot.empty(); }),
                    out.folds.end());
    return out;
}

// -------------------------------------------------------- parse: features ---

FeatureTable parseFeatures(const std::string& text) {
    FeatureTable out;
    enum class Sec { None, Channels, PerShot } sec = Sec::None;
    const auto isFeature = [](const std::string& n) {
        for (int c = 0; c < blss::kFeatures; ++c)
            if (n == blss::kFeatureNames[c]) return true;
        return false;
    };
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);
        if (line.empty()) continue;
        if (line.rfind("input channels over", 0) == 0) {
            out.caption = line;
            sec = Sec::Channels;
            continue;
        }
        if (line.rfind("per-shot mean of each channel", 0) == 0) {
            sec = Sec::PerShot;
            continue;
        }
        const std::vector<std::string> tok = tokenize(line);
        if (tok.empty()) continue;
        if (sec == Sec::PerShot && tok[0] == "feature") {
            // FIXED WIDTH, not tokens: the header is `"  %-11s"` then `" %8s"`
            // per shot then `"   spread"`, and a shot name truncated to eight
            // characters routinely CONTAINS A SPACE ("poles pa", "flat slo",
            // "whip whi"). Splitting it on whitespace invented three extra
            // columns and misaligned every row after them.
            out.shots.clear();
            for (size_t j = 0; 13 + j * 9 + 9 <= raw.size(); ++j) {
                const std::string slot = trimmed(raw.substr(13 + j * 9, 9));
                if (slot == "spread") break;
                out.shots.push_back(slot);
            }
            if (out.shots.empty())
                for (size_t i = 1; i + 1 < tok.size(); ++i) out.shots.push_back(tok[i]);
            continue;
        }
        if (!isFeature(tok[0])) continue;

        if (sec == Sec::Channels) {
            if (tok.size() < 10) continue;
            FeatureRow r;
            r.name = tok[0];
            if (!asNumber(tok[1], r.mean) || !asNumber(tok[2], r.sd) ||
                !asNumber(tok[3], r.min) || !asNumber(tok[4], r.max) ||
                !asPercent(tok[5], r.at0) || !asPercent(tok[6], r.at1))
                continue;
            asNumber(tok[7], r.rPoint);
            asNumber(tok[8], r.rTemporal);
            asNumber(tok[9], r.rSharpen);
            out.rows.push_back(r);
        } else if (sec == Sec::PerShot) {
            FeatureShotRow r;
            r.name = tok[0];
            std::vector<double> nums;
            for (size_t i = 1; i < tok.size(); ++i) {
                double v = 0;
                if (asNumber(tok[i], v)) nums.push_back(v);
            }
            if (nums.size() < 2) continue;
            r.spread = nums.back();
            r.means.assign(nums.begin(), nums.end() - 1);
            out.perShot.push_back(r);
        }
    }
    // The rows are the authority on how many shots there are - they are pure
    // numbers. If the header could not be recovered into the same count, name
    // the columns by index rather than drawing a table whose headings have
    // slipped against its cells.
    if (!out.perShot.empty() && out.shots.size() != out.perShot.front().means.size()) {
        out.shots.clear();
        for (size_t i = 0; i < out.perShot.front().means.size(); ++i)
            out.shots.push_back("shot" + std::to_string(i));
    }
    return out;
}

// ----------------------------------------------------------- parse: probe ---

namespace {
bool isFeatureName(const std::string& n) {
    for (int c = 0; c < blss::kFeatures; ++c)
        if (n == blss::kFeatureNames[c]) return true;
    return false;
}

// "0.000/0.412/1.000" and "0.000..1.000" - the two composite cells of the probe
// table. Split rather than tokenized, because the tool prints them as ONE
// whitespace-free field on purpose (a slash-separated triple is what fits the
// column) and splitting on the separator is the only reading that stays right
// when a value is negative or four digits wide.
bool split3(const std::string& s, char sep, double& a, double& b, double& c) {
    const size_t i = s.find(sep);
    if (i == std::string::npos) return false;
    const size_t j = s.find(sep, i + 1);
    if (j == std::string::npos) return false;
    return asNumber(s.substr(0, i), a) && asNumber(s.substr(i + 1, j - i - 1), b) &&
           asNumber(s.substr(j + 1), c);
}
bool split2(const std::string& s, const char* sep, double& a, double& b) {
    const size_t i = s.find(sep);
    if (i == std::string::npos) return false;
    return asNumber(s.substr(0, i), a) && asNumber(s.substr(i + std::strlen(sep)), b);
}
}  // namespace

ProbeTable parseProbe(const std::string& text) {
    ProbeTable out;
    bool inSection = false;
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);
        if (line.empty()) continue;
        // The tool's own banner. Anchoring on it rather than on "a line whose
        // first token is a channel name" is what keeps the channel table above
        // (which starts identically) out of this one.
        if (line.rfind("A MEASURED CONSOLE VECTOR", 0) == 0) {
            inSection = true;
            out.rows.clear();
            continue;
        }
        if (!inSection) continue;
        const std::vector<std::string> tok = tokenize(line);
        if (tok.empty() || !isFeatureName(tok[0])) continue;
        ProbeRow r;
        r.name = tok[0];
        if (tok.size() < 8 || isDash(tok[1])) {
            r.given = false;
            r.verdict = "not in the probe";
            ++out.missing;
            out.rows.push_back(r);
            continue;
        }
        if (!split3(tok[1], '/', r.lo, r.mid, r.hi)) continue;
        if (!asNumber(tok[2], r.spread)) continue;
        if (!split2(tok[3], "..", r.corpusLo, r.corpusHi)) continue;
        if (!asPercent(tok[4], r.pct) || !asPercent(tok[5], r.supp) ||
            !asPercent(tok[6], r.band))
            continue;
        r.given = true;
        r.verdict = join(tok, 7, tok.size());
        // THE TOOL OWNS THE THRESHOLDS. A second copy of "under 1% support is
        // extrapolation" here would be a second answer to the question this
        // whole table exists to settle, and the two would drift the first time
        // either moved - so the flags are read off the words it printed.
        r.outOfRange = r.verdict.find("OUT OF RANGE") != std::string::npos;
        r.noSupport = r.verdict.find("no support") != std::string::npos;
        r.constant = r.verdict.find("CONSTANT") != std::string::npos;
        out.outOfRange += r.outOfRange ? 1 : 0;
        out.tails += r.noSupport ? 1 : 0;
        out.constants += r.constant ? 1 : 0;
        out.rows.push_back(r);
    }
    return out;
}

ProbeVerdict probeVerdict(const ProbeTable& t) {
    ProbeVerdict v;
    if (!t.ok()) {
        v.headline = "No console vector has been placed yet.";
        v.why = "Run the game with debug view 2 on, then read its log back here.";
        return v;
    }
    char b[512];
    if (t.outOfRange > 0) {
        v.level = ProbeVerdict::Level::Mismatch;
        std::snprintf(b, sizeof(b),
                      "THE CONSOLE IS FEEDING THIS NETWORK INPUTS IT NEVER SAW: %d channel(s) "
                      "outside the corpus' range.",
                      t.outOfRange);
        v.headline = b;
        v.why = "A value outside the range the corpus covered is not interpolation, it is "
                "extrapolation from a 12-unit hidden layer - the network's answer there is "
                "whatever its weights happen to extend to. This is the exact shape of the "
                "-0.40 dB result: fitted on one distribution, run on another. Add shots that "
                "cover the content the console is drawing, and re-train.";
        return v;
    }
    if (t.tails > 0 || t.constants > 0 || t.missing > 0) {
        v.level = ProbeVerdict::Level::Thin;
        // Only the categories that actually fired. "0 channel(s) with under 1%
        // of the corpus behind them" is a sentence that makes a reader hunt for
        // a problem that is not there, in a verdict whose whole job is to say
        // which problem IS there.
        std::string parts;
        const auto add = [&](int n, const char* what) {
            if (n <= 0) return;
            char one[160];
            std::snprintf(one, sizeof(one), "%d channel(s) %s", n, what);
            parts += (parts.empty() ? "" : ", ");
            parts += one;
        };
        add(t.tails, "with under 1% of the corpus behind them");
        add(t.constants, "constant across the whole frame");
        add(t.missing, "absent from the line");
        std::snprintf(b, sizeof(b), "IN RANGE, BUT THINLY TAUGHT: %s.", parts.c_str());
        v.headline = b;
        v.why.clear();
        if (t.tails > 0)
            v.why += "1% of the tiles is 1% of the gradient, so a value with no support was "
                     "barely taught even though it is representable. ";
        if (t.constants > 0)
            v.why += "A channel that is CONSTANT across the console's whole frame is a "
                     "different problem: the network is making no per-tile decision from it at "
                     "all there, whatever the corpus taught. ";
        if (t.missing > 0)
            v.why += "A channel absent from the line was not measured - check the BLSSFEAT line "
                     "is complete, or that the build is not older than the channel. ";
        return v;
    }
    v.level = ProbeVerdict::Level::Matches;
    v.headline = "Every channel the console produced is inside what this corpus taught.";
    v.why = "That is the strongest statement this instrument can make, and it is about ONE "
            "frame - the one the game last logged. Probe the parts of the game that look "
            "different from each other, not just the first screen.";
    return v;
}

std::string lastFeatLine(const std::string& logText) {
    std::string best;
    for (const std::string& raw : splitLines(logText)) {
        if (raw.find("BLSSFEAT") == std::string::npos) continue;
        const std::string line = trimmed(raw);
        if (!line.empty()) best = line;
    }
    return best;
}

// --------------------------------------------------- is the corpus any good ---

const char* channelPurpose(const std::string& name) {
    if (name == "motion")
        return "how far a tile moved since the last frame - whether the history is usable at "
               "all";
    if (name == "depth") return "how near the tile's surface is";
    if (name == "depthGrad")
        return "how sharply depth changes across the tile - where silhouettes are";
    if (name == "edgeDens")
        return "how much geometric edge runs through the tile - where the aliasing is";
    if (name == "texDetail")
        return "how hard the texture is being minified - the channel that predicts TEXTURE "
               "aliasing";
    if (name == "coverage") return "how much of the tile any geometry covered at all";
    return "";
}

namespace {
// A channel that does not MOVE is a channel the network's weights cannot use,
// whatever else is true of it. 0.02 is the same number the Inputs tab has
// always coloured its sd column at.
constexpr double kFlatSd = 0.02;
// ...and one whose sd is essentially zero is not merely flat, it is a constant:
// the net has one fewer input than the topology says.
constexpr double kDeadSd = 0.005;
// Against its clamp on half the corpus. Measured precedent: the shot the
// bestiary net has always lost on is the one where depth spends the whole shot
// pinned at 1.0, and 58.6% of ALL bestiary tiles read it there.
constexpr double kPinnedPct = 50.0;
// NO CHANNEL PREDICTS ANYTHING. The correlations are importance-weighted
// against the oracle's own answer, so when every one of them is inside the
// noise the oracle is asking for the SAME weights everywhere - which means
// there is nothing per-tile to learn, whatever the channels look like.
// Measured: examples/showcase peaks at 0.021 over all 18 (channel, output)
// pairs, examples/upscaler-lab at 0.253.
constexpr double kDeadCorr = 0.05;
}  // namespace

CorpusHealth corpusHealth(const FeatureTable& t) {
    CorpusHealth h;
    if (!t.ok()) {
        // THE REFUSAL. An unmeasured corpus must never render as a healthy one,
        // which is the same standard the speed verdict's "TOO CLOSE TO CALL"
        // holds itself to.
        h.headline = "Not measured - so this cannot tell you whether the corpus is any good.";
        h.why = "Report the input channels and it will say, in one line, whether a network "
                "fitted to these frames can learn anything.";
        return h;
    }
    h.ok = true;
    h.shots = (int)t.shots.size();
    double peakCorr = 0.0;
    for (const FeatureRow& r : t.rows) {
        const std::string why = channelPurpose(r.name);
        peakCorr = std::max({peakCorr, std::fabs(r.rPoint), std::fabs(r.rTemporal),
                             std::fabs(r.rSharpen)});
        CorpusFinding f;
        f.channel = r.name;
        if (r.sd < kDeadSd && r.at0 >= 99.0) {
            f.level = CorpusFinding::Level::Fatal;
            f.what = r.name + " is 0 on every tile of this corpus (" + why +
                     "). The network cannot learn a channel that never varies - it has one "
                     "fewer input than its topology says.";
            f.fix = r.name == "texDetail"
                        ? "Your corpus has no textured surfaces. Put textured geometry in the "
                          "shots, or expect this net to generalise badly to anything textured."
                        : "Add shots over content where this varies, or accept that the net is "
                          "deciding from five inputs.";
            ++h.dead;
        } else if (r.sd < kDeadSd && r.at1 >= 99.0) {
            f.level = CorpusFinding::Level::Fatal;
            f.what = r.name + " is pinned at its clamp on every tile (" + why +
                     "), so it is a constant and the network cannot use it.";
            f.fix = "Add shots where this is not saturated - for depth that means content "
                    "further away than a few units, for coverage it means frames with sky or "
                    "empty tiles in them.";
            ++h.dead;
        } else if (r.at1 >= kPinnedPct) {
            f.level = CorpusFinding::Level::Warn;
            char b[64];
            std::snprintf(b, sizeof(b), "%.0f%%", r.at1);
            f.what = r.name + " sits against its clamp on " + b + " of the corpus' tiles (" +
                     why + "), so on most of the corpus it carries no information.";
            f.fix = "A saturated feature is a feature the network does not have. Aim some shots "
                    "at content that does not saturate it.";
            ++h.pinned;
        } else if (r.sd < kFlatSd) {
            f.level = CorpusFinding::Level::Warn;
            char b[64];
            std::snprintf(b, sizeof(b), "%.3f", r.sd);
            f.what = r.name + " barely moves across the whole corpus (sd " + b + "; " + why +
                     ").";
            f.fix = "Add shots whose content differs in this respect, or the net will decide "
                    "as if the channel were a bias.";
            ++h.flat;
        } else {
            continue;
        }
        h.findings.push_back(std::move(f));
    }
    // A channel that is the same number in every camera move cannot tell the
    // shots apart, which is the one thing it exists to do.
    for (const FeatureShotRow& r : t.perShot) {
        if (r.spread >= kFlatSd || r.means.size() < 2) continue;
        bool alreadyNamed = false;
        for (const CorpusFinding& f : h.findings) alreadyNamed |= (f.channel == r.name);
        if (alreadyNamed) continue;
        CorpusFinding f;
        f.level = CorpusFinding::Level::Note;
        f.channel = r.name;
        char b[64];
        std::snprintf(b, sizeof(b), "%.3f", r.spread);
        f.what = r.name + " reads almost the same in every camera move (spread " + b +
                 "), so it cannot tell your shots apart.";
        f.fix = "Not fatal - it still varies WITHIN a shot - but a shot that differs in this "
                "channel would teach the net more than another angle on the same content.";
        h.findings.push_back(std::move(f));
        ++h.blind;
    }
    if (h.shots > 0 && h.shots < 4) {
        CorpusFinding f;
        f.level = CorpusFinding::Level::Warn;
        f.what = "Only " + std::to_string(h.shots) +
                 " camera move(s) in the whole corpus. Neighbouring frames of one move are near "
                 "duplicates, so this is a much smaller sample than the frame count suggests.";
        f.fix = "Add training shots, or turn more of the six automatic moves back on.";
        h.findings.push_back(std::move(f));
        ++h.pinned;
    }

    char b[512];
    // ORDER MATTERS: "nothing correlates" outranks every per-channel finding,
    // because a corpus whose oracle asks for the same answer everywhere has
    // nothing to teach no matter how healthy its inputs look.
    if (peakCorr < kDeadCorr) {
        h.verdict = CorpusHealth::Verdict::Unusable;
        h.headline = "THERE IS NOTHING HERE TO LEARN. Do not fit a network to this corpus.";
        std::snprintf(b, sizeof(b),
                      "No input channel correlates with what the oracle asked for above "
                      "%.3f (the strongest of all %d channel-output pairs). The oracle wants "
                      "essentially the SAME weights in every tile of every frame, so a "
                      "per-tile network has no decision to make - it can only add fill. Check "
                      "the headroom verdict above: a corpus that looks like this normally "
                      "belongs to a scene whose oracle ceiling is near zero.",
                      peakCorr, (int)t.rows.size() * 3);
        h.why = b;
        return h;
    }
    if (h.dead > 0) {
        h.verdict = CorpusHealth::Verdict::Unusable;
        std::snprintf(b, sizeof(b),
                      "DO NOT SHIP A NET FITTED TO THIS CORPUS: %d of the %d input channels are "
                      "constant.",
                      h.dead, (int)t.rows.size());
        h.headline = b;
        h.why = "A constant channel is a channel the network does not have, and the console "
                "will feed it a value the corpus never contained. That is exactly how a net "
                "measured -0.40 dB - worse than leaving the upscaler off - on a real project.";
        return h;
    }
    if (h.pinned > 0 || h.flat > 0) {
        h.verdict = CorpusHealth::Verdict::Thin;
        std::snprintf(b, sizeof(b),
                      "TRAINABLE, BUT THIN: %d channel(s) saturated and %d that barely move.",
                      h.pinned, h.flat);
        h.headline = b;
        h.why = "The net will fit, and it will decide from fewer inputs than it has. Probe a "
                "real console frame against this corpus before shipping it - a channel that is "
                "pinned here and not pinned on the console is the mismatch that costs decibels.";
        return h;
    }
    h.verdict = CorpusHealth::Verdict::Good;
    std::snprintf(b, sizeof(b), "Every input channel varies and none is saturated, over %d "
                                "camera move(s).",
                  h.shots);
    h.headline = b;
    h.why = "That is a statement about the CORPUS, not about the scene: it says the network "
            "has something to learn from, not that there is headroom to win. The headroom "
            "verdict above answers the second question.";
    return h;
}

// ------------------------------------------ did the plan reach the tool ---

std::vector<CorpusScene> parseCorpusScenes(const std::string& text) {
    std::vector<CorpusScene> out;
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);
        // "[blss]   scene 'vale': 50 mesh(es) + 5 animated part(s), 7724 triangle(s), 6 shot(s)"
        if (line.rfind("[blss]", 0) != 0) continue;
        const size_t at = line.find("scene '");
        if (at == std::string::npos) continue;
        const size_t close = line.find('\'', at + 7);
        if (close == std::string::npos) continue;
        CorpusScene s;
        s.name = line.substr(at + 7, close - at - 7);
        const std::vector<std::string> tok = tokenize(line.substr(close));
        for (size_t i = 1; i < tok.size(); ++i) {
            double v = 0;
            if (tok[i].rfind("shot(s)", 0) == 0 && asNumber(tok[i - 1], v)) s.shots = (int)v;
            if (tok[i].rfind("triangle(s)", 0) == 0 && asNumber(tok[i - 1], v))
                s.triangles = (size_t)v;
        }
        if (s.shots > 0) out.push_back(std::move(s));
    }
    return out;
}

// -------------------------------------------------------------- will it be faster ---

SpeedEstimate speedFrom(double coverages) {
    SpeedEstimate e;
    if (!(coverages >= 0.0)) return e;  // NaN-safe
    e.ok = true;
    e.coverages = coverages;
    e.breakEven = fill::breakEven();
    e.fillMs = coverages * fill::kPassMs;
    e.savedMs = fill::kSavedFraction * e.fillMs - (fill::kEeCostMs + fill::kCompositeGsMs);
    if (e.savedMs <= 0.0) {
        e.band = SpeedEstimate::Band::Loss;
        return e;  // lo/hi stay 1.0: a slowdown is stated in ms, not as a ratio
    }
    e.band = coverages < e.breakEven * 1.5 ? SpeedEstimate::Band::Marginal
                                           : SpeedEstimate::Band::Win;
    // The GS-bound limit: the whole frame is fill, so the whole frame shrinks
    // to what survives plus what BLSS costs. Nothing can beat this.
    const double onAllFill = fill::kKeptFraction * e.fillMs + fill::kEeCostMs + fill::kCompositeGsMs;
    e.hi = onAllFill > 1e-6 ? e.fillMs / onAllFill : 1.0;
    // ...and the other end: fill is 60 % of the frame, the rest is EE work BLSS
    // does not touch. That fraction is an assumption and is named wherever this
    // range is printed; it is worth knowing that on the one scene where both
    // arms were measured (upscaler-lab, 1.60x at ~75 coverages) the outcome
    // landed exactly on this end of the range.
    const double totalOff = e.fillMs / 0.60;
    const double totalOn = totalOff - e.savedMs;
    e.lo = totalOn > 1e-6 ? totalOff / totalOn : e.hi;
    if (e.lo > e.hi) std::swap(e.lo, e.hi);
    return e;
}

// ------------------------------------------------------------- one answer ---

namespace {
std::string msText(double ms) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.1f ms", ms < 0 ? -ms : ms);
    return b;
}
std::string rangeText(const SpeedEstimate& s) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.1f-%.1fx", s.lo, s.hi);
    return b;
}
}  // namespace

Recommendation recommend(const EvalSummary& quality, bool haveQuality, const SpeedEstimate& speed,
                         bool haveSpeed) {
    Recommendation r;
    const bool q = haveQuality && quality.ok;
    const bool s = haveSpeed && speed.ok;
    if (!q && !s) {
        r.headline = "Nothing has been measured yet.";
        r.why = "The two buttons above answer the two halves: whether the picture has anything "
                "to gain, and whether the frame will get shorter.";
        return r;
    }
    const bool headroom = q && quality.oracleMargin >= kNoHeadroomDb;
    // THREE SPEED STATES, NOT TWO, and the middle one is the reason this
    // function exists rather than an `if (savedMs > 0)` in the draw call. A
    // scene estimated at 15 coverages against a ~13 break-even saves about
    // 1 ms - which is smaller than the things the count admits it cannot see,
    // so "TURN IT ON FOR THE SPEED" there is a confident wrong answer, and a
    // confident wrong answer is worse than a range. examples/showcase is
    // exactly that scene and is what caught it.
    const bool win = s && speed.band == SpeedEstimate::Band::Win;
    const bool loss = s && speed.band == SpeedEstimate::Band::Loss;
    const bool close = s && speed.band == SpeedEstimate::Band::Marginal;

    if (q && s) {
        if (!headroom && loss) {
            r.verdict = Recommendation::Verdict::Off;
            r.headline = "LEAVE IT OFF. There is no picture to gain and no time to gain.";
        } else if (!headroom && close) {
            r.verdict = Recommendation::Verdict::Mixed;
            r.headline = "TOO CLOSE TO CALL, and there is no picture to gain either.";
        } else if (!headroom && win) {
            r.verdict = Recommendation::Verdict::SpeedOnly;
            r.headline = "TURN IT ON FOR THE SPEED, not for the picture.";
        } else if (headroom && loss) {
            r.verdict = Recommendation::Verdict::QualityOnly;
            r.headline = "ONLY IF YOU CAN AFFORD " + msText(speed.savedMs) +
                         " A FRAME - the picture improves, the frame gets longer.";
        } else if (headroom && close) {
            r.verdict = Recommendation::Verdict::Mixed;
            r.headline = "WORTH IT FOR THE PICTURE; the speed is too close to call.";
        } else {
            r.verdict = Recommendation::Verdict::On;
            r.headline = "TURN IT ON. The picture has room and the frame gets shorter.";
        }
    } else if (s) {
        r.verdict = win ? Recommendation::Verdict::SpeedOnly : Recommendation::Verdict::Mixed;
        r.headline = win    ? "The frame should get shorter - the picture half is unmeasured."
                     : close ? "The speed is too close to call - and the picture half is "
                               "unmeasured."
                             : "The frame will get LONGER - and the picture half is unmeasured.";
    } else {
        r.verdict = Recommendation::Verdict::Mixed;
        r.headline = headroom
                         ? "The picture has room - whether the frame gets shorter is unmeasured."
                         : "The picture has nothing to gain - the speed half is unmeasured.";
    }

    // The two facts, always in the same order and always with their units, so
    // the sentence above can be checked against them.
    std::string why;
    if (q) {
        char b[192];
        std::snprintf(b, sizeof(b), "Picture: the oracle ceiling is %+.2f dB at %.2f passes%s. ",
                      quality.oracleMargin, quality.oraclePasses,
                      headroom ? "" : " - indistinguishable from plain bilinear");
        why += b;
    }
    if (s) {
        char b[320];
        if (win)
            std::snprintf(b, sizeof(b),
                          "Speed: about %.0f full-screen coverages against a ~%.0f break-even, so "
                          "roughly %s off the GS - %s if the frame is mostly fill.",
                          speed.coverages, speed.breakEven, msText(speed.savedMs).c_str(),
                          rangeText(speed).c_str());
        else if (close)
            std::snprintf(b, sizeof(b),
                          "Speed: about %.0f full-screen coverages against a ~%.0f break-even - "
                          "only %s a frame either way, which is inside what this count admits it "
                          "cannot see. It is a FLOOR, so the console is likelier to land above "
                          "the line than below it.",
                          speed.coverages, speed.breakEven, msText(speed.savedMs).c_str());
        else
            std::snprintf(b, sizeof(b),
                          "Speed: about %.0f full-screen coverages, below the ~%.0f break-even, so "
                          "the frame gets about %s LONGER.",
                          speed.coverages, speed.breakEven, msText(speed.savedMs).c_str());
        why += b;
    }
    r.why = why;
    return r;
}

// ------------------------------------------------------------------ errors ---

std::vector<std::string> parseErrors(const std::string& text) {
    static const char* const kNoise[] = {
        "blss: rendering", "blss: labelling",   "blss: training on",
        "blss: final loss", "blss: seed 0x",     "blss: leave-one-shot-out",
        "blss: holding at zero", "blss: wrote",
    };
    std::vector<std::string> out;
    for (const std::string& raw : splitLines(text)) {
        const std::string line = trimmed(raw);
        const bool ours = line.rfind("blss: ", 0) == 0 || line.rfind("[editor] ", 0) == 0;
        if (!ours) continue;
        bool noise = false;
        for (const char* n : kNoise) noise = noise || line.rfind(n, 0) == 0;
        if (noise) continue;
        out.push_back(line);
        if (out.size() >= 12) break;
    }
    return out;
}

}  // namespace blssui

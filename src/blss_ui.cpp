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
        out.status = "cross-validating: " + t.substr(6) +
                     " (the fold loop prints nothing until it is done)";
        return true;
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

// -------------------------------------------------------------------------
// The devkit windows and their host-side ticks (docs/devkit.md): Live Link,
// Live Logic, the Live Debugger, the time machine and the game-error modal.
//
// Split out of app.cpp so the editor builds in parallel: it was one 26k-line
// translation unit and therefore the whole build's critical path. These are
// still App:: members declared in app.hpp - the assetbrowser.cpp precedent.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "aisupport.hpp"
#include "animedit.hpp"
#include "decalproj.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "gl_loader.h"
#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "uvunwrap.hpp"
#include "stochtile.hpp"
#include "templates.hpp"
#include "wavconvert.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#include "platform.hpp"

// ---------------------------------------------------------------------------
// Game error catcher. A failed TYRA_ASSERT / TYRA_TRAP in the running game
// (e.g. a missing texture) no longer takes over the console screen - the engine
// prints the dump to the log and halts (see vendor/tyra debug.hpp). The editor
// tails that log and raises a copyable dialog so the error is not silent.
// ---------------------------------------------------------------------------

// The stable delimiters TyraDebug::trap() prints around an assertion dump (see
// vendor/tyra/engine/inc/debug/debug.hpp). Matched as substrings so a leading
// log prefix (the Debug window's nothing, or the runner's "[ps2] "/timestamp)
// does not defeat detection.
// The engine's delimited error block. TyraX-built games print TYRAX; the old
// TYRA banner is still accepted so an ELF built before the rename still reports.
static const char kTyraAssertBanner[] = "==============  TYRAX  =============";
static const char kTyraAssertBannerLegacy[] = "==============  TYRA  ==============";
static const char kTyraAssertClose[] = "====================================";

// Returns the last complete TYRA assertion block in `text` (from its banner
// line through the closing rule, inclusive), or "" when there is none / it is
// still being written.
static std::string extractLastTyraAssert(const std::string& text) {
    size_t banner = text.rfind(kTyraAssertBanner);
    if (banner == std::string::npos) banner = text.rfind(kTyraAssertBannerLegacy);
    if (banner == std::string::npos) return "";
    // Back up to the start of the banner's line so a log prefix is dropped and
    // the dump reads cleanly in the dialog.
    const size_t nl = text.rfind('\n', banner);
    const size_t lineStart = (nl == std::string::npos) ? 0 : nl + 1;
    const size_t close = text.find(kTyraAssertClose, banner);
    if (close == std::string::npos) return "";
    const size_t lineEnd = text.find('\n', close);
    return text.substr(lineStart,
                       (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);
}

std::string App::latestGameAssert() const {
    // PCSX2 runs: the game writes TYRA output to bin/log.txt on the host fs.
    std::string text;
    if (hasProject_) {
        const std::string gameLog =
            (std::filesystem::path(project_.dir) / "bin" / "log.txt").string();
        text = readTextFileTail(gameLog, 1u << 20);  // last 1 MB
    }
    // "Run on PS2" logs to the console instead (writeLogsToFile is off under
    // ps2link); that stream arrives in the runner log as "[ps2] ..." lines.
    // Append its tail so networked asserts are caught too - a PCSX2 run's
    // runner log carries no assertion banner, so this stays harmless there.
    const std::string rlog = runner_.log();
    const size_t kRunnerTail = 256u * 1024u;
    text += '\n';
    text += rlog.size() > kRunnerTail ? rlog.substr(rlog.size() - kRunnerTail) : rlog;
    return extractLastTyraAssert(text);
}

// Streams scene edits to the running (debug-profile) game - see the member
// block in app.hpp for the design. Self-throttled; the actual file write only
// happens when the live-representable state really changed.
void App::liveLinkTick() {
    namespace fs = std::filesystem;
    if (!hasProject_ || !project_.settings.liveLink ||
        project_.settings.buildProfile != "debug") {
        liveLinkState_ = LiveLinkState::Off;
        return;
    }

    const double now = ImGui::GetTime();
    if (now < liveLinkNextTick_) return;  // keep the last state between ticks
    liveLinkNextTick_ = now + 0.1;  // ~10 Hz matches the game's poll cadence

    // The as-built record the running build was made from. Re-read at most
    // every 1.5 s - a finished build (or Clean) must be picked up, but the
    // file is not worth hitting the disk for at tick rate.
    const fs::path binDir = fs::path(project_.dir) / "bin";
    if (now >= liveLinkSigNextRead_) {
        liveLinkSigNextRead_ = now + 1.5;
        liveLinkBuilt_ = LiveLinkBuilt();
        std::ifstream f(binDir / "livelink.sig");
        std::string line;
        if (f && std::getline(f, line) && line == "2") {
            LiveLinkBuilt b;
            bool ok = true;
            while (std::getline(f, line)) {
                if (line.rfind("ctx ", 0) == 0) {
                    b.ctxHash = std::strtoull(line.c_str() + 4, nullptr, 16);
                } else if (line.rfind("scene ", 0) == 0) {
                    b.scenes.emplace_back();
                } else if (line.size() >= 33 && !b.scenes.empty()) {
                    char* end = nullptr;
                    const uint64_t id = std::strtoull(line.c_str(), &end, 16);
                    const uint64_t recipe = std::strtoull(end, nullptr, 16);
                    b.scenes.back().emplace_back(id, recipe);
                } else {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                b.loaded = true;
                liveLinkBuilt_ = std::move(b);
            }
        }
    }
    if (!liveLinkBuilt_.loaded) {
        liveLinkState_ = LiveLinkState::NoBuild;
        return;
    }

    // Is the live project representable against the as-built record? Built
    // objects must keep their recipe (a live patch can't change it); new
    // objects need an equal-recipe as-built template to clone (and must be
    // spawnable at all); the cross-object context must be untouched.
    const int scene = project_.activeScene;
    if (liveLinkBuilt_.ctxHash != project::liveLinkContextHash(project_) ||
        scene >= (int)liveLinkBuilt_.scenes.size()) {
        liveLinkState_ = LiveLinkState::RebuildNeeded;
        return;
    }
    const auto& built = liveLinkBuilt_.scenes[scene];
    const SceneData& sc = project_.active();

    // Per-record resolution: templateIdx = -1 for as-built ids, else the
    // as-built index of an equal-recipe object to clone. The spawn pool holds
    // 32 clones - past that the session stops being representable.
    struct Rec {
        uint64_t id;
        int32_t tmpl;
        const SceneObject* o;
    };
    std::vector<Rec> recs;
    recs.reserve(sc.objects.size());
    int spawned = 0;
    for (const SceneObject& o : sc.objects) {
        const uint64_t id = project::liveLinkIdHash(o);
        const uint64_t recipe = project::liveLinkRecipeHash(o);
        int32_t tmpl = -1;
        bool foundId = false;
        for (size_t i = 0; i < built.size(); ++i) {
            if (built[i].first == id) {
                foundId = true;
                if (built[i].second != recipe) {
                    liveLinkState_ = LiveLinkState::RebuildNeeded;
                    return;
                }
                break;
            }
        }
        if (!foundId) {
            if (!project::liveLinkCanSpawnLive(o) ||
                ++spawned > 32 /* MAX_SPAWNED_OBJECTS */) {
                liveLinkState_ = LiveLinkState::RebuildNeeded;
                return;
            }
            tmpl = -1;
            for (size_t i = 0; i < built.size(); ++i)
                if (built[i].second == recipe) {
                    tmpl = (int32_t)i;
                    break;
                }
            if (tmpl < 0) {
                liveLinkState_ = LiveLinkState::RebuildNeeded;
                return;
            }
        }
        recs.push_back({id, tmpl, &o});
    }
    liveLinkState_ = LiveLinkState::Live;

    // Snapshot body: scene + count + one 64-byte record per object (id +
    // template + 12 floats). Deletions are implicit - the game hides whatever
    // is absent.
    std::vector<unsigned char> body;
    body.reserve(8 + recs.size() * 64);
    auto put = [&](const void* v, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(v);
        body.insert(body.end(), b, b + n);
    };
    const int32_t scene32 = scene;
    const int32_t count = (int32_t)recs.size();
    put(&scene32, 4);
    put(&count, 4);
    const uint32_t pad = 0;
    for (const Rec& r : recs) {
        put(&r.id, 8);
        put(&r.tmpl, 4);
        put(&pad, 4);
        put(r.o->position, 12);
        put(r.o->rotation, 12);
        put(r.o->scale, 12);
        put(r.o->color, 12);
    }
    if (body == liveLinkLastPayload_) return;  // nothing to stream

    // Full file: header (magic/version/seq + body's scene/count/reserved),
    // records, footer echoing seq - the game rejects torn writes. Written to
    // a sibling tmp and renamed so the game never sees a half file; if the
    // rename loses a race with the game's fread, retry on the next tick.
    const uint32_t seq = liveLinkSeq_ + 1;
    std::vector<unsigned char> file;
    file.reserve(24 + body.size() - 8 + 4);
    const uint32_t magic = 0x4C4C5854, version = 2, reserved = 0;
    const uint32_t footer = seq ^ 0x5A5A5A5AU;
    auto app32 = [&](const void* v) {
        const unsigned char* b = static_cast<const unsigned char*>(v);
        file.insert(file.end(), b, b + 4);
    };
    app32(&magic), app32(&version), app32(&seq);
    file.insert(file.end(), body.begin(), body.begin() + 8);  // scene, count
    app32(&reserved);
    file.insert(file.end(), body.begin() + 8, body.end());
    app32(&footer);

    const fs::path tmp = binDir / "livelink.tmp";
    const fs::path dst = binDir / "livelink.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(file.data()),
                  (std::streamsize)file.size());
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) return;  // game holds the file open right now - next tick retries

    liveLinkSeq_ = seq;
    liveLinkLastPayload_ = std::move(body);
}

// ---------------------------------------------------------------------------
// Live Debugger (docs/live-debugger.md). Everything here is reading files and
// bookkeeping: the game's snapshot in, a command file out, and the derived
// state the Debugger window + the Flow Graph overlay draw from.
// ---------------------------------------------------------------------------

// bin/crash.txt, written by the game's crash handler (docs/devkit.md). Parsed
// rather than just shown, so the addresses can be turned into names and the
// devkit's own history can be put next to them.
// bin/vucap.bin: one VU1 DMA chain the game handed over. Re-read only when the
// file changed, so an armed capture shows up by itself.
//
// "Changed" has to mean the timestamp, not the size: the game overwrites the
// file in place and a second capture of the same draw is the SAME LENGTH down
// to the byte (the chain is built from the same bag, and the VU1 memory tail is
// a fixed 16 KiB). Keying the cache on size alone made every capture after the
// first a no-op - the panel kept showing frame N forever while the game happily
// wrote frames N+1, N+2, ... over it.
void App::dbgReadVuCapture() {
    namespace fs = std::filesystem;
    if (!hasProject_) return;
    const fs::path path = fs::path(project_.dir) / "bin" / "vucap.bin";
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    const size_t size = ec ? 0 : (size_t)sz;
    std::error_code wec;
    const auto wt = fs::last_write_time(path, wec);
    const long long stamp =
        wec ? 0 : (long long)wt.time_since_epoch().count();
    if (size == dbgVuCapSize_ && stamp == dbgVuCapStamp_) return;
    if (!size) {
        dbgVuCapSize_ = size;
        dbgVuCapStamp_ = stamp;
        dbgVuCapTorn_ = 0;
        dbgVuCap_ = vucap::Capture();
        return;
    }
    // The game writes this file from inside a frame, in several fwrite()s, so a
    // poll can land on a half-written one. A v3 capture always ends with the
    // 16 KiB VU1 memory tail: no tail means we read it too early, so leave the
    // stamp alone and try again next poll rather than committing a torn decode.
    // A file that stays incomplete (a truncated write, not a race) is committed
    // after a few tries so the error is visible instead of silently ignored.
    vucap::Capture cap;
    const bool ok = vucap::load(path.string(), cap);
    if (ok && !cap.hasVuMem && ++dbgVuCapTorn_ < 4) return;
    dbgVuCapSize_ = size;
    dbgVuCapStamp_ = stamp;
    dbgVuCapTorn_ = 0;
    dbgVuCap_ = std::move(cap);
    dbgVuCapWaiting_ = false;
    if (dbgVuCap_.loaded)
        statusMessage_ = "VU capture: frame " + std::to_string(dbgVuCap_.frame) +
                         ", " + std::to_string(dbgVuCap_.qw) + " quadwords, " +
                         std::to_string(dbgVuCap_.triangleCount()) + " triangles";
}

void App::dbgReadCrashReport() {
    namespace fs = std::filesystem;
    if (!hasProject_) return;
    const fs::path path = fs::path(project_.dir) / "bin" / "crash.txt";
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    const size_t size = ec ? 0 : (size_t)sz;
    if (size == dbgCrashSize_) return;  // unchanged (0 == 0 covers "no crash")
    dbgCrashSize_ = size;
    dbgCrash_ = DbgCrash();
    if (!size) return;  // the Runner deletes it at build start: crash cleared

    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    dbgCrash_.raw = ss.str();
    dbgCrash_.present = true;

    // The report is deliberately line-oriented ("| key : value"), so this is a
    // scan, not a parser.
    std::istringstream ls(dbgCrash_.raw);
    std::string line;
    auto valueAfter = [](const std::string& l) {
        const size_t colon = l.find(':');
        if (colon == std::string::npos) return std::string();
        size_t i = colon + 1;
        while (i < l.size() && isspace((unsigned char)l[i])) ++i;
        return l.substr(i);
    };
    while (std::getline(ls, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("| CRASH:", 0) == 0) {
            dbgCrash_.cause = valueAfter(line);
        } else if (line.rfind("| epc", 0) == 0) {
            dbgCrash_.epc = (uint32_t)std::strtoul(valueAfter(line).c_str(), nullptr, 0);
        } else if (line.rfind("| badvaddr", 0) == 0) {
            dbgCrash_.badvaddr =
                (uint32_t)std::strtoul(valueAfter(line).c_str(), nullptr, 0);
        } else if (line.rfind("| frame", 0) == 0) {
            dbgCrash_.frame =
                (uint32_t)std::strtoul(valueAfter(line).c_str(), nullptr, 0);
            const size_t sc = line.find("scene:");
            if (sc != std::string::npos)
                dbgCrash_.scene = std::atoi(line.c_str() + sc + 6);
        } else if (line.rfind("| #", 0) == 0) {
            const size_t hex = line.find("0x");
            if (hex != std::string::npos)
                dbgCrash_.trace.push_back(
                    (uint32_t)std::strtoul(line.c_str() + hex, nullptr, 16));
        }
    }
    // Pull the editor forward - a crash deserves the same attention an assert
    // gets (PCSX2 has the foreground when the game dies).
    showDebugger_ = true;
    if (window_) {
        glfwRequestWindowAttention(window_);
        glfwFocusWindow(window_);
    }
    statusMessage_ = "Game crashed: " + dbgCrash_.cause;
}

// EPC + backtrace -> function names and source lines, via the PS2 toolchain in
// the build container (needs the unstripped bin/<name>.elf.sym a debug build
// keeps). On demand: it costs a container round-trip.
void App::dbgResolveCrashNames() {
    if (!dbgCrash_.present) return;
    std::vector<uint32_t> addrs;
    addrs.push_back(dbgCrash_.epc);
    for (uint32_t a : dbgCrash_.trace) addrs.push_back(a);
    std::string err;
    dbgCrash_.names = elfsym::symbolize(
        project_.dir, "bin/" + project_.elfName() + ".sym", addrs, &err);
    dbgCrash_.namesError = err;
}

std::string App::dbgBreakpointKey(const std::string& objectId, int nodeId) const {
    return objectId + ":" + std::to_string(nodeId);
}

bool App::dbgHasBreakpoint(const std::string& objectId, int nodeId) const {
    const std::string k = dbgBreakpointKey(objectId, nodeId);
    for (const std::string& b : project_.debugBreakpoints)
        if (b == k) return true;
    return false;
}

void App::dbgToggleBreakpoint(const std::string& objectId, int nodeId) {
    const std::string k = dbgBreakpointKey(objectId, nodeId);
    auto& list = project_.debugBreakpoints;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i] == k) {
            list.erase(list.begin() + i);
            dbgCmdWritten_ = false;  // the game's list must follow
            setDirty(true);
            return;
        }
    list.push_back(k);
    dbgCmdWritten_ = false;
    setDirty(true);
}

int App::dbgKeyFor(int scene, const std::string& objectId, int nodeId) const {
    for (const livedbg::NodeSym& n : dbgSyms_.nodes)
        if (n.scene == scene && n.nodeId == nodeId && n.objectId == objectId)
            return n.key;
    return -1;
}

float App::dbgNodeHeat(int key) const {
    if (key < 0 || key >= (int)dbgHeat_.size() || dbgHeat_[key] <= 0.0)
        return FLT_MAX;
    return (float)(ImGui::GetTime() - dbgHeat_[key]);
}

void App::dbgFireNode(int key, bool andRun) {
    if (key < 0) return;
    for (uint16_t k : dbgFireQueue_)
        if (k == (uint16_t)key) return;
    if ((int)dbgFireQueue_.size() >= livedbg::kMaxForced) return;
    dbgFireQueue_.push_back((uint16_t)key);
    dbgCmd_.fireAndRun = andRun;
    dbgCmdWritten_ = false;
}

bool App::dbgIsWatched(int objectIndex) const {
    for (const DbgObjTrack& t : dbgObjWatch_)
        if (t.index == objectIndex) return true;
    return false;
}

void App::dbgToggleObjectWatch(int objectIndex) {
    for (size_t i = 0; i < dbgObjWatch_.size(); ++i)
        if (dbgObjWatch_[i].index == objectIndex) {
            dbgObjWatch_.erase(dbgObjWatch_.begin() + i);
            dbgCmdWritten_ = false;  // the game's sample list must follow
            return;
        }
    if ((int)dbgObjWatch_.size() >= livedbg::kMaxWatchObjects) return;
    DbgObjTrack t;
    t.index = objectIndex;
    const auto& objs = project_.objects();
    if (objectIndex >= 0 && objectIndex < (int)objs.size())
        t.name = objs[objectIndex].name;
    dbgObjWatch_.push_back(std::move(t));
    dbgCmdWritten_ = false;
}

int App::dbgTimerFrames(int key) const {
    if (key < 0) return -1;
    for (const auto& t : dbgSnap_.timers)
        if (t.first == key) return t.second;
    return -1;
}

// --- The time machine -------------------------------------------------------
// docs/time-machine.md. The reader is deliberately dumb: the payload is a
// codegen detail, so the editor stores the bytes and hands the right ones back.
// What it does own is the history and its budget.

void App::livetimeTick() {
    namespace fs = std::filesystem;
    if (!hasProject_ || !project_.settings.timeMachine ||
        project_.settings.buildProfile != "debug") {
        if (!timeHistory_.empty()) timeHistory_.clear();
        timeHaveLast_ = false;
        timeScrub_ = -1;
        return;
    }
    const double now = ImGui::GetTime();
    if (now < timeNextTick_) return;
    // The game rewrites its capture every 6 frames (~8 Hz at 50 Hz); reading
    // faster than it writes only costs disk hits for the same bytes.
    timeNextTick_ = now + 0.1;

    timeHistory_.setBudget((size_t)timeBudgetMb_ << 20);

    livetime::Snapshot s;
    if (!livetime::readSnapshot(
            (fs::path(project_.dir) / "bin" / "livetime.bin").string(), s))
        return;  // no capture yet, or a torn write - retry next tick
    if (!timeHistory_.ingest(s)) return;  // same capture as last time
    timeLast_ = s;
    timeHaveLast_ = true;
    timeLastSeen_ = now;
    // A capture that arrived after a rewind means the game is running forward
    // again, so stop pinning the scrub to a frame that is now in the past.
    if (timeScrub_ >= (int)timeHistory_.count()) timeScrub_ = -1;
}

void App::timeMachineRewind(int index) {
    namespace fs = std::filesystem;
    if (index < 0 || index >= (int)timeHistory_.count()) return;
    livetime::Snapshot s = timeHistory_.at((size_t)index);
    // The game applies a restore when the sequence CHANGES, so a capture can be
    // pushed twice (rewind, run, rewind to the same spot) only if the number
    // moves. Counting up from the newest capture keeps it ahead of anything the
    // game has seen.
    const uint32_t base = timeHistory_.newest().seq;
    timeRestoreSeq_ = (timeRestoreSeq_ > base ? timeRestoreSeq_ : base) + 1;
    s.seq = timeRestoreSeq_;
    const std::string err = livetime::writeRestore(
        (fs::path(project_.dir) / "bin" / "livetime.rst").string(), s);
    if (!err.empty()) {
        timeStatus_ = "Rewind failed: " + err;
        return;
    }
    char msg[128];
    std::snprintf(msg, sizeof(msg), "Rewound %u frames (to frame %u)",
                  timeHistory_.newest().frame - timeHistory_.at((size_t)index).frame,
                  timeHistory_.at((size_t)index).frame);
    timeStatus_ = msg;
}

void App::drawTimeMachinePanel() {
    if (project_.settings.buildProfile != "debug") {
        ImGui::TextDisabled(
            "Rewinding needs the debug build profile\n"
            "(Project > Preferences > Build).");
        return;
    }
    if (!project_.settings.timeMachine) {
        ImGui::TextDisabled(
            "The time machine is off for this project\n"
            "(Project > Preferences > Build > Time machine).");
        return;
    }
    if (timeHistory_.empty()) {
        ImGui::TextDisabled(
            "No captures yet. Build & run (F5), and the game starts\n"
            "streaming its state a few times a second.");
        return;
    }

    const size_t count = timeHistory_.count();
    const uint32_t newestFrame = timeHistory_.newest().frame;
    // Frames -> seconds at the project's refresh rate: what the user actually
    // wants to know is "how far back can I go", in the units they think in.
    const float hz = project_.settings.videoSystem == "ntsc" ? 60.0f : 50.0f;
    ImGui::Text("%zu captures, %.1f s of history", count,
                (float)timeHistory_.frameSpan() / hz);
    ImGui::SameLine();
    ImGui::TextDisabled("(%.1f of %d MB)",
                        (double)timeHistory_.bytes() / (1024.0 * 1024.0),
                        timeBudgetMb_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "The history lives in the editor's memory, never on disk - the\n"
            "only files the channel touches are two fixed-size ones next to\n"
            "the ELF. Closing the project drops it. The budget is in\n"
            "Edit > Preferences.");
    if (ImGui::GetTime() - timeLastSeen_ > 2.0)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "The game stopped reporting - this history is frozen.");

    // The scrub picks WHICH capture; nothing happens to the game until Rewind.
    int scrub = timeScrub_ < 0 ? (int)count - 1 : timeScrub_;
    ImGui::SetNextItemWidth(-FLT_MIN);
    char label[64];
    std::snprintf(label, sizeof(label), "-%.1f s",
                  (float)(newestFrame - timeHistory_.at((size_t)ImClamp(
                                                            scrub, 0, (int)count - 1))
                                            .frame) /
                      hz);
    if (ImGui::SliderInt("##rewindscrub", &scrub, 0, (int)count - 1, label))
        timeScrub_ = scrub;
    scrub = ImClamp(scrub, 0, (int)count - 1);

    const livetime::Snapshot& sel = timeHistory_.at((size_t)scrub);
    ImGui::TextDisabled("frame %u, scene %d, %d objects, %zu B", sel.frame,
                        sel.scene, sel.objectCount, sel.state.size());

    if (ImGui::Button("Rewind to here")) timeMachineRewind(scrub);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Puts the RUNNING game back into this capture: object transforms,\n"
            "physics, animation, flow variables, save values and where the\n"
            "player stands. The game keeps running from there - with Live\n"
            "Logic on you can edit a graph first and watch the new logic play\n"
            "out on the old situation.");
    ImGui::SameLine();
    if (ImGui::Button("Live")) timeScrub_ = -1;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Follow the newest capture again.");
    ImGui::SameLine();
    if (ImGui::Button("Clear history")) {
        timeHistory_.clear();
        timeScrub_ = -1;
        timeStatus_ = "History cleared";
    }

    if (!timeStatus_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", timeStatus_.c_str());
    }

    ImGui::Separator();
    ImGui::TextDisabled("What a rewind puts back");
    prefHelp(
        "Objects (transform, physics, animation), the walker and its motion,\n"
        "flow variables, save values, and every graph's own timers and\n"
        "latches.\n\n"
        "NOT put back: sequences mid-play, menus, audio and particles.\n"
        "See docs/time-machine.md.");
}

void App::livedbgTick() {
    namespace fs = std::filesystem;
    if (!hasProject_ || !project_.settings.liveDebug ||
        project_.settings.buildProfile != "debug") {
        dbgState_ = DbgState::Off;
        return;
    }
    const double now = ImGui::GetTime();
    if (now < dbgNextTick_) return;  // keep the last state between ticks
    dbgNextTick_ = now + 0.05;       // 20 Hz: the graph overlay is animated

    // A crash report is cheap to look for and the most important thing there
    // is, so check for it a few times a second.
    if (now >= dbgCrashNextRead_) {
        dbgCrashNextRead_ = now + 0.4;
        dbgReadCrashReport();
        dbgReadVuCapture();
    }

    // The symbol table is a build artifact of codegen (src/gen/livedbg.sym).
    // Re-read it rarely - a build or a --refresh-gen must be picked up, but it
    // is not worth a disk hit at tick rate.
    if (now >= dbgSymNextRead_) {
        dbgSymNextRead_ = now + 1.5;
        livedbg::Symbols s;
        if (livedbg::loadSymbols(
                (fs::path(project_.dir) / "src" / "gen" / "livedbg.sym").string(), s)) {
            if (s.hash != dbgSyms_.hash) {
                // A different table means different keys: everything derived
                // from the old one (heat, deltas, history) is meaningless now.
                dbgHeat_.clear();
                dbgPrevHits_.clear();
                dbgTimeline_.clear();
                dbgScrub_ = -1;
            }
            dbgSyms_ = std::move(s);
        }
    }
    if (!dbgSyms_.loaded) {
        dbgState_ = DbgState::NoBuild;
        return;
    }

    const fs::path binDir = fs::path(project_.dir) / "bin";
    livedbg::Snapshot snap;
    if (livedbg::readSnapshot((binDir / "livedbg.bin").string(), snap) &&
        (snap.seq != dbgSnap_.seq || snap.frame != dbgSnap_.frame)) {
        // A restarted game rewinds its frame counter: drop the history and the
        // heat so the new run is not drawn on top of the old one's trail.
        const bool restarted = snap.frame + 1 < dbgSnap_.frame;
        if (restarted) {
            dbgHeat_.clear();
            dbgPrevHits_.clear();
            dbgTimeline_.clear();
            dbgScrub_ = -1;
            for (DbgObjTrack& t : dbgObjWatch_) t.samples.clear();
            dbgCmdWritten_ = false;  // re-arm the breakpoints in the new run
        }
        // FPS from the editor's own wall clock across two snapshots: what the
        // player actually sees, and it needs nothing from the engine.
        if (!restarted && dbgSnapPrevTime_ > 0.0 && snap.frame > dbgSnapPrevFrame_) {
            const double dt = now - dbgSnapPrevTime_;
            if (dt > 0.15) {  // long enough to be a measurement, not a jitter
                dbgFps_ = (float)((snap.frame - dbgSnapPrevFrame_) / dt);
                dbgSnapPrevTime_ = now;
                dbgSnapPrevFrame_ = snap.frame;
            }
        } else {
            dbgSnapPrevTime_ = now;
            dbgSnapPrevFrame_ = snap.frame;
        }

        dbgHeat_.resize(dbgSyms_.nodes.size(), 0.0);
        // Heat comes from the DELTA between two snapshots, so the first one of
        // a session is only a baseline - counting it would flash every node
        // that has ever fired the moment the editor attaches.
        const bool baseline = dbgPrevHits_.size() != snap.hits.size();
        if (baseline) dbgPrevHits_.assign(snap.hits.size(), 0);
        if (!baseline)
            for (size_t i = 0; i < snap.hits.size() && i < dbgHeat_.size(); ++i)
                if (snap.hits[i] != dbgPrevHits_[i]) dbgHeat_[i] = now;
        dbgPrevHits_ = snap.hits;
        dbgTimeline_.ingest(snap);
        // Fold the watched-object samples into their tracks (the game flushes
        // its ring, so each sample arrives exactly once; a restarted game is
        // handled by the reset above).
        for (const livedbg::ObjWatch& w : snap.objects)
            for (DbgObjTrack& t : dbgObjWatch_) {
                if (t.index != w.index) continue;
                for (const livedbg::ObjSample& sm : w.samples) {
                    if (!t.samples.empty() && sm.frame <= t.samples.back().frame)
                        continue;
                    t.samples.push_back(sm);
                }
                if (t.samples.size() > kDbgTrackSamples)
                    t.samples.erase(t.samples.begin(),
                                    t.samples.begin() +
                                        (t.samples.size() - kDbgTrackSamples));
            }
        dbgSnap_ = std::move(snap);
        dbgSnapTime_ = now;
    }

    // The game is "there" while its snapshots keep arriving. A halted game
    // still flushes (its loop keeps running), so silence means gone, not paused.
    const bool reporting = dbgSnapTime_ > 0.0 && now - dbgSnapTime_ < 2.0;
    // A game that WAS reporting and stopped, with no crash report and no
    // assertion, is a hang (or an exception nobody caught): the devkit
    // heartbeat is the only witness. Remember where it died - the fire history,
    // the watch curves and the timers from just before are still in memory, and
    // that is the post-mortem.
    if (!reporting && dbgSnapTime_ > 0.0 && !dbgLostGame_ &&
        !dbgCrash_.present && dbgSnap_.frame > 0) {
        dbgLostGame_ = true;
        dbgLostAtFrame_ = dbgSnap_.frame;
        statusMessage_ = "The game stopped reporting at frame " +
                         std::to_string(dbgLostAtFrame_) +
                         " - crash or hang? See the Debugger.";
    }
    if (reporting) dbgLostGame_ = false;
    if (!reporting) {
        dbgState_ = DbgState::Waiting;
    } else if (dbgSnap_.hash != dbgSyms_.hash) {
        dbgState_ = DbgState::Stale;
    } else {
        dbgState_ = dbgSnap_.halted ? DbgState::Halted : DbgState::Running;
    }

    // Command file. Written when the desired state changed - and whenever the
    // file is missing, which is how a fresh run (the Runner deletes it) gets
    // this session's breakpoints back without the user doing anything.
    livedbg::Command want = dbgCmd_;
    want.breakpoints.clear();
    for (const livedbg::NodeSym& n : dbgSyms_.nodes)
        if (dbgHasBreakpoint(n.objectId, n.nodeId) &&
            (int)want.breakpoints.size() < livedbg::kMaxBreakpoints)
            want.breakpoints.push_back((uint16_t)n.key);
    want.fire = dbgFireQueue_;
    want.watchObjects.clear();
    for (const DbgObjTrack& t : dbgObjWatch_)
        if (t.index >= 0) want.watchObjects.push_back((uint16_t)t.index);
    const bool missing = !fs::exists(binDir / "livedbg.cmd");
    if (!dbgCmdWritten_ || missing || !want.sameStateAs(dbgCmd_)) {
        want.seq = dbgCmd_.seq + 1;
        if (livedbg::writeCommand((binDir / "livedbg.cmd").string(), want)
                .empty()) {
            dbgCmd_ = want;
            dbgCmdWritten_ = true;
            // A force-fire is a one-shot: it has been handed over, and the
            // step/pause request must not re-fire on the next resend either.
            dbgFireQueue_.clear();
            dbgCmd_.fire.clear();
            dbgCmd_.stepFrames = 0;
            dbgCmd_.stepUntilFire = false;
            dbgCmd_.fireAndRun = false;
            dbgCmd_.captureVu = false;
            dbgCmd_.measureRam = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Live Logic (docs/live-logic.md): compile every edited graph into the
// interpreter's IR and stream it to the running game. The heavy lifting is in
// livelogic.cpp; this is the "when to (re)compile and write" half.
// ---------------------------------------------------------------------------
void App::liveLogicTick() {
    namespace fs = std::filesystem;
    if (!hasProject_ || !project_.settings.liveLogic ||
        project_.settings.buildProfile != "debug") {
        liveLogicState_ = LogicState::Off;
        return;
    }
    const double now = ImGui::GetTime();
    if (now < liveLogicNextTick_) return;
    liveLogicNextTick_ = now + 0.15;  // recompiling every graph is not free

    // What the running ELF compiled natively (codegen writes it at build start).
    if (now >= liveLogicBuiltNextRead_) {
        liveLogicBuiltNextRead_ = now + 1.5;
        livelogic::BuiltList list;
        if (livelogic::loadBuiltList(
                (fs::path(project_.dir) / "src" / "gen" / "livelogic.built")
                    .string(),
                list))
            liveLogicBuilt_ = std::move(list);
    }
    if (!liveLogicBuilt_.loaded) {
        liveLogicState_ = LogicState::NoBuild;
        return;
    }

    // Every graph that differs from the built one needs a patch; the ones the
    // IR cannot express are reported instead (the chip goes amber).
    std::vector<livelogic::Program> programs;
    liveLogicBlocked_.clear();
    for (size_t si = 0; si < project_.scenes.size(); ++si) {
        const SceneData& sc = project_.scenes[si];
        for (size_t oi = 0; oi < sc.objects.size(); ++oi) {
            const SceneObject& o = sc.objects[oi];
            if (o.flowGraph.empty()) continue;
            const uint64_t live = livelogic::graphHash(o.flowGraph);
            uint64_t built = 0;
            bool wasBuilt = false;
            for (const livelogic::BuiltGraph& g : liveLogicBuilt_.graphs)
                if (g.scene == (int)si && g.objectId == o.id) {
                    built = g.hash;
                    wasBuilt = true;
                    break;
                }
            if (wasBuilt && built == live) continue;  // native copy is correct
            const livelogic::Capability cap =
                livelogic::capability(project_, sc, o.flowGraph);
            if (!cap.patchable) {
                std::string why;
                for (size_t i = 0; i < cap.reasons.size(); ++i)
                    why += (i ? ", " : "") + cap.reasons[i];
                liveLogicBlocked_.emplace_back(o.name, why);
                continue;
            }
            if (!wasBuilt) {
                // A graph added since the build: the object itself may not even
                // exist in the running game. Live Link spawns new objects but
                // cannot give them logic, so this is a rebuild case.
                liveLogicBlocked_.emplace_back(
                    o.name, "the graph did not exist at build time");
                continue;
            }
            livelogic::Program prog;
            if (!livelogic::compile(project_, (int)si, oi, prog)) {
                liveLogicBlocked_.emplace_back(o.name, "the graph did not compile");
                continue;
            }
            // Debug keys, so a patched graph still lights up in the Debugger.
            for (livelogic::Block& b : prog.blocks)
                if (const int k = dbgKeyFor((int)si, o.id, b.nodeId); k >= 0)
                    b.dbgKey = (uint16_t)k;
            for (livelogic::Instr& in : prog.instrs)
                if (const int k = dbgKeyFor((int)si, o.id, in.nodeId); k >= 0)
                    in.dbgKey = (uint16_t)k;
            if ((int)programs.size() < livelogic::kMaxPrograms)
                programs.push_back(std::move(prog));
        }
    }
    liveLogicPatchCount_ = (int)programs.size();
    liveLogicState_ = !liveLogicBlocked_.empty() ? LogicState::Blocked
                      : programs.empty()         ? LogicState::InSync
                                                 : LogicState::Patched;

    const fs::path binDir = fs::path(project_.dir) / "bin";
    const fs::path patch = binDir / "livelogic.bin";
    if (programs.empty()) {
        // Back in sync (or nothing patchable): remove the patch so the game
        // hands its graphs back to the compiled scripts.
        if (!liveLogicLastPayload_.empty() || fs::exists(patch)) {
            std::error_code ec;
            fs::remove(patch, ec);
            liveLogicLastPayload_.clear();
        }
        return;
    }

    // Bytes first, seq second: an unchanged program must not rewrite the file
    // (the game would re-apply it and reset every graph's state).
    std::vector<unsigned char> body = livelogic::encode(programs, 0);
    if (body == liveLogicLastPayload_ && fs::exists(patch)) return;
    const uint32_t seq = ++liveLogicSeq_;
    const std::vector<unsigned char> file = livelogic::encode(programs, seq);
    if (livelogic::write(patch.string(), file).empty())
        liveLogicLastPayload_ = std::move(body);
}

// The VU tab's verdict block: the handful of questions worth asking of every
// capture, answered before the user has to read a single hex value. All of it
// comes out of the decode (src/vucap.cpp); nothing here is a guess about the
// scene - a finding either counts vertices or it is not printed.
static void dbgVuDrawFindings(const vucap::Capture& c) {
    if (!c.hasVuMem) return;
    const int inTris = c.inputTris();
    const int outTris = c.outputTris();
    ImGui::Separator();
    ImGui::Text("input: %d mesh(es), %d triangles   ->   staged in VU1: "
                "%d packet(s), %d triangles",
                (int)c.meshes.size(), inTris, (int)c.gifs.size(), outTris);
    ImGui::TextDisabled(
        "VU1 memory is never cleared, so that count includes what earlier runs "
        "left. The checks below read the BIGGEST geometry packet (%d vertices) "
        "- this run's output.",
        c.gsVerts);

    const ImU32 warn = IM_COL32(240, 190, 90, 255);
    int findings = 0;
    auto say = [&](const char* fmt, ...) {
        ++findings;
        va_list ap;
        va_start(ap, fmt);
        ImGui::PushStyleColor(ImGuiCol_Text, warn);
        ImGui::TextV(fmt, ap);
        ImGui::PopStyleColor();
        va_end(ap);
    };
    if (c.hasWindow && c.gsVerts)
        ImGui::TextDisabled(
            "packet on screen: x %.0f..%.0f, y %.0f..%.0f  vs  window x "
            "%.0f..%.0f, y %.0f..%.0f  (%d vertex/vertices outside it, which is "
            "ordinary - the GS scissors them)",
            c.gsX0, c.gsX1, c.gsY0, c.gsY1, c.winX0, c.winX1, c.winY0, c.winY1,
            c.gsOffWindow);
    if (c.packetOffscreen)
        say("! that packet misses the drawing window entirely - nothing of it "
            "can appear on screen");
    if (c.hugeTris)
        say("! %d staged triangle(s) span nearly the whole GS plane - the "
            "classic sign of a vertex at or behind w = 0",
            c.hugeTris);
    if (c.behindVerts)
        say("! %d input vertex/vertices have clip w <= 0 (behind the camera)",
            c.behindVerts);
    else if (c.meshes.size() > 1)
        ImGui::TextDisabled(
            "(the w <= 0 and host-reference checks need a single-mesh flush: "
            "VU1 memory keeps only the LAST mesh's MVP - pin a flush that "
            "carries one mesh to get them)");
    if (c.degenerateTris)
        say("! %d input triangle(s) have no area - they cost a transform and "
            "draw nothing",
            c.degenerateTris);
    if (c.gsZeroAlpha)
        say("! %d staged vertices are fully transparent in a packet with no "
            "+ABE - check the material",
            c.gsZeroAlpha);
    if (!findings)
        ImGui::TextDisabled(
            "Nothing obviously wrong: the staged packet reaches the drawing "
            "window, no triangle is degenerate and nothing is behind w = 0.");
    if (!c.hasWindow)
        ImGui::TextDisabled(
            "(The on-screen check needs a capture from a game built after this "
            "panel grew it - rebuild to get it.)");
    ImGui::Separator();
}

// Tools > Debugger (F9). The state of the running game's logic: what fired,
// what the variables hold, where it is stopped - plus the transport controls
// and the breakpoint list. The graph itself is the other half of this UI (the
// Flow Graph window's overlay); this panel is the instrument cluster.
void App::drawDebuggerWindow() {
    if (!showDebugger_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(420), scaled(520)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debugger", &showDebugger_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }

    // --- object / node naming helpers --------------------------------------
    auto objectName = [&](int scene, const std::string& id) -> std::string {
        if (scene >= 0 && scene < (int)project_.scenes.size())
            for (const SceneObject& o : project_.scenes[scene].objects)
                if (o.id == id) return o.name;
        return "(deleted object)";
    };
    auto nodeTitle = [&](const livedbg::NodeSym& n) -> std::string {
        const FlowNodeType* t = flowNodeType(n.type);
        return t ? t->title : n.type;
    };
    auto nodeLabel = [&](const livedbg::NodeSym& n) -> std::string {
        return objectName(n.scene, n.objectId) + " \xc2\xb7 " + nodeTitle(n);
    };
    // Jumps the Flow Graph window to a node's owner (and selects the object).
    auto revealNode = [&](const livedbg::NodeSym& n) {
        if (n.scene >= 0 && n.scene < (int)project_.scenes.size() &&
            n.scene != project_.activeScene) {
            // Same steps the Project window's scene list takes - terrain and
            // lighting are per scene, and staged pastes belong to the old one.
            project_.activeScene = n.scene;
            clearSelection();
            cancelPastePlacement();
            flowPositionsApplied_ = false;
            applyProjectToViewport();
        }
        const auto& objs = project_.objects();
        for (size_t i = 0; i < objs.size(); ++i)
            if (objs[i].id == n.objectId) {
                flowGraphObject_ = (int)i;
                flowPositionsApplied_ = false;
                selectedObject_ = (int)i;
                ImGui::SetWindowFocus("Flow Graph");
                break;
            }
    };

    // --- state chip + guidance ---------------------------------------------
    struct Chip { ImU32 col; const char* text; };
    Chip chip{IM_COL32(150, 150, 150, 255), "off"};
    switch (dbgState_) {
        case DbgState::Off: chip = {IM_COL32(150, 150, 150, 255), "OFF"}; break;
        case DbgState::NoBuild:
            chip = {IM_COL32(140, 140, 140, 255), "NO SYMBOLS"};
            break;
        case DbgState::Waiting:
            chip = {IM_COL32(150, 170, 200, 255), "WAITING FOR THE GAME"};
            break;
        case DbgState::Stale:
            chip = {IM_COL32(240, 175, 70, 255), "STALE (rebuild)"};
            break;
        case DbgState::Running:
            chip = {IM_COL32(95, 200, 115, 255), "RUNNING"};
            break;
        case DbgState::Halted:
            chip = {IM_COL32(245, 130, 90, 255), "HALTED"};
            break;
    }
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = scaled(5.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(
            ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r,
            chip.col);
        ImGui::Dummy(ImVec2(r * 2.0f + scaled(4.0f), 0.0f));
        ImGui::SameLine();
        ImGui::TextColored(ImColor(chip.col), "%s", chip.text);
    }
    if (dbgState_ == DbgState::Halted && dbgSnap_.breakKey >= 0) {
        if (const livedbg::NodeSym* n = dbgSyms_.find(dbgSnap_.breakKey)) {
            ImGui::SameLine();
            ImGui::Text("at %s", nodeLabel(*n).c_str());
            if (ImGui::IsItemClicked()) revealNode(*n);
        }
    }
    if (dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted) {
        ImGui::SameLine(0.0f, scaled(12.0f));
        ImGui::TextDisabled("frame %u \xc2\xb7 %.0f fps \xc2\xb7 scene %d",
                            dbgSnap_.frame, dbgFps_, dbgSnap_.scene);
    }

    switch (dbgState_) {
        case DbgState::Off:
            ImGui::TextWrapped(
                "The Live Debugger is compiled only into debug builds with the "
                "\"Live Debugger\" preference on.");
            if (project_.settings.buildProfile != "debug")
                ImGui::TextDisabled(
                    "This project builds in the release profile "
                    "(Project > Preferences > Build).");
            if (ImGui::Checkbox("Live Debugger (project setting)",
                                &project_.settings.liveDebug))
                commitChange();
            break;
        case DbgState::NoBuild:
            ImGui::TextWrapped(
                "No symbol table yet. Build & Run (F5) once - codegen writes "
                "src/gen/livedbg.sym next to the generated sources, and the "
                "game starts reporting as soon as it boots.");
            break;
        case DbgState::Waiting:
            ImGui::TextWrapped(
                "Symbols loaded (%d nodes). Waiting for a game to report - "
                "Build & Run (F5) for PCSX2, or F6 for a real console.",
                (int)dbgSyms_.nodes.size());
            break;
        case DbgState::Stale:
            ImGui::TextWrapped(
                "The running game was built from different graphs, so its node "
                "numbering no longer matches the project. Build & Run (F5) to "
                "resync - nothing is highlighted until then.");
            break;
        default: break;
    }

    // --- transport ---------------------------------------------------------
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    ImGui::Separator();
    ImGui::BeginDisabled(!live);
    const float btnW = scaled(104.0f);
    if (dbgState_ == DbgState::Halted) {
        if (ImGui::Button("\xe2\x96\xb6 Continue", ImVec2(btnW, 0))) {
            dbgCmd_.halt = false;
            dbgCmd_.stepFrames = 0;
            dbgCmd_.stepUntilFire = false;
            dbgCmdWritten_ = false;
            dbgScrub_ = -1;
        }
    } else {
        if (ImGui::Button("\xe2\x8f\xb8 Pause", ImVec2(btnW, 0))) {
            dbgCmd_.halt = true;
            dbgCmd_.stepFrames = 0;
            dbgCmd_.stepUntilFire = false;
            dbgCmdWritten_ = false;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "F10. A halt freezes the world - scripts, the walker, particles, "
            "animation -\nwhile the game keeps presenting frames, so you can "
            "still look at what you stopped.");
    ImGui::SameLine();
    if (ImGui::Button("\xe2\x8f\xad Step frame", ImVec2(btnW, 0))) {
        dbgCmd_.halt = false;
        dbgCmd_.stepFrames = 1;
        dbgCmd_.stepUntilFire = false;
        dbgCmdWritten_ = false;
        dbgScrub_ = -1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("F11. Runs exactly one frame, then stops again.");
    ImGui::SameLine();
    if (ImGui::Button("\xe2\x8f\xa9 Step node", ImVec2(btnW, 0))) {
        dbgCmd_.halt = false;
        dbgCmd_.stepFrames = 0;
        dbgCmd_.stepUntilFire = true;
        dbgCmdWritten_ = false;
        dbgScrub_ = -1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Runs until ANY instrumented node fires, then stops on it -\n"
            "the fastest way to find out what actually runs next.");
    ImGui::EndDisabled();

    // --- crash / lost game -------------------------------------------------
    // The loudest thing in the panel, because it is the most important: a real
    // EE exception, or a heartbeat that simply stopped.
    if (dbgCrash_.present) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("CRASH: %s", dbgCrash_.cause.empty()
                                            ? "EE exception"
                                            : dbgCrash_.cause.c_str());
        ImGui::PopStyleColor();
        ImGui::Text("epc 0x%08x   badvaddr 0x%08x   frame %u   scene %d",
                    dbgCrash_.epc, dbgCrash_.badvaddr, dbgCrash_.frame,
                    dbgCrash_.scene);
        if (ImGui::SmallButton("Resolve names")) dbgResolveCrashNames();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Runs the PS2 toolchain's addr2line in the build container "
                "against\nthe unstripped bin/*.elf.sym a debug build keeps - "
                "turns these\naddresses into functions and source lines.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy report")) ImGui::SetClipboardText(dbgCrash_.raw.c_str());
        // A crash is terminal - the game is idling on its crash screen and the
        // only way on is to launch it again. Offer that HERE rather than making
        // the reader go find F5/F6: same transport the crash came from (the PS2
        // if its file server is still up, otherwise the emulator) and no
        // rebuild, since nothing was edited.
        ImGui::SameLine();
        const bool crashBusy = runner_.busy();
        const bool onPs2 = runner_.ps2ClientAlive();
        ImGui::BeginDisabled(crashBusy);
        if (ImGui::SmallButton(onPs2 ? "Run again on PS2" : "Run again")) {
            dbgCrash_ = DbgCrash();
            dbgCrashSize_ = 0;
            if (onPs2)
                runner_.buildAndRunPs2(projectForBuild(), false);
            else
                runner_.buildAndRun(projectForBuild(), false);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Relaunches the SAME build (no rebuild) and clears this "
                "report.\nRebuild first with F5 / F6 if you changed anything.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) {
            dbgCrash_ = DbgCrash();
            dbgCrashSize_ = 0;
        }
        if (!dbgCrash_.namesError.empty())
            ImGui::TextDisabled("%s", dbgCrash_.namesError.c_str());
        if (!dbgCrash_.names.empty()) {
            // A demangled C++ name carries its whole template expansion and runs
            // to hundreds of characters, while the Debugger is a narrow docked
            // panel - so the backtrace gets its OWN horizontally scrolling box
            // instead of running off the right edge where it cannot be read.
            // Wrapping was the other option and is worse here: one frame per
            // line is what makes a backtrace scannable.
            int rows = 0;
            for (const elfsym::Location& l : dbgCrash_.names)
                if (!l.func.empty()) ++rows;
            if (rows > 8) rows = 8;
            const float lineH = ImGui::GetTextLineHeightWithSpacing();
            ImGui::BeginChild("##crashtrace",
                              ImVec2(0.0f, lineH * ((float)rows + 0.6f)),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t i = 0; i < dbgCrash_.names.size(); ++i) {
                const elfsym::Location& l = dbgCrash_.names[i];
                const char* label = i == 0 ? "crash" : "called from";
                if (l.func.empty()) continue;
                ImGui::Text("%-11s %s", label, l.func.c_str());
                if (!l.source.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", l.source.c_str());
                }
            }
            ImGui::EndChild();
        }
        // The post-mortem the devkit already has: what the graphs did just
        // before, and where the watched objects were.
        const auto& frames = dbgTimeline_.frames();
        if (!frames.empty()) {
            ImGui::TextDisabled("Last flow-graph nodes before the crash:");
            int shown = 0;
            for (size_t i = frames.size(); i-- > 0 && shown < 8;) {
                for (int k : frames[i].keys) {
                    if (shown++ >= 8) break;
                    if (const livedbg::NodeSym* n = dbgSyms_.find(k))
                        ImGui::BulletText("frame %u: %s", frames[i].frame,
                                          nodeLabel(*n).c_str());
                }
            }
        }
        for (const DbgObjTrack& t : dbgObjWatch_)
            if (!t.samples.empty()) {
                const livedbg::ObjSample& s = t.samples.back();
                ImGui::BulletText("%s at %.2f, %.2f, %.2f (frame %u)",
                                  t.name.c_str(), s.pos[0], s.pos[1], s.pos[2],
                                  s.frame);
            }
    } else if (dbgLostGame_) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.7f, 0.35f, 1.0f));
        ImGui::TextWrapped(
            "The game stopped reporting at frame %u - no crash report and no "
            "assertion, so it hung (or took an exception with the crash handler "
            "off).",
            dbgLostAtFrame_);
        ImGui::PopStyleColor();
        const auto& frames = dbgTimeline_.frames();
        if (!frames.empty()) {
            ImGui::TextDisabled("The last nodes that ran:");
            int shown = 0;
            for (size_t i = frames.size(); i-- > 0 && shown < 6;)
                for (int k : frames[i].keys) {
                    if (shown++ >= 6) break;
                    if (const livedbg::NodeSym* n = dbgSyms_.find(k))
                        ImGui::BulletText("frame %u: %s", frames[i].frame,
                                          nodeLabel(*n).c_str());
                }
        }
        if (!project_.settings.eeCrashHandler) {
            ImGui::TextDisabled("Tip: turn the EE crash handler on");
            prefHelp(
                "Preferences > Build > \"EE crash handler\" makes the game write\n"
                "a register dump and a backtrace instead of just going quiet\n"
                "(experimental).");
        }
    }

    // Armed countdowns, right under the transport: a Delay only advances on
    // frames that RUN, so a halted game (or a single-frame Fire) leaves the
    // branch behind it looking dead. Saying so here is cheaper than a doc.
    if (live && !dbgSnap_.timers.empty()) {
        const float fps = dbgFps_ > 1.0f ? dbgFps_ : 50.0f;
        int soonest = 1 << 30;
        for (const auto& t : dbgSnap_.timers)
            soonest = ImMin(soonest, t.second);
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
                           "\xe2\x8f\xb1 %d armed timer%s, next in %.1fs",
                           (int)dbgSnap_.timers.size(),
                           dbgSnap_.timers.size() == 1 ? "" : "s",
                           soonest / fps);
        if (dbgState_ == DbgState::Halted) {
            ImGui::SameLine();
            ImGui::TextDisabled("(frozen - Continue for it to fire)");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Delay nodes counting down. They only advance while the game "
                "runs:\nafter a one-frame Fire, or while stopped, the branch "
                "after a Delay never comes.");
    }

    // --- tabs --------------------------------------------------------------
    if (!ImGui::BeginTabBar("##dbgtabs")) {
        ImGui::End();
        return;
    }

    // Watch: flow variables + save values, live or as of the scrubbed frame.
    if (ImGui::BeginTabItem("Watch")) {
        const std::vector<float>* vals = &dbgSnap_.vars;
        const auto& frames = dbgTimeline_.frames();
        if (dbgScrub_ >= 0 && dbgScrub_ < (int)frames.size() &&
            !frames[dbgScrub_].vars.empty())
            vals = &frames[dbgScrub_].vars;
        if (dbgSyms_.vars.empty()) {
            ImGui::TextDisabled(
                "This project has no flow variables and no save values.");
            ImGui::TextWrapped(
                "Variables nodes (Set/Get Int, Bool, Position) and Save values "
                "show up here automatically.");
        } else if (ImGui::BeginTable("##watch", 3,
                                     ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingStretchProp |
                                         ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 0.2f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < dbgSyms_.vars.size(); ++i) {
                const livedbg::VarSym& v = dbgSyms_.vars[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(v.name.c_str());
                ImGui::TableSetColumnIndex(1);
                const char* kind = v.kind == 'i'   ? "int"
                                   : v.kind == 'b' ? "bool"
                                   : v.kind == 'p' ? "position"
                                                   : "save value";
                ImGui::TextDisabled("%s", kind);
                ImGui::TableSetColumnIndex(2);
                const size_t o = i * 3;
                if (o + 2 >= vals->size()) {
                    ImGui::TextDisabled("-");
                } else if (v.kind == 'b') {
                    ImGui::TextColored((*vals)[o] != 0.0f
                                           ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                                           : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                       "%s", (*vals)[o] != 0.0f ? "true" : "false");
                } else if (v.kind == 'p') {
                    ImGui::Text("%.2f, %.2f, %.2f", (*vals)[o], (*vals)[o + 1],
                                (*vals)[o + 2]);
                } else if (v.kind == 'i') {
                    ImGui::Text("%d", (int)(*vals)[o]);
                } else {
                    ImGui::Text("%g", (*vals)[o]);
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }

    // Timeline: the rewind. One column per frame that had a fire; click (or
    // drag the slider) to inspect that frame - the graph overlay follows.
    if (ImGui::BeginTabItem("Timeline")) {
        const auto& frames = dbgTimeline_.frames();
        if (frames.empty()) {
            ImGui::TextDisabled("Nothing has fired yet.");
        } else {
            const bool liveView = dbgScrub_ < 0;
            if (liveView) {
                ImGui::TextDisabled(
                    "Live. Click a column (or drag below) to rewind - the graph "
                    "shows that frame.");
            } else {
                const int idx = ImClamp(dbgScrub_, 0, (int)frames.size() - 1);
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                                   "Rewound to frame %u (%d fired)",
                                   frames[idx].frame,
                                   (int)frames[idx].keys.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Back to live")) dbgScrub_ = -1;
            }

            // The tape: newest frames on the right, bar height = fires in that
            // frame, breakpoint-carrying frames tinted.
            const float h = scaled(72.0f);
            const ImVec2 size(ImGui::GetContentRegionAvail().x, h);
            const ImVec2 org = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##tape", size);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(org, ImVec2(org.x + size.x, org.y + size.y),
                              IM_COL32(22, 24, 28, 255), scaled(3.0f));
            const float colW = ImMax(scaled(2.0f), 3.0f);
            const int fits = (int)(size.x / colW);
            const int first = ImMax(0, (int)frames.size() - fits);
            int maxFires = 1;
            for (size_t i = first; i < frames.size(); ++i)
                maxFires = ImMax(maxFires, (int)frames[i].keys.size());
            const bool hovered = ImGui::IsItemHovered();
            int hoverIdx = -1;
            for (size_t i = first; i < frames.size(); ++i) {
                const float x = org.x + (i - first) * colW;
                const float frac = (float)frames[i].keys.size() / (float)maxFires;
                const float bh = ImMax(scaled(2.0f), frac * (h - scaled(8.0f)));
                const bool sel = dbgScrub_ == (int)i;
                ImU32 col = sel ? IM_COL32(250, 200, 90, 255)
                                : IM_COL32(90, 175, 235, 220);
                dl->AddRectFilled(ImVec2(x, org.y + h - bh - scaled(4.0f)),
                                  ImVec2(x + colW - 1.0f, org.y + h - scaled(4.0f)),
                                  col);
                if (hovered && ImGui::GetMousePos().x >= x &&
                    ImGui::GetMousePos().x < x + colW)
                    hoverIdx = (int)i;
            }
            if (hoverIdx >= 0) {
                ImGui::BeginTooltip();
                ImGui::Text("frame %u", frames[hoverIdx].frame);
                int shown = 0;
                for (int k : frames[hoverIdx].keys) {
                    if (++shown > 8) {
                        ImGui::TextDisabled("...");
                        break;
                    }
                    if (const livedbg::NodeSym* n = dbgSyms_.find(k))
                        ImGui::TextDisabled("%s", nodeLabel(*n).c_str());
                }
                ImGui::EndTooltip();
            }
            if (hoverIdx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                dbgScrub_ = hoverIdx;
            int scrub = dbgScrub_ < 0 ? (int)frames.size() - 1 : dbgScrub_;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##scrub", &scrub, 0, (int)frames.size() - 1,
                                 "frame %d of history"))
                dbgScrub_ = scrub;

            // What fired in the inspected frame, in order.
            const int idx = ImClamp(dbgScrub_ < 0 ? (int)frames.size() - 1 : dbgScrub_,
                                    0, (int)frames.size() - 1);
            ImGui::BeginChild("##fires", ImVec2(0, 0), true);
            {
                for (int k : frames[idx].keys) {
                    const livedbg::NodeSym* n = dbgSyms_.find(k);
                    if (!n) continue;
                    const FlowNodeType* t = flowNodeType(n->type);
                    ImGui::PushID(k);
                    ImGui::Bullet();
                    ImGui::SameLine();
                    if (ImGui::Selectable(nodeLabel(*n).c_str())) revealNode(*n);
                    if (t && t->trigger) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(trigger)");
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndTabItem();
    }

    // Rewind: the same axis as the Timeline above, but it moves the WORLD.
    // The execution log says what ran on frame N; this puts the game back into
    // the state it was in on frame N (docs/time-machine.md).
    if (ImGui::BeginTabItem("Rewind")) {
        drawTimeMachinePanel();
        ImGui::EndTabItem();
    }

    // Nodes: everything instrumented in the graph currently being edited, with
    // its hit count, a breakpoint toggle and (for triggers) a Fire button.
    if (ImGui::BeginTabItem("Nodes")) {
        const auto& objs = project_.objects();
        const bool haveGraph = flowGraphObject_ >= 0 &&
                               flowGraphObject_ < (int)objs.size();
        if (!haveGraph) {
            ImGui::TextDisabled("No graph selected in the Flow Graph window.");
        } else {
            ImGui::Text("Graph of \"%s\"", objs[flowGraphObject_].name.c_str());
            ImGui::TextDisabled(
                "Right-clicking a node in the Flow Graph does the same.");
            if (ImGui::BeginTable("##nodes", 4,
                                  ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Break", ImGuiTableColumnFlags_WidthFixed,
                                        scaled(44.0f));
                ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthStretch, 0.2f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableHeadersRow();
                const std::string ownerId = objs[flowGraphObject_].id;
                for (const FlowNode& n : objs[flowGraphObject_].flowGraph.nodes) {
                    const FlowNodeType* t = flowNodeType(n.type);
                    if (!t || (t->pure && !t->trigger)) continue;  // data node
                    const int key = dbgKeyFor(project_.activeScene, ownerId, n.id);
                    ImGui::TableNextRow();
                    ImGui::PushID(n.id);
                    ImGui::TableSetColumnIndex(0);
                    bool bp = dbgHasBreakpoint(ownerId, n.id);
                    if (ImGui::Checkbox("##bp", &bp))
                        dbgToggleBreakpoint(ownerId, n.id);
                    ImGui::TableSetColumnIndex(1);
                    const float age = dbgNodeHeat(key);
                    if (age < 0.35f)
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s",
                                           t->title);
                    else
                        ImGui::TextUnformatted(t->title);
                    ImGui::TableSetColumnIndex(2);
                    if (key >= 0 && key < (int)dbgSnap_.hits.size())
                        ImGui::Text("%u", dbgSnap_.hits[key]);
                    else
                        ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(3);
                    const int armed = dbgTimerFrames(key);
                    if (t->trigger && key >= 0) {
                        ImGui::BeginDisabled(!live);
                        if (ImGui::SmallButton("Fire"))
                            dbgFireNode(key, ImGui::GetIO().KeyShift);
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "Runs everything wired to this trigger in the "
                                "running game, once.\n"
                                "Shift+click = fire AND "
                                "continue, so a Delay in the branch gets frames "
                                "to finish.");
                    } else if (armed > 0) {
                        // An armed countdown is the usual reason a branch looks
                        // dead: show it counting instead of leaving a blank.
                        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
                                           "%d f (%.1fs)", armed,
                                           armed / (dbgFps_ > 1.0f ? dbgFps_ : 50.0f));
                    } else if (age < FLT_MAX) {
                        ImGui::TextDisabled("%.1fs ago", age);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndTabItem();
    }

    // Stats: the frame's vital signs. Every number here already existed
    // somewhere the developer could not see it - the engine's own counters, the
    // VU1 tap's per-flush accounting, the scene's object list - and the whole
    // feature is carrying them across the same channel as everything else.
    if (ImGui::BeginTabItem("Stats")) {
        const livedbg::Stats& st = dbgSnap_.stats;
        if (!live || !st.valid) {
            ImGui::TextDisabled("No stats yet.");
            prefHelp(
                "They arrive with the game's snapshots - run a debug build with\n"
                "the Live Debugger on. A game built before this panel existed\n"
                "reports none: rebuild it.");
        } else {
            ImGui::SeparatorText("Frame");
            ImGui::Text("%d FPS", st.fps);
            ImGui::SameLine();
            ImGui::TextDisabled("|  %d bag flush(es) to VU1, %u quadwords, "
                                "%u vertices",
                                st.flushes, st.qw, st.verts);
            if (st.maxChunkVerts) {
                ImGui::TextDisabled("Largest single stream: %d vertices",
                                    st.maxChunkVerts);
                prefHelp(
                    "That IS the VU1 buffer's capacity for this vertex layout,\n"
                    "and the size a mesh gets cut into.");
            }

            ImGui::SeparatorText("GS VRAM");
            const float freeMB = st.vramFreeKB / 1024.0f;
            const float lowMB = st.vramMinFreeKB / 1024.0f;
            ImGui::Text("%.2f MB free, largest block %u KB", freeMB,
                        st.vramLargestKB);
            // 4 MB total on the console; the bar is against what is left, which
            // is what actually runs out.
            ImGui::ProgressBar(ImClamp(freeMB / 4.0f, 0.0f, 1.0f),
                               ImVec2(-FLT_MIN, 0), "");
            ImGui::TextDisabled("low-water mark %.2f MB   |   %d textures "
                                "resident (peak %d)",
                                lowMB, st.vramResident, st.vramPeak);
            ImGui::TextDisabled("%u binds, %u hits, %u uploads, %u evictions "
                                "(cumulative)",
                                st.vramBinds, st.vramHits, st.vramUploads,
                                st.vramEvictions);
            if (st.vramEvictions)
                ImGui::TextColored(ImVec4(0.94f, 0.75f, 0.35f, 1.0f),
                                   "Evictions happen: the working set does not "
                                   "fit, so textures are re-uploaded.");

            ImGui::SeparatorText("EE memory");
            if (st.ramFreeKB)
                ImGui::Text("%.2f MB free at frame %u", st.ramFreeKB / 1024.0f,
                            st.ramFrame);
            else
                ImGui::TextDisabled("not measured yet");
            ImGui::BeginDisabled(!live);
            if (ImGui::Button("Measure now")) {
                dbgCmd_.measureRam = true;
                dbgCmdWritten_ = false;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "On request only. The engine measures free RAM by "
                    "allocating every free block\nuntil malloc fails and then "
                    "freeing the chain - honest, but not something to\nrun once "
                    "a frame.");

            ImGui::SeparatorText("Scene");
            ImGui::Text("%d objects: %d active, %d visible", st.objects,
                        st.objActive, st.objVisible);
        }
        ImGui::EndTabItem();
    }

    // VU: what the EE actually fed VU1 for one draw - the DMA chain decoded,
    // and the vertex stream it carried drawn as a wireframe. VU1 debugging is
    // otherwise blind (no printf, output straight to the GS), and this is the
    // half we own (docs/devkit.md).
    if (ImGui::BeginTabItem("VU")) {
        if (!live) dbgVuCapWaiting_ = false;  // nobody left to answer
        ImGui::BeginDisabled(!live);
        if (ImGui::Button("Capture VU1 packet")) {
            dbgCmd_.captureVu = true;
            dbgCmd_.vuFlush = dbgVuPinFlush_ ? dbgVuFlushWanted_ : -1;
            dbgCmdWritten_ = false;
            dbgVuCapWaiting_ = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Asks the game for one DMA chain it sends to VU1: the scales, "
                "the\nGIF tag, the matrices and the vertex stream, exactly as "
                "packed.\nA frame sends one chain per bag flush - unpinned, "
                "each click\nwalks to the next one.");
        // A frame sends the same draws in the same order, so "the next packet"
        // forever means the same picture forever. Unpinned the game advances one
        // flush per click; pinned it re-grabs the same draw, which is what you
        // want when watching ONE thing change over time.
        ImGui::SameLine();
        ImGui::Checkbox("pin flush", &dbgVuPinFlush_);
        if (dbgVuPinFlush_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(90.0f));
            const int maxFlush =
                dbgVuCap_.flushCount > 0 ? dbgVuCap_.flushCount - 1 : 63;
            ImGui::SliderInt("##vuflush", &dbgVuFlushWanted_, 0, maxFlush,
                             "flush %d");
        }
        // Two captures of the same draw look alike; say plainly whether what is
        // on screen is the answer to the last click or still the previous one.
        if (dbgVuCapWaiting_) {
            ImGui::SameLine();
            ImGui::TextDisabled("waiting for the game...");
        }

        // The flush map: every draw of the last complete frame, so finding the
        // one you care about is reading a table instead of clicking through
        // dozens of captures. Click a row to capture THAT draw.
        if (!dbgSnap_.flushes.empty() &&
            ImGui::CollapsingHeader("Flush map - the frame's draws")) {
            uint32_t totalV = 0;
            for (const livedbg::FlushInfo& f : dbgSnap_.flushes)
                totalV += (uint32_t)f.verts;
            ImGui::TextDisabled(
                "%d flush(es), %u vertices in the last complete frame. Click a "
                "row to capture it.",
                (int)dbgSnap_.flushes.size(), totalV);
            const float h = scaled(150.0f);
            if (ImGui::BeginTable("##flushmap", 5,
                                  ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_ScrollY,
                                  ImVec2(0, h))) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthStretch, 0.12f);
                ImGui::TableSetupColumn("verts", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("qw", ImGuiTableColumnFlags_WidthStretch, 0.18f);
                ImGui::TableSetupColumn("unpacks", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("program", ImGuiTableColumnFlags_WidthStretch, 0.26f);
                ImGui::TableHeadersRow();
                int biggest = 0;
                for (const livedbg::FlushInfo& f : dbgSnap_.flushes)
                    biggest = ImMax(biggest, f.verts);
                for (size_t i = 0; i < dbgSnap_.flushes.size(); ++i) {
                    const livedbg::FlushInfo& f = dbgSnap_.flushes[i];
                    ImGui::TableNextRow();
                    ImGui::PushID((int)i);
                    ImGui::TableSetColumnIndex(0);
                    char label[24];  // fits any 64-bit size_t
                    std::snprintf(label, sizeof(label), "%zu", i);
                    const bool pinnedHere =
                        dbgVuPinFlush_ && dbgVuFlushWanted_ == (int)i;
                    if (ImGui::Selectable(label, pinnedHere,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        dbgVuPinFlush_ = true;
                        dbgVuFlushWanted_ = (int)i;
                        dbgCmd_.captureVu = true;
                        dbgCmd_.vuFlush = (int)i;
                        dbgCmdWritten_ = false;
                        dbgVuCapWaiting_ = true;
                    }
                    ImGui::TableSetColumnIndex(1);
                    // The fattest draws are the ones worth looking at first.
                    if (biggest > 0 && f.verts >= biggest / 2)
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%d",
                                           f.verts);
                    else
                        ImGui::Text("%d", f.verts);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", f.qw);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", f.unpacks);
                    ImGui::TableSetColumnIndex(4);
                    if (f.program)
                        ImGui::Text("@%d", f.program);
                    else
                        ImGui::TextDisabled("carried over");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        if (!dbgVuCap_.loaded) {
            ImGui::TextWrapped(
                "No capture yet. %s",
                dbgVuCap_.error.empty() ? "" : dbgVuCap_.error.c_str());
        } else {
            if (dbgVuCap_.flushIndex >= 0)
                ImGui::Text("frame %u  |  flush %d of %d  |  %d quadwords  |  "
                            "%d mesh(es)",
                            dbgVuCap_.frame, dbgVuCap_.flushIndex,
                            dbgVuCap_.flushCount, dbgVuCap_.qw,
                            (int)dbgVuCap_.meshes.size());
            else
                ImGui::Text("frame %u  |  %d quadwords  |  %d unpack(s)  |  "
                            "%d mesh(es)",
                            dbgVuCap_.frame, dbgVuCap_.qw,
                            (int)dbgVuCap_.unpacks.size(),
                            (int)dbgVuCap_.meshes.size());
            {
                std::string progs;
                for (size_t i = 0; i < dbgVuCap_.mscal.size(); ++i)
                    progs += (i ? ", " : "") + std::to_string(dbgVuCap_.mscal[i]);
                std::string res;
                if (dbgVuCap_.renderWidth > 0)
                    res = "  |  rendering at " +
                          std::to_string(dbgVuCap_.renderWidth) + "x" +
                          std::to_string(dbgVuCap_.renderHeight);
                ImGui::TextDisabled("microprogram entry: %s%s",
                                    progs.empty() ? "-" : progs.c_str(),
                                    res.c_str());
            }

            dbgVuDrawFindings(dbgVuCap_);

            // One flush is a whole bag: a dozen meshes, each with its own
            // position stream. Pick which one the preview draws - they cannot
            // be drawn together, every mesh being in its OWN model space.
            const int meshes = (int)dbgVuCap_.meshes.size();
            dbgVuMesh_ = meshes ? ImClamp(dbgVuMesh_, 0, meshes - 1) : 0;
            if (meshes > 1) {
                ImGui::SetNextItemWidth(scaled(180.0f));
                ImGui::SliderInt("##vumesh", &dbgVuMesh_, 0, meshes - 1,
                                 "mesh %d");
                ImGui::SameLine();
                const vucap::Mesh& m = dbgVuCap_.meshes[dbgVuMesh_];
                std::string tail;
                // A chain that only ever says MSCNT re-runs the program the
                // PREVIOUS chain loaded - it is not in this capture at all.
                tail += m.program >= 0
                            ? "  program @" + std::to_string(m.program)
                            : std::string("  program carried over (MSCNT)");
                if (m.degenerate)
                    tail += "  " + std::to_string(m.degenerate) + " degenerate";
                ImGui::TextDisabled(
                    "of %d - %d verts, %d tris, %.1f units across%s", meshes,
                    m.verts, m.tris, m.extent(), tail.c_str());
            }

            // The wireframe: the captured positions are model space, so this is
            // its own little orbit view rather than an overlay on the scene.
            const std::vector<float>* verts =
                meshes ? dbgVuCap_.verticesOf(dbgVuMesh_) : dbgVuCap_.vertices();
            if (verts && verts->size() >= 12) {
                const float h = scaled(220.0f);
                const ImVec2 size(ImGui::GetContentRegionAvail().x, h);
                const ImVec2 org = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##vuview", size);
                if (ImGui::IsItemActive()) {
                    dbgVuYaw_ += ImGui::GetIO().MouseDelta.x * 0.01f;
                    dbgVuPitch_ += ImGui::GetIO().MouseDelta.y * 0.01f;
                }
                if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
                    dbgVuZoom_ = ImClamp(
                        dbgVuZoom_ * (1.0f + ImGui::GetIO().MouseWheel * 0.1f),
                        0.1f, 20.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(org, ImVec2(org.x + size.x, org.y + size.y),
                                  IM_COL32(18, 20, 24, 255), scaled(3.0f));
                // Fit the soup in the box, then spin it with the mouse.
                float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
                float hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
                const size_t n = verts->size() / 4;
                for (size_t i = 0; i < n; ++i)
                    for (int a = 0; a < 3; ++a) {
                        lo[a] = ImMin(lo[a], (*verts)[i * 4 + a]);
                        hi[a] = ImMax(hi[a], (*verts)[i * 4 + a]);
                    }
                const float cx = (lo[0] + hi[0]) * 0.5f;
                const float cy = (lo[1] + hi[1]) * 0.5f;
                const float cz = (lo[2] + hi[2]) * 0.5f;
                float extent = 0.001f;
                for (int a = 0; a < 3; ++a) extent = ImMax(extent, hi[a] - lo[a]);
                const float scale = (ImMin(size.x, size.y) * 0.38f) / extent *
                                    dbgVuZoom_;
                const float cyaw = cosf(dbgVuYaw_), syaw = sinf(dbgVuYaw_);
                const float cp = cosf(dbgVuPitch_), sp = sinf(dbgVuPitch_);
                auto project = [&](size_t vi) {
                    const float x = (*verts)[vi * 4 + 0] - cx;
                    const float y = (*verts)[vi * 4 + 1] - cy;
                    const float z = (*verts)[vi * 4 + 2] - cz;
                    const float rx = x * cyaw + z * syaw;
                    const float rz = -x * syaw + z * cyaw;
                    const float ry = y * cp - rz * sp;
                    return ImVec2(org.x + size.x * 0.5f + rx * scale,
                                  org.y + size.y * 0.5f - ry * scale);
                };
                dl->PushClipRect(org, ImVec2(org.x + size.x, org.y + size.y), true);
                const size_t tris = n / 3;
                for (size_t t = 0; t < tris && t < 4000; ++t) {
                    const ImVec2 a = project(t * 3 + 0);
                    const ImVec2 b = project(t * 3 + 1);
                    const ImVec2 c = project(t * 3 + 2);
                    const ImU32 col = IM_COL32(120, 210, 255, 150);
                    dl->AddLine(a, b, col);
                    dl->AddLine(b, c, col);
                    dl->AddLine(c, a, col);
                }
                dl->PopClipRect();
                ImGui::TextDisabled(
                    "%zu vertices, %zu triangles - drag to orbit, wheel to zoom. "
                    "Model space, as packed.",
                    n, tris);
            }

            // What the microprogram STAGED for the GS: the GIF packets it built
            // in VU1 memory, decoded to screen-space vertices.
            if (dbgVuCap_.hasVuMem) {
                ImGui::Separator();
                ImGui::Text("VU1 memory captured - %d GIF packet(s), %d GS vertices",
                            (int)dbgVuCap_.gifs.size(), dbgVuCap_.outputVerts());
                if (dbgVuCap_.hasMvp)
                    ImGui::TextDisabled(
                        "MVP in VU1: [%.3f %.3f %.3f %.3f] ... (quadword 0)",
                        dbgVuCap_.mvp[0], dbgVuCap_.mvp[4], dbgVuCap_.mvp[8],
                        dbgVuCap_.mvp[12]);
                for (size_t i = 0; i < dbgVuCap_.gifs.size(); ++i) {
                    const vucap::GifPacket& g = dbgVuCap_.gifs[i];
                    if (!g.hasGeometry) continue;
                    if (!ImGui::TreeNode(
                            (void*)(intptr_t)i, "@VU1 %d  %s  %d verts  [%s]%s",
                            g.vuAddr, g.primName().c_str(), (int)g.verts.size(),
                            g.regs.c_str(), g.eop ? "  EOP" : ""))
                        continue;
                    for (size_t v = 0; v < g.verts.size() && v < 24; ++v) {
                        const vucap::GsVertex& gv = g.verts[v];
                        ImGui::Text("v%-3zu  x %8.1f  y %8.1f  z %8u  rgba %3u %3u %3u %3u",
                                    v, gv.px(), gv.py(), gv.z, gv.r, gv.g, gv.b,
                                    gv.a);
                    }
                    if (g.verts.size() > 24) ImGui::TextDisabled("...");
                    ImGui::TreePop();
                }
                if (dbgVuCap_.diffCompared) {
                    ImGui::TextDisabled(
                        "Host reference (diagnostic): max dx %.1f, dy %.1f over %d "
                        "vertices, 12.4 units.",
                        dbgVuCap_.diffMaxX, dbgVuCap_.diffMaxY,
                        dbgVuCap_.diffCompared);
                    prefHelp(
                        "One flush can carry several meshes and VU1 memory holds\n"
                        "only the LAST MVP, so this comparison is exact only for\n"
                        "a single-mesh flush - read it as a hint, not a verdict\n"
                        "(docs/devkit.md).");
                }
            }

            // The chain itself, decoded.
            if (ImGui::BeginChild("##vusteps", ImVec2(0, scaled(160.0f)), true)) {
                for (const vucap::Step& st : dbgVuCap_.steps)
                    ImGui::TextUnformatted(st.text.c_str());
            }
            ImGui::EndChild();
        }
        ImGui::EndTabItem();
    }

    // Objects: what the game's own RuntimeObject holds for a watched object,
    // sampled every frame - live values plus a curve per axis. This is the
    // answer to "where is it ACTUALLY, and what did it do a second ago".
    if (ImGui::BeginTabItem("Objects")) {
        const auto& objs = project_.objects();
        ImGui::BeginDisabled(selectedObject_ < 0 ||
                             selectedObject_ >= (int)objs.size() ||
                             ((int)dbgObjWatch_.size() >= livedbg::kMaxWatchObjects &&
                              !dbgIsWatched(selectedObject_)));
        if (ImGui::SmallButton(dbgIsWatched(selectedObject_) ? "- Unwatch selected"
                                                            : "+ Watch selected"))
            dbgToggleObjectWatch(selectedObject_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Trails in viewport", &dbgShowTrails_);
        ImGui::SameLine();
        ImGui::TextDisabled("%d/%d", (int)dbgObjWatch_.size(),
                            livedbg::kMaxWatchObjects);
        if (dbgObjWatch_.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Nothing watched yet.");
            prefHelp(
                "Select an object and click \"+ Watch selected\" above. The game\n"
                "then reports that object's position, rotation, scale, color and\n"
                "flags EVERY frame, and the path it takes is drawn in the\n"
                "viewport.");
        }
        for (size_t i = 0; i < dbgObjWatch_.size(); ++i) {
            DbgObjTrack& t = dbgObjWatch_[i];
            ImGui::PushID((int)i);
            ImGui::Separator();
            const std::string label =
                (t.index >= 0 && t.index < (int)objs.size() ? objs[t.index].name
                                                            : t.name) +
                "  (#" + std::to_string(t.index) + ")";
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - scaled(26.0f));
            if (ImGui::SmallButton("x")) {
                dbgToggleObjectWatch(t.index);
                ImGui::PopID();
                break;
            }
            if (t.samples.empty()) {
                ImGui::TextDisabled("waiting for samples...");
                ImGui::PopID();
                continue;
            }
            const livedbg::ObjSample& s = t.samples.back();
            ImGui::Text("pos %.2f, %.2f, %.2f", s.pos[0], s.pos[1], s.pos[2]);
            ImGui::Text("rot %.1f, %.1f, %.1f   scale %.2f, %.2f, %.2f",
                        s.rot[0], s.rot[1], s.rot[2], s.scale[0], s.scale[1],
                        s.scale[2]);
            ImGui::Text("color %.2f, %.2f, %.2f", s.color[0], s.color[1],
                        s.color[2]);
            ImGui::SameLine();
            ImGui::TextColored(s.visible ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                                         : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               s.visible ? "visible" : "hidden");
            if (!s.active) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.4f, 1.0f), "inactive");
            }
            // One plot per axis, over the whole kept history - ImGui's own
            // PlotLines is enough and costs nothing to maintain.
            const int n = (int)t.samples.size();
            static std::vector<float> series;
            for (int axis = 0; axis < 3; ++axis) {
                series.resize(n);
                float lo = FLT_MAX, hi = -FLT_MAX;
                for (int k = 0; k < n; ++k) {
                    series[k] = t.samples[k].pos[axis];
                    lo = ImMin(lo, series[k]);
                    hi = ImMax(hi, series[k]);
                }
                if (hi - lo < 0.01f) {  // a flat line still deserves a scale
                    const float mid = (hi + lo) * 0.5f;
                    lo = mid - 0.5f;
                    hi = mid + 0.5f;
                }
                char overlay[48];
                std::snprintf(overlay, sizeof(overlay), "%c  %.2f .. %.2f",
                              "XYZ"[axis], lo, hi);
                // Per-axis id: three plots inside one PushID scope with the
                // same label are three widgets claiming one ID, which ImGui
                // reports as a conflict (and makes tooltips/hover ambiguous).
                const char* plotId = axis == 0 ? "##plotX"
                                     : axis == 1 ? "##plotY"
                                                 : "##plotZ";
                ImGui::PlotLines(plotId, series.data(), n, 0, overlay, lo, hi,
                                 ImVec2(-FLT_MIN, scaled(38.0f)));
            }
            ImGui::TextDisabled("%d samples (%.1fs of history)", n,
                                n / (dbgFps_ > 1.0f ? dbgFps_ : 50.0f));
            ImGui::PopID();
        }
        ImGui::EndTabItem();
    }

    // Logic: what is currently hot-patched into the running game, and what
    // still needs a rebuild (docs/live-logic.md).
    if (ImGui::BeginTabItem("Logic")) {
        if (!project_.settings.liveLogic) {
            ImGui::TextWrapped(
                "Live Logic is off for this project - editing a graph needs a "
                "rebuild.");
            if (ImGui::Checkbox("Live Logic (project setting)",
                                &project_.settings.liveLogic))
                commitChange();
        } else {
            switch (liveLogicState_) {
                case LogicState::Off:
                    ImGui::TextDisabled(
                        "Release profile - graphs run as compiled C++ only.");
                    break;
                case LogicState::NoBuild:
                    ImGui::TextDisabled("No built-graph list yet.");
                    prefHelp(
                        "Build & Run (F5) once; after that, editing a graph takes\n"
                        "effect in the running game without another build.");
                    break;
                case LogicState::InSync:
                    ImGui::TextColored(ImVec4(0.6f, 0.75f, 0.6f, 1.0f),
                                       "In sync with the build - nothing to "
                                       "patch.");
                    ImGui::TextWrapped(
                        "Edit any graph and it is compiled and streamed into the "
                        "running game within a fraction of a second.");
                    break;
                case LogicState::Patched:
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f),
                                       "%d graph(s) running from the editor's "
                                       "patch",
                                       liveLogicPatchCount_);
                    ImGui::TextWrapped(
                        "Their compiled C++ stands down while the patch is "
                        "live; a rebuild folds the edits back into native "
                        "code.");
                    break;
                case LogicState::Blocked:
                    ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
                                       "Rebuild needed");
                    break;
            }
            if (!liveLogicBlocked_.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped(
                    "These edited graphs cannot be hot-patched - the "
                    "interpreter has no instruction for what they use:");
                for (const auto& e : liveLogicBlocked_) {
                    ImGui::Bullet();
                    ImGui::TextWrapped("%s - %s", e.first.c_str(),
                                       e.second.c_str());
                }
                ImGui::TextDisabled(
                    "Build & Run (F5) and everything is native again.");
            }
        }
        ImGui::EndTabItem();
    }

    // Breakpoints: the whole project's list, resolvable to names and clearable.
    if (ImGui::BeginTabItem("Breakpoints")) {
        if (project_.debugBreakpoints.empty()) {
            ImGui::TextWrapped(
                "No breakpoints. Right-click a node in the Flow Graph (or use "
                "the Nodes tab) to stop the game the moment it runs.");
        } else {
            if (ImGui::SmallButton("Clear all")) {
                project_.debugBreakpoints.clear();
                dbgCmdWritten_ = false;
                setDirty(true);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d of %d slots",
                                (int)project_.debugBreakpoints.size(),
                                livedbg::kMaxBreakpoints);
            ImGui::Separator();
            for (size_t i = 0; i < project_.debugBreakpoints.size(); ++i) {
                const std::string& b = project_.debugBreakpoints[i];
                const size_t colon = b.rfind(':');
                if (colon == std::string::npos) continue;
                const std::string objId = b.substr(0, colon);
                const int nodeId = std::atoi(b.c_str() + colon + 1);
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("x")) {
                    project_.debugBreakpoints.erase(
                        project_.debugBreakpoints.begin() + i);
                    dbgCmdWritten_ = false;
                    setDirty(true);
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                bool named = false;
                for (const livedbg::NodeSym& n : dbgSyms_.nodes)
                    if (n.objectId == objId && n.nodeId == nodeId) {
                        if (ImGui::Selectable(nodeLabel(n).c_str())) revealNode(n);
                        named = true;
                        break;
                    }
                if (!named)
                    ImGui::TextDisabled("node %d of %s (not in this build)",
                                        nodeId, objId.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}

void App::pollGameError() {
    if (!hasProject_) return;
    const double now = ImGui::GetTime();
    if (now < errorNextPoll_) return;
    errorNextPoll_ = now + 0.5;  // a log tail twice a second is plenty

    // Detect a fresh run / a cleared log by a shrinking source. The Runner
    // deletes bin/log.txt before each launch, and Output "Clear" empties the
    // runner log - either shrinking means the previously seen dump is gone, so
    // forget it and let an identical new error (same missing file, same line)
    // pop again instead of being deduped against the stale text.
    const size_t gsz =
        fileSizeOr0((std::filesystem::path(project_.dir) / "bin" / "log.txt").string());
    const size_t rsz = runner_.log().size();
    if (gsz < errorGameLogSize_ || rsz < errorRunnerLogSize_) errorSeenSig_.clear();
    errorGameLogSize_ = gsz;
    errorRunnerLogSize_ = rsz;

    const std::string block = latestGameAssert();
    if (block.empty() || block == errorSeenSig_) return;
    errorSeenSig_ = block;  // handled once, whether or not we pop
    if (errorPopupEnabled_) {
        errorModalText_ = block;
        openErrorPopup_ = true;
        // The game window (PCSX2) has the foreground when an error fires, so the
        // dialog would open behind it unnoticed. Flash the taskbar entry and
        // pull the editor forward so the error actually gets the user's
        // attention. (requestAttention flashes even if focusWindow is refused.)
        if (window_) {
            glfwRequestWindowAttention(window_);
            glfwFocusWindow(window_);
        }
    }
}

void App::drawErrorModal() {
    if (openErrorPopup_) {
        ImGui::OpenPopup("Game error");
        openErrorPopup_ = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(640), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Game error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    // The engine tags non-fatal errors (a recovered asset load) with this
    // header; everything else is a fatal assertion that stopped the game.
    const bool fatal = errorModalText_.find("Non-fatal") == std::string::npos;
    if (fatal) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "The game hit an assertion and stopped.");
        ImGui::TextDisabled(
            "The same dump is in the Debug window (Game log). Fix the cause and\n"
            "run again - a common one is a missing or non-power-of-two texture.");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                           "The game reported an error but kept running.");
        ImGui::TextDisabled(
            "A missing asset was skipped - a placeholder (magenta checkerboard)\n"
            "texture, or no sound. Fix it and rebuild. The same dump is in the\n"
            "Debug window (Game log).");
    }
    ImGui::Separator();

    // Read-only but fully selectable, so the whole dump can be copied.
    ImGui::InputTextMultiline("##errtext", const_cast<char*>(errorModalText_.c_str()),
                              errorModalText_.size() + 1,
                              ImVec2(scaled(620), scaled(240)),
                              ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Copy", ImVec2(scaled(120), 0)))
        ImGui::SetClipboardText(errorModalText_.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();

    // The off switch, right where the noise is (mirrors the Debug window
    // checkbox; both persist to editor.ini).
    bool consoleOnly = !errorPopupEnabled_;
    if (ImGui::Checkbox("Only log to console (don't pop up on errors)", &consoleOnly)) {
        errorPopupEnabled_ = !consoleOnly;
        saveGlobalConfig();
    }
    ImGui::EndPopup();
}

// -------------------------------------------------------------------------
// UI scripting (docs/ui-scripting.md) - driving the EDITOR without a human.
//
// Same idea as the Remote Pad one file over: instead of asking the window
// manager to focus a window and synthesising OS clicks at guessed pixel
// coordinates, we go through what the application already owns - ImGui's item
// bookkeeping (which widget is where, by name: src/uiscript.cpp) and ImGui's own
// event queue. So a script says `click "Remote Pad/Cross"`, nothing reaches the
// OS, no window needs the focus, and the same script works at any DPI or UI
// scale.
//
// The tick runs once per frame immediately BEFORE ImGui::NewFrame(): it reads
// the item map the LAST frame produced (so a target only becomes clickable a
// frame after the UI that owns it was drawn - which is exactly why every step
// that needs a target WAITS for it instead of sleeping a guessed interval), then
// queues this frame's input. Injecting after the GLFW backend's NewFrame is what
// makes our position win: the backend only feeds the real cursor when the window
// is focused, and a later event in the queue overrides an earlier one anyway.
// -------------------------------------------------------------------------

void App::setUiScript(const std::vector<uiscript::Step>& steps) {
    uiScript_ = steps;
    uiScriptActive_ = !steps.empty();
    uiStepIndex_ = 0;
    uiStepPhase_ = 0;
}

void App::uiScriptTick() {
    if (!uiScriptActive_) return;
    ImGuiIO& io = ImGui::GetIO();
    // glfwGetTime, not ImGui::GetTime: this runs BEFORE NewFrame, so ImGui's
    // clock is still last frame's - and it is exactly 0.0 on the first frame,
    // which is also our "this step has not started yet" sentinel.
    const double now = glfwGetTime();

    auto finishStep = [&]() {
        uiStepIndex_++;
        uiStepPhase_ = 0;
        uiStepStarted_ = 0.0;
    };
    auto failStep = [&](const uiscript::Step& s, const std::string& why) {
        std::printf("[ui] FAILED: %s\n     %s\n", s.source.c_str(), why.c_str());
        std::fflush(stdout);
        uiScriptFailed_ = true;
        uiScriptActive_ = false;
        exitConfirmed_ = true;  // never leave an unattended run sitting on a modal
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    };
    // What a failed lookup should say: the labels that ARE there. Guessing from
    // a blank "not found" is the single most expensive part of UI scripting.
    auto candidates = [&](const std::string& target) {
        std::string wantWindow;
        const size_t slash = target.find('/');
        if (slash != std::string::npos) wantWindow = target.substr(0, slash);
        std::string out;
        int n = 0;
        for (const uiscript::Item& it : uiscript::items()) {
            if (it.label.empty()) continue;
            if (!wantWindow.empty() && it.window.rfind(wantWindow, 0) != 0) continue;
            if (n++ >= 12) {
                out += ", ...";
                break;
            }
            out += (n > 1 ? ", " : "") + it.window + "/" + it.label;
        }
        if (out.empty()) out = "(nothing on screen yet)";
        return "on screen now: " + out;
    };

    if (uiStepIndex_ >= uiScript_.size()) {
        uiScriptActive_ = false;
        uiscript::beginFrame();
        return;
    }
    const uiscript::Step& s = uiScript_[uiStepIndex_];
    if (uiStepStarted_ == 0.0) {
        uiStepStarted_ = now;
        std::printf("[ui] %s\n", s.source.c_str());
        std::fflush(stdout);
    }

    // Steps that need a widget resolve it first, and wait for it to appear. The
    // timeout is generous: opening a project or a bake can hold a frame for a
    // while, and a false failure is worse than a slow pass.
    const bool needsTarget =
        s.kind == uiscript::Step::Click || s.kind == uiscript::Step::DoubleClick ||
        s.kind == uiscript::Step::RightClick ||
        s.kind == uiscript::Step::HoldClick || s.kind == uiscript::Step::Hover ||
        s.kind == uiscript::Step::Drag || s.kind == uiscript::Step::Wheel ||
        s.kind == uiscript::Step::Expect;
    if (needsTarget && uiStepPhase_ == 0) {
        // Everything here but Expect ends in a mouse CLICK, so exclude the
        // whole-window items (see uiscript::find). Wheel is the exception: what
        // it usually aims at is a canvas that submits no item of its own, and
        // the middle of the window is exactly the right place to scroll.
        const bool clickable =
            s.kind != uiscript::Step::Expect && s.kind != uiscript::Step::Wheel;
        const uiscript::Item* it = uiscript::find(s.arg, clickable);
        if (!it) {
            if (now - uiStepStarted_ > 5.0)
                failStep(s, "no widget matching '" + s.arg + "'; " +
                                candidates(s.arg));
            uiscript::beginFrame();
            return;  // try again next frame
        }
        uiTargetX_ = (it->x0 + it->x1) * 0.5f;
        uiTargetY_ = (it->y0 + it->y1) * 0.5f;
        if (s.kind == uiscript::Step::Expect) {
            finishStep();
            uiscript::beginFrame();
            return;
        }
        // Hover first, press next frame: that is what a real cursor does, and
        // some widgets (drag sources, menus) only arm once hovered.
        io.AddMousePosEvent(uiTargetX_, uiTargetY_);
        uiStepPhase_ = 1;
        uiscript::beginFrame();
        return;
    }

    switch (s.kind) {
        case uiscript::Step::Expect:  // handled above
            finishStep();
            break;
        case uiscript::Step::ExpectNot: {
            const uiscript::Item* it = uiscript::find(s.arg);
            if (it)
                failStep(s, "'" + s.arg + "' is on screen and should not be");
            else
                finishStep();
            break;
        }
        case uiscript::Step::ExpectChecked:
        case uiscript::Step::ExpectUnchecked: {
            // ImGui reports a checkbox's / menu item's tick through the same
            // hook the boxes come from, so a script can assert STATE instead of
            // a human reading it off a screenshot.
            const uiscript::Item* it = uiscript::find(s.arg);
            const bool want = s.kind == uiscript::Step::ExpectChecked;
            if (!it) {
                if (now - uiStepStarted_ > 5.0)
                    failStep(s, "no widget matching '" + s.arg + "'; " +
                                    candidates(s.arg));
            } else if (!it->checkable) {
                failStep(s, "'" + s.arg + "' is not a checkable widget");
            } else if (it->checked != want) {
                failStep(s, std::string("'") + s.arg + "' is " +
                                (it->checked ? "checked" : "unchecked") +
                                " and should be " + (want ? "checked" : "unchecked"));
            } else {
                finishStep();
            }
            break;
        }
        case uiscript::Step::Hover:
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            if (uiStepPhase_++ >= 2) finishStep();
            break;
        case uiscript::Step::Click:
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            if (uiStepPhase_ == 1) {
                io.AddMouseButtonEvent(0, true);
                uiStepPhase_ = 2;
            } else if (uiStepPhase_ == 2) {
                io.AddMouseButtonEvent(0, false);
                uiStepPhase_ = 3;
            } else {
                finishStep();  // one settle frame, so the UI has reacted
            }
            break;
        case uiscript::Step::RightClick:
            // The context-menu button. Same shape as Click on the OTHER index -
            // which is the whole point: a right-click menu used to be the one
            // part of the editor a script could not reach, so nothing that hangs
            // off one (the flow and procedural canvases, the object list) could
            // be asserted without a human.
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            if (uiStepPhase_ == 1) {
                io.AddMouseButtonEvent(1, true);
                uiStepPhase_ = 2;
            } else if (uiStepPhase_ == 2) {
                io.AddMouseButtonEvent(1, false);
                uiStepPhase_ = 3;
            } else {
                finishStep();
            }
            break;
        case uiscript::Step::DoubleClick:
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            if (uiStepPhase_ == 1 || uiStepPhase_ == 3) {
                io.AddMouseButtonEvent(0, true);
                uiStepPhase_++;
            } else if (uiStepPhase_ == 2 || uiStepPhase_ == 4) {
                io.AddMouseButtonEvent(0, false);
                uiStepPhase_++;
            } else {
                finishStep();
            }
            break;
        case uiscript::Step::HoldClick:
            // Holding matters for anything that reads "is this being pressed
            // right now" rather than a click - the Remote Pad's buttons are the
            // reason this exists.
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            if (uiStepPhase_ == 1) {
                io.AddMouseButtonEvent(0, true);
                uiStepUntil_ = now + s.seconds;
                uiStepPhase_ = 2;
            } else if (uiStepPhase_ == 2) {
                if (now >= uiStepUntil_) {
                    io.AddMouseButtonEvent(0, false);
                    uiStepPhase_ = 3;
                }
            } else {
                finishStep();
            }
            break;
        case uiscript::Step::Drag: {
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            const int kSteps = 6;  // a slide, not a teleport: ImGui needs motion
            if (uiStepPhase_ == 1) {
                io.AddMouseButtonEvent(0, true);
                uiStepPhase_ = 2;
            } else if (uiStepPhase_ >= 2 && uiStepPhase_ < 2 + kSteps) {
                const float t = (float)(uiStepPhase_ - 1) / (float)kSteps;
                io.AddMousePosEvent(uiTargetX_ + s.dx * t, uiTargetY_ + s.dy * t);
                uiStepPhase_++;
            } else if (uiStepPhase_ == 2 + kSteps) {
                io.AddMousePosEvent(uiTargetX_ + s.dx, uiTargetY_ + s.dy);
                io.AddMouseButtonEvent(0, false);
                uiStepPhase_++;
            } else {
                finishStep();
            }
            break;
        }
        case uiscript::Step::Wheel: {
            // The cursor has to STAY on the target while the notches arrive: a
            // canvas zoom keeps the point under the mouse fixed, so a wheel
            // event at the wrong position also pans. One notch per frame, like a
            // real wheel - a whole turn in a single event is not what an
            // accumulating handler is written against.
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            const int notches = std::max(1, (int)std::lround(std::abs(s.dy)));
            if (uiStepPhase_ <= notches) {
                io.AddMouseWheelEvent(0.0f, s.dy < 0.0f ? -1.0f : 1.0f);
                uiStepPhase_++;
            } else {
                finishStep();
            }
            break;
        }
        case uiscript::Step::Key: {
            int key = 0;
            bool ctrl = false, shift = false, alt = false;
            if (!uiscript::parseChord(s.arg, key, ctrl, shift, alt)) {
                failStep(s, "cannot parse the key chord '" + s.arg + "'");
                break;
            }
            if (uiStepPhase_ == 0) {
                if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
                if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
                if (alt) io.AddKeyEvent(ImGuiMod_Alt, true);
                io.AddKeyEvent((ImGuiKey)key, true);
                uiStepPhase_ = 1;
            } else if (uiStepPhase_ == 1) {
                io.AddKeyEvent((ImGuiKey)key, false);
                if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
                if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
                if (alt) io.AddKeyEvent(ImGuiMod_Alt, false);
                uiStepPhase_ = 2;
            } else {
                finishStep();
            }
            break;
        }
        case uiscript::Step::Text:
            if (uiStepPhase_ == 0) {
                for (char c : s.arg)
                    io.AddInputCharacter((unsigned int)(unsigned char)c);
                uiStepPhase_ = 1;
            } else {
                finishStep();
            }
            break;
        case uiscript::Step::Wait:
            if (uiStepPhase_ == 0) {
                uiStepUntil_ = now + s.seconds;
                uiStepPhase_ = 1;
            } else if (now >= uiStepUntil_) {
                finishStep();
            }
            break;
        case uiscript::Step::Frames:
            if (uiStepPhase_ == 0) {
                uiStepFrames_ = s.n;
                uiStepPhase_ = 1;
            }
            if (--uiStepFrames_ <= 0) finishStep();
            break;
        case uiscript::Step::Shot:
            // The write happens after this frame renders (run() does it), so the
            // capture holds the UI the script has built up to here.
            uiShotPath_ = s.arg;
            finishStep();
            break;
        case uiscript::Step::Dump:
            std::printf("%s", uiscript::dumpText().c_str());
            std::fflush(stdout);
            finishStep();
            break;
        case uiscript::Step::Log:
            std::printf("[ui] %s\n", s.arg.c_str());
            std::fflush(stdout);
            finishStep();
            break;
        case uiscript::Step::Quit:
            std::printf("[ui] done - %zu step(s)%s\n", uiScript_.size(),
                        uiScriptFailed_ ? ", WITH FAILURES" : "");
            std::fflush(stdout);
            uiScriptActive_ = false;
            exitConfirmed_ = true;  // a script must not stop on a save prompt
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            break;
    }
    uiscript::beginFrame();
}

// -------------------------------------------------------------------------
// Remote Pad (docs/remote-pad.md) - the input direction of the host: channel.
//
// The editor holds the controller: while the window is open we rewrite
// bin/livepad.bin and the running game overlays it on the physical pad. The
// point is that NOTHING needs the keyboard focus - PCSX2 can sit behind the
// editor (or on the other monitor) and still be driven, which is what makes
// testing a pad-driven feature possible without a human in front of the
// emulator. The same file is what the --pad CLI writes from a script.
// -------------------------------------------------------------------------

void App::remotePadTick() {
    namespace fs = std::filesystem;
    const bool usable = hasProject_ && project_.settings.remotePad &&
                        project_.settings.buildProfile == "debug";
    // "Driving" means the window is open. Anything else (window closed, project
    // closed, preference off) has to let go explicitly - a game left holding a
    // direction because a panel was closed would look exactly like a stuck pad.
    const bool want = usable && showRemotePad_;
    if (!want) {
        if (padAttached_) {
            padState_.clear();
            padAttached_ = false;
            if (hasProject_)
                livepad::write(
                    (fs::path(project_.dir) / "bin" / "livepad.bin").string(),
                    padState_, ++padSeq_, false);
            padStatus_.clear();
        }
        return;
    }
    // Seed the sequence from the clock, for the same reason Live Link does: a
    // restarted editor must not reuse a number the still-running game already
    // applied, or its first state reads as "nothing changed".
    if (!padAttached_) {
        padSeq_ = (uint32_t)std::time(nullptr);
        padAttached_ = true;
    }
    const double now = ImGui::GetTime();
    if (now < padNextWrite_) return;
    padNextWrite_ = now + 0.04;  // ~25 Hz: keeps the game's stale watchdog fed
    padStatus_ = livepad::write(
        (fs::path(project_.dir) / "bin" / "livepad.bin").string(), padState_,
        ++padSeq_, true);
}

void App::drawRemotePadWindow() {
    if (!showRemotePad_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(470), scaled(400)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Remote Pad", &showRemotePad_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }
    if (project_.settings.buildProfile != "debug") {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "This project builds in the RELEASE profile.");
        ImGui::TextWrapped(
            "A release build carries no Remote Pad (the devkit's zero-cost "
            "rule), so nothing here reaches the game. Switch the profile in "
            "Project > Preferences > Build.");
        ImGui::End();
        return;
    }
    if (!project_.settings.remotePad) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "The \"Remote Pad\" preference is off.");
        ImGui::TextWrapped(
            "Turn it on in Project > Preferences > Build and rebuild - the "
            "poller is compiled into the game, so this needs a build, not just "
            "a save.");
        ImGui::End();
        return;
    }

    const int pi = padTarget_;
    livepad::State& st = padState_;
    const double now = ImGui::GetTime();
    // A click has to survive the trip: the state is snapshotted ~25 times a
    // second and the game reads whatever it finds, so a 20 ms click could fall
    // between two writes and never exist. Every press is therefore latched for
    // kMinHold, which is also long enough for the game to see it as a press AND
    // a release rather than a single ambiguous frame.
    const double kMinHold = 0.12;
    auto held = [&](const char* name, bool down) {
        const int idx = livepad::buttonByName(name);
        const uint32_t m = livepad::buttonBit(idx);
        if (idx < 0) return;
        if (down && (st.buttons[pi] & m) == 0) padLatch_[pi][idx] = now + kMinHold;
        if (down || now < padLatch_[pi][idx])
            st.buttons[pi] |= m;
        else
            st.buttons[pi] &= ~m;
    };
    // A button is HELD while its widget is held down: that is the whole point -
    // ImGui reports the mouse being down, we mirror it into the state and the
    // ticker announces it.
    auto padButton = [&](const char* label, const char* name, float w) {
        const uint32_t m = livepad::buttonBit(livepad::buttonByName(name));
        const bool on = (st.buttons[pi] & m) != 0;
        if (on)
            ImGui::PushStyleColor(
                ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::Button(label, ImVec2(w, scaled(26)));
        if (on) ImGui::PopStyleColor();
        held(name, ImGui::IsItemActive());
    };

    if (ImGui::RadioButton("Pad 1", padTarget_ == 0)) padTarget_ = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Pad 2", padTarget_ == 1)) padTarget_ = 1;
    ImGui::SameLine();
    prefHelp(
        "Pad 2 only does something in a multiplayer project (Project >\n"
        "Preferences > Multiplayer) - that is where the generated game opens\n"
        "the second connector. Start on pad 2 is what hot-joins player two.");
    ImGui::SameLine();
    if (ImGui::Button("Release all")) {
        padState_.clear();
        // The latches too, or a just-clicked button comes straight back.
        for (int p = 0; p < livepad::kPads; ++p)
            for (int b = 0; b < 16; ++b) padLatch_[p][b] = 0.0;
    }

    ImGui::Separator();

    const float bw = scaled(62);
    ImGui::BeginGroup();  // D-pad + left shoulders
    ImGui::Dummy(ImVec2(bw, 0));
    ImGui::SameLine();
    padButton("Up", "DpadUp", bw);
    padButton("Left", "DpadLeft", bw);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(bw, 0));
    ImGui::SameLine();
    padButton("Right", "DpadRight", bw);
    ImGui::Dummy(ImVec2(bw, 0));
    ImGui::SameLine();
    padButton("Down", "DpadDown", bw);
    ImGui::Spacing();
    padButton("L1", "L1", bw);
    ImGui::SameLine();
    padButton("L2", "L2", bw);
    padButton("L3", "L3", bw);
    ImGui::SameLine();
    padButton("Select", "Select", bw);
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, scaled(24));

    ImGui::BeginGroup();  // face buttons + right shoulders
    ImGui::Dummy(ImVec2(bw, 0));
    ImGui::SameLine();
    padButton("Tri", "Triangle", bw);
    padButton("Square", "Square", bw);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(bw, 0));
    ImGui::SameLine();
    padButton("Circle", "Circle", bw);
    ImGui::Dummy(ImVec2(bw, 0));
    ImGui::SameLine();
    padButton("Cross", "Cross", bw);
    ImGui::Spacing();
    padButton("R1", "R1", bw);
    ImGui::SameLine();
    padButton("R2", "R2", bw);
    padButton("R3", "R3", bw);
    ImGui::SameLine();
    padButton("Start", "Start", bw);
    ImGui::EndGroup();

    ImGui::Separator();

    // The sticks are what the generated walker actually reads (it polls the
    // analog sticks, never the D-pad - a held Up does nothing), so they get
    // real sliders, each with a Centre: a slider left at -80 is a player who
    // never stops walking.
    auto stick = [&](const char* label, int base) {
        int x = st.axes[pi][base], y = st.axes[pi][base + 1];
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(scaled(140));
        if (ImGui::SliderInt("X", &x, -127, 127)) st.axes[pi][base] = (int8_t)x;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(140));
        if (ImGui::SliderInt("Y", &y, -127, 127))
            st.axes[pi][base + 1] = (int8_t)y;
        ImGui::SameLine();
        if (ImGui::Button("Centre")) {
            st.axes[pi][base] = 0;
            st.axes[pi][base + 1] = 0;
        }
        ImGui::PopID();
    };
    stick("Left stick (move)", 0);
    stick("Right stick (look)", 2);

    ImGui::Checkbox("Drive with the editor's keyboard", &padKeyboard_);
    prefHelp(
        "WASD / arrows = left stick, IJKL = right stick, Space = Cross,\n"
        "E = Circle, Q = Square, R = Triangle, Enter = Start,\n"
        "Backspace = Select, 1/2 = L1/L2, 3/4 = R1/R2. The keys reach the GAME\n"
        "while THIS window is focused, so PCSX2 never needs the focus - that is\n"
        "the whole point. Nothing is read while you are typing anywhere else in\n"
        "the editor.");
    if (padKeyboard_ &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        struct KeyMap {
            ImGuiKey key;
            const char* button;
        };
        static const KeyMap kKeys[] = {
            {ImGuiKey_Space, "Cross"},  {ImGuiKey_E, "Circle"},
            {ImGuiKey_Q, "Square"},     {ImGuiKey_R, "Triangle"},
            {ImGuiKey_Enter, "Start"},  {ImGuiKey_Backspace, "Select"},
            {ImGuiKey_1, "L1"},         {ImGuiKey_2, "L2"},
            {ImGuiKey_3, "R1"},         {ImGuiKey_4, "R2"},
        };
        for (const KeyMap& k : kKeys) held(k.button, ImGui::IsKeyDown(k.key));
        auto axis = [](ImGuiKey neg, ImGuiKey pos, ImGuiKey neg2, ImGuiKey pos2) {
            int v = 0;
            if (ImGui::IsKeyDown(neg) || ImGui::IsKeyDown(neg2)) v -= 127;
            if (ImGui::IsKeyDown(pos) || ImGui::IsKeyDown(pos2)) v += 127;
            return (int8_t)v;
        };
        st.axes[pi][0] =
            axis(ImGuiKey_A, ImGuiKey_D, ImGuiKey_LeftArrow, ImGuiKey_RightArrow);
        st.axes[pi][1] =
            axis(ImGuiKey_W, ImGuiKey_S, ImGuiKey_UpArrow, ImGuiKey_DownArrow);
        st.axes[pi][2] = axis(ImGuiKey_J, ImGuiKey_L, ImGuiKey_J, ImGuiKey_L);
        st.axes[pi][3] = axis(ImGuiKey_I, ImGuiKey_K, ImGuiKey_I, ImGuiKey_K);
    }

    ImGui::Separator();
    if (!padStatus_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s",
                           padStatus_.c_str());
    else
        ImGui::TextDisabled("Driving bin/livepad.bin at ~25 Hz.");
    ImGui::TextDisabled("Same channel from a script:");
    ImGui::TextDisabled("  tyrax-editor --pad <project> \"stick l 0 -127; wait 2\"");
    ImGui::End();
}

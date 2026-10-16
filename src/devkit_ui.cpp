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
#include "hardware_timeline.hpp"
#include "theme.hpp"
#include "toolkit.hpp"

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
// The engine's delimited error block.
static const char kTyraAssertBanner[] = "==============  TYRAX  =============";
static const char kTyraAssertClose[] = "====================================";

// Returns the last complete TYRA assertion block in `text` (from its banner
// line through the closing rule, inclusive), or "" when there is none / it is
// still being written.
static std::string extractLastTyraAssert(const std::string& text) {
    size_t banner = text.rfind(kTyraAssertBanner);
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

    // Snapshot body: scene + count + one 80-byte record per object (id +
    // template + 12 floats + the resolved player speeds + pad). Deletions are
    // implicit - the game hides whatever is absent.
    std::vector<unsigned char> body;
    body.reserve(8 + recs.size() * 80);
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
        // Player records carry the RESOLVED speed tiers (the same functions
        // codegen bakes with), so a speed edit streams instead of flipping
        // the chip amber. DYNAMIC LIGHT records reuse the same slot (their
        // types never collide) for brightness/radius/flicker, and the record
        // tail carries the spot angle (0 = keep). Zeros on everything else.
        float extra[3] = {0.0f, 0.0f, 0.0f};
        float tail = 0.0f;
        if (r.o->type == PrimitiveType::Player) {
            extra[0] = r.o->playerWalkSpeed;
            extra[1] = project::playerRunSpeed(*r.o);
            extra[2] = project::playerSprintSpeed(*r.o, project_.settings);
        } else if (r.o->type == PrimitiveType::PointLight &&
                   r.o->lightDynamic) {
            extra[0] = r.o->lightBright;
            extra[1] = r.o->lightRadius;
            extra[2] = r.o->lightFlicker;
            tail = r.o->lightSpot ? r.o->lightSpotAngle : 0.0f;
        }
        put(extra, 12);
        put(&tail, 4);
    }
    if (body == liveLinkLastPayload_) return;  // nothing to stream

    // Full file: header (magic/version/seq + body's scene/count/reserved),
    // records, footer echoing seq - the game rejects torn writes. Written to
    // a sibling tmp and renamed so the game never sees a half file; if the
    // rename loses a race with the game's fread, retry on the next tick.
    const uint32_t seq = liveLinkSeq_ + 1;
    std::vector<unsigned char> file;
    file.reserve(24 + body.size() - 8 + 4);
    const uint32_t magic = 0x4C4C5854, version = 4, reserved = 0;
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

// bin/frame.tga: the picture the game took OF ITSELF (docs/devkit.md). Same
// change-detection rule as the VU capture above - the timestamp, never the
// size, because two shots of one scene are the same length to the byte.
//
// The TGA is decoded BY HAND rather than through stbi_load, and that is not
// avoidance of a library: the editor's stb_image is built `STBI_ONLY_PNG` +
// `STBI_ONLY_JPEG`, so it answers *unknown image type* to every TGA there is
// (which is how this was found - on screen, in the error text below). Adding
// TGA to it would widen what every OTHER stbi_load in the editor accepts - the
// asset importer above all - for the sake of one debug preview, where the
// format has exactly ONE writer whose source is known (ps2sdk libdebug): an
// 18-byte header, image type 2, always 32 bits whatever the GS pixel format
// was, uncompressed, BGRA, bottom row first. Two dozen lines, and they can be
// exact about the two things a general decoder has to guess at - the row order
// and the alpha.
void App::dbgReadFrameShot() {
    namespace fs = std::filesystem;
    if (!hasProject_) return;
    const fs::path path = fs::path(project_.dir) / "bin" / "frame.tga";
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    const size_t size = ec ? 0 : (size_t)sz;
    std::error_code wec;
    const auto wt = fs::last_write_time(path, wec);
    const long long stamp = wec ? 0 : (long long)wt.time_since_epoch().count();
    if (size == dbgShotSize_ && stamp == dbgShotStamp_) return;
    if (!size) {  // the Runner deleted it, or the game has just truncated it
        dbgShotSize_ = 0;
        dbgShotStamp_ = stamp;
        dbgShotTorn_ = 0;
        dbgShotPartial_ = 0;
        return;
    }
    // The game writes the header and then ~900 KB of pixels, a line at a time,
    // over host: - so a poll lands mid-write often, not rarely. The file is
    // exactly 18 + w*h*4 bytes and its header says w and h, which makes "is
    // this file finished" answerable before reading any of it.
    //
    // WAITING IS DECIDED BY PROGRESS, NOT BY A NUMBER OF TRIES. A short file
    // that is still GROWING is a write in flight and the wait restarts; only a
    // short file that has stopped growing for six polls (~2.4 s) is a truncated
    // write, and that is what gets reported. The count alone was an emulator
    // assumption: PCSX2 finishes the megabyte between two polls, while over
    // ps2link it is a network round trip per 1.4 KB and takes about three
    // seconds - so the six tries ran out DURING a perfectly good capture and
    // the panel called it malformed.
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    unsigned char head[18] = {};
    f.read(reinterpret_cast<char*>(head), sizeof(head));
    if (f.gcount() != (std::streamsize)sizeof(head)) return;
    const int w = (int)head[12] | ((int)head[13] << 8);
    const int h = (int)head[14] | ((int)head[15] << 8);
    const bool shaped = head[2] == 2 && head[16] == 32 && w > 0 && h > 0;
    const size_t want = 18 + (size_t)w * (size_t)h * 4;
    if (shaped && size < want) {
        if (size != dbgShotPartial_) {  // it grew: the game is still writing
            dbgShotPartial_ = size;
            dbgShotTorn_ = 0;
            return;
        }
        if (++dbgShotTorn_ < 6) return;
    }

    dbgShotSize_ = size;
    dbgShotStamp_ = stamp;
    dbgShotTorn_ = 0;
    dbgShotPartial_ = 0;
    dbgShotWaiting_ = false;
    if (!shaped || size < want) {
        dbgShotError_ =
            "bin/frame.tga is not the 32-bit TGA the game writes (" +
            std::to_string(size) + " bytes, header says " + std::to_string(w) +
            "x" + std::to_string(h) + " at " + std::to_string(head[16]) +
            " bpp)";
        return;
    }
    std::vector<unsigned char> src((size_t)w * (size_t)h * 4);
    f.read(reinterpret_cast<char*>(src.data()), (std::streamsize)src.size());
    if (f.gcount() != (std::streamsize)src.size()) return;  // lost the race

    std::vector<unsigned char> rgba(src.size());
    for (int y = 0; y < h; ++y) {
        // Bottom row first (descriptor bit 5 clear), and BGRA - so the copy
        // both flips and swizzles. Alpha is FORCED opaque: the GS stores it
        // 0..128 and a frame buffer's alpha is a working channel, not
        // coverage, so taken literally the preview comes out half transparent
        // or (where the game left it at zero) invisible. Only the colour here
        // is a picture.
        const unsigned char* s = &src[(size_t)(h - 1 - y) * (size_t)w * 4];
        unsigned char* d = &rgba[(size_t)y * (size_t)w * 4];
        for (int x = 0; x < w; ++x, s += 4, d += 4) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = 255;
        }
    }
    if (!dbgShotTex_) glGenTextures(1, &dbgShotTex_);
    glBindTexture(GL_TEXTURE_2D, dbgShotTex_);
    glUploadTexRgba(w, h, rgba.data());
    dbgShotW_ = w;
    dbgShotH_ = h;
    dbgShotPixels_ = rgba;
    dbgShotError_.clear();

    // KEEP THE PICTURE. bin/frame.tga is a CHANNEL, not an album: one file,
    // overwritten by the next capture and deleted by every launch (a stale
    // shot is a perfectly valid-looking answer to the next session's first
    // one). So a decoded capture is also written out as an ordinary PNG under
    // the project's own screenshots/ - one file per capture, named by the
    // clock so the folder sorts itself, outside bin/ so a Clean does not take
    // the lot, and in a format anything can open. Nothing new is linked for
    // it: stb_image_write is already here.
    dbgShotFile_.clear();
    const fs::path shots = fs::path(project_.dir) / "screenshots";
    std::error_code sec;
    fs::create_directories(shots, sec);
    const std::string when = platform::fileTimeStamp();
    fs::path png = shots / ("frame-" + when + ".png");
    for (int n = 2; n < 100 && fs::exists(png, sec); ++n)  // twice in a second
        png = shots / ("frame-" + when + "-" + std::to_string(n) + ".png");
    if (stbi_write_png(png.string().c_str(), w, h, 4, rgba.data(), w * 4))
        dbgShotFile_ = png.string();
    else
        dbgShotError_ = "could not write " + png.string();

    statusMessage_ = "Frame capture: " + std::to_string(w) + "x" +
                     std::to_string(h) + " from the running game" +
                     (dbgShotFile_.empty()
                          ? std::string()
                          : " - saved as screenshots/" +
                                fs::path(dbgShotFile_).filename().string());
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

// --- The input recorder -----------------------------------------------------
// docs/input-replay.md. Almost everything here is about the NEXT run: the
// mode is staged into the Runner, which prepares bin/ before it launches. The
// only thing that talks to a running game is replayTick(), which reads the
// status the game writes.

void App::replayRescan(bool force) {
    namespace fs = std::filesystem;
    const double now = ImGui::GetTime();
    if (!force && now < replayScanAt_) return;
    replayScanAt_ = now + 1.0;
    replayFiles_.clear();
    if (!hasProject_) return;
    std::error_code ec;
    const fs::path dir = fs::path(project_.dir) / "recordings";
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != ".tyrarep") continue;
        replayFiles_.push_back(e.path().filename().string());
    }
    std::sort(replayFiles_.begin(), replayFiles_.end());
}

void App::replayTick() {
    namespace fs = std::filesystem;
    const bool on = hasProject_ && project_.settings.inputRecorder &&
                    project_.settings.buildProfile == "debug";
    // Keep the Runner's staging in step with the radio button rather than
    // setting it at each of the fifteen places a build or a run starts. The
    // worker only READS it while a launch is in flight and the UI thread only
    // writes it while the Runner is idle, which is the same contract every
    // other launch input here has.
    if (!runner_.busy()) {
        Runner::ReplayLaunch want;
        if (on && replayArm_ == ReplayArm::Record) {
            want.mode = Runner::ReplayLaunch::Record;
        } else if (on && replayArm_ == ReplayArm::Play && !replayFile_.empty()) {
            want.mode = Runner::ReplayLaunch::Play;
            want.file = (fs::path(project_.dir) / "recordings" / replayFile_)
                            .string();
        }
        runner_.replay_ = want;
    }
    if (!on) {
        replayHaveStatus_ = false;
        return;
    }
    const double now = ImGui::GetTime();
    if (now < replayNextTick_) return;
    // The game rewrites replay.st on chunk boundaries - once or twice a second
    // at the PCSX2 cadence - so reading faster only costs disk hits.
    replayNextTick_ = now + 0.25;
    livereplay::Status s;
    if (!livereplay::readStatus(
            (fs::path(project_.dir) / "bin" / "replay.st").string(), s))
        return;  // not running, or a torn write - retry next tick
    replayStatus_ = s;
    replayHaveStatus_ = true;
}

std::string App::replayStopAndSave(const std::string& name) {
    namespace fs = std::filesystem;
    if (!hasProject_) return "no project is open";
    std::string clean = sanitizeAssetName(name);
    if (clean.empty()) return "give the recording a name first";
    const fs::path binDir = fs::path(project_.dir) / "bin";
    const fs::path raw = binDir / "replay.out";
    std::error_code ec;
    if (!fs::exists(raw, ec))
        return "there is no recording in bin/ - was this run recorded?";

    // Ask the game to finish cleanly, then give it a moment to write its
    // terminal chunk. Not waiting is survivable (the parser tolerates a
    // truncated tail and Save canonicalizes what parsed), so a game that has
    // already exited costs a short pause and nothing else.
    {
        std::ofstream f(binDir / "replay.stop");
        if (f) f << "stop\n";
    }
    const auto sizeOf = [&](const fs::path& p) -> uintmax_t {
        std::error_code e;
        const uintmax_t n = fs::file_size(p, e);
        return e ? 0 : n;
    };
    uintmax_t last = sizeOf(raw);
    for (int i = 0; i < 20; ++i) {  // up to ~2 s
        platform::sleepMs(100);
        livereplay::Status s;
        if (livereplay::readStatus((binDir / "replay.st").string(), s) && s.done)
            break;
        const uintmax_t now = sizeOf(raw);
        if (now == last && i >= 5) break;  // the game is not writing any more
        last = now;
    }
    fs::remove(binDir / "replay.stop", ec);

    const fs::path dest =
        fs::path(project_.dir) / "recordings" / (clean + ".tyrarep");
    const std::string err = livereplay::finalize(raw.string(), dest.string());
    if (!err.empty()) return err;
    replayRescan(true);
    replayFile_ = dest.filename().string();
    return "";
}

void App::drawReplayPanel() {
    namespace fs = std::filesystem;
    if (project_.settings.buildProfile != "debug") {
        ImGui::TextDisabled(
            "Recording and replaying need the debug build profile\n"
            "(Project > Preferences > Build).");
        return;
    }
    if (!project_.settings.inputRecorder) {
        ImGui::TextDisabled(
            "The input recorder is off for this project\n"
            "(Project > Preferences > Build > Input recorder).");
        return;
    }
    replayRescan(false);

    // What the NEXT run does. Staged into the Runner, which prepares bin/
    // before it launches - so a choice made here needs a Build & Run (F5) or a
    // Run to take effect, and says so.
    ImGui::TextDisabled("Next run");
    int arm = (int)replayArm_;
    bool changed = ImGui::RadioButton("Live", &arm, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Record", &arm, 1);
    ImGui::SameLine();
    ImGui::BeginDisabled(replayFiles_.empty());
    // "Play back", not "Replay": the TAB is called Replay, and an ImGui label
    // IS its id - two widgets sharing one makes the radio unreachable from
    // --ui-script (there is no way to say which of the two you meant) and
    // reads ambiguously to a person as well.
    changed |= ImGui::RadioButton("Play back", &arm, 2);
    ImGui::EndDisabled();
    if (replayFiles_.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("No recordings yet - record a run and Save it.");
    if (changed) replayArm_ = (ReplayArm)arm;

    if (replayArm_ == ReplayArm::Play) {
        if (replayFile_.empty() && !replayFiles_.empty())
            replayFile_ = replayFiles_.front();
        ImGui::SetNextItemWidth(scaled(260));
        if (ImGui::BeginCombo("##replayfile", replayFile_.c_str())) {
            for (size_t i = 0; i < replayFiles_.size(); ++i) {
                const bool sel = replayFiles_[i] == replayFile_;
                // An explicit id: two recordings can share a display name only
                // by accident, but a Selectable's LABEL is its id and a
                // collision breaks the click silently.
                if (ImGui::Selectable(
                        (replayFiles_[i] + "##rep" + std::to_string(i)).c_str(),
                        sel))
                    replayFile_ = replayFiles_[i];
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Show file")) {
            platform::revealInFileManager(
                (fs::path(project_.dir) / "recordings" / replayFile_).string());
        }
        // What the file itself says, so a mismatch is visible before the run
        // rather than as a hundred divergences during it.
        livereplay::Recording rec;
        std::string err;
        if (!replayFile_.empty() &&
            livereplay::read((fs::path(project_.dir) / "recordings" /
                              replayFile_).string(), rec, err)) {
            const float hz = rec.header.frameRate ? (float)rec.header.frameRate
                                                  : 50.0f;
            ImGui::TextDisabled("%u frames, %.1f s at %u Hz%s",
                                (unsigned)rec.frames.size(),
                                (float)rec.frames.size() / hz,
                                rec.header.frameRate,
                                rec.truncated ? " (truncated)" : "");
            const float projHz =
                project_.settings.videoSystem == "ntsc" ? 60.0f : 50.0f;
            if ((float)rec.header.frameRate != projHz)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                   "This project runs at %.0f Hz - the game "
                                   "will refuse this recording.",
                                   projHz);
            else if (rec.header.layout != project::inputLayoutHash(project_))
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                   "The project changed since this was "
                                   "recorded - expect divergences.");
        } else if (!replayFile_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                               err.c_str());
        }
    }
    ImGui::TextDisabled("Takes effect on the next Build & Run (F5) or Run.");

    ImGui::Separator();

    // What the RUNNING game is doing, straight out of bin/replay.st.
    if (!replayHaveStatus_) {
        ImGui::TextDisabled("The game is not recording or replaying.");
    } else if (replayStatus_.mode == livereplay::Status::Record) {
        ImGui::Text("Recording: frame %u", replayStatus_.frame);
        if (replayStatus_.done) ImGui::TextDisabled("(stopped)");
    } else if (replayStatus_.mode == livereplay::Status::Replay) {
        if (replayStatus_.done)
            ImGui::Text("Replay finished: %u frames, %u divergence(s)",
                        replayStatus_.frame, replayStatus_.divergences);
        else
            ImGui::Text("Replay: %u / %u", replayStatus_.frame,
                        replayStatus_.total);
        if (replayStatus_.divergences)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "Diverged at frame %u (%u frame(s) differ)",
                               replayStatus_.firstDivergent,
                               replayStatus_.divergences);
        else if (replayStatus_.done)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f),
                               "Reproduced exactly.");
    } else {
        ImGui::TextDisabled("The game is not recording or replaying.");
    }

    ImGui::Separator();

    // Saving is what turns bin/replay.out - a raw, possibly half-written
    // append log - into a canonical file worth committing.
    ImGui::SetNextItemWidth(scaled(200));
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", replaySaveName_.c_str());
    if (ImGui::InputTextWithHint("##repname", "recording name", buf,
                                 sizeof(buf)))
        replaySaveName_ = buf;
    ImGui::SameLine();
    const bool haveRaw = hasProject_ &&
                         fs::exists(fs::path(project_.dir) / "bin" / "replay.out");
    ImGui::BeginDisabled(!haveRaw || replaySaveName_.empty());
    if (ImGui::Button("Save recording")) {
        const std::string err = replayStopAndSave(replaySaveName_);
        replayMsg_ = err.empty()
                         ? "Saved recordings/" + replayFile_
                         : "Save failed: " + err;
    }
    ImGui::EndDisabled();
    if (!haveRaw && ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Nothing to save: bin/replay.out does not exist, so this run was\n"
            "not recorded. Pick Record above and run again.");
    if (ImGui::IsItemHovered() && haveRaw)
        ImGui::SetTooltip(
            "Asks the game to finish the recording, then writes it into the\n"
            "project's recordings/ folder - whole chunks, a frame count and a\n"
            "clean end. A recording killed with the emulator still saves: the\n"
            "unfinished tail is dropped and everything before it is kept.");

    if (!replayMsg_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", replayMsg_.c_str());
    }

    ImGui::Separator();
    ImGui::TextDisabled("What a replay reproduces");
    prefHelp(
        "Both pads (buttons, click edges and stick axes), the USB keyboard\n"
        "and mouse, the frame time, and the seed of every runtime procedural\n"
        "volume. That is everything the game's own behaviour depends on, so\n"
        "the run repeats itself.\n\n"
        "NOT reproduced: memory-card saves (PCSX2 keeps its card between\n"
        "runs) and anything you change while it plays - a rewind, a Live\n"
        "Logic patch or a Live Link edit all move the world out from under\n"
        "the recording on purpose. See docs/input-replay.md.");
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
        dbgReadFrameShot();
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

    // How old the snapshot FILE is, which is a different question from
    // "did a new snapshot arrive". The two channels this rides on fail
    // differently and only one of them is visible from here: a ps2link
    // session's file server is a ps2client the editor spawned, and when it
    // goes the console keeps running while every devkit file freezes at its
    // last write. That leaves a valid, parseable, permanently stale
    // livedbg.bin - which without this reads exactly like "no data yet".
    if (now >= dbgSnapFileNextStat_) {
        dbgSnapFileNextStat_ = now + 0.5;
        std::error_code ec;
        const fs::path snapPath = binDir / "livedbg.bin";
        const fs::file_time_type t = fs::last_write_time(snapPath, ec);
        if (ec) {
            dbgSnapFileAge_ = -1.0;  // not there at all
        } else {
            // NEVER against the system clock: file_clock's epoch is
            // implementation-defined and on this libstdc++ sits in the
            // future, so a system_clock difference is nonsense. Its own
            // clock's now() is the only correct comparand.
            const auto d = fs::file_time_type::clock::now() - t;
            dbgSnapFileAge_ =
                std::chrono::duration<double>(d).count();
            if (dbgSnapFileAge_ < 0.0) dbgSnapFileAge_ = 0.0;
        }
    }

    // The file went away: a launch cleared the channel (both Runner paths
    // delete it), so whatever reported before is over - not a hang. Without
    // this a relaunch read as "stopped reporting" while the new game booted.
    if (dbgSnapFileAge_ < 0.0) dbgSnapTime_ = 0.0;
    // A snapshot file that was already old when this editor first saw it is a
    // leftover of an earlier session (opening a project whose game ran
    // yesterday), not a game: never attach to it, or it reads as "reporting"
    // for a moment and then as a game that stopped reporting.
    const int reportEvery = project_.settings.liveDebugSnapshotFrames > 0
                                ? project_.settings.liveDebugSnapshotFrames : 25;
    const bool freshFile = dbgSnapFileAge_ >= 0.0 &&
                           dbgSnapFileAge_ < std::max(5.0, 2.0 + 3.0 * reportEvery / 25.0);
    livedbg::Snapshot snap;
    if ((dbgSnapTime_ > 0.0 || freshFile) &&
        livedbg::readSnapshot((binDir / "livedbg.bin").string(), snap) &&
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
        // The Watch page's Changed column: which rows this report changed. The
        // first report of a run (or of a different table) is a baseline.
        if (restarted || dbgPrevVars_.size() != snap.vars.size()) {
            dbgVarChanged_.assign(snap.vars.size() / 3, 0);
            if (restarted) dbgFrameMs_.clear();
        } else {
            for (size_t v = 0; v + 2 < snap.vars.size() && v / 3 < dbgVarChanged_.size(); v += 3)
                if (snap.vars[v] != dbgPrevVars_[v] || snap.vars[v + 1] != dbgPrevVars_[v + 1] ||
                    snap.vars[v + 2] != dbgPrevVars_[v + 2])
                    dbgVarChanged_[v / 3] = snap.frame;
        }
        dbgPrevVars_ = snap.vars;
        // The Stats page's frame-time history: the game's own rendered rate.
        if (snap.stats.valid) {
            const int fx10 = snap.stats.fpsX10 > 0 ? snap.stats.fpsX10 : snap.stats.fps * 10;
            if (fx10 > 0) {
                dbgFrameMs_.push_back(10000.0f / (float)fx10);
                if (dbgFrameMs_.size() > kDbgFrameMsKeep)
                    dbgFrameMs_.erase(dbgFrameMs_.begin(),
                                      dbgFrameMs_.begin() + (long)(dbgFrameMs_.size() - kDbgFrameMsKeep));
            }
        }
        dbgSnap_ = std::move(snap);
        dbgSnapTime_ = now;
    }

    // The game is "there" while its snapshots keep arriving. A halted game
    // still flushes (its loop keeps running), so silence means gone, not paused.
    // Allow several report intervals at the observed game rate. A deliberately
    // slow report channel must not look like a crash every two seconds.
    const double reportFps = dbgSnap_.stats.fpsX10 > 0
                                 ? dbgSnap_.stats.fpsX10 / 10.0
                                 : std::max(1, dbgSnap_.stats.fps);
    const int reportFrames = project_.settings.liveDebugSnapshotFrames > 0
                                 ? project_.settings.liveDebugSnapshotFrames : 25;
    const double silenceLimit = std::max(2.0, 1.0 + 3.0 * reportFrames / reportFps);
    const bool reporting = dbgSnapTime_ > 0.0 && now - dbgSnapTime_ < silenceLimit;
    // A game that WAS reporting and stopped, with no crash report and no
    // assertion, is a hang (or an exception nobody caught): the devkit
    // heartbeat is the only witness. Remember where it died - the fire history,
    // the watch curves and the timers from just before are still in memory, and
    // that is the post-mortem.
    // Only while the game's process is still up (Runner::gameStatus: PCSX2 on
    // this ELF, or our ps2client): a game that was closed, stopped or
    // redeployed went quiet because it is gone, which is no crash and no hang.
    if (!reporting && dbgSnapTime_ > 0.0 && !dbgLostGame_ &&
        !dbgCrash_.present && dbgSnap_.frame > 0 && runner_.gameStatus().running) {
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
        if (want.captureRenderCost) {
            want.seq = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (!want.seq) want.seq = 1;
        }
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
            dbgCmd_.captureFrame = false;
            if (dbgCmd_.captureRenderCost) dbgRenderCostSeq_ = want.seq;
            dbgCmd_.captureRenderCost = false;
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
    if (!hasProject_ || project_.settings.buildProfile != "debug") {
        liveLogicState_ = LogicState::Off;
        return;
    }
    // Live Logic OFF still owes one answer: "are my graph edits in the running
    // game?" - and the honest answer is no. Nothing else says it. Live Link is
    // the chip people watch and it stays green, correctly, because it streams
    // OBJECT edits and has never carried flow-graph logic; the editor used to
    // return here and show nothing at all, so an edited graph that needs a
    // rebuild was indistinguishable from one already running. Compare the
    // hashes (cheap - no compile, no capability check) and let the chip say so.
    if (!project_.settings.liveLogic) {
        const double nowOff = ImGui::GetTime();
        // The throttle must LEAVE the last answer standing. Clearing the state
        // here and re-deciding only every half second made the chip correct
        // for one frame in thirty and invisible in practice - the state is
        // what the toolbar reads every frame, not a scratch variable.
        if (nowOff < liveLogicNextTick_) return;
        liveLogicNextTick_ = nowOff + 0.5;
        liveLogicState_ = LogicState::Off;
        livelogic::BuiltList list;
        if (!livelogic::loadBuiltList(
                (fs::path(project_.dir) / "src" / "gen" / "livelogic.built")
                    .string(),
                list))
            return;
        for (size_t si = 0; si < project_.scenes.size(); ++si) {
            const SceneData& sc = project_.scenes[si];
            for (const SceneObject& o : sc.objects) {
                if (o.flowGraph.empty()) continue;
                const uint64_t live = livelogic::graphHash(o.flowGraph);
                bool wasBuilt = false;
                for (const livelogic::BuiltGraph& g : list.graphs)
                    if (g.scene == (int)si && g.objectId == o.id) {
                        wasBuilt = true;
                        if (g.hash != live) liveLogicState_ = LogicState::OffStale;
                        break;
                    }
                // A graph the build never saw (a new object, or one that had
                // no graph then) is the same answer: nothing in the running
                // game runs it.
                if (!wasBuilt) liveLogicState_ = LogicState::OffStale;
            }
        }
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

// Why the panel is empty, in ONE line, with the remedy. An empty panel is the
// single most expensive failure this channel has: a dead transport and a game
// that has not booted yet look identical, and the difference is on disk the
// whole time (see livedbgTick's file-age stat). Called by the Debugger's state
// block AND the Stats tab, so the two cannot tell different stories.
//
// It used to say all of that inline, and the panel opened on a paragraph nobody
// reads under stress. The rest lives in dbgSilenceDetail(), one hover away.
// A frozen bin/livedbg.bin while PCSX2 is not running this ELF is simply the
// last session's file: the game was closed. Over ps2link the same silence is
// ambiguous (the console may still be running with its file server gone), so
// that case keeps the "stale snapshot" reading and its explanation.
bool App::dbgGameClosed() const {
    return !runOnPs2_ && !runner_.gameStatus().running;
}

std::string App::dbgSilenceReason() const {
    if (dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted)
        return {};
    if (dbgState_ == DbgState::Off || dbgState_ == DbgState::NoBuild) return {};
    if (dbgSnapFileAge_ < 0.0)
        return "Nothing is reporting yet - Build & Run (F5 / F6).";
    if (dbgGameClosed()) return "The game is not running - Build & Run (F5 / F6).";
    // Running/Waiting already accounts for the configured report interval and
    // observed frame rate in livedbgTick. File age adds transport context here.
    const double age = dbgSnapFileAge_;
    std::string when;
    if (age < 90.0)
        when = std::to_string((int)(age + 0.5)) + "s";
    else
        when = std::to_string((int)(age / 60.0 + 0.5)) + " min";
    return "No new data for " + when + " - run it again (F5 / F6).";
}

// The long version of the above, for a (?) next to it. Everything the sentence
// had to drop: which file, and why a console that is still visibly running can
// stop reporting without anything having crashed.
const char* App::dbgSilenceDetail() const {
    if (dbgSnapFileAge_ < 0.0)
        return "F5 builds and runs it in PCSX2, F6 deploys it to a console.\n"
               "bin/livedbg.bin has not appeared yet. If the game IS running,\n"
               "it was built before the Live Debugger preference was switched\n"
               "on: rebuild it.";
    if (dbgGameClosed())
        return "No PCSX2 is running this project's ELF, so bin/livedbg.bin is\n"
               "what the last session left behind. F5 builds and runs it again.";
    return "F5 for PCSX2, F6 for a console. That session is OVER:\n"
           "bin/livedbg.bin stopped changing, so what is on disk is a snapshot\n"
           "of a session that is over. The game may still be running: over\n"
           "ps2link the file server is a ps2client this editor spawned, and\n"
           "closing the editor, stopping the game or redeploying THIS project\n"
           "takes it down - the console then keeps running with no host: to\n"
           "write to. The cure is a redeploy (Run on PS2, F6), not a retry.\n"
           "Under PCSX2 it means the game itself stopped.";
}

// --- The Debugger window ----------------------------------------------------
// Tools > Debugger (F9). The state of the running game's logic: what fired,
// what the variables hold, where it is stopped - plus the transport, the
// breakpoint list and the frame's performance. The graph itself is the other
// half of this UI (the Flow Graph window's overlay); this panel is the
// instrument cluster.
//
// Layout (docs/live-debugger.md): a status header (state pill, frame and both
// frame rates, the transport with its keys), a section RAIL grouped by the
// question asked - Logic / World / Performance - instead of thirteen tabs in
// one strip, the page, a right-hand column that says where the game stopped
// (or why it is not running), and ONE time strip at the bottom that replaced
// the Timeline, Rewind and Replay tabs: node fires per frame, the inspected
// frame, where the world is, and the input recorder's state.
//
// The window lives in two docks - the Default layout's bottom drawer (wide and
// short) and the Logic layout's right column (narrow and tall) - so every part
// adapts: the rail collapses to icons, the right column folds into the top of
// the page, the transport drops to its own line and the time strip collapses.

namespace {

// What a watch value reads as. A scalar fact is one float in the game and a
// TYPE in the catalog, so it prints the way the catalog means it - true/false,
// an enum's option name - instead of the number the console stores. `tone`:
// 0 plain, 1 true, 2 false/none, 3 enum.
std::string dbgValueText(const Project& p, const livedbg::VarSym& v, const std::vector<float>& vals,
                         size_t row, int* tone) {
    *tone = 0;
    const size_t o = row * 3;
    if (o + 2 >= vals.size()) {
        *tone = 2;
        return "-";
    }
    char buf[96];
    const facts::Fact* fact = nullptr;
    if (v.kind == 'f' || v.kind == 'F')
        for (const facts::Fact& f : p.facts)
            if (f.name == v.name) {
                fact = &f;
                break;
            }
    if (v.kind == 'b' || (fact && fact->type == facts::Type::Bool)) {
        *tone = vals[o] != 0.0f ? 1 : 2;
        return vals[o] != 0.0f ? "true" : "false";
    }
    if (v.kind == 'p' || v.kind == 'F') {
        // Three floats, which is what a position IS.
        std::snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f", vals[o], vals[o + 1], vals[o + 2]);
        return buf;
    }
    if (v.kind == 'i' || (fact && fact->type == facts::Type::Int)) {
        std::snprintf(buf, sizeof(buf), "%d", (int)lroundf(vals[o]));
        return buf;
    }
    if (fact && fact->type == facts::Type::Enum) {
        const int idx = (int)lroundf(vals[o]);
        *tone = 3;
        if (idx >= 0 && idx < (int)fact->options.size()) return fact->options[(size_t)idx];
        std::snprintf(buf, sizeof(buf), "%d (no option)", idx);  // say WHICH number
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%g", vals[o]);
    return buf;
}

// The Kind column, and one of the two things the filter matches against.
const char* dbgKindText(char k) {
    switch (k) {
        case 'i': return "int";
        case 'b': return "bool";
        case 'p': return "position";
        case 'f': return "fact";
        case 'F': return "fact pos";
        default: return "save";
    }
}

// "18 432": frame numbers are read, not computed with - a thin space every
// three digits makes a five-digit frame readable at a glance.
std::string dbgFrameText(uint32_t f) {
    std::string s = std::to_string(f);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, " ");
    return s;
}

ImU32 dbgCol(const ImVec4& c, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

// An inline small-caps caption ("FRAME TIME") that, unlike ui::Caption, is an
// ordinary item a SameLine can follow.
void dbgCaps(const char* text) {
    char up[64];
    std::snprintf(up, sizeof(up), "%s", text);
    for (char* c = up; *c; ++c) *c = (char)std::toupper((unsigned char)*c);
    ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 0.8f);
    ImGui::TextColored(theme::semantics().textDim, "%s", up);
    ImGui::PopFont();
}

}  // namespace

std::string App::dbgNodeLabel(const livedbg::NodeSym& n) const {
    std::string obj = "(deleted object)";
    if (n.scene >= 0 && n.scene < (int)project_.scenes.size())
        for (const SceneObject& o : project_.scenes[n.scene].objects)
            if (o.id == n.objectId) {
                obj = o.name;
                break;
            }
    const FlowNodeType* t = flowNodeType(n.type);
    return obj + " \xc2\xb7 " + (t ? std::string(t->title) : n.type);
}

// Jumps the Flow Graph window to a node's owner (and selects the object).
void App::dbgRevealNode(const livedbg::NodeSym& n) {
    if (n.scene >= 0 && n.scene < (int)project_.scenes.size() && n.scene != project_.activeScene) {
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
}

void App::dbgSetTransport(int what) {
    // 0 = pause/continue toggle, 1 = step one frame, 2 = run until a node fires
    dbgCmd_.stepFrames = 0;
    dbgCmd_.stepUntilFire = false;
    if (what == 0) {
        dbgCmd_.halt = dbgState_ != DbgState::Halted;
    } else {
        dbgCmd_.halt = false;
        if (what == 1) dbgCmd_.stepFrames = 1;
        else dbgCmd_.stepUntilFire = true;
    }
    dbgCmdWritten_ = false;
    if (!dbgCmd_.halt) dbgScrub_ = -1;
}

void App::dbgBuildAndRun() {
    // F5's own path: build, launch PCSX2, bring this panel forward.
    if (runner_.busy()) return;
    openDebuggerForLaunch();
    runner_.buildAndRun(projectForBuild(), true, false, project_.activeScene);
}

// The header: where the game is, and the transport.
void App::dbgDrawHeader() {
    const theme::Semantics& sem = theme::semantics();
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    const ImGuiStyle& st = ImGui::GetStyle();

    // --- the state pill
    std::string pill;
    int kind = 4;
    bool pauseIcon = false;
    if (dbgCrash_.present) {
        pill = "Crashed";
        kind = 2;
    } else if (dbgLostGame_) {
        pill = "Stopped reporting";
        kind = 2;
    } else {
        switch (dbgState_) {
            case DbgState::Off: pill = "Off"; break;
            case DbgState::NoBuild: pill = "Not built yet"; break;
            case DbgState::Waiting:
                // A frozen file is a DIFFERENT thing from an absent one: one is
                // "not started yet", the other "the transport died and the
                // console is still running".
                if (dbgSnapFileAge_ >= 0.0 && dbgGameClosed()) {
                    pill = "Not running";
                    kind = 4;
                } else if (dbgSnapFileAge_ >= 0.0) {
                    pill = "Stale snapshot";
                    kind = 1;
                } else {
                    pill = "Waiting for the game";
                    kind = 3;
                }
                break;
            case DbgState::Stale:
                pill = "Rebuild needed";
                kind = 1;
                break;
            case DbgState::Running:
                pill = runner_.ps2ClientAlive() ? "Running \xc2\xb7 PS2" : "Running \xc2\xb7 PCSX2";
                kind = 0;
                break;
            case DbgState::Halted:
                pill = dbgSnap_.breakKey >= 0 ? "Halted on breakpoint" : "Paused";
                kind = 1;
                pauseIcon = true;
                break;
        }
    }
    ImGui::AlignTextToFramePadding();
    ui::StatePill(pill.c_str(), kind, pauseIcon, ui::RtIcon::Pause);
    if (dbgState_ == DbgState::Waiting || dbgState_ == DbgState::NoBuild)
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && dbgState_ == DbgState::Waiting)
            ImGui::SetTooltip("%s", dbgSilenceDetail());

    // --- the transport, right-aligned; on its own line when the row is full
    const bool halted = dbgState_ == DbgState::Halted;
    const char* why = live ? nullptr : "No game is reporting - Build & Run (F5) a debug build.";
    ui::TransportItem items[3] = {
        halted ? ui::TransportItem{"Continue", ui::RtIcon::Play, nullptr, "F10", true, live,
                                   why ? why : "Let the game run again (F10)."}
               : ui::TransportItem{"Pause", ui::RtIcon::Pause, nullptr, "F10", false, live,
                                   why ? why
                                       : "F10. A halt freezes the world - scripts, the walker, "
                                         "particles, animation -\nwhile the game keeps "
                                         "presenting frames, so you can still look at what you "
                                         "stopped."},
        {"Step frame", ui::RtIcon::StepFrame, nullptr, "F11", false, live,
         why ? why : "F11. Runs exactly one frame, then stops again."},
        {"Step node", ui::RtIcon::StepNode, nullptr, nullptr, false, live,
         why ? why
             : "Runs until ANY instrumented node fires, then stops on it -\nthe fastest way to "
               "find out what actually runs next."},
    };
    const float iconW = ImGui::GetFrameHeight() + 6.0f;
    // Too narrow for the words (the Logic layout's column): icons only, the
    // names and keys stay in the tooltips.
    if (ui::TransportBarWidth(items, 3) + iconW * 2 + scaled(24) > ImGui::GetWindowContentRegionMax().x)
        for (ui::TransportItem& it : items) {
            it.text = "";
            it.key = nullptr;
        }
    const float barW = ui::TransportBarWidth(items, 3);
    const float tailW = barW + st.ItemSpacing.x * 3 + iconW * 2 + scaled(8);
    // --- frame / fps / scene, in the mono face
    if (live) {
        ImGui::SameLine(0.0f, scaled(10));
        const livedbg::Stats& s = dbgSnap_.stats;
        // Two rates, not one: rendered = loop iterations, presented = flips
        // (about twice as many with frame extrapolation). The game's own
        // measurement when it reports one, else frames over this editor's clock.
        const float rendered = s.fpsX10 > 0 ? s.fpsX10 / 10.0f : dbgFps_;
        const float presented = s.presentedX10 > 0 ? s.presentedX10 / 10.0f : rendered;
        std::string scene = std::to_string(dbgSnap_.scene);
        if (dbgSnap_.scene >= 0 && dbgSnap_.scene < (int)project_.scenes.size())
            scene = project_.scenes[dbgSnap_.scene].name;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "frame %s \xc2\xb7 %.1f / %.1f fps \xc2\xb7 scene %s",
                      dbgFrameText(dbgSnap_.frame).c_str(), rendered, presented, scene.c_str());
        ui::FontScope mono(ui::monoFont());
        // Shorter forms on a narrow dock: drop the scene, then the rates.
        // Keep the transport on this row when the full text is what would push it off.
        const float full = ImGui::GetContentRegionMax().x - ImGui::GetCursorPosX();
        const float room = full - tailW > ImGui::CalcTextSize("frame 00 000").x ? full - tailW : full;
        if (ImGui::CalcTextSize(buf).x > room)
            std::snprintf(buf, sizeof(buf), "frame %s \xc2\xb7 %.1f / %.1f fps",
                          dbgFrameText(dbgSnap_.frame).c_str(), rendered, presented);
        if (ImGui::CalcTextSize(buf).x > room)
            std::snprintf(buf, sizeof(buf), "frame %s", dbgFrameText(dbgSnap_.frame).c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(sem.textDim, "%s", buf);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Rendered / presented frames per second, as the game measures them.\n"
                "Rendered = game loops; presented = buffer flips - frame extrapolation\n"
                "shows about twice as many frames as it renders (docs/profiling.md).");
    }

    const float right = ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() + tailW > right) ImGui::NewLine();
    ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), right - tailW));
    const float rowY = ImGui::GetCursorPosY();
    switch (ui::TransportBar("##transport", items, 3)) {
        case 0: dbgSetTransport(0); break;
        case 1: dbgSetTransport(1); break;
        case 2: dbgSetTransport(2); break;
        default: break;
    }
    ImGui::SameLine(0.0f, scaled(10));
    ImGui::SetCursorPosY(rowY + 3.0f);
    if (ui::IconButton("Capture screen", ui::RtIcon::Camera, iconW - 6.0f,
                       live ? "Capture the console's own frame (Screen)"
                            : "Capture screen - needs a running debug game",
                       live)) {
        dbgCmd_.captureFrame = true;
        dbgCmdWritten_ = false;
        dbgShotWaiting_ = true;
        dbgPage_ = DbgPage::Screen;
    }
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::SetCursorPosY(rowY + 3.0f);
    if (ui::IconButton("Debugger settings", ui::RtIcon::More, iconW - 6.0f))
        ImGui::OpenPopup("##dbgsettings");
    if (ImGui::BeginPopup("##dbgsettings")) {
        // The devkit switches this panel depends on, where the panel is - the
        // same project settings as Project > Preferences > Build.
        ui::Caption("This project's devkit");
        bool changed = false;
        changed |= ui::Switch("Live Debugger", &project_.settings.liveDebug);
        changed |= ui::Switch("Live Logic (hot-patch graphs)", &project_.settings.liveLogic);
        changed |= ui::Switch("Time machine (rewind)", &project_.settings.timeMachine);
        changed |= ui::Switch("Input recorder", &project_.settings.inputRecorder);
        if (changed) commitChange();
        ImGui::TextDisabled("Built into the next debug build.");
        ImGui::Spacing();
        ui::Caption("This window");
        ui::Switch("Watched-object trails in the viewport", &dbgShowTrails_);
        if (!timeHistory_.empty()) {
            ImGui::Spacing();
            ui::Caption("Rewind history");
            const float hz = project_.settings.videoSystem == "ntsc" ? 60.0f : 50.0f;
            ImGui::TextDisabled("%zu captures, %.1f s (%.1f of %d MB)", timeHistory_.count(),
                                (float)timeHistory_.frameSpan() / hz,
                                (double)timeHistory_.bytes() / (1024.0 * 1024.0), timeBudgetMb_);
            if (ImGui::Button("Clear history")) {
                timeHistory_.clear();
                timeScrub_ = -1;
                timeStatus_ = "History cleared";
            }
        }
        ImGui::EndPopup();
    }
}

// The rail: one row per page, grouped by the question it answers, each with
// the count that says whether there is anything there without opening it.
void App::dbgDrawRail(bool compact, bool horizontal) {
    const theme::Semantics& sem = theme::semantics();
    struct Row {
        DbgPage page;
        const char* label;
        ui::RtIcon icon;
        std::string count;
        int kind;
    };
    int watchedNow = 0;
    for (const DbgObjTrack& t : dbgObjWatch_)
        if (t.index >= 0) ++watchedNow;
    const std::string objCount =
        std::to_string(watchedNow) + " / " + std::to_string(livedbg::kMaxWatchObjects);
    std::string patches;
    int patchKind = 4;
    if (!liveLogicBlocked_.empty()) {
        patches = std::to_string(liveLogicBlocked_.size());
        patchKind = 1;
    } else if (liveLogicPatchCount_ > 0) {
        patches = std::to_string(liveLogicPatchCount_);
        patchKind = 3;
    }
    const int bps = (int)project_.debugBreakpoints.size();
    const Row logic[] = {
        {DbgPage::Watch, "Watch", ui::RtIcon::Eye, dbgSyms_.loaded ? std::to_string(dbgSyms_.vars.size()) : "", 4},
        {DbgPage::Nodes, "Nodes", ui::RtIcon::Nodes, dbgSyms_.loaded ? std::to_string(dbgSyms_.nodes.size()) : "", 4},
        {DbgPage::Breakpoints, "Breakpoints", ui::RtIcon::Breakpoint, bps ? std::to_string(bps) : "", bps ? 2 : 4},
        {DbgPage::Patches, "Hot patches", ui::RtIcon::Bolt, patches, patchKind},
    };
    const Row world[] = {
        {DbgPage::Objects, "Objects", ui::RtIcon::Cube, watchedNow ? objCount : "", 4},
        {DbgPage::Screen, "Screen", ui::RtIcon::Monitor, "", 4},
    };
    const Row perf[] = {
        {DbgPage::Stats, "Stats", ui::RtIcon::Bars, "", 4},
        {DbgPage::RenderCost, "Render cost", ui::RtIcon::Lines, "", 4},
        {DbgPage::Hardware, "Hardware timeline", ui::RtIcon::Timeline, "", 4},
        {DbgPage::Vu, "VU memory", ui::RtIcon::Chip, "", 4},
    };
    auto group = [&](const char* caption, const Row* rows, int n, bool first) {
        if (horizontal) {
            // A short dock (the bottom drawer): one row of icons over the page.
            for (int i = 0; i < n; ++i) {
                if (!first || i) ImGui::SameLine(0.0f, i ? scaled(2) : scaled(14));
                if (ui::RailRow(rows[i].label, rows[i].icon, rows[i].count.c_str(), rows[i].kind,
                                dbgPage_ == rows[i].page, true))
                    dbgPage_ = rows[i].page;
            }
            return;
        }
        if (!compact) {
            if (!first) ImGui::Dummy(ImVec2(0, scaled(6)));
            ui::Caption(caption);
        } else if (!first) {
            ImGui::Dummy(ImVec2(0, scaled(4)));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), dbgCol(sem.border));
            ImGui::Dummy(ImVec2(0, scaled(4)));
        }
        for (int i = 0; i < n; ++i)
            if (ui::RailRow(rows[i].label, rows[i].icon, rows[i].count.c_str(), rows[i].kind,
                            dbgPage_ == rows[i].page, compact))
                dbgPage_ = rows[i].page;
    };
    group("Logic", logic, 4, true);
    group("World", world, 2, false);
    group("Performance", perf, 4, false);
    if (horizontal) return;

    // Armed countdowns, at the foot of the rail: a Delay only advances on
    // frames that RUN, so a halted game (or a single-frame Fire) leaves the
    // branch behind it looking dead. Saying so here is cheaper than a doc.
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    if (!compact && live && !dbgSnap_.timers.empty()) {
        const float fps = dbgFps_ > 1.0f ? dbgFps_ : 50.0f;
        int soonest = 1 << 30, soonestKey = -1;
        for (const auto& t : dbgSnap_.timers)
            if (t.second < soonest) {
                soonest = t.second;
                soonestKey = t.first;
            }
        const float cardH = ImGui::GetTextLineHeightWithSpacing() * 3.6f;
        const float y = ImGui::GetWindowHeight() - cardH - ImGui::GetStyle().WindowPadding.y;
        if (y > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(y);
        ui::BeginCard("##dbgtimers", ImVec2(0, cardH));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float fs = ImGui::GetFontSize();
        ui::DrawRtIcon(ImGui::GetWindowDrawList(), ui::RtIcon::Timer,
                       ImVec2(p.x + fs * 0.5f, p.y + ImGui::GetTextLineHeight() * 0.5f), fs * 0.85f,
                       dbgCol(sem.warn));
        ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 1.3f, p.y));
        ImGui::Text("%d armed timer%s", (int)dbgSnap_.timers.size(),
                    dbgSnap_.timers.size() == 1 ? "" : "s");
        std::string who = "Delay";
        if (const livedbg::NodeSym* n = dbgSyms_.find(soonestKey)) who = dbgNodeLabel(*n);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(sem.textDim, "%s fires in %.1f s.%s", who.c_str(), soonest / fps,
                           dbgState_ == DbgState::Halted ? " Frozen - Continue to let it count." : "");
        ImGui::PopTextWrapPos();
        ui::EndCard();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Delay nodes counting down. They only advance while the game runs:\n"
                "after a one-frame Fire, or while stopped, the branch after a Delay\n"
                "never comes.");
    }
}

// Not running: what is missing, in order, with the fix next to it.
void App::dbgDrawChecklist() {
    const theme::Semantics& sem = theme::semantics();
    ui::Caption("Not running");
    const bool debug = project_.settings.buildProfile == "debug";
    const bool on = project_.settings.liveDebug;
    const bool built = dbgSyms_.loaded;
    const bool stale = dbgState_ == DbgState::Stale;
    auto item = [&](bool ok, const char* text, const char* detail) {
        const float fs = ImGui::GetFontSize();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + fs * 0.5f, p.y + ImGui::GetTextLineHeight() * 0.5f);
        if (ok) {
            dl->AddCircleFilled(c, fs * 0.5f, dbgCol(sem.ok, 0.16f), 16);
            ui::DrawRtIcon(dl, ui::RtIcon::Check, c, fs * 0.62f, dbgCol(sem.ok));
        } else {
            dl->AddCircle(c, fs * 0.45f, dbgCol(sem.textDim, 0.7f), 16, 1.3f);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 1.4f, p.y));
        ImGui::BeginGroup();
        if (ok) ImGui::TextUnformatted(text);
        else ImGui::TextColored(sem.text, "%s", text);
        if (detail && *detail) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(sem.textDim, "%s", detail);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetCursorScreenPos().y));
    };
    item(debug, "Debug build profile",
         debug ? nullptr : "A release build carries no debugger (the devkit's zero-cost rule).");
    if (!debug) {
        ImGui::Indent(ImGui::GetFontSize() * 1.4f);
        if (ImGui::Button("Switch to debug")) {
            project_.settings.buildProfile = "debug";
            commitChange();
        }
        ImGui::Unindent(ImGui::GetFontSize() * 1.4f);
    }
    item(on, "Live Debugger on", on ? nullptr : "Project > Preferences > Build.");
    if (!on) {
        ImGui::Indent(ImGui::GetFontSize() * 1.4f);
        if (ui::Switch("Live Debugger (project setting)", &project_.settings.liveDebug))
            commitChange();
        ImGui::Unindent(ImGui::GetFontSize() * 1.4f);
    }
    item(built && !stale,
         stale ? "Built from these graphs" : "Built with symbols",
         !built ? "Codegen writes src/gen/livedbg.sym on the first build."
         : stale ? "The running game was built from different graphs." : nullptr);
    std::string why = dbgSilenceReason();
    item(false, "Game reporting",
         dbgState_ == DbgState::Waiting && !why.empty() ? why.c_str() : "Nothing is reporting yet.");
    if (dbgState_ == DbgState::Waiting && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", dbgSilenceDetail());
    ImGui::Spacing();
    const bool busy = runner_.busy();
    if (ui::PrimaryButton(busy ? "Building...##dbgrun" : "Build & Run (F5)", -1.0f, !busy && debug))
        dbgBuildAndRun();
}

// Halted: where, which breakpoint, what the node did, what ran this frame.
void App::dbgDrawStoppedAt(bool showPath) {
    const theme::Semantics& sem = theme::semantics();
    const bool halted = dbgState_ == DbgState::Halted;
    const livedbg::NodeSym* n = halted ? dbgSyms_.find(dbgSnap_.breakKey) : nullptr;
    if (halted) {
        ui::Caption("Stopped at");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(sem.warn.x, sem.warn.y, sem.warn.z, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(sem.warn.x, sem.warn.y, sem.warn.z, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ImGui::GetFontSize() * 0.8f, ImGui::GetFontSize() * 0.7f));
        ImGui::BeginChild("##stopped", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        const float fs = ImGui::GetFontSize();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(p.x + fs * 0.4f, p.y + fs * 0.75f), fs * 0.55f, dbgCol(sem.warn, 0.18f), 16);
        dl->AddCircleFilled(ImVec2(p.x + fs * 0.4f, p.y + fs * 0.75f), fs * 0.32f, dbgCol(sem.warn), 16);
        ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 1.3f, p.y));
        ImGui::BeginGroup();
        if (n) {
            const FlowNodeType* t = flowNodeType(n->type);
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted(t ? t->title : n->type.c_str());
            }
            std::string obj = dbgNodeLabel(*n);
            obj = obj.substr(0, obj.find(" \xc2\xb7 "));
            std::string scene = n->scene >= 0 && n->scene < (int)project_.scenes.size()
                                    ? project_.scenes[n->scene].name
                                    : std::to_string(n->scene);
            ImGui::TextColored(sem.textDim, "on %s \xc2\xb7 scene %s", obj.c_str(), scene.c_str());
        } else {
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted("Paused");
            }
            ImGui::TextColored(sem.textDim, "by you, at frame %s", dbgFrameText(dbgSnap_.frame).c_str());
        }
        ImGui::EndGroup();
        if (n) {
            // What the node DID, when the game said so: a World Fact write
            // carries the node that made it (FactEvent::src), so a Set Fact
            // breakpoint reads as "name <- value", not as a node title.
            std::vector<const livedbg::VarSym*> factSlots;
            for (const livedbg::VarSym& v : dbgSyms_.vars)
                if (v.kind == 'f') factSlots.push_back(&v);
            for (const livedbg::VarSym& v : dbgSyms_.vars)
                if (v.kind == 'F') factSlots.push_back(&v);
            std::string did;
            for (const livedbg::FactEvent& e : dbgSnap_.factEvents)
                if (e.src == dbgSnap_.breakKey && e.slot >= 0 && e.slot < (int)factSlots.size()) {
                    const livedbg::VarSym& v = *factSlots[(size_t)e.slot];
                    std::vector<float> one = {e.value, 0.0f, 0.0f};
                    int tone = 0;
                    did = v.name + " \xe2\x86\x90 " + dbgValueText(project_, v, one, 0, &tone);
                }
            if (!did.empty()) {
                ImGui::Spacing();
                ui::FontScope mono(ui::monoFont());
                ImGui::PushStyleColor(ImGuiCol_FrameBg, sem.field);
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s", did.c_str());
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##stopdid", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(sem.textDim, "The node has run - a breakpoint stops at the end of its frame.");
            ImGui::PopTextWrapPos();
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Reveal", ImVec2(half, 0))) dbgRevealNode(*n);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Open this node's graph in the Flow Graph window.");
            ImGui::SameLine();
            const FlowNodeType* t = flowNodeType(n->type);
            if (t && t->trigger) {
                if (ImGui::Button("Fire & continue", ImVec2(half, 0))) dbgFireNode(dbgSnap_.breakKey, true);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Fire this trigger once more and let the game run,\n"
                                      "so a Delay in its branch gets frames to finish.");
            } else if (ImGui::Button("Remove breakpoint", ImVec2(half, 0))) {
                dbgToggleBreakpoint(n->objectId, n->nodeId);
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
    }

    // The path a frame took: the inspected frame on the strip, else the newest.
    const auto& frames = dbgTimeline_.frames();
    if (showPath && !frames.empty()) {
        const int idx = dbgScrub_ >= 0 ? ImClamp(dbgScrub_, 0, (int)frames.size() - 1) : (int)frames.size() - 1;
        const auto& fr = frames[(size_t)idx];
        const std::string cap = dbgScrub_ >= 0 ? "Frame " + dbgFrameText(fr.frame) + "'s path"
                                               : std::string(halted ? "This frame's path" : "Last frame's path");
        ui::Caption(cap.c_str());
        const float fs = ImGui::GetFontSize();
        int shown = 0;
        for (int k : fr.keys) {
            if (++shown > 12) {
                ImGui::TextDisabled("... %d more", (int)fr.keys.size() - 12);
                break;
            }
            const livedbg::NodeSym* s = dbgSyms_.find(k);
            if (!s) continue;
            ImGui::PushID(shown);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool isBreak = halted && k == dbgSnap_.breakKey;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, p.y + ImGui::GetTextLineHeight() * 0.5f - fs * 0.25f),
                ImVec2(p.x + fs * 0.5f, p.y + ImGui::GetTextLineHeight() * 0.5f + fs * 0.25f),
                dbgCol(isBreak ? sem.warn : sem.ok), 2.0f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.9f, p.y));
            char hits[24] = "";
            if (k >= 0 && k < (int)dbgSnap_.hits.size()) std::snprintf(hits, sizeof(hits), "%u", dbgSnap_.hits[k]);
            const float hw = ImGui::CalcTextSize(hits).x;
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable(dbgNodeLabel(*s).c_str(), false, 0,
                                  ImVec2(ImGui::GetContentRegionAvail().x - hw - fs * 0.5f, 0)))
                dbgRevealNode(*s);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - hw);
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::TextColored(sem.textDim, "%s", hits);
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    if (halted && !project_.debugBreakpoints.empty()) {
        ui::Caption("Breakpoints");
        dbgDrawBreakpointList(true);
    }
}

// The breakpoint rows, shared by the Breakpoints page and the Stopped card.
void App::dbgDrawBreakpointList(bool compactRows) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    for (size_t i = 0; i < project_.debugBreakpoints.size(); ++i) {
        const std::string b = project_.debugBreakpoints[i];
        const size_t colon = b.rfind(':');
        if (colon == std::string::npos) continue;
        const std::string objId = b.substr(0, colon);
        const int nodeId = std::atoi(b.c_str() + colon + 1);
        const livedbg::NodeSym* sym = nullptr;
        for (const livedbg::NodeSym& n : dbgSyms_.nodes)
            if (n.objectId == objId && n.nodeId == nodeId) {
                sym = &n;
                break;
            }
        ImGui::PushID((int)i);
        const bool hit = sym && dbgState_ == DbgState::Halted && sym->key == dbgSnap_.breakKey;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + fs * 0.3f, p.y + ImGui::GetFrameHeight() * 0.5f), fs * 0.25f,
            dbgCol(hit ? sem.warn : sem.danger), 12);
        ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.9f, p.y));
        char tail[32] = "";
        if (hit) std::snprintf(tail, sizeof(tail), "hit");
        else if (sym && sym->key < (int)dbgSnap_.hits.size()) std::snprintf(tail, sizeof(tail), "%u", dbgSnap_.hits[sym->key]);
        const float tw = ImGui::CalcTextSize(tail).x + (compactRows ? 0.0f : ImGui::GetFrameHeight() + fs * 0.6f);
        ImGui::SetNextItemAllowOverlap();
        const std::string label = sym ? dbgNodeLabel(*sym)
                                      : "node " + std::to_string(nodeId) + " of " + objId + " (not in this build)";
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImMax(1.0f, ImGui::GetContentRegionAvail().x - tw - fs * 0.5f),
                                     ImGui::GetFrameHeight())) &&
            sym)
            dbgRevealNode(*sym);
        ImGui::PopStyleVar();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(hit ? sem.warn : sem.textDim, "%s", tail);
        bool removed = false;
        if (!compactRows) {
            ImGui::SameLine();
            removed = ui::IconButton("Remove breakpoint", ui::RtIcon::Close, 0.0f);
        }
        ImGui::PopID();
        if (removed) {
            project_.debugBreakpoints.erase(project_.debugBreakpoints.begin() + (long)i);
            dbgCmdWritten_ = false;
            setDirty(true);
            break;
        }
    }
}

void App::dbgDrawWatchPage() {
    const theme::Semantics& sem = theme::semantics();
    const std::vector<float>* vals = &dbgSnap_.vars;
    const auto& frames = dbgTimeline_.frames();
    uint32_t asOf = dbgSnap_.frame;
    if (dbgScrub_ >= 0 && dbgScrub_ < (int)frames.size() && !frames[dbgScrub_].vars.empty()) {
        vals = &frames[dbgScrub_].vars;
        asOf = frames[dbgScrub_].frame;
    }
    if (!dbgSyms_.loaded) {
        ui::EmptyState(ui::Glyph::Eye, "Nothing to watch yet",
                       "The watch list comes from the first debug build's symbol table.");
        return;
    }
    if (dbgSyms_.vars.empty()) {
        ui::EmptyState(ui::Glyph::Eye, "No variables in this project",
                       "Variables nodes (Set/Get Int, Bool, Position), save values and World Facts "
                       "show up here automatically.");
        return;
    }
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    auto classOf = [](char k) { return (k == 'f' || k == 'F') ? 1 : (k == 's' ? 2 : 3); };
    const uint32_t newest = dbgSnap_.frame;
    auto justChanged = [&](size_t i) {
        return live && dbgScrub_ < 0 && i < dbgVarChanged_.size() && dbgVarChanged_[i] != 0 &&
               dbgVarChanged_[i] == newest;
    };
    int counts[5] = {0, 0, 0, 0, 0};
    for (size_t i = 0; i < dbgSyms_.vars.size(); ++i) {
        ++counts[0];
        ++counts[classOf(dbgSyms_.vars[i].kind)];
        if (justChanged(i)) ++counts[4];
    }
    // The filter: a search box (name and kind, "marta", "bool") and the chips
    // that answer "which kind" and "what did the last frame change".
    {
        char fbuf[128];
        std::snprintf(fbuf, sizeof(fbuf), "%s", dbgWatchFilter_.c_str());
        ImGui::SetNextItemWidth(ImMin(scaled(260), ImGui::GetContentRegionAvail().x));
        if (ImGui::InputTextWithHint("##watchfilter", "Filter by name or kind", fbuf, sizeof(fbuf)))
            dbgWatchFilter_ = fbuf;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Matches the name and the kind: \"marta\", \"bool\", \"save\".");
        static const char* const kChips[5] = {"All", "Facts", "Save values", "Flow variables", "Just changed"};
        for (int c = 0; c < 5; ++c) {
            if (c == 4 && counts[4] == 0 && dbgWatchKind_ != 4) continue;
            const float w = ui::FilterChipWidth(kChips[c], counts[c]);
            ui::ChipFlow(w, false);
            if (c == 4) ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
            if (ui::FilterChip(kChips[c], dbgWatchKind_ == c, counts[c])) dbgWatchKind_ = c;
            if (c == 4) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Changed between the previous report and this one.");
            }
        }
        if (live) {
            const std::string t = "Values as of frame " + dbgFrameText(asOf);
            const float tw = ImGui::CalcTextSize(t.c_str()).x;
            ImGui::SameLine();
            if (ImGui::GetCursorPosX() + tw > ImGui::GetContentRegionMax().x) ImGui::NewLine();
            ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - tw));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(dbgScrub_ >= 0 ? sem.warn : sem.textDim, "%s", t.c_str());
        }
    }
    const std::string needle = lower(dbgWatchFilter_);
    auto matches = [&](size_t i) {
        const livedbg::VarSym& v = dbgSyms_.vars[i];
        if (dbgWatchKind_ >= 1 && dbgWatchKind_ <= 3 && classOf(v.kind) != dbgWatchKind_) return false;
        if (dbgWatchKind_ == 4 && !justChanged(i)) return false;
        if (needle.empty()) return true;
        return lower(v.name + " " + dbgKindText(v.kind)).find(needle) != std::string::npos;
    };
    int shown = 0;
    for (size_t i = 0; i < dbgSyms_.vars.size(); ++i)
        if (matches(i)) ++shown;
    if (shown == 0) {
        // Say so rather than showing an empty table, which reads as "the game
        // stopped reporting".
        ImGui::TextDisabled("Nothing matches.");
        return;
    }
    ImGui::Spacing();
    // A narrow dock drops the Kind column (the filter chips still say it):
    // the name is what has to fit.
    const bool showKind = ImGui::GetContentRegionAvail().x >= scaled(460);
    if (ImGui::BeginTable("##watch", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 0.13f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableSetupColumn("Changed", ImGuiTableColumnFlags_WidthStretch, 0.13f);
        ImGui::TableSetColumnEnabled(1, showKind);
        ImGui::TableHeadersRow();
        // The mono face must be popped before EndTable (the table is a child
        // window), hence the scope.
        ImGui::PushFont(ui::monoFont(), 0.0f);
        for (size_t i = 0; i < dbgSyms_.vars.size(); ++i) {
            if (!matches(i)) continue;
            const livedbg::VarSym& v = dbgSyms_.vars[i];
            const bool hot = justChanged(i);
            ImGui::TableNextRow();
            if (hot) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, dbgCol(sem.warn, 0.08f));
            ImGui::TableSetColumnIndex(0);
            // The leaf in the text colour, its namespace dim: a fact catalog
            // is "world.generator.partA" forty times over, and the part that
            // differs is the last one.
            const size_t dot = v.name.rfind('.');
            if (hot) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(p.x - ImGui::GetFontSize() * 0.45f, p.y + ImGui::GetTextLineHeight() * 0.5f),
                    ImGui::GetFontSize() * 0.17f, dbgCol(sem.warn), 8);
            }
            if (dot != std::string::npos) {
                ImGui::TextColored(sem.textDim, "%.*s", (int)dot + 1, v.name.c_str());
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(v.name.c_str() + dot + 1);
            } else {
                ImGui::TextUnformatted(v.name.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(sem.textDim, "%s", dbgKindText(v.kind));
            ImGui::TableSetColumnIndex(2);
            int tone = 0;
            const std::string val = live || dbgScrub_ >= 0 ? dbgValueText(project_, v, *vals, i, &tone) : "-";
            if (!live && dbgScrub_ < 0) tone = 2;
            const ImVec4 col = tone == 1 ? sem.ok : tone == 2 ? sem.textDim : tone == 3 ? sem.warn : sem.text;
            ImGui::TextColored(col, "%s", val.c_str());
            ImGui::TableSetColumnIndex(3);
            const uint32_t ch = i < dbgVarChanged_.size() ? dbgVarChanged_[i] : 0;
            const std::string chs = ch ? dbgFrameText(ch) : "-";
            const float cw = ImGui::CalcTextSize(chs.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - cw));
            ImGui::TextColored(hot ? sem.warn : sem.textDim, "%s", chs.c_str());
            if (ch && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip("Last changed in the report of frame %u.", ch);
            }
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

// Nodes: everything instrumented in the graph currently being edited, with its
// hit count, a breakpoint toggle and (for triggers) a Fire button.
void App::dbgDrawNodesPage() {
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    const auto& objs = project_.objects();
    const bool haveGraph = flowGraphObject_ >= 0 && flowGraphObject_ < (int)objs.size();
    if (!haveGraph) {
        ui::EmptyState(ui::Glyph::Nodes, "No graph open",
                       "Pick an object's graph in the Flow Graph window - its nodes show here with "
                       "their hit counts.");
        return;
    }
    ImGui::Text("Graph of \"%s\"", objs[flowGraphObject_].name.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("The graph open in the Flow Graph window. Right-clicking a node\n"
                          "there sets breakpoints and fires triggers too.");
    {
        int instrumented = 0;
        for (const FlowNode& n : objs[flowGraphObject_].flowGraph.nodes)
            if (const FlowNodeType* t = flowNodeType(n.type); t && !(t->pure && !t->trigger)) ++instrumented;
        if (instrumented == 0) {
            ImGui::TextDisabled("No triggers or actions in this graph - nothing here reports.");
            return;
        }
    }
    if (ImGui::BeginTable("##nodes", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Break", ImGuiTableColumnFlags_WidthFixed, scaled(44.0f));
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
            if (ImGui::Checkbox("##bp", &bp)) dbgToggleBreakpoint(ownerId, n.id);
            ImGui::TableSetColumnIndex(1);
            const float age = dbgNodeHeat(key);
            if (age < 0.35f)
                ImGui::TextColored(theme::semantics().warn, "%s", t->title);
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
                if (ImGui::SmallButton("Fire")) dbgFireNode(key, ImGui::GetIO().KeyShift);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(
                        "Runs everything wired to this trigger in the running game, once.\n"
                        "Shift+click = fire AND continue, so a Delay in the branch gets frames "
                        "to finish.");
            } else if (armed > 0) {
                // An armed countdown is the usual reason a branch looks dead.
                ImGui::TextColored(theme::semantics().accent, "%d f (%.1fs)", armed,
                                   armed / (dbgFps_ > 1.0f ? dbgFps_ : 50.0f));
            } else if (age < FLT_MAX) {
                ImGui::TextDisabled("%.1fs ago", age);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void App::dbgDrawBreakpointsPage() {
    if (project_.debugBreakpoints.empty()) {
        ui::EmptyState(ui::Glyph::Nodes, "No breakpoints",
                       "Right-click a node in the Flow Graph (or tick it on the Nodes page) to stop "
                       "the game the moment it runs.");
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%d of %d slots", (int)project_.debugBreakpoints.size(), livedbg::kMaxBreakpoints);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Clear all").x -
                    ImGui::GetStyle().FramePadding.x * 2.6f);
    if (ui::DangerButton("Clear all")) {
        project_.debugBreakpoints.clear();
        dbgCmdWritten_ = false;
        setDirty(true);
        return;
    }
    ImGui::Spacing();
    dbgDrawBreakpointList(false);
}

// Hot patches: what is currently hot-patched into the running game, and what
// still needs a rebuild (docs/live-logic.md).
void App::dbgDrawPatchesPage() {
    const theme::Semantics& sem = theme::semantics();
    if (!project_.settings.liveLogic) {
        ImGui::TextWrapped("Live Logic is off for this project - editing a graph needs a rebuild.");
        if (ui::Switch("Live Logic (project setting)", &project_.settings.liveLogic)) commitChange();
        return;
    }
    switch (liveLogicState_) {
        case LogicState::Off:
        case LogicState::OffStale:
            ImGui::TextDisabled("Release profile - graphs run as compiled C++ only.");
            break;
        case LogicState::NoBuild:
            ImGui::TextDisabled("No built-graph list yet.");
            prefHelp("Build & Run (F5) once; after that, editing a graph takes\n"
                     "effect in the running game without another build.");
            break;
        case LogicState::InSync:
            ui::StatusLine(0, "In sync with the build - nothing to patch.");
            ImGui::TextWrapped("Edit any graph and it is compiled and streamed into the running "
                               "game within a fraction of a second.");
            break;
        case LogicState::Patched: {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%d graph(s) running from the editor's patch",
                          liveLogicPatchCount_);
            ui::StatusLine(3, buf);
            ImGui::TextWrapped("Their compiled C++ stands down while the patch is live; a rebuild "
                               "folds the edits back into native code.");
            break;
        }
        case LogicState::Blocked:
            ui::StatusLine(1, "Rebuild needed");
            break;
    }
    if (!liveLogicBlocked_.empty()) {
        ImGui::Spacing();
        ui::Caption("Cannot be hot-patched");
        for (const auto& e : liveLogicBlocked_) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s - %s", e.first.c_str(), e.second.c_str());
        }
        ImGui::TextColored(sem.textDim, "Build & Run (F5) and everything is native again.");
    }
}

// Objects: what the game's own RuntimeObject holds for a watched object,
// sampled every frame - live values plus a curve per axis.
void App::dbgDrawObjectsPage() {
    const auto& objs = project_.objects();
    ImGui::BeginDisabled(selectedObject_ < 0 || selectedObject_ >= (int)objs.size() ||
                         ((int)dbgObjWatch_.size() >= livedbg::kMaxWatchObjects &&
                          !dbgIsWatched(selectedObject_)));
    if (ImGui::Button(dbgIsWatched(selectedObject_) ? "- Unwatch selected" : "+ Watch selected"))
        dbgToggleObjectWatch(selectedObject_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%d/%d watched", (int)dbgObjWatch_.size(), livedbg::kMaxWatchObjects);
    if (dbgObjWatch_.empty()) {
        ui::EmptyState(ui::Glyph::Box, "Nothing watched yet",
                       "Select an object and Watch it: the game reports its position, rotation, "
                       "scale, colour and flags every frame, and its path is drawn in the viewport.");
        return;
    }
    for (size_t i = 0; i < dbgObjWatch_.size(); ++i) {
        DbgObjTrack& t = dbgObjWatch_[i];
        ImGui::PushID((int)i);
        ImGui::Separator();
        const std::string label =
            (t.index >= 0 && t.index < (int)objs.size() ? objs[t.index].name : t.name) + "  (#" +
            std::to_string(t.index) + ")";
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ui::IconButton("Stop watching", ui::RtIcon::Close)) {
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
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::Text("pos   %.2f, %.2f, %.2f", s.pos[0], s.pos[1], s.pos[2]);
            ImGui::Text("rot   %.1f, %.1f, %.1f   scale %.2f, %.2f, %.2f", s.rot[0], s.rot[1], s.rot[2],
                        s.scale[0], s.scale[1], s.scale[2]);
            ImGui::Text("color %.2f, %.2f, %.2f", s.color[0], s.color[1], s.color[2]);
        }
        ImGui::SameLine();
        ImGui::TextColored(s.visible ? theme::semantics().ok : theme::semantics().textDim,
                           s.visible ? "visible" : "hidden");
        if (!s.active) {
            ImGui::SameLine();
            ImGui::TextColored(theme::semantics().warn, "inactive");
        }
        // One plot per axis, over the whole kept history.
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
            std::snprintf(overlay, sizeof(overlay), "%c  %.2f .. %.2f", "XYZ"[axis], lo, hi);
            // Per-axis id: three plots with one label are one ID to ImGui.
            const char* plotId = axis == 0 ? "##plotX" : axis == 1 ? "##plotY" : "##plotZ";
            ImGui::PlotLines(plotId, series.data(), n, 0, overlay, lo, hi, ImVec2(-FLT_MIN, scaled(38.0f)));
        }
        ImGui::TextDisabled("%d samples (%.1fs of history)", n, n / (dbgFps_ > 1.0f ? dbgFps_ : 50.0f));
        ImGui::PopID();
    }
}

// Screen: the picture the game took of ITSELF (docs/devkit.md, "The game's own
// screenshot") - it reads the frame buffer out of GS VRAM and hands over a TGA.
void App::dbgDrawScreenPage() {
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    if (!live) dbgShotWaiting_ = false;  // nobody left to answer
    if (ui::PrimaryButton("Capture frame", 0.0f, live)) {
        dbgCmd_.captureFrame = true;
        dbgCmdWritten_ = false;
        dbgShotWaiting_ = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(
            "Asks the game to read its last finished frame out of GS VRAM. Works on a\n"
            "real PlayStation 2, and on a locked or remote desktop where the emulator's\n"
            "own screenshot key cannot.\n\nKept as a PNG in the project's screenshots/ folder.\n\n"
            "One shot: ~900 KB over host:, so it costs the game a visible hitch -\nabout three "
            "seconds of it over ps2link.");
    if (dbgShotWaiting_) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("waiting for the game...");
    }
    if (dbgShotTex_) {
        ImGui::SameLine();
        // The SAVED PNG, never bin/frame.tga: the channel file is overwritten
        // by the next capture and deleted by every launch.
        if (ImGui::Button("Show file")) {
            std::error_code ec;
            if (!dbgShotFile_.empty() && std::filesystem::exists(dbgShotFile_, ec)) {
                dbgShotError_.clear();
                platform::revealInFileManager(dbgShotFile_);
            } else {
                dbgShotError_ = "the saved PNG is gone - capture again to write one.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy image")) {
            if (platform::copyImageToClipboard(dbgShotPixels_.data(), dbgShotW_, dbgShotH_, dbgShotFile_)) {
                dbgShotError_.clear();
                statusMessage_ = "Frame image copied to the clipboard";
            } else {
                dbgShotError_ = "could not put the frame image on the clipboard.";
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the actual bitmap, ready to paste into chat or an image editor.");
    }
    if (!dbgShotError_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::semantics().danger, "%s", dbgShotError_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!dbgShotTex_) {
        ui::EmptyState(ui::Glyph::Screen, "No capture yet",
                       "The frame comes back over the same host: channel as everything else here - "
                       "a real PlayStation 2 included - and is kept as a PNG in screenshots/.");
        return;
    }
    ImGui::TextDisabled("%dx%d - the frame buffer as the GS holds it", dbgShotW_, dbgShotH_);
    if (!dbgShotFile_.empty())
        ImGui::TextDisabled("screenshots/%s", std::filesystem::path(dbgShotFile_).filename().string().c_str());
    // Fit and keep the buffer's own aspect: this is the GS RASTER, not the TV.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale = dbgShotW_ > 0 ? ImMin(1.0f, avail.x / (float)dbgShotW_) : 1.0f;
    if (dbgShotH_ > 0 && avail.y > scaled(80)) scale = ImMin(scale, avail.y / (float)dbgShotH_);
    ImGui::Image((ImTextureID)(intptr_t)dbgShotTex_, ImVec2((float)dbgShotW_ * scale, (float)dbgShotH_ * scale));
}

// Stats: the frame's vital signs - tiles for the numbers people compare, a
// frame-time history against the budget, and the two memory budgets as meters.
void App::dbgDrawStatsPage() {
    const theme::Semantics& sem = theme::semantics();
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    const livedbg::Stats& st = dbgSnap_.stats;
    if (!live || !st.valid) {
        // Say WHICH silence: not booted, no runtime, or a dead file server.
        const std::string why = dbgSilenceReason();
        if (!why.empty())
            ui::EmptyState(ui::Glyph::Sliders, "No stats", why.c_str());
        else if (!live)
            ui::EmptyState(ui::Glyph::Sliders, "No stats yet",
                           "They arrive with the game's reports - run a debug build with the Live "
                           "Debugger on.");
        else
            ui::EmptyState(ui::Glyph::Sliders, "No stats block",
                           "This game was built before the Stats page existed. Rebuild it (F5 / F6).");
        return;
    }
    const float hz = project_.settings.videoSystem == "ntsc" ? 60.0f : 50.0f;
    const float budgetMs = 1000.0f / hz;
    const float rendered = st.fpsX10 > 0 ? st.fpsX10 / 10.0f : (float)st.fps;
    const float presented = st.presentedX10 > 0 ? st.presentedX10 / 10.0f : rendered;
    float worst = 0.0f, sum = 0.0f;
    for (float ms : dbgFrameMs_) {
        worst = ImMax(worst, ms);
        sum += ms;
    }
    const float avgMs = dbgFrameMs_.empty() ? (rendered > 0 ? 1000.0f / rendered : 0.0f)
                                            : sum / (float)dbgFrameMs_.size();

    // --- tiles
    {
        char v[5][32], s[5][64];
        std::snprintf(v[0], 32, "%.1f fps", rendered);
        std::snprintf(s[0], 64, "game loops / s");
        std::snprintf(v[1], 32, "%.1f fps", presented);
        std::snprintf(s[1], 64, presented > rendered * 1.15f ? "with frame extrapolation" : "buffer flips / s");
        std::snprintf(v[2], 32, "%.1f ms", avgMs);
        std::snprintf(s[2], 64, "worst %.1f \xc2\xb7 budget %.1f", worst, budgetMs);
        std::snprintf(v[3], 32, "%u", st.verts);
        std::snprintf(s[3], 64, "%d flushes \xc2\xb7 %u qw", st.flushes, st.qw);
        std::snprintf(v[4], 32, "%d / %d", st.objVisible, st.objects);
        std::snprintf(s[4], 64, "%d active", st.objActive);
        const char* labels[5] = {"Rendered", "Presented", "Frame time", "VU1 vertices", "Objects visible"};
        const ImU32 cols[5] = {dbgCol(rendered + 0.5f < hz ? sem.warn : sem.ok), dbgCol(sem.ok),
                               dbgCol(worst > budgetMs ? sem.warn : sem.text), 0, 0};
        const float gap = scaled(8);
        const float avail = ImGui::GetContentRegionAvail().x;
        const int perRow = avail > scaled(640) ? 5 : avail > scaled(380) ? 3 : 2;
        const float w = (avail - gap * (perRow - 1)) / perRow;
        for (int i = 0; i < 5; ++i) {
            if (i % perRow) ImGui::SameLine(0.0f, gap);
            ui::KpiTile(labels[i], v[i], s[i], cols[i], w);
            if (i == 1 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "Measured by the game itself: rendered = game loops per second,\n"
                    "presented = buffer flips. Frame extrapolation synthesises a frame\n"
                    "per rendered one, so it shows about twice as often as it renders.\n"
                    "Neither is the frame-timing rig, which measures sub-frame WORK in\n"
                    "milliseconds (docs/profiling.md).");
        }
    }
    ImGui::Spacing();

    // --- frame time over the kept reports, against the budget line
    ui::BeginCard("##frametime", ImVec2(0, 0));
    {
        dbgCaps("Frame time");
        ImGui::SameLine();
        ImGui::TextDisabled("budget %.1f ms \xc2\xb7 %s %.0f Hz", budgetMs, hz > 55.0f ? "NTSC" : "PAL", hz);
        const float h = scaled(70);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("Frame time chart", ImVec2(ImMax(1.0f, w), h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float top = ImMax(budgetMs * 1.5f, worst * 1.1f);
        const float by = p.y + h - h * (budgetMs / top);
        const int n = (int)dbgFrameMs_.size();
        const int cap = (int)kDbgFrameMsKeep;
        const float bw = w / (float)cap;
        int hover = -1;
        for (int i = 0; i < n; ++i) {
            const float ms = dbgFrameMs_[(size_t)i];
            const float x = p.x + w - (n - i) * bw;
            const float bh = ImMax(1.0f, h * ImMin(1.0f, ms / top));
            dl->AddRectFilled(ImVec2(x, p.y + h - bh), ImVec2(x + ImMax(1.0f, bw - 1.0f), p.y + h),
                              dbgCol(ms > budgetMs ? sem.danger : sem.ok, 0.85f), 1.0f);
            if (ImGui::IsItemHovered() && ImGui::GetMousePos().x >= x && ImGui::GetMousePos().x < x + bw) hover = i;
        }
        for (float x = p.x; x < p.x + w; x += 8.0f)
            dl->AddLine(ImVec2(x, by), ImVec2(ImMin(x + 4.0f, p.x + w), by), dbgCol(sem.danger, 0.5f));
        if (hover >= 0)
            ImGui::SetTooltip("%.1f ms (%.1f fps rendered)", dbgFrameMs_[(size_t)hover],
                              1000.0f / dbgFrameMs_[(size_t)hover]);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("One bar per report: the game's own rendered rate as milliseconds a frame.");
        ImGui::PopTextWrapPos();
    }
    ui::EndCard();
    ImGui::Spacing();

    // --- the two memory budgets
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool two = avail > scaled(560);
    const float cardW = two ? (avail - scaled(8)) * 0.5f : avail;
    ui::BeginCard("##vram", ImVec2(cardW, 0));
    {
        const float freeMB = st.vramFreeKB / 1024.0f;
        const float usedMB = ImMax(0.0f, 4.0f - freeMB);
        dbgCaps("GS VRAM");
        ImGui::SameLine();
        {
            ui::FontScope mono(ui::monoFont());
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.2f / 4.00 MB", usedMB);
            ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(),
                                       ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(buf).x));
            ImGui::TextUnformatted(buf);
        }
        const float lowUsed = ImMax(0.0f, 4.0f - st.vramMinFreeKB / 1024.0f);
        const float fr[2] = {usedMB / 4.0f, ImMax(0.0f, lowUsed - usedMB) / 4.0f};
        const ImU32 cl[2] = {dbgCol(sem.accent), dbgCol(sem.accent, 0.35f)};
        ui::Meter(fr, cl, 2);
        ui::LegendSwatch(dbgCol(sem.accent), "in use now");
        ImGui::SameLine(0.0f, scaled(14));
        ui::LegendSwatch(dbgCol(sem.accent, 0.35f), "peak (low-water)");
        ImGui::TextDisabled("largest free block %u KB \xc2\xb7 %d textures resident (peak %d)", st.vramLargestKB,
                            st.vramResident, st.vramPeak);
        ui::FontScope mono(ui::monoFont());
        ImGui::TextDisabled("%u binds \xc2\xb7 %u hits \xc2\xb7 %u uploads \xc2\xb7 %u evictions", st.vramBinds,
                            st.vramHits, st.vramUploads, st.vramEvictions);
        if (st.vramEvictions) {
            ImGui::TextColored(sem.warn, "Evictions: the working set does not fit, textures re-upload.");
        }
    }
    ui::EndCard();
    if (two) ImGui::SameLine(0.0f, scaled(8));
    ui::BeginCard("##eeram", ImVec2(cardW, 0));
    {
        dbgCaps("EE memory");
        ImGui::SameLine();
        const char* btn = "Measure now";
        ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(),
                                   ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(btn).x -
                                       ImGui::GetStyle().FramePadding.x * 2.0f));
        if (ImGui::SmallButton(btn)) {
            dbgCmd_.measureRam = true;
            dbgCmdWritten_ = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "On request only. The engine measures free RAM by allocating every free block\n"
                "until malloc fails and then freeing the chain - honest, but not something to\n"
                "run once a frame.");
        // ramFrame is the "it answered" signal: zero KB with a frame behind it
        // is a FAILED measurement, not "not measured yet".
        if (st.ramFreeKB) {
            const float freeMB = st.ramFreeKB / 1024.0f;
            const float fr[1] = {ImClamp((32.0f - freeMB) / 32.0f, 0.0f, 1.0f)};
            const ImU32 cl[1] = {dbgCol(sem.warn)};
            ui::Meter(fr, cl, 1);
            ImGui::TextDisabled("%.2f MB free of 32 MB \xc2\xb7 measured at frame %s", freeMB,
                                dbgFrameText(st.ramFrame).c_str());
        } else if (st.ramFrame) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(sem.warn,
                               "Measured at frame %u and found 0 MB free - the game is out of memory "
                               "or the probe failed.",
                               st.ramFrame);
            ImGui::PopTextWrapPos();
        } else {
            const float fr[1] = {0.0f};
            const ImU32 cl[1] = {dbgCol(sem.warn)};
            ui::Meter(fr, cl, 1);
            ImGui::TextDisabled("not measured yet");
        }
        ui::FontScope mono(ui::monoFont());
        ImGui::TextDisabled("%d objects \xc2\xb7 %d active \xc2\xb7 %d visible", st.objects, st.objActive,
                            st.objVisible);
        if (st.maxChunkVerts) ImGui::TextDisabled("largest VU1 stream %d verts", st.maxChunkVerts);
    }
    ui::EndCard();
}

void App::dbgDrawRenderCostPage() {
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    if (dbgRenderCostProject_ != project_.dir) {
        dbgRenderCostProject_ = project_.dir;
        dbgRenderCost_ = {};
        dbgRenderBaseline_ = {};
        dbgRenderCostWaiting_ = false;
        dbgRenderCostSeq_ = 0;
    }
    if (ui::PrimaryButton("Measure render cost", 0.0f, live && !dbgRenderCostWaiting_)) {
        dbgCmd_.captureRenderCost = true;
        dbgCmdWritten_ = false;
        dbgRenderCostWaiting_ = true;
        dbgRenderCostSeq_ = 0;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(
            "On-demand render-pass attribution. Synchronizes VU/GS between stages, so the\n"
            "measured pass is slower than normal. It excludes update, vsync and file transfer.");
    if (dbgRenderCostWaiting_) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Waiting for the game...");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel measurement")) dbgRenderCostWaiting_ = false;
    }
    const double costNow = ImGui::GetTime();
    if (dbgRenderCostWaiting_ && dbgRenderCostSeq_ && costNow >= dbgRenderCostPoll_) {
        dbgRenderCostPoll_ = costNow + 0.5;
        livedbg::RenderCost result;
        if (livedbg::readRenderCost((std::filesystem::path(project_.dir) / "bin" / "rendercost.txt").string(),
                                    result) &&
            result.seq == dbgRenderCostSeq_) {
            dbgRenderCost_ = std::move(result);
            dbgRenderCostWaiting_ = false;
        }
    }
    if (!dbgRenderCost_.seq) {
        ui::EmptyState(ui::Glyph::Sliders, "No measurement yet",
                       "Measure one synchronized render pass of the running game - phases first, "
                       "then every drawn object. Needs a debug game built with 1.80 or later.");
        return;
    }
    ImGui::Text("Synchronized render pass: %.3f ms (scene %d)", dbgRenderCost_.totalMs, dbgRenderCost_.scene);
    if (dbgRenderBaseline_.seq && dbgRenderBaseline_.scene == dbgRenderCost_.scene) {
        ImGui::SameLine();
        ImGui::TextDisabled("total delta %+.3f ms", dbgRenderCost_.totalMs - dbgRenderBaseline_.totalMs);
    }
    if (ImGui::Button("Keep as baseline")) dbgRenderBaseline_ = dbgRenderCost_;
    ImGui::SameLine();
    if (ImGui::Button("Copy render cost CSV"))
        ImGui::SetClipboardText(livedbg::renderCostCsv(dbgRenderCost_).c_str());
    ImGui::SameLine();
    // The Obj_* rows are ~15 per drawn object: the answer to "why is THIS
    // object dear", and noise in every other reading.
    ui::Switch("Per-object pipeline detail", &dbgRenderCostDetail_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Adds each object's own counters (Obj_ rows): EE time of its\n"
                          "main and companion passes, bounds/prepare/dispatch/DMA, and\n"
                          "package counts (cull/guard/clip/outside, as whole numbers).");
    ImGui::TextDisabled("Object rows belong to Objects. Engine counters overlap phases; do not sum them.");
    // Resolve the label and the baseline delta ONCE, before sorting: both are
    // what the table shows and sorts on.
    struct CostView {
        std::string label;
        double ms = 0;
        double delta = 0;
        bool hasDelta = false;
        bool object = false;
    };
    const bool haveBase = dbgRenderBaseline_.seq && dbgRenderBaseline_.scene == dbgRenderCost_.scene;
    std::vector<CostView> view;
    view.reserve(dbgRenderCost_.rows.size());
    for (const auto& row : dbgRenderCost_.rows) {
        const bool detail = row.label.rfind("Obj_", 0) == 0;
        if (detail && !dbgRenderCostDetail_) continue;
        CostView v;
        v.ms = row.ms;
        v.object = row.object >= 0;
        std::string label = row.label;
        std::replace(label.begin(), label.end(), '_', ' ');
        if (v.object) {
            label = "Object #" + std::to_string(row.object);
            const int si = dbgRenderCost_.scene;
            if (si >= 0 && si < (int)project_.scenes.size() && row.object < (int)project_.scenes[si].objects.size())
                label += " " + project_.scenes[si].objects[row.object].name;
            if (detail) {
                std::string stage = row.label.substr(4);
                std::replace(stage.begin(), stage.end(), '_', ' ');
                label += " - " + stage;
            }
        }
        v.label = std::move(label);
        if (haveBase)
            for (const auto& old : dbgRenderBaseline_.rows)
                if (old.object == row.object && old.label == row.label) {
                    v.delta = row.ms - old.ms;
                    v.hasDelta = true;
                    break;
                }
        view.push_back(std::move(v));
    }
    if (ImGui::BeginTable("Render cost results", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable |
                              ImGuiTableFlags_SortTristate | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Stage / object");
        // Both cost columns prefer descending - "what is dearest" - but neither
        // carries DefaultSort: an untouched capture reads in its grouping.
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Delta ms", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableHeadersRow();
        int sortCol = -1;
        bool sortAsc = false;
        if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
            if (specs->SpecsCount > 0) {
                sortCol = specs->Specs[0].ColumnIndex;
                sortAsc = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            }
        std::stable_sort(view.begin(), view.end(), [&](const CostView& a, const CostView& b) {
            // Tristate: a third click clears the sort - phases first and
            // dearest first, objects after them.
            if (sortCol < 0) {
                if (a.object != b.object) return !a.object;
                return a.ms > b.ms;
            }
            if (sortCol == 0) {
                std::string la = a.label, lb = b.label;
                auto lower = [](std::string& t) {
                    std::transform(t.begin(), t.end(), t.begin(),
                                   [](unsigned char c) { return (char)std::tolower(c); });
                };
                lower(la);
                lower(lb);
                if (la == lb) return false;
                return sortAsc ? la < lb : lb < la;
            }
            if (sortCol == 2) {
                // A row the baseline does not carry sorts last both ways.
                if (a.hasDelta != b.hasDelta) return a.hasDelta;
                if (!a.hasDelta || a.delta == b.delta) return false;
                return sortAsc ? a.delta < b.delta : a.delta > b.delta;
            }
            if (a.ms == b.ms) return false;
            return sortAsc ? a.ms < b.ms : a.ms > b.ms;
        });
        for (const auto& v : view) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(v.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", v.ms);
            ImGui::TableSetColumnIndex(2);
            if (v.hasDelta) ImGui::Text("%+.3f", v.delta);
        }
        ImGui::EndTable();
    }
}

// VU memory: what the EE actually fed VU1 for one draw - the DMA chain decoded,
// and the vertex stream it carried drawn as a wireframe. VU1 debugging is
// otherwise blind (no printf, output straight to the GS), and this is the half
// we own (docs/devkit.md).
void App::dbgDrawVuPage() {
const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
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
}

// The loudest thing in the panel, because it is the most important: a real EE
// exception, or a heartbeat that simply stopped. Drawn across the top of the
// window, above the rail and the page.
void App::dbgDrawCrashBanner() {
if (!dbgCrash_.present && !dbgLostGame_) return;
const theme::Semantics& sem = theme::semantics();
ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(sem.danger.x, sem.danger.y, sem.danger.z, 0.07f));
ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(sem.danger.x, sem.danger.y, sem.danger.z, 0.4f));
ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 3.0f);
ImGui::BeginChild("##dbgcrash", ImVec2(0, 0),
                  ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
ImGui::PopStyleVar();
ImGui::PopStyleColor(2);
if (dbgCrash_.present) {
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
            runner_.buildAndRunPs2(projectForBuild(), false, false,
                                   project_.activeScene);
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
                                      dbgNodeLabel(*n).c_str());
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.7f, 0.35f, 1.0f));
    char lost[96];
    std::snprintf(lost, sizeof(lost),
                  "The game stopped reporting at frame %u - it hung.",
                  dbgLostAtFrame_);
    textWrappedHelp(
        lost,
        "No crash report and no assertion came with it: either a real\n"
        "hang, or an exception taken with the EE crash handler off.");
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
                                      dbgNodeLabel(*n).c_str());
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
ImGui::EndChild();
ImGui::Spacing();
}

// The one time strip: node fires per frame (the old Timeline), the frame being
// inspected, where the WORLD is (the time machine), and the input recorder.
// Three tabs with three sliders over the same frame axis used to answer these
// separately; here "I am looking at frame N, the world is at M" is one picture.
void App::dbgDrawTimeStrip(bool open) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const auto& frames = dbgTimeline_.frames();
    const float hz = project_.settings.videoSystem == "ntsc" ? 60.0f : 50.0f;
    const bool debug = project_.settings.buildProfile == "debug";

    // --- the header row
    ImGui::AlignTextToFramePadding();
    dbgCaps("Time");
    // The tail of the row (recorder state, Replay, Rewind, collapse) is
    // measured first: on a narrow dock the description gives way to it.
    std::string rec;
    int recKind = 4;
    if (replayHaveStatus_ && replayStatus_.mode == livereplay::Status::Record && !replayStatus_.done) {
        const int secs = (int)(replayStatus_.frame / hz);
        char b[48];
        std::snprintf(b, sizeof(b), "REC input %02d:%02d", secs / 60, secs % 60);
        rec = b;
        recKind = 2;
    } else if (replayHaveStatus_ && replayStatus_.mode == livereplay::Status::Replay) {
        char b[96];
        if (replayStatus_.divergences)
            std::snprintf(b, sizeof(b), "PLAY %u / %u \xc2\xb7 %u divergences", replayStatus_.frame,
                          replayStatus_.total, replayStatus_.divergences);
        else
            std::snprintf(b, sizeof(b), "PLAY %u / %u%s", replayStatus_.frame, replayStatus_.total,
                          replayStatus_.done ? " \xc2\xb7 exact" : "");
        rec = b;
        recKind = replayStatus_.divergences ? 1 : 3;
    } else if (replayArm_ == ReplayArm::Record) {
        rec = "REC armed - next run";
    } else if (replayArm_ == ReplayArm::Play && !replayFile_.empty()) {
        rec = "PLAY armed - next run";
    }
    const char* rewindLabel = "Rewind world";
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float fpx = ImGui::GetStyle().FramePadding.x * 2;
    float tail = ImGui::GetFrameHeight() + sp;
    tail += ImGui::CalcTextSize("Replay...").x + fpx + sp;
    tail += ImGui::CalcTextSize(rewindLabel).x + fpx + sp;
    if (!rec.empty()) tail += ui::StatePillWidth(rec.c_str()) + sp;
    else if (!open) tail += ImGui::CalcTextSize("Record input").x + fpx + sp;
    std::string desc;
    if (open && frames.empty()) {
        desc = "node fires per frame - nothing has fired yet";
    } else if (open) {
        char b[160];
        std::snprintf(b, sizeof(b), "node fires per frame \xc2\xb7 %d frames kept (%.0f s) \xc2\xb7 click one to inspect it",
                      (int)frames.size(), (frames.back().frame - frames.front().frame) / hz);
        desc = b;
    } else if (dbgScrub_ >= 0 && dbgScrub_ < (int)frames.size()) {
        desc = "inspecting frame " + dbgFrameText(frames[(size_t)dbgScrub_].frame);
    } else {
        desc = "live \xc2\xb7 " + std::to_string(frames.size()) + " frames kept";
    }
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionMax().x - ImGui::GetCursorPosX() - tail - sp;
    if (room > scaled(60)) {
        // Shortened rather than wrapped: it is a caption, the strip says the rest.
        while (desc.size() > 4 && ImGui::CalcTextSize(desc.c_str()).x > room)
            desc = desc.substr(0, desc.size() - 5) + "...";
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(dbgScrub_ >= 0 && !open ? sem.warn : sem.textDim, "%s", desc.c_str());
        ImGui::SameLine();
    }
    const bool canRewind = dbgScrub_ >= 0 && dbgScrub_ < (int)frames.size() && !timeHistory_.empty() &&
                           timeHistory_.indexAtOrBefore(frames[(size_t)dbgScrub_].frame) >= 0;
    if (ImGui::GetCursorPosX() + tail > ImGui::GetContentRegionMax().x + 1.0f) ImGui::NewLine();
    ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - tail));
    const float rowY = ImGui::GetCursorPosY();
    if (!rec.empty()) {
        ui::StatePill(rec.c_str(), recKind, recKind == 2, ui::RtIcon::Record);
        ImGui::SameLine();
    } else if (!open) {
        ImGui::BeginDisabled(!debug || !project_.settings.inputRecorder);
        if (ImGui::Button("Record input")) replayArm_ = ReplayArm::Record;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(project_.settings.inputRecorder
                                  ? "Record the next run's input (takes effect on the next Build & Run)."
                                  : "Turn on the input recorder first (... > Input recorder).");
        ImGui::SameLine();
    }
    ImGui::SetCursorPosY(rowY);
    if (ImGui::Button("Replay...")) ImGui::OpenPopup("##dbgreplay");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Record the next run's input, play a recording back, save one.");
    ImGui::SetNextWindowSize(ImVec2(scaled(420), 0));
    if (ImGui::BeginPopup("##dbgreplay")) {
        drawReplayPanel();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRewind);
    if (ImGui::Button(rewindLabel) && canRewind) {
        timeMachineRewind(timeHistory_.indexAtOrBefore(frames[(size_t)dbgScrub_].frame));
        statusMessage_ = timeStatus_;  // "Rewound N frames" or why not
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (canRewind)
            ImGui::SetTooltip(
                "Put the RUNNING game back into its capture at or before the inspected frame:\n"
                "object transforms, physics, animation, flow variables, save values and where\n"
                "the player stands. Not put back: sequences mid-play, menus, audio, particles.\n"
                "Rewinding during a replay diverges it (docs/time-machine.md).");
        else if (!project_.settings.timeMachine)
            ImGui::SetTooltip("The time machine is off for this project (... > Time machine).");
        else
            ImGui::SetTooltip("Click a past frame on the strip, then rewind the world to it.");
    }
    ImGui::SameLine();
    if (ui::IconButton(open ? "Collapse time strip" : "Expand time strip",
                       open ? ui::RtIcon::ChevronDown : ui::RtIcon::ChevronUp))
        dbgTimeOpen_ = !open;

    // --- the strip itself: bars on a frame-number axis
    if (!open) return;  // collapsed: the header row is the whole strip
    const float h = ImMax(scaled(28), ImGui::GetContentRegionAvail().y - scaled(2));
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    if (w < 4.0f || h < 4.0f) return;
    ImGui::InvisibleButton("Time strip", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(org, ImVec2(org.x + w, org.y + h), dbgCol(sem.field), scaled(5));
    if (frames.empty()) {
        if (open)
            dl->AddText(ImVec2(org.x + fs * 0.8f, org.y + (h - fs) * 0.5f), dbgCol(sem.textDim, 0.8f),
                        dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted
                            ? "Waiting for the first node to fire."
                            : "Fires appear here while a debug game runs.");
        return;
    }
    const uint32_t f0 = frames.front().frame;
    const uint32_t f1 = ImMax(frames.back().frame, dbgSnap_.frame);
    const float span = (float)ImMax<uint32_t>(1, f1 - f0);
    const float pad = scaled(6);
    const float iw = w - pad * 2.0f;
    auto xOf = [&](uint32_t f) { return org.x + pad + iw * ((float)(f - f0) / span); };
    int maxFires = 1;
    for (const auto& fr : frames) maxFires = ImMax(maxFires, (int)fr.keys.size());
    const float bw = ImClamp(iw / span, ImMax(2.0f, scaled(2)), scaled(9));
    const float base = org.y + h - scaled(4);
    const float top = org.y + (open ? scaled(14) : scaled(2));
    int hoverIdx = -1;
    float hoverDist = FLT_MAX;
    const float mx = ImGui::GetMousePos().x;
    for (size_t i = 0; i < frames.size(); ++i) {
        const float x = xOf(frames[i].frame);
        const float frac = (float)frames[i].keys.size() / (float)maxFires;
        const float bh = ImMax(2.0f, frac * (base - top));
        const bool sel = dbgScrub_ == (int)i;
        bool hasBreak = false;
        if (dbgState_ == DbgState::Halted && i + 1 == frames.size())
            for (int k : frames[i].keys) hasBreak |= k == dbgSnap_.breakKey;
        dl->AddRectFilled(ImVec2(x - bw * 0.5f, base - bh), ImVec2(x + bw * 0.5f, base),
                          dbgCol(sel || hasBreak ? sem.warn : sem.ok, open ? 0.9f : 0.6f), ImMin(1.5f, bw * 0.3f));
        if (hovered && std::fabs(mx - x) < hoverDist && std::fabs(mx - x) < ImMax(bw, scaled(6))) {
            hoverDist = std::fabs(mx - x);
            hoverIdx = (int)i;
        }
    }
    if (!open) return;
    // Where the world is: the newest capture the time machine holds (the game
    // runs on from a rewind, so this is also "after the rewind").
    if (!timeHistory_.empty()) {
        const uint32_t wf = timeHistory_.newest().frame;
        if (wf >= f0) {
            const float x = ImMin(xOf(wf), org.x + w - pad);
            dl->AddLine(ImVec2(x, org.y + scaled(2)), ImVec2(x, org.y + h - scaled(2)), dbgCol(sem.accent, 0.7f), 1.5f);
        }
    }
    // The playhead: the inspected frame, or the live edge.
    const bool scrubbed = dbgScrub_ >= 0 && dbgScrub_ < (int)frames.size();
    const uint32_t pf = scrubbed ? frames[(size_t)dbgScrub_].frame : dbgSnap_.frame;
    {
        const float x = ImClamp(xOf(pf), org.x + pad, org.x + w - pad);
        const ImU32 c = dbgCol(scrubbed || dbgState_ == DbgState::Halted ? sem.warn : sem.textDim);
        for (float y = org.y + scaled(2); y < org.y + h - scaled(2); y += 6.0f)
            dl->AddLine(ImVec2(x, y), ImVec2(x, ImMin(y + 3.0f, org.y + h - scaled(2))), c, 2.0f);
        const std::string tag = (dbgState_ == DbgState::Halted && !scrubbed ? "halted " : scrubbed ? "frame " : "live ") +
                                dbgFrameText(pf);
        ui::FontScope mono(ui::monoFont());
        const float tfs = ImGui::GetFontSize() * 0.85f;
        const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(tfs, FLT_MAX, 0.0f, tag.c_str());
        const float tx = ImClamp(x - ts.x - scaled(6), org.x + pad, org.x + w - ts.x - scaled(8));
        dl->AddRectFilled(ImVec2(tx - scaled(4), org.y + scaled(2)), ImVec2(tx + ts.x + scaled(4), org.y + scaled(2) + ts.y),
                          dbgCol(scrubbed || dbgState_ == DbgState::Halted ? sem.warn : sem.surface,
                                 scrubbed || dbgState_ == DbgState::Halted ? 0.18f : 1.0f),
                          scaled(3));
        dl->AddText(ImGui::GetFont(), tfs, ImVec2(tx, org.y + scaled(2)),
                    dbgCol(scrubbed || dbgState_ == DbgState::Halted ? sem.warn : sem.textDim), tag.c_str());
    }
    if (hoverIdx >= 0) {
        ImGui::BeginTooltip();
        ImGui::Text("frame %s", dbgFrameText(frames[(size_t)hoverIdx].frame).c_str());
        int shown = 0;
        for (int k : frames[(size_t)hoverIdx].keys) {
            if (++shown > 8) {
                ImGui::TextDisabled("...");
                break;
            }
            if (const livedbg::NodeSym* n = dbgSyms_.find(k)) ImGui::TextDisabled("%s", dbgNodeLabel(*n).c_str());
        }
        ImGui::TextDisabled("click to inspect \xc2\xb7 double-click = live");
        ImGui::EndTooltip();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) dbgScrub_ = -1;
        else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) dbgScrub_ = hoverIdx;
    } else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        dbgScrub_ = -1;
    }
    // Arrow keys walk the inspected frame while the strip is hovered.
    if (hovered && !frames.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            dbgScrub_ = ImMax(0, (dbgScrub_ < 0 ? (int)frames.size() - 1 : dbgScrub_) - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && dbgScrub_ >= 0)
            dbgScrub_ = dbgScrub_ + 1 >= (int)frames.size() ? -1 : dbgScrub_ + 1;
    }
}

void App::drawDebuggerWindow() {
    if (!showDebugger_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(900), scaled(560)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debugger", &showDebugger_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ui::EmptyState(ui::Glyph::Nodes, "No project open", "Open a project, then Build & Run a debug build.");
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    const float W = ImGui::GetContentRegionAvail().x;

    dbgDrawHeader();
    ImGui::Spacing();
    dbgDrawCrashBanner();

    // How the window is docked decides the layout, not a setting: the bottom
    // drawer is wide and short, the Logic layout's column narrow and tall.
    const bool compactRail = W < scaled(600);
    const bool wideAside = W >= scaled(940);
    const bool halted = dbgState_ == DbgState::Halted;
    const bool scrubbed = dbgScrub_ >= 0;
    const bool wantAside = halted || !live || scrubbed;
    const float availH = ImGui::GetContentRegionAvail().y;
    const bool timeOpen = dbgTimeOpen_ && availH >= scaled(300);
    const float stripH = timeOpen ? ImClamp(availH * 0.22f, scaled(96), scaled(130))
                                  : ImGui::GetFrameHeight() + scaled(16);
    float bodyH = ImMax(scaled(60), availH - stripH - ImGui::GetStyle().ItemSpacing.y);
    // Too short for a column of ten rows (the bottom drawer): the rail becomes
    // one row of icons above the page, the counts as dots and in the tooltips.
    const bool railRow = bodyH < scaled(400) && !compactRail;

    // --- rail
    if (railRow) {
        const float y0 = ImGui::GetCursorPosY();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, scaled(2)));
        dbgDrawRail(true, true);
        ImGui::PopStyleVar();
        bodyH = ImMax(scaled(40), bodyH - (ImGui::GetCursorPosY() - y0));
    } else {
        const float railW = compactRail ? ImGui::GetFrameHeight() + scaled(18) : scaled(180);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, scaled(2)));
        ImGui::BeginChild("##dbgrail", ImVec2(railW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
        dbgDrawRail(compactRail);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        {
            const ImVec2 a = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y - bodyH), a, dbgCol(sem.border));
        }
        ImGui::SameLine(0.0f, 0.0f);
    }

    // --- page (+ the aside folded into its top when the window is narrow)
    const float asideW = wideAside && wantAside ? scaled(300) : 0.0f;
    ImGui::BeginChild("##dbgpage", ImVec2(asideW > 0 ? -asideW : 0.0f, bodyH),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    // Narrow: the checklist leads every page (nothing works without it); the
    // Stopped card only the Logic pages, where it is the context.
    const bool logicPage = dbgPage_ == DbgPage::Watch || dbgPage_ == DbgPage::Nodes ||
                           dbgPage_ == DbgPage::Breakpoints || dbgPage_ == DbgPage::Patches;
    if (!wideAside && wantAside && (!live || logicPage)) {
        if (!live) dbgDrawChecklist();
        else dbgDrawStoppedAt(scrubbed);
        ImGui::Spacing();
    }
    switch (dbgPage_) {
        case DbgPage::Watch: dbgDrawWatchPage(); break;
        case DbgPage::Nodes: dbgDrawNodesPage(); break;
        case DbgPage::Breakpoints: dbgDrawBreakpointsPage(); break;
        case DbgPage::Patches: dbgDrawPatchesPage(); break;
        case DbgPage::Objects: dbgDrawObjectsPage(); break;
        case DbgPage::Screen: dbgDrawScreenPage(); break;
        case DbgPage::Stats: dbgDrawStatsPage(); break;
        case DbgPage::RenderCost: dbgDrawRenderCostPage(); break;
        case DbgPage::Hardware: hardware_timeline::draw(project_.dir); break;
        case DbgPage::Vu: dbgDrawVuPage(); break;
    }
    ImGui::EndChild();
    if (asideW > 0) {
        ImGui::SameLine(0.0f, 0.0f);
        {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x, a.y + bodyH), dbgCol(sem.border));
        }
        ImGui::BeginChild("##dbgaside", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (!live) dbgDrawChecklist();
        else dbgDrawStoppedAt(true);
        ImGui::EndChild();
    }

    // --- the time strip
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x + W, a.y), dbgCol(sem.border));
    }
    ImGui::BeginChild("##dbgtime", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    dbgDrawTimeStrip(timeOpen);
    ImGui::EndChild();
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
        s.kind == uiscript::Step::Expect || s.kind == uiscript::Step::Drop;
    // The ten menus sit behind the logo button: a step naming one of them
    // while that menu is closed opens it first (uiscript::namesMainMenu), so a
    // script written for the old menu bar still reads `click Tools`.
    if (needsTarget && uiStepPhase_ == 0 && !logoMenuOpen_ && uiscript::namesMainMenu(s.arg))
        logoMenuOpenRequest_ = true;
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
        // The pointing steps' optional offset from that centre (Drag and Wheel
        // read dx/dy as their own motion, so they are left alone). It is how a
        // script reaches something DRAWN over a widget instead of submitted as
        // one - the viewport being one huge item.
        if (s.kind == uiscript::Step::Click ||
            s.kind == uiscript::Step::RightClick ||
            s.kind == uiscript::Step::DoubleClick ||
            s.kind == uiscript::Step::Hover || s.kind == uiscript::Step::Drop) {
            uiTargetX_ += s.dx;
            uiTargetY_ += s.dy;
        }
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
        case uiscript::Step::Drop: {
            const char* file = s.path.c_str();
            handleFileDrop(1, &file, uiTargetX_, uiTargetY_);
            finishStep();
            break;
        }
        case uiscript::Step::Click:
            io.AddMousePosEvent(uiTargetX_, uiTargetY_);
            if (uiStepPhase_ == 1) {
                if (s.shiftClick) io.AddKeyEvent(ImGuiMod_Shift, true);
                io.AddMouseButtonEvent(0, true);
                uiStepPhase_ = 2;
            } else if (uiStepPhase_ == 2) {
                io.AddMouseButtonEvent(0, false);
                if (s.shiftClick) io.AddKeyEvent(ImGuiMod_Shift, false);
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
                io.AddMouseButtonEvent(s.n, true);
                uiStepPhase_ = 2;
            } else if (uiStepPhase_ >= 2 && uiStepPhase_ < 2 + kSteps) {
                const float t = (float)(uiStepPhase_ - 1) / (float)kSteps;
                io.AddMousePosEvent(uiTargetX_ + s.dx * t, uiTargetY_ + s.dy * t);
                uiStepPhase_++;
            } else if (uiStepPhase_ == 2 + kSteps) {
                io.AddMousePosEvent(uiTargetX_ + s.dx, uiTargetY_ + s.dy);
                io.AddMouseButtonEvent(s.n, false);
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
            // "ctrl" means the editor's Ctrl shortcuts (Ctrl+S, Ctrl+K...). With
            // ConfigMacOSXBehaviors ImGui swaps Ctrl and Cmd as events arrive,
            // so on macOS the chord is sent as Super to land on ImGuiMod_Ctrl.
            const ImGuiKey ctrlMod = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            if (uiStepPhase_ == 0) {
                if (ctrl) io.AddKeyEvent(ctrlMod, true);
                if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
                if (alt) io.AddKeyEvent(ImGuiMod_Alt, true);
                io.AddKeyEvent((ImGuiKey)key, true);
                uiStepPhase_ = 1;
            } else if (uiStepPhase_ == 1) {
                io.AddKeyEvent((ImGuiKey)key, false);
                if (ctrl) io.AddKeyEvent(ctrlMod, false);
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

// One step of a running pad script. A step with no duration is applied and the
// next one taken in the same frame ("press" is a state change plus a wait);
// a step with a duration holds until its deadline.
void App::padScriptTick() {
    if (!padScriptRunning_) return;
    const double now = ImGui::GetTime();
    while (padScriptRunning_ && now >= padScriptUntil_) {
        if (padScriptStep_ >= padScript_.size()) {
            padScriptRunning_ = false;
            padState_.clear();  // never leave the game holding a button
            break;
        }
        const livepad::Step& s = padScript_[padScriptStep_++];
        padState_ = s.state;
        padScriptUntil_ = now + s.seconds;
        if (s.seconds > 0.0) break;
    }
}

void App::remotePadTick() {
    namespace fs = std::filesystem;
    const bool usable = hasProject_ && project_.settings.remotePad &&
                        project_.settings.buildProfile == "debug";
    // "Driving" means the window is open. Anything else (window closed, project
    // closed, preference off) has to let go explicitly - a game left holding a
    // direction because a panel was closed would look exactly like a stuck pad.
    // A script drives the pad with no panel open: the assistant is holding the
    // controller, and the panel is a different way of holding the same one.
    const bool want = usable && (showRemotePad_ || padScriptRunning_);
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
    // Advance a script EVERY frame, not at the write cadence: a `press cross 0.1`
    // is shorter than two writes apart, and a step resolved late would be
    // announced late or not at all.
    padScriptTick();
    const double now = ImGui::GetTime();
    if (now < padNextWrite_) return;
    padNextWrite_ = now + 0.04;  // ~25 Hz: keeps the game's stale watchdog fed
    padStatus_ = livepad::write(
        (fs::path(project_.dir) / "bin" / "livepad.bin").string(), padState_,
        ++padSeq_, true);
}

// Tools > Remote Pad (docs/remote-pad.md): a drawn DualShock - shoulders on
// top, d-pad left, the four face buttons as their shapes (this is the one
// place the editor depicts real pad buttons), Select/Start between them and two
// round sticks dragged in 2-D. A header pill says whether a game is listening,
// keyboard capture is a switch with a visible focus ring and key caps printed
// on the buttons, and the footer is Release all plus the same moment as a
// --pad script line.
void App::drawRemotePadWindow() {
    if (!showRemotePad_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(600), scaled(590)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Remote Pad", &showRemotePad_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    if (!hasProject_) {
        ui::EmptyState(ui::Glyph::Box, "No project open", "Open a project, then run a debug build.");
        ImGui::End();
        return;
    }

    // --- preconditions: a card with the fix, not a wall of text
    const bool debug = project_.settings.buildProfile == "debug";
    if (!debug || !project_.settings.remotePad) {
        ui::BeginCard("##padpre", ImVec2(0, 0));
        ui::StatusLine(1, !debug ? "This project builds in the release profile."
                                 : "The Remote Pad preference is off.");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(sem.textDim, "%s",
                           !debug ? "A release build carries no Remote Pad (the devkit's zero-cost rule), "
                                    "so nothing here would reach the game."
                                  : "The poller is compiled into the game, so turning it on needs a "
                                    "build, not just a save.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (!debug) {
            if (ui::PrimaryButton("Switch to debug profile")) {
                project_.settings.buildProfile = "debug";
                commitChange();
            }
        } else if (ui::PrimaryButton("Turn on Remote Pad")) {
            project_.settings.remotePad = true;
            commitChange();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(sem.textDim, "then Build & Run (F5)");
        ui::EndCard();
        ImGui::End();
        return;
    }

    const bool single = project_.settings.multiplayer == "off";
    if (single) padTarget_ = 0;
    const int pi = padTarget_;
    livepad::State& st = padState_;
    const double now = ImGui::GetTime();
    const bool shift = ImGui::GetIO().KeyShift;

    // --- header: is anyone listening, and which port
    {
        const bool listening = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
        char pill[96];
        int kind = 4;
        if (!padStatus_.empty()) {
            std::snprintf(pill, sizeof(pill), "Write failed");
            kind = 2;
        } else if (listening) {
            std::snprintf(pill, sizeof(pill), "Game listening \xc2\xb7 pad %d \xc2\xb7 25 Hz", pi + 1);
            kind = 0;
        } else if (!project_.settings.liveDebug) {
            std::snprintf(pill, sizeof(pill), "Writing \xc2\xb7 25 Hz");
            kind = 3;
        } else {
            std::snprintf(pill, sizeof(pill), "No game running");
        }
        ImGui::AlignTextToFramePadding();
        ui::StatePill(pill, kind);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", !padStatus_.empty() ? padStatus_.c_str()
                                    : listening ? "The game's debug channel is reporting, so it reads bin/livepad.bin."
                                    : !project_.settings.liveDebug
                                        ? "Writing bin/livepad.bin. Without the Live Debugger the editor\n"
                                          "cannot tell whether a game is reading it."
                                        : "Nothing is reporting - Build & Run (F5) a debug build.");
        if (kind == 4 && !runner_.busy()) {
            ImGui::SameLine();
            if (ui::GhostButton("Run (F5)")) dbgBuildAndRun();
        }
        const char* ports[2] = {"Pad 1", "Pad 2"};
        const float segW = scaled(130);
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - segW));
        ImGui::BeginDisabled(single);
        int port = pi;
        if (ui::Segmented("##padport", ports, 2, &port, segW)) padTarget_ = port;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(single ? "Single player - turn on Multiplayer in Project Preferences\n"
                                       "to open the second port."
                                     : "Which connector the panel drives. Start on pad 2 hot-joins player two.");
    }
    ImGui::Spacing();

    // A click has to survive the trip: the state is snapshotted ~25 times a
    // second, so every press is latched for kMinHold - long enough for the
    // game to see a press AND a release rather than one ambiguous frame.
    const double kMinHold = 0.12;
    auto held = [&](int idx, bool down) {
        if (idx < 0) return;
        const uint32_t m = livepad::buttonBit(idx);
        if (down && (st.buttons[pi] & m) == 0) padLatch_[pi][idx] = now + kMinHold;
        if (down || now < padLatch_[pi][idx] || padSticky_[pi][idx])
            st.buttons[pi] |= m;
        else
            st.buttons[pi] &= ~m;
    };
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool capturing = padKeyboard_ && focused;

    // --- the controller canvas, in the 560 x 330 design space
    const float availW = ImGui::GetContentRegionAvail().x;
    const float k = ImMin(availW / 560.0f, scaled(1.25f));
    const float cw = 560.0f * k, ch = 330.0f * k;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    auto P = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + cw, o.y + ch), dbgCol(sem.field), scaled(12));
    if (capturing) {
        dl->AddRect(ImVec2(o.x - 2, o.y - 2), ImVec2(o.x + cw + 2, o.y + ch + 2), dbgCol(sem.accent, 0.18f),
                    scaled(13), 0, 5.0f);
        dl->AddRect(o, ImVec2(o.x + cw, o.y + ch), dbgCol(sem.accent, 0.6f), scaled(12), 0, 1.5f);
    } else {
        dl->AddRect(o, ImVec2(o.x + cw, o.y + ch), dbgCol(sem.border), scaled(12));
    }
    // The silhouette.
    dl->PathLineTo(P(150, 70));
    dl->PathBezierCubicCurveTo(P(210, 58), P(350, 58), P(410, 70));
    dl->PathBezierCubicCurveTo(P(470, 80), P(505, 110), P(520, 170));
    dl->PathBezierCubicCurveTo(P(538, 240), P(528, 300), P(488, 306));
    dl->PathBezierCubicCurveTo(P(452, 311), P(432, 270), P(404, 236));
    dl->PathBezierCubicCurveTo(P(380, 210), P(180, 210), P(156, 236));
    dl->PathBezierCubicCurveTo(P(128, 270), P(108, 311), P(72, 306));
    dl->PathBezierCubicCurveTo(P(32, 300), P(22, 240), P(40, 170));
    dl->PathBezierCubicCurveTo(P(55, 110), P(90, 80), P(150, 70));
    dl->PathFillConcave(dbgCol(sem.surface));
    dl->PathLineTo(P(150, 70));
    dl->PathBezierCubicCurveTo(P(210, 58), P(350, 58), P(410, 70));
    dl->PathBezierCubicCurveTo(P(470, 80), P(505, 110), P(520, 170));
    dl->PathBezierCubicCurveTo(P(538, 240), P(528, 300), P(488, 306));
    dl->PathBezierCubicCurveTo(P(452, 311), P(432, 270), P(404, 236));
    dl->PathBezierCubicCurveTo(P(380, 210), P(180, 210), P(156, 236));
    dl->PathBezierCubicCurveTo(P(128, 270), P(108, 311), P(72, 306));
    dl->PathBezierCubicCurveTo(P(32, 300), P(22, 240), P(40, 170));
    dl->PathBezierCubicCurveTo(P(55, 110), P(90, 80), P(150, 70));
    dl->PathStroke(dbgCol(sem.border), ImDrawFlags_Closed, 1.5f);

    enum Shape { Text, Arrow, Tri, Sq, Circ, Cross };
    // One pad button: an InvisibleButton named `label` (the --ui-script name,
    // "Remote Pad/Cross"), held while the mouse holds it.
    auto padButton = [&](const char* label, const char* name, float x, float y, float w, float h,
                         Shape shape, float dir, const char* key, bool round) {
        const int idx = livepad::buttonByName(name);
        const uint32_t m = livepad::buttonBit(idx);
        ImGui::SetCursorScreenPos(P(x, y));
        ImGui::InvisibleButton(label, ImVec2(w * k, h * k));
        const bool active = ImGui::IsItemActive();
        if (padShiftHold_ && shift && ImGui::IsItemClicked() && idx >= 0) {
            padSticky_[pi][idx] = !padSticky_[pi][idx];
        } else if (ImGui::IsItemClicked() && idx >= 0 && padSticky_[pi][idx]) {
            padSticky_[pi][idx] = false;  // a plain click lets a latched button go
        }
        held(idx, active && !(padShiftHold_ && shift));
        const bool on = (st.buttons[pi] & m) != 0;
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        const ImVec2 a = P(x, y), b = P(x + w, y + h);
        const float r = round ? (b.x - a.x) * 0.5f : scaled(7) * k;
        if (on) dl->AddRectFilled(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), dbgCol(sem.accent, 0.18f), r + 3);
        dl->AddRectFilled(a, b, on ? dbgCol(sem.accent, 0.28f) : dbgCol(t > 0 ? sem.border : sem.field, 1.0f), r);
        dl->AddRect(a, b, on ? dbgCol(sem.accent) : dbgCol(sem.border, 1.5f), r, 0, on ? 1.5f : 1.0f);
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float s = (b.y - a.y) * 0.24f;
        const float th = ImMax(1.5f, 2.2f * k);
        switch (shape) {
            case Text: {
                const char* end = ImGui::FindRenderedTextEnd(label);
                const ImVec2 ts = ImGui::CalcTextSize(label, end);
                dl->AddText(ImVec2(c.x - ts.x * 0.5f - (key && padKeyboard_ ? scaled(7) : 0), c.y - ts.y * 0.5f),
                            dbgCol(on ? sem.text : sem.textDim, on ? 1.0f : 0.95f), label, end);
                break;
            }
            case Arrow: {
                const float cs = std::cos(dir), sn = std::sin(dir);
                auto R = [&](float px, float py) { return ImVec2(c.x + px * cs - py * sn, c.y + px * sn + py * cs); };
                dl->AddTriangleFilled(R(s, 0), R(-s * 0.7f, -s * 0.8f), R(-s * 0.7f, s * 0.8f),
                                      dbgCol(on ? sem.text : sem.textDim));
                break;
            }
            case Tri:
                dl->AddTriangle(ImVec2(c.x, c.y - s * 1.1f), ImVec2(c.x + s * 1.1f, c.y + s * 0.8f),
                                ImVec2(c.x - s * 1.1f, c.y + s * 0.8f), dbgCol(sem.ok), th);
                break;
            case Sq:
                dl->AddRect(ImVec2(c.x - s * 0.9f, c.y - s * 0.9f), ImVec2(c.x + s * 0.9f, c.y + s * 0.9f),
                            dbgCol(sem.special), 1.0f, 0, th);
                break;
            case Circ: dl->AddCircle(c, s, dbgCol(sem.danger), 20, th); break;
            case Cross:
                dl->AddLine(ImVec2(c.x - s * 0.85f, c.y - s * 0.85f), ImVec2(c.x + s * 0.85f, c.y + s * 0.85f),
                            dbgCol(on ? sem.text : sem.accent), th);
                dl->AddLine(ImVec2(c.x + s * 0.85f, c.y - s * 0.85f), ImVec2(c.x - s * 0.85f, c.y + s * 0.85f),
                            dbgCol(on ? sem.text : sem.accent), th);
                break;
        }
        if (idx >= 0 && padSticky_[pi][idx])
            dl->AddCircleFilled(ImVec2(b.x - scaled(5) * k, a.y + scaled(5) * k), scaled(2.5f), dbgCol(sem.warn), 8);
        // The key that drives it, printed small so the map is learnable.
        if (key && padKeyboard_) {
            const float fs = ImGui::GetFontSize() * 0.72f;
            ImFont* f = ui::monoFont() ? ui::monoFont() : ImGui::GetFont();
            const ImVec2 ks = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, key);
            const ImVec2 kp = shape == Text ? ImVec2(b.x - ks.x - scaled(6) * k, c.y - ks.y * 0.5f)
                                            : ImVec2(c.x - ks.x * 0.5f, b.y + scaled(3));
            dl->AddText(f, fs, kp, dbgCol(sem.textDim, 0.85f), key);
        }
    };
    // shoulders
    padButton("L2", "L2", 56, 14, 64, 28, Text, 0, "2", false);
    padButton("L1", "L1", 126, 14, 64, 28, Text, 0, "1", false);
    padButton("R1", "R1", 370, 14, 64, 28, Text, 0, "3", false);
    padButton("R2", "R2", 440, 14, 64, 28, Text, 0, "4", false);
    // d-pad
    const float pi_ = 3.14159265f;
    padButton("Up", "DpadUp", 106, 92, 36, 36, Arrow, -pi_ * 0.5f, nullptr, false);
    padButton("Left", "DpadLeft", 70, 128, 36, 36, Arrow, pi_, nullptr, false);
    padButton("Right", "DpadRight", 142, 128, 36, 36, Arrow, 0.0f, nullptr, false);
    padButton("Down", "DpadDown", 106, 164, 36, 36, Arrow, pi_ * 0.5f, nullptr, false);
    // face buttons
    padButton("Triangle", "Triangle", 413, 88, 38, 38, Tri, 0, "R", true);
    padButton("Square", "Square", 374, 127, 38, 38, Sq, 0, "Q", true);
    padButton("Circle", "Circle", 452, 127, 38, 38, Circ, 0, "E", true);
    padButton("Cross", "Cross", 413, 166, 38, 38, Cross, 0, "Spc", true);
    // middle
    padButton("Select", "Select", 202, 128, 70, 24, Text, 0, nullptr, false);
    padButton("Start", "Start", 288, 128, 70, 24, Text, 0, nullptr, false);

    // The sticks: drag in 2-D, the value shown as -1..1. Released, a stick
    // springs back like a real one - unless Shift is held, which leaves it
    // there (a player that keeps walking while you watch PCSX2). A click
    // without a drag is L3 / R3; a double-click centres a parked stick.
    auto stick = [&](const char* label, int side, float cx, float cy, const char* keys) {
        const int base = side * 2;
        const float R = 40.0f * k;
        const ImVec2 c = P(cx, cy);
        dl->AddCircleFilled(c, R, dbgCol(sem.field), 32);
        dl->AddCircle(c, R, dbgCol(sem.border), 32);
        dl->AddLine(ImVec2(c.x, c.y - R * 0.8f), ImVec2(c.x, c.y + R * 0.8f), dbgCol(sem.border, 0.6f));
        dl->AddLine(ImVec2(c.x - R * 0.8f, c.y), ImVec2(c.x + R * 0.8f, c.y), dbgCol(sem.border, 0.6f));
        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
        ImGui::InvisibleButton(label, ImVec2(R * 2, R * 2));
        const int idx = livepad::buttonByName(label);
        if (ImGui::IsItemActive()) {
            const ImVec2 d(ImGui::GetMousePos().x - c.x, ImGui::GetMousePos().y - c.y);
            const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            if (std::fabs(drag.x) + std::fabs(drag.y) > 3.0f) padStickDragged_[side] = true;
            if (padStickDragged_[side]) {
                float x = d.x / (R * 0.75f), y = d.y / (R * 0.75f);
                const float len = std::sqrt(x * x + y * y);
                if (len > 1.0f) {
                    x /= len;
                    y /= len;
                }
                st.axes[pi][base] = (int8_t)lroundf(x * 127.0f);
                st.axes[pi][base + 1] = (int8_t)lroundf(y * 127.0f);
            }
        }
        if (ImGui::IsItemDeactivated()) {
            if (!padStickDragged_[side]) held(idx, true);  // a click: tap L3 / R3
            else if (!shift) st.axes[pi][base] = st.axes[pi][base + 1] = 0;
            padStickDragged_[side] = false;
        } else if (!ImGui::IsItemActive()) {
            held(idx, false);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            st.axes[pi][base] = st.axes[pi][base + 1] = 0;
        const float vx = st.axes[pi][base] / 127.0f, vy = st.axes[pi][base + 1] / 127.0f;
        const bool moved = st.axes[pi][base] != 0 || st.axes[pi][base + 1] != 0;
        const bool pressed = idx >= 0 && (st.buttons[pi] & livepad::buttonBit(idx)) != 0;
        const ImVec2 kn(c.x + vx * R * 0.58f, c.y + vy * R * 0.58f);
        if (moved) dl->AddLine(c, kn, dbgCol(sem.accent), 2.0f);
        dl->AddCircleFilled(kn, 13.0f * k, moved || pressed ? dbgCol(sem.accent, 0.35f) : dbgCol(sem.surface), 24);
        dl->AddCircle(kn, 13.0f * k, moved || pressed ? dbgCol(sem.accent) : dbgCol(sem.border, 2.0f), 24, 1.5f);
        char v[48];
        std::snprintf(v, sizeof(v), "%c  %.2f, %.2f", side ? 'R' : 'L', vx, vy);
        ImFont* f = ui::monoFont() ? ui::monoFont() : ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * 0.85f;
        const ImVec2 vs = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, v);
        dl->AddText(f, fs, ImVec2(c.x - vs.x * 0.5f, c.y + R + scaled(6)), dbgCol(moved ? sem.accent : sem.textDim), v);
        char hint[48];
        std::snprintf(hint, sizeof(hint), "%s%sclick = %s", padKeyboard_ ? keys : "", padKeyboard_ ? " \xc2\xb7 " : "",
                      label);
        const float hs = ImGui::GetFontSize() * 0.8f;
        const ImVec2 hz = ImGui::GetFont()->CalcTextSizeA(hs, FLT_MAX, 0.0f, hint);
        dl->AddText(ImGui::GetFont(), hs, ImVec2(c.x - hz.x * 0.5f, c.y + R + scaled(6) + vs.y + 2.0f),
                    dbgCol(sem.textDim, 0.8f), hint);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
            ImGui::SetTooltip("Drag to push the stick; it springs back on release.\n"
                              "Shift while releasing leaves it there; double-click centres it.\n"
                              "A click without a drag presses %s.", label);
    };
    stick("L3", 0, 210, 232, "WASD");
    stick("R3", 1, 350, 232, "IJKL");

    if (capturing) {
        const float fs = ImGui::GetFontSize() * 0.85f;
        const ImVec2 p = P(12, 330 - 22);
        ui::DrawRtIcon(dl, ui::RtIcon::Keyboard, ImVec2(p.x + fs * 0.5f, p.y + fs * 0.55f), fs, dbgCol(sem.accent));
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + fs * 1.4f, p.y), dbgCol(sem.accent), "Keys go to the game");
        const char* esc = "Esc releases";
        const float ew = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, esc).x;
        dl->AddText(ImGui::GetFont(), fs, ImVec2(o.x + cw - ew - 12 * k, p.y), dbgCol(sem.textDim), esc);
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + ch));
    ImGui::Dummy(ImVec2(cw, scaled(8)));

    // --- the keyboard: reaches the GAME while THIS window has focus, so PCSX2
    // never needs it. Nothing is read while you type anywhere else.
    ui::Switch("Drive with the keyboard (While this window has focus. PCSX2 never needs the focus.)",
               &padKeyboard_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("WASD / arrows = left stick, IJKL = right stick, Space = Cross,\n"
                          "E = Circle, Q = Square, R = Triangle, Enter = Start,\n"
                          "Backspace = Select, 1/2 = L1/L2, 3/4 = R1/R2. Esc lets go of the focus.");
    ui::Switch("Shift-click holds a button (Until you click it again - for charge moves and menus.)",
               &padShiftHold_);
    if (capturing) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::SetWindowFocus(nullptr);
        } else {
            struct KeyMap {
                ImGuiKey key;
                const char* button;
            };
            static const KeyMap kKeys[] = {
                {ImGuiKey_Space, "Cross"},  {ImGuiKey_E, "Circle"},          {ImGuiKey_Q, "Square"},
                {ImGuiKey_R, "Triangle"},   {ImGuiKey_Enter, "Start"},       {ImGuiKey_Backspace, "Select"},
                {ImGuiKey_1, "L1"},         {ImGuiKey_2, "L2"},              {ImGuiKey_3, "R1"},
                {ImGuiKey_4, "R2"},
            };
            for (const KeyMap& km : kKeys)
                if (ImGui::IsKeyDown(km.key)) held(livepad::buttonByName(km.button), true);
            auto axis = [](ImGuiKey neg, ImGuiKey pos, ImGuiKey neg2, ImGuiKey pos2) {
                int v = 0;
                if (ImGui::IsKeyDown(neg) || ImGui::IsKeyDown(neg2)) v -= 127;
                if (ImGui::IsKeyDown(pos) || ImGui::IsKeyDown(pos2)) v += 127;
                return (int8_t)v;
            };
            if (!padStickDragged_[0]) {
                st.axes[pi][0] = axis(ImGuiKey_A, ImGuiKey_D, ImGuiKey_LeftArrow, ImGuiKey_RightArrow);
                st.axes[pi][1] = axis(ImGuiKey_W, ImGuiKey_S, ImGuiKey_UpArrow, ImGuiKey_DownArrow);
            }
            if (!padStickDragged_[1]) {
                st.axes[pi][2] = axis(ImGuiKey_J, ImGuiKey_L, ImGuiKey_J, ImGuiKey_L);
                st.axes[pi][3] = axis(ImGuiKey_I, ImGuiKey_K, ImGuiKey_I, ImGuiKey_K);
            }
        }
    }

    // --- footer: the panic button, and this moment as a --pad script line
    ImGui::Spacing();
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        dl->AddLine(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y), dbgCol(sem.border));
        ImGui::Dummy(ImVec2(0, scaled(4)));
    }
    if (ui::DangerButton("Release all")) {
        padState_.clear();
        // The latches too, or a just-clicked button comes straight back.
        for (int p = 0; p < livepad::kPads; ++p)
            for (int b = 0; b < 16; ++b) {
                padLatch_[p][b] = 0.0;
                padSticky_[p][b] = false;
            }
    }
    std::string script;
    for (int p = 0; p < livepad::kPads; ++p) {
        std::string part;
        std::string btns;
        for (int b = 0; b < 16; ++b)
            if (st.buttons[p] & livepad::buttonBit(b)) btns += std::string(btns.empty() ? "" : ",") + kPadButtonNames[b];
        if (!btns.empty()) part += "hold " + btns;
        for (int s = 0; s < 2; ++s)
            if (st.axes[p][s * 2] || st.axes[p][s * 2 + 1])
                part += std::string(part.empty() ? "" : "; ") + "stick " + (s ? "r " : "l ") +
                        std::to_string(st.axes[p][s * 2]) + " " + std::to_string(st.axes[p][s * 2 + 1]);
        if (part.empty()) continue;
        if (p == 1) part = "pad 2; " + part;
        script += (script.empty() ? "" : "; ") + part;
    }
    if (script.empty()) script = "press cross";
    script += "; wait 1";
    const std::string line = "tyrax-editor --pad " + platform::shellArg(project_.dir) + " \"" + script + "\"";
    ImGui::SameLine();
    const float copyW = ImGui::GetFrameHeight();
    {
        ui::FontScope mono(ui::monoFont());
        const float room = ImGui::GetContentRegionAvail().x - copyW - ImGui::GetStyle().ItemSpacing.x;
        std::string shown = "tyrax-editor --pad " + project_.name + " \"" + script + "\"";
        while (shown.size() > 8 && ImGui::CalcTextSize(shown.c_str()).x > room)
            shown = shown.substr(0, shown.size() - 4) + "...";
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(sem.textDim, "%s", shown.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("The same moment from a script (docs/remote-pad.md):\n%s", line.c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - copyW);
    if (ui::IconButton("Copy script line", ui::RtIcon::Copy, copyW, "Copy as a --pad command line")) {
        ImGui::SetClipboardText(line.c_str());
        statusMessage_ = "Remote Pad script line copied";
    }
    ImGui::End();
}

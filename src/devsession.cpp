#include "devsession.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#define TYRAX_GETPID _getpid
#else
#include <unistd.h>
#define TYRAX_GETPID getpid
#endif

namespace devsession {
namespace {

// A session goes stale after a minute of silence: the editor heartbeats every
// few seconds, so a minute is many missed beats, and short enough that a
// crashed instance stops being reported as live while the user still remembers
// crashing it.
constexpr long long kStaleAfter = 60;
// ...and its file is swept away after a day, so a machine that crashes often
// does not accumulate them.
constexpr long long kReapAfter = 24 * 60 * 60;

long long nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    if (v && *v) return v;
    return fallback ? fallback : "";
}

// Per-user state, per platform convention. Windows keeps editor.ini in
// LOCALAPPDATA and this belongs beside it; elsewhere the XDG base-directory
// spec calls this "state" (data that should persist but is not config and not
// a cache), which is exactly what a session pointer is.
std::filesystem::path stateDir() {
#ifdef _WIN32
    std::string base = envOr("LOCALAPPDATA", nullptr);
    if (base.empty()) base = envOr("USERPROFILE", nullptr);
    if (base.empty()) return {};
    return std::filesystem::path(base) / "tyra-editor";
#else
    const std::string xdg = envOr("XDG_STATE_HOME", nullptr);
    if (!xdg.empty()) return std::filesystem::path(xdg) / "tyra-editor";
    const std::string home = envOr("HOME", nullptr);
    if (home.empty()) return {};
    return std::filesystem::path(home) / ".local" / "state" / "tyra-editor";
#endif
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == ' ' ||
                     s[b - 1] == '\t'))
        --b;
    return s.substr(a, b - a);
}

}  // namespace

long long Info::ageSeconds() const {
    if (heartbeat <= 0) return -1;
    const long long age = nowSeconds() - heartbeat;
    return age < 0 ? 0 : age;  // a clock that moved backwards is not "future"
}

bool Info::live() const {
    const long long age = ageSeconds();
    return age >= 0 && age <= kStaleAfter;
}

int selfPid() { return (int)TYRAX_GETPID(); }

std::string dir() {
    const auto base = stateDir();
    if (base.empty()) return {};
    const auto d = base / "sessions";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d.string();
}

bool publish(const Info& info) {
    const std::string d = dir();
    if (d.empty() || info.pid <= 0) return false;
    const std::filesystem::path target =
        std::filesystem::path(d) / (std::to_string(info.pid) + ".ini");
    const std::filesystem::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << "pid=" << info.pid << "\n"
          << "started=" << info.started << "\n"
          << "heartbeat=" << (info.heartbeat ? info.heartbeat : nowSeconds())
          << "\n"
          << "project=" << info.project << "\n"
          << "name=" << info.name << "\n"
          << "scene=" << info.scene << "\n"
          << "profile=" << info.profile << "\n"
          << "liveDebug=" << (info.liveDebug ? 1 : 0) << "\n"
          << "liveLink=" << (info.liveLink ? 1 : 0) << "\n"
          << "gameLive=" << (info.gameLive ? 1 : 0) << "\n"
          << "gameFrame=" << info.gameFrame << "\n"
          << "gameHalted=" << (info.gameHalted ? 1 : 0) << "\n"
          << "transport=" << info.transport << "\n";
        if (!f) return false;
    }
    // Rename over the old one: a reader never sees a half-written pointer.
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void retire(int pid) {
    const std::string d = dir();
    if (d.empty() || pid <= 0) return;
    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::path(d) / (std::to_string(pid) + ".ini"), ec);
}

std::vector<Info> list() {
    std::vector<Info> out;
    const std::string d = dir();
    if (d.empty()) return out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(d, ec)) {
        if (ec) break;
        if (!e.is_regular_file() || e.path().extension() != ".ini") continue;
        std::ifstream f(e.path());
        if (!f) continue;
        Info i;
        std::string line;
        while (std::getline(f, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = trim(line.substr(0, eq));
            const std::string v = trim(line.substr(eq + 1));
            if (k == "pid") i.pid = std::atoi(v.c_str());
            else if (k == "started") i.started = std::atoll(v.c_str());
            else if (k == "heartbeat") i.heartbeat = std::atoll(v.c_str());
            else if (k == "project") i.project = v;
            else if (k == "name") i.name = v;
            else if (k == "scene") i.scene = v;
            else if (k == "profile") i.profile = v;
            else if (k == "liveDebug") i.liveDebug = v == "1";
            else if (k == "liveLink") i.liveLink = v == "1";
            else if (k == "gameLive") i.gameLive = v == "1";
            else if (k == "gameFrame") i.gameFrame = (unsigned)std::atoll(v.c_str());
            else if (k == "gameHalted") i.gameHalted = v == "1";
            else if (k == "transport") i.transport = v;
        }
        if (i.pid <= 0) continue;
        if (i.ageSeconds() > kReapAfter) {  // long dead: sweep it up
            std::error_code rec;
            std::filesystem::remove(e.path(), rec);
            continue;
        }
        out.push_back(std::move(i));
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
        return a.heartbeat > b.heartbeat;
    });
    return out;
}

}  // namespace devsession

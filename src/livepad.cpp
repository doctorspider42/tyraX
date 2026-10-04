#include "livepad.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "input.hpp"
#include "platform.hpp"

namespace livepad {
namespace {

constexpr uint32_t kMagic = 0x44505854U;  // "TXPD"
constexpr uint32_t kVersion = 1U;
constexpr uint32_t kFooterXor = 0x5A5A5A5AU;

// How hard to try when the replace below loses the race against the game
// reading the same file. Five spare tries, 4 ms apart, plus the denied syscalls
// themselves: measured to absorb an exclusive 60 ms hold on the file, while
// leaving the driver's own 40 ms refresh cadence undisturbed (a full 9 s script
// still finishes in 9.1 s). Anything longer is not a race any more, and is
// reported instead - see the refresh/step split in main.cpp.
constexpr int kReplaceRetries = 5;
constexpr int kReplaceRetryMs = 4;

void put32(unsigned char* p, uint32_t v) { std::memcpy(p, &v, 4); }
uint32_t get32(const unsigned char* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/** -127..127, clamped. Accepts a plain integer or a decimal. */
bool parseAxis(const std::string& tok, int8_t& out) {
    try {
        const double v = std::stod(tok);
        out = (int8_t)std::max(-127.0, std::min(127.0, v));
        return true;
    } catch (...) {
        return false;
    }
}

bool parseSeconds(const std::string& tok, double& out) {
    try {
        out = std::stod(tok);
        return out >= 0.0;
    } catch (...) {
        return false;
    }
}

}  // namespace

// ------------------------------------------------------------------ state ---

bool State::neutral() const {
    for (int p = 0; p < kPads; ++p) {
        if (buttons[p]) return false;
        for (int a = 0; a < 4; ++a)
            if (axes[p][a]) return false;
    }
    return true;
}

void State::clear() { *this = State(); }

bool State::operator==(const State& o) const {
    for (int p = 0; p < kPads; ++p) {
        if (buttons[p] != o.buttons[p]) return false;
        for (int a = 0; a < 4; ++a)
            if (axes[p][a] != o.axes[p][a]) return false;
    }
    return true;
}

uint32_t buttonBit(int index) {
    if (index < 0 || index > 15) return 0;
    return 1U << index;
}

int buttonByName(const std::string& name) {
    const std::string n = lower(trim(name));
    if (n.empty()) return -1;
    // The canonical names first (Cross, DpadUp, L1, ...), then the shorthands
    // a person actually types at a prompt.
    for (int i = 0; i < 16; ++i)
        if (n == lower(kPadButtonNames[i])) return i;
    struct Alias {
        const char* name;
        const char* canonical;
    };
    static const Alias kAliases[] = {
        {"x", "Cross"},      {"o", "Circle"},    {"sq", "Square"},
        {"tri", "Triangle"}, {"up", "DpadUp"},   {"down", "DpadDown"},
        {"left", "DpadLeft"}, {"right", "DpadRight"},
    };
    for (const Alias& a : kAliases)
        if (n == a.name) return padButtonIndex(a.canonical);
    return -1;
}

// ------------------------------------------------------------------- file ---

std::vector<unsigned char> encode(const State& s, uint32_t seq, bool attached) {
    std::vector<unsigned char> b((size_t)kFileSize, 0);
    unsigned char* p = b.data();
    put32(p + 0, kMagic);
    put32(p + 4, kVersion);
    put32(p + 8, seq);
    put32(p + 12, attached ? 1U : 0U);
    put32(p + 16, s.buttons[0]);
    put32(p + 20, s.buttons[1]);
    for (int pad = 0; pad < kPads; ++pad)
        for (int a = 0; a < 4; ++a)
            p[24 + pad * 4 + a] = (unsigned char)(int8_t)s.axes[pad][a];
    put32(p + 32, seq ^ kFooterXor);
    return b;
}

bool decode(const std::vector<unsigned char>& bytes, State& out, uint32_t& seq,
            bool& attached) {
    if (bytes.size() != (size_t)kFileSize) return false;
    const unsigned char* p = bytes.data();
    if (get32(p + 0) != kMagic || get32(p + 4) != kVersion) return false;
    seq = get32(p + 8);
    if (get32(p + 32) != (seq ^ kFooterXor)) return false;  // torn write
    attached = get32(p + 12) != 0U;
    out.buttons[0] = get32(p + 16);
    out.buttons[1] = get32(p + 20);
    for (int pad = 0; pad < kPads; ++pad)
        for (int a = 0; a < 4; ++a)
            out.axes[pad][a] = (int8_t)p[24 + pad * 4 + a];
    return true;
}

std::string write(const std::string& path, const State& s, uint32_t seq,
                  bool attached) {
    namespace fs = std::filesystem;
    const std::vector<unsigned char> bytes = encode(s, seq, attached);
    const fs::path target(path);
    const fs::path tmp(path + ".tmp");
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "cannot write " + tmp.string();
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
        if (!f) return "write failed: " + tmp.string();
    }
    // Rename over the old file so the game never reads a half-written state.
    //
    // On Windows that replace RACES THE READER. The running game re-opens this
    // file roughly every frame through PCSX2's Host Filesystem, and Windows
    // refuses to rename over (or delete) a file another process has open, so a
    // replace lands on "Access is denied" about once in 200 tries at the
    // driver's 40 ms refresh - which used to kill about one 9 s hold in five,
    // and every one of them under heavier contention. Measured 2026-08-03 with
    // PCSX2 running a fixture: zero denials with the emulator stopped.
    // The reader never holds the file for long, so retrying inside
    // the same 40 ms window is all it takes; POSIX rename has no such problem
    // and simply succeeds on the first attempt.
    for (int attempt = 0;; ++attempt) {
        fs::rename(tmp, target, ec);
        if (!ec) return "";
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (!ec) return "";
        if (attempt >= kReplaceRetries) {
            const std::string why = ec.message();
            std::error_code cleanup;  // NOT ec: it carries the failure to report
            fs::remove(tmp, cleanup);
            return "cannot replace " + target.string() + " after " +
                   std::to_string(kReplaceRetries + 1) + " tries: " + why;
        }
        platform::sleepMs(kReplaceRetryMs);
    }
}

// ----------------------------------------------------------------- script ---

bool parseScript(const std::string& text, std::vector<Step>& out,
                 std::string& err) {
    out.clear();
    err.clear();

    // Split on newlines and ';' - a one-liner from a shell and a script file
    // are the same language.
    std::vector<std::string> cmds;
    std::string cur;
    for (char c : text) {
        if (c == '\n' || c == ';') {
            cmds.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    cmds.push_back(cur);

    State st;
    int pad = 0;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        err = "line " + std::to_string(lineNo) + ": " + why;
        out.clear();
        return false;
    };

    for (const std::string& raw : cmds) {
        ++lineNo;
        std::string line = raw;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;

        std::vector<std::string> tok;
        {
            std::istringstream is(line);
            std::string t;
            while (is >> t) tok.push_back(t);
        }
        const std::string cmd = lower(tok[0]);

        // A button list is comma-separated so "press up,cross" is one moment
        // rather than two - the difference matters for a diagonal or a combo.
        auto buttonMask = [&](const std::string& list, uint32_t& mask) {
            mask = 0;
            std::string one;
            std::istringstream is(list);
            while (std::getline(is, one, ',')) {
                const int i = buttonByName(one);
                if (i < 0) return false;
                mask |= buttonBit(i);
            }
            return mask != 0;
        };

        if (cmd == "pad") {
            if (tok.size() != 2) return fail("pad needs a connector: pad 1|2");
            if (tok[1] == "1")
                pad = 0;
            else if (tok[1] == "2")
                pad = 1;
            else
                return fail("pad must be 1 or 2, got '" + tok[1] + "'");
            continue;
        }
        if (cmd == "press" || cmd == "tap") {
            if (tok.size() < 2 || tok.size() > 3)
                return fail("press needs buttons: press cross [seconds]");
            uint32_t mask = 0;
            if (!buttonMask(tok[1], mask))
                return fail("unknown button '" + tok[1] + "'");
            double secs = 0.1;
            if (tok.size() == 3 && !parseSeconds(tok[2], secs))
                return fail("bad duration '" + tok[2] + "'");
            st.buttons[pad] |= mask;
            out.push_back({st, secs, line});
            st.buttons[pad] &= ~mask;
            out.push_back({st, 0.0, "(release " + tok[1] + ")"});
            continue;
        }
        if (cmd == "hold") {
            if (tok.size() != 2) return fail("hold needs buttons: hold up");
            uint32_t mask = 0;
            if (!buttonMask(tok[1], mask))
                return fail("unknown button '" + tok[1] + "'");
            st.buttons[pad] |= mask;
            out.push_back({st, 0.0, line});
            continue;
        }
        if (cmd == "release") {
            if (tok.size() != 2)
                return fail("release needs buttons or 'all': release up");
            if (lower(tok[1]) == "all") {
                st.buttons[pad] = 0;
            } else {
                uint32_t mask = 0;
                if (!buttonMask(tok[1], mask))
                    return fail("unknown button '" + tok[1] + "'");
                st.buttons[pad] &= ~mask;
            }
            out.push_back({st, 0.0, line});
            continue;
        }
        if (cmd == "stick") {
            if (tok.size() != 4)
                return fail("stick needs a side and two axes: stick l 0 -127");
            const std::string side = lower(tok[1]);
            const int base = (side == "l" || side == "left") ? 0
                             : (side == "r" || side == "right") ? 2
                                                                : -1;
            if (base < 0) return fail("stick side must be l or r");
            int8_t x = 0, y = 0;
            if (!parseAxis(tok[2], x) || !parseAxis(tok[3], y))
                return fail("stick axes must be numbers in -127..127");
            st.axes[pad][base + 0] = x;
            st.axes[pad][base + 1] = y;
            out.push_back({st, 0.0, line});
            continue;
        }
        if (cmd == "wait" || cmd == "sleep") {
            if (tok.size() != 2) return fail("wait needs seconds: wait 1.5");
            double secs = 0.0;
            if (!parseSeconds(tok[1], secs))
                return fail("bad duration '" + tok[1] + "'");
            out.push_back({st, secs, line});
            continue;
        }
        if (cmd == "neutral" || cmd == "center" || cmd == "centre") {
            st.clear();
            out.push_back({st, 0.0, line});
            continue;
        }
        // A bare button name is the commonest thing anyone types; treat it as a
        // tap rather than making them remember the verb.
        uint32_t mask = 0;
        if (buttonMask(tok[0], mask) && tok.size() <= 2) {
            double secs = 0.1;
            if (tok.size() == 2 && !parseSeconds(tok[1], secs))
                return fail("bad duration '" + tok[1] + "'");
            st.buttons[pad] |= mask;
            out.push_back({st, secs, line});
            st.buttons[pad] &= ~mask;
            out.push_back({st, 0.0, "(release " + tok[0] + ")"});
            continue;
        }
        return fail("unknown command '" + tok[0] + "'");
    }
    return true;
}

}  // namespace livepad

#include "logview.hpp"

#include <cctype>
#include <iterator>  // std::size over the marker tables

namespace logview {

namespace {

constexpr size_t kNpos = std::string_view::npos;

char lower(char c) { return (char)std::tolower((unsigned char)c); }

// Case-insensitive substring search. The needles below are all lower case.
size_t ifind(std::string_view hay, std::string_view needle) {
    if (needle.empty() || needle.size() > hay.size()) return kNpos;
    const size_t last = hay.size() - needle.size();
    for (size_t i = 0; i <= last; ++i) {
        size_t j = 0;
        while (j < needle.size() && lower(hay[i + j]) == needle[j]) ++j;
        if (j == needle.size()) return i;
    }
    return kNpos;
}

// The markers, matched case-insensitively anywhere in the message. The EARLIEST
// match wins (see firstOf), which is what tells "[editor] Warning: texture bake
// failed" (a warning) from "[editor] ISO export failed" (an error): both words
// appear in the first one, and the one that comes first is what the line is
// about.
//
// "====ERR:" / "==WARN:" are the engine's own prefixes (vendor/tyra
// debug.hpp) - the only markers here that are exact rather than heuristic.
constexpr std::string_view kErrorMarks[] = {
    "====err:", "error:", "] error ", "*** [", "fatal", "failed", "failure",
    "undefined reference", "ld returned", "not found", "no such file",
};
constexpr std::string_view kWarnMarks[] = {
    "==warn:", "warning", "could not", "cannot", "unable to", "deprecated",
};

size_t firstOf(std::string_view m, const std::string_view* marks, size_t n) {
    size_t best = kNpos;
    for (size_t i = 0; i < n; ++i) {
        const size_t at = ifind(m, marks[i]);
        if (at < best) best = at;
    }
    return best;
}

// Length of the runner's "HH:MM:SS " stamp (platform::logTimeStamp), 0 when the
// line carries none (the Debug window's file sources do not).
size_t stampLen(std::string_view l) {
    if (l.size() < 9) return 0;
    auto digit = [&](size_t i) { return l[i] >= '0' && l[i] <= '9'; };
    const bool stamp = digit(0) && digit(1) && l[2] == ':' && digit(3) && digit(4) &&
                       l[5] == ':' && digit(6) && digit(7) && l[8] == ' ';
    return stamp ? 9 : 0;
}

// Classifies one message (stamp already stripped), with no continuation context.
Level classifyBody(std::string_view m) {
    // The Runner's echo of a command it is about to run. Never a diagnostic -
    // and skipping the marker scan for it is what keeps a "-Werror" flag or a
    // path with "error" in it out of the error bucket.
    if (m.rfind("> ", 0) == 0) return Level::Verbose;

    // The console's own printf stream over ps2link: classify what the GAME
    // said, not the channel it arrived on.
    if (m.rfind("[ps2] ", 0) == 0) m.remove_prefix(6);
    const bool editor = m.rfind("[editor] ", 0) == 0;
    if (editor) m.remove_prefix(9);

    const size_t err = firstOf(m, kErrorMarks, std::size(kErrorMarks));
    const size_t warn = firstOf(m, kWarnMarks, std::size(kWarnMarks));
    if (err != kNpos && (warn == kNpos || err <= warn)) return Level::Error;
    if (warn != kNpos) return Level::Warning;

    // TYRA_LOG output: the engine's running commentary (every asset it loads).
    if (m.rfind("LOG: ", 0) == 0) return Level::Verbose;
    // Everything the editor itself says about a build is progress worth seeing;
    // raw tool output (compiler command lines, rsync, docker) is not.
    return editor ? Level::Info : Level::Verbose;
}

// The engine's delimited error block (TyraDebug::trap / softError). TyraX-built
// games print TYRAX; the pre-rename banner is still accepted, exactly as the
// error catcher in devkit_ui.cpp accepts it.
bool isBlockBanner(std::string_view m) {
    return m.find("  TYRAX  ") != kNpos || m.find("  TYRA  ") != kNpos;
}

// Fills ln.level / ln.cont for one raw line and advances the parse state.
void classifyLine(std::string_view raw, State& st, Line& ln) {
    std::string_view l = raw;
    if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
    const std::string_view m = l.substr(stampLen(l));

    ln.level = Level::Error;
    if (isBlockBanner(m)) {
        st.inBlock = true;
        st.prev = Level::Error;
        ln.cont = false;  // the banner IS the entry; its body continues it
        return;
    }
    if (st.inBlock) {
        // The closing rule ends the dump; its own line still belongs to it.
        if (m.rfind("====", 0) == 0) st.inBlock = false;
        st.prev = Level::Error;
        ln.cont = true;
        return;
    }

    // A blank line separates entries: it ends the one above (so the next
    // indented line cannot inherit across the gap) and is itself nothing.
    if (m.empty()) {
        st.prev = Level::Verbose;
        ln.level = Level::Verbose;
        ln.cont = false;
        return;
    }

    // A continuation of the previous entry: gcc indents the source snippet and
    // its notes, an assertion body prefixes every line with '|'. Inheriting is
    // what keeps a diagnostic readable when the verbose lines around it are
    // hidden - a lone "error:" line with its three explaining lines filtered
    // away is worse than no filter at all.
    const bool indented = m[0] == ' ' || m[0] == '\t' || m.rfind("| ", 0) == 0;
    if (indented && (st.prev == Level::Error || st.prev == Level::Warning)) {
        ln.level = st.prev;
        ln.cont = true;
        return;
    }

    st.prev = classifyBody(m);
    ln.level = st.prev;
    ln.cont = false;
}

}  // namespace

const char* label(Level l, bool plural) {
    switch (l) {
        case Level::Error: return plural ? "errors" : "error";
        case Level::Warning: return plural ? "warnings" : "warning";
        case Level::Info: return "info";
        default: return "verbose";
    }
}

size_t parse(std::string_view log, size_t from, State& st, std::vector<Line>& out) {
    size_t i = from;
    for (;;) {
        const size_t nl = log.find('\n', i);
        if (nl == kNpos) return i;  // no complete line left - resume here
        Line ln;
        ln.begin = i;
        ln.end = nl;
        if (ln.end > ln.begin && log[ln.end - 1] == '\r') --ln.end;
        classifyLine(log.substr(i, nl - i), st, ln);
        out.push_back(ln);
        i = nl + 1;
    }
}

void appendPartial(std::string_view log, size_t from, const State& st,
                   std::vector<Line>& out) {
    if (from >= log.size()) return;
    State tmp = st;
    Line ln;
    ln.begin = from;
    ln.end = log.size();
    if (ln.end > ln.begin && log[ln.end - 1] == '\r') --ln.end;
    classifyLine(log.substr(from), tmp, ln);
    out.push_back(ln);
}

std::vector<Line> parse(std::string_view log) {
    std::vector<Line> out;
    State st;
    const size_t resume = parse(log, 0, st, out);
    appendPartial(log, resume, st, out);
    return out;
}

Level classify(std::string_view line) {
    State st;
    Line ln;
    classifyLine(line, st, ln);
    return ln.level;
}

std::string join(std::string_view log, const std::vector<Line>& lines, unsigned mask) {
    std::string out;
    for (const Line& ln : lines) {
        if (!(mask & bit(ln.level))) continue;
        out.append(log.substr(ln.begin, ln.end - ln.begin));
        out.push_back('\n');
    }
    return out;
}

}  // namespace logview

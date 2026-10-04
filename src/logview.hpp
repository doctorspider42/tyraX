// Severity classification for the editor's log panels (docs/log-panels.md):
// the *Output* window (the Runner's build/run stream) and the *Debug* window
// (the game's own log.txt and PCSX2's emulog.txt).
//
// Why it is its own translation unit: the whole thing is a pure function of
// text - no ImGui, no GL, no Project - so the classification of a real build
// log can be checked from a 30-line host harness instead of by eye in a
// docked panel (the treegen/placement pattern). The panels only pick colours
// and draw.
//
// The one design rule: a diagnostic is not a line, it is a RUN of lines (a gcc
// error plus its source snippet and notes, the `|` body of a TYRAX assertion
// dump). Continuation lines inherit their entry's level, because a filter that
// keeps the "error:" line and drops the three indented lines under it hides
// exactly the part the reader needed.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace logview {

// Ordered least to most severe. The values are bit positions in the panels'
// filter mask, which is persisted in editor.ini - so they are append-only.
enum class Level { Verbose = 0, Info, Warning, Error, Count };

inline constexpr int kLevelCount = (int)Level::Count;
inline constexpr unsigned kAll = (1u << kLevelCount) - 1u;

inline unsigned bit(Level l) { return 1u << (int)l; }

/** The bucket's name as a filter chip shows it ("errors", "warnings", "info",
 * "verbose") - lower case, plural unless `plural` says otherwise. */
const char* label(Level l, bool plural = true);

/** One line of a log, as an offset range into the text it was parsed from
 * (nothing is copied - a build log is a megabyte and it is re-classified while
 * it streams). `end` excludes the newline and any carriage return. */
struct Line {
    size_t begin = 0;
    size_t end = 0;
    Level level = Level::Verbose;
    // This line CONTINUES the entry above it (a gcc source snippet or note, the
    // body of a TYRAX dump) rather than starting one of its own. It filters with
    // its entry, but a count of problems must not count it - "1 error" for a
    // diagnostic that spans four lines is the truthful number.
    bool cont = false;
};

// Carried across incremental parse() calls: a diagnostic spans several lines,
// and a chunk boundary must not break the inheritance.
struct State {
    Level prev = Level::Verbose;  // level of the entry the last line belonged to
    bool inBlock = false;         // inside a TYRAX assertion / soft-error dump
};

/** Classifies the COMPLETE lines of `log` from offset `from` on, appending them
 * to `out`, and returns the offset to resume from (one past the last newline
 * consumed). `st` carries the continuation context, so an append-only log is
 * classified once per line rather than once per frame. */
size_t parse(std::string_view log, size_t from, State& st, std::vector<Line>& out);

/** Appends the trailing PARTIAL line - the text after the last newline, i.e. a
 * line still being written, or a file that simply does not end with one - so the
 * newest output is visible immediately. Classified against a copy of `st`, which
 * is why the caller must drop this line again before its next parse() call. */
void appendPartial(std::string_view log, size_t from, const State& st,
                   std::vector<Line>& out);

/** Whole-text convenience: parse from scratch (tests, one-shot callers). */
std::vector<Line> parse(std::string_view log);

/** The level a line gets on its own, ignoring any continuation context. The
 * runner's "HH:MM:SS " stamp and its "[editor] " / "[ps2] " channel prefixes are
 * skipped first. Exposed for tests. */
Level classify(std::string_view line);

/** The kept lines, newline-joined: what "Copy" puts on the clipboard and what
 * the selectable-text mode shows. */
std::string join(std::string_view log, const std::vector<Line>& lines, unsigned mask);

}  // namespace logview
